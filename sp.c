#include "sp.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <poll.h>
#include <errno.h>
#include <sys/wait.h>

int current_read_fd = -1;
int current_reply_fd = -1;
int parent_write_fd =-1;
int parent_read_fd =-1;
int is_sp_main = 0;
char *consultant_path = NULL;
typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} LineBuffer;

typedef struct {
    int read_fd;   
    int write_fd;
    pid_t pid;
    LineBuffer buf;  
} ChildNode;
ChildNode children[32];
typedef struct NODE{
    char *key;
    char *value;
    struct NODE *next;
} NODE;
NODE *head = NULL;


int buffer_append(LineBuffer *b, const char *src, size_t n) {

    size_t need = b->len + n + 1;

    if (need > b->capacity) {
        size_t newcap = b->capacity ? b->capacity : 4096;

        while (newcap < need) {
            newcap *= 2;
        }

        char *temp = realloc(b->data, newcap);

        if (temp == NULL)
            return -1;

        b->data = temp;
        b->capacity = newcap;
    }

    memcpy(b->data + b->len, src, n);

    b->len += n;
    b->data[b->len] = '\0';

    return 0;
}

char *buffer_pop_line(LineBuffer *b) {

    if (b->len == 0)
        return NULL;

    char *newline = memchr(b->data, '\n', b->len);

    if (newline == NULL)
        return NULL;

    size_t line_len = newline - b->data;

    char *line = malloc(line_len + 1);

    if (line == NULL)
        return NULL;

    memcpy(line, b->data, line_len);
    line[line_len] = '\0';

    size_t consumed = line_len + 1;

    memmove(
        b->data,
        b->data + consumed,
        b->len - consumed
    );

    b->len -= consumed;
    b->data[b->len] = '\0';

    return line;
}
typedef struct fetchcommand{
  char hash[17];
  int replyfd;
  int is_active;
}fetchcommand;
fetchcommand fetchtable[32];

void process_command_line(char *line, int r_fd, int rep_fd) {
    Command command = {.buf = NULL, .len = 0, .type = UNKNOWN};

    char *space = strchr(line, ' ');
    if (space != NULL) {
        *space = '\0';
        command.buf = space + 1;
        command.len = strlen(command.buf);
    }

    if (strcmp(line, "create") == 0) {
        command.type = CREATE;
    } else if (strcmp(line, "store") == 0) {
        command.type = STORE;
    } else if (strcmp(line, "delete") == 0) {
        command.type = DELETE;
    } else if (strcmp(line, "fetch") == 0) {
        command.type = FETCH;
    } else if (strcmp(line, "kill") == 0) {
        command.type = KILL;
    } else if (strcmp(line, "update") == 0) {
        command.type = UPDATE;
    }
    
    current_read_fd = r_fd;
    current_reply_fd = rep_fd;

    command_handler(&command);
}

void process_parent_reply_line(char *line) {
    char *space = strchr(line, ' ');
    if (space == NULL) return;

    size_t hash_len = space - line;
    char hash[17];
    if (hash_len >= sizeof(hash)) return;
    memcpy(hash, line, hash_len);
    hash[hash_len] = '\0';

    // 尋找當初是誰發起這個 hash 查詢的
    for (int i = 0; i < 32; i++) {
        if (fetchtable[i].replyfd != -1 && strcmp(fetchtable[i].hash, hash) == 0) {
            int target_fd = fetchtable[i].replyfd;
            fetchtable[i].replyfd = -1; 

            // 轉發給目標 (Judge 或下一層的 Child)
            char result[4096];
            snprintf(result, sizeof(result), "%s\n", line);
            write(target_fd, result, strlen(result));
            break;
        }
    }
}

int main(int argc, char *argv[]) {
  if (argc > 1) {
    fprintf(stderr, "Usage: ./sp\n");
    return 1;
  }

  // TODO: Determine whether the current process is SP main.
  //       After that, you may want to make the following code the same for both
  //       SP main and SP child processes. Therefore, you will set some
  //       variables here.
  
  int read_fd = STDIN_FILENO;
  int reply_fd = STDOUT_FILENO;

  char *pr = getenv("PARENT_READ_FD");
  char *pw = getenv("PARENT_WRITE_FD");
  consultant_path = getenv("JUDGE_CONSULTANT");
  if (pr != NULL && pw != NULL) {
    parent_read_fd = atoi(pr);
    parent_write_fd = atoi(pw);
    int flags = fcntl(parent_read_fd, F_GETFD);
    if (flags != -1) {
        fcntl(parent_read_fd, F_SETFD, flags | FD_CLOEXEC);
    }
    flags = fcntl(parent_write_fd, F_GETFD);
    if (flags != -1) {
        fcntl(parent_write_fd, F_SETFD, flags | FD_CLOEXEC);
    }
    unsetenv("PARENT_READ_FD");
    unsetenv("PARENT_WRITE_FD");
    is_sp_main = 0;
  } else if (pr == NULL && pw == NULL) {
    is_sp_main = 1;
    parent_read_fd = -1;
    parent_write_fd = -1;
  } else {
      fprintf(stderr, "Invalid parent FD environment\n");
      return 1;
  }

  char *cmd_path = getenv("CMDPIPENAME");
  char *reply_path = getenv("REPLYPIPENAME");
  if(cmd_path!= NULL && reply_path !=NULL){
    mkfifo(cmd_path,0666);
    mkfifo(reply_path,0666);
    read_fd = open(cmd_path, O_RDONLY | O_CLOEXEC);
    reply_fd = open(reply_path,O_WRONLY | O_CLOEXEC);
  }
  // end TODO

  for(int i = 0; i < 32; i++) {
      children[i].read_fd = -1;
      children[i].write_fd = -1;
      fetchtable[i].replyfd = -1;
      children[i].pid = -1;
      memset(&children[i].buf, 0, sizeof(LineBuffer));
  }

  LineBuffer judge_buffer = {0};
  LineBuffer parent_buffer = {0}; 

  while (1) {
    struct pollfd pfds[34]; // 0: Judge, 1: Parent, 2~33: Children

   
    pfds[0].fd = read_fd;
    pfds[0].events = POLLIN | POLLHUP;
    pfds[0].revents = 0;
    pfds[1].fd = parent_read_fd;
    pfds[1].events = POLLIN | POLLHUP;
    pfds[1].revents = 0;

    // 2 ~ 33: 監聽所有存活的 Children
    for (int i = 0; i < 32; i++) {
        pfds[i + 2].fd = children[i].read_fd;
        pfds[i + 2].events = POLLIN | POLLHUP;
        pfds[i + 2].revents = 0;
    }

    int ready = poll(pfds, 34, -1);

    if (ready < 0) {
        if (errno == EINTR)
            continue;
        perror("poll");
        break;
    }
    // 1. 處理 Judge 通道 (pfds[0])
    if (pfds[0].revents & (POLLIN | POLLHUP)) {
        char chunk[4096];
        ssize_t bytes = read(read_fd, chunk, sizeof(chunk));
        if (bytes > 0) {
            if (buffer_append(&judge_buffer, chunk, bytes) == 0) {
                char *line;
                while ((line = buffer_pop_line(&judge_buffer)) != NULL) {
                    process_command_line(line, read_fd, reply_fd);
                    free(line);
                }
            }
        } else if (bytes == 0) {
            kill_handler(NULL,0);
        }
    }

    // 2. 處理 Parent 通道 (pfds[1])
    if (parent_read_fd != -1 && (pfds[1].revents & (POLLIN | POLLHUP))) {
        char chunk[4096];
        ssize_t bytes = read(parent_read_fd, chunk, sizeof(chunk));
        if (bytes > 0) {
            if (buffer_append(&parent_buffer, chunk, bytes) == 0) {
                char *line;
                while ((line = buffer_pop_line(&parent_buffer)) != NULL) {
                    process_parent_reply_line(line);
                    free(line);
                }
            }
        } else if (bytes == 0) {
            close(parent_read_fd);
            parent_read_fd = -1;
            kill_handler(NULL, 0);
        }
    }

    // 3. 處理所有 Children 通道 (pfds[2] ~ pfds[33])
    for (int i = 0; i < 32; i++) {
        if (children[i].read_fd != -1 && (pfds[i + 2].revents & (POLLIN | POLLHUP))) {
            char chunk[4096];
            ssize_t bytes = read(children[i].read_fd, chunk, sizeof(chunk));
            if (bytes > 0) {
                if (buffer_append(&children[i].buf, chunk, bytes) == 0) {
                    char *line;
                    while ((line = buffer_pop_line(&children[i].buf)) != NULL) {
                        process_command_line(line, children[i].read_fd, children[i].write_fd);
                        free(line);
                    }
                }
            } else if (bytes == 0) {
                // Child 管線關閉，立即收屍避免 Zombie[cite: 13]
                close(children[i].read_fd);
                close(children[i].write_fd);
                if (children[i].pid > 0) {
                    waitpid(children[i].pid, NULL, 0); 
                }
                free(children[i].buf.data);
                memset(&children[i].buf, 0, sizeof(LineBuffer));
                children[i].read_fd = -1;
                children[i].write_fd = -1;
                children[i].pid = -1;
            }
        }
    }
  }
  
    // TODO: poll the STDIO/FIFO for commands.
   
    // end TODO

    // TODO: poll parent, and child processes for custom commands.

    // end TODO

    // TODO: classify the command and execute it.
    // end TODO
    
    // TODO: handle custom commands.

    // end TODO
    
  
  free(judge_buffer.data);
}



int command_handler(Command *cmd) {
  switch (cmd->type) {
    case CREATE:
      create_handler(cmd->buf, cmd->len);
      break;
    case STORE:
      store_handler(cmd->buf, cmd->len);
      break;
    case DELETE:
      delete_handler(cmd->buf, cmd->len);
      break;
    case FETCH:
      fetch_handler(cmd->buf, cmd->len);
      break;
    case KILL:
      kill_handler(cmd->buf, cmd->len);
      break;
    case UPDATE:
      update_handler(cmd->buf, cmd->len);
      break;
    default:
      fprintf(stderr, "Unknown command type\n");
      return -1;
  }
  return 0;
}

int create_handler(char *buf, size_t len) {
  // TODO: Implement the CREATE command handler.
  char *command_fifo = strtok(buf, " \n");
  char *reply_fifo = strtok(NULL,"\n");
  if(command_fifo==NULL||reply_fifo==NULL)
    return -1;
  int slot = -1;
  for (int i = 0; i < 32; i++) {
      if (children[i].read_fd == -1) {
          slot = i;
          break;
      }
  }
  int p2cfd[2];
  int c2pfd[2];
  if(pipe(p2cfd)==-1||pipe(c2pfd)==-1)
    return -1;
  pid_t pid = fork();
  if(pid == 0){
    close(p2cfd[1]);
    close(c2pfd[0]);
    char p2c_str[16], c2p_str[16];
    sprintf(p2c_str, "%d", p2cfd[0]); 
    sprintf(c2p_str, "%d", c2pfd[1]);
    setenv("PARENT_READ_FD", p2c_str, 1);  
    setenv("PARENT_WRITE_FD", c2p_str, 1); 
    setenv("CMDPIPENAME",command_fifo,1);
    setenv("REPLYPIPENAME",reply_fifo,1);
    char *args[]= {"./sp", NULL};
    execvp("./sp",args);
  }else if(pid > 0){
    close(p2cfd[0]); 
    close(c2pfd[1]);
    fcntl(c2pfd[0], F_SETFD, FD_CLOEXEC);
    fcntl(p2cfd[1], F_SETFD, FD_CLOEXEC);
    children[slot].read_fd = c2pfd[0];
    children[slot].write_fd = p2cfd[1];
    children[slot].pid = pid;
    return 0;
  }else {
    return -1;
  }
}

int store_handler(char *buf, size_t len) {
  // TODO: Implement the STORE command handler.
  char *ptr = strchr(buf,' ');
  ptr[0]= '\0';
  NODE *cur = head;
  while(cur!=NULL){
    if(strcmp(cur->key,buf)==0){
      char *nvalue = malloc(strlen(&ptr[1])+1);
      strcpy(nvalue,&ptr[1]);
      free(cur->value);
      cur->value = nvalue;
      return 0;
    }
    cur = cur->next;
  }
  
  NODE *newnode = (NODE *)malloc(sizeof(NODE));
  char *nkey = malloc(strlen(buf)+1);
  strcpy(nkey,buf);
  char *nvalue = malloc(strlen(&ptr[1])+1);
  strcpy(nvalue,&ptr[1]);
  newnode->key= nkey;
  newnode->value = nvalue;
  newnode->next = NULL; 

  if(head == NULL){
    head = newnode;
  }else{
    NODE *cur = head;
    while(cur->next!=NULL){
      cur = cur->next;
    }
    cur->next = newnode;
  }
  
  return 0;

}

int delete_handler(char *buf, size_t len) {
  // TODO: Implement the DELETE command handler.
  NODE *cur = head;
  if(head == NULL)
    return 0;
  if(strcmp(cur->key,buf)==0){
    NODE *delete = cur;
    head = cur->next;
    free(delete->key);
    free(delete->value);
    free(delete);
    return 0;
  }
  while(cur->next!=NULL){
    if(strcmp(cur->next->key,buf)==0){
      NODE *next = cur->next;
      cur->next = cur->next->next;
      free(next->key);
      free(next->value);
      free(next);
      return 0;
    }
    cur = cur->next;
  }
  return 0;
}

int fetch_handler(char *buf, size_t len) {
  // TODO: Implement the FETCH command handler.
  char *ptr = strchr(buf,' ');
  ptr[0]= '\0';
  
  NODE *cur = head;
  while(cur!=NULL){
    if(strcmp(cur->key,&ptr[1])==0){
      char result[4096];
      snprintf(result, sizeof(result), "%s %s\n",buf,cur->value);
      write(current_reply_fd,result,strlen(result));
      return 0;
    }
    cur = cur->next;
  }

  if (is_sp_main == 1) {
    char ans_value[4096];
    while (1) {
      int to_c[2], from_c[2]; // to_c: SP寫給顧問, from_c: 顧問寫給SP
      if (pipe(to_c) == -1 || pipe(from_c) == -1) return -1;
      
      pid_t cpid = fork();
      if (cpid == 0) {
          // 這裡是 Consultant 子行程
          // SP main calls fork() and exec() on the consultant
          dup2(to_c[0], STDIN_FILENO);   // 把 STDIN 接到 to_c 的讀取端
          dup2(from_c[1], STDOUT_FILENO);// 把 STDOUT 接到 from_c 的寫入端
          close(to_c[0]); close(to_c[1]);
          close(from_c[0]); close(from_c[1]);
          
          char *c_args[] = {consultant_path, NULL};
          execv(consultant_path, c_args);
          exit(1); 
      }
      
      close(to_c[0]); // 關閉用不到的讀取端
      close(from_c[1]); // 關閉用不到的寫入端
      
      char key_buf[4096];
      snprintf(key_buf, sizeof(key_buf), "%s\n", &ptr[1]);
      write(to_c[1], key_buf, strlen(key_buf));
      close(to_c[1]); 
      
      char c_reply[4096];
      memset(c_reply, 0, sizeof(c_reply));
      read(from_c[0], c_reply, sizeof(c_reply) - 1);
      close(from_c[0]);
      
      waitpid(cpid, NULL, 0); 
      if (strncmp(c_reply, "Ok: ", 4) == 0) {
          // 成功了！把 v 抽出來
          char *v_start = c_reply + 4;
          char *newline = strchr(v_start, '\n');
          if (newline) *newline = '\0'; 
          strcpy(ans_value, v_start);
          break; 
      }
    }
    char result[4096];
    snprintf(result, sizeof(result), "%s %s\n", buf, ans_value);
    write(current_reply_fd, result, strlen(result)); // 直接回傳給問問題的那個 fd
    return 0;

  } else {
      for(int i=0;i<32;i++){
        if(fetchtable[i].replyfd==-1){
          fetchtable[i].replyfd=current_reply_fd;
          strcpy(fetchtable[i].hash,buf);
          char result[4096];
          snprintf(result, sizeof(result), "fetch %s %s\n",buf,&ptr[1]);
          write(parent_write_fd,result,strlen(result));
          break;
        }
      }
  }
  return 0;
}

int kill_handler(char *buf, size_t len) {
  // TODO: Implement the KILL command handler.
    for (int i = 0; i < 32; i++) {
        if (children[i].pid > 0) {
            close(children[i].write_fd); 
            waitpid(children[i].pid, NULL, 0); 
            close(children[i].read_fd);
            if (children[i].buf.data) {
                free(children[i].buf.data);
            }
            children[i].pid = -1;
            children[i].read_fd = -1;
            children[i].write_fd = -1;
        }
    }

    // 2. 自己的 FIFO 
    char *cmd_path = getenv("CMDPIPENAME");
    char *reply_path = getenv("REPLYPIPENAME");
    if (cmd_path != NULL && reply_path != NULL) {
        unlink(cmd_path);   
        unlink(reply_path); 
    }

    exit(0);
    return 0;
}

int update_handler(char *buf, size_t len) {
  // TODO: Implement the UPDATE command handler.
  struct stat st;
  if (stat(buf, &st) == 0 && S_ISREG(st.st_mode) && access(buf, X_OK) == 0) {
      static char *dynamic_consultant = NULL;
      if (dynamic_consultant != NULL) {
          free(dynamic_consultant); 
      }
      dynamic_consultant = strdup(buf);
      consultant_path = dynamic_consultant; 
  }
  return 0;
}
