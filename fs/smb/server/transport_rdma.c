// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *   Copyright (C) 2018, LG Electronics.
 *
 *   Author(s): Long Li <longli@microsoft.com>,
 *		Hyunchul Lee <hyc.lee@gmail.com>
 */

#define SUBMOD_NAME	"smb_direct"

#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/string_choices.h>

#include "glob.h"
#include "connection.h"
#include "smb_common.h"
#include "../common/smb2status.h"
#include "transport_rdma.h"
#include "../common/smbdirect/smbdirect_public.h"


#define SMB_DIRECT_PORT_IWARP		5445
#define SMB_DIRECT_PORT_INFINIBAND	445

/* SMB_DIRECT negotiation timeout (for the server) in seconds */
#define SMB_DIRECT_NEGOTIATE_TIMEOUT		5

/* The timeout to wait for a keepalive message from peer in seconds */
#define SMB_DIRECT_KEEPALIVE_SEND_INTERVAL	120

/* The timeout to wait for a keepalive message from peer in seconds */
#define SMB_DIRECT_KEEPALIVE_RECV_TIMEOUT	5

/*
 * Default maximum number of RDMA read/write outstanding on this connection
 * This value is possibly decreased during QP creation on hardware limit
 */
#define SMB_DIRECT_CM_INITIATOR_DEPTH		8

/*
 * User configurable initial values per SMB_DIRECT transport connection
 * as defined in [MS-SMBD] 3.1.1.1
 * Those may change after a SMB_DIRECT negotiation
 */

/* The local peer's maximum number of credits to grant to the peer */
static int smb_direct_receive_credit_max = 255;

/* The remote peer's credit request of local peer */
static int smb_direct_send_credit_target = 255;

/* The maximum single message size can be sent to remote peer */
static int smb_direct_max_send_size = 1364;

/*
 * The maximum fragmented upper-layer payload receive size supported
 *
 * Assume max_payload_per_credit is
 * smb_direct_receive_credit_max - 24 = 1340
 *
 * The maximum number would be
 * smb_direct_receive_credit_max * max_payload_per_credit
 *
 *                       1340 * 255 = 341700 (0x536C4)
 *
 * The minimum value from the spec is 131072 (0x20000)
 *
 * For now we use the logic we used before:
 *                 (1364 * 255) / 2 = 173910 (0x2A756)
 */
static int smb_direct_max_fragmented_recv_size = (1364 * 255) / 2;

/*  The maximum single-message size which can be received */
static int smb_direct_max_receive_size = 1364;

static int smb_direct_max_read_write_size = SMBD_DEFAULT_IOSIZE;

static struct smb_direct_listener {
	int			port;

	struct task_struct	*thread;

	struct smbdirect_socket *socket;
} smb_direct_ib_listener, smb_direct_iw_listener;

static struct workqueue_struct *smb_direct_wq;

struct smb_direct_transport {
	struct ksmbd_transport	transport;

	struct smbdirect_socket *socket;
};

static bool smb_direct_logging_needed(struct smbdirect_socket *sc,
				      void *private_ptr,
				      unsigned int lvl,
				      unsigned int cls)
{
	if (lvl <= SMBDIRECT_LOG_ERR)
		return true;

	if (lvl > SMBDIRECT_LOG_INFO)
		return false;

	switch (cls) {
	/*
	 * These were more or less also logged before
	 * the move to common code.
	 *
	 * SMBDIRECT_LOG_RDMA_MR was not used, but
	 * that's client only code and we should
	 * notice if it's used on the server...
	 */
	case SMBDIRECT_LOG_RDMA_EVENT:
	case SMBDIRECT_LOG_RDMA_SEND:
	case SMBDIRECT_LOG_RDMA_RECV:
	case SMBDIRECT_LOG_WRITE:
	case SMBDIRECT_LOG_READ:
	case SMBDIRECT_LOG_NEGOTIATE:
	case SMBDIRECT_LOG_OUTGOING:
	case SMBDIRECT_LOG_RDMA_RW:
	case SMBDIRECT_LOG_RDMA_MR:
		return true;
	/*
	 * These were not logged before the move
	 * to common code.
	 */
	case SMBDIRECT_LOG_KEEP_ALIVE:
	case SMBDIRECT_LOG_INCOMING:
		return false;
	}

	/*
	 * Log all unknown messages
	 */
	return true;
}

static void smb_direct_logging_vaprintf(struct smbdirect_socket *sc,
					const char *func,
					unsigned int line,
					void *private_ptr,
					unsigned int lvl,
					unsigned int cls,
					struct va_format *vaf)
{
	if (lvl <= SMBDIRECT_LOG_ERR)
		pr_err("%pV", vaf);
	else
		ksmbd_debug(RDMA, "%pV", vaf);
}

#define KSMBD_TRANS(t) (&(t)->transport)
#define SMBD_TRANS(t)	(container_of(t, \
				struct smb_direct_transport, transport))

static const struct ksmbd_transport_ops ksmbd_smb_direct_transport_ops;

void init_smbd_max_io_size(unsigned int sz)
{
	sz = clamp_val(sz, SMBD_MIN_IOSIZE, SMBD_MAX_IOSIZE);
	smb_direct_max_read_write_size = sz;
}

unsigned int get_smbd_max_read_write_size(struct ksmbd_transport *kt)
{
	struct smb_direct_transport *t;
	const struct smbdirect_socket_parameters *sp;

	if (kt->ops != &ksmbd_smb_direct_transport_ops)
		return 0;

	t = SMBD_TRANS(kt);
	sp = smbdirect_socket_get_current_parameters(t->socket);

	return sp->max_read_write_size;
}

static struct smb_direct_transport *alloc_transport(struct smbdirect_socket *sc)
{
	struct smb_direct_transport *t;
	struct ksmbd_conn *conn;

	t = kzalloc_obj(*t, KSMBD_DEFAULT_GFP);
	if (!t)
		return NULL;
	t->socket = sc;

	conn = ksmbd_conn_alloc();
	if (!conn)
		goto conn_alloc_failed;

	down_write(&conn_list_lock);
	hash_add(conn_list, &conn->hlist, 0);
	up_write(&conn_list_lock);

	conn->transport = KSMBD_TRANS(t);
	KSMBD_TRANS(t)->conn = conn;
	KSMBD_TRANS(t)->ops = &ksmbd_smb_direct_transport_ops;

	return t;

conn_alloc_failed:
	kfree(t);
	return NULL;
}

static void smb_direct_free_transport(struct ksmbd_transport *kt)
{
	struct smb_direct_transport *t = SMBD_TRANS(kt);

	smbdirect_socket_release(t->socket);
	kfree(t);
}

static void free_transport(struct smb_direct_transport *t)
{
	smbdirect_socket_shutdown(t->socket);
	ksmbd_conn_free(KSMBD_TRANS(t)->conn);
}

static int smb_direct_read(struct ksmbd_transport *t, char *buf,
			   unsigned int size, int unused)
{
	struct smb_direct_transport *st = SMBD_TRANS(t);
	struct smbdirect_socket *sc = st->socket;
	struct msghdr msg = { .msg_flags = 0, };
	struct kvec iov = {
		.iov_base = buf,
		.iov_len = size,
	};
	int ret;

	iov_iter_kvec(&msg.msg_iter, ITER_DEST, &iov, 1, size);

	ret = smbdirect_connection_recvmsg(sc, &msg, 0);
	if (ret == -ERESTARTSYS)
		ret = -EINTR;
	return ret;
}

static int smb_direct_writev(struct ksmbd_transport *t,
			     struct kvec *iov, int niovs, int buflen,
			     bool need_invalidate, unsigned int remote_key)
{
	struct smb_direct_transport *st = SMBD_TRANS(t);
	struct smbdirect_socket *sc = st->socket;
	struct iov_iter iter;

	iov_iter_kvec(&iter, ITER_SOURCE, iov, niovs, buflen);

	return smbdirect_connection_send_iter(sc, &iter, 0,
					      need_invalidate, remote_key);
}

static int smb_direct_rdma_write(struct ksmbd_transport *t,
				 void *buf, unsigned int buflen,
				 struct smbdirect_buffer_descriptor_v1 *desc,
				 unsigned int desc_len)
{
	struct smb_direct_transport *st = SMBD_TRANS(t);
	struct smbdirect_socket *sc = st->socket;

	return smbdirect_connection_rdma_xmit(sc, buf, buflen,
					      desc, desc_len, false);
}

static int smb_direct_rdma_read(struct ksmbd_transport *t,
				void *buf, unsigned int buflen,
				struct smbdirect_buffer_descriptor_v1 *desc,
				unsigned int desc_len)
{
	struct smb_direct_transport *st = SMBD_TRANS(t);
	struct smbdirect_socket *sc = st->socket;

	return smbdirect_connection_rdma_xmit(sc, buf, buflen,
					      desc, desc_len, true);
}

static void smb_direct_disconnect(struct ksmbd_transport *t)
{
	struct smb_direct_transport *st = SMBD_TRANS(t);
	struct smbdirect_socket *sc = st->socket;

	ksmbd_debug(RDMA, "Disconnecting sc=%p\n", sc);

	free_transport(st);
}

static void smb_direct_shutdown(struct ksmbd_transport *t)
{
	struct smb_direct_transport *st = SMBD_TRANS(t);
	struct smbdirect_socket *sc = st->socket;

	ksmbd_debug(RDMA, "smb-direct shutdown sc=%p\n", sc);

	smbdirect_socket_shutdown(sc);
}

static int smb_direct_new_connection(struct smb_direct_listener *listener,
				     struct smbdirect_socket *client_sc)
{
	struct smb_direct_transport *t;
	struct task_struct *handler;
	int ret;

	t = alloc_transport(client_sc);
	if (!t) {
		smbdirect_socket_release(client_sc);
		return -ENOMEM;
	}

	handler = kthread_run(ksmbd_conn_handler_loop,
			      KSMBD_TRANS(t)->conn, "ksmbd:r%u",
			      listener->port);
	if (IS_ERR(handler)) {
		ret = PTR_ERR(handler);
		pr_err("Can't start thread\n");
		goto out_err;
	}

	return 0;
out_err:
	free_transport(t);
	return ret;
}

static int smb_direct_listener_kthread_fn(void *p)
{
	struct smb_direct_listener *listener = (struct smb_direct_listener *)p;
	struct smbdirect_socket *client_sc = NULL;

	while (!kthread_should_stop()) {
		struct proto_accept_arg arg = { .err = -EINVAL, };
		long timeo = MAX_SCHEDULE_TIMEOUT;

		if (!listener->socket)
			break;
		client_sc = smbdirect_socket_accept(listener->socket, timeo, &arg);
		if (!client_sc && arg.err == -EINVAL)
			break;
		if (!client_sc)
			continue;

		ksmbd_debug(CONN, "connect success: accepted new connection\n");
		smb_direct_new_connection(listener, client_sc);
	}

	ksmbd_debug(CONN, "releasing socket\n");
	return 0;
}

static void smb_direct_listener_destroy(struct smb_direct_listener *listener)
{
	int ret;

	if (listener->socket)
		smbdirect_socket_shutdown(listener->socket);

	if (listener->thread) {
		ret = kthread_stop(listener->thread);
		if (ret)
			pr_err("failed to stop forker thread\n");
		listener->thread = NULL;
	}

	if (listener->socket) {
		smbdirect_socket_release(listener->socket);
		listener->socket = NULL;
	}

	listener->port = 0;
}

static int smb_direct_listen(struct smb_direct_listener *listener,
			     int port)
{
	struct net *net = current->nsproxy->net_ns;
	struct task_struct *kthread;
	struct sockaddr_in sin = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= htonl(INADDR_ANY),
		.sin_port		= htons(port),
	};
	struct smbdirect_socket_parameters init_params = {};
	struct smbdirect_socket_parameters *sp;
	struct smbdirect_socket *sc;
	u64 port_flags = 0;
	int ret;

	switch (port) {
	case SMB_DIRECT_PORT_IWARP:
		/*
		 * only allow iWarp devices
		 * for port 5445.
		 */
		port_flags |= SMBDIRECT_FLAG_PORT_RANGE_ONLY_IW;
		break;
	case SMB_DIRECT_PORT_INFINIBAND:
		/*
		 * only allow InfiniBand, RoCEv1 or RoCEv2
		 * devices for port 445.
		 *
		 * (Basically don't allow iWarp devices)
		 */
		port_flags |= SMBDIRECT_FLAG_PORT_RANGE_ONLY_IB;
		break;
	default:
		pr_err("unsupported smbdirect port=%d!\n", port);
		return -ENODEV;
	}

	ret = smbdirect_socket_create_kern(net, &sc);
	if (ret) {
		pr_err("smbdirect_socket_create_kern() failed: %d %1pe\n",
		       ret, ERR_PTR(ret));
		return ret;
	}

	/*
	 * Create the initial parameters
	 */
	sp = &init_params;
	sp->flags |= port_flags;
	sp->negotiate_timeout_msec = SMB_DIRECT_NEGOTIATE_TIMEOUT * 1000;
	sp->initiator_depth = SMB_DIRECT_CM_INITIATOR_DEPTH;
	sp->responder_resources = 1;
	sp->recv_credit_max = smb_direct_receive_credit_max;
	sp->send_credit_target = smb_direct_send_credit_target;
	sp->max_send_size = smb_direct_max_send_size;
	sp->max_fragmented_recv_size = smb_direct_max_fragmented_recv_size;
	sp->max_recv_size = smb_direct_max_receive_size;
	sp->max_read_write_size = smb_direct_max_read_write_size;
	sp->keepalive_interval_msec = SMB_DIRECT_KEEPALIVE_SEND_INTERVAL * 1000;
	sp->keepalive_timeout_msec = SMB_DIRECT_KEEPALIVE_RECV_TIMEOUT * 1000;

	smbdirect_socket_set_logging(sc, NULL,
				     smb_direct_logging_needed,
				     smb_direct_logging_vaprintf);
	ret = smbdirect_socket_set_initial_parameters(sc, sp);
	if (ret) {
		pr_err("Failed smbdirect_socket_set_initial_parameters(): %d %1pe\n",
		       ret, ERR_PTR(ret));
		goto err;
	}
	ret = smbdirect_socket_set_kernel_settings(sc, IB_POLL_WORKQUEUE, KSMBD_DEFAULT_GFP);
	if (ret) {
		pr_err("Failed smbdirect_socket_set_kernel_settings(): %d %1pe\n",
		       ret, ERR_PTR(ret));
		goto err;
	}
	ret = smbdirect_socket_set_custom_workqueue(sc, smb_direct_wq);
	if (ret) {
		pr_err("Failed smbdirect_socket_set_custom_workqueue(): %d %1pe\n",
		       ret, ERR_PTR(ret));
		goto err;
	}

	ret = smbdirect_socket_bind(sc, (struct sockaddr *)&sin);
	if (ret) {
		pr_err("smbdirect_socket_bind() failed: %d %1pe\n",
		       ret, ERR_PTR(ret));
		goto err;
	}

	ret = smbdirect_socket_listen(sc, 10);
	if (ret) {
		pr_err("Port[%d] smbdirect_socket_listen() failed: %d %1pe\n",
		       port, ret, ERR_PTR(ret));
		goto err;
	}

	listener->port = port;
	listener->socket = sc;

	kthread = kthread_run(smb_direct_listener_kthread_fn,
			      listener,
			      "ksmbd-smbdirect-listener-%u", port);
	if (IS_ERR(kthread)) {
		ret = PTR_ERR(kthread);
		pr_err("Can't start ksmbd listen kthread: %d %1pe\n",
		       ret, ERR_PTR(ret));
		goto err;
	}

	listener->thread = kthread;
	return 0;
err:
	smb_direct_listener_destroy(listener);
	return ret;
}

int ksmbd_rdma_init(void)
{
	int ret;

	smb_direct_ib_listener = smb_direct_iw_listener = (struct smb_direct_listener) {
		.socket = NULL,
	};

	/* When a client is running out of send credits, the credits are
	 * granted by the server's sending a packet using this queue.
	 * This avoids the situation that a clients cannot send packets
	 * for lack of credits
	 */
	smb_direct_wq = alloc_workqueue("ksmbd-smb_direct-wq",
					WQ_HIGHPRI | WQ_MEM_RECLAIM | WQ_PERCPU,
					0);
	if (!smb_direct_wq) {
		ret = -ENOMEM;
		goto err;
	}

	ret = smb_direct_listen(&smb_direct_ib_listener,
				SMB_DIRECT_PORT_INFINIBAND);
	if (ret) {
		pr_err("Can't listen on InfiniBand/RoCEv1/RoCEv2: %d\n", ret);
		goto err;
	}

	ksmbd_debug(RDMA, "InfiniBand/RoCEv1/RoCEv2 RDMA listener. socket=%p\n",
		    smb_direct_ib_listener.socket);

	ret = smb_direct_listen(&smb_direct_iw_listener,
				SMB_DIRECT_PORT_IWARP);
	if (ret) {
		pr_err("Can't listen on iWarp: %d\n", ret);
		goto err;
	}

	ksmbd_debug(RDMA, "iWarp RDMA listener. socket=%p\n",
		    smb_direct_iw_listener.socket);

	return 0;
err:
	ksmbd_rdma_stop_listening();
	ksmbd_rdma_destroy();
	return ret;
}

void ksmbd_rdma_stop_listening(void)
{
	smb_direct_listener_destroy(&smb_direct_ib_listener);
	smb_direct_listener_destroy(&smb_direct_iw_listener);
}

void ksmbd_rdma_destroy(void)
{
	if (smb_direct_wq) {
		destroy_workqueue(smb_direct_wq);
		smb_direct_wq = NULL;
	}
}

bool ksmbd_rdma_capable_netdev(struct net_device *netdev)
{
	u8 node_type = smbdirect_netdev_rdma_capable_node_type(netdev);

	return node_type != RDMA_NODE_UNSPECIFIED;
}

static const struct ksmbd_transport_ops ksmbd_smb_direct_transport_ops = {
	.disconnect	= smb_direct_disconnect,
	.shutdown	= smb_direct_shutdown,
	.writev		= smb_direct_writev,
	.read		= smb_direct_read,
	.rdma_read	= smb_direct_rdma_read,
	.rdma_write	= smb_direct_rdma_write,
	.free_transport = smb_direct_free_transport,
};
