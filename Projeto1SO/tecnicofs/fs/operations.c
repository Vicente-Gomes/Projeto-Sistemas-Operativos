#include "operations.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

int tfs_init() {
    state_init();

    /* create root inode */
    int root = inode_create(T_DIRECTORY);
    if (root != ROOT_DIR_INUM) {
        return -1;
    }

    return 0;
}

int tfs_destroy() {
    state_destroy();
    return 0;
}

static bool valid_pathname(char const *name) {
    return name != NULL && strlen(name) > 1 && name[0] == '/';
}


int tfs_lookup(char const *name) {
    if (!valid_pathname(name)) {
        return -1;
    }

    // skip the initial '/' character
    name++;
    
    return find_in_dir(ROOT_DIR_INUM, name);
}

int tfs_open(char const *name, int flags) {
    int inum;
    size_t offset;

    /* Checks if the path name is valid */
    if (!valid_pathname(name)) {
        return -1;
    }
    pthread_mutex_lock(&inode_table_lock);
    inum = tfs_lookup(name);
    if (inum >= 0) {
        /* The file already exists */
        inode_t *inode = inode_get(inum);
        if (inode == NULL) {
            pthread_mutex_unlock(&inode_table_lock);
            return -1;
        }
        pthread_rwlock_wrlock(&inode->rwl);
        pthread_mutex_unlock(&inode_table_lock);

        /* Trucate (if requested) */
        if (flags & TFS_O_TRUNC) {

            if (inode->i_size > 0) {
                if(inode->i_node_type == T_DIRECTORY){
                    if(inode->i_data_block[0] == -1 && data_block_free(inode->i_data_block[0]) == -1){
                        pthread_rwlock_unlock(&inode->rwl);
                        return -1;
                    }
                }
                else{
                    for(int i=0; i < INDIRECT_ACCESS_BLOCK && inode->i_data_block[i] != -1 ; i++){
                        if (data_block_free(inode->i_data_block[i]) == -1) {
                            pthread_rwlock_unlock(&inode->rwl);
                            return -1;
                        }
                        inode->i_data_block[i] = -1;
                    }
                    if(inode->i_data_block[INDIRECT_ACCESS_BLOCK] != -1){
                        int last_block = inode->i_data_block[INDIRECT_ACCESS_BLOCK];
                        int *bonus_block = data_block_get(last_block);
                        if(bonus_block==NULL){
                            pthread_rwlock_unlock(&inode->rwl);
                            return -1;
                        }
                        for(int j=0; j<INDIRECT_REFERENCES && bonus_block[j] != -1; j++){
                            if(data_block_free(bonus_block[j]) == -1){
                                pthread_rwlock_unlock(&inode->rwl);
                                return -1;}
                            bonus_block[j] = -1;
                        }
                        if(data_block_free(last_block) == -1){
                            pthread_rwlock_unlock(&inode->rwl);
                            return -1;
                        }
                        last_block = -1;
                    }
                }
                inode->i_size = 0;
            }
            pthread_rwlock_unlock(&inode->rwl);
        }
        /* Determine initial offset */
        if (flags & TFS_O_APPEND) {
            offset = inode->i_size;
        } else {
            offset = 0;
        }
        pthread_rwlock_unlock(&inode->rwl);
    }
     else if (flags & TFS_O_CREAT) {
        pthread_mutex_unlock(&inode_table_lock);
        /* The file doesn't exist; the flags specify that it should be created*/
        /* Create inode */
        inum = inode_create(T_FILE);
        if (inum == -1) {
            return -1;
        }
        /* Add entry in the root directory */
        if (add_dir_entry(ROOT_DIR_INUM, inum, name + 1) == -1) {
            inode_delete(inum);
            return -1;
        }
        offset = 0;
    } else {
        pthread_mutex_unlock(&inode_table_lock);
        return -1;
    }

    /* Finally, add entry to the open file table and
     * return the corresponding handle */
    return add_to_open_file_table(inum, offset);

    /* Note: for simplification, if file was created with TFS_O_CREAT and there
     * is an error adding an entry to the open file table, the file is not
     * opened but it remains created */
}


int tfs_close(int fhandle) { return remove_from_open_file_table(fhandle); }

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {
    size_t bytes_to_write, current_offset, bytes_written=0;
    void *block;

    pthread_mutex_lock(&open_file_table_lock);
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        pthread_mutex_unlock(&open_file_table_lock);
        return -1;
    }
    pthread_mutex_lock(&file->file_lock);
    pthread_mutex_unlock(&open_file_table_lock);

    pthread_mutex_lock(&inode_table_lock);
    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        pthread_mutex_unlock(&inode_table_lock);
        pthread_mutex_unlock(&file->file_lock);
        return -1;
    }
    pthread_rwlock_wrlock(&inode->rwl);
    pthread_mutex_unlock(&inode_table_lock);

    size_t inicial_offset = file->of_offset;
    size_t inicial_size = inode->i_size;
    while(to_write > 0){
        block = get_current_data_block(inode,file);
        if(block == NULL){
            pthread_rwlock_unlock(&inode->rwl);
            pthread_mutex_unlock(&file->file_lock);
            return -1;
        }
        current_offset = get_current_offset(file);

        if(to_write + current_offset <= BLOCK_SIZE){
            bytes_to_write = to_write;
        }
        else bytes_to_write = (size_t)(BLOCK_SIZE - current_offset);
        memcpy(block + current_offset, buffer, bytes_to_write);
        file->of_offset+=bytes_to_write;
        buffer += bytes_to_write;
        to_write -= bytes_to_write;
        inode->i_size += bytes_to_write;
        bytes_written += bytes_to_write;
    }
        if(inicial_offset != inicial_size){
            if(bytes_written + inicial_offset > inicial_size)
                inode->i_size = bytes_written + inicial_offset;
            else inode->i_size = inicial_size;
        }
    pthread_mutex_unlock(&file->file_lock);
    pthread_rwlock_unlock(&inode->rwl);
    return (ssize_t)bytes_written;   
}


int tfs_copy_to_external_fs(char const *source_path, char const *dest_path){
    int fhandle;
    FILE *dest;
    ssize_t bytes_read = BUFFER_SIZE;
    pthread_mutex_lock(&open_file_table_lock);
    fhandle = tfs_open(source_path, 0);

    if(fhandle == -1){
        pthread_mutex_unlock(&open_file_table_lock);
        return -1;
    }

    open_file_entry_t *file = get_open_file_entry(fhandle);
    pthread_mutex_lock(&file->file_lock);
    pthread_mutex_unlock(&open_file_table_lock);

    pthread_mutex_lock(&inode_table_lock);
    inode_t *inode = inode_get(file->of_inumber);
    if(inode == NULL){
        pthread_mutex_unlock(&inode_table_lock);
        return -1;
    }
    pthread_rwlock_rdlock(&inode->rwl);
    pthread_mutex_unlock(&inode_table_lock);
    

    dest = fopen(dest_path, "w");
    if(dest == NULL){
        pthread_rwlock_unlock(&inode->rwl);
        pthread_mutex_unlock(&file->file_lock);
        return -1;
    }


    char buffer[BUFFER_SIZE+1];
    memset(buffer,0,BUFFER_SIZE);

    while(bytes_read == BUFFER_SIZE){
        memset(buffer,0,BUFFER_SIZE);
        bytes_read = tfs_read(fhandle, buffer, BUFFER_SIZE);
        if(bytes_read == -1){
            pthread_rwlock_unlock(&inode->rwl);
            pthread_mutex_unlock(&file->file_lock);
            return -1;
        }
        if(fwrite(buffer, sizeof(char), (size_t)bytes_read, dest) == -1){
            pthread_rwlock_unlock(&inode->rwl);
            pthread_mutex_unlock(&file->file_lock);
            return -1;
        }
    }

    tfs_close(fhandle);
    pthread_rwlock_unlock(&inode->rwl);
    pthread_mutex_unlock(&file->file_lock);
    fclose(dest);
    return 0;
}


ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    pthread_mutex_lock(&open_file_table_lock);
    open_file_entry_t *file = get_open_file_entry(fhandle);
    pthread_mutex_lock(&file->file_lock);
    pthread_mutex_unlock(&open_file_table_lock);
    void *block;
    size_t bytes_read = 0;
    size_t current_offset;
    if (file == NULL) {
        pthread_mutex_unlock(&file->file_lock);
        return -1;
    }
    pthread_mutex_lock(&inode_table_lock);
    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        pthread_mutex_unlock(&file->file_lock);
        pthread_mutex_unlock(&inode_table_lock);
        return -1;
    }
    pthread_rwlock_rdlock(&inode->rwl);
    pthread_mutex_unlock(&inode_table_lock);

    /* Determine how many bytes to read */
    size_t to_read = inode->i_size - file->of_offset;
    if (to_read > len) {
        to_read = len;
    }

    while(to_read){
        current_offset = get_current_offset(file);
        block = get_current_data_block(inode, file);
        if (block == NULL) {
            pthread_rwlock_unlock(&inode->rwl);
            pthread_mutex_unlock(&file->file_lock);
            return -1;
        }
        if(current_offset + to_read <= BLOCK_SIZE){
            memcpy(buffer + bytes_read, block + current_offset, to_read);
            file->of_offset += to_read;
            bytes_read += to_read;
            to_read = 0;
        }
        else{
            memcpy(buffer+bytes_read,block + current_offset, BLOCK_SIZE - current_offset);
            file->of_offset += BLOCK_SIZE - current_offset;
            bytes_read += (size_t)(BLOCK_SIZE - current_offset);
            to_read -= (size_t)(BLOCK_SIZE - current_offset);
        }
                    
    }
    pthread_mutex_unlock(&file->file_lock);
    pthread_rwlock_unlock(&inode->rwl);
    return (ssize_t)bytes_read;
}

