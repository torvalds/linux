/******************************************************************************
 * fsif.h
 * 
 * Interface to FS level split device drivers.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Copyright (c) 2007, Grzegorz Milos, <gm281@cam.ac.uk>.
 */

#ifndef __XEN_PUBLIC_IO_FSIF_H__
#define __XEN_PUBLIC_IO_FSIF_H__

#include "ring.h"
#include "../grant_table.h"

#define REQ_FILE_OPEN        1
#define REQ_FILE_CLOSE       2
#define REQ_FILE_READ        3
#define REQ_FILE_WRITE       4
#define REQ_STAT             5
#define REQ_FILE_TRUNCATE    6
#define REQ_REMOVE           7
#define REQ_RENAME           8
#define REQ_CREATE           9
#define REQ_DIR_LIST        10
#define REQ_CHMOD           11
#define REQ_FS_SPACE        12
#define REQ_FILE_SYNC       13

struct fsif_open_request {
    grant_ref_t gref;
};

struct fsif_close_request {
    uint32_t fd;
};

struct fsif_read_request {
    uint32_t fd;
    int32_t pad;
    uint64_t len;
    uint64_t offset;
    grant_ref_t grefs[1];  /* Variable length */
};

struct fsif_write_request {
    uint32_t fd;
    int32_t pad;
    uint64_t len;
    uint64_t offset;
    grant_ref_t grefs[1];  /* Variable length */
};

struct fsif_stat_request {
    uint32_t fd;
};

/* This structure is a copy of some fields from stat structure, returned
 * via the ring. */
struct fsif_stat_response {
    int32_t  stat_mode;
    uint32_t stat_uid;
    uint32_t stat_gid;
    int32_t  stat_ret;
    int64_t  stat_size;
    int64_t  stat_atime;
    int64_t  stat_mtime;
    int64_t  stat_ctime;
};

struct fsif_truncate_request {
    uint32_t fd;
    int32_t pad;
    int64_t length;
};

struct fsif_remove_request {
    grant_ref_t gref;
};

struct fsif_rename_request {
    uint16_t old_name_offset;
    uint16_t new_name_offset;
    grant_ref_t gref;
};

struct fsif_create_request {
    int8_t directory;
    int8_t pad;
    int16_t pad2;
    int32_t mode;
    grant_ref_t gref;
};

struct fsif_list_request {
    uint32_t offset;
    grant_ref_t gref;
};

#define NR_FILES_SHIFT  0
#define NR_FILES_SIZE   16   /* 16 bits for the number of files mask */
#define NR_FILES_MASK   (((1ULL << NR_FILES_SIZE) - 1) << NR_FILES_SHIFT)
#define ERROR_SIZE      32   /* 32 bits for the error mask */
#define ERROR_SHIFT     (NR_FILES_SIZE + NR_FILES_SHIFT)
#define ERROR_MASK      (((1ULL << ERROR_SIZE) - 1) << ERROR_SHIFT)
#define HAS_MORE_SHIFT  (ERROR_SHIFT + ERROR_SIZE)    
#define HAS_MORE_FLAG   (1ULL << HAS_MORE_SHIFT)

struct fsif_chmod_request {
    uint32_t fd;
    int32_t mode;
};

struct fsif_space_request {
    grant_ref_t gref;
};

struct fsif_sync_request {
    uint32_t fd;
};


/* FS operation request */
struct fsif_request {
    uint8_t type;                 /* Type of the request                  */
    uint8_t pad;
    uint16_t id;                  /* Request ID, copied to the response   */
    uint32_t pad2;
    union {
        struct fsif_open_request     fopen;
        struct fsif_close_request    fclose;
        struct fsif_read_request     fread;
        struct fsif_write_request    fwrite;
        struct fsif_stat_request     fstat;
        struct fsif_truncate_request ftruncate;
        struct fsif_remove_request   fremove;
        struct fsif_rename_request   frename;
        struct fsif_create_request   fcreate;
        struct fsif_list_request     flist;
        struct fsif_chmod_request    fchmod;
        struct fsif_space_request    fspace;
        struct fsif_sync_request     fsync;
    } u;
};
typedef struct fsif_request fsif_request_t;

/* FS operation response */
struct fsif_response {
    uint16_t id;
    uint16_t pad1;
    uint32_t pad2;
    union {
        uint64_t ret_val;
        struct fsif_stat_response fstat;
    } u;
};

typedef struct fsif_response fsif_response_t;

#define FSIF_RING_ENTRY_SIZE   64

#define FSIF_NR_READ_GNTS  ((FSIF_RING_ENTRY_SIZE - sizeof(struct fsif_read_request)) /  \
                                sizeof(grant_ref_t) + 1)
#define FSIF_NR_WRITE_GNTS ((FSIF_RING_ENTRY_SIZE - sizeof(struct fsif_write_request)) / \
                                sizeof(grant_ref_t) + 1)

DEFINE_RING_TYPES(fsif, struct fsif_request, struct fsif_response);

#define STATE_INITIALISED     "init"
#define STATE_READY           "ready"
#define STATE_CLOSING         "closing"
#define STATE_CLOSED          "closed"


#endif
