#ifndef COMMON_H
#define COMMON_H
#include <stdio.h>

#define MAX_PIPENAME_SIZE (40)
#define OPCODE_MESSAGE_SIZE (2)
#define MAX_CLIENTS (20)

/* tfs_open flags */
enum {
    TFS_O_CREAT = 0b001,
    TFS_O_TRUNC = 0b010,
    TFS_O_APPEND = 0b100,
};

typedef struct __attribute__((__packed__)) Mount_Messages{
    char client_pipe[MAX_PIPENAME_SIZE];
}Mount_Message;

typedef struct __attribute__((__packed__)) Unmount_Messages{
    int session_id;
}Unmount_Message;

typedef struct __attribute__((__packed__)) Open_Messages{
    int session_id;
    char name[MAX_PIPENAME_SIZE];
    int flags;
}Open_Message;

typedef struct __attribute__((__packed__)) Close_Messages{
    int session_id;
    int fhandle;
}Close_Message;

typedef struct __attribute__((__packed__)) Write_Messages{
    int session_id;
    int fhandle;
    size_t len;
}Write_Message;

typedef struct __attribute__((__packed__)) Read_Messages{
    int session_id;
    int fhandle;
    size_t len;
}Read_Message;

typedef struct __attribute__((__packed__)) Shutdown_Messages{
    int session_id;
} Shutdown_Message;

// operation codes (for client-server requests) 
enum {
    TFS_OP_CODE_MOUNT = 1,
    TFS_OP_CODE_UNMOUNT = 2,
    TFS_OP_CODE_OPEN = 3,
    TFS_OP_CODE_CLOSE = 4,
    TFS_OP_CODE_WRITE = 5,
    TFS_OP_CODE_READ = 6,
    TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED = 7
};

#endif /* COMMON_H */