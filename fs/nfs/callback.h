/*
 * linux/fs/nfs/callback.h
 *
 * Copyright (C) 2004 Trond Myklebust
 *
 * NFSv4 callback definitions
 */
#ifndef __LINUX_FS_NFS_CALLBACK_H
#define __LINUX_FS_NFS_CALLBACK_H

#define NFS4_CALLBACK 0x40000000
#define NFS4_CALLBACK_XDRSIZE 2048
#define NFS4_CALLBACK_BUFSIZE (1024 + NFS4_CALLBACK_XDRSIZE)

enum nfs4_callback_procnum {
	CB_NULL = 0,
	CB_COMPOUND = 1,
};

enum nfs4_callback_opnum {
	OP_CB_GETATTR = 3,
	OP_CB_RECALL  = 4,
	OP_CB_ILLEGAL = 10044,
};

struct cb_compound_hdr_arg {
	unsigned int taglen;
	const char *tag;
	unsigned int callback_ident;
	unsigned nops;
};

struct cb_compound_hdr_res {
	__be32 *status;
	unsigned int taglen;
	const char *tag;
	__be32 *nops;
};

struct cb_getattrargs {
	struct sockaddr *addr;
	struct nfs_fh fh;
	uint32_t bitmap[2];
};

struct cb_getattrres {
	__be32 status;
	uint32_t bitmap[2];
	uint64_t size;
	uint64_t change_attr;
	struct timespec ctime;
	struct timespec mtime;
};

struct cb_recallargs {
	struct sockaddr *addr;
	struct nfs_fh fh;
	nfs4_stateid stateid;
	uint32_t truncate;
};

extern __be32 nfs4_callback_getattr(struct cb_getattrargs *args, struct cb_getattrres *res);
extern __be32 nfs4_callback_recall(struct cb_recallargs *args, void *dummy);

#ifdef CONFIG_NFS_V4
extern int nfs_callback_up(void);
extern void nfs_callback_down(void);
#else
#define nfs_callback_up()	(0)
#define nfs_callback_down()	do {} while(0)
#endif

extern unsigned int nfs_callback_set_tcpport;
extern unsigned short nfs_callback_tcpport;

#endif /* __LINUX_FS_NFS_CALLBACK_H */
