#ifndef __PPC_IPCBUF_H__
#define __PPC_IPCBUF_H__

/*
 * The ipc64_perm structure for PPC architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 1 32-bit value to fill up for 8-byte alignment
 * - 2 miscellaneous 64-bit values (so that this structure matches
 *                                  PPC64 ipc64_perm)
 */

struct ipc64_perm
{
	__kernel_key_t		key;
	__kernel_uid_t		uid;
	__kernel_gid_t		gid;
	__kernel_uid_t		cuid;
	__kernel_gid_t		cgid;
	__kernel_mode_t		mode;
	unsigned long		seq;
	unsigned int		__pad2;
	unsigned long long	__unused1;
	unsigned long long	__unused2;
};

#endif /* __PPC_IPCBUF_H__ */
