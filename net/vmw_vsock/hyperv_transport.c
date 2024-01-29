// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hyper-V transport for vsock
 *
 * Hyper-V Sockets supplies a byte-stream based communication mechanism
 * between the host and the VM. This driver implements the necessary
 * support in the VM by introducing the new vsock transport.
 *
 * Copyright (c) 2017, Microsoft Corporation.
 */
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/hyperv.h>
#include <net/sock.h>
#include <net/af_vsock.h>
#include <asm/hyperv-tlfs.h>

/* Older (VMBUS version 'VERSION_WIN10' or before) Windows hosts have some
 * stricter requirements on the hv_sock ring buffer size of six 4K pages.
 * hyperv-tlfs defines HV_HYP_PAGE_SIZE as 4K. Newer hosts don't have this
 * limitation; but, keep the defaults the same for compat.
 */
#define RINGBUFFER_HVS_RCV_SIZE (HV_HYP_PAGE_SIZE * 6)
#define RINGBUFFER_HVS_SND_SIZE (HV_HYP_PAGE_SIZE * 6)
#define RINGBUFFER_HVS_MAX_SIZE (HV_HYP_PAGE_SIZE * 64)

/* The MTU is 16KB per the host side's design */
#define HVS_MTU_SIZE		(1024 * 16)

/* How long to wait for graceful shutdown of a connection */
#define HVS_CLOSE_TIMEOUT (8 * HZ)

struct vmpipe_proto_header {
	u32 pkt_type;
	u32 data_size;
};

/* For recv, we use the VMBus in-place packet iterator APIs to directly copy
 * data from the ringbuffer into the userspace buffer.
 */
struct hvs_recv_buf {
	/* The header before the payload data */
	struct vmpipe_proto_header hdr;

	/* The payload */
	u8 data[HVS_MTU_SIZE];
};

/* We can send up to HVS_MTU_SIZE bytes of payload to the host, but let's use
 * a smaller size, i.e. HVS_SEND_BUF_SIZE, to maximize concurrency between the
 * guest and the host processing as one VMBUS packet is the smallest processing
 * unit.
 *
 * Note: the buffer can be eliminated in the future when we add new VMBus
 * ringbuffer APIs that allow us to directly copy data from userspace buffer
 * to VMBus ringbuffer.
 */
#define HVS_SEND_BUF_SIZE \
		(HV_HYP_PAGE_SIZE - sizeof(struct vmpipe_proto_header))

struct hvs_send_buf {
	/* The header before the payload data */
	struct vmpipe_proto_header hdr;

	/* The payload */
	u8 data[HVS_SEND_BUF_SIZE];
};

#define HVS_HEADER_LEN	(sizeof(struct vmpacket_descriptor) + \
			 sizeof(struct vmpipe_proto_header))

/* See 'prev_indices' in hv_ringbuffer_read(), hv_ringbuffer_write(), and
 * __hv_pkt_iter_next().
 */
#define VMBUS_PKT_TRAILER_SIZE	(sizeof(u64))

#define HVS_PKT_LEN(payload_len)	(HVS_HEADER_LEN + \
					 ALIGN((payload_len), 8) + \
					 VMBUS_PKT_TRAILER_SIZE)

/* Upper bound on the size of a VMbus packet for hv_sock */
#define HVS_MAX_PKT_SIZE	HVS_PKT_LEN(HVS_MTU_SIZE)

union hvs_service_id {
	guid_t	srv_id;

	struct {
		unsigned int svm_port;
		unsigned char b[sizeof(guid_t) - sizeof(unsigned int)];
	};
};

/* Per-socket state (accessed via vsk->trans) */
struct hvsock {
	struct vsock_sock *vsk;

	guid_t vm_srv_id;
	guid_t host_srv_id;

	struct vmbus_channel *chan;
	struct vmpacket_descriptor *recv_desc;

	/* The length of the payload not delivered to userland yet */
	u32 recv_data_len;
	/* The offset of the payload */
	u32 recv_data_off;

	/* Have we sent the zero-length packet (FIN)? */
	bool fin_sent;
};

/* In the VM, we support Hyper-V Sockets with AF_VSOCK, and the endpoint is
 * <cid, port> (see struct sockaddr_vm). Note: cid is not really used here:
 * when we write apps to connect to the host, we can only use VMADDR_CID_ANY
 * or VMADDR_CID_HOST (both are equivalent) as the remote cid, and when we
 * write apps to bind() & listen() in the VM, we can only use VMADDR_CID_ANY
 * as the local cid.
 *
 * On the host, Hyper-V Sockets are supported by Winsock AF_HYPERV:
 * https://docs.microsoft.com/en-us/virtualization/hyper-v-on-windows/user-
 * guide/make-integration-service, and the endpoint is <VmID, ServiceId> with
 * the below sockaddr:
 *
 * struct SOCKADDR_HV
 * {
 *    ADDRESS_FAMILY Family;
 *    USHORT Reserved;
 *    GUID VmId;
 *    GUID ServiceId;
 * };
 * Note: VmID is not used by Linux VM and actually it isn't transmitted via
 * VMBus, because here it's obvious the host and the VM can easily identify
 * each other. Though the VmID is useful on the host, especially in the case
 * of Windows container, Linux VM doesn't need it at all.
 *
 * To make use of the AF_VSOCK infrastructure in Linux VM, we have to limit
 * the available GUID space of SOCKADDR_HV so that we can create a mapping
 * between AF_VSOCK port and SOCKADDR_HV Service GUID. The rule of writing
 * Hyper-V Sockets apps on the host and in Linux VM is:
 *
 ****************************************************************************
 * The only valid Service GUIDs, from the perspectives of both the host and *
 * Linux VM, that can be connected by the other end, must conform to this   *
 * format: <port>-facb-11e6-bd58-64006a7986d3.                              *
 ****************************************************************************
 *
 * When we write apps on the host to connect(), the GUID ServiceID is used.
 * When we write apps in Linux VM to connect(), we only need to specify the
 * port and the driver will form the GUID and use that to request the host.
 *
 */

/* 00000000-facb-11e6-bd58-64006a7986d3 */
static const guid_t srv_id_template =
	GUID_INIT(0x00000000, 0xfacb, 0x11e6, 0xbd, 0x58,
		  0x64, 0x00, 0x6a, 0x79, 0x86, 0xd3);

static bool hvs_check_transport(struct vsock_sock *vsk);

static bool is_valid_srv_id(const guid_t *id)
{
	return !memcmp(&id->b[4], &srv_id_template.b[4], sizeof(guid_t) - 4);
}

static unsigned int get_port_by_srv_id(const guid_t *svr_id)
{
	return *((unsigned int *)svr_id);
}

static void hvs_addr_init(struct sockaddr_vm *addr, const guid_t *svr_id)
{
	unsigned int port = get_port_by_srv_id(svr_id);

	vsock_addr_init(addr, VMADDR_CID_ANY, port);
}

static void hvs_set_channel_pending_send_size(struct vmbus_channel *chan)
{
	set_channel_pending_send_size(chan,
				      HVS_PKT_LEN(HVS_SEND_BUF_SIZE));

	virt_mb();
}

static bool hvs_channel_readable(struct vmbus_channel *chan)
{
	u32 readable = hv_get_bytes_to_read(&chan->inbound);

	/* 0-size payload means FIN */
	return readable >= HVS_PKT_LEN(0);
}

static int hvs_channel_readable_payload(struct vmbus_channel *chan)
{
	u32 readable = hv_get_bytes_to_read(&chan->inbound);

	if (readable > HVS_PKT_LEN(0)) {
		/* At least we have 1 byte to read. We don't need to return
		 * the exact readable bytes: see vsock_stream_recvmsg() ->
		 * vsock_stream_has_data().
		 */
		return 1;
	}

	if (readable == HVS_PKT_LEN(0)) {
		/* 0-size payload means FIN */
		return 0;
	}

	/* No payload or FIN */
	return -1;
}

static size_t hvs_channel_writable_bytes(struct vmbus_channel *chan)
{
	u32 writeable = hv_get_bytes_to_write(&chan->outbound);
	size_t ret;

	/* The ringbuffer mustn't be 100% full, and we should reserve a
	 * zero-length-payload packet for the FIN: see hv_ringbuffer_write()
	 * and hvs_shutdown().
	 */
	if (writeable <= HVS_PKT_LEN(1) + HVS_PKT_LEN(0))
		return 0;

	ret = writeable - HVS_PKT_LEN(1) - HVS_PKT_LEN(0);

	return round_down(ret, 8);
}

static int __hvs_send_data(struct vmbus_channel *chan,
			   struct vmpipe_proto_header *hdr,
			   size_t to_write)
{
	hdr->pkt_type = 1;
	hdr->data_size = to_write;
	return vmbus_sendpacket(chan, hdr, sizeof(*hdr) + to_write,
				0, VM_PKT_DATA_INBAND, 0);
}

static int hvs_send_data(struct vmbus_channel *chan,
			 struct hvs_send_buf *send_buf, size_t to_write)
{
	return __hvs_send_data(chan, &send_buf->hdr, to_write);
}

static void hvs_channel_cb(void *ctx)
{
	struct sock *sk = (struct sock *)ctx;
	struct vsock_sock *vsk = vsock_sk(sk);
	struct hvsock *hvs = vsk->trans;
	struct vmbus_channel *chan = hvs->chan;

	if (hvs_channel_readable(chan))
		sk->sk_data_ready(sk);

	if (hv_get_bytes_to_write(&chan->outbound) > 0)
		sk->sk_write_space(sk);
}

static void hvs_do_close_lock_held(struct vsock_sock *vsk,
				   bool cancel_timeout)
{
	struct sock *sk = sk_vsock(vsk);

	sock_set_flag(sk, SOCK_DONE);
	vsk->peer_shutdown = SHUTDOWN_MASK;
	if (vsock_stream_has_data(vsk) <= 0)
		sk->sk_state = TCP_CLOSING;
	sk->sk_state_change(sk);
	if (vsk->close_work_scheduled &&
	    (!cancel_timeout || cancel_delayed_work(&vsk->close_work))) {
		vsk->close_work_scheduled = false;
		vsock_remove_sock(vsk);

		/* Release the reference taken while scheduling the timeout */
		sock_put(sk);
	}
}

static void hvs_close_connection(struct vmbus_channel *chan)
{
	struct sock *sk = get_per_channel_state(chan);

	lock_sock(sk);
	hvs_do_close_lock_held(vsock_sk(sk), true);
	release_sock(sk);

	/* Release the refcnt for the channel that's opened in
	 * hvs_open_connection().
	 */
	sock_put(sk);
}

static void hvs_open_connection(struct vmbus_channel *chan)
{
	guid_t *if_instance, *if_type;
	unsigned char conn_from_host;

	struct sockaddr_vm addr;
	struct sock *sk, *new = NULL;
	struct vsock_sock *vnew = NULL;
	struct hvsock *hvs = NULL;
	struct hvsock *hvs_new = NULL;
	int rcvbuf;
	int ret;
	int sndbuf;

	if_type = &chan->offermsg.offer.if_type;
	if_instance = &chan->offermsg.offer.if_instance;
	conn_from_host = chan->offermsg.offer.u.pipe.user_def[0];
	if (!is_valid_srv_id(if_type))
		return;

	hvs_addr_init(&addr, conn_from_host ? if_type : if_instance);
	sk = vsock_find_bound_socket(&addr);
	if (!sk)
		return;

	lock_sock(sk);
	if ((conn_from_host && sk->sk_state != TCP_LISTEN) ||
	    (!conn_from_host && sk->sk_state != TCP_SYN_SENT))
		goto out;

	if (conn_from_host) {
		if (sk->sk_ack_backlog >= sk->sk_max_ack_backlog)
			goto out;

		new = vsock_create_connected(sk);
		if (!new)
			goto out;

		new->sk_state = TCP_SYN_SENT;
		vnew = vsock_sk(new);

		hvs_addr_init(&vnew->local_addr, if_type);

		/* Remote peer is always the host */
		vsock_addr_init(&vnew->remote_addr,
				VMADDR_CID_HOST, VMADDR_PORT_ANY);
		vnew->remote_addr.svm_port = get_port_by_srv_id(if_instance);
		ret = vsock_assign_transport(vnew, vsock_sk(sk));
		/* Transport assigned (looking at remote_addr) must be the
		 * same where we received the request.
		 */
		if (ret || !hvs_check_transport(vnew)) {
			sock_put(new);
			goto out;
		}
		hvs_new = vnew->trans;
		hvs_new->chan = chan;
	} else {
		hvs = vsock_sk(sk)->trans;
		hvs->chan = chan;
	}

	set_channel_read_mode(chan, HV_CALL_DIRECT);

	/* Use the socket buffer sizes as hints for the VMBUS ring size. For
	 * server side sockets, 'sk' is the parent socket and thus, this will
	 * allow the child sockets to inherit the size from the parent. Keep
	 * the mins to the default value and align to page size as per VMBUS
	 * requirements.
	 * For the max, the socket core library will limit the socket buffer
	 * size that can be set by the user, but, since currently, the hv_sock
	 * VMBUS ring buffer is physically contiguous allocation, restrict it
	 * further.
	 * Older versions of hv_sock host side code cannot handle bigger VMBUS
	 * ring buffer size. Use the version number to limit the change to newer
	 * versions.
	 */
	if (vmbus_proto_version < VERSION_WIN10_V5) {
		sndbuf = RINGBUFFER_HVS_SND_SIZE;
		rcvbuf = RINGBUFFER_HVS_RCV_SIZE;
	} else {
		sndbuf = max_t(int, sk->sk_sndbuf, RINGBUFFER_HVS_SND_SIZE);
		sndbuf = min_t(int, sndbuf, RINGBUFFER_HVS_MAX_SIZE);
		sndbuf = ALIGN(sndbuf, HV_HYP_PAGE_SIZE);
		rcvbuf = max_t(int, sk->sk_rcvbuf, RINGBUFFER_HVS_RCV_SIZE);
		rcvbuf = min_t(int, rcvbuf, RINGBUFFER_HVS_MAX_SIZE);
		rcvbuf = ALIGN(rcvbuf, HV_HYP_PAGE_SIZE);
	}

	chan->max_pkt_size = HVS_MAX_PKT_SIZE;

	ret = vmbus_open(chan, sndbuf, rcvbuf, NULL, 0, hvs_channel_cb,
			 conn_from_host ? new : sk);
	if (ret != 0) {
		if (conn_from_host) {
			hvs_new->chan = NULL;
			sock_put(new);
		} else {
			hvs->chan = NULL;
		}
		goto out;
	}

	set_per_channel_state(chan, conn_from_host ? new : sk);

	/* This reference will be dropped by hvs_close_connection(). */
	sock_hold(conn_from_host ? new : sk);
	vmbus_set_chn_rescind_callback(chan, hvs_close_connection);

	/* Set the pending send size to max packet size to always get
	 * notifications from the host when there is enough writable space.
	 * The host is optimized to send notifications only when the pending
	 * size boundary is crossed, and not always.
	 */
	hvs_set_channel_pending_send_size(chan);

	if (conn_from_host) {
		new->sk_state = TCP_ESTABLISHED;
		sk_acceptq_added(sk);

		hvs_new->vm_srv_id = *if_type;
		hvs_new->host_srv_id = *if_instance;

		vsock_insert_connected(vnew);

		vsock_enqueue_accept(sk, new);
	} else {
		sk->sk_state = TCP_ESTABLISHED;
		sk->sk_socket->state = SS_CONNECTED;

		vsock_insert_connected(vsock_sk(sk));
	}

	sk->sk_state_change(sk);

out:
	/* Release refcnt obtained when we called vsock_find_bound_socket() */
	sock_put(sk);

	release_sock(sk);
}

static u32 hvs_get_local_cid(void)
{
	return VMADDR_CID_ANY;
}

static int hvs_sock_init(struct vsock_sock *vsk, struct vsock_sock *psk)
{
	struct hvsock *hvs;
	struct sock *sk = sk_vsock(vsk);

	hvs = kzalloc(sizeof(*hvs), GFP_KERNEL);
	if (!hvs)
		return -ENOMEM;

	vsk->trans = hvs;
	hvs->vsk = vsk;
	sk->sk_sndbuf = RINGBUFFER_HVS_SND_SIZE;
	sk->sk_rcvbuf = RINGBUFFER_HVS_RCV_SIZE;
	return 0;
}

static int hvs_connect(struct vsock_sock *vsk)
{
	union hvs_service_id vm, host;
	struct hvsock *h = vsk->trans;

	vm.srv_id = srv_id_template;
	vm.svm_port = vsk->local_addr.svm_port;
	h->vm_srv_id = vm.srv_id;

	host.srv_id = srv_id_template;
	host.svm_port = vsk->remote_addr.svm_port;
	h->host_srv_id = host.srv_id;

	return vmbus_send_tl_connect_request(&h->vm_srv_id, &h->host_srv_id);
}

static void hvs_shutdown_lock_held(struct hvsock *hvs, int mode)
{
	struct vmpipe_proto_header hdr;

	if (hvs->fin_sent || !hvs->chan)
		return;

	/* It can't fail: see hvs_channel_writable_bytes(). */
	(void)__hvs_send_data(hvs->chan, &hdr, 0);
	hvs->fin_sent = true;
}

static int hvs_shutdown(struct vsock_sock *vsk, int mode)
{
	if (!(mode & SEND_SHUTDOWN))
		return 0;

	hvs_shutdown_lock_held(vsk->trans, mode);
	return 0;
}

static void hvs_close_timeout(struct work_struct *work)
{
	struct vsock_sock *vsk =
		container_of(work, struct vsock_sock, close_work.work);
	struct sock *sk = sk_vsock(vsk);

	sock_hold(sk);
	lock_sock(sk);
	if (!sock_flag(sk, SOCK_DONE))
		hvs_do_close_lock_held(vsk, false);

	vsk->close_work_scheduled = false;
	release_sock(sk);
	sock_put(sk);
}

/* Returns true, if it is safe to remove socket; false otherwise */
static bool hvs_close_lock_held(struct vsock_sock *vsk)
{
	struct sock *sk = sk_vsock(vsk);

	if (!(sk->sk_state == TCP_ESTABLISHED ||
	      sk->sk_state == TCP_CLOSING))
		return true;

	if ((sk->sk_shutdown & SHUTDOWN_MASK) != SHUTDOWN_MASK)
		hvs_shutdown_lock_held(vsk->trans, SHUTDOWN_MASK);

	if (sock_flag(sk, SOCK_DONE))
		return true;

	/* This reference will be dropped by the delayed close routine */
	sock_hold(sk);
	INIT_DELAYED_WORK(&vsk->close_work, hvs_close_timeout);
	vsk->close_work_scheduled = true;
	schedule_delayed_work(&vsk->close_work, HVS_CLOSE_TIMEOUT);
	return false;
}

static void hvs_release(struct vsock_sock *vsk)
{
	bool remove_sock;

	remove_sock = hvs_close_lock_held(vsk);
	if (remove_sock)
		vsock_remove_sock(vsk);
}

static void hvs_destruct(struct vsock_sock *vsk)
{
	struct hvsock *hvs = vsk->trans;
	struct vmbus_channel *chan = hvs->chan;

	if (chan)
		vmbus_hvsock_device_unregister(chan);

	kfree(hvs);
}

static int hvs_dgram_bind(struct vsock_sock *vsk, struct sockaddr_vm *addr)
{
	return -EOPNOTSUPP;
}

static int hvs_dgram_dequeue(struct vsock_sock *vsk, struct msghdr *msg,
			     size_t len, int flags)
{
	return -EOPNOTSUPP;
}

static int hvs_dgram_enqueue(struct vsock_sock *vsk,
			     struct sockaddr_vm *remote, struct msghdr *msg,
			     size_t dgram_len)
{
	return -EOPNOTSUPP;
}

static bool hvs_dgram_allow(u32 cid, u32 port)
{
	return false;
}

static int hvs_update_recv_data(struct hvsock *hvs)
{
	struct hvs_recv_buf *recv_buf;
	u32 pkt_len, payload_len;

	pkt_len = hv_pkt_len(hvs->recv_desc);

	if (pkt_len < HVS_HEADER_LEN)
		return -EIO;

	recv_buf = (struct hvs_recv_buf *)(hvs->recv_desc + 1);
	payload_len = recv_buf->hdr.data_size;

	if (payload_len > pkt_len - HVS_HEADER_LEN ||
	    payload_len > HVS_MTU_SIZE)
		return -EIO;

	if (payload_len == 0)
		hvs->vsk->peer_shutdown |= SEND_SHUTDOWN;

	hvs->recv_data_len = payload_len;
	hvs->recv_data_off = 0;

	return 0;
}

static ssize_t hvs_stream_dequeue(struct vsock_sock *vsk, struct msghdr *msg,
				  size_t len, int flags)
{
	struct hvsock *hvs = vsk->trans;
	bool need_refill = !hvs->recv_desc;
	struct hvs_recv_buf *recv_buf;
	u32 to_read;
	int ret;

	if (flags & MSG_PEEK)
		return -EOPNOTSUPP;

	if (need_refill) {
		hvs->recv_desc = hv_pkt_iter_first(hvs->chan);
		if (!hvs->recv_desc)
			return -ENOBUFS;
		ret = hvs_update_recv_data(hvs);
		if (ret)
			return ret;
	}

	recv_buf = (struct hvs_recv_buf *)(hvs->recv_desc + 1);
	to_read = min_t(u32, len, hvs->recv_data_len);
	ret = memcpy_to_msg(msg, recv_buf->data + hvs->recv_data_off, to_read);
	if (ret != 0)
		return ret;

	hvs->recv_data_len -= to_read;
	if (hvs->recv_data_len == 0) {
		hvs->recv_desc = hv_pkt_iter_next(hvs->chan, hvs->recv_desc);
		if (hvs->recv_desc) {
			ret = hvs_update_recv_data(hvs);
			if (ret)
				return ret;
		}
	} else {
		hvs->recv_data_off += to_read;
	}

	return to_read;
}

static ssize_t hvs_stream_enqueue(struct vsock_sock *vsk, struct msghdr *msg,
				  size_t len)
{
	struct hvsock *hvs = vsk->trans;
	struct vmbus_channel *chan = hvs->chan;
	struct hvs_send_buf *send_buf;
	ssize_t to_write, max_writable;
	ssize_t ret = 0;
	ssize_t bytes_written = 0;

	BUILD_BUG_ON(sizeof(*send_buf) != HV_HYP_PAGE_SIZE);

	send_buf = kmalloc(sizeof(*send_buf), GFP_KERNEL);
	if (!send_buf)
		return -ENOMEM;

	/* Reader(s) could be draining data from the channel as we write.
	 * Maximize bandwidth, by iterating until the channel is found to be
	 * full.
	 */
	while (len) {
		max_writable = hvs_channel_writable_bytes(chan);
		if (!max_writable)
			break;
		to_write = min_t(ssize_t, len, max_writable);
		to_write = min_t(ssize_t, to_write, HVS_SEND_BUF_SIZE);
		/* memcpy_from_msg is safe for loop as it advances the offsets
		 * within the message iterator.
		 */
		ret = memcpy_from_msg(send_buf->data, msg, to_write);
		if (ret < 0)
			goto out;

		ret = hvs_send_data(hvs->chan, send_buf, to_write);
		if (ret < 0)
			goto out;

		bytes_written += to_write;
		len -= to_write;
	}
out:
	/* If any data has been sent, return that */
	if (bytes_written)
		ret = bytes_written;
	kfree(send_buf);
	return ret;
}

static s64 hvs_stream_has_data(struct vsock_sock *vsk)
{
	struct hvsock *hvs = vsk->trans;
	s64 ret;

	if (hvs->recv_data_len > 0)
		return 1;

	switch (hvs_channel_readable_payload(hvs->chan)) {
	case 1:
		ret = 1;
		break;
	case 0:
		vsk->peer_shutdown |= SEND_SHUTDOWN;
		ret = 0;
		break;
	default: /* -1 */
		ret = 0;
		break;
	}

	return ret;
}

static s64 hvs_stream_has_space(struct vsock_sock *vsk)
{
	struct hvsock *hvs = vsk->trans;

	return hvs_channel_writable_bytes(hvs->chan);
}

static u64 hvs_stream_rcvhiwat(struct vsock_sock *vsk)
{
	return HVS_MTU_SIZE + 1;
}

static bool hvs_stream_is_active(struct vsock_sock *vsk)
{
	struct hvsock *hvs = vsk->trans;

	return hvs->chan != NULL;
}

static bool hvs_stream_allow(u32 cid, u32 port)
{
	if (cid == VMADDR_CID_HOST)
		return true;

	return false;
}

static
int hvs_notify_poll_in(struct vsock_sock *vsk, size_t target, bool *readable)
{
	struct hvsock *hvs = vsk->trans;

	*readable = hvs_channel_readable(hvs->chan);
	return 0;
}

static
int hvs_notify_poll_out(struct vsock_sock *vsk, size_t target, bool *writable)
{
	*writable = hvs_stream_has_space(vsk) > 0;

	return 0;
}

static
int hvs_notify_recv_init(struct vsock_sock *vsk, size_t target,
			 struct vsock_transport_recv_notify_data *d)
{
	return 0;
}

static
int hvs_notify_recv_pre_block(struct vsock_sock *vsk, size_t target,
			      struct vsock_transport_recv_notify_data *d)
{
	return 0;
}

static
int hvs_notify_recv_pre_dequeue(struct vsock_sock *vsk, size_t target,
				struct vsock_transport_recv_notify_data *d)
{
	return 0;
}

static
int hvs_notify_recv_post_dequeue(struct vsock_sock *vsk, size_t target,
				 ssize_t copied, bool data_read,
				 struct vsock_transport_recv_notify_data *d)
{
	return 0;
}

static
int hvs_notify_send_init(struct vsock_sock *vsk,
			 struct vsock_transport_send_notify_data *d)
{
	return 0;
}

static
int hvs_notify_send_pre_block(struct vsock_sock *vsk,
			      struct vsock_transport_send_notify_data *d)
{
	return 0;
}

static
int hvs_notify_send_pre_enqueue(struct vsock_sock *vsk,
				struct vsock_transport_send_notify_data *d)
{
	return 0;
}

static
int hvs_notify_send_post_enqueue(struct vsock_sock *vsk, ssize_t written,
				 struct vsock_transport_send_notify_data *d)
{
	return 0;
}

static
int hvs_notify_set_rcvlowat(struct vsock_sock *vsk, int val)
{
	return -EOPNOTSUPP;
}

static struct vsock_transport hvs_transport = {
	.module                   = THIS_MODULE,

	.get_local_cid            = hvs_get_local_cid,

	.init                     = hvs_sock_init,
	.destruct                 = hvs_destruct,
	.release                  = hvs_release,
	.connect                  = hvs_connect,
	.shutdown                 = hvs_shutdown,

	.dgram_bind               = hvs_dgram_bind,
	.dgram_dequeue            = hvs_dgram_dequeue,
	.dgram_enqueue            = hvs_dgram_enqueue,
	.dgram_allow              = hvs_dgram_allow,

	.stream_dequeue           = hvs_stream_dequeue,
	.stream_enqueue           = hvs_stream_enqueue,
	.stream_has_data          = hvs_stream_has_data,
	.stream_has_space         = hvs_stream_has_space,
	.stream_rcvhiwat          = hvs_stream_rcvhiwat,
	.stream_is_active         = hvs_stream_is_active,
	.stream_allow             = hvs_stream_allow,

	.notify_poll_in           = hvs_notify_poll_in,
	.notify_poll_out          = hvs_notify_poll_out,
	.notify_recv_init         = hvs_notify_recv_init,
	.notify_recv_pre_block    = hvs_notify_recv_pre_block,
	.notify_recv_pre_dequeue  = hvs_notify_recv_pre_dequeue,
	.notify_recv_post_dequeue = hvs_notify_recv_post_dequeue,
	.notify_send_init         = hvs_notify_send_init,
	.notify_send_pre_block    = hvs_notify_send_pre_block,
	.notify_send_pre_enqueue  = hvs_notify_send_pre_enqueue,
	.notify_send_post_enqueue = hvs_notify_send_post_enqueue,

	.notify_set_rcvlowat      = hvs_notify_set_rcvlowat
};

static bool hvs_check_transport(struct vsock_sock *vsk)
{
	return vsk->transport == &hvs_transport;
}

static int hvs_probe(struct hv_device *hdev,
		     const struct hv_vmbus_device_id *dev_id)
{
	struct vmbus_channel *chan = hdev->channel;

	hvs_open_connection(chan);

	/* Always return success to suppress the unnecessary error message
	 * in vmbus_probe(): on error the host will rescind the device in
	 * 30 seconds and we can do cleanup at that time in
	 * vmbus_onoffer_rescind().
	 */
	return 0;
}

static void hvs_remove(struct hv_device *hdev)
{
	struct vmbus_channel *chan = hdev->channel;

	vmbus_close(chan);
}

/* hv_sock connections can not persist across hibernation, and all the hv_sock
 * channels are forced to be rescinded before hibernation: see
 * vmbus_bus_suspend(). Here the dummy hvs_suspend() and hvs_resume()
 * are only needed because hibernation requires that every vmbus device's
 * driver should have a .suspend and .resume callback: see vmbus_suspend().
 */
static int hvs_suspend(struct hv_device *hv_dev)
{
	/* Dummy */
	return 0;
}

static int hvs_resume(struct hv_device *dev)
{
	/* Dummy */
	return 0;
}

/* This isn't really used. See vmbus_match() and vmbus_probe() */
static const struct hv_vmbus_device_id id_table[] = {
	{},
};

static struct hv_driver hvs_drv = {
	.name		= "hv_sock",
	.hvsock		= true,
	.id_table	= id_table,
	.probe		= hvs_probe,
	.remove		= hvs_remove,
	.suspend	= hvs_suspend,
	.resume		= hvs_resume,
};

static int __init hvs_init(void)
{
	int ret;

	if (vmbus_proto_version < VERSION_WIN10)
		return -ENODEV;

	ret = vmbus_driver_register(&hvs_drv);
	if (ret != 0)
		return ret;

	ret = vsock_core_register(&hvs_transport, VSOCK_TRANSPORT_F_G2H);
	if (ret) {
		vmbus_driver_unregister(&hvs_drv);
		return ret;
	}

	return 0;
}

static void __exit hvs_exit(void)
{
	vsock_core_unregister(&hvs_transport);
	vmbus_driver_unregister(&hvs_drv);
}

module_init(hvs_init);
module_exit(hvs_exit);

MODULE_DESCRIPTION("Hyper-V Sockets");
MODULE_VERSION("1.0.0");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_VSOCK);
