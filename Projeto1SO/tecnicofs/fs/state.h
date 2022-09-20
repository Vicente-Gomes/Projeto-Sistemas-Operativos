#ifndef STATE_H
#define STATE_H

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#define BLOCKS 11
#define INDIRECT_ACCESS_BLOCK 10
#define INDIRECT_REFERENCES (BLOCK_SIZE/sizeof(int))


/*
 * Directory entry
 */
typedef struct {
    char d_name[MAX_FILE_NAME];
    int d_inumber;
} dir_entry_t;

typedef enum { T_FILE, T_DIRECTORY } inode_type;

pthread_mutexattr_t Attr;

pthread_mutex_t inode_table_lock;
/*
 * I-node
 */
typedef struct {
    inode_type i_node_type;
    size_t i_size;
    int i_data_block[11];
    pthread_rwlock_t rwl;

    /* in a real FS, more fields would exist here */
} inode_t;

pthread_mutex_t block_table_lock;

typedef enum { FREE = 0, TAKEN = 1 } allocation_state_t;

pthread_mutex_t open_file_table_lock;
/*
 * Open file entry (in open file table)
 */
typedef struct {
    int of_inumber;
    size_t of_offset;
    pthread_mutex_t file_lock;
} open_file_entry_t;

#define MAX_DIR_ENTRIES (BLOCK_SIZE / sizeof(dir_entry_t))

void state_init();
void state_destroy();

int inode_create(inode_type n_type);
int inode_delete(int inumber);
inode_t *inode_get(int inumber);

int clear_dir_entry(int inumber, int sub_inumber);
int add_dir_entry(int inumber, int sub_inumber, char const *sub_name);
int find_in_dir(int inumber, char const *sub_name);

int data_block_alloc();
int data_block_free(int block_number);
void *data_block_get(int block_number);

int add_to_open_file_table(int inumber, size_t offset);
int remove_from_open_file_table(int fhandle);
open_file_entry_t *get_open_file_entry(int fhandle);

size_t get_current_offset(open_file_entry_t *file);
void *get_current_data_block(inode_t *inode, open_file_entry_t *file);
int bonus_block_init(inode_t *inode);

#endif // STATE_H
