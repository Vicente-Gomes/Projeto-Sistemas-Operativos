#include "operations.h"
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

/*
 * Session entry
 */
typedef struct {
    int id;
    int client_fd;
    void *thread_buffer;
    pthread_t t_id;
    pthread_mutex_t lock;
    pthread_cond_t active_request;
} session_entry;


/* Session Ids */
pthread_mutex_t table_lock;
static session_entry session_table[MAX_CLIENTS];
static char freesession_entries[MAX_CLIENTS];
int server_pipe_fd;

int close_all_pipes(int fd1){
    for(int i=0; i<MAX_CLIENTS; i++){
        if(freesession_entries[i]==FREE){
            if(close(session_table[i].client_fd) == -1)
                return -1;
        }
    }
    if(close(fd1) == -1)
        return -1;
    return 0;
}


int worker_mount(int s_id){
    if((write(session_table[s_id].client_fd, &s_id, sizeof(int))) == -1){
        return -1;
    }
    return 0;
}

int worker_unmount(void *buffer){
    int outcome = 0;
    Unmount_Message message;
    memcpy(&message, buffer + sizeof(int), sizeof(Unmount_Message));
    pthread_mutex_lock(&table_lock);
    if(freesession_entries[message.session_id] == FREE)
        outcome = -1;
    else
        freesession_entries[message.session_id] = FREE;
    int fdc = session_table[message.session_id].client_fd;
    if((write(fdc, &outcome, sizeof(int))) == -1){
        return -1;
    }
    close(fdc);
    pthread_mutex_unlock(&table_lock);
    return 0;
}

int worker_open(void *buffer){
    int outcome = 0;
    Open_Message message;
    memcpy(&message, buffer + sizeof(int), sizeof(Open_Message));
    outcome = tfs_open(message.name, message.flags);
    int fdc = session_table[message.session_id].client_fd;
    if((write(fdc, &outcome, sizeof(int))) == -1){
        return -1;
    }
    return 0;
}

int worker_close(void *buffer){
    int outcome;
    Close_Message message;
    memcpy(&message, buffer + sizeof(int), sizeof(Close_Message));
    outcome = tfs_close(message.fhandle);
    int fdc = session_table[message.session_id].client_fd;
    if((write(fdc, &outcome, sizeof(int))) == -1){
        return -1;
    }
    return 0;
}

int worker_write(void *buffer){
    ssize_t bytes_written;
    size_t length;
    Write_Message message;
    memcpy(&message, buffer + sizeof(int), sizeof(Write_Message));
    length = message.len;
    char content[length];
    memcpy(&content, buffer+sizeof(int)+sizeof(Write_Message), length);
    bytes_written = tfs_write(message.fhandle,content,length);
    
    int fdc = session_table[message.session_id].client_fd;
    if((write(fdc, &bytes_written, sizeof(int))) == -1){
        return -1;
    }
    return 0;
}

int worker_read(void *buffer){
    size_t len,message_size;
    ssize_t current,bytes_file;
    int bytes_file_read;
    Read_Message message;
    memcpy(&message, buffer + sizeof(int), sizeof(Read_Message));
    len = message.len;
    char content[len];
    bytes_file = tfs_read(message.fhandle,&content,len);
    bytes_file_read = (int)bytes_file;

    int fdc = session_table[message.session_id].client_fd;

    if(bytes_file == -1){
        if((current = write(fdc, &bytes_file_read, sizeof(int))) == -1){
            return -1;
        }
        return 0;
    }

    message_size = sizeof(int) + (size_t)bytes_file;
    void *message_read = malloc(message_size);
    memcpy(message_read,&bytes_file_read, sizeof(int));
    memcpy(message_read+sizeof(int), content, (size_t)bytes_file);


    if((current = write(fdc, message_read, message_size)) == -1){
        return -1;
    }

    return 0;
}

int worker_shutdown(void *buffer){
    int outcome, fdc;
    Shutdown_Message message;
    memcpy(&message, buffer + sizeof(int), sizeof(Shutdown_Message));
    outcome = tfs_destroy_after_all_closed();

    fdc = session_table[message.session_id].client_fd;
    if((write(fdc, &outcome, sizeof(int))) == -1){
        return -1;
    }
    close_all_pipes(server_pipe_fd);
    exit(0);
}

void* thread_func(void *session_id){
    int s_id = *((int*)session_id), opcode;

    while(1){
        pthread_mutex_lock(&session_table[s_id].lock);
        while(session_table[s_id].thread_buffer == NULL){
            pthread_cond_wait(&session_table[s_id].active_request, &session_table[s_id].lock);
        }
        pthread_mutex_unlock(&session_table[s_id].lock);
        memcpy(&opcode, session_table[s_id].thread_buffer,sizeof(int));
        switch(opcode){
            case(TFS_OP_CODE_MOUNT):
                worker_mount(s_id);
                break;

            case(TFS_OP_CODE_UNMOUNT):
                worker_unmount(session_table[s_id].thread_buffer);
                break;

            case(TFS_OP_CODE_OPEN):
                worker_open(session_table[s_id].thread_buffer);
                break;
            
            case(TFS_OP_CODE_CLOSE):
                worker_close(session_table[s_id].thread_buffer);
                break;

            case(TFS_OP_CODE_WRITE):
                worker_write(session_table[s_id].thread_buffer);
                break;
            
            case(TFS_OP_CODE_READ):
                worker_read(session_table[s_id].thread_buffer);
                break;

            case(TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED):
                break;

            default :
                break;
        }
        free(session_table[s_id].thread_buffer);
        session_table[s_id].thread_buffer = NULL;
    }
    return NULL;
}


int tfs_mount(int fds){
    size_t bytes_read=0;
    ssize_t current;
    int fdc, opcode = TFS_OP_CODE_MOUNT;
    Mount_Message message;
    while(bytes_read < sizeof(Mount_Message)){
        if((current = read(fds, &message + bytes_read, (size_t)(sizeof(Mount_Message)-bytes_read))) == -1){
            return -1;
        }
        bytes_read += (size_t)current;
    }
    
    if((fdc = open(message.client_pipe, O_WRONLY)) < 0){
        return -1;
    }
    
    int valid_session = 0;
    pthread_mutex_lock(&table_lock);
    for(int i=0; i<MAX_CLIENTS; i++){
        if(freesession_entries[i]==FREE){
            freesession_entries[i]=TAKEN;
            session_table[i].thread_buffer = malloc(sizeof(int) + sizeof(Mount_Message));
            session_table[i].client_fd = fdc;
            memcpy(session_table[i].thread_buffer, &opcode, sizeof(int));
            memcpy(session_table[i].thread_buffer + sizeof(int), &message, sizeof(Mount_Message));
            pthread_cond_signal(&session_table[i].active_request);    
            valid_session=1;
            break;
        }
    }
    pthread_mutex_unlock(&table_lock);
    if(valid_session == 0){
        int fail = -1;
        if(write(fdc, &fail, sizeof(int)) == -1){
            return -1;
        }
    }

    return 0;
}
    
int tfs_unmount(int fds){
    size_t bytes_read=0;
    ssize_t current;
    int opcode = TFS_OP_CODE_UNMOUNT;
    Unmount_Message message;
    
    while(bytes_read < sizeof(Unmount_Message)){
        if((current = read(fds, &message + bytes_read, (size_t)(sizeof(Unmount_Message)-bytes_read))) == -1){
            return -1;
        }
        bytes_read += (size_t)current;
    }
    session_table[message.session_id].thread_buffer = malloc(sizeof(int) + sizeof(Unmount_Message));
    memcpy(session_table[message.session_id].thread_buffer, &opcode, sizeof(int));
    memcpy(session_table[message.session_id].thread_buffer + sizeof(int), &message, sizeof(Unmount_Message));
    pthread_cond_signal(&session_table[message.session_id].active_request);
    return 0;
}

int tfs_open_server(int fds){
    size_t bytes_read=0;
    ssize_t current;
    int opcode = TFS_OP_CODE_OPEN;
    Open_Message message;
    while(bytes_read < sizeof(Open_Message)){
        if((current = read(fds, &message + bytes_read, (size_t)(sizeof(Open_Message)-bytes_read))) == -1){
            return -1;
        }
        bytes_read += (size_t)current;
    }
    session_table[message.session_id].thread_buffer = malloc(sizeof(int) + sizeof(Open_Message));
    memcpy(session_table[message.session_id].thread_buffer, &opcode, sizeof(int));
    memcpy(session_table[message.session_id].thread_buffer + sizeof(int), &message, sizeof(Open_Message));
    pthread_cond_signal(&session_table[message.session_id].active_request);
    return 0;
}

int tfs_close_server(int fds){
    size_t bytes_read=0;
    ssize_t current;
    int opcode = TFS_OP_CODE_CLOSE;
    Close_Message message;
    while(bytes_read < sizeof(Close_Message)){
        if((current = read(fds, &message + bytes_read, (size_t)(sizeof(Close_Message)-bytes_read))) == -1){
            return -1;
        }
        bytes_read += (size_t)current;
    }
    session_table[message.session_id].thread_buffer = malloc(sizeof(int) + sizeof(Close_Message));
    memcpy(session_table[message.session_id].thread_buffer, &opcode, sizeof(int));
    memcpy(session_table[message.session_id].thread_buffer + sizeof(int), &message, sizeof(Close_Message));
    pthread_cond_signal(&session_table[message.session_id].active_request);
    return 0;
}

int tfs_write_server(int fds){
    size_t bytes_read=0,len;
    ssize_t current;
    int opcode = TFS_OP_CODE_WRITE;
    Write_Message message;
    while(bytes_read < sizeof(Write_Message)){
        if((current = read(fds, &message + bytes_read, (size_t)(sizeof(Write_Message)-bytes_read))) == -1){
            return -1;
        }
        bytes_read += (size_t)current;
    }
    bytes_read=0;
    len = message.len;
    char content[len];
    while(bytes_read < len){
        if ((current = read(fds, content + bytes_read, (size_t)(len - bytes_read))) == -1){
            return -1;
        }
        bytes_read += (size_t)current;
    }
    session_table[message.session_id].thread_buffer = malloc(sizeof(int) + sizeof(Write_Message) + len);
    memcpy(session_table[message.session_id].thread_buffer, &opcode, sizeof(int));
    memcpy(session_table[message.session_id].thread_buffer + sizeof(int), &message, sizeof(Write_Message));
    memcpy(session_table[message.session_id].thread_buffer + sizeof(int) + sizeof(Write_Message), content, len);
    pthread_cond_signal(&session_table[message.session_id].active_request);
    return 0;
}

int tfs_read_server(int fds){
    size_t bytes_read=0;
    ssize_t current;
    int opcode = TFS_OP_CODE_READ;
    Read_Message message;
    while(bytes_read < sizeof(Read_Message)){
        if((current = read(fds, &message + bytes_read, (size_t)(sizeof(Read_Message)-bytes_read))) == -1){
            return -1;
        }
        bytes_read += (size_t)current;
    }
    
    session_table[message.session_id].thread_buffer = malloc(sizeof(int) + sizeof(Read_Message));
    memcpy(session_table[message.session_id].thread_buffer, &opcode, sizeof(int));
    memcpy(session_table[message.session_id].thread_buffer + sizeof(int), &message, sizeof(Read_Message));
    pthread_cond_signal(&session_table[message.session_id].active_request);
    
    return 0;
}

int tfs_shutdown_server(int fds){
    int opcode = TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED;
    size_t bytes_read=0;
    ssize_t current;
    Shutdown_Message message;
    while(bytes_read < sizeof(Shutdown_Message)){
        if((current = read(fds, &message + bytes_read, (size_t)(sizeof(Shutdown_Message)-bytes_read))) == -1){
            return -1;
        }
        bytes_read += (size_t)current;
    }
    
    session_table[message.session_id].thread_buffer = malloc(sizeof(int) + sizeof(Shutdown_Message));
    memcpy(session_table[message.session_id].thread_buffer, &opcode, sizeof(int));
    memcpy(session_table[message.session_id].thread_buffer + sizeof(int), &message, sizeof(Shutdown_Message));
    pthread_cond_signal(&session_table[message.session_id].active_request);
    return 0;
}


int main(int argc, char **argv) {
    int fd1, op_code;
    char buffer[OPCODE_MESSAGE_SIZE];
    buffer[OPCODE_MESSAGE_SIZE-1] = '\0';
    if(pthread_mutex_init(&table_lock, NULL) == -1)
        return -1;
    
    for(int i = 0; i<MAX_CLIENTS; i++){
        session_entry new_session;
        new_session.thread_buffer = NULL;
        new_session.id = i;
        session_table[i] = new_session;
        if(pthread_cond_init(&session_table[i].active_request, NULL) != 0 || pthread_mutex_init(&session_table[i].lock, NULL) != 0)
            return -1;
        if(pthread_create(&session_table[i].t_id,NULL, thread_func, (void*)(&(session_table[i].id))) == -1){
            return -1;
        }
        freesession_entries[i] = FREE;
    }

    if (argc < 2) {
        printf("Please specify the pathname of the server's pipe.\n");
        return 1;
    }

    char *pipename = argv[1];
    printf("Starting TecnicoFS server with pipe called %s\n", pipename);
    unlink(pipename);
    mkfifo(pipename, 0777);
    if((fd1 = open(pipename, O_RDONLY)) < 0)
        return -1;

    if(tfs_init()==-1){
        return -1;
    }

    while(1){
        ssize_t current;
        memset(buffer, '\0', sizeof(char));
            if((current = read(fd1, buffer, sizeof(char))) == -1){
                return -1;
            }

        op_code = atoi(buffer);

        switch(op_code){
            case(TFS_OP_CODE_MOUNT):
                if(tfs_mount(fd1) == -1){
                    close_all_pipes(fd1);
                    return -1;
                }
                break;

            case(TFS_OP_CODE_UNMOUNT):
                if(tfs_unmount(fd1) == -1){
                    close_all_pipes(fd1);
                    return -1;
                }
                break;

            case(TFS_OP_CODE_OPEN):
                if(tfs_open_server(fd1) == -1){
                    close_all_pipes(fd1);
                    return -1;
                }
                break;
            
            case(TFS_OP_CODE_CLOSE):
                if(tfs_close_server(fd1) == -1){
                    close_all_pipes(fd1);
                    return -1;
                }
                break;

            case(TFS_OP_CODE_WRITE):
                if(tfs_write_server(fd1) == -1){
                    close_all_pipes(fd1);
                    return -1;
                }
                break;
            
            case(TFS_OP_CODE_READ):
                if(tfs_read_server(fd1) == -1){
                    close_all_pipes(fd1);
                    return -1;
                }
                break;

            case(TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED):
                if(tfs_shutdown_server(fd1) == -1){
                    close_all_pipes(fd1);
                    return -1;
                }
                break;

            default :
                break;
        }
    }
    return 0;
}
