#include <stdio.h>
#include "fs/operations.h"
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define THREADS 2
#define SIZE 3072

char *path = "/f1";
int fd;

void* write_function(void* arg){
    fd = tfs_open(path, TFS_O_CREAT);
    assert(fd != -1);
    char input[SIZE]; 
    char m = *((char*)arg); 
    memset(input, m, SIZE);
    assert(tfs_write(fd, input, SIZE) == SIZE);
    assert(tfs_close(fd) != -1);
    return NULL;
}

// Duas threads com file handles diferentes tentam escrever no mesmo inode

int main() {
    pthread_t tid[THREADS];
    assert(tfs_init() != -1);
    char input_check_A[SIZE*THREADS];
    memset(input_check_A, 'A', SIZE);
    char input_check_B[SIZE];
    memset(input_check_B, 'B', SIZE);
    char output[SIZE];
    int arg[THREADS];

    for(int i = 0; i < THREADS; i++){
        arg[i] = 'A'+i;
    }

    for (int i=0; i<THREADS; i++){
        assert(pthread_create(&tid[i], NULL, write_function, (void*)&arg[i])!=-1);
    }


    for (int i = 0; i < THREADS; i++){
    pthread_join(tid[i], NULL);
    }

   tfs_copy_to_external_fs(path, "out2");

    fd = tfs_open(path, 0);
    assert(fd != -1 );

 
    assert(tfs_read(fd, output, THREADS*SIZE) == SIZE);
    if(output[0] == 'A')
        assert(memcmp(input_check_A, output,SIZE) == 0);
    else if(output[0] == 'B')
        assert(memcmp(input_check_B, output,SIZE) == 0);
    assert(tfs_close(fd) != -1);
    printf("Successful test\n");
    return 0;
}