/* SPDX-License-Identifier: GPL-2.0 */
/*
 * struct nfs_fh is an NFS version-agnostic data structure that
 * stores an NFS file handle. It is also commonly used in NFS
 * related APIs.
 */
#ifndef _LINUX_NFS_FH_H
#define _LINUX_NFS_FH_H

#include <linux/types.h>
#include <linux/string.h>
#include <linux/crc32.h>

/*
 * The largest file handle size today is an NFSv4 file handle,
 * which can be up to 128 octets long.
 */
#define NFS_MAXFHSIZE		128
struct nfs_fh {
	unsigned short		size;
	unsigned char		data[NFS_MAXFHSIZE];
};

/**
 * nfs_compare_fh - Compare two NFS file handles
 * @a: An NFS file handle to be compared
 * @b: An NFS file handle to be compared
 *
 * Checks only "size" bytes in each data field.
 *
 * Return: %false if the two file handles are equal, otherwise %true
 */
static inline bool nfs_compare_fh(const struct nfs_fh *a, const struct nfs_fh *b)
{
	return a->size != b->size || memcmp(a->data, b->data, a->size) != 0;
}

/**
 * nfs_copy_fh - Copy an NFS file handle
 * @target: Destination file handle
 * @source: Source file handle
 *
 * Copies source->size bytes of file handle data into target.
 */
static inline void nfs_copy_fh(struct nfs_fh *target, const struct nfs_fh *source)
{
	target->size = source->size;
	memcpy(target->data, source->data, source->size);
}

/**
 * nfs_fhandle_hash - Calculate the crc32 hash for the filehandle
 * @fh: An NFS file handle to hash
 *
 * Return: a crc32 hash for the filehandle that is compatible with
 * the one displayed by "wireshark"
 */
static inline u32 nfs_fhandle_hash(const struct nfs_fh *fh)
{
	return ~crc32_le(0xFFFFFFFF, &fh->data[0], fh->size);
}

#endif /* _LINUX_NFS_FH_H */
