/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 *
 * $FreeBSD$
 */

/* Kernel API (v1) exported from the VMCI guest driver. */

#ifndef _VMCI_KERNEL_API_1_H_
#define _VMCI_KERNEL_API_1_H_

#include "vmci_call_defs.h"
#include "vmci_defs.h"

/* Define version 1. */
#undef  VMCI_KERNEL_API_VERSION
#define VMCI_KERNEL_API_VERSION_1	1
#define VMCI_KERNEL_API_VERSION		VMCI_KERNEL_API_VERSION_1

/* VMCI Datagram API. */
int	vmci_datagram_create_handle(uint32_t resource_id, uint32_t flags,
	    vmci_datagram_recv_cb recv_cb, void *client_data,
	    struct vmci_handle *out_handle);
int	vmci_datagram_create_handle_priv(uint32_t resource_id, uint32_t flags,
	    vmci_privilege_flags priv_flags, vmci_datagram_recv_cb recv_cb,
	    void *client_data, struct vmci_handle *out_handle);
int	vmci_datagram_destroy_handle(struct vmci_handle handle);
int	vmci_datagram_send(struct vmci_datagram *msg);

/* VMCI Utility API. */
vmci_id vmci_get_context_id(void);

/* VMCI Event API. */
typedef void (*vmci_event_cb)(vmci_id sub_id, struct vmci_event_data *ed,
		    void *client_data);

int	vmci_event_subscribe(vmci_event_type event, vmci_event_cb callback,
	    void *callback_data, vmci_id *sub_id);
int	vmci_event_unsubscribe(vmci_id sub_id);

/* VMCI Queue Pair API. */
struct vmci_qpair;

int	vmci_qpair_alloc(struct vmci_qpair **qpair, struct vmci_handle *handle,
	    uint64_t produce_q_size, uint64_t consume_q_size, vmci_id peer,
	    uint32_t flags, vmci_privilege_flags priv_flags);
int	vmci_qpair_detach(struct vmci_qpair **qpair);
int	vmci_qpair_get_produce_indexes(const struct vmci_qpair *qpair,
	    uint64_t *producer_tail, uint64_t *consumer_head);
int	vmci_qpair_get_consume_indexes(const struct vmci_qpair *qpair,
	    uint64_t *consumer_tail, uint64_t *producer_head);
int64_t	vmci_qpair_produce_free_space(const struct vmci_qpair *qpair);
int64_t	vmci_qpair_produce_buf_ready(const struct vmci_qpair *qpair);
int64_t	vmci_qpair_consume_free_space(const struct vmci_qpair *qpair);
int64_t	vmci_qpair_consume_buf_ready(const struct vmci_qpair *qpair);
ssize_t	vmci_qpair_enqueue(struct vmci_qpair *qpair, const void *buf,
	    size_t buf_size, int mode);
ssize_t	vmci_qpair_dequeue(struct vmci_qpair *qpair, void *buf,
	    size_t buf_size, int mode);
ssize_t vmci_qpair_peek(struct vmci_qpair *qpair, void *buf,
	    size_t buf_size, int mode);
ssize_t	vmci_qpair_enquev(struct vmci_qpair *qpair, void *iov, size_t iov_size,
	    int mode);
ssize_t	vmci_qpair_dequev(struct vmci_qpair *qpair, void *iov, size_t iov_size,
	    int mode);
ssize_t	vmci_qpair_peekv(struct vmci_qpair *qpair, void *iov, size_t iov_size,
	    int mode);

#endif /* !_VMCI_KERNEL_API_1_H_ */
