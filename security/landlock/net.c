// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock - Network management and hooks
 *
 * Copyright © 2022-2023 Huawei Tech. Co., Ltd.
 * Copyright © 2022-2025 Microsoft Corporation
 */

#include <linux/in.h>
#include <linux/lsm_audit.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <net/ipv6.h>

#include "audit.h"
#include "common.h"
#include "cred.h"
#include "limits.h"
#include "net.h"
#include "ruleset.h"

int landlock_append_net_rule(struct landlock_ruleset *const ruleset,
			     const u16 port, access_mask_t access_rights,
			     const u32 flags)
{
	int err;
	const struct landlock_id id = {
		.key.data = (__force uintptr_t)htons(port),
		.type = LANDLOCK_KEY_NET_PORT,
	};

	BUILD_BUG_ON(sizeof(port) > sizeof(id.key.data));

	/* Transforms relative access rights to absolute ones. */
	access_rights |= LANDLOCK_MASK_ACCESS_NET &
			 ~landlock_get_net_access_mask(ruleset, 0);

	mutex_lock(&ruleset->lock);
	err = landlock_insert_rule(ruleset, id, access_rights, flags);
	mutex_unlock(&ruleset->lock);

	return err;
}

static int current_check_access_socket(struct socket *const sock,
				       struct sockaddr *const address,
				       const int addrlen,
				       access_mask_t access_request,
				       bool connecting)
{
	unsigned short sock_family;
	__be16 port;
	struct layer_masks layer_masks = {};
	const struct landlock_rule *rule;
	struct landlock_id id = {
		.type = LANDLOCK_KEY_NET_PORT,
	};
	const struct access_masks masks = {
		.net = access_request,
	};
	const struct landlock_cred_security *const subject =
		landlock_get_applicable_subject(current_cred(), masks, NULL);
	struct lsm_network_audit audit_net = {};

	if (!subject)
		return 0;

	/* Checks for minimal header length to safely read sa_family. */
	if (addrlen < offsetofend(typeof(*address), sa_family))
		return -EINVAL;

	/*
	 * The socket is not locked, so sk_family can change concurrently due to
	 * e.g. setsockopt(IPV6_ADDRFORM).
	 */
	sock_family = READ_ONCE(sock->sk->sk_family);

	switch (address->sa_family) {
	case AF_UNSPEC:
		if (access_request == LANDLOCK_ACCESS_NET_CONNECT_TCP ||
		    (access_request == LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP &&
		     connecting)) {
			/*
			 * Connecting to an address with AF_UNSPEC dissolves the
			 * remote association while retaining the socket object
			 * (i.e., the file descriptor). For TCP, it has the same
			 * effect as closing the connection. For UDP, it removes
			 * any preset remote address. As for dropping
			 * privileges, these actions are always allowed.  Let
			 * the network stack handle potential inconsistencies
			 * and return -EINVAL if needed.
			 */
			return 0;
		} else if (access_request ==
			   LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP) {
			if (sock_family == AF_INET6) {
				/*
				 * We cannot allow sending UDP datagrams to an
				 * explicit AF_UNSPEC address on IPv6 sockets,
				 * even if AF_UNSPEC is treated as "no address"
				 * on such sockets (so it should always be
				 * allowed).  That's because the socket's family
				 * can change under our feet (if another thread
				 * calls setsockopt(IPV6_ADDRFORM)) to IPv4,
				 * which would then treat AF_UNSPEC as AF_INET.
				 */
				audit_net.family = AF_UNSPEC;
				audit_net.sk = sock->sk;
				landlock_init_layer_masks(
					subject->domain, access_request,
					&layer_masks, LANDLOCK_KEY_NET_PORT);
				landlock_log_denial(
					subject,
					&(struct landlock_request){
						.type = LANDLOCK_REQUEST_NET_ACCESS,
						.audit.type =
							LSM_AUDIT_DATA_NET,
						.audit.u.net = &audit_net,
						.access = access_request,
						.layer_masks = &layer_masks,
					});
				return -EACCES;
			}
		} else if (access_request == LANDLOCK_ACCESS_NET_BIND_TCP ||
			   access_request == LANDLOCK_ACCESS_NET_BIND_UDP) {
			/*
			 * Binding to an AF_UNSPEC address is treated
			 * differently by IPv4 and IPv6 sockets. The socket's
			 * family may change under our feet due to
			 * setsockopt(IPV6_ADDRFORM), but that's ok: we either
			 * reject entirely for IPv6 or require
			 * %LANDLOCK_ACCESS_NET_BIND_TCP or
			 * %LANDLOCK_ACCESS_NET_BIND_UDP for IPv4, so it cannot
			 * be used to bypass the policy.
			 *
			 * IPv4 sockets map AF_UNSPEC to AF_INET for
			 * retrocompatibility for bind accesses, only if the
			 * address is INADDR_ANY (cf. __inet_bind). IPv6
			 * sockets always reject it.
			 *
			 * Checking the address is required to not wrongfully
			 * return -EACCES instead of -EAFNOSUPPORT or -EINVAL.
			 * We could return 0 and let the network stack handle
			 * these checks, but it is safer to return a proper
			 * error and test consistency thanks to kselftest.
			 */
			if (sock_family == AF_INET) {
				const struct sockaddr_in *const sockaddr =
					(struct sockaddr_in *)address;

				if (addrlen < sizeof(struct sockaddr_in))
					return -EINVAL;

				if (sockaddr->sin_addr.s_addr !=
				    htonl(INADDR_ANY))
					return -EAFNOSUPPORT;
			} else {
				if (addrlen < SIN6_LEN_RFC2133)
					return -EINVAL;
				else
					return -EAFNOSUPPORT;
			}
		} else {
			WARN_ON_ONCE(1);
		}
		/*
		 * AF_UNSPEC is treated as AF_INET only in
		 * bind(AF_UNSPEC+INADDR_ANY) on IPv4 sockets and when sending
		 * to AF_UNSPEC addresses on IPv4 sockets.
		 */
		fallthrough;
	case AF_INET: {
		const struct sockaddr_in *addr4;

		if (addrlen < sizeof(struct sockaddr_in))
			return -EINVAL;

		addr4 = (struct sockaddr_in *)address;
		port = addr4->sin_port;

		if (access_request == LANDLOCK_ACCESS_NET_CONNECT_TCP ||
		    access_request == LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP) {
			audit_net.dport = port;
			audit_net.v4info.daddr = addr4->sin_addr.s_addr;
		} else if (access_request == LANDLOCK_ACCESS_NET_BIND_TCP ||
			   access_request == LANDLOCK_ACCESS_NET_BIND_UDP) {
			audit_net.sport = port;
			audit_net.v4info.saddr = addr4->sin_addr.s_addr;
		} else {
			WARN_ON_ONCE(1);
		}
		break;
	}

#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6: {
		const struct sockaddr_in6 *addr6;

		if (addrlen < SIN6_LEN_RFC2133)
			return -EINVAL;

		addr6 = (struct sockaddr_in6 *)address;
		port = addr6->sin6_port;

		if (access_request == LANDLOCK_ACCESS_NET_CONNECT_TCP ||
		    access_request == LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP) {
			audit_net.dport = port;
			audit_net.v6info.daddr = addr6->sin6_addr;
		} else if (access_request == LANDLOCK_ACCESS_NET_BIND_TCP ||
			   access_request == LANDLOCK_ACCESS_NET_BIND_UDP) {
			audit_net.sport = port;
			audit_net.v6info.saddr = addr6->sin6_addr;
		} else {
			WARN_ON_ONCE(1);
		}
		break;
	}
#endif /* IS_ENABLED(CONFIG_IPV6) */

	default:
		return 0;
	}

	/*
	 * Checks sa_family consistency to not wrongfully return
	 * -EACCES instead of -EINVAL.  Valid sa_family changes are
	 * only (from AF_INET or AF_INET6) to AF_UNSPEC.
	 *
	 * We could return 0 and let the network stack handle this
	 * check, but it is safer to return a proper error and test
	 * consistency thanks to kselftest.
	 */
	if (address->sa_family != sock_family &&
	    address->sa_family != AF_UNSPEC)
		return -EINVAL;

	id.key.data = (__force uintptr_t)port;
	BUILD_BUG_ON(sizeof(port) > sizeof(id.key.data));

	rule = landlock_find_rule(subject->domain, id);
	access_request = landlock_init_layer_masks(subject->domain,
						   access_request, &layer_masks,
						   LANDLOCK_KEY_NET_PORT);
	if (!access_request)
		return 0;

	if (landlock_unmask_layers(rule, &layer_masks))
		return 0;

	audit_net.family = address->sa_family;
	audit_net.sk = sock->sk;
	landlock_log_denial(subject,
			    &(struct landlock_request){
				    .type = LANDLOCK_REQUEST_NET_ACCESS,
				    .audit.type = LSM_AUDIT_DATA_NET,
				    .audit.u.net = &audit_net,
				    .access = access_request,
				    .layer_masks = &layer_masks,
			    });
	return -EACCES;
}

static int current_check_autobind_udp_socket(struct socket *const sock)
{
	const struct access_masks bind_udp = {
		.net = LANDLOCK_ACCESS_NET_BIND_UDP,
	};
	struct sockaddr_storage port0 = {};
	unsigned short num;
	bool slow;

	/* Quick return for non-Landlocked tasks. */
	if (!landlock_get_applicable_subject(current_cred(), bind_udp, NULL))
		return 0;

	/*
	 * On UDP sockets, if a local port has not already been bound, calling
	 * connect() or sending a first datagram has the side effect of
	 * autobinding an ephemeral port: we also have to check that the process
	 * would have had the right to bind(0) explicitly.  Hold the socket lock
	 * around the inet_num read to exclude udp_lib_get_port()'s transient
	 * inet_num = snum write that is reverted to 0 on a failing reuseport
	 * bind.
	 */
	slow = lock_sock_fast(sock->sk);
	num = inet_sk(sock->sk)->inet_num;
	unlock_sock_fast(sock->sk, slow);
	if (num != 0)
		return 0;

	/*
	 * Construct a struct sockaddr* with port 0 to pretend the process tried
	 * to bind() on that address.
	 */
	port0.ss_family = READ_ONCE(sock->sk->sk_family);

	return current_check_access_socket(sock, (struct sockaddr *)&port0,
					   sizeof(port0), bind_udp.net, false);
}

static int hook_socket_bind(struct socket *const sock,
			    struct sockaddr *const address, const int addrlen)
{
	access_mask_t access_request;

	if (sk_is_tcp(sock->sk))
		access_request = LANDLOCK_ACCESS_NET_BIND_TCP;
	else if (sk_is_udp(sock->sk))
		access_request = LANDLOCK_ACCESS_NET_BIND_UDP;
	else
		return 0;

	return current_check_access_socket(sock, address, addrlen,
					   access_request, false);
}

static int hook_socket_connect(struct socket *const sock,
			       struct sockaddr *const address,
			       const int addrlen)
{
	access_mask_t access_request;
	int ret = 0;

	if (sk_is_tcp(sock->sk))
		access_request = LANDLOCK_ACCESS_NET_CONNECT_TCP;
	else if (sk_is_udp(sock->sk))
		access_request = LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP;
	else
		return 0;

	ret = current_check_access_socket(sock, address, addrlen,
					  access_request, true);

	/*
	 * connect()ing to an AF_UNSPEC address does not trigger an autobind and
	 * should never be restricted.
	 */
	if (ret == 0 && sk_is_udp(sock->sk) &&
	    addrlen >= offsetofend(typeof(*address), sa_family) &&
	    address->sa_family != AF_UNSPEC)
		ret = current_check_autobind_udp_socket(sock);

	return ret;
}

static int hook_socket_sendmsg(struct socket *const sock,
			       struct msghdr *const msg, const int size)
{
	struct sockaddr *const address = msg->msg_name;
	const int addrlen = msg->msg_namelen;
	access_mask_t access_request;
	int ret = 0;

	if (sk_is_udp(sock->sk))
		access_request = LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP;
	else
		return 0;

	if (address != NULL)
		ret = current_check_access_socket(sock, address, addrlen,
						  access_request, false);

	if (ret == 0)
		ret = current_check_autobind_udp_socket(sock);

	return ret;
}

static struct security_hook_list landlock_hooks[] __ro_after_init = {
	LSM_HOOK_INIT(socket_bind, hook_socket_bind),
	LSM_HOOK_INIT(socket_connect, hook_socket_connect),
	LSM_HOOK_INIT(socket_sendmsg, hook_socket_sendmsg),
};

__init void landlock_add_net_hooks(void)
{
	security_add_hooks(landlock_hooks, ARRAY_SIZE(landlock_hooks),
			   &landlock_lsmid);
}
