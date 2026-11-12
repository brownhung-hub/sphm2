#include "sp.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <poll.h>
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
  int is_sp_main =0;
  int read_fd = STDIN_FILENO;
  int reply_fd = STDOUT_FILENO;
  char *cmd_path = getenv("CMDPIPENAME");
  char *reply_path = getenv("REPLYPIPENAME");
  char *consultant_path = getenv("JUDGE_CONSULTANT");
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
  }
  while (1) {
    struct pollfd pfds[33];
    int num=0;
    pfds[0].fd=read_fd;
    pfds[0].events=POLLIN;
    int judge_index = 0;
    num++;
    fetchcommand fetchtable[32];
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
}

int kill_handler(char *buf, size_t len) {
  // TODO: Implement the KILL command handler.
}

int update_handler(char *buf, size_t len) {
  // TODO: Implement the UPDATE command handler.
}
