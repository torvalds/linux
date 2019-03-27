/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 *
 * $FreeBSD$
 */

/* VMCI QueuePair API definition. */

#ifndef _VMCI_QUEUE_PAIR_H_
#define _VMCI_QUEUE_PAIR_H_

#include "vmci_kernel_if.h"
#include "vmci_queue.h"

int	vmci_qp_guest_endpoints_init(void);
void	vmci_qp_guest_endpoints_exit(void);
void	vmci_qp_guest_endpoints_sync(void);
void	vmci_qp_guest_endpoints_convert(bool to_local, bool device_reset);

int	vmci_queue_pair_alloc(struct vmci_handle *handle,
	    struct vmci_queue **produce_q, uint64_t produce_size,
	    struct vmci_queue **consume_q, uint64_t consume_size,
	    vmci_id peer, uint32_t flags, vmci_privilege_flags priv_flags);
int	vmci_queue_pair_detach(struct vmci_handle handle);

#endif /* !_VMCI_QUEUE_PAIR_H_ */
