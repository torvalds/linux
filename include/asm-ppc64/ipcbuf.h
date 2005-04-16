#ifndef __PPC64_IPCBUF_H__
#define __PPC64_IPCBUF_H__

/*
 * The ipc64_perm structure for the PPC is identical to kern_ipc_perm
 * as we have always had 32-bit UIDs and GIDs in the kernel.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

struct ipc64_perm
{
	__kernel_key_t	key;
	__kernel_uid_t	uid;
	__kernel_gid_t	gid;
	__kernel_uid_t	cuid;
	__kernel_gid_t	cgid;
	__kernel_mode_t	mode;
	unsigned int	seq;
	unsigned int	__pad1;
	unsigned long	__unused1;
	unsigned long	__unused2;
};

#endif /* __PPC64_IPCBUF_H__ */
