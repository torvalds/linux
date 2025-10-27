// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *   Copyright (C) 2018, LG Electronics.
 *
 *   Author(s): Long Li <longli@microsoft.com>,
 *		Hyunchul Lee <hyc.lee@gmail.com>
 */

#define SUBMOD_NAME	"smb_direct"

#define SMBDIRECT_USE_INLINE_C_FILES 1

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

static LIST_HEAD(smb_direct_device_list);
static DEFINE_RWLOCK(smb_direct_device_lock);

struct smb_direct_device {
	struct ib_device	*ib_dev;
	struct list_head	list;
};

static struct smb_direct_listener {
	int			port;
	struct rdma_cm_id	*cm_id;
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

static struct smb_direct_transport *alloc_transport(struct rdma_cm_id *cm_id)
{
	struct smb_direct_transport *t;
	struct smbdirect_socket *sc;
	struct smbdirect_socket_parameters init_params = {};
	struct smbdirect_socket_parameters *sp;
	struct ksmbd_conn *conn;
	int ret;

	/*
	 * Create the initial parameters
	 */
	sp = &init_params;
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

	t = kzalloc_obj(*t, KSMBD_DEFAULT_GFP);
	if (!t)
		return NULL;
	ret = smbdirect_socket_create_accepting(cm_id, &sc);
	if (ret)
		goto socket_create_failed;
	smbdirect_socket_set_logging(sc, NULL,
				     smb_direct_logging_needed,
				     smb_direct_logging_vaprintf);
	ret = smbdirect_socket_set_initial_parameters(sc, sp);
	if (ret)
		goto set_params_failed;
	ret = smbdirect_socket_set_kernel_settings(sc, IB_POLL_WORKQUEUE, KSMBD_DEFAULT_GFP);
	if (ret)
		goto set_settings_failed;
	ret = smbdirect_socket_set_custom_workqueue(sc, smb_direct_wq);
	if (ret)
		goto set_workqueue_failed;

	conn = ksmbd_conn_alloc();
	if (!conn)
		goto conn_alloc_failed;

	down_write(&conn_list_lock);
	hash_add(conn_list, &conn->hlist, 0);
	up_write(&conn_list_lock);

	conn->transport = KSMBD_TRANS(t);
	KSMBD_TRANS(t)->conn = conn;
	KSMBD_TRANS(t)->ops = &ksmbd_smb_direct_transport_ops;

	t->socket = sc;
	return t;

conn_alloc_failed:
set_workqueue_failed:
set_settings_failed:
set_params_failed:
	smbdirect_socket_release(sc);
socket_create_failed:
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

static int smb_direct_prepare(struct ksmbd_transport *t)
{
	struct smb_direct_transport *st = SMBD_TRANS(t);
	struct smbdirect_socket *sc = st->socket;
	int ret;

	ksmbd_debug(RDMA, "SMB_DIRECT Waiting for connection\n");
	ret = smbdirect_connection_wait_for_connected(sc);
	if (ret) {
		ksmbd_debug(RDMA, "SMB_DIRECT connection failed %d => %1pe\n",
			    ret, ERR_PTR(ret));
		return ret;
	}

	ksmbd_debug(RDMA, "SMB_DIRECT connection ready\n");
	return 0;
}

static int smb_direct_handle_connect_request(struct rdma_cm_id *new_cm_id,
					     struct rdma_cm_event *event)
{
	struct smb_direct_listener *listener = new_cm_id->context;
	struct smb_direct_transport *t;
	struct smbdirect_socket *sc;
	struct task_struct *handler;
	int ret;

	if (!smbdirect_frwr_is_supported(&new_cm_id->device->attrs)) {
		ksmbd_debug(RDMA,
			    "Fast Registration Work Requests is not supported. device capabilities=%llx\n",
			    new_cm_id->device->attrs.device_cap_flags);
		return -EPROTONOSUPPORT;
	}

	t = alloc_transport(new_cm_id);
	if (!t)
		return -ENOMEM;
	sc = t->socket;

	ret = smbdirect_accept_connect_request(sc, &event->param.conn);
	if (ret)
		goto out_err;

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

static int smb_direct_listen_handler(struct rdma_cm_id *cm_id,
				     struct rdma_cm_event *event)
{
	switch (event->event) {
	case RDMA_CM_EVENT_CONNECT_REQUEST: {
		int ret = smb_direct_handle_connect_request(cm_id, event);

		if (ret) {
			pr_err("Can't create transport: %d\n", ret);
			return ret;
		}

		ksmbd_debug(RDMA, "Received connection request. cm_id=%p\n",
			    cm_id);
		break;
	}
	default:
		pr_err("Unexpected listen event. cm_id=%p, event=%s (%d)\n",
		       cm_id, rdma_event_msg(event->event), event->event);
		break;
	}
	return 0;
}

static int smb_direct_listen(struct smb_direct_listener *listener,
			     int port)
{
	int ret;
	struct rdma_cm_id *cm_id;
	u8 node_type = RDMA_NODE_UNSPECIFIED;
	struct sockaddr_in sin = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= htonl(INADDR_ANY),
		.sin_port		= htons(port),
	};

	switch (port) {
	case SMB_DIRECT_PORT_IWARP:
		/*
		 * only allow iWarp devices
		 * for port 5445.
		 */
		node_type = RDMA_NODE_RNIC;
		break;
	case SMB_DIRECT_PORT_INFINIBAND:
		/*
		 * only allow InfiniBand, RoCEv1 or RoCEv2
		 * devices for port 445.
		 *
		 * (Basically don't allow iWarp devices)
		 */
		node_type = RDMA_NODE_IB_CA;
		break;
	default:
		pr_err("unsupported smbdirect port=%d!\n", port);
		return -ENODEV;
	}

	cm_id = rdma_create_id(&init_net, smb_direct_listen_handler,
			       listener, RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(cm_id)) {
		pr_err("Can't create cm id: %ld\n", PTR_ERR(cm_id));
		return PTR_ERR(cm_id);
	}

	ret = rdma_restrict_node_type(cm_id, node_type);
	if (ret) {
		pr_err("rdma_restrict_node_type(%u) failed %d\n",
		       node_type, ret);
		goto err;
	}

	ret = rdma_bind_addr(cm_id, (struct sockaddr *)&sin);
	if (ret) {
		pr_err("Can't bind: %d\n", ret);
		goto err;
	}

	ret = rdma_listen(cm_id, 10);
	if (ret) {
		pr_err("Can't listen: %d\n", ret);
		goto err;
	}

	listener->port = port;
	listener->cm_id = cm_id;

	return 0;
err:
	listener->port = 0;
	listener->cm_id = NULL;
	rdma_destroy_id(cm_id);
	return ret;
}

static int smb_direct_ib_client_add(struct ib_device *ib_dev)
{
	struct smb_direct_device *smb_dev;

	if (!smbdirect_frwr_is_supported(&ib_dev->attrs))
		return 0;

	smb_dev = kzalloc_obj(*smb_dev, KSMBD_DEFAULT_GFP);
	if (!smb_dev)
		return -ENOMEM;
	smb_dev->ib_dev = ib_dev;

	write_lock(&smb_direct_device_lock);
	list_add(&smb_dev->list, &smb_direct_device_list);
	write_unlock(&smb_direct_device_lock);

	ksmbd_debug(RDMA, "ib device added: name %s\n", ib_dev->name);
	return 0;
}

static void smb_direct_ib_client_remove(struct ib_device *ib_dev,
					void *client_data)
{
	struct smb_direct_device *smb_dev, *tmp;

	write_lock(&smb_direct_device_lock);
	list_for_each_entry_safe(smb_dev, tmp, &smb_direct_device_list, list) {
		if (smb_dev->ib_dev == ib_dev) {
			list_del(&smb_dev->list);
			kfree(smb_dev);
			break;
		}
	}
	write_unlock(&smb_direct_device_lock);
}

static struct ib_client smb_direct_ib_client = {
	.name	= "ksmbd_smb_direct_ib",
	.add	= smb_direct_ib_client_add,
	.remove	= smb_direct_ib_client_remove,
};

int ksmbd_rdma_init(void)
{
	int ret;

	smb_direct_ib_listener = smb_direct_iw_listener = (struct smb_direct_listener) {
		.cm_id = NULL,
	};

	ret = ib_register_client(&smb_direct_ib_client);
	if (ret) {
		pr_err("failed to ib_register_client\n");
		return ret;
	}

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

	ksmbd_debug(RDMA, "InfiniBand/RoCEv1/RoCEv2 RDMA listener. cm_id=%p\n",
		    smb_direct_ib_listener.cm_id);

	ret = smb_direct_listen(&smb_direct_iw_listener,
				SMB_DIRECT_PORT_IWARP);
	if (ret) {
		pr_err("Can't listen on iWarp: %d\n", ret);
		goto err;
	}

	ksmbd_debug(RDMA, "iWarp RDMA listener. cm_id=%p\n",
		    smb_direct_iw_listener.cm_id);

	return 0;
err:
	ksmbd_rdma_stop_listening();
	ksmbd_rdma_destroy();
	return ret;
}

void ksmbd_rdma_stop_listening(void)
{
	if (!smb_direct_ib_listener.cm_id && !smb_direct_iw_listener.cm_id)
		return;

	ib_unregister_client(&smb_direct_ib_client);

	if (smb_direct_ib_listener.cm_id)
		rdma_destroy_id(smb_direct_ib_listener.cm_id);
	if (smb_direct_iw_listener.cm_id)
		rdma_destroy_id(smb_direct_iw_listener.cm_id);

	smb_direct_ib_listener = smb_direct_iw_listener = (struct smb_direct_listener) {
		.cm_id = NULL,
	};
}

void ksmbd_rdma_destroy(void)
{
	if (smb_direct_wq) {
		destroy_workqueue(smb_direct_wq);
		smb_direct_wq = NULL;
	}
}

static bool ksmbd_find_rdma_capable_netdev(struct net_device *netdev)
{
	struct smb_direct_device *smb_dev;
	int i;
	bool rdma_capable = false;

	read_lock(&smb_direct_device_lock);
	list_for_each_entry(smb_dev, &smb_direct_device_list, list) {
		for (i = 0; i < smb_dev->ib_dev->phys_port_cnt; i++) {
			struct net_device *ndev;

			ndev = ib_device_get_netdev(smb_dev->ib_dev, i + 1);
			if (!ndev)
				continue;

			if (ndev == netdev) {
				dev_put(ndev);
				rdma_capable = true;
				goto out;
			}
			dev_put(ndev);
		}
	}
out:
	read_unlock(&smb_direct_device_lock);

	if (rdma_capable == false) {
		struct ib_device *ibdev;

		ibdev = ib_device_get_by_netdev(netdev, RDMA_DRIVER_UNKNOWN);
		if (ibdev) {
			rdma_capable = smbdirect_frwr_is_supported(&ibdev->attrs);
			ib_device_put(ibdev);
		}
	}

	ksmbd_debug(RDMA, "netdev(%s) rdma capable : %s\n",
		    netdev->name, str_true_false(rdma_capable));

	return rdma_capable;
}

bool ksmbd_rdma_capable_netdev(struct net_device *netdev)
{
	struct net_device *lower_dev;
	struct list_head *iter;

	if (ksmbd_find_rdma_capable_netdev(netdev))
		return true;

	/* check if netdev is bridge or VLAN */
	if (netif_is_bridge_master(netdev) ||
	    netdev->priv_flags & IFF_802_1Q_VLAN)
		netdev_for_each_lower_dev(netdev, lower_dev, iter)
			if (ksmbd_find_rdma_capable_netdev(lower_dev))
				return true;

	/* check if netdev is IPoIB safely without layer violation */
	if (netdev->type == ARPHRD_INFINIBAND)
		return true;

	return false;
}

static const struct ksmbd_transport_ops ksmbd_smb_direct_transport_ops = {
	.prepare	= smb_direct_prepare,
	.disconnect	= smb_direct_disconnect,
	.shutdown	= smb_direct_shutdown,
	.writev		= smb_direct_writev,
	.read		= smb_direct_read,
	.rdma_read	= smb_direct_rdma_read,
	.rdma_write	= smb_direct_rdma_write,
	.free_transport = smb_direct_free_transport,
};

/*
 * This is a temporary solution until all code
 * is moved to smbdirect_all_c_files.c and we
 * have an smbdirect.ko that exports the required
 * functions.
 */
#include "../common/smbdirect/smbdirect_all_c_files.c"
