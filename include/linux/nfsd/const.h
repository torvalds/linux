/*
 * include/linux/nfsd/const.h
 *
 * Various constants related to NFS.
 *
 * Copyright (C) 1995-1997 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_NFSD_CONST_H
#define _LINUX_NFSD_CONST_H

#include <linux/nfs.h>
#include <linux/nfs2.h>
#include <linux/nfs3.h>
#include <linux/nfs4.h>

/*
 * Maximum protocol version supported by knfsd
 */
#define NFSSVC_MAXVERS		3

/*
 * Maximum blocksize supported by daemon currently at 32K
 */
#define NFSSVC_MAXBLKSIZE	(32*1024)

#ifdef __KERNEL__

#ifndef NFS_SUPER_MAGIC
# define NFS_SUPER_MAGIC	0x6969
#endif

#define NFSD_BUFSIZE		(1024 + NFSSVC_MAXBLKSIZE)

#ifdef CONFIG_NFSD_V4
# define NFSSVC_XDRSIZE		NFS4_SVC_XDRSIZE
#elif defined(CONFIG_NFSD_V3)
# define NFSSVC_XDRSIZE		NFS3_SVC_XDRSIZE
#else
# define NFSSVC_XDRSIZE		NFS2_SVC_XDRSIZE
#endif

#endif /* __KERNEL__ */

#endif /* _LINUX_NFSD_CONST_H */
