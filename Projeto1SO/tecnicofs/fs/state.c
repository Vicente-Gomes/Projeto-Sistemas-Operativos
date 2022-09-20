#include "state.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

/* Persistent FS state  (in reality, it should be maintained in secondary
 * memory; for simplicity, this project maintains it in primary memory) */

/* I-node table */
static inode_t inode_table[INODE_TABLE_SIZE];
static char freeinode_ts[INODE_TABLE_SIZE];

/* Data blocks */
static char fs_data[BLOCK_SIZE * DATA_BLOCKS];
static char free_blocks[DATA_BLOCKS];

/* Volatile FS state */

static open_file_entry_t open_file_table[MAX_OPEN_FILES];
static char free_open_file_entries[MAX_OPEN_FILES];

static inline bool valid_inumber(int inumber) {
    return inumber >= 0 && inumber < INODE_TABLE_SIZE;
}

static inline bool valid_block_number(int block_number) {
    return block_number >= 0 && block_number < DATA_BLOCKS;
}

static inline bool valid_file_handle(int file_handle) {
    return file_handle >= 0 && file_handle < MAX_OPEN_FILES;
}

/**
 * We need to defeat the optimizer for the insert_delay() function.
 * Under optimization, the empty loop would be completely optimized away.
 * This function tells the compiler that the assembly code being run (which is
 * none) might potentially change *all memory in the process*.
 *
 * This prevents the optimizer from optimizing this code away, because it does
 * not know what it does and it may have side effects.
 *
 * Reference with more information: https://youtu.be/nXaxk27zwlk?t=2775
 *
 * Exercise: try removing this function and look at the assembly generated to
 * compare.
 */
static void touch_all_memory() { __asm volatile("" : : : "memory"); }

/*
 * Auxiliary function to insert a delay.
 * Used in accesses to persistent FS state as a way of emulating access
 * latencies as if such data structures were really stored in secondary memory.
 */
static void insert_delay() {
    for (int i = 0; i < DELAY; i++) {
        touch_all_memory();
    }
}

/*
 * Initializes FS state
 */
void state_init() {

    pthread_mutexattr_init(&Attr);
    pthread_mutexattr_settype(&Attr, PTHREAD_MUTEX_RECURSIVE);
    

    pthread_mutex_init(&inode_table_lock, &Attr);
    for (size_t i = 0; i < INODE_TABLE_SIZE; i++) {
        pthread_rwlock_init(&inode_table[i].rwl, NULL);
        freeinode_ts[i] = FREE;
    }

    pthread_mutex_init(&block_table_lock, NULL);

    for (size_t i = 0; i < DATA_BLOCKS; i++) {
        free_blocks[i] = FREE;
    }

    pthread_mutex_init(&open_file_table_lock, &Attr);

    for (size_t i = 0; i < MAX_OPEN_FILES; i++) {
        pthread_mutex_init(&open_file_table[i].file_lock,&Attr);
        free_open_file_entries[i] = FREE;
    }
}

void state_destroy() { /* nothing to do */
        pthread_mutex_destroy(&inode_table_lock);
        pthread_mutex_destroy(&open_file_table_lock);
        pthread_mutex_destroy(&block_table_lock);
        for (int i = 0; i < INODE_TABLE_SIZE; i++){
            pthread_rwlock_destroy(&inode_table[i].rwl);
        }
        for(int i = 0; i < MAX_OPEN_FILES; i++){
            pthread_mutex_destroy(&open_file_table[i].file_lock);
        }
        
}

/*
 * Creates a new i-node in the i-node table.
 * Input:
 *  - n_type: the type of the node (file or directory)
 * Returns:
 *  new i-node's number if successfully created, -1 otherwise
 */
int inode_create(inode_type n_type) {
    pthread_mutex_lock(&inode_table_lock);
    for (int inumber = 0; inumber < INODE_TABLE_SIZE; inumber++) {
        if ((inumber * (int) sizeof(allocation_state_t)) == 0) {
            insert_delay(); // simulate storage access delay (to freeinode_ts)
        }

        /* Finds first free entry in i-node table */
        if (freeinode_ts[inumber] == FREE) {
            /* Found a free entry, so takes it for the new i-node*/
            freeinode_ts[inumber] = TAKEN;
            insert_delay(); // simulate storage access delay (to i-node)
            inode_table[inumber].i_node_type = n_type;

            if (n_type == T_DIRECTORY) {
                /* Initializes directory (filling its block with empty
                 * entries, labeled with inumber==-1) */
                int b = data_block_alloc();
                if (b == -1) {
                    freeinode_ts[inumber] = FREE;
                    pthread_mutex_unlock(&inode_table_lock);
                    return -1;
                }
                inode_table[inumber].i_size = BLOCK_SIZE;
                inode_table[inumber].i_data_block[0] = b;
                dir_entry_t *dir_entry = (dir_entry_t *)data_block_get(b);
                if (dir_entry == NULL) {
                    freeinode_ts[inumber] = FREE;
                    pthread_mutex_unlock(&inode_table_lock);
                    return -1;
                }

                for (size_t i = 0; i < MAX_DIR_ENTRIES; i++) {
                    dir_entry[i].d_inumber = -1;
                }
            } else {
                /* In case of a new file, simply sets its size to 0 */
                inode_table[inumber].i_size = 0;
                for(int i=0; i<BLOCKS;i++)
                    inode_table[inumber].i_data_block[i] = -1;
            }
            pthread_mutex_unlock(&inode_table_lock);
            return inumber;
        }
    }
    return -1;
}




/*
 * Deletes the i-node.
 * Input:
 *  - inumber: i-node's number
 * Returns: 0 if successful, -1 if failed
 */
int inode_delete(int inumber) {
    // simulate storage access delay (to i-node and freeinode_ts)
    insert_delay();
    insert_delay();

    pthread_mutex_lock(&inode_table_lock);

    if (!valid_inumber(inumber) || freeinode_ts[inumber] == FREE) {
        pthread_mutex_unlock(&inode_table_lock);
        return -1;
    }

    freeinode_ts[inumber] = FREE;
    if(inode_table[inumber].i_node_type == T_DIRECTORY){
        if(inode_table[inumber].i_data_block[0] == -1){
            if (data_block_free(inode_table[inumber].i_data_block[0]) == -1){
                pthread_mutex_unlock(&inode_table_lock);
                return -1;
            }
            inode_table[inumber].i_data_block[0] = -1;
        }
    }
    else{
        if (inode_table[inumber].i_size > 0) {
            for(int i=0; i<INDIRECT_ACCESS_BLOCK && inode_table[inumber].i_data_block[i] != -1; i++){
                if (data_block_free(inode_table[inumber].i_data_block[i]) == -1){
                    pthread_mutex_unlock(&inode_table_lock);
                    return -1;
                }
                inode_table[inumber].i_data_block[i] = -1;
            }
            if(inode_table[inumber].i_data_block[INDIRECT_ACCESS_BLOCK] != -1){
                int last_block = inode_table[inumber].i_data_block[INDIRECT_ACCESS_BLOCK];
                int *bonus_block = data_block_get(last_block);
                if(bonus_block==NULL){
                    pthread_mutex_unlock(&inode_table_lock);
                    return -1;
                }
                for(int j=0; bonus_block[j] != -1 && j<INDIRECT_REFERENCES; j++){
                    if(data_block_free(bonus_block[j]) == -1){
                        pthread_mutex_unlock(&inode_table_lock);
                        return -1;
                    }
                    bonus_block[j]= -1;
                }
                if(data_block_free(last_block) == -1){
                    pthread_mutex_unlock(&inode_table_lock);
                    return -1;
                }
                last_block = -1;
            }
        }
    }

    /* TODO: handle non-empty directories (either return error, or recursively
     * delete children */
    pthread_mutex_unlock(&inode_table_lock);
    return 0;
}

/*
 * Returns a pointer to an existing i-node.
 * Input:
 *  - inumber: identifier of the i-node
 * Returns: pointer if successful, NULL if failed
 */
inode_t *inode_get(int inumber) {
    if (!valid_inumber(inumber)) {
        return NULL;
    }

    insert_delay(); // simulate storage access delay to i-node
    return &inode_table[inumber];
}

/*
 * Adds an entry to the i-node directory data.
 * Input:
 *  - inumber: identifier of the i-node
 *  - sub_inumber: identifier of the sub i-node entry
 *  - sub_name: name of the sub i-node entry
 * Returns: SUCCESS or FAIL
 */
int add_dir_entry(int inumber, int sub_inumber, char const *sub_name) {
    pthread_mutex_lock(&inode_table_lock);

    if (!valid_inumber(inumber) || !valid_inumber(sub_inumber)) {
        pthread_mutex_unlock(&inode_table_lock);
        return -1;
    }

    insert_delay(); // simulate storage access delay to i-node with inumber
    if (inode_table[inumber].i_node_type != T_DIRECTORY) {
        pthread_mutex_unlock(&inode_table_lock);
        return -1;
    }

    if (strlen(sub_name) == 0) {
        pthread_mutex_unlock(&inode_table_lock);
        return -1;
    }

    /* Locates the block containing the directory's entries */
    dir_entry_t *dir_entry =
        (dir_entry_t *)data_block_get(inode_table[inumber].i_data_block[0]);
    if (dir_entry == NULL) {
        pthread_mutex_unlock(&inode_table_lock);
        return -1;
    }

    /* Finds and fills the first empty entry */
    for (size_t i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (dir_entry[i].d_inumber == -1) {
            dir_entry[i].d_inumber = sub_inumber;
            strncpy(dir_entry[i].d_name, sub_name, MAX_FILE_NAME - 1);
            dir_entry[i].d_name[MAX_FILE_NAME - 1] = 0;
            pthread_mutex_unlock(&inode_table_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&inode_table_lock);
    return -1;
}

/* Looks for a given name inside a directory
 * Input:
 * 	- parent directory's i-node number
 * 	- name to search
 * 	Returns i-number linked to the target name, -1 if not found
 */
int find_in_dir(int inumber, char const *sub_name) {
    insert_delay(); // simulate storage access delay to i-node with inumber
    pthread_mutex_lock(&inode_table_lock);
    if (!valid_inumber(inumber) ||
        inode_table[inumber].i_node_type != T_DIRECTORY) {
        pthread_mutex_unlock(&inode_table_lock);
        return -1;
    }
    /* Locates the block containing the directory's entries */
    dir_entry_t *dir_entry =
        (dir_entry_t *)data_block_get(inode_table[inumber].i_data_block[0]);
    if (dir_entry == NULL) {
        pthread_mutex_unlock(&inode_table_lock);
        return -1;
    }

    /* Iterates over the directory entries looking for one that has the target
     * name */
    for (int i = 0; i < MAX_DIR_ENTRIES; i++){
        if ((dir_entry[i].d_inumber != -1) &&
            (strncmp(dir_entry[i].d_name, sub_name, MAX_FILE_NAME) == 0)) {
            pthread_mutex_unlock(&inode_table_lock);
            return dir_entry[i].d_inumber;
        }
    }
    pthread_mutex_unlock(&inode_table_lock);
    return -1;
}

/*
 * Allocated a new data block
 * Returns: block index if successful, -1 otherwise
 */
int data_block_alloc() {
    pthread_mutex_lock(&block_table_lock);
    for (int i = 0; i < DATA_BLOCKS; i++) {
        if (i * (int) sizeof(allocation_state_t) % BLOCK_SIZE == 0) {
            insert_delay(); // simulate storage access delay to free_blocks
        }

        if (free_blocks[i] == FREE) {
            free_blocks[i] = TAKEN;
            pthread_mutex_unlock(&block_table_lock);
            return i;
        }
    }
    pthread_mutex_unlock(&block_table_lock);
    return -1;
}

/* Frees a data block
 * Input
 * 	- the block index
 * Returns: 0 if success, -1 otherwise
 */
int data_block_free(int block_number) {
    pthread_mutex_lock(&block_table_lock);
    if (!valid_block_number(block_number)) {
        pthread_mutex_unlock(&block_table_lock);
        return -1;
    }

    insert_delay(); // simulate storage access delay to free_blocks
    free_blocks[block_number] = FREE;
    pthread_mutex_unlock(&block_table_lock);
    return 0;
}

/* Returns a pointer to the contents of a given block
 * Input:
 * 	- Block's index
 * Returns: pointer to the first byte of the block, NULL otherwise
 */
void *data_block_get(int block_number) {
    if (!valid_block_number(block_number)) {
        return NULL;
    }

    insert_delay(); // simulate storage access delay to block
    return &fs_data[block_number * BLOCK_SIZE];
}

/* Add new entry to the open file table
 * Inputs:
 * 	- I-node number of the file to open
 * 	- Initial offset
 * Returns: file handle if successful, -1 otherwise
 */
int add_to_open_file_table(int inumber, size_t offset) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        pthread_mutex_lock(&open_file_table[i].file_lock);
        if (free_open_file_entries[i] == FREE) {
            free_open_file_entries[i] = TAKEN;
            open_file_table[i].of_inumber = inumber;
            open_file_table[i].of_offset = offset;
            pthread_mutex_unlock(&open_file_table[i].file_lock);
            return i;
        }
        pthread_mutex_unlock(&open_file_table[i].file_lock);
    }
    return -1;
}

/* Frees an entry from the open file table
 * Inputs:
 * 	- file handle to free/close
 * Returns 0 is success, -1 otherwise
 */
int remove_from_open_file_table(int fhandle) {
    pthread_mutex_lock(&open_file_table_lock);
    if (!valid_file_handle(fhandle) || free_open_file_entries[fhandle] != TAKEN) {
        pthread_mutex_unlock(&open_file_table_lock);
        return -1;
    }
    free_open_file_entries[fhandle] = FREE;
    pthread_mutex_unlock(&open_file_table_lock);
    return 0;
}

/* Returns pointer to a given entry in the open file table
 * Inputs:
 * 	 - file handle
 * Returns: pointer to the entry if sucessful, NULL otherwise
 */
open_file_entry_t *get_open_file_entry(int fhandle) {
    if (!valid_file_handle(fhandle)) {
        return NULL;
    }
    return &open_file_table[fhandle];
}


size_t get_current_offset(open_file_entry_t *file){
    size_t current_block = (file->of_offset / (size_t)BLOCK_SIZE);
    if(current_block < INDIRECT_ACCESS_BLOCK){
        return (size_t)file->of_offset % BLOCK_SIZE;
    } else{
        return (size_t)(file->of_offset - (10*BLOCK_SIZE)) % BLOCK_SIZE;
    }
}


void *get_current_data_block(inode_t *inode, open_file_entry_t *file){
        size_t current_block = (file->of_offset / (size_t)BLOCK_SIZE);
        if(current_block < INDIRECT_ACCESS_BLOCK){
            if(inode->i_data_block[current_block] == -1){
                if((inode->i_data_block[current_block] = data_block_alloc()) == -1){
                    return NULL;
                }
            }
            return data_block_get(inode->i_data_block[current_block]);
        }
        else{
                if(inode->i_data_block[INDIRECT_ACCESS_BLOCK] == -1){
                    if((inode->i_data_block[INDIRECT_ACCESS_BLOCK] = bonus_block_init(inode)) == -1){
                        return NULL;
                    }
                }
            int *indirect_block = data_block_get(inode->i_data_block[INDIRECT_ACCESS_BLOCK]);
            current_block -= INDIRECT_ACCESS_BLOCK;
            if(indirect_block[current_block] == -1){
                if((indirect_block[current_block] = data_block_alloc()) == -1){
                    return NULL;
                }
            }
            return data_block_get(indirect_block[current_block]);
        }
}

int bonus_block_init(inode_t *inode){
    
    int last_block = inode->i_data_block[INDIRECT_ACCESS_BLOCK] = data_block_alloc();
    int *bonus_block = data_block_get(last_block);
    for(int i=0; i<INDIRECT_REFERENCES;i++){
        bonus_block[i] = -1;
    }
    return last_block;
}

