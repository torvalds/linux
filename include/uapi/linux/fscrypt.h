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

#define FSCRYPT_KEY_DESCRIPTOR_SIZE	8

/* Encryption policy flags */
#define FSCRYPT_POLICY_FLAGS_PAD_4		0x00
#define FSCRYPT_POLICY_FLAGS_PAD_8		0x01
#define FSCRYPT_POLICY_FLAGS_PAD_16		0x02
#define FSCRYPT_POLICY_FLAGS_PAD_32		0x03
#define FSCRYPT_POLICY_FLAGS_PAD_MASK		0x03
#define FSCRYPT_POLICY_FLAG_DIRECT_KEY		0x04	/* use master key directly */
#define FSCRYPT_POLICY_FLAGS_VALID		0x07

/* Encryption algorithms */
#define FSCRYPT_MODE_AES_256_XTS		1
#define FSCRYPT_MODE_AES_256_CTS		4
#define FSCRYPT_MODE_AES_128_CBC		5
#define FSCRYPT_MODE_AES_128_CTS		6
#define FSCRYPT_MODE_ADIANTUM			9

struct fscrypt_policy {
	__u8 version;
	__u8 contents_encryption_mode;
	__u8 filenames_encryption_mode;
	__u8 flags;
	__u8 master_key_descriptor[FSCRYPT_KEY_DESCRIPTOR_SIZE];
};

/*
 * Process-subscribed "logon" key description prefix and payload format.
 * Deprecated; prefer FS_IOC_ADD_ENCRYPTION_KEY instead.
 */
#define FSCRYPT_KEY_DESC_PREFIX		"fscrypt:"
#define FSCRYPT_KEY_DESC_PREFIX_SIZE	8
#define FSCRYPT_MAX_KEY_SIZE		64
struct fscrypt_key {
	__u32 mode;
	__u8 raw[FSCRYPT_MAX_KEY_SIZE];
	__u32 size;
};

/*
 * Keys are specified by an arbitrary 8-byte key "descriptor",
 * matching fscrypt_policy::master_key_descriptor.
 */
#define FSCRYPT_KEY_SPEC_TYPE_DESCRIPTOR	1

/*
 * Specifies a key.  This doesn't contain the actual key itself; this is just
 * the "name" of the key.
 */
struct fscrypt_key_specifier {
	__u32 type;	/* one of FSCRYPT_KEY_SPEC_TYPE_* */
	__u32 __reserved;
	union {
		__u8 __reserved[32]; /* reserve some extra space */
		__u8 descriptor[FSCRYPT_KEY_DESCRIPTOR_SIZE];
	} u;
};

/* Struct passed to FS_IOC_ADD_ENCRYPTION_KEY */
struct fscrypt_add_key_arg {
	struct fscrypt_key_specifier key_spec;
	__u32 raw_size;
	__u32 __reserved[9];
	__u8 raw[];
};

#define FS_IOC_SET_ENCRYPTION_POLICY		_IOR('f', 19, struct fscrypt_policy)
#define FS_IOC_GET_ENCRYPTION_PWSALT		_IOW('f', 20, __u8[16])
#define FS_IOC_GET_ENCRYPTION_POLICY		_IOW('f', 21, struct fscrypt_policy)
#define FS_IOC_ADD_ENCRYPTION_KEY		_IOWR('f', 23, struct fscrypt_add_key_arg)

/**********************************************************************/

/* old names; don't add anything new here! */
#ifndef __KERNEL__
#define FS_KEY_DESCRIPTOR_SIZE		FSCRYPT_KEY_DESCRIPTOR_SIZE
#define FS_POLICY_FLAGS_PAD_4		FSCRYPT_POLICY_FLAGS_PAD_4
#define FS_POLICY_FLAGS_PAD_8		FSCRYPT_POLICY_FLAGS_PAD_8
#define FS_POLICY_FLAGS_PAD_16		FSCRYPT_POLICY_FLAGS_PAD_16
#define FS_POLICY_FLAGS_PAD_32		FSCRYPT_POLICY_FLAGS_PAD_32
#define FS_POLICY_FLAGS_PAD_MASK	FSCRYPT_POLICY_FLAGS_PAD_MASK
#define FS_POLICY_FLAG_DIRECT_KEY	FSCRYPT_POLICY_FLAG_DIRECT_KEY
#define FS_POLICY_FLAGS_VALID		FSCRYPT_POLICY_FLAGS_VALID
#define FS_ENCRYPTION_MODE_INVALID	0	/* never used */
#define FS_ENCRYPTION_MODE_AES_256_XTS	FSCRYPT_MODE_AES_256_XTS
#define FS_ENCRYPTION_MODE_AES_256_GCM	2	/* never used */
#define FS_ENCRYPTION_MODE_AES_256_CBC	3	/* never used */
#define FS_ENCRYPTION_MODE_AES_256_CTS	FSCRYPT_MODE_AES_256_CTS
#define FS_ENCRYPTION_MODE_AES_128_CBC	FSCRYPT_MODE_AES_128_CBC
#define FS_ENCRYPTION_MODE_AES_128_CTS	FSCRYPT_MODE_AES_128_CTS
#define FS_ENCRYPTION_MODE_SPECK128_256_XTS	7	/* removed */
#define FS_ENCRYPTION_MODE_SPECK128_256_CTS	8	/* removed */
#define FS_ENCRYPTION_MODE_ADIANTUM	FSCRYPT_MODE_ADIANTUM
#define FS_KEY_DESC_PREFIX		FSCRYPT_KEY_DESC_PREFIX
#define FS_KEY_DESC_PREFIX_SIZE		FSCRYPT_KEY_DESC_PREFIX_SIZE
#define FS_MAX_KEY_SIZE			FSCRYPT_MAX_KEY_SIZE
#endif /* !__KERNEL__ */

#endif /* _UAPI_LINUX_FSCRYPT_H */
