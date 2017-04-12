/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef _ORANGEFS_DEV_PROTO_H
#define _ORANGEFS_DEV_PROTO_H

/*
 * types and constants shared between user space and kernel space for
 * device interaction using a common protocol
 */

/*
 * valid orangefs kernel operation types
 */
#define ORANGEFS_VFS_OP_INVALID           0xFF000000
#define ORANGEFS_VFS_OP_FILE_IO        0xFF000001
#define ORANGEFS_VFS_OP_LOOKUP         0xFF000002
#define ORANGEFS_VFS_OP_CREATE         0xFF000003
#define ORANGEFS_VFS_OP_GETATTR        0xFF000004
#define ORANGEFS_VFS_OP_REMOVE         0xFF000005
#define ORANGEFS_VFS_OP_MKDIR          0xFF000006
#define ORANGEFS_VFS_OP_READDIR        0xFF000007
#define ORANGEFS_VFS_OP_SETATTR        0xFF000008
#define ORANGEFS_VFS_OP_SYMLINK        0xFF000009
#define ORANGEFS_VFS_OP_RENAME         0xFF00000A
#define ORANGEFS_VFS_OP_STATFS         0xFF00000B
#define ORANGEFS_VFS_OP_TRUNCATE       0xFF00000C
#define ORANGEFS_VFS_OP_RA_FLUSH       0xFF00000D
#define ORANGEFS_VFS_OP_FS_MOUNT       0xFF00000E
#define ORANGEFS_VFS_OP_FS_UMOUNT      0xFF00000F
#define ORANGEFS_VFS_OP_GETXATTR       0xFF000010
#define ORANGEFS_VFS_OP_SETXATTR          0xFF000011
#define ORANGEFS_VFS_OP_LISTXATTR         0xFF000012
#define ORANGEFS_VFS_OP_REMOVEXATTR       0xFF000013
#define ORANGEFS_VFS_OP_PARAM          0xFF000014
#define ORANGEFS_VFS_OP_PERF_COUNT     0xFF000015
#define ORANGEFS_VFS_OP_CANCEL            0xFF00EE00
#define ORANGEFS_VFS_OP_FSYNC          0xFF00EE01
#define ORANGEFS_VFS_OP_FSKEY             0xFF00EE02
#define ORANGEFS_VFS_OP_READDIRPLUS       0xFF00EE03
#define ORANGEFS_VFS_OP_FEATURES	0xFF00EE05 /* 2.9.6 */

/* features is a 64-bit unsigned bitmask */
#define ORANGEFS_FEATURE_READAHEAD 1

/*
 * Misc constants. Please retain them as multiples of 8!
 * Otherwise 32-64 bit interactions will be messed up :)
 */
#define ORANGEFS_MAX_DEBUG_STRING_LEN	0x00000800

/*
 * The maximum number of directory entries in a single request is 96.
 * XXX: Why can this not be higher. The client-side code can handle up to 512.
 * XXX: What happens if we expect more than the client can return?
 */
#define ORANGEFS_MAX_DIRENT_COUNT_READDIR 96

#include "upcall.h"
#include "downcall.h"

#endif
