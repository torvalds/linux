/*
 *  Name                         : qnxtypes.h
 *  Author                       : Richard Frowijn
 *  Function                     : standard qnx types
 *  Version                      : 1.0.2
 *  Last modified                : 2000-01-06
 *
 *  History                      : 22-03-1998 created
 *
 */

#ifndef _QNX4TYPES_H
#define _QNX4TYPES_H

typedef __u16 qnx4_nxtnt_t;
typedef __u8  qnx4_ftype_t;

typedef struct {
	__u32 xtnt_blk;
	__u32 xtnt_size;
} qnx4_xtnt_t;

typedef __u16 qnx4_mode_t;
typedef __u16 qnx4_muid_t;
typedef __u16 qnx4_mgid_t;
typedef __u32 qnx4_off_t;
typedef __u16 qnx4_nlink_t;

#endif
