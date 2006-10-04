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
 * Maximum blocksizes supported by daemon under various circumstances.
 */
#define NFSSVC_MAXBLKSIZE	RPCSVC_MAXPAYLOAD
/* NFSv2 is limited by the protocol specification, see RFC 1094 */
#define NFSSVC_MAXBLKSIZE_V2	(8*1024)

#ifdef __KERNEL__

#include <linux/sunrpc/msg_prot.h>

#ifndef NFS_SUPER_MAGIC
# define NFS_SUPER_MAGIC	0x6969
#endif

/*
 * Largest number of bytes we need to allocate for an NFS
 * call or reply.  Used to control buffer sizes.  We use
 * the length of v3 WRITE, READDIR and READDIR replies
 * which are an RPC header, up to 26 XDR units of reply
 * data, and some page data.
 *
 * Note that accuracy here doesn't matter too much as the
 * size is rounded up to a page size when allocating space.
 */
#define NFSD_BUFSIZE		((RPC_MAX_HEADER_WITH_AUTH+26)*XDR_UNIT + NFSSVC_MAXBLKSIZE)

#ifdef CONFIG_NFSD_V4
# define NFSSVC_XDRSIZE		NFS4_SVC_XDRSIZE
#elif defined(CONFIG_NFSD_V3)
# define NFSSVC_XDRSIZE		NFS3_SVC_XDRSIZE
#else
# define NFSSVC_XDRSIZE		NFS2_SVC_XDRSIZE
#endif

#endif /* __KERNEL__ */

#endif /* _LINUX_NFSD_CONST_H */
