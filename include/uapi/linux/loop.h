/* SPDX-License-Identifier: GPL-1.0+ WITH Linux-syscall-note */
/*
 * Copyright 1993 by Theodore Ts'o.
 */
#ifndef _UAPI_LINUX_LOOP_H
#define _UAPI_LINUX_LOOP_H


#define LO_NAME_SIZE	64
#define LO_KEY_SIZE	32


/*
 * Loop flags
 */
enum {
	LO_FLAGS_READ_ONLY	= 1,
	LO_FLAGS_AUTOCLEAR	= 4,
	LO_FLAGS_PARTSCAN	= 8,
	LO_FLAGS_DIRECT_IO	= 16,
};

/* LO_FLAGS that can be set using LOOP_SET_STATUS(64) */
#define LOOP_SET_STATUS_SETTABLE_FLAGS (LO_FLAGS_AUTOCLEAR | LO_FLAGS_PARTSCAN)

/* LO_FLAGS that can be cleared using LOOP_SET_STATUS(64) */
#define LOOP_SET_STATUS_CLEARABLE_FLAGS (LO_FLAGS_AUTOCLEAR)

/* LO_FLAGS that can be set using LOOP_CONFIGURE */
#define LOOP_CONFIGURE_SETTABLE_FLAGS (LO_FLAGS_READ_ONLY | LO_FLAGS_AUTOCLEAR \
				       | LO_FLAGS_PARTSCAN | LO_FLAGS_DIRECT_IO)

#include <asm/posix_types.h>	/* for __kernel_old_dev_t */
#include <linux/types.h>	/* for __u64 */

/* Backwards compatibility version */
struct loop_info {
	int		   lo_number;		/* ioctl r/o */
	__kernel_old_dev_t lo_device; 		/* ioctl r/o */
	unsigned long	   lo_inode; 		/* ioctl r/o */
	__kernel_old_dev_t lo_rdevice; 		/* ioctl r/o */
	int		   lo_offset;
	int		   lo_encrypt_type;		/* obsolete, ignored */
	int		   lo_encrypt_key_size; 	/* ioctl w/o */
	int		   lo_flags;
	char		   lo_name[LO_NAME_SIZE];
	unsigned char	   lo_encrypt_key[LO_KEY_SIZE]; /* ioctl w/o */
	unsigned long	   lo_init[2];
	char		   reserved[4];
};

struct loop_info64 {
	__u64		   lo_device;			/* ioctl r/o */
	__u64		   lo_inode;			/* ioctl r/o */
	__u64		   lo_rdevice;			/* ioctl r/o */
	__u64		   lo_offset;
	__u64		   lo_sizelimit;/* bytes, 0 == max available */
	__u32		   lo_number;			/* ioctl r/o */
	__u32		   lo_encrypt_type;		/* obsolete, ignored */
	__u32		   lo_encrypt_key_size;		/* ioctl w/o */
	__u32		   lo_flags;
	__u8		   lo_file_name[LO_NAME_SIZE];
	__u8		   lo_crypt_name[LO_NAME_SIZE];
	__u8		   lo_encrypt_key[LO_KEY_SIZE]; /* ioctl w/o */
	__u64		   lo_init[2];
};

/**
 * struct loop_config - Complete configuration for a loop device.
 * @fd: fd of the file to be used as a backing file for the loop device.
 * @block_size: block size to use; ignored if 0.
 * @info: struct loop_info64 to configure the loop device with.
 *
 * This structure is used with the LOOP_CONFIGURE ioctl, and can be used to
 * atomically setup and configure all loop device parameters at once.
 */
struct loop_config {
	__u32			fd;
	__u32                   block_size;
	struct loop_info64	info;
	__u64			__reserved[8];
};

/*
 * Loop filter types
 */

#define LO_CRYPT_NONE		0
#define LO_CRYPT_XOR		1
#define LO_CRYPT_DES		2
#define LO_CRYPT_FISH2		3    /* Twofish encryption */
#define LO_CRYPT_BLOW		4
#define LO_CRYPT_CAST128	5
#define LO_CRYPT_IDEA		6
#define LO_CRYPT_DUMMY		9
#define LO_CRYPT_SKIPJACK	10
#define LO_CRYPT_CRYPTOAPI	18
#define MAX_LO_CRYPT		20

/*
 * IOCTL commands --- we will commandeer 0x4C ('L')
 */

#define LOOP_SET_FD		0x4C00
#define LOOP_CLR_FD		0x4C01
#define LOOP_SET_STATUS		0x4C02
#define LOOP_GET_STATUS		0x4C03
#define LOOP_SET_STATUS64	0x4C04
#define LOOP_GET_STATUS64	0x4C05
#define LOOP_CHANGE_FD		0x4C06
#define LOOP_SET_CAPACITY	0x4C07
#define LOOP_SET_DIRECT_IO	0x4C08
#define LOOP_SET_BLOCK_SIZE	0x4C09
#define LOOP_CONFIGURE		0x4C0A

/* /dev/loop-control interface */
#define LOOP_CTL_ADD		0x4C80
#define LOOP_CTL_REMOVE		0x4C81
#define LOOP_CTL_GET_FREE	0x4C82
#endif /* _UAPI_LINUX_LOOP_H */
