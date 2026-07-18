/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2025, Stefan Metzmacher
 */

#ifndef __LINUX_SMBDIRECT_H__
#define __LINUX_SMBDIRECT_H__

#include <linux/types.h>

/* SMB-DIRECT buffer descriptor V1 structure [MS-SMBD] 2.2.3.1 */
struct smbdirect_buffer_descriptor_v1 {
	__le64 offset;
	__le32 token;
	__le32 length;
} __packed;

/*
 * Connection parameters mostly from [MS-SMBD] 3.1.1.1
 *
 * These are setup and negotiated at the beginning of a
 * connection and remain constant unless explicitly changed.
 *
 * Some values are important for the upper layer.
 */
struct smbdirect_socket_parameters {
	__u64 flags;
#define SMBDIRECT_FLAG_PORT_RANGE_ONLY_IB ((__u64)0x1)
#define SMBDIRECT_FLAG_PORT_RANGE_ONLY_IW ((__u64)0x2)
	__u32 resolve_addr_timeout_msec;
	__u32 resolve_route_timeout_msec;
	__u32 rdma_connect_timeout_msec;
	__u32 negotiate_timeout_msec;
	__u16 initiator_depth;     /* limited to U8_MAX */
	__u16 responder_resources; /* limited to U8_MAX */
	__u16 recv_credit_max;
	__u16 send_credit_target;
	__u32 max_send_size;
	__u32 max_fragmented_send_size;
	__u32 max_recv_size;
	__u32 max_fragmented_recv_size;
	__u32 max_read_write_size;
	__u32 max_frmr_depth;
	__u32 keepalive_interval_msec;
	__u32 keepalive_timeout_msec;
} __packed;

#define SMBDIRECT_FLAG_PORT_RANGE_MASK ( \
		SMBDIRECT_FLAG_PORT_RANGE_ONLY_IB | \
		SMBDIRECT_FLAG_PORT_RANGE_ONLY_IW)

struct smbdirect_socket;
struct smbdirect_send_batch;
struct smbdirect_mr_io;

#include <rdma/rw.h>

u8 smbdirect_netdev_rdma_capable_node_type(struct net_device *netdev);

bool smbdirect_frwr_is_supported(const struct ib_device_attr *attrs);

int smbdirect_socket_create_kern(struct net *net, struct smbdirect_socket **_sc);

int smbdirect_socket_create_accepting(struct rdma_cm_id *id, struct smbdirect_socket **_sc);

int smbdirect_socket_set_initial_parameters(struct smbdirect_socket *sc,
					    const struct smbdirect_socket_parameters *sp);

const struct smbdirect_socket_parameters *
smbdirect_socket_get_current_parameters(struct smbdirect_socket *sc);

int smbdirect_socket_set_kernel_settings(struct smbdirect_socket *sc,
					 enum ib_poll_context poll_ctx,
					 gfp_t gfp_mask);

#define SMBDIRECT_LOG_ERR		0x0
#define SMBDIRECT_LOG_INFO		0x1

#define SMBDIRECT_LOG_OUTGOING			0x1
#define SMBDIRECT_LOG_INCOMING			0x2
#define SMBDIRECT_LOG_READ			0x4
#define SMBDIRECT_LOG_WRITE			0x8
#define SMBDIRECT_LOG_RDMA_SEND			0x10
#define SMBDIRECT_LOG_RDMA_RECV			0x20
#define SMBDIRECT_LOG_KEEP_ALIVE		0x40
#define SMBDIRECT_LOG_RDMA_EVENT		0x80
#define SMBDIRECT_LOG_RDMA_MR			0x100
#define SMBDIRECT_LOG_RDMA_RW			0x200
#define SMBDIRECT_LOG_NEGOTIATE			0x400
void smbdirect_socket_set_logging(struct smbdirect_socket *sc,
				  void *private_ptr,
				  bool (*needed)(struct smbdirect_socket *sc,
						 void *private_ptr,
						 unsigned int lvl,
						 unsigned int cls),
				  void (*vaprintf)(struct smbdirect_socket *sc,
						   const char *func,
						   unsigned int line,
						   void *private_ptr,
						   unsigned int lvl,
						   unsigned int cls,
						   struct va_format *vaf));

bool smbdirect_connection_is_connected(struct smbdirect_socket *sc);

int smbdirect_connection_wait_for_connected(struct smbdirect_socket *sc);

int smbdirect_socket_bind(struct smbdirect_socket *sc, struct sockaddr *addr);

void smbdirect_socket_shutdown(struct smbdirect_socket *sc);

void smbdirect_socket_release(struct smbdirect_socket *sc);

int smbdirect_connection_send_batch_flush(struct smbdirect_socket *sc,
					  struct smbdirect_send_batch *batch,
					  bool is_last);

/*
 * This is only temporary and only needed
 * as long as the client still requires
 * to use smbdirect_connection_send_single_iter()
 */
struct smbdirect_send_batch_storage {
	union {
		struct list_head __msg_list;
		__aligned_u64 __space[5];
	};
};

struct smbdirect_send_batch *
smbdirect_init_send_batch_storage(struct smbdirect_send_batch_storage *storage,
				  bool need_invalidate_rkey,
				  unsigned int remote_key);

int smbdirect_connection_send_single_iter(struct smbdirect_socket *sc,
					  struct smbdirect_send_batch *batch,
					  struct iov_iter *iter,
					  unsigned int flags,
					  u32 remaining_data_length);

int smbdirect_connection_send_wait_zero_pending(struct smbdirect_socket *sc);

int smbdirect_connection_send_iter(struct smbdirect_socket *sc,
				   struct iov_iter *iter,
				   unsigned int flags,
				   bool need_invalidate,
				   unsigned int remote_key);

int smbdirect_connection_recvmsg(struct smbdirect_socket *sc,
				 struct msghdr *msg,
				 unsigned int flags);

int smbdirect_connect(struct smbdirect_socket *sc,
		      const struct sockaddr *dst);

int smbdirect_connect_sync(struct smbdirect_socket *sc,
			   const struct sockaddr *dst);

int smbdirect_socket_listen(struct smbdirect_socket *sc, int backlog);

struct smbdirect_socket *smbdirect_socket_accept(struct smbdirect_socket *lsc,
						 long timeo,
						 struct proto_accept_arg *arg);

int smbdirect_connection_rdma_xmit(struct smbdirect_socket *sc,
				   void *buf, size_t buf_len,
				   struct smbdirect_buffer_descriptor_v1 *desc,
				   size_t desc_len,
				   bool is_read);

struct smbdirect_mr_io *
smbdirect_connection_register_mr_io(struct smbdirect_socket *sc,
				    struct iov_iter *iter,
				    bool writing,
				    bool need_invalidate);

void smbdirect_mr_io_fill_buffer_descriptor(struct smbdirect_mr_io *mr,
					    struct smbdirect_buffer_descriptor_v1 *v1);

void smbdirect_connection_deregister_mr_io(struct smbdirect_mr_io *mr);

void smbdirect_connection_legacy_debug_proc_show(struct smbdirect_socket *sc,
						 unsigned int rdma_readwrite_threshold,
						 struct seq_file *m);

#endif /* __LINUX_SMBDIRECT_H__ */
