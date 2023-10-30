/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/nfsd.yaml */
/* YNL-GEN user header */

#ifndef _LINUX_NFSD_GEN_H
#define _LINUX_NFSD_GEN_H

#include <stdlib.h>
#include <string.h>
#include <linux/types.h>
#include <linux/nfsd_netlink.h>

struct ynl_sock;

extern const struct ynl_family ynl_nfsd_family;

/* Enums */
const char *nfsd_op_str(int op);

/* Common nested types */
/* ============== NFSD_CMD_RPC_STATUS_GET ============== */
/* NFSD_CMD_RPC_STATUS_GET - dump */
struct nfsd_rpc_status_get_list {
	struct nfsd_rpc_status_get_list *next;
	struct nfsd_rpc_status_get_rsp obj __attribute__ ((aligned (8)));
};

void nfsd_rpc_status_get_list_free(struct nfsd_rpc_status_get_list *rsp);

struct nfsd_rpc_status_get_list *nfsd_rpc_status_get_dump(struct ynl_sock *ys);

#endif /* _LINUX_NFSD_GEN_H */
