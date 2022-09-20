#include <stdio.h>
#include "fs/operations.h"
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define THREADS 3
#define SIZE 15000

char *path = "/f1";
int fd;
int bytes_read = 0;

void* read_function(){
    char input[SIZE];
    bytes_read += (int)tfs_read(fd, input, SIZE);
    return NULL;
}

int main() {
    pthread_t tid[THREADS];
    assert(tfs_init() != -1);
    char string_to_write[SIZE];
    memset(string_to_write, 'A', SIZE);
    fd = tfs_open(path, TFS_O_CREAT);
    tfs_write(fd, string_to_write, SIZE);
    assert(tfs_close(fd)!=-1);
    fd = tfs_open(path, 0);
    for (int i=0; i<THREADS; i++){
        assert(pthread_create(&tid[i], NULL, read_function, NULL)!=-1);
    }
    for (int i=0; i<THREADS; i++){
        pthread_join(tid[i], NULL);
    }
    printf("%d \n", bytes_read);
    assert(bytes_read == SIZE);
    assert(tfs_close(fd) != -1);
    printf("Successful test\n");
    return 0;
}