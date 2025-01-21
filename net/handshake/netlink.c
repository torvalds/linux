// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic netlink handshake service
 *
 * Author: Chuck Lever <chuck.lever@oracle.com>
 *
 * Copyright (c) 2023, Oracle and/or its affiliates.
 */

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/mm.h>

#include <net/sock.h>
#include <net/genetlink.h>
#include <net/netns/generic.h>

#include <kunit/visibility.h>

#include <uapi/linux/handshake.h>
#include "handshake.h"
#include "genl.h"

#include <trace/events/handshake.h>

/**
 * handshake_genl_notify - Notify handlers that a request is waiting
 * @net: target network namespace
 * @proto: handshake protocol
 * @flags: memory allocation control flags
 *
 * Returns zero on success or a negative errno if notification failed.
 */
int handshake_genl_notify(struct net *net, const struct handshake_proto *proto,
			  gfp_t flags)
{
	struct sk_buff *msg;
	void *hdr;

	/* Disable notifications during unit testing */
	if (!test_bit(HANDSHAKE_F_PROTO_NOTIFY, &proto->hp_flags))
		return 0;

	if (!genl_has_listeners(&handshake_nl_family, net,
				proto->hp_handler_class))
		return -ESRCH;

	msg = genlmsg_new(GENLMSG_DEFAULT_SIZE, flags);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &handshake_nl_family, 0,
			  HANDSHAKE_CMD_READY);
	if (!hdr)
		goto out_free;

	if (nla_put_u32(msg, HANDSHAKE_A_ACCEPT_HANDLER_CLASS,
			proto->hp_handler_class) < 0) {
		genlmsg_cancel(msg, hdr);
		goto out_free;
	}

	genlmsg_end(msg, hdr);
	return genlmsg_multicast_netns(&handshake_nl_family, net, msg,
				       0, proto->hp_handler_class, flags);

out_free:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

/**
 * handshake_genl_put - Create a generic netlink message header
 * @msg: buffer in which to create the header
 * @info: generic netlink message context
 *
 * Returns a ready-to-use header, or NULL.
 */
struct nlmsghdr *handshake_genl_put(struct sk_buff *msg,
				    struct genl_info *info)
{
	return genlmsg_put(msg, info->snd_portid, info->snd_seq,
			   &handshake_nl_family, 0, info->genlhdr->cmd);
}
EXPORT_SYMBOL(handshake_genl_put);

int handshake_nl_accept_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = sock_net(skb->sk);
	struct handshake_net *hn = handshake_pernet(net);
	struct handshake_req *req = NULL;
	struct socket *sock;
	int class, fd, err;

	err = -EOPNOTSUPP;
	if (!hn)
		goto out_status;

	err = -EINVAL;
	if (GENL_REQ_ATTR_CHECK(info, HANDSHAKE_A_ACCEPT_HANDLER_CLASS))
		goto out_status;
	class = nla_get_u32(info->attrs[HANDSHAKE_A_ACCEPT_HANDLER_CLASS]);

	err = -EAGAIN;
	req = handshake_req_next(hn, class);
	if (!req)
		goto out_status;

	sock = req->hr_sk->sk_socket;
	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		err = fd;
		goto out_complete;
	}

	err = req->hr_proto->hp_accept(req, info, fd);
	if (err) {
		put_unused_fd(fd);
		goto out_complete;
	}

	fd_install(fd, get_file(sock->file));

	trace_handshake_cmd_accept(net, req, req->hr_sk, fd);
	return 0;

out_complete:
	handshake_complete(req, -EIO, NULL);
out_status:
	trace_handshake_cmd_accept_err(net, req, NULL, err);
	return err;
}

int handshake_nl_done_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = sock_net(skb->sk);
	struct handshake_req *req;
	struct socket *sock;
	int fd, status, err;

	if (GENL_REQ_ATTR_CHECK(info, HANDSHAKE_A_DONE_SOCKFD))
		return -EINVAL;
	fd = nla_get_s32(info->attrs[HANDSHAKE_A_DONE_SOCKFD]);

	sock = sockfd_lookup(fd, &err);
	if (!sock)
		return err;

	req = handshake_req_hash_lookup(sock->sk);
	if (!req) {
		err = -EBUSY;
		trace_handshake_cmd_done_err(net, req, sock->sk, err);
		sockfd_put(sock);
		return err;
	}

	trace_handshake_cmd_done(net, req, sock->sk, fd);

	status = -EIO;
	if (info->attrs[HANDSHAKE_A_DONE_STATUS])
		status = nla_get_u32(info->attrs[HANDSHAKE_A_DONE_STATUS]);

	handshake_complete(req, status, info);
	sockfd_put(sock);
	return 0;
}

static unsigned int handshake_net_id;

static int __net_init handshake_net_init(struct net *net)
{
	struct handshake_net *hn = net_generic(net, handshake_net_id);
	unsigned long tmp;
	struct sysinfo si;

	/*
	 * Arbitrary limit to prevent handshakes that do not make
	 * progress from clogging up the system. The cap scales up
	 * with the amount of physical memory on the system.
	 */
	si_meminfo(&si);
	tmp = si.totalram / (25 * si.mem_unit);
	hn->hn_pending_max = clamp(tmp, 3UL, 50UL);

	spin_lock_init(&hn->hn_lock);
	hn->hn_pending = 0;
	hn->hn_flags = 0;
	INIT_LIST_HEAD(&hn->hn_requests);
	return 0;
}

static void __net_exit handshake_net_exit(struct net *net)
{
	struct handshake_net *hn = net_generic(net, handshake_net_id);
	struct handshake_req *req;
	LIST_HEAD(requests);

	/*
	 * Drain the net's pending list. Requests that have been
	 * accepted and are in progress will be destroyed when
	 * the socket is closed.
	 */
	spin_lock(&hn->hn_lock);
	set_bit(HANDSHAKE_F_NET_DRAINING, &hn->hn_flags);
	list_splice_init(&requests, &hn->hn_requests);
	spin_unlock(&hn->hn_lock);

	while (!list_empty(&requests)) {
		req = list_first_entry(&requests, struct handshake_req, hr_list);
		list_del(&req->hr_list);

		/*
		 * Requests on this list have not yet been
		 * accepted, so they do not have an fd to put.
		 */

		handshake_complete(req, -ETIMEDOUT, NULL);
	}
}

static struct pernet_operations handshake_genl_net_ops = {
	.init		= handshake_net_init,
	.exit		= handshake_net_exit,
	.id		= &handshake_net_id,
	.size		= sizeof(struct handshake_net),
};

/**
 * handshake_pernet - Get the handshake private per-net structure
 * @net: network namespace
 *
 * Returns a pointer to the net's private per-net structure for the
 * handshake module, or NULL if handshake_init() failed.
 */
struct handshake_net *handshake_pernet(struct net *net)
{
	return handshake_net_id ?
		net_generic(net, handshake_net_id) : NULL;
}
EXPORT_SYMBOL_IF_KUNIT(handshake_pernet);

static int __init handshake_init(void)
{
	int ret;

	ret = handshake_req_hash_init();
	if (ret) {
		pr_warn("handshake: hash initialization failed (%d)\n", ret);
		return ret;
	}

	ret = genl_register_family(&handshake_nl_family);
	if (ret) {
		pr_warn("handshake: netlink registration failed (%d)\n", ret);
		handshake_req_hash_destroy();
		return ret;
	}

	/*
	 * ORDER: register_pernet_subsys must be done last.
	 *
	 *	If initialization does not make it past pernet_subsys
	 *	registration, then handshake_net_id will remain 0. That
	 *	shunts the handshake consumer API to return ENOTSUPP
	 *	to prevent it from dereferencing something that hasn't
	 *	been allocated.
	 */
	ret = register_pernet_subsys(&handshake_genl_net_ops);
	if (ret) {
		pr_warn("handshake: pernet registration failed (%d)\n", ret);
		genl_unregister_family(&handshake_nl_family);
		handshake_req_hash_destroy();
	}

	return ret;
}

static void __exit handshake_exit(void)
{
	unregister_pernet_subsys(&handshake_genl_net_ops);
	handshake_net_id = 0;

	handshake_req_hash_destroy();
	genl_unregister_family(&handshake_nl_family);
}

module_init(handshake_init);
module_exit(handshake_exit);
