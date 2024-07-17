/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
#ifndef _UAPI_MPTCP_H
#define _UAPI_MPTCP_H

#ifndef __KERNEL__
#include <netinet/in.h>		/* for sockaddr_in and sockaddr_in6	*/
#include <sys/socket.h>		/* for struct sockaddr			*/
#endif

#include <linux/const.h>
#include <linux/types.h>
#include <linux/in.h>		/* for sockaddr_in			*/
#include <linux/in6.h>		/* for sockaddr_in6			*/
#include <linux/socket.h>	/* for sockaddr_storage and sa_family	*/

#define MPTCP_SUBFLOW_FLAG_MCAP_REM		_BITUL(0)
#define MPTCP_SUBFLOW_FLAG_MCAP_LOC		_BITUL(1)
#define MPTCP_SUBFLOW_FLAG_JOIN_REM		_BITUL(2)
#define MPTCP_SUBFLOW_FLAG_JOIN_LOC		_BITUL(3)
#define MPTCP_SUBFLOW_FLAG_BKUP_REM		_BITUL(4)
#define MPTCP_SUBFLOW_FLAG_BKUP_LOC		_BITUL(5)
#define MPTCP_SUBFLOW_FLAG_FULLY_ESTABLISHED	_BITUL(6)
#define MPTCP_SUBFLOW_FLAG_CONNECTED		_BITUL(7)
#define MPTCP_SUBFLOW_FLAG_MAPVALID		_BITUL(8)

#define MPTCP_PM_CMD_GRP_NAME	"mptcp_pm_cmds"
#define MPTCP_PM_EV_GRP_NAME	"mptcp_pm_events"

#include <linux/mptcp_pm.h>

#define MPTCP_INFO_FLAG_FALLBACK		_BITUL(0)
#define MPTCP_INFO_FLAG_REMOTE_KEY_RECEIVED	_BITUL(1)

#define MPTCP_PM_ADDR_FLAG_SIGNAL                      (1 << 0)
#define MPTCP_PM_ADDR_FLAG_SUBFLOW                     (1 << 1)
#define MPTCP_PM_ADDR_FLAG_BACKUP                      (1 << 2)
#define MPTCP_PM_ADDR_FLAG_FULLMESH                    (1 << 3)
#define MPTCP_PM_ADDR_FLAG_IMPLICIT                    (1 << 4)

struct mptcp_info {
	__u8	mptcpi_subflows;
	__u8	mptcpi_add_addr_signal;
	__u8	mptcpi_add_addr_accepted;
	__u8	mptcpi_subflows_max;
	__u8	mptcpi_add_addr_signal_max;
	__u8	mptcpi_add_addr_accepted_max;
	__u32	mptcpi_flags;
	__u32	mptcpi_token;
	__u64	mptcpi_write_seq;
	__u64	mptcpi_snd_una;
	__u64	mptcpi_rcv_nxt;
	__u8	mptcpi_local_addr_used;
	__u8	mptcpi_local_addr_max;
	__u8	mptcpi_csum_enabled;
	__u32	mptcpi_retransmits;
	__u64	mptcpi_bytes_retrans;
	__u64	mptcpi_bytes_sent;
	__u64	mptcpi_bytes_received;
	__u64	mptcpi_bytes_acked;
	__u8	mptcpi_subflows_total;
	__u8	reserved[3];
	__u32	mptcpi_last_data_sent;
	__u32	mptcpi_last_data_recv;
	__u32	mptcpi_last_ack_recv;
};

/* MPTCP Reset reason codes, rfc8684 */
#define MPTCP_RST_EUNSPEC	0
#define MPTCP_RST_EMPTCP	1
#define MPTCP_RST_ERESOURCE	2
#define MPTCP_RST_EPROHIBIT	3
#define MPTCP_RST_EWQ2BIG	4
#define MPTCP_RST_EBADPERF	5
#define MPTCP_RST_EMIDDLEBOX	6

struct mptcp_subflow_data {
	__u32		size_subflow_data;		/* size of this structure in userspace */
	__u32		num_subflows;			/* must be 0, set by kernel */
	__u32		size_kernel;			/* must be 0, set by kernel */
	__u32		size_user;			/* size of one element in data[] */
} __attribute__((aligned(8)));

struct mptcp_subflow_addrs {
	union {
		__kernel_sa_family_t sa_family;
		struct sockaddr sa_local;
		struct sockaddr_in sin_local;
		struct sockaddr_in6 sin6_local;
		struct __kernel_sockaddr_storage ss_local;
	};
	union {
		struct sockaddr sa_remote;
		struct sockaddr_in sin_remote;
		struct sockaddr_in6 sin6_remote;
		struct __kernel_sockaddr_storage ss_remote;
	};
};

struct mptcp_subflow_info {
	__u32				id;
	struct mptcp_subflow_addrs	addrs;
};

struct mptcp_full_info {
	__u32		size_tcpinfo_kernel;	/* must be 0, set by kernel */
	__u32		size_tcpinfo_user;
	__u32		size_sfinfo_kernel;	/* must be 0, set by kernel */
	__u32		size_sfinfo_user;
	__u32		num_subflows;		/* must be 0, set by kernel (real subflow count) */
	__u32		size_arrays_user;	/* max subflows that userspace is interested in;
						 * the buffers at subflow_info/tcp_info
						 * are respectively at least:
						 *  size_arrays * size_sfinfo_user
						 *  size_arrays * size_tcpinfo_user
						 * bytes wide
						 */
	__aligned_u64		subflow_info;
	__aligned_u64		tcp_info;
	struct mptcp_info	mptcp_info;
};

/* MPTCP socket options */
#define MPTCP_INFO		1
#define MPTCP_TCPINFO		2
#define MPTCP_SUBFLOW_ADDRS	3
#define MPTCP_FULL_INFO		4

#endif /* _UAPI_MPTCP_H */
