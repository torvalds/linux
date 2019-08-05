/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * fscrypt user API
 *
 * These ioctls can be used on filesystems that support fscrypt.  See the
 * "User API" section of Documentation/filesystems/fscrypt.rst.
 */
#ifndef _UAPI_LINUX_FSCRYPT_H
#define _UAPI_LINUX_FSCRYPT_H

#include <linux/types.h>

#define FS_KEY_DESCRIPTOR_SIZE	8

/* Encryption policy flags */
#define FS_POLICY_FLAGS_PAD_4		0x00
#define FS_POLICY_FLAGS_PAD_8		0x01
#define FS_POLICY_FLAGS_PAD_16		0x02
#define FS_POLICY_FLAGS_PAD_32		0x03
#define FS_POLICY_FLAGS_PAD_MASK	0x03
#define FS_POLICY_FLAG_DIRECT_KEY	0x04	/* use master key directly */
#define FS_POLICY_FLAGS_VALID		0x07

/* Encryption algorithms */
#define FS_ENCRYPTION_MODE_INVALID		0
#define FS_ENCRYPTION_MODE_AES_256_XTS		1
#define FS_ENCRYPTION_MODE_AES_256_GCM		2
#define FS_ENCRYPTION_MODE_AES_256_CBC		3
#define FS_ENCRYPTION_MODE_AES_256_CTS		4
#define FS_ENCRYPTION_MODE_AES_128_CBC		5
#define FS_ENCRYPTION_MODE_AES_128_CTS		6
#define FS_ENCRYPTION_MODE_SPECK128_256_XTS	7 /* Removed, do not use. */
#define FS_ENCRYPTION_MODE_SPECK128_256_CTS	8 /* Removed, do not use. */
#define FS_ENCRYPTION_MODE_ADIANTUM		9

struct fscrypt_policy {
	__u8 version;
	__u8 contents_encryption_mode;
	__u8 filenames_encryption_mode;
	__u8 flags;
	__u8 master_key_descriptor[FS_KEY_DESCRIPTOR_SIZE];
};

#define FS_IOC_SET_ENCRYPTION_POLICY	_IOR('f', 19, struct fscrypt_policy)
#define FS_IOC_GET_ENCRYPTION_PWSALT	_IOW('f', 20, __u8[16])
#define FS_IOC_GET_ENCRYPTION_POLICY	_IOW('f', 21, struct fscrypt_policy)

/* Parameters for passing an encryption key into the kernel keyring */
#define FS_KEY_DESC_PREFIX		"fscrypt:"
#define FS_KEY_DESC_PREFIX_SIZE		8

/* Structure that userspace passes to the kernel keyring */
#define FS_MAX_KEY_SIZE			64

struct fscrypt_key {
	__u32 mode;
	__u8 raw[FS_MAX_KEY_SIZE];
	__u32 size;
};

#endif /* _UAPI_LINUX_FSCRYPT_H */
