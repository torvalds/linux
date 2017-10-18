/*
 * security/tomoyo/network.c
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 */

#include "common.h"
#include <linux/slab.h>

/* Structure for holding inet domain socket's address. */
struct tomoyo_inet_addr_info {
	__be16 port;           /* In network byte order. */
	const __be32 *address; /* In network byte order. */
	bool is_ipv6;
};

/* Structure for holding unix domain socket's address. */
struct tomoyo_unix_addr_info {
	u8 *addr; /* This may not be '\0' terminated string. */
	unsigned int addr_len;
};

/* Structure for holding socket address. */
struct tomoyo_addr_info {
	u8 protocol;
	u8 operation;
	struct tomoyo_inet_addr_info inet;
	struct tomoyo_unix_addr_info unix0;
};

/* String table for socket's protocols. */
const char * const tomoyo_proto_keyword[TOMOYO_SOCK_MAX] = {
	[SOCK_STREAM]    = "stream",
	[SOCK_DGRAM]     = "dgram",
	[SOCK_RAW]       = "raw",
	[SOCK_SEQPACKET] = "seqpacket",
	[0] = " ", /* Dummy for avoiding NULL pointer dereference. */
	[4] = " ", /* Dummy for avoiding NULL pointer dereference. */
};

/**
 * tomoyo_parse_ipaddr_union - Parse an IP address.
 *
 * @param: Pointer to "struct tomoyo_acl_param".
 * @ptr:   Pointer to "struct tomoyo_ipaddr_union".
 *
 * Returns true on success, false otherwise.
 */
bool tomoyo_parse_ipaddr_union(struct tomoyo_acl_param *param,
			       struct tomoyo_ipaddr_union *ptr)
{
	u8 * const min = ptr->ip[0].in6_u.u6_addr8;
	u8 * const max = ptr->ip[1].in6_u.u6_addr8;
	char *address = tomoyo_read_token(param);
	const char *end;

	if (!strchr(address, ':') &&
	    in4_pton(address, -1, min, '-', &end) > 0) {
		ptr->is_ipv6 = false;
		if (!*end)
			ptr->ip[1].s6_addr32[0] = ptr->ip[0].s6_addr32[0];
		else if (*end++ != '-' ||
			 in4_pton(end, -1, max, '\0', &end) <= 0 || *end)
			return false;
		return true;
	}
	if (in6_pton(address, -1, min, '-', &end) > 0) {
		ptr->is_ipv6 = true;
		if (!*end)
			memmove(max, min, sizeof(u16) * 8);
		else if (*end++ != '-' ||
			 in6_pton(end, -1, max, '\0', &end) <= 0 || *end)
			return false;
		return true;
	}
	return false;
}

/**
 * tomoyo_print_ipv4 - Print an IPv4 address.
 *
 * @buffer:     Buffer to write to.
 * @buffer_len: Size of @buffer.
 * @min_ip:     Pointer to __be32.
 * @max_ip:     Pointer to __be32.
 *
 * Returns nothing.
 */
static void tomoyo_print_ipv4(char *buffer, const unsigned int buffer_len,
			      const __be32 *min_ip, const __be32 *max_ip)
{
	snprintf(buffer, buffer_len, "%pI4%c%pI4", min_ip,
		 *min_ip == *max_ip ? '\0' : '-', max_ip);
}

/**
 * tomoyo_print_ipv6 - Print an IPv6 address.
 *
 * @buffer:     Buffer to write to.
 * @buffer_len: Size of @buffer.
 * @min_ip:     Pointer to "struct in6_addr".
 * @max_ip:     Pointer to "struct in6_addr".
 *
 * Returns nothing.
 */
static void tomoyo_print_ipv6(char *buffer, const unsigned int buffer_len,
			      const struct in6_addr *min_ip,
			      const struct in6_addr *max_ip)
{
	snprintf(buffer, buffer_len, "%pI6c%c%pI6c", min_ip,
		 !memcmp(min_ip, max_ip, 16) ? '\0' : '-', max_ip);
}

/**
 * tomoyo_print_ip - Print an IP address.
 *
 * @buf:  Buffer to write to.
 * @size: Size of @buf.
 * @ptr:  Pointer to "struct ipaddr_union".
 *
 * Returns nothing.
 */
void tomoyo_print_ip(char *buf, const unsigned int size,
		     const struct tomoyo_ipaddr_union *ptr)
{
	if (ptr->is_ipv6)
		tomoyo_print_ipv6(buf, size, &ptr->ip[0], &ptr->ip[1]);
	else
		tomoyo_print_ipv4(buf, size, &ptr->ip[0].s6_addr32[0],
				  &ptr->ip[1].s6_addr32[0]);
}

/*
 * Mapping table from "enum tomoyo_network_acl_index" to
 * "enum tomoyo_mac_index" for inet domain socket.
 */
static const u8 tomoyo_inet2mac
[TOMOYO_SOCK_MAX][TOMOYO_MAX_NETWORK_OPERATION] = {
	[SOCK_STREAM] = {
		[TOMOYO_NETWORK_BIND]    = TOMOYO_MAC_NETWORK_INET_STREAM_BIND,
		[TOMOYO_NETWORK_LISTEN]  =
		TOMOYO_MAC_NETWORK_INET_STREAM_LISTEN,
		[TOMOYO_NETWORK_CONNECT] =
		TOMOYO_MAC_NETWORK_INET_STREAM_CONNECT,
	},
	[SOCK_DGRAM] = {
		[TOMOYO_NETWORK_BIND]    = TOMOYO_MAC_NETWORK_INET_DGRAM_BIND,
		[TOMOYO_NETWORK_SEND]    = TOMOYO_MAC_NETWORK_INET_DGRAM_SEND,
	},
	[SOCK_RAW]    = {
		[TOMOYO_NETWORK_BIND]    = TOMOYO_MAC_NETWORK_INET_RAW_BIND,
		[TOMOYO_NETWORK_SEND]    = TOMOYO_MAC_NETWORK_INET_RAW_SEND,
	},
};

/*
 * Mapping table from "enum tomoyo_network_acl_index" to
 * "enum tomoyo_mac_index" for unix domain socket.
 */
static const u8 tomoyo_unix2mac
[TOMOYO_SOCK_MAX][TOMOYO_MAX_NETWORK_OPERATION] = {
	[SOCK_STREAM] = {
		[TOMOYO_NETWORK_BIND]    = TOMOYO_MAC_NETWORK_UNIX_STREAM_BIND,
		[TOMOYO_NETWORK_LISTEN]  =
		TOMOYO_MAC_NETWORK_UNIX_STREAM_LISTEN,
		[TOMOYO_NETWORK_CONNECT] =
		TOMOYO_MAC_NETWORK_UNIX_STREAM_CONNECT,
	},
	[SOCK_DGRAM] = {
		[TOMOYO_NETWORK_BIND]    = TOMOYO_MAC_NETWORK_UNIX_DGRAM_BIND,
		[TOMOYO_NETWORK_SEND]    = TOMOYO_MAC_NETWORK_UNIX_DGRAM_SEND,
	},
	[SOCK_SEQPACKET] = {
		[TOMOYO_NETWORK_BIND]    =
		TOMOYO_MAC_NETWORK_UNIX_SEQPACKET_BIND,
		[TOMOYO_NETWORK_LISTEN]  =
		TOMOYO_MAC_NETWORK_UNIX_SEQPACKET_LISTEN,
		[TOMOYO_NETWORK_CONNECT] =
		TOMOYO_MAC_NETWORK_UNIX_SEQPACKET_CONNECT,
	},
};

/**
 * tomoyo_same_inet_acl - Check for duplicated "struct tomoyo_inet_acl" entry.
 *
 * @a: Pointer to "struct tomoyo_acl_info".
 * @b: Pointer to "struct tomoyo_acl_info".
 *
 * Returns true if @a == @b except permission bits, false otherwise.
 */
static bool tomoyo_same_inet_acl(const struct tomoyo_acl_info *a,
				 const struct tomoyo_acl_info *b)
{
	const struct tomoyo_inet_acl *p1 = container_of(a, typeof(*p1), head);
	const struct tomoyo_inet_acl *p2 = container_of(b, typeof(*p2), head);

	return p1->protocol == p2->protocol &&
		tomoyo_same_ipaddr_union(&p1->address, &p2->address) &&
		tomoyo_same_number_union(&p1->port, &p2->port);
}

/**
 * tomoyo_same_unix_acl - Check for duplicated "struct tomoyo_unix_acl" entry.
 *
 * @a: Pointer to "struct tomoyo_acl_info".
 * @b: Pointer to "struct tomoyo_acl_info".
 *
 * Returns true if @a == @b except permission bits, false otherwise.
 */
static bool tomoyo_same_unix_acl(const struct tomoyo_acl_info *a,
				 const struct tomoyo_acl_info *b)
{
	const struct tomoyo_unix_acl *p1 = container_of(a, typeof(*p1), head);
	const struct tomoyo_unix_acl *p2 = container_of(b, typeof(*p2), head);

	return p1->protocol == p2->protocol &&
		tomoyo_same_name_union(&p1->name, &p2->name);
}

/**
 * tomoyo_merge_inet_acl - Merge duplicated "struct tomoyo_inet_acl" entry.
 *
 * @a:         Pointer to "struct tomoyo_acl_info".
 * @b:         Pointer to "struct tomoyo_acl_info".
 * @is_delete: True for @a &= ~@b, false for @a |= @b.
 *
 * Returns true if @a is empty, false otherwise.
 */
static bool tomoyo_merge_inet_acl(struct tomoyo_acl_info *a,
				  struct tomoyo_acl_info *b,
				  const bool is_delete)
{
	u8 * const a_perm =
		&container_of(a, struct tomoyo_inet_acl, head)->perm;
	u8 perm = *a_perm;
	const u8 b_perm = container_of(b, struct tomoyo_inet_acl, head)->perm;

	if (is_delete)
		perm &= ~b_perm;
	else
		perm |= b_perm;
	*a_perm = perm;
	return !perm;
}

/**
 * tomoyo_merge_unix_acl - Merge duplicated "struct tomoyo_unix_acl" entry.
 *
 * @a:         Pointer to "struct tomoyo_acl_info".
 * @b:         Pointer to "struct tomoyo_acl_info".
 * @is_delete: True for @a &= ~@b, false for @a |= @b.
 *
 * Returns true if @a is empty, false otherwise.
 */
static bool tomoyo_merge_unix_acl(struct tomoyo_acl_info *a,
				  struct tomoyo_acl_info *b,
				  const bool is_delete)
{
	u8 * const a_perm =
		&container_of(a, struct tomoyo_unix_acl, head)->perm;
	u8 perm = *a_perm;
	const u8 b_perm = container_of(b, struct tomoyo_unix_acl, head)->perm;

	if (is_delete)
		perm &= ~b_perm;
	else
		perm |= b_perm;
	*a_perm = perm;
	return !perm;
}

/**
 * tomoyo_write_inet_network - Write "struct tomoyo_inet_acl" list.
 *
 * @param: Pointer to "struct tomoyo_acl_param".
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
int tomoyo_write_inet_network(struct tomoyo_acl_param *param)
{
	struct tomoyo_inet_acl e = { .head.type = TOMOYO_TYPE_INET_ACL };
	int error = -EINVAL;
	u8 type;
	const char *protocol = tomoyo_read_token(param);
	const char *operation = tomoyo_read_token(param);

	for (e.protocol = 0; e.protocol < TOMOYO_SOCK_MAX; e.protocol++)
		if (!strcmp(protocol, tomoyo_proto_keyword[e.protocol]))
			break;
	for (type = 0; type < TOMOYO_MAX_NETWORK_OPERATION; type++)
		if (tomoyo_permstr(operation, tomoyo_socket_keyword[type]))
			e.perm |= 1 << type;
	if (e.protocol == TOMOYO_SOCK_MAX || !e.perm)
		return -EINVAL;
	if (param->data[0] == '@') {
		param->data++;
		e.address.group =
			tomoyo_get_group(param, TOMOYO_ADDRESS_GROUP);
		if (!e.address.group)
			return -ENOMEM;
	} else {
		if (!tomoyo_parse_ipaddr_union(param, &e.address))
			goto out;
	}
	if (!tomoyo_parse_number_union(param, &e.port) ||
	    e.port.values[1] > 65535)
		goto out;
	error = tomoyo_update_domain(&e.head, sizeof(e), param,
				     tomoyo_same_inet_acl,
				     tomoyo_merge_inet_acl);
out:
	tomoyo_put_group(e.address.group);
	tomoyo_put_number_union(&e.port);
	return error;
}

/**
 * tomoyo_write_unix_network - Write "struct tomoyo_unix_acl" list.
 *
 * @param: Pointer to "struct tomoyo_acl_param".
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_write_unix_network(struct tomoyo_acl_param *param)
{
	struct tomoyo_unix_acl e = { .head.type = TOMOYO_TYPE_UNIX_ACL };
	int error;
	u8 type;
	const char *protocol = tomoyo_read_token(param);
	const char *operation = tomoyo_read_token(param);

	for (e.protocol = 0; e.protocol < TOMOYO_SOCK_MAX; e.protocol++)
		if (!strcmp(protocol, tomoyo_proto_keyword[e.protocol]))
			break;
	for (type = 0; type < TOMOYO_MAX_NETWORK_OPERATION; type++)
		if (tomoyo_permstr(operation, tomoyo_socket_keyword[type]))
			e.perm |= 1 << type;
	if (e.protocol == TOMOYO_SOCK_MAX || !e.perm)
		return -EINVAL;
	if (!tomoyo_parse_name_union(param, &e.name))
		return -EINVAL;
	error = tomoyo_update_domain(&e.head, sizeof(e), param,
				     tomoyo_same_unix_acl,
				     tomoyo_merge_unix_acl);
	tomoyo_put_name_union(&e.name);
	return error;
}

/**
 * tomoyo_audit_net_log - Audit network log.
 *
 * @r:         Pointer to "struct tomoyo_request_info".
 * @family:    Name of socket family ("inet" or "unix").
 * @protocol:  Name of protocol in @family.
 * @operation: Name of socket operation.
 * @address:   Name of address.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_audit_net_log(struct tomoyo_request_info *r,
				const char *family, const u8 protocol,
				const u8 operation, const char *address)
{
	return tomoyo_supervisor(r, "network %s %s %s %s\n", family,
				 tomoyo_proto_keyword[protocol],
				 tomoyo_socket_keyword[operation], address);
}

/**
 * tomoyo_audit_inet_log - Audit INET network log.
 *
 * @r: Pointer to "struct tomoyo_request_info".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_audit_inet_log(struct tomoyo_request_info *r)
{
	char buf[128];
	int len;
	const __be32 *address = r->param.inet_network.address;

	if (r->param.inet_network.is_ipv6)
		tomoyo_print_ipv6(buf, sizeof(buf), (const struct in6_addr *)
				  address, (const struct in6_addr *) address);
	else
		tomoyo_print_ipv4(buf, sizeof(buf), address, address);
	len = strlen(buf);
	snprintf(buf + len, sizeof(buf) - len, " %u",
		 r->param.inet_network.port);
	return tomoyo_audit_net_log(r, "inet", r->param.inet_network.protocol,
				    r->param.inet_network.operation, buf);
}

/**
 * tomoyo_audit_unix_log - Audit UNIX network log.
 *
 * @r: Pointer to "struct tomoyo_request_info".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_audit_unix_log(struct tomoyo_request_info *r)
{
	return tomoyo_audit_net_log(r, "unix", r->param.unix_network.protocol,
				    r->param.unix_network.operation,
				    r->param.unix_network.address->name);
}

/**
 * tomoyo_check_inet_acl - Check permission for inet domain socket operation.
 *
 * @r:   Pointer to "struct tomoyo_request_info".
 * @ptr: Pointer to "struct tomoyo_acl_info".
 *
 * Returns true if granted, false otherwise.
 */
static bool tomoyo_check_inet_acl(struct tomoyo_request_info *r,
				  const struct tomoyo_acl_info *ptr)
{
	const struct tomoyo_inet_acl *acl =
		container_of(ptr, typeof(*acl), head);
	const u8 size = r->param.inet_network.is_ipv6 ? 16 : 4;

	if (!(acl->perm & (1 << r->param.inet_network.operation)) ||
	    !tomoyo_compare_number_union(r->param.inet_network.port,
					 &acl->port))
		return false;
	if (acl->address.group)
		return tomoyo_address_matches_group
			(r->param.inet_network.is_ipv6,
			 r->param.inet_network.address, acl->address.group);
	return acl->address.is_ipv6 == r->param.inet_network.is_ipv6 &&
		memcmp(&acl->address.ip[0],
		       r->param.inet_network.address, size) <= 0 &&
		memcmp(r->param.inet_network.address,
		       &acl->address.ip[1], size) <= 0;
}

/**
 * tomoyo_check_unix_acl - Check permission for unix domain socket operation.
 *
 * @r:   Pointer to "struct tomoyo_request_info".
 * @ptr: Pointer to "struct tomoyo_acl_info".
 *
 * Returns true if granted, false otherwise.
 */
static bool tomoyo_check_unix_acl(struct tomoyo_request_info *r,
				  const struct tomoyo_acl_info *ptr)
{
	const struct tomoyo_unix_acl *acl =
		container_of(ptr, typeof(*acl), head);

	return (acl->perm & (1 << r->param.unix_network.operation)) &&
		tomoyo_compare_name_union(r->param.unix_network.address,
					  &acl->name);
}

/**
 * tomoyo_inet_entry - Check permission for INET network operation.
 *
 * @address: Pointer to "struct tomoyo_addr_info".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_inet_entry(const struct tomoyo_addr_info *address)
{
	const int idx = tomoyo_read_lock();
	struct tomoyo_request_info r;
	int error = 0;
	const u8 type = tomoyo_inet2mac[address->protocol][address->operation];

	if (type && tomoyo_init_request_info(&r, NULL, type)
	    != TOMOYO_CONFIG_DISABLED) {
		r.param_type = TOMOYO_TYPE_INET_ACL;
		r.param.inet_network.protocol = address->protocol;
		r.param.inet_network.operation = address->operation;
		r.param.inet_network.is_ipv6 = address->inet.is_ipv6;
		r.param.inet_network.address = address->inet.address;
		r.param.inet_network.port = ntohs(address->inet.port);
		do {
			tomoyo_check_acl(&r, tomoyo_check_inet_acl);
			error = tomoyo_audit_inet_log(&r);
		} while (error == TOMOYO_RETRY_REQUEST);
	}
	tomoyo_read_unlock(idx);
	return error;
}

/**
 * tomoyo_check_inet_address - Check permission for inet domain socket's operation.
 *
 * @addr:     Pointer to "struct sockaddr".
 * @addr_len: Size of @addr.
 * @port:     Port number.
 * @address:  Pointer to "struct tomoyo_addr_info".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_check_inet_address(const struct sockaddr *addr,
				     const unsigned int addr_len,
				     const u16 port,
				     struct tomoyo_addr_info *address)
{
	struct tomoyo_inet_addr_info *i = &address->inet;

	switch (addr->sa_family) {
	case AF_INET6:
		if (addr_len < SIN6_LEN_RFC2133)
			goto skip;
		i->is_ipv6 = true;
		i->address = (__be32 *)
			((struct sockaddr_in6 *) addr)->sin6_addr.s6_addr;
		i->port = ((struct sockaddr_in6 *) addr)->sin6_port;
		break;
	case AF_INET:
		if (addr_len < sizeof(struct sockaddr_in))
			goto skip;
		i->is_ipv6 = false;
		i->address = (__be32 *)
			&((struct sockaddr_in *) addr)->sin_addr;
		i->port = ((struct sockaddr_in *) addr)->sin_port;
		break;
	default:
		goto skip;
	}
	if (address->protocol == SOCK_RAW)
		i->port = htons(port);
	return tomoyo_inet_entry(address);
skip:
	return 0;
}

/**
 * tomoyo_unix_entry - Check permission for UNIX network operation.
 *
 * @address: Pointer to "struct tomoyo_addr_info".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_unix_entry(const struct tomoyo_addr_info *address)
{
	const int idx = tomoyo_read_lock();
	struct tomoyo_request_info r;
	int error = 0;
	const u8 type = tomoyo_unix2mac[address->protocol][address->operation];

	if (type && tomoyo_init_request_info(&r, NULL, type)
	    != TOMOYO_CONFIG_DISABLED) {
		char *buf = address->unix0.addr;
		int len = address->unix0.addr_len - sizeof(sa_family_t);

		if (len <= 0) {
			buf = "anonymous";
			len = 9;
		} else if (buf[0]) {
			len = strnlen(buf, len);
		}
		buf = tomoyo_encode2(buf, len);
		if (buf) {
			struct tomoyo_path_info addr;

			addr.name = buf;
			tomoyo_fill_path_info(&addr);
			r.param_type = TOMOYO_TYPE_UNIX_ACL;
			r.param.unix_network.protocol = address->protocol;
			r.param.unix_network.operation = address->operation;
			r.param.unix_network.address = &addr;
			do {
				tomoyo_check_acl(&r, tomoyo_check_unix_acl);
				error = tomoyo_audit_unix_log(&r);
			} while (error == TOMOYO_RETRY_REQUEST);
			kfree(buf);
		} else
			error = -ENOMEM;
	}
	tomoyo_read_unlock(idx);
	return error;
}

/**
 * tomoyo_check_unix_address - Check permission for unix domain socket's operation.
 *
 * @addr:     Pointer to "struct sockaddr".
 * @addr_len: Size of @addr.
 * @address:  Pointer to "struct tomoyo_addr_info".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_check_unix_address(struct sockaddr *addr,
				     const unsigned int addr_len,
				     struct tomoyo_addr_info *address)
{
	struct tomoyo_unix_addr_info *u = &address->unix0;

	if (addr->sa_family != AF_UNIX)
		return 0;
	u->addr = ((struct sockaddr_un *) addr)->sun_path;
	u->addr_len = addr_len;
	return tomoyo_unix_entry(address);
}

/**
 * tomoyo_kernel_service - Check whether I'm kernel service or not.
 *
 * Returns true if I'm kernel service, false otherwise.
 */
static bool tomoyo_kernel_service(void)
{
	/* Nothing to do if I am a kernel service. */
	return uaccess_kernel();
}

/**
 * tomoyo_sock_family - Get socket's family.
 *
 * @sk: Pointer to "struct sock".
 *
 * Returns one of PF_INET, PF_INET6, PF_UNIX or 0.
 */
static u8 tomoyo_sock_family(struct sock *sk)
{
	u8 family;

	if (tomoyo_kernel_service())
		return 0;
	family = sk->sk_family;
	switch (family) {
	case PF_INET:
	case PF_INET6:
	case PF_UNIX:
		return family;
	default:
		return 0;
	}
}

/**
 * tomoyo_socket_listen_permission - Check permission for listening a socket.
 *
 * @sock: Pointer to "struct socket".
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_socket_listen_permission(struct socket *sock)
{
	struct tomoyo_addr_info address;
	const u8 family = tomoyo_sock_family(sock->sk);
	const unsigned int type = sock->type;
	struct sockaddr_storage addr;
	int addr_len;

	if (!family || (type != SOCK_STREAM && type != SOCK_SEQPACKET))
		return 0;
	{
		const int error = sock->ops->getname(sock, (struct sockaddr *)
						     &addr, &addr_len, 0);

		if (error)
			return error;
	}
	address.protocol = type;
	address.operation = TOMOYO_NETWORK_LISTEN;
	if (family == PF_UNIX)
		return tomoyo_check_unix_address((struct sockaddr *) &addr,
						 addr_len, &address);
	return tomoyo_check_inet_address((struct sockaddr *) &addr, addr_len,
					 0, &address);
}

/**
 * tomoyo_socket_connect_permission - Check permission for setting the remote address of a socket.
 *
 * @sock:     Pointer to "struct socket".
 * @addr:     Pointer to "struct sockaddr".
 * @addr_len: Size of @addr.
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_socket_connect_permission(struct socket *sock,
				     struct sockaddr *addr, int addr_len)
{
	struct tomoyo_addr_info address;
	const u8 family = tomoyo_sock_family(sock->sk);
	const unsigned int type = sock->type;

	if (!family)
		return 0;
	address.protocol = type;
	switch (type) {
	case SOCK_DGRAM:
	case SOCK_RAW:
		address.operation = TOMOYO_NETWORK_SEND;
		break;
	case SOCK_STREAM:
	case SOCK_SEQPACKET:
		address.operation = TOMOYO_NETWORK_CONNECT;
		break;
	default:
		return 0;
	}
	if (family == PF_UNIX)
		return tomoyo_check_unix_address(addr, addr_len, &address);
	return tomoyo_check_inet_address(addr, addr_len, sock->sk->sk_protocol,
					 &address);
}

/**
 * tomoyo_socket_bind_permission - Check permission for setting the local address of a socket.
 *
 * @sock:     Pointer to "struct socket".
 * @addr:     Pointer to "struct sockaddr".
 * @addr_len: Size of @addr.
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_socket_bind_permission(struct socket *sock, struct sockaddr *addr,
				  int addr_len)
{
	struct tomoyo_addr_info address;
	const u8 family = tomoyo_sock_family(sock->sk);
	const unsigned int type = sock->type;

	if (!family)
		return 0;
	switch (type) {
	case SOCK_STREAM:
	case SOCK_DGRAM:
	case SOCK_RAW:
	case SOCK_SEQPACKET:
		address.protocol = type;
		address.operation = TOMOYO_NETWORK_BIND;
		break;
	default:
		return 0;
	}
	if (family == PF_UNIX)
		return tomoyo_check_unix_address(addr, addr_len, &address);
	return tomoyo_check_inet_address(addr, addr_len, sock->sk->sk_protocol,
					 &address);
}

/**
 * tomoyo_socket_sendmsg_permission - Check permission for sending a datagram.
 *
 * @sock: Pointer to "struct socket".
 * @msg:  Pointer to "struct msghdr".
 * @size: Unused.
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_socket_sendmsg_permission(struct socket *sock, struct msghdr *msg,
				     int size)
{
	struct tomoyo_addr_info address;
	const u8 family = tomoyo_sock_family(sock->sk);
	const unsigned int type = sock->type;

	if (!msg->msg_name || !family ||
	    (type != SOCK_DGRAM && type != SOCK_RAW))
		return 0;
	address.protocol = type;
	address.operation = TOMOYO_NETWORK_SEND;
	if (family == PF_UNIX)
		return tomoyo_check_unix_address((struct sockaddr *)
						 msg->msg_name,
						 msg->msg_namelen, &address);
	return tomoyo_check_inet_address((struct sockaddr *) msg->msg_name,
					 msg->msg_namelen,
					 sock->sk->sk_protocol, &address);
}
