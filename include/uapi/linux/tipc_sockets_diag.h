/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* AF_TIPC sock_diag interface for querying open sockets */

#ifndef _UAPI__TIPC_SOCKETS_DIAG_H__
#define _UAPI__TIPC_SOCKETS_DIAG_H__

#include <linux/types.h>
#include <linux/sock_diag.h>

/* Request */
struct tipc_sock_diag_req {
	__u8	sdiag_family;	/* must be AF_TIPC */
	__u8	sdiag_protocol;	/* must be 0 */
	__u16	pad;		/* must be 0 */
	__u32	tidiag_states;	/* query*/
};
#endif /* _UAPI__TIPC_SOCKETS_DIAG_H__ */
