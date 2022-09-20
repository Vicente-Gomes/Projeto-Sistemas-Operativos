#include <stdio.h>
#include "fs/operations.h"
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define THREADS 2
#define NUM_FILES 19


int open[THREADS];


void* open_function1(){
    char *path_name = "/f20";
    open[0]= tfs_open(path_name, TFS_O_CREAT);
    return NULL;
}

void* open_function2(){
    char *path_name = "/f21";
    open[1] = tfs_open(path_name, TFS_O_CREAT);
    return NULL;
}


int main() {
    pthread_t tid[THREADS];
    char file_names[NUM_FILES][4] = {"/f1", "/f2", "/f3", "/f4", "/f5", "/f6", "/f7", "/f8", "/f9", "/f10", "/f11", 
    "/f12", "/f13", "/f14", "/f15", "/f16", "/f17", "/f18", "/f19"};
    assert(tfs_init() != -1);
    for(int i = 0; i < NUM_FILES; i++){
        int fd = tfs_open(file_names[i], TFS_O_CREAT);
        assert(fd != -1);
    }

    assert(pthread_create(&tid[0], NULL, open_function1, NULL) != -1);
    assert(pthread_create(&tid[1], NULL, open_function2, NULL) != -1);

    for (int k = 0; k < THREADS; k++){
        pthread_join(tid[k], NULL);
    }
    
    int successful_allocations = 0;
    int failed_allocations = 0;
    for(int i = 0; i < THREADS; i++){
        if(open[i] == 19)
            successful_allocations++;
        else if(open[i] == -1)
            failed_allocations++;
    }
    assert(successful_allocations == 1 && failed_allocations == THREADS-1);

    printf("Succesful test\n");
}