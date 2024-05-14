// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2016 Namjae Jeon <linkinjeon@kernel.org>
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#include <linux/freezer.h>

#include "smb_common.h"
#include "server.h"
#include "auth.h"
#include "connection.h"
#include "transport_tcp.h"

#define IFACE_STATE_DOWN		BIT(0)
#define IFACE_STATE_CONFIGURED		BIT(1)

static atomic_t active_num_conn;

struct interface {
	struct task_struct	*ksmbd_kthread;
	struct socket		*ksmbd_socket;
	struct list_head	entry;
	char			*name;
	struct mutex		sock_release_lock;
	int			state;
};

static LIST_HEAD(iface_list);

static int bind_additional_ifaces;

struct tcp_transport {
	struct ksmbd_transport		transport;
	struct socket			*sock;
	struct kvec			*iov;
	unsigned int			nr_iov;
};

static struct ksmbd_transport_ops ksmbd_tcp_transport_ops;

static void tcp_stop_kthread(struct task_struct *kthread);
static struct interface *alloc_iface(char *ifname);

#define KSMBD_TRANS(t)	(&(t)->transport)
#define TCP_TRANS(t)	((struct tcp_transport *)container_of(t, \
				struct tcp_transport, transport))

static inline void ksmbd_tcp_nodelay(struct socket *sock)
{
	tcp_sock_set_nodelay(sock->sk);
}

static inline void ksmbd_tcp_reuseaddr(struct socket *sock)
{
	sock_set_reuseaddr(sock->sk);
}

static inline void ksmbd_tcp_rcv_timeout(struct socket *sock, s64 secs)
{
	lock_sock(sock->sk);
	if (secs && secs < MAX_SCHEDULE_TIMEOUT / HZ - 1)
		sock->sk->sk_rcvtimeo = secs * HZ;
	else
		sock->sk->sk_rcvtimeo = MAX_SCHEDULE_TIMEOUT;
	release_sock(sock->sk);
}

static inline void ksmbd_tcp_snd_timeout(struct socket *sock, s64 secs)
{
	sock_set_sndtimeo(sock->sk, secs);
}

static struct tcp_transport *alloc_transport(struct socket *client_sk)
{
	struct tcp_transport *t;
	struct ksmbd_conn *conn;

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return NULL;
	t->sock = client_sk;

	conn = ksmbd_conn_alloc();
	if (!conn) {
		kfree(t);
		return NULL;
	}

	conn->transport = KSMBD_TRANS(t);
	KSMBD_TRANS(t)->conn = conn;
	KSMBD_TRANS(t)->ops = &ksmbd_tcp_transport_ops;
	return t;
}

static void free_transport(struct tcp_transport *t)
{
	kernel_sock_shutdown(t->sock, SHUT_RDWR);
	sock_release(t->sock);
	t->sock = NULL;

	ksmbd_conn_free(KSMBD_TRANS(t)->conn);
	kfree(t->iov);
	kfree(t);
}

/**
 * kvec_array_init() - initialize a IO vector segment
 * @new:	IO vector to be initialized
 * @iov:	base IO vector
 * @nr_segs:	number of segments in base iov
 * @bytes:	total iovec length so far for read
 *
 * Return:	Number of IO segments
 */
static unsigned int kvec_array_init(struct kvec *new, struct kvec *iov,
				    unsigned int nr_segs, size_t bytes)
{
	size_t base = 0;

	while (bytes || !iov->iov_len) {
		int copy = min(bytes, iov->iov_len);

		bytes -= copy;
		base += copy;
		if (iov->iov_len == base) {
			iov++;
			nr_segs--;
			base = 0;
		}
	}

	memcpy(new, iov, sizeof(*iov) * nr_segs);
	new->iov_base += base;
	new->iov_len -= base;
	return nr_segs;
}

/**
 * get_conn_iovec() - get connection iovec for reading from socket
 * @t:		TCP transport instance
 * @nr_segs:	number of segments in iov
 *
 * Return:	return existing or newly allocate iovec
 */
static struct kvec *get_conn_iovec(struct tcp_transport *t, unsigned int nr_segs)
{
	struct kvec *new_iov;

	if (t->iov && nr_segs <= t->nr_iov)
		return t->iov;

	/* not big enough -- allocate a new one and release the old */
	new_iov = kmalloc_array(nr_segs, sizeof(*new_iov), GFP_KERNEL);
	if (new_iov) {
		kfree(t->iov);
		t->iov = new_iov;
		t->nr_iov = nr_segs;
	}
	return new_iov;
}

static unsigned short ksmbd_tcp_get_port(const struct sockaddr *sa)
{
	switch (sa->sa_family) {
	case AF_INET:
		return ntohs(((struct sockaddr_in *)sa)->sin_port);
	case AF_INET6:
		return ntohs(((struct sockaddr_in6 *)sa)->sin6_port);
	}
	return 0;
}

/**
 * ksmbd_tcp_new_connection() - create a new tcp session on mount
 * @client_sk:	socket associated with new connection
 *
 * whenever a new connection is requested, create a conn thread
 * (session thread) to handle new incoming smb requests from the connection
 *
 * Return:	0 on success, otherwise error
 */
static int ksmbd_tcp_new_connection(struct socket *client_sk)
{
	struct sockaddr *csin;
	int rc = 0;
	struct tcp_transport *t;
	struct task_struct *handler;

	t = alloc_transport(client_sk);
	if (!t) {
		sock_release(client_sk);
		return -ENOMEM;
	}

	csin = KSMBD_TCP_PEER_SOCKADDR(KSMBD_TRANS(t)->conn);
	if (kernel_getpeername(client_sk, csin) < 0) {
		pr_err("client ip resolution failed\n");
		rc = -EINVAL;
		goto out_error;
	}

	handler = kthread_run(ksmbd_conn_handler_loop,
			      KSMBD_TRANS(t)->conn,
			      "ksmbd:%u",
			      ksmbd_tcp_get_port(csin));
	if (IS_ERR(handler)) {
		pr_err("cannot start conn thread\n");
		rc = PTR_ERR(handler);
		free_transport(t);
	}
	return rc;

out_error:
	free_transport(t);
	return rc;
}

/**
 * ksmbd_kthread_fn() - listen to new SMB connections and callback server
 * @p:		arguments to forker thread
 *
 * Return:	0 on success, error number otherwise
 */
static int ksmbd_kthread_fn(void *p)
{
	struct socket *client_sk = NULL;
	struct interface *iface = (struct interface *)p;
	int ret;

	while (!kthread_should_stop()) {
		mutex_lock(&iface->sock_release_lock);
		if (!iface->ksmbd_socket) {
			mutex_unlock(&iface->sock_release_lock);
			break;
		}
		ret = kernel_accept(iface->ksmbd_socket, &client_sk,
				    SOCK_NONBLOCK);
		mutex_unlock(&iface->sock_release_lock);
		if (ret) {
			if (ret == -EAGAIN)
				/* check for new connections every 100 msecs */
				schedule_timeout_interruptible(HZ / 10);
			continue;
		}

		if (server_conf.max_connections &&
		    atomic_inc_return(&active_num_conn) >= server_conf.max_connections) {
			pr_info_ratelimited("Limit the maximum number of connections(%u)\n",
					    atomic_read(&active_num_conn));
			atomic_dec(&active_num_conn);
			sock_release(client_sk);
			continue;
		}

		ksmbd_debug(CONN, "connect success: accepted new connection\n");
		client_sk->sk->sk_rcvtimeo = KSMBD_TCP_RECV_TIMEOUT;
		client_sk->sk->sk_sndtimeo = KSMBD_TCP_SEND_TIMEOUT;

		ksmbd_tcp_new_connection(client_sk);
	}

	ksmbd_debug(CONN, "releasing socket\n");
	return 0;
}

/**
 * ksmbd_tcp_run_kthread() - start forker thread
 * @iface: pointer to struct interface
 *
 * start forker thread(ksmbd/0) at module init time to listen
 * on port 445 for new SMB connection requests. It creates per connection
 * server threads(ksmbd/x)
 *
 * Return:	0 on success or error number
 */
static int ksmbd_tcp_run_kthread(struct interface *iface)
{
	int rc;
	struct task_struct *kthread;

	kthread = kthread_run(ksmbd_kthread_fn, (void *)iface, "ksmbd-%s",
			      iface->name);
	if (IS_ERR(kthread)) {
		rc = PTR_ERR(kthread);
		return rc;
	}
	iface->ksmbd_kthread = kthread;

	return 0;
}

/**
 * ksmbd_tcp_readv() - read data from socket in given iovec
 * @t:			TCP transport instance
 * @iov_orig:		base IO vector
 * @nr_segs:		number of segments in base iov
 * @to_read:		number of bytes to read from socket
 * @max_retries:	maximum retry count
 *
 * Return:	on success return number of bytes read from socket,
 *		otherwise return error number
 */
static int ksmbd_tcp_readv(struct tcp_transport *t, struct kvec *iov_orig,
			   unsigned int nr_segs, unsigned int to_read,
			   int max_retries)
{
	int length = 0;
	int total_read;
	unsigned int segs;
	struct msghdr ksmbd_msg;
	struct kvec *iov;
	struct ksmbd_conn *conn = KSMBD_TRANS(t)->conn;

	iov = get_conn_iovec(t, nr_segs);
	if (!iov)
		return -ENOMEM;

	ksmbd_msg.msg_control = NULL;
	ksmbd_msg.msg_controllen = 0;

	for (total_read = 0; to_read; total_read += length, to_read -= length) {
		try_to_freeze();

		if (!ksmbd_conn_alive(conn)) {
			total_read = -ESHUTDOWN;
			break;
		}
		segs = kvec_array_init(iov, iov_orig, nr_segs, total_read);

		length = kernel_recvmsg(t->sock, &ksmbd_msg,
					iov, segs, to_read, 0);

		if (length == -EINTR) {
			total_read = -ESHUTDOWN;
			break;
		} else if (ksmbd_conn_need_reconnect(conn)) {
			total_read = -EAGAIN;
			break;
		} else if (length == -ERESTARTSYS || length == -EAGAIN) {
			/*
			 * If max_retries is negative, Allow unlimited
			 * retries to keep connection with inactive sessions.
			 */
			if (max_retries == 0) {
				total_read = length;
				break;
			} else if (max_retries > 0) {
				max_retries--;
			}

			usleep_range(1000, 2000);
			length = 0;
			continue;
		} else if (length <= 0) {
			total_read = length;
			break;
		}
	}
	return total_read;
}

/**
 * ksmbd_tcp_read() - read data from socket in given buffer
 * @t:		TCP transport instance
 * @buf:	buffer to store read data from socket
 * @to_read:	number of bytes to read from socket
 *
 * Return:	on success return number of bytes read from socket,
 *		otherwise return error number
 */
static int ksmbd_tcp_read(struct ksmbd_transport *t, char *buf,
			  unsigned int to_read, int max_retries)
{
	struct kvec iov;

	iov.iov_base = buf;
	iov.iov_len = to_read;

	return ksmbd_tcp_readv(TCP_TRANS(t), &iov, 1, to_read, max_retries);
}

static int ksmbd_tcp_writev(struct ksmbd_transport *t, struct kvec *iov,
			    int nvecs, int size, bool need_invalidate,
			    unsigned int remote_key)

{
	struct msghdr smb_msg = {.msg_flags = MSG_NOSIGNAL};

	return kernel_sendmsg(TCP_TRANS(t)->sock, &smb_msg, iov, nvecs, size);
}

static void ksmbd_tcp_disconnect(struct ksmbd_transport *t)
{
	free_transport(TCP_TRANS(t));
	if (server_conf.max_connections)
		atomic_dec(&active_num_conn);
}

static void tcp_destroy_socket(struct socket *ksmbd_socket)
{
	int ret;

	if (!ksmbd_socket)
		return;

	/* set zero to timeout */
	ksmbd_tcp_rcv_timeout(ksmbd_socket, 0);
	ksmbd_tcp_snd_timeout(ksmbd_socket, 0);

	ret = kernel_sock_shutdown(ksmbd_socket, SHUT_RDWR);
	if (ret)
		pr_err("Failed to shutdown socket: %d\n", ret);
	sock_release(ksmbd_socket);
}

/**
 * create_socket - create socket for ksmbd/0
 *
 * Return:	0 on success, error number otherwise
 */
static int create_socket(struct interface *iface)
{
	int ret;
	struct sockaddr_in6 sin6;
	struct sockaddr_in sin;
	struct socket *ksmbd_socket;
	bool ipv4 = false;

	ret = sock_create(PF_INET6, SOCK_STREAM, IPPROTO_TCP, &ksmbd_socket);
	if (ret) {
		if (ret != -EAFNOSUPPORT)
			pr_err("Can't create socket for ipv6, fallback to ipv4: %d\n", ret);
		ret = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP,
				  &ksmbd_socket);
		if (ret) {
			pr_err("Can't create socket for ipv4: %d\n", ret);
			goto out_clear;
		}

		sin.sin_family = PF_INET;
		sin.sin_addr.s_addr = htonl(INADDR_ANY);
		sin.sin_port = htons(server_conf.tcp_port);
		ipv4 = true;
	} else {
		sin6.sin6_family = PF_INET6;
		sin6.sin6_addr = in6addr_any;
		sin6.sin6_port = htons(server_conf.tcp_port);
	}

	ksmbd_tcp_nodelay(ksmbd_socket);
	ksmbd_tcp_reuseaddr(ksmbd_socket);

	ret = sock_setsockopt(ksmbd_socket,
			      SOL_SOCKET,
			      SO_BINDTODEVICE,
			      KERNEL_SOCKPTR(iface->name),
			      strlen(iface->name));
	if (ret != -ENODEV && ret < 0) {
		pr_err("Failed to set SO_BINDTODEVICE: %d\n", ret);
		goto out_error;
	}

	if (ipv4)
		ret = kernel_bind(ksmbd_socket, (struct sockaddr *)&sin,
				  sizeof(sin));
	else
		ret = kernel_bind(ksmbd_socket, (struct sockaddr *)&sin6,
				  sizeof(sin6));
	if (ret) {
		pr_err("Failed to bind socket: %d\n", ret);
		goto out_error;
	}

	ksmbd_socket->sk->sk_rcvtimeo = KSMBD_TCP_RECV_TIMEOUT;
	ksmbd_socket->sk->sk_sndtimeo = KSMBD_TCP_SEND_TIMEOUT;

	ret = kernel_listen(ksmbd_socket, KSMBD_SOCKET_BACKLOG);
	if (ret) {
		pr_err("Port listen() error: %d\n", ret);
		goto out_error;
	}

	iface->ksmbd_socket = ksmbd_socket;
	ret = ksmbd_tcp_run_kthread(iface);
	if (ret) {
		pr_err("Can't start ksmbd main kthread: %d\n", ret);
		goto out_error;
	}
	iface->state = IFACE_STATE_CONFIGURED;

	return 0;

out_error:
	tcp_destroy_socket(ksmbd_socket);
out_clear:
	iface->ksmbd_socket = NULL;
	return ret;
}

static int ksmbd_netdev_event(struct notifier_block *nb, unsigned long event,
			      void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);
	struct interface *iface;
	int ret, found = 0;

	switch (event) {
	case NETDEV_UP:
		if (netif_is_bridge_port(netdev))
			return NOTIFY_OK;

		list_for_each_entry(iface, &iface_list, entry) {
			if (!strcmp(iface->name, netdev->name)) {
				found = 1;
				if (iface->state != IFACE_STATE_DOWN)
					break;
				ret = create_socket(iface);
				if (ret)
					return NOTIFY_OK;
				break;
			}
		}
		if (!found && bind_additional_ifaces) {
			iface = alloc_iface(kstrdup(netdev->name, GFP_KERNEL));
			if (!iface)
				return NOTIFY_OK;
			ret = create_socket(iface);
			if (ret)
				break;
		}
		break;
	case NETDEV_DOWN:
		list_for_each_entry(iface, &iface_list, entry) {
			if (!strcmp(iface->name, netdev->name) &&
			    iface->state == IFACE_STATE_CONFIGURED) {
				tcp_stop_kthread(iface->ksmbd_kthread);
				iface->ksmbd_kthread = NULL;
				mutex_lock(&iface->sock_release_lock);
				tcp_destroy_socket(iface->ksmbd_socket);
				iface->ksmbd_socket = NULL;
				mutex_unlock(&iface->sock_release_lock);

				iface->state = IFACE_STATE_DOWN;
				break;
			}
		}
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block ksmbd_netdev_notifier = {
	.notifier_call = ksmbd_netdev_event,
};

int ksmbd_tcp_init(void)
{
	register_netdevice_notifier(&ksmbd_netdev_notifier);

	return 0;
}

static void tcp_stop_kthread(struct task_struct *kthread)
{
	int ret;

	if (!kthread)
		return;

	ret = kthread_stop(kthread);
	if (ret)
		pr_err("failed to stop forker thread\n");
}

void ksmbd_tcp_destroy(void)
{
	struct interface *iface, *tmp;

	unregister_netdevice_notifier(&ksmbd_netdev_notifier);

	list_for_each_entry_safe(iface, tmp, &iface_list, entry) {
		list_del(&iface->entry);
		kfree(iface->name);
		kfree(iface);
	}
}

static struct interface *alloc_iface(char *ifname)
{
	struct interface *iface;

	if (!ifname)
		return NULL;

	iface = kzalloc(sizeof(struct interface), GFP_KERNEL);
	if (!iface) {
		kfree(ifname);
		return NULL;
	}

	iface->name = ifname;
	iface->state = IFACE_STATE_DOWN;
	list_add(&iface->entry, &iface_list);
	mutex_init(&iface->sock_release_lock);
	return iface;
}

int ksmbd_tcp_set_interfaces(char *ifc_list, int ifc_list_sz)
{
	int sz = 0;

	if (!ifc_list_sz) {
		struct net_device *netdev;

		rtnl_lock();
		for_each_netdev(&init_net, netdev) {
			if (netif_is_bridge_port(netdev))
				continue;
			if (!alloc_iface(kstrdup(netdev->name, GFP_KERNEL)))
				return -ENOMEM;
		}
		rtnl_unlock();
		bind_additional_ifaces = 1;
		return 0;
	}

	while (ifc_list_sz > 0) {
		if (!alloc_iface(kstrdup(ifc_list, GFP_KERNEL)))
			return -ENOMEM;

		sz = strlen(ifc_list);
		if (!sz)
			break;

		ifc_list += sz + 1;
		ifc_list_sz -= (sz + 1);
	}

	bind_additional_ifaces = 0;

	return 0;
}

static struct ksmbd_transport_ops ksmbd_tcp_transport_ops = {
	.read		= ksmbd_tcp_read,
	.writev		= ksmbd_tcp_writev,
	.disconnect	= ksmbd_tcp_disconnect,
};
