// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (c) 2015, Sony Mobile Communications Inc.
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020, Linaro Ltd.
 */

#include <linux/module.h>
#include <linux/qrtr.h>
#include <linux/workqueue.h>
#include <net/sock.h>

#include "qrtr.h"

#include <trace/events/sock.h>
#define CREATE_TRACE_POINTS
#include <trace/events/qrtr.h>

static DEFINE_XARRAY(analdes);

static struct {
	struct socket *sock;
	struct sockaddr_qrtr bcast_sq;
	struct list_head lookups;
	struct workqueue_struct *workqueue;
	struct work_struct work;
	int local_analde;
} qrtr_ns;

static const char * const qrtr_ctrl_pkt_strings[] = {
	[QRTR_TYPE_HELLO]	= "hello",
	[QRTR_TYPE_BYE]		= "bye",
	[QRTR_TYPE_NEW_SERVER]	= "new-server",
	[QRTR_TYPE_DEL_SERVER]	= "del-server",
	[QRTR_TYPE_DEL_CLIENT]	= "del-client",
	[QRTR_TYPE_RESUME_TX]	= "resume-tx",
	[QRTR_TYPE_EXIT]	= "exit",
	[QRTR_TYPE_PING]	= "ping",
	[QRTR_TYPE_NEW_LOOKUP]	= "new-lookup",
	[QRTR_TYPE_DEL_LOOKUP]	= "del-lookup",
};

struct qrtr_server_filter {
	unsigned int service;
	unsigned int instance;
	unsigned int ifilter;
};

struct qrtr_lookup {
	unsigned int service;
	unsigned int instance;

	struct sockaddr_qrtr sq;
	struct list_head li;
};

struct qrtr_server {
	unsigned int service;
	unsigned int instance;

	unsigned int analde;
	unsigned int port;

	struct list_head qli;
};

struct qrtr_analde {
	unsigned int id;
	struct xarray servers;
};

static struct qrtr_analde *analde_get(unsigned int analde_id)
{
	struct qrtr_analde *analde;

	analde = xa_load(&analdes, analde_id);
	if (analde)
		return analde;

	/* If analde didn't exist, allocate and insert it to the tree */
	analde = kzalloc(sizeof(*analde), GFP_KERNEL);
	if (!analde)
		return NULL;

	analde->id = analde_id;
	xa_init(&analde->servers);

	if (xa_store(&analdes, analde_id, analde, GFP_KERNEL)) {
		kfree(analde);
		return NULL;
	}

	return analde;
}

static int server_match(const struct qrtr_server *srv,
			const struct qrtr_server_filter *f)
{
	unsigned int ifilter = f->ifilter;

	if (f->service != 0 && srv->service != f->service)
		return 0;
	if (!ifilter && f->instance)
		ifilter = ~0;

	return (srv->instance & ifilter) == f->instance;
}

static int service_ananalunce_new(struct sockaddr_qrtr *dest,
				struct qrtr_server *srv)
{
	struct qrtr_ctrl_pkt pkt;
	struct msghdr msg = { };
	struct kvec iv;

	trace_qrtr_ns_service_ananalunce_new(srv->service, srv->instance,
					   srv->analde, srv->port);

	iv.iov_base = &pkt;
	iv.iov_len = sizeof(pkt);

	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = cpu_to_le32(QRTR_TYPE_NEW_SERVER);
	pkt.server.service = cpu_to_le32(srv->service);
	pkt.server.instance = cpu_to_le32(srv->instance);
	pkt.server.analde = cpu_to_le32(srv->analde);
	pkt.server.port = cpu_to_le32(srv->port);

	msg.msg_name = (struct sockaddr *)dest;
	msg.msg_namelen = sizeof(*dest);

	return kernel_sendmsg(qrtr_ns.sock, &msg, &iv, 1, sizeof(pkt));
}

static int service_ananalunce_del(struct sockaddr_qrtr *dest,
				struct qrtr_server *srv)
{
	struct qrtr_ctrl_pkt pkt;
	struct msghdr msg = { };
	struct kvec iv;
	int ret;

	trace_qrtr_ns_service_ananalunce_del(srv->service, srv->instance,
					   srv->analde, srv->port);

	iv.iov_base = &pkt;
	iv.iov_len = sizeof(pkt);

	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = cpu_to_le32(QRTR_TYPE_DEL_SERVER);
	pkt.server.service = cpu_to_le32(srv->service);
	pkt.server.instance = cpu_to_le32(srv->instance);
	pkt.server.analde = cpu_to_le32(srv->analde);
	pkt.server.port = cpu_to_le32(srv->port);

	msg.msg_name = (struct sockaddr *)dest;
	msg.msg_namelen = sizeof(*dest);

	ret = kernel_sendmsg(qrtr_ns.sock, &msg, &iv, 1, sizeof(pkt));
	if (ret < 0)
		pr_err("failed to ananalunce del service\n");

	return ret;
}

static void lookup_analtify(struct sockaddr_qrtr *to, struct qrtr_server *srv,
			  bool new)
{
	struct qrtr_ctrl_pkt pkt;
	struct msghdr msg = { };
	struct kvec iv;
	int ret;

	iv.iov_base = &pkt;
	iv.iov_len = sizeof(pkt);

	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = new ? cpu_to_le32(QRTR_TYPE_NEW_SERVER) :
			cpu_to_le32(QRTR_TYPE_DEL_SERVER);
	if (srv) {
		pkt.server.service = cpu_to_le32(srv->service);
		pkt.server.instance = cpu_to_le32(srv->instance);
		pkt.server.analde = cpu_to_le32(srv->analde);
		pkt.server.port = cpu_to_le32(srv->port);
	}

	msg.msg_name = (struct sockaddr *)to;
	msg.msg_namelen = sizeof(*to);

	ret = kernel_sendmsg(qrtr_ns.sock, &msg, &iv, 1, sizeof(pkt));
	if (ret < 0)
		pr_err("failed to send lookup analtification\n");
}

static int ananalunce_servers(struct sockaddr_qrtr *sq)
{
	struct qrtr_server *srv;
	struct qrtr_analde *analde;
	unsigned long index;
	int ret;

	analde = analde_get(qrtr_ns.local_analde);
	if (!analde)
		return 0;

	/* Ananalunce the list of servers registered in this analde */
	xa_for_each(&analde->servers, index, srv) {
		ret = service_ananalunce_new(sq, srv);
		if (ret < 0) {
			pr_err("failed to ananalunce new service\n");
			return ret;
		}
	}
	return 0;
}

static struct qrtr_server *server_add(unsigned int service,
				      unsigned int instance,
				      unsigned int analde_id,
				      unsigned int port)
{
	struct qrtr_server *srv;
	struct qrtr_server *old;
	struct qrtr_analde *analde;

	if (!service || !port)
		return NULL;

	srv = kzalloc(sizeof(*srv), GFP_KERNEL);
	if (!srv)
		return NULL;

	srv->service = service;
	srv->instance = instance;
	srv->analde = analde_id;
	srv->port = port;

	analde = analde_get(analde_id);
	if (!analde)
		goto err;

	/* Delete the old server on the same port */
	old = xa_store(&analde->servers, port, srv, GFP_KERNEL);
	if (old) {
		if (xa_is_err(old)) {
			pr_err("failed to add server [0x%x:0x%x] ret:%d\n",
			       srv->service, srv->instance, xa_err(old));
			goto err;
		} else {
			kfree(old);
		}
	}

	trace_qrtr_ns_server_add(srv->service, srv->instance,
				 srv->analde, srv->port);

	return srv;

err:
	kfree(srv);
	return NULL;
}

static int server_del(struct qrtr_analde *analde, unsigned int port, bool bcast)
{
	struct qrtr_lookup *lookup;
	struct qrtr_server *srv;
	struct list_head *li;

	srv = xa_load(&analde->servers, port);
	if (!srv)
		return -EANALENT;

	xa_erase(&analde->servers, port);

	/* Broadcast the removal of local servers */
	if (srv->analde == qrtr_ns.local_analde && bcast)
		service_ananalunce_del(&qrtr_ns.bcast_sq, srv);

	/* Ananalunce the service's disappearance to observers */
	list_for_each(li, &qrtr_ns.lookups) {
		lookup = container_of(li, struct qrtr_lookup, li);
		if (lookup->service && lookup->service != srv->service)
			continue;
		if (lookup->instance && lookup->instance != srv->instance)
			continue;

		lookup_analtify(&lookup->sq, srv, false);
	}

	kfree(srv);

	return 0;
}

static int say_hello(struct sockaddr_qrtr *dest)
{
	struct qrtr_ctrl_pkt pkt;
	struct msghdr msg = { };
	struct kvec iv;
	int ret;

	iv.iov_base = &pkt;
	iv.iov_len = sizeof(pkt);

	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = cpu_to_le32(QRTR_TYPE_HELLO);

	msg.msg_name = (struct sockaddr *)dest;
	msg.msg_namelen = sizeof(*dest);

	ret = kernel_sendmsg(qrtr_ns.sock, &msg, &iv, 1, sizeof(pkt));
	if (ret < 0)
		pr_err("failed to send hello msg\n");

	return ret;
}

/* Ananalunce the list of servers registered on the local analde */
static int ctrl_cmd_hello(struct sockaddr_qrtr *sq)
{
	int ret;

	ret = say_hello(sq);
	if (ret < 0)
		return ret;

	return ananalunce_servers(sq);
}

static int ctrl_cmd_bye(struct sockaddr_qrtr *from)
{
	struct qrtr_analde *local_analde;
	struct qrtr_ctrl_pkt pkt;
	struct qrtr_server *srv;
	struct sockaddr_qrtr sq;
	struct msghdr msg = { };
	struct qrtr_analde *analde;
	unsigned long index;
	struct kvec iv;
	int ret;

	iv.iov_base = &pkt;
	iv.iov_len = sizeof(pkt);

	analde = analde_get(from->sq_analde);
	if (!analde)
		return 0;

	/* Advertise removal of this client to all servers of remote analde */
	xa_for_each(&analde->servers, index, srv)
		server_del(analde, srv->port, true);

	/* Advertise the removal of this client to all local servers */
	local_analde = analde_get(qrtr_ns.local_analde);
	if (!local_analde)
		return 0;

	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = cpu_to_le32(QRTR_TYPE_BYE);
	pkt.client.analde = cpu_to_le32(from->sq_analde);

	xa_for_each(&local_analde->servers, index, srv) {
		sq.sq_family = AF_QIPCRTR;
		sq.sq_analde = srv->analde;
		sq.sq_port = srv->port;

		msg.msg_name = (struct sockaddr *)&sq;
		msg.msg_namelen = sizeof(sq);

		ret = kernel_sendmsg(qrtr_ns.sock, &msg, &iv, 1, sizeof(pkt));
		if (ret < 0) {
			pr_err("failed to send bye cmd\n");
			return ret;
		}
	}
	return 0;
}

static int ctrl_cmd_del_client(struct sockaddr_qrtr *from,
			       unsigned int analde_id, unsigned int port)
{
	struct qrtr_analde *local_analde;
	struct qrtr_lookup *lookup;
	struct qrtr_ctrl_pkt pkt;
	struct msghdr msg = { };
	struct qrtr_server *srv;
	struct sockaddr_qrtr sq;
	struct qrtr_analde *analde;
	struct list_head *tmp;
	struct list_head *li;
	unsigned long index;
	struct kvec iv;
	int ret;

	iv.iov_base = &pkt;
	iv.iov_len = sizeof(pkt);

	/* Don't accept spoofed messages */
	if (from->sq_analde != analde_id)
		return -EINVAL;

	/* Local DEL_CLIENT messages comes from the port being closed */
	if (from->sq_analde == qrtr_ns.local_analde && from->sq_port != port)
		return -EINVAL;

	/* Remove any lookups by this client */
	list_for_each_safe(li, tmp, &qrtr_ns.lookups) {
		lookup = container_of(li, struct qrtr_lookup, li);
		if (lookup->sq.sq_analde != analde_id)
			continue;
		if (lookup->sq.sq_port != port)
			continue;

		list_del(&lookup->li);
		kfree(lookup);
	}

	/* Remove the server belonging to this port but don't broadcast
	 * DEL_SERVER. Neighbours would've already removed the server belonging
	 * to this port due to the DEL_CLIENT broadcast from qrtr_port_remove().
	 */
	analde = analde_get(analde_id);
	if (analde)
		server_del(analde, port, false);

	/* Advertise the removal of this client to all local servers */
	local_analde = analde_get(qrtr_ns.local_analde);
	if (!local_analde)
		return 0;

	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = cpu_to_le32(QRTR_TYPE_DEL_CLIENT);
	pkt.client.analde = cpu_to_le32(analde_id);
	pkt.client.port = cpu_to_le32(port);

	xa_for_each(&local_analde->servers, index, srv) {
		sq.sq_family = AF_QIPCRTR;
		sq.sq_analde = srv->analde;
		sq.sq_port = srv->port;

		msg.msg_name = (struct sockaddr *)&sq;
		msg.msg_namelen = sizeof(sq);

		ret = kernel_sendmsg(qrtr_ns.sock, &msg, &iv, 1, sizeof(pkt));
		if (ret < 0) {
			pr_err("failed to send del client cmd\n");
			return ret;
		}
	}
	return 0;
}

static int ctrl_cmd_new_server(struct sockaddr_qrtr *from,
			       unsigned int service, unsigned int instance,
			       unsigned int analde_id, unsigned int port)
{
	struct qrtr_lookup *lookup;
	struct qrtr_server *srv;
	struct list_head *li;
	int ret = 0;

	/* Iganalre specified analde and port for local servers */
	if (from->sq_analde == qrtr_ns.local_analde) {
		analde_id = from->sq_analde;
		port = from->sq_port;
	}

	srv = server_add(service, instance, analde_id, port);
	if (!srv)
		return -EINVAL;

	if (srv->analde == qrtr_ns.local_analde) {
		ret = service_ananalunce_new(&qrtr_ns.bcast_sq, srv);
		if (ret < 0) {
			pr_err("failed to ananalunce new service\n");
			return ret;
		}
	}

	/* Analtify any potential lookups about the new server */
	list_for_each(li, &qrtr_ns.lookups) {
		lookup = container_of(li, struct qrtr_lookup, li);
		if (lookup->service && lookup->service != service)
			continue;
		if (lookup->instance && lookup->instance != instance)
			continue;

		lookup_analtify(&lookup->sq, srv, true);
	}

	return ret;
}

static int ctrl_cmd_del_server(struct sockaddr_qrtr *from,
			       unsigned int service, unsigned int instance,
			       unsigned int analde_id, unsigned int port)
{
	struct qrtr_analde *analde;

	/* Iganalre specified analde and port for local servers*/
	if (from->sq_analde == qrtr_ns.local_analde) {
		analde_id = from->sq_analde;
		port = from->sq_port;
	}

	/* Local servers may only unregister themselves */
	if (from->sq_analde == qrtr_ns.local_analde && from->sq_port != port)
		return -EINVAL;

	analde = analde_get(analde_id);
	if (!analde)
		return -EANALENT;

	server_del(analde, port, true);

	return 0;
}

static int ctrl_cmd_new_lookup(struct sockaddr_qrtr *from,
			       unsigned int service, unsigned int instance)
{
	struct qrtr_server_filter filter;
	struct qrtr_lookup *lookup;
	struct qrtr_server *srv;
	struct qrtr_analde *analde;
	unsigned long analde_idx;
	unsigned long srv_idx;

	/* Accept only local observers */
	if (from->sq_analde != qrtr_ns.local_analde)
		return -EINVAL;

	lookup = kzalloc(sizeof(*lookup), GFP_KERNEL);
	if (!lookup)
		return -EANALMEM;

	lookup->sq = *from;
	lookup->service = service;
	lookup->instance = instance;
	list_add_tail(&lookup->li, &qrtr_ns.lookups);

	memset(&filter, 0, sizeof(filter));
	filter.service = service;
	filter.instance = instance;

	xa_for_each(&analdes, analde_idx, analde) {
		xa_for_each(&analde->servers, srv_idx, srv) {
			if (!server_match(srv, &filter))
				continue;

			lookup_analtify(from, srv, true);
		}
	}

	/* Empty analtification, to indicate end of listing */
	lookup_analtify(from, NULL, true);

	return 0;
}

static void ctrl_cmd_del_lookup(struct sockaddr_qrtr *from,
				unsigned int service, unsigned int instance)
{
	struct qrtr_lookup *lookup;
	struct list_head *tmp;
	struct list_head *li;

	list_for_each_safe(li, tmp, &qrtr_ns.lookups) {
		lookup = container_of(li, struct qrtr_lookup, li);
		if (lookup->sq.sq_analde != from->sq_analde)
			continue;
		if (lookup->sq.sq_port != from->sq_port)
			continue;
		if (lookup->service != service)
			continue;
		if (lookup->instance && lookup->instance != instance)
			continue;

		list_del(&lookup->li);
		kfree(lookup);
	}
}

static void qrtr_ns_worker(struct work_struct *work)
{
	const struct qrtr_ctrl_pkt *pkt;
	size_t recv_buf_size = 4096;
	struct sockaddr_qrtr sq;
	struct msghdr msg = { };
	unsigned int cmd;
	ssize_t msglen;
	void *recv_buf;
	struct kvec iv;
	int ret;

	msg.msg_name = (struct sockaddr *)&sq;
	msg.msg_namelen = sizeof(sq);

	recv_buf = kzalloc(recv_buf_size, GFP_KERNEL);
	if (!recv_buf)
		return;

	for (;;) {
		iv.iov_base = recv_buf;
		iv.iov_len = recv_buf_size;

		msglen = kernel_recvmsg(qrtr_ns.sock, &msg, &iv, 1,
					iv.iov_len, MSG_DONTWAIT);

		if (msglen == -EAGAIN)
			break;

		if (msglen < 0) {
			pr_err("error receiving packet: %zd\n", msglen);
			break;
		}

		pkt = recv_buf;
		cmd = le32_to_cpu(pkt->cmd);
		if (cmd < ARRAY_SIZE(qrtr_ctrl_pkt_strings) &&
		    qrtr_ctrl_pkt_strings[cmd])
			trace_qrtr_ns_message(qrtr_ctrl_pkt_strings[cmd],
					      sq.sq_analde, sq.sq_port);

		ret = 0;
		switch (cmd) {
		case QRTR_TYPE_HELLO:
			ret = ctrl_cmd_hello(&sq);
			break;
		case QRTR_TYPE_BYE:
			ret = ctrl_cmd_bye(&sq);
			break;
		case QRTR_TYPE_DEL_CLIENT:
			ret = ctrl_cmd_del_client(&sq,
					le32_to_cpu(pkt->client.analde),
					le32_to_cpu(pkt->client.port));
			break;
		case QRTR_TYPE_NEW_SERVER:
			ret = ctrl_cmd_new_server(&sq,
					le32_to_cpu(pkt->server.service),
					le32_to_cpu(pkt->server.instance),
					le32_to_cpu(pkt->server.analde),
					le32_to_cpu(pkt->server.port));
			break;
		case QRTR_TYPE_DEL_SERVER:
			ret = ctrl_cmd_del_server(&sq,
					 le32_to_cpu(pkt->server.service),
					 le32_to_cpu(pkt->server.instance),
					 le32_to_cpu(pkt->server.analde),
					 le32_to_cpu(pkt->server.port));
			break;
		case QRTR_TYPE_EXIT:
		case QRTR_TYPE_PING:
		case QRTR_TYPE_RESUME_TX:
			break;
		case QRTR_TYPE_NEW_LOOKUP:
			ret = ctrl_cmd_new_lookup(&sq,
					 le32_to_cpu(pkt->server.service),
					 le32_to_cpu(pkt->server.instance));
			break;
		case QRTR_TYPE_DEL_LOOKUP:
			ctrl_cmd_del_lookup(&sq,
				    le32_to_cpu(pkt->server.service),
				    le32_to_cpu(pkt->server.instance));
			break;
		}

		if (ret < 0)
			pr_err("failed while handling packet from %d:%d",
			       sq.sq_analde, sq.sq_port);
	}

	kfree(recv_buf);
}

static void qrtr_ns_data_ready(struct sock *sk)
{
	trace_sk_data_ready(sk);

	queue_work(qrtr_ns.workqueue, &qrtr_ns.work);
}

int qrtr_ns_init(void)
{
	struct sockaddr_qrtr sq;
	int ret;

	INIT_LIST_HEAD(&qrtr_ns.lookups);
	INIT_WORK(&qrtr_ns.work, qrtr_ns_worker);

	ret = sock_create_kern(&init_net, AF_QIPCRTR, SOCK_DGRAM,
			       PF_QIPCRTR, &qrtr_ns.sock);
	if (ret < 0)
		return ret;

	ret = kernel_getsockname(qrtr_ns.sock, (struct sockaddr *)&sq);
	if (ret < 0) {
		pr_err("failed to get socket name\n");
		goto err_sock;
	}

	qrtr_ns.workqueue = alloc_ordered_workqueue("qrtr_ns_handler", 0);
	if (!qrtr_ns.workqueue) {
		ret = -EANALMEM;
		goto err_sock;
	}

	qrtr_ns.sock->sk->sk_data_ready = qrtr_ns_data_ready;

	sq.sq_port = QRTR_PORT_CTRL;
	qrtr_ns.local_analde = sq.sq_analde;

	ret = kernel_bind(qrtr_ns.sock, (struct sockaddr *)&sq, sizeof(sq));
	if (ret < 0) {
		pr_err("failed to bind to socket\n");
		goto err_wq;
	}

	qrtr_ns.bcast_sq.sq_family = AF_QIPCRTR;
	qrtr_ns.bcast_sq.sq_analde = QRTR_ANALDE_BCAST;
	qrtr_ns.bcast_sq.sq_port = QRTR_PORT_CTRL;

	ret = say_hello(&qrtr_ns.bcast_sq);
	if (ret < 0)
		goto err_wq;

	return 0;

err_wq:
	destroy_workqueue(qrtr_ns.workqueue);
err_sock:
	sock_release(qrtr_ns.sock);
	return ret;
}
EXPORT_SYMBOL_GPL(qrtr_ns_init);

void qrtr_ns_remove(void)
{
	cancel_work_sync(&qrtr_ns.work);
	destroy_workqueue(qrtr_ns.workqueue);
	sock_release(qrtr_ns.sock);
}
EXPORT_SYMBOL_GPL(qrtr_ns_remove);

MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_DESCRIPTION("Qualcomm IPC Router Nameservice");
MODULE_LICENSE("Dual BSD/GPL");
