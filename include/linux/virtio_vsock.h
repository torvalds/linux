/*
 * This header, excluding the #ifdef __KERNEL__ part, is BSD licensed so
 * anyone can use the definitions to implement compatible drivers/servers:
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Copyright (C) Red Hat, Inc., 2013-2015
 * Copyright (C) Asias He <asias@redhat.com>, 2013
 * Copyright (C) Stefan Hajnoczi <stefanha@redhat.com>, 2015
 */

#ifndef _LINUX_VIRTIO_VSOCK_H
#define _LINUX_VIRTIO_VSOCK_H

#include <uapi/linux/virtio_vsock.h>
#include <linux/socket.h>
#include <net/sock.h>

#define VIRTIO_VSOCK_DEFAULT_MIN_BUF_SIZE	128
#define VIRTIO_VSOCK_DEFAULT_BUF_SIZE		(1024 * 256)
#define VIRTIO_VSOCK_DEFAULT_MAX_BUF_SIZE	(1024 * 256)
#define VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE	(1024 * 4)
#define VIRTIO_VSOCK_MAX_BUF_SIZE		0xFFFFFFFFUL
#define VIRTIO_VSOCK_MAX_PKT_BUF_SIZE		(1024 * 64)
#define VIRTIO_VSOCK_MAX_TX_BUF_SIZE		(1024 * 1024 * 16)
#define VIRTIO_VSOCK_MAX_DGRAM_SIZE		(1024 * 64)

struct vsock_transport_recv_notify_data;
struct vsock_transport_send_notify_data;
struct sockaddr_vm;
struct vsock_sock;

enum {
	VSOCK_VQ_CTRL	= 0,
	VSOCK_VQ_RX	= 1, /* for host to guest data */
	VSOCK_VQ_TX	= 2, /* for guest to host data */
	VSOCK_VQ_MAX	= 3,
};

/* virtio transport socket state */
struct virtio_transport {
	struct virtio_transport_pkt_ops	*ops;
	struct vsock_sock *vsk;

	u32 buf_size;
	u32 buf_size_min;
	u32 buf_size_max;

	struct mutex tx_lock;
	struct mutex rx_lock;

	struct list_head rx_queue;
	u32 rx_bytes;

	/* Protected by trans->tx_lock */
	u32 tx_cnt;
	u32 buf_alloc;
	u32 peer_fwd_cnt;
	u32 peer_buf_alloc;
	/* Protected by trans->rx_lock */
	u32 fwd_cnt;

	/* Protected by sk_lock */
	u16 dgram_id;
	struct list_head incomplete_dgrams; /* dgram fragments */
};

struct virtio_vsock_pkt {
	struct virtio_vsock_hdr	hdr;
	struct virtio_transport	*trans;
	struct work_struct work;
	struct list_head list;
	void *buf;
	u32 len;
	u32 off;
};

struct virtio_vsock_pkt_info {
	u32 remote_cid, remote_port;
	struct msghdr *msg;
	u32 pkt_len;
	u16 type;
	u16 op;
	u32 flags;
	u16 dgram_id;
	u16 dgram_len;
};

struct virtio_transport_pkt_ops {
	int (*send_pkt)(struct vsock_sock *vsk,
			struct virtio_vsock_pkt_info *info);
};

void virtio_vsock_dumppkt(const char *func,
			  const struct virtio_vsock_pkt *pkt);

struct sock *
virtio_transport_get_pending(struct sock *listener,
			     struct virtio_vsock_pkt *pkt);
struct virtio_vsock_pkt *
virtio_transport_alloc_pkt(struct vsock_sock *vsk,
			   struct virtio_vsock_pkt_info *info,
			   size_t len,
			   u32 src_cid,
			   u32 src_port,
			   u32 dst_cid,
			   u32 dst_port);
ssize_t
virtio_transport_stream_dequeue(struct vsock_sock *vsk,
				struct msghdr *msg,
				size_t len,
				int type);
int
virtio_transport_dgram_dequeue(struct vsock_sock *vsk,
			       struct msghdr *msg,
			       size_t len, int flags);

s64 virtio_transport_stream_has_data(struct vsock_sock *vsk);
s64 virtio_transport_stream_has_space(struct vsock_sock *vsk);

int virtio_transport_do_socket_init(struct vsock_sock *vsk,
				 struct vsock_sock *psk);
u64 virtio_transport_get_buffer_size(struct vsock_sock *vsk);
u64 virtio_transport_get_min_buffer_size(struct vsock_sock *vsk);
u64 virtio_transport_get_max_buffer_size(struct vsock_sock *vsk);
void virtio_transport_set_buffer_size(struct vsock_sock *vsk, u64 val);
void virtio_transport_set_min_buffer_size(struct vsock_sock *vsk, u64 val);
void virtio_transport_set_max_buffer_size(struct vsock_sock *vs, u64 val);
int
virtio_transport_notify_poll_in(struct vsock_sock *vsk,
				size_t target,
				bool *data_ready_now);
int
virtio_transport_notify_poll_out(struct vsock_sock *vsk,
				 size_t target,
				 bool *space_available_now);

int virtio_transport_notify_recv_init(struct vsock_sock *vsk,
	size_t target, struct vsock_transport_recv_notify_data *data);
int virtio_transport_notify_recv_pre_block(struct vsock_sock *vsk,
	size_t target, struct vsock_transport_recv_notify_data *data);
int virtio_transport_notify_recv_pre_dequeue(struct vsock_sock *vsk,
	size_t target, struct vsock_transport_recv_notify_data *data);
int virtio_transport_notify_recv_post_dequeue(struct vsock_sock *vsk,
	size_t target, ssize_t copied, bool data_read,
	struct vsock_transport_recv_notify_data *data);
int virtio_transport_notify_send_init(struct vsock_sock *vsk,
	struct vsock_transport_send_notify_data *data);
int virtio_transport_notify_send_pre_block(struct vsock_sock *vsk,
	struct vsock_transport_send_notify_data *data);
int virtio_transport_notify_send_pre_enqueue(struct vsock_sock *vsk,
	struct vsock_transport_send_notify_data *data);
int virtio_transport_notify_send_post_enqueue(struct vsock_sock *vsk,
	ssize_t written, struct vsock_transport_send_notify_data *data);

u64 virtio_transport_stream_rcvhiwat(struct vsock_sock *vsk);
bool virtio_transport_stream_is_active(struct vsock_sock *vsk);
bool virtio_transport_stream_allow(u32 cid, u32 port);
int virtio_transport_dgram_bind(struct vsock_sock *vsk,
				struct sockaddr_vm *addr);
bool virtio_transport_dgram_allow(u32 cid, u32 port);

int virtio_transport_connect(struct vsock_sock *vsk);

int virtio_transport_shutdown(struct vsock_sock *vsk, int mode);

void virtio_transport_release(struct vsock_sock *vsk);

ssize_t
virtio_transport_stream_enqueue(struct vsock_sock *vsk,
				struct msghdr *msg,
				size_t len);
int
virtio_transport_dgram_enqueue(struct vsock_sock *vsk,
			       struct sockaddr_vm *remote_addr,
			       struct msghdr *msg,
			       size_t len);

void virtio_transport_destruct(struct vsock_sock *vsk);

void virtio_transport_recv_pkt(struct virtio_vsock_pkt *pkt);
void virtio_transport_free_pkt(struct virtio_vsock_pkt *pkt);
void virtio_transport_inc_tx_pkt(struct virtio_vsock_pkt *pkt);
void virtio_transport_dec_tx_pkt(struct virtio_vsock_pkt *pkt);
u32 virtio_transport_get_credit(struct virtio_transport *trans, u32 wanted);
void virtio_transport_put_credit(struct virtio_transport *trans, u32 credit);
#endif /* _LINUX_VIRTIO_VSOCK_H */
