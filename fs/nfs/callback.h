/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/fs/nfs/callback.h
 *
 * Copyright (C) 2004 Trond Myklebust
 *
 * NFSv4 callback definitions
 */
#ifndef __LINUX_FS_NFS_CALLBACK_H
#define __LINUX_FS_NFS_CALLBACK_H
#include <linux/sunrpc/svc.h>

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
/* Callback operations new to NFSv4.1 */
	OP_CB_LAYOUTRECALL  = 5,
	OP_CB_NOTIFY        = 6,
	OP_CB_PUSH_DELEG    = 7,
	OP_CB_RECALL_ANY    = 8,
	OP_CB_RECALLABLE_OBJ_AVAIL = 9,
	OP_CB_RECALL_SLOT   = 10,
	OP_CB_SEQUENCE      = 11,
	OP_CB_WANTS_CANCELLED = 12,
	OP_CB_NOTIFY_LOCK   = 13,
	OP_CB_NOTIFY_DEVICEID = 14,
/* Callback operations new to NFSv4.2 */
	OP_CB_OFFLOAD = 15,
	OP_CB_ILLEGAL = 10044,
};

struct nfs4_slot;
struct cb_process_state {
	__be32			drc_status;
	struct nfs_client	*clp;
	struct nfs4_slot	*slot;
	u32			minorversion;
	struct net		*net;
};

struct cb_compound_hdr_arg {
	unsigned int taglen;
	const char *tag;
	unsigned int minorversion;
	unsigned int cb_ident; /* v4.0 callback identifier */
	unsigned nops;
};

struct cb_compound_hdr_res {
	__be32 *status;
	unsigned int taglen;
	const char *tag;
	__be32 *nops;
};

struct cb_getattrargs {
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
	struct nfs_fh fh;
	nfs4_stateid stateid;
	uint32_t truncate;
};

#if defined(CONFIG_NFS_V4_1)

struct referring_call {
	uint32_t			rc_sequenceid;
	uint32_t			rc_slotid;
};

struct referring_call_list {
	struct nfs4_sessionid		rcl_sessionid;
	uint32_t			rcl_nrefcalls;
	struct referring_call 		*rcl_refcalls;
};

struct cb_sequenceargs {
	struct sockaddr			*csa_addr;
	struct nfs4_sessionid		csa_sessionid;
	uint32_t			csa_sequenceid;
	uint32_t			csa_slotid;
	uint32_t			csa_highestslotid;
	uint32_t			csa_cachethis;
	uint32_t			csa_nrclists;
	struct referring_call_list	*csa_rclists;
};

struct cb_sequenceres {
	__be32				csr_status;
	struct nfs4_sessionid		csr_sessionid;
	uint32_t			csr_sequenceid;
	uint32_t			csr_slotid;
	uint32_t			csr_highestslotid;
	uint32_t			csr_target_highestslotid;
};

extern __be32 nfs4_callback_sequence(void *argp, void *resp,
				       struct cb_process_state *cps);

#define RCA4_TYPE_MASK_RDATA_DLG	0
#define RCA4_TYPE_MASK_WDATA_DLG	1
#define RCA4_TYPE_MASK_DIR_DLG         2
#define RCA4_TYPE_MASK_FILE_LAYOUT     3
#define RCA4_TYPE_MASK_BLK_LAYOUT      4
#define RCA4_TYPE_MASK_OBJ_LAYOUT_MIN  8
#define RCA4_TYPE_MASK_OBJ_LAYOUT_MAX  9
#define RCA4_TYPE_MASK_OTHER_LAYOUT_MIN 12
#define RCA4_TYPE_MASK_OTHER_LAYOUT_MAX 15
#define RCA4_TYPE_MASK_ALL 0xf31f

struct cb_recallanyargs {
	uint32_t	craa_objs_to_keep;
	uint32_t	craa_type_mask;
};

extern __be32 nfs4_callback_recallany(void *argp, void *resp,
					struct cb_process_state *cps);

struct cb_recallslotargs {
	uint32_t	crsa_target_highest_slotid;
};
extern __be32 nfs4_callback_recallslot(void *argp, void *resp,
					 struct cb_process_state *cps);

struct cb_layoutrecallargs {
	uint32_t		cbl_recall_type;
	uint32_t		cbl_layout_type;
	uint32_t		cbl_layoutchanged;
	union {
		struct {
			struct nfs_fh		cbl_fh;
			struct pnfs_layout_range cbl_range;
			nfs4_stateid		cbl_stateid;
		};
		struct nfs_fsid		cbl_fsid;
	};
};

extern __be32 nfs4_callback_layoutrecall(void *argp, void *resp,
		struct cb_process_state *cps);

struct cb_devicenotifyitem {
	uint32_t		cbd_notify_type;
	uint32_t		cbd_layout_type;
	struct nfs4_deviceid	cbd_dev_id;
	uint32_t		cbd_immediate;
};

struct cb_devicenotifyargs {
	int				 ndevs;
	struct cb_devicenotifyitem	 *devs;
};

extern __be32 nfs4_callback_devicenotify(void *argp, void *resp,
		struct cb_process_state *cps);

struct cb_notify_lock_args {
	struct nfs_fh			cbnl_fh;
	struct nfs_lowner		cbnl_owner;
	bool				cbnl_valid;
};

extern __be32 nfs4_callback_notify_lock(void *argp, void *resp,
					 struct cb_process_state *cps);
#endif /* CONFIG_NFS_V4_1 */
#ifdef CONFIG_NFS_V4_2
struct cb_offloadargs {
	struct nfs_fh		coa_fh;
	nfs4_stateid		coa_stateid;
	uint32_t		error;
	uint64_t		wr_count;
	struct nfs_writeverf	wr_writeverf;
};

extern __be32 nfs4_callback_offload(void *args, void *dummy,
				    struct cb_process_state *cps);
#endif /* CONFIG_NFS_V4_2 */
extern int check_gss_callback_principal(struct nfs_client *, struct svc_rqst *);
extern __be32 nfs4_callback_getattr(void *argp, void *resp,
				    struct cb_process_state *cps);
extern __be32 nfs4_callback_recall(void *argp, void *resp,
				   struct cb_process_state *cps);
#if IS_ENABLED(CONFIG_NFS_V4)
extern int nfs_callback_up(u32 minorversion, struct rpc_xprt *xprt);
extern void nfs_callback_down(int minorversion, struct net *net);
#endif /* CONFIG_NFS_V4 */
/*
 * nfs41: Callbacks are expected to not cause substantial latency,
 * so we limit their concurrency to 1 by setting up the maximum number
 * of slots for the backchannel.
 */
#define NFS41_BC_MIN_CALLBACKS 1
#define NFS41_BC_MAX_CALLBACKS 1

#define NFS4_MIN_NR_CALLBACK_THREADS 1

extern unsigned int nfs_callback_set_tcpport;
extern unsigned short nfs_callback_nr_threads;

#endif /* __LINUX_FS_NFS_CALLBACK_H */
