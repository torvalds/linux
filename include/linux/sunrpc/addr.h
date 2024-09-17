/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/include/linux/sunrpc/addr.h
 *
 * Various routines for copying and comparing sockaddrs and for
 * converting them to and from presentation format.
 */
#ifndef _LINUX_SUNRPC_ADDR_H
#define _LINUX_SUNRPC_ADDR_H

#include <linux/socket.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <net/ipv6.h>

size_t		rpc_ntop(const struct sockaddr *, char *, const size_t);
size_t		rpc_pton(struct net *, const char *, const size_t,
			 struct sockaddr *, const size_t);
char *		rpc_sockaddr2uaddr(const struct sockaddr *, gfp_t);
size_t		rpc_uaddr2sockaddr(struct net *, const char *, const size_t,
				   struct sockaddr *, const size_t);

static inline unsigned short rpc_get_port(const struct sockaddr *sap)
{
	switch (sap->sa_family) {
	case AF_INET:
		return ntohs(((struct sockaddr_in *)sap)->sin_port);
	case AF_INET6:
		return ntohs(((struct sockaddr_in6 *)sap)->sin6_port);
	}
	return 0;
}

static inline void rpc_set_port(struct sockaddr *sap,
				const unsigned short port)
{
	switch (sap->sa_family) {
	case AF_INET:
		((struct sockaddr_in *)sap)->sin_port = htons(port);
		break;
	case AF_INET6:
		((struct sockaddr_in6 *)sap)->sin6_port = htons(port);
		break;
	}
}

#define IPV6_SCOPE_DELIMITER		'%'
#define IPV6_SCOPE_ID_LEN		sizeof("%nnnnnnnnnn")

static inline bool rpc_cmp_addr4(const struct sockaddr *sap1,
				 const struct sockaddr *sap2)
{
	const struct sockaddr_in *sin1 = (const struct sockaddr_in *)sap1;
	const struct sockaddr_in *sin2 = (const struct sockaddr_in *)sap2;

	return sin1->sin_addr.s_addr == sin2->sin_addr.s_addr;
}

static inline bool __rpc_copy_addr4(struct sockaddr *dst,
				    const struct sockaddr *src)
{
	const struct sockaddr_in *ssin = (struct sockaddr_in *) src;
	struct sockaddr_in *dsin = (struct sockaddr_in *) dst;

	dsin->sin_family = ssin->sin_family;
	dsin->sin_addr.s_addr = ssin->sin_addr.s_addr;
	return true;
}

#if IS_ENABLED(CONFIG_IPV6)
static inline bool rpc_cmp_addr6(const struct sockaddr *sap1,
				 const struct sockaddr *sap2)
{
	const struct sockaddr_in6 *sin1 = (const struct sockaddr_in6 *)sap1;
	const struct sockaddr_in6 *sin2 = (const struct sockaddr_in6 *)sap2;

	if (!ipv6_addr_equal(&sin1->sin6_addr, &sin2->sin6_addr))
		return false;
	else if (ipv6_addr_type(&sin1->sin6_addr) & IPV6_ADDR_LINKLOCAL)
		return sin1->sin6_scope_id == sin2->sin6_scope_id;

	return true;
}

static inline bool __rpc_copy_addr6(struct sockaddr *dst,
				    const struct sockaddr *src)
{
	const struct sockaddr_in6 *ssin6 = (const struct sockaddr_in6 *) src;
	struct sockaddr_in6 *dsin6 = (struct sockaddr_in6 *) dst;

	dsin6->sin6_family = ssin6->sin6_family;
	dsin6->sin6_addr = ssin6->sin6_addr;
	dsin6->sin6_scope_id = ssin6->sin6_scope_id;
	return true;
}
#else	/* !(IS_ENABLED(CONFIG_IPV6) */
static inline bool rpc_cmp_addr6(const struct sockaddr *sap1,
				   const struct sockaddr *sap2)
{
	return false;
}

static inline bool __rpc_copy_addr6(struct sockaddr *dst,
				    const struct sockaddr *src)
{
	return false;
}
#endif	/* !(IS_ENABLED(CONFIG_IPV6) */

/**
 * rpc_cmp_addr - compare the address portion of two sockaddrs.
 * @sap1: first sockaddr
 * @sap2: second sockaddr
 *
 * Just compares the family and address portion. Ignores port, but
 * compares the scope if it's a link-local address.
 *
 * Returns true if the addrs are equal, false if they aren't.
 */
static inline bool rpc_cmp_addr(const struct sockaddr *sap1,
				const struct sockaddr *sap2)
{
	if (sap1->sa_family == sap2->sa_family) {
		switch (sap1->sa_family) {
		case AF_INET:
			return rpc_cmp_addr4(sap1, sap2);
		case AF_INET6:
			return rpc_cmp_addr6(sap1, sap2);
		}
	}
	return false;
}

/**
 * rpc_cmp_addr_port - compare the address and port number of two sockaddrs.
 * @sap1: first sockaddr
 * @sap2: second sockaddr
 */
static inline bool rpc_cmp_addr_port(const struct sockaddr *sap1,
				     const struct sockaddr *sap2)
{
	if (!rpc_cmp_addr(sap1, sap2))
		return false;
	return rpc_get_port(sap1) == rpc_get_port(sap2);
}

/**
 * rpc_copy_addr - copy the address portion of one sockaddr to another
 * @dst: destination sockaddr
 * @src: source sockaddr
 *
 * Just copies the address portion and family. Ignores port, scope, etc.
 * Caller is responsible for making certain that dst is large enough to hold
 * the address in src. Returns true if address family is supported. Returns
 * false otherwise.
 */
static inline bool rpc_copy_addr(struct sockaddr *dst,
				 const struct sockaddr *src)
{
	switch (src->sa_family) {
	case AF_INET:
		return __rpc_copy_addr4(dst, src);
	case AF_INET6:
		return __rpc_copy_addr6(dst, src);
	}
	return false;
}

/**
 * rpc_get_scope_id - return scopeid for a given sockaddr
 * @sa: sockaddr to get scopeid from
 *
 * Returns the value of the sin6_scope_id for AF_INET6 addrs, or 0 if
 * not an AF_INET6 address.
 */
static inline u32 rpc_get_scope_id(const struct sockaddr *sa)
{
	if (sa->sa_family != AF_INET6)
		return 0;

	return ((struct sockaddr_in6 *) sa)->sin6_scope_id;
}

#endif /* _LINUX_SUNRPC_ADDR_H */
