// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock LSM - Network management and hooks
 *
 * Copyright © 2022-2023 Huawei Tech. Co., Ltd.
 * Copyright © 2022-2023 Microsoft Corporation
 */

#include <linux/in.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <net/ipv6.h>

#include "common.h"
#include "cred.h"
#include "limits.h"
#include "net.h"
#include "ruleset.h"

int landlock_append_net_rule(struct landlock_ruleset *const ruleset,
			     const u16 port, access_mask_t access_rights)
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
	err = landlock_insert_rule(ruleset, id, access_rights);
	mutex_unlock(&ruleset->lock);

	return err;
}

static access_mask_t
get_raw_handled_net_accesses(const struct landlock_ruleset *const domain)
{
	access_mask_t access_dom = 0;
	size_t layer_level;

	for (layer_level = 0; layer_level < domain->num_layers; layer_level++)
		access_dom |= landlock_get_net_access_mask(domain, layer_level);
	return access_dom;
}

static const struct landlock_ruleset *get_current_net_domain(void)
{
	const struct landlock_ruleset *const dom =
		landlock_get_current_domain();

	if (!dom || !get_raw_handled_net_accesses(dom))
		return NULL;

	return dom;
}

static int current_check_access_socket(struct socket *const sock,
				       struct sockaddr *const address,
				       const int addrlen,
				       const access_mask_t access_request)
{
	__be16 port;
	layer_mask_t layer_masks[LANDLOCK_NUM_ACCESS_NET] = {};
	const struct landlock_rule *rule;
	access_mask_t handled_access;
	struct landlock_id id = {
		.type = LANDLOCK_KEY_NET_PORT,
	};
	const struct landlock_ruleset *const dom = get_current_net_domain();

	if (!dom)
		return 0;
	if (WARN_ON_ONCE(dom->num_layers < 1))
		return -EACCES;

	/* Checks if it's a (potential) TCP socket. */
	if (sock->type != SOCK_STREAM)
		return 0;

	/* Checks for minimal header length to safely read sa_family. */
	if (addrlen < offsetofend(typeof(*address), sa_family))
		return -EINVAL;

	switch (address->sa_family) {
	case AF_UNSPEC:
	case AF_INET:
		if (addrlen < sizeof(struct sockaddr_in))
			return -EINVAL;
		port = ((struct sockaddr_in *)address)->sin_port;
		break;

#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		if (addrlen < SIN6_LEN_RFC2133)
			return -EINVAL;
		port = ((struct sockaddr_in6 *)address)->sin6_port;
		break;
#endif /* IS_ENABLED(CONFIG_IPV6) */

	default:
		return 0;
	}

	/* Specific AF_UNSPEC handling. */
	if (address->sa_family == AF_UNSPEC) {
		/*
		 * Connecting to an address with AF_UNSPEC dissolves the TCP
		 * association, which have the same effect as closing the
		 * connection while retaining the socket object (i.e., the file
		 * descriptor).  As for dropping privileges, closing
		 * connections is always allowed.
		 *
		 * For a TCP access control system, this request is legitimate.
		 * Let the network stack handle potential inconsistencies and
		 * return -EINVAL if needed.
		 */
		if (access_request == LANDLOCK_ACCESS_NET_CONNECT_TCP)
			return 0;

		/*
		 * For compatibility reason, accept AF_UNSPEC for bind
		 * accesses (mapped to AF_INET) only if the address is
		 * INADDR_ANY (cf. __inet_bind).  Checking the address is
		 * required to not wrongfully return -EACCES instead of
		 * -EAFNOSUPPORT.
		 *
		 * We could return 0 and let the network stack handle these
		 * checks, but it is safer to return a proper error and test
		 * consistency thanks to kselftest.
		 */
		if (access_request == LANDLOCK_ACCESS_NET_BIND_TCP) {
			/* addrlen has already been checked for AF_UNSPEC. */
			const struct sockaddr_in *const sockaddr =
				(struct sockaddr_in *)address;

			if (sock->sk->__sk_common.skc_family != AF_INET)
				return -EINVAL;

			if (sockaddr->sin_addr.s_addr != htonl(INADDR_ANY))
				return -EAFNOSUPPORT;
		}
	} else {
		/*
		 * Checks sa_family consistency to not wrongfully return
		 * -EACCES instead of -EINVAL.  Valid sa_family changes are
		 * only (from AF_INET or AF_INET6) to AF_UNSPEC.
		 *
		 * We could return 0 and let the network stack handle this
		 * check, but it is safer to return a proper error and test
		 * consistency thanks to kselftest.
		 */
		if (address->sa_family != sock->sk->__sk_common.skc_family)
			return -EINVAL;
	}

	id.key.data = (__force uintptr_t)port;
	BUILD_BUG_ON(sizeof(port) > sizeof(id.key.data));

	rule = landlock_find_rule(dom, id);
	handled_access = landlock_init_layer_masks(
		dom, access_request, &layer_masks, LANDLOCK_KEY_NET_PORT);
	if (landlock_unmask_layers(rule, handled_access, &layer_masks,
				   ARRAY_SIZE(layer_masks)))
		return 0;

	return -EACCES;
}

static int hook_socket_bind(struct socket *const sock,
			    struct sockaddr *const address, const int addrlen)
{
	return current_check_access_socket(sock, address, addrlen,
					   LANDLOCK_ACCESS_NET_BIND_TCP);
}

static int hook_socket_connect(struct socket *const sock,
			       struct sockaddr *const address,
			       const int addrlen)
{
	return current_check_access_socket(sock, address, addrlen,
					   LANDLOCK_ACCESS_NET_CONNECT_TCP);
}

static struct security_hook_list landlock_hooks[] __ro_after_init = {
	LSM_HOOK_INIT(socket_bind, hook_socket_bind),
	LSM_HOOK_INIT(socket_connect, hook_socket_connect),
};

__init void landlock_add_net_hooks(void)
{
	security_add_hooks(landlock_hooks, ARRAY_SIZE(landlock_hooks),
			   LANDLOCK_NAME);
}
