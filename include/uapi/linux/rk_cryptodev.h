/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR MIT) */
/*
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI_RK_CRYPTODEV_H
#define _UAPI_RK_CRYPTODEV_H

#include <linux/types.h>
#include <linux/version.h>

#ifndef __KERNEL__
#define __user
#endif

/* input of RIOCCRYPT_FD */
struct crypt_fd_op {
	__u32	ses;		/* session identifier */
	__u16	op;		/* COP_ENCRYPT or COP_DECRYPT */
	__u16	flags;		/* see COP_FLAG_* */
	__u32	len;		/* length of source data */
	int	src_fd;		/* source data */
	int	dst_fd;		/* pointer to output data */
	/* pointer to output data for hash/MAC operations */
	__u8	__user *mac;
	/* initialization vector for encryption operations */
	__u8	__user *iv;
};

/* input of RIOCCRYPT_FD_MAP/RIOCCRYPT_FD_UNMAP */
struct crypt_fd_map_op {
	int	dma_fd;		/* session identifier */
	__u32	phys_addr;	/* physics addr */
};

#define AOP_ENCRYPT	0
#define AOP_DECRYPT	1

#define COP_FLAG_RSA_PUB	(0 << 8) /* decode as rsa pub key */
#define COP_FLAG_RSA_PRIV	(1 << 8) /* decode as rsa priv key */

#define RK_RSA_BER_KEY_MAX	8192	/* The key encoded by ber does not exceed 8K Byte */
#define RK_RSA_KEY_MAX_BITS	4096
#define RK_RSA_KEY_MAX_BYTES	(RK_RSA_KEY_MAX_BITS / 8)

/* input of RIOCCRYPT_RSA_CRYPT */
struct crypt_rsa_op {
	__u16		op;		/* AOP_ENCRYPT/AOP_DECRYPT */
	__u16		flags;		/* see COP_FLAG_* */
	__u8		reserve[4];
	__u64		key;		/* BER coding RSA key */
	__u64		in;		/* pointer to input data */
	__u64		out;		/* pointer to output data */
	__u32		key_len;	/* length of key data */
	__u32		in_len;		/* length of input data */
	__u32		out_len;	/* length of output data */
};

#define RIOCCRYPT_FD		_IOWR('r', 104, struct crypt_fd_op)
#define RIOCCRYPT_FD_MAP	_IOWR('r', 105, struct crypt_fd_map_op)
#define RIOCCRYPT_FD_UNMAP	_IOW('r',  106, struct crypt_fd_map_op)
#define RIOCCRYPT_CPU_ACCESS	_IOW('r',  107, struct crypt_fd_map_op)
#define RIOCCRYPT_DEV_ACCESS	_IOW('r',  108, struct crypt_fd_map_op)
#define RIOCCRYPT_RSA_CRYPT	_IOWR('r', 109, struct crypt_rsa_op)

#endif
