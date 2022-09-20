#include <stdio.h>
#include "fs/operations.h"
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define THREADS 3
#define SIZE 3072

char *path = "/f1";
int fd;

void* write_function(void* arg){
    char input[SIZE];
    char m = *((char*)arg); 
    memset(input, m, SIZE);
    tfs_write(fd, input, SIZE);
    return NULL;
}

// Tres threads tentam escrever no mesmo inode com o mesmo file handle

int main() {
    pthread_t tid[THREADS];
    assert(tfs_init() != -1);
    fd = tfs_open(path, TFS_O_CREAT);
    assert(fd != -1);
    char input_check_A[SIZE];
    memset(input_check_A, 'A', SIZE);
    char input_check_B[SIZE];
    memset(input_check_B, 'B', SIZE); 
    char input_check_C[SIZE];
    memset(input_check_C, 'C', SIZE);
    char output [THREADS*SIZE];
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

    assert(tfs_close(fd) != -1);

    fd = tfs_open(path, 0);
    assert(fd != -1 );

    tfs_copy_to_external_fs(path, "out");
    assert(tfs_read(fd, output, THREADS*SIZE) == THREADS*SIZE);
    for(int i=0; i < THREADS; i++){
        if(output[i*SIZE] == 'A'){
            assert(memcmp(input_check_A, output + (SIZE * i) ,SIZE) == 0);
        }
        else if(output[i*SIZE] == 'B'){
            assert(memcmp(input_check_B, output + (SIZE * i), SIZE) == 0);
        }
        else if(output[i*SIZE] == 'C'){
            assert(memcmp(input_check_C, output + (SIZE * i), SIZE) == 0);
        }
    }
    assert(tfs_close(fd) != -1);
    printf("Successful test\n");
    return 0;
}