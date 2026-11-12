#include "sp.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <poll.h>


int currentfd = -1;
int parent_write_fd =-1;
int parent_read_fd =-1;
int is_sp_main = 0;
char *consultant_path = NULL;
typedef struct NODE{
    char *key;
    char *value;
    struct NODE *next;
} NODE;
NODE *head = NULL;

typedef struct fetchcommand{
  char hash[17];
  int replyfd;
  int is_active;
}fetchcommand;
fetchcommand fetchtable[32];
typedef struct {
    int read_fd;   
    int write_fd;  
} ChildNode;

ChildNode children[32];
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
  char *cmd_path = getenv("CMDPIPENAME");
  char *reply_path = getenv("REPLYPIPENAME");
  consultant_path = getenv("JUDGE_CONSULTANT");
  parent_read_fd=atoi(getenv("PARENT_read_FD"));
  parent_write_fd=atoi(getenv("PARENT_WRITE_FD"));
  if(cmd_path!= NULL && reply_path !=NULL){
    mkfifo(cmd_path,0666);
    mkfifo(reply_path,0666);
    read_fd = open(cmd_path, O_RDONLY | O_CLOEXEC);
    reply_fd = open(reply_path,O_WRONLY | O_CLOEXEC);
  }else{
    is_sp_main = 1;
  }
  // end TODO

  for(int i = 0; i < 32; i++) {
      children[i].read_fd = -1;
      children[i].write_fd = -1;
      fetchtable[i].replyfd = -1;
  }
   
  while (1) {
    struct pollfd pfds[33];
    int num=0;
    pfds[0].fd=read_fd;
    pfds[0].events=POLLIN;
    int judge_index = 0;
    num++;
    char *command_buffer = NULL;
    Command command = {.buf = command_buffer, .len = 0, .type = UNKNOWN};
    size_t size =0;

    for (int i = 0; i < 32; i++) {
        if (children[i].read_fd != -1) {
            pfds[num].fd = children[i].read_fd;
            pfds[num].events = POLLIN;
            num++;
        }
    }
    // TODO: poll the STDIO/FIFO for commands.
    int ready = poll(pfds,num,-1);
    for(int i=0;i<num;i++){
      if(pfds[i].revents&POLLIN){
        char buffer[4096];
        memset(buffer,0,sizeof(buffer));
        int bytes = read(pfds[i].fd,buffer,sizeof(buffer)-1);
        if(bytes > 0){
          command_buffer = malloc(strlen(buffer));
          strcpy(command_buffer,buffer);
          currentfd = pfds[i].fd;
          break;
        }else if (bytes==0){
          close(pfds[i].fd);
          if (i == judge_index) {
              return 0;
          } else {
              for (int j = 0; j < 32; j++) {
                  if (children[j].read_fd == pfds[i].fd) {
                      close(children[j].write_fd); // 寫入端也一起關掉
                      children[j].read_fd = -1;
                      children[j].write_fd = -1;
                      break;
                  }
              }
          }
        }
      }
    }
    // end TODO

    // TODO: poll parent, and child processes for custom commands.

    // end TODO

    // TODO: classify the command and execute it.
    if(command_buffer!=NULL){
      char *action = strtok(command_buffer, " \n");
      if(action!=NULL){
        int is_reply = 0;
        for (int i = 0; i < 32; i++) {
            if (fetchtable[i].replyfd != -1 && strcmp(fetchtable[i].hash, action) == 0) {
                is_reply = 1;
                char *value = strtok(NULL, "\n");
                if (value != NULL) {
                    char result[4096];
                    snprintf(result, sizeof(result), "%s %s\n", action, value);
                    write(fetchtable[i].replyfd, result, strlen(result));
                    
                    fetchtable[i].replyfd = -1; 
                }
                break;
            }
        }
        if (is_reply) {
          free(command_buffer);
          continue; 
        }
        if(strcmp(action,"create")==0){
          command.type= CREATE;
        }else if(strcmp(action,"store")==0){
          command.type= STORE;
        }else if(strcmp(action,"delete")==0){
          command.type= DELETE;
        }else if(strcmp(action,"fetch")==0){
          command.type= FETCH;
        }else if(strcmp(action,"kill")==0){
          command.type= KILL;
        }else if(strcmp(action,"update")==0){
          command.type= UPDATE;
        }else{
          command.type= UNKNOWN;
        }
      }
      char *args = strtok(NULL, "\n");
      if(args!=NULL){
        command.buf = args;
        command.len = strlen(args);
      }
      command_handler(&command);
    }
    
    // end TODO
    
    // TODO: handle custom commands.

    // end TODO
    free(command_buffer);
  }
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
    children[slot].read_fd = c2pfd[0];
    children[slot].write_fd = p2cfd[1];
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
  char *ptr = strchr(buf,'\n');
  ptr[0]= '\0';
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
      write(currentfd,result,strlen(result));
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
      write(currentfd, result, strlen(result)); // 直接回傳給問問題的那個 fd
      return 0;

  } else {
      for(int i=0;i<32;i++){
        if(fetchtable[i].replyfd==-1){
          fetchtable[i].replyfd=currentfd;
          strcpy(fetchtable[i].hash,buf);
          char result[4096];
          snprintf(result, sizeof(result), "fetch %s %s\n ",buf,&ptr[1]);
          write(parent_write_fd,result,strlen(result));
          break;
        }
      }
  }
  return 0;
}

int kill_handler(char *buf, size_t len) {
  // TODO: Implement the KILL command handler.
}

int update_handler(char *buf, size_t len) {
  // TODO: Implement the UPDATE command handler.
}
