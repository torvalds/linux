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
	int taglen;
	const char *tag;
	unsigned int callback_ident;
	unsigned nops;
};

struct cb_compound_hdr_res {
	uint32_t *status;
	int taglen;
	const char *tag;
	uint32_t *nops;
};

struct cb_getattrargs {
	struct sockaddr_in *addr;
	struct nfs_fh fh;
	uint32_t bitmap[2];
};

struct cb_getattrres {
	uint32_t status;
	uint32_t bitmap[2];
	uint64_t size;
	uint64_t change_attr;
	struct timespec ctime;
	struct timespec mtime;
};

struct cb_recallargs {
	struct sockaddr_in *addr;
	struct nfs_fh fh;
	nfs4_stateid stateid;
	uint32_t truncate;
};

extern unsigned nfs4_callback_getattr(struct cb_getattrargs *args, struct cb_getattrres *res);
extern unsigned nfs4_callback_recall(struct cb_recallargs *args, void *dummy);

extern int nfs_callback_up(void);
extern int nfs_callback_down(void);

extern unsigned short nfs_callback_tcpport;

#endif /* __LINUX_FS_NFS_CALLBACK_H */
