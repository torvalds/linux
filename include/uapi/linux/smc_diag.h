/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_SMC_DIAG_H_
#define _UAPI_SMC_DIAG_H_

#include <linux/types.h>
#include <linux/inet_diag.h>
#include <rdma/ib_user_verbs.h>

/* Request structure */
struct smc_diag_req {
	__u8	diag_family;
	__u8	pad[2];
	__u8	diag_ext;		/* Query extended information */
	struct inet_diag_sockid	id;
};

/* Base info structure. It contains socket identity (addrs/ports/cookie) based
 * on the internal clcsock, and more SMC-related socket data
 */
struct smc_diag_msg {
	__u8	diag_family;
	__u8	diag_state;
	__u8	diag_fallback;
	__u8	diag_shutdown;
	struct inet_diag_sockid id;

	__u32	diag_uid;
	__u64	diag_inode;
};

/* Extensions */

enum {
	SMC_DIAG_NONE,
	SMC_DIAG_CONNINFO,
	SMC_DIAG_LGRINFO,
	SMC_DIAG_SHUTDOWN,
	__SMC_DIAG_MAX,
};

#define SMC_DIAG_MAX (__SMC_DIAG_MAX - 1)

/* SMC_DIAG_CONNINFO */

struct smc_diag_cursor {
	__u16	reserved;
	__u16	wrap;
	__u32	count;
};

struct smc_diag_conninfo {
	__u32			token;		/* unique connection id */
	__u32			sndbuf_size;	/* size of send buffer */
	__u32			rmbe_size;	/* size of RMB element */
	__u32			peer_rmbe_size;	/* size of peer RMB element */
	/* local RMB element cursors */
	struct smc_diag_cursor	rx_prod;	/* received producer cursor */
	struct smc_diag_cursor	rx_cons;	/* received consumer cursor */
	/* peer RMB element cursors */
	struct smc_diag_cursor	tx_prod;	/* sent producer cursor */
	struct smc_diag_cursor	tx_cons;	/* sent consumer cursor */
	__u8			rx_prod_flags;	/* received producer flags */
	__u8			rx_conn_state_flags; /* recvd connection flags*/
	__u8			tx_prod_flags;	/* sent producer flags */
	__u8			tx_conn_state_flags; /* sent connection flags*/
	/* send buffer cursors */
	struct smc_diag_cursor	tx_prep;	/* prepared to be sent cursor */
	struct smc_diag_cursor	tx_sent;	/* sent cursor */
	struct smc_diag_cursor	tx_fin;		/* confirmed sent cursor */
};

/* SMC_DIAG_LINKINFO */

struct smc_diag_linkinfo {
	__u8 link_id;			/* link identifier */
	__u8 ibname[IB_DEVICE_NAME_MAX]; /* name of the RDMA device */
	__u8 ibport;			/* RDMA device port number */
	__u8 gid[40];			/* local GID */
	__u8 peer_gid[40];		/* peer GID */
};

struct smc_diag_lgrinfo {
	struct smc_diag_linkinfo	lnk[1];
	__u8				role;
};
#endif /* _UAPI_SMC_DIAG_H_ */
