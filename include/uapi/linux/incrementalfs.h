/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Userspace interface for Incremental FS.
 *
 * Incremental FS is special-purpose Linux virtual file system that allows
 * execution of a program while its binary and resource files are still being
 * lazily downloaded over the network, USB etc.
 *
 * Copyright 2019 Google LLC
 */
#ifndef _UAPI_LINUX_INCREMENTALFS_H
#define _UAPI_LINUX_INCREMENTALFS_H

#include <linux/limits.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/xattr.h>

/* ===== constants ===== */
#define INCFS_NAME "incremental-fs"
#define INCFS_MAGIC_NUMBER (0x5346434e49ul)
#define INCFS_DATA_FILE_BLOCK_SIZE 4096
#define INCFS_HEADER_VER 1

/* TODO: This value is assumed in incfs_copy_signature_info_from_user to be the
 * actual signature length. Set back to 64 when fixed.
 */
#define INCFS_MAX_HASH_SIZE 32
#define INCFS_MAX_FILE_ATTR_SIZE 512

#define INCFS_PENDING_READS_FILENAME ".pending_reads"
#define INCFS_LOG_FILENAME ".log"
#define INCFS_XATTR_ID_NAME (XATTR_USER_PREFIX "incfs.id")
#define INCFS_XATTR_SIZE_NAME (XATTR_USER_PREFIX "incfs.size")
#define INCFS_XATTR_METADATA_NAME (XATTR_USER_PREFIX "incfs.metadata")

#define INCFS_MAX_SIGNATURE_SIZE 8096

#define INCFS_IOCTL_BASE_CODE 'g'

/* ===== ioctl requests on the command dir ===== */

/* Create a new file */
#define INCFS_IOC_CREATE_FILE \
	_IOWR(INCFS_IOCTL_BASE_CODE, 30, struct incfs_new_file_args)

/* Read file signature */
#define INCFS_IOC_READ_FILE_SIGNATURE                                          \
	_IOWR(INCFS_IOCTL_BASE_CODE, 31, struct incfs_get_file_sig_args)

enum incfs_compression_alg {
	COMPRESSION_NONE = 0,
	COMPRESSION_LZ4 = 1
};

enum incfs_block_flags {
	INCFS_BLOCK_FLAGS_NONE = 0,
	INCFS_BLOCK_FLAGS_HASH = 1,
};

typedef struct {
	__u8 bytes[16];
} incfs_uuid_t __attribute__((aligned (8)));

/*
 * Description of a pending read. A pending read - a read call by
 * a userspace program for which the filesystem currently doesn't have data.
 */
struct incfs_pending_read_info {
	/* Id of a file that is being read from. */
	incfs_uuid_t file_id;

	/* A number of microseconds since system boot to the read. */
	__aligned_u64 timestamp_us;

	/* Index of a file block that is being read. */
	__u32 block_index;

	/* A serial number of this pending read. */
	__u32 serial_number;
};

/*
 * A struct to be written into a control file to load a data or hash
 * block to a data file.
 */
struct incfs_new_data_block {
	/* Index of a data block. */
	__u32 block_index;

	/* Length of data */
	__u32 data_len;

	/*
	 * A pointer to an actual data for the block.
	 *
	 * Equivalent to: __u8 *data;
	 */
	__aligned_u64 data;

	/*
	 * Compression algorithm used to compress the data block.
	 * Values from enum incfs_compression_alg.
	 */
	__u8 compression;

	/* Values from enum incfs_block_flags */
	__u8 flags;

	__u16 reserved1;

	__u32 reserved2;

	__aligned_u64 reserved3;
};

enum incfs_hash_tree_algorithm {
	INCFS_HASH_TREE_NONE = 0,
	INCFS_HASH_TREE_SHA256 = 1
};

struct incfs_file_signature_info {
	/*
	 * A pointer to file's root hash (if determined != 0)
	 * Actual hash size determined by hash_tree_alg.
	 * Size of the buffer should be at least INCFS_MAX_HASH_SIZE
	 *
	 * Equivalent to: u8 *root_hash;
	 */
	__aligned_u64 root_hash;

	/*
	 * A pointer to additional data that was attached to the root hash
	 * before signing.
	 *
	 * Equivalent to: u8 *additional_data;
	 */
	__aligned_u64 additional_data;

	/* Size of additional data. */
	__u32 additional_data_size;

	__u32 reserved1;

	/*
	 * A pointer to pkcs7 signature DER blob.
	 *
	 * Equivalent to: u8 *signature;
	 */
	__aligned_u64 signature;


	/* Size of pkcs7 signature DER blob */
	__u32 signature_size;

	__u32 reserved2;

	/* Value from incfs_hash_tree_algorithm */
	__u8 hash_tree_alg;
};

/*
 * Create a new file or directory.
 */
struct incfs_new_file_args {
	/* Id of a file to create. */
	incfs_uuid_t file_id;

	/*
	 * Total size of the new file. Ignored if S_ISDIR(mode).
	 */
	__aligned_u64 size;

	/*
	 * File mode. Permissions and dir flag.
	 */
	__u16 mode;

	__u16 reserved1;

	__u32 reserved2;

	/*
	 * A pointer to a null-terminated relative path to the file's parent
	 * dir.
	 * Max length: PATH_MAX
	 *
	 * Equivalent to: char *directory_path;
	 */
	__aligned_u64 directory_path;

	/*
	 * A pointer to a null-terminated file's name.
	 * Max length: PATH_MAX
	 *
	 * Equivalent to: char *file_name;
	 */
	__aligned_u64 file_name;

	/*
	 * A pointer to a file attribute to be set on creation.
	 *
	 * Equivalent to: u8 *file_attr;
	 */
	__aligned_u64 file_attr;

	/*
	 * Length of the data buffer specfied by file_attr.
	 * Max value: INCFS_MAX_FILE_ATTR_SIZE
	 */
	__u32 file_attr_len;

	__u32 reserved4;

	/* struct incfs_file_signature_info *signature_info; */
	__aligned_u64 signature_info;

	__aligned_u64 reserved5;

	__aligned_u64 reserved6;
};

/*
 * Request a digital signature blob for a given file.
 * Argument for INCFS_IOC_READ_FILE_SIGNATURE ioctl
 */
struct incfs_get_file_sig_args {
	/*
	 * A pointer to the data buffer to save an signature blob to.
	 *
	 * Equivalent to: u8 *file_signature;
	 */
	__aligned_u64 file_signature;

	/* Size of the buffer at file_signature. */
	__u32 file_signature_buf_size;

	/*
	 * Number of bytes save file_signature buffer.
	 * It is set after ioctl done.
	 */
	__u32 file_signature_len_out;
};

#endif /* _UAPI_LINUX_INCREMENTALFS_H */
