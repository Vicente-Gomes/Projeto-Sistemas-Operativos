#include "tecnicofs_client_api.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define NOT_OPEN 1
#define OPEN 0

char client_pipe_name[MAX_PIPENAME_SIZE];
int session_id, client_pipe_fd, server_pipe_fd;


int request_handler(void *write_buffer, size_t to_write, int open_client_pipe){
    size_t bytes_written=0, bytes_read=0, to_read = sizeof(int);
    int server_answer;
    while(bytes_written < to_write){
        ssize_t written;
        if((written = write(server_pipe_fd, write_buffer + bytes_written, (to_write-bytes_written))) == -1){
            return -1;
        }
        bytes_written += (size_t)written;
    }
    if(open_client_pipe){
        if((client_pipe_fd = open(client_pipe_name, O_RDONLY)) == -1)
            return -1;
    }
    while(bytes_read < to_read){
        ssize_t current_read;
        if((current_read = read(client_pipe_fd, &server_answer + bytes_read, (to_read-bytes_read))) == -1){
            return -1;
        }
        bytes_read += (size_t)current_read;
    }
    return server_answer;
}


char opcode_convert(int opcode){
    char buffer[OPCODE_MESSAGE_SIZE], opcode_char;
    sprintf(buffer,"%d", opcode);
    opcode_char = buffer[0];
    return opcode_char;
}

int tfs_mount(char const *client_pipe_path, char const *server_pipe_path) {
    size_t message_size = sizeof(char) + sizeof(Mount_Message), pipe_size = strlen(client_pipe_path);
    Mount_Message message;
    void *write_buffer = malloc(message_size);
    char opcode = opcode_convert(TFS_OP_CODE_MOUNT);
    memcpy(client_pipe_name,client_pipe_path, pipe_size);
    memset(client_pipe_name+(pipe_size),'\0', (size_t)(MAX_PIPENAME_SIZE-pipe_size));
    memcpy(message.client_pipe, client_pipe_name, MAX_PIPENAME_SIZE);
    memcpy(write_buffer, &opcode, sizeof(char));
    memcpy(write_buffer+sizeof(char), &message, sizeof(Mount_Message));

    unlink(client_pipe_path);


    if((server_pipe_fd = open(server_pipe_path, O_WRONLY)) < 0)
        return -1;

    if(mkfifo(client_pipe_path, 0777) < 0)
        return -1;

    if((session_id = request_handler(write_buffer, message_size, NOT_OPEN)) == -1){
        return -1;
    }
    
    return 0;
}

int tfs_unmount() {
    int fail; 
    size_t message_size = sizeof(char) + sizeof(Unmount_Message);
    Unmount_Message message;
    char op_code = opcode_convert(TFS_OP_CODE_UNMOUNT);
    void *write_buffer = malloc(message_size);
    memcpy(write_buffer, &op_code, sizeof(char));
    message.session_id = session_id;
    memcpy(write_buffer+sizeof(char), &message, sizeof(Unmount_Message));
    if((fail = request_handler(write_buffer, message_size, OPEN)) == -1){
        return -1;
    }
    close(client_pipe_fd);
    close(server_pipe_fd);
    unlink(client_pipe_name);
    return 0;
}

int tfs_open(char const *name, int flags) {
    int fhandle;
    size_t message_size = sizeof(char) + sizeof(Open_Message);
    Open_Message message;
    char op_code = opcode_convert(TFS_OP_CODE_OPEN);
    void *write_buffer = malloc(message_size);
    memcpy(write_buffer, &op_code, sizeof(char));
    message.session_id = session_id;
    strcpy(message.name, name);
    message.flags = flags;
    memcpy(write_buffer+sizeof(char), &message, sizeof(Open_Message));
    if((fhandle = request_handler(write_buffer, message_size, OPEN)) == -1){
        return -1;
    }
    return fhandle;
}

int tfs_close(int fhandle) { 
    size_t message_size = sizeof(char) + sizeof(Close_Message);
    Close_Message message;
    char op_code = opcode_convert(TFS_OP_CODE_CLOSE);
    void *write_buffer = malloc(message_size);
    memcpy(write_buffer, &op_code, sizeof(char));
    message.session_id = session_id;
    message.fhandle = fhandle;
    memcpy(write_buffer+sizeof(char), &message, sizeof(Close_Message));
    if((fhandle = request_handler(write_buffer, message_size, OPEN)) == -1){
        return -1;
    }
    return 0;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t len) {
    size_t message_size = sizeof(char) + sizeof(Write_Message) + len;
    ssize_t bytes_written;
    Write_Message message;
    char op_code = opcode_convert(TFS_OP_CODE_WRITE);
    void *write_buffer = malloc(message_size);
    memcpy(write_buffer, &op_code, sizeof(char));
    message.session_id = session_id;
    message.len = len;
    message.fhandle = fhandle;
    memcpy(write_buffer+sizeof(char), &message, sizeof(Write_Message));
    memcpy(write_buffer+sizeof(char)+sizeof(Write_Message),buffer,len);
    if((bytes_written = request_handler(write_buffer, message_size, OPEN)) == -1){
        return -1;
    }

    return bytes_written;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    size_t message_size = sizeof(char) + sizeof(Read_Message), bytes_written=0, bytes_read=0;
    int file_bytes_read = 0;
    Read_Message message;
    char op_code = opcode_convert(TFS_OP_CODE_READ);
    void *write_buffer = malloc(message_size);
    memcpy(write_buffer, &op_code, sizeof(char));
    message.session_id = session_id;
    message.fhandle = fhandle; 
    message.len = len;
    memcpy(write_buffer+sizeof(char), &message, sizeof(Read_Message));
    while(bytes_written < message_size){
        ssize_t written;
        if((written = write(server_pipe_fd, write_buffer + bytes_written, (size_t)(message_size-bytes_written))) == -1){
            return -1;
        }
        bytes_written += (size_t)written;
    }
    while(bytes_read < sizeof(int)){
        ssize_t current_read;
        if((current_read = read(client_pipe_fd, (&file_bytes_read) + bytes_read, sizeof(int)-bytes_read)) == -1){
            close(client_pipe_fd);
            return -1;
        }
        bytes_read += (size_t)current_read;
    }
    if(file_bytes_read == -1){
        return -1;
    }
    bytes_read=0;
    while(bytes_read < file_bytes_read){
        ssize_t current_read;
        if((current_read = read(client_pipe_fd, buffer + bytes_read, (size_t)file_bytes_read-bytes_read)) == -1){
            return -1;
        }
        bytes_read += (size_t)current_read;
    }

    return file_bytes_read;
}

int tfs_shutdown_after_all_closed() {
    int success; 
    size_t message_size = sizeof(char) + sizeof(Shutdown_Message);
    Shutdown_Message message;
    char op_code = opcode_convert(TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED);
    void *write_buffer = malloc(message_size);
    memcpy(write_buffer, &op_code, sizeof(char));
    message.session_id = session_id;
    memcpy(write_buffer+sizeof(char), &message, sizeof(Shutdown_Message));
    if((success = request_handler(write_buffer, message_size, OPEN)) == -1){
        return -1;
    }
    if(close(client_pipe_fd) == -1)
        return -1;
    if(close(server_pipe_fd) == -1)
        return -1;
    unlink(client_pipe_name);
    return 0;
}
