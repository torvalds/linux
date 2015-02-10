/*
 * VMware VMCI Driver
 *
 * Copyright (C) 2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation version 2 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#ifndef __VMW_VMCI_API_H__
#define __VMW_VMCI_API_H__

#include <linux/uidgid.h>
#include <linux/vmw_vmci_defs.h>

#undef  VMCI_KERNEL_API_VERSION
#define VMCI_KERNEL_API_VERSION_1 1
#define VMCI_KERNEL_API_VERSION_2 2
#define VMCI_KERNEL_API_VERSION   VMCI_KERNEL_API_VERSION_2

struct msghdr;
typedef void (vmci_device_shutdown_fn) (void *device_registration,
					void *user_data);

int vmci_datagram_create_handle(u32 resource_id, u32 flags,
				vmci_datagram_recv_cb recv_cb,
				void *client_data,
				struct vmci_handle *out_handle);
int vmci_datagram_create_handle_priv(u32 resource_id, u32 flags, u32 priv_flags,
				     vmci_datagram_recv_cb recv_cb,
				     void *client_data,
				     struct vmci_handle *out_handle);
int vmci_datagram_destroy_handle(struct vmci_handle handle);
int vmci_datagram_send(struct vmci_datagram *msg);
int vmci_doorbell_create(struct vmci_handle *handle, u32 flags,
			 u32 priv_flags,
			 vmci_callback notify_cb, void *client_data);
int vmci_doorbell_destroy(struct vmci_handle handle);
int vmci_doorbell_notify(struct vmci_handle handle, u32 priv_flags);
u32 vmci_get_context_id(void);
bool vmci_is_context_owner(u32 context_id, kuid_t uid);

int vmci_event_subscribe(u32 event,
			 vmci_event_cb callback, void *callback_data,
			 u32 *subid);
int vmci_event_unsubscribe(u32 subid);
u32 vmci_context_get_priv_flags(u32 context_id);
int vmci_qpair_alloc(struct vmci_qp **qpair,
		     struct vmci_handle *handle,
		     u64 produce_qsize,
		     u64 consume_qsize,
		     u32 peer, u32 flags, u32 priv_flags);
int vmci_qpair_detach(struct vmci_qp **qpair);
int vmci_qpair_get_produce_indexes(const struct vmci_qp *qpair,
				   u64 *producer_tail,
				   u64 *consumer_head);
int vmci_qpair_get_consume_indexes(const struct vmci_qp *qpair,
				   u64 *consumer_tail,
				   u64 *producer_head);
s64 vmci_qpair_produce_free_space(const struct vmci_qp *qpair);
s64 vmci_qpair_produce_buf_ready(const struct vmci_qp *qpair);
s64 vmci_qpair_consume_free_space(const struct vmci_qp *qpair);
s64 vmci_qpair_consume_buf_ready(const struct vmci_qp *qpair);
ssize_t vmci_qpair_enqueue(struct vmci_qp *qpair,
			   const void *buf, size_t buf_size, int mode);
ssize_t vmci_qpair_dequeue(struct vmci_qp *qpair,
			   void *buf, size_t buf_size, int mode);
ssize_t vmci_qpair_peek(struct vmci_qp *qpair, void *buf, size_t buf_size,
			int mode);
ssize_t vmci_qpair_enquev(struct vmci_qp *qpair,
			  void *iov, size_t iov_size, int mode);
ssize_t vmci_qpair_dequev(struct vmci_qp *qpair,
			  struct msghdr *msg, size_t iov_size, int mode);
ssize_t vmci_qpair_peekv(struct vmci_qp *qpair, struct msghdr *msg, size_t iov_size,
			 int mode);

#endif /* !__VMW_VMCI_API_H__ */
