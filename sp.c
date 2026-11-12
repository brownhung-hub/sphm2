#include "sp.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
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

  while (1) {
    char *command_buffer = NULL;
    Command command = {.buf = command_buffer, .len = 0, .type = UNKNOWN};
    size_t size =0;
    FILE *read_stream = (read_fd == STDIN_FILENO)? stdin: fdopen(read_fd,"r");
    if(getline(&command_buffer,&size,read_stream)==-1){
      free(command_buffer);
      break;
    }
    
    // TODO: poll the STDIO/FIFO for commands.

    // end TODO

    // TODO: poll parent, and child processes for custom commands.

    // end TODO

    // TODO: classify the command and execute it.
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
  pid_t pid = fork();
  if(pid == 0){
    setenv("CMDPIPENAME",command_fifo,1);
    setenv("REPLYPIPENAME",reply_fifo,1);
    char *args[]= {"./sp", NULL};
    execvp("./sp",args);
  }else if(pid ==1){
    return 0;
  }else {
    return -1;
  }
}

int store_handler(char *buf, size_t len) {
  // TODO: Implement the STORE command handler.
}

int delete_handler(char *buf, size_t len) {
  // TODO: Implement the DELETE command handler.
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
