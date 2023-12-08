/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * NFS protocol definitions
 *
 * This file contains constants for Version 2 of the protocol.
 */
#ifndef _LINUX_NFS2_H
#define _LINUX_NFS2_H

#define NFS2_PORT	2049
#define NFS2_MAXDATA	8192
#define NFS2_MAXPATHLEN	1024
#define NFS2_MAXNAMLEN	255
#define NFS2_MAXGROUPS	16
#define NFS2_FHSIZE	32
#define NFS2_COOKIESIZE	4
#define NFS2_FIFO_DEV	(-1)
#define NFS2MODE_FMT	0170000
#define NFS2MODE_DIR	0040000
#define NFS2MODE_CHR	0020000
#define NFS2MODE_BLK	0060000
#define NFS2MODE_REG	0100000
#define NFS2MODE_LNK	0120000
#define NFS2MODE_SOCK	0140000
#define NFS2MODE_FIFO	0010000


/* NFSv2 file types - beware, these are not the same in NFSv3 */
enum nfs2_ftype {
	NF2NON = 0,
	NF2REG = 1,
	NF2DIR = 2,
	NF2BLK = 3,
	NF2CHR = 4,
	NF2LNK = 5,
	NF2SOCK = 6,
	NF2BAD = 7,
	NF2FIFO = 8
};

struct nfs2_fh {
	char			data[NFS2_FHSIZE];
};

/*
 * Procedure numbers for NFSv2
 */
#define NFS2_VERSION		2
#define NFSPROC_NULL		0
#define NFSPROC_GETATTR		1
#define NFSPROC_SETATTR		2
#define NFSPROC_ROOT		3
#define NFSPROC_LOOKUP		4
#define NFSPROC_READLINK	5
#define NFSPROC_READ		6
#define NFSPROC_WRITECACHE	7
#define NFSPROC_WRITE		8
#define NFSPROC_CREATE		9
#define NFSPROC_REMOVE		10
#define NFSPROC_RENAME		11
#define NFSPROC_LINK		12
#define NFSPROC_SYMLINK		13
#define NFSPROC_MKDIR		14
#define NFSPROC_RMDIR		15
#define NFSPROC_READDIR		16
#define NFSPROC_STATFS		17

#endif /* _LINUX_NFS2_H */
