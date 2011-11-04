/*
 * Copyright 2009, Oracle.  All rights reserved.
 *
 * Convert socket addresses to presentation addresses and universal
 * addresses, and vice versa.
 *
 * Universal addresses are introduced by RFC 1833 and further refined by
 * recent RFCs describing NFSv4.  The universal address format is part
 * of the external (network) interface provided by rpcbind version 3
 * and 4, and by NFSv4.  Such an address is a string containing a
 * presentation format IP address followed by a port number in
 * "hibyte.lobyte" format.
 *
 * IPv6 addresses can also include a scope ID, typically denoted by
 * a '%' followed by a device name or a non-negative integer.  Refer to
 * RFC 4291, Section 2.2 for details on IPv6 presentation formats.
 */

#include <net/ipv6.h>
#include <linux/sunrpc/clnt.h>
#include <linux/slab.h>

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)

static size_t rpc_ntop6_noscopeid(const struct sockaddr *sap,
				  char *buf, const int buflen)
{
	const struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sap;
	const struct in6_addr *addr = &sin6->sin6_addr;

	/*
	 * RFC 4291, Section 2.2.2
	 *
	 * Shorthanded ANY address
	 */
	if (ipv6_addr_any(addr))
		return snprintf(buf, buflen, "::");

	/*
	 * RFC 4291, Section 2.2.2
	 *
	 * Shorthanded loopback address
	 */
	if (ipv6_addr_loopback(addr))
		return snprintf(buf, buflen, "::1");

	/*
	 * RFC 4291, Section 2.2.3
	 *
	 * Special presentation address format for mapped v4
	 * addresses.
	 */
	if (ipv6_addr_v4mapped(addr))
		return snprintf(buf, buflen, "::ffff:%pI4",
					&addr->s6_addr32[3]);

	/*
	 * RFC 4291, Section 2.2.1
	 */
	return snprintf(buf, buflen, "%pI6c", addr);
}

static size_t rpc_ntop6(const struct sockaddr *sap,
			char *buf, const size_t buflen)
{
	const struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sap;
	char scopebuf[IPV6_SCOPE_ID_LEN];
	size_t len;
	int rc;

	len = rpc_ntop6_noscopeid(sap, buf, buflen);
	if (unlikely(len == 0))
		return len;

	if (!(ipv6_addr_type(&sin6->sin6_addr) & IPV6_ADDR_LINKLOCAL))
		return len;
	if (sin6->sin6_scope_id == 0)
		return len;

	rc = snprintf(scopebuf, sizeof(scopebuf), "%c%u",
			IPV6_SCOPE_DELIMITER, sin6->sin6_scope_id);
	if (unlikely((size_t)rc > sizeof(scopebuf)))
		return 0;

	len += rc;
	if (unlikely(len > buflen))
		return 0;

	strcat(buf, scopebuf);
	return len;
}

#else	/* !(defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)) */

static size_t rpc_ntop6_noscopeid(const struct sockaddr *sap,
				  char *buf, const int buflen)
{
	return 0;
}

static size_t rpc_ntop6(const struct sockaddr *sap,
			char *buf, const size_t buflen)
{
	return 0;
}

#endif	/* !(defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)) */

static int rpc_ntop4(const struct sockaddr *sap,
		     char *buf, const size_t buflen)
{
	const struct sockaddr_in *sin = (struct sockaddr_in *)sap;

	return snprintf(buf, buflen, "%pI4", &sin->sin_addr);
}

/**
 * rpc_ntop - construct a presentation address in @buf
 * @sap: socket address
 * @buf: construction area
 * @buflen: size of @buf, in bytes
 *
 * Plants a %NUL-terminated string in @buf and returns the length
 * of the string, excluding the %NUL.  Otherwise zero is returned.
 */
size_t rpc_ntop(const struct sockaddr *sap, char *buf, const size_t buflen)
{
	switch (sap->sa_family) {
	case AF_INET:
		return rpc_ntop4(sap, buf, buflen);
	case AF_INET6:
		return rpc_ntop6(sap, buf, buflen);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rpc_ntop);

static size_t rpc_pton4(const char *buf, const size_t buflen,
			struct sockaddr *sap, const size_t salen)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)sap;
	u8 *addr = (u8 *)&sin->sin_addr.s_addr;

	if (buflen > INET_ADDRSTRLEN || salen < sizeof(struct sockaddr_in))
		return 0;

	memset(sap, 0, sizeof(struct sockaddr_in));

	if (in4_pton(buf, buflen, addr, '\0', NULL) == 0)
		return 0;

	sin->sin_family = AF_INET;
	return sizeof(struct sockaddr_in);
}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
static int rpc_parse_scope_id(const char *buf, const size_t buflen,
			      const char *delim, struct sockaddr_in6 *sin6)
{
	char *p;
	size_t len;

	if ((buf + buflen) == delim)
		return 1;

	if (*delim != IPV6_SCOPE_DELIMITER)
		return 0;

	if (!(ipv6_addr_type(&sin6->sin6_addr) & IPV6_ADDR_LINKLOCAL))
		return 0;

	len = (buf + buflen) - delim - 1;
	p = kstrndup(delim + 1, len, GFP_KERNEL);
	if (p) {
		unsigned long scope_id = 0;
		struct net_device *dev;

		dev = dev_get_by_name(&init_net, p);
		if (dev != NULL) {
			scope_id = dev->ifindex;
			dev_put(dev);
		} else {
			if (strict_strtoul(p, 10, &scope_id) == 0) {
				kfree(p);
				return 0;
			}
		}

		kfree(p);

		sin6->sin6_scope_id = scope_id;
		return 1;
	}

	return 0;
}

static size_t rpc_pton6(const char *buf, const size_t buflen,
			struct sockaddr *sap, const size_t salen)
{
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sap;
	u8 *addr = (u8 *)&sin6->sin6_addr.in6_u;
	const char *delim;

	if (buflen > (INET6_ADDRSTRLEN + IPV6_SCOPE_ID_LEN) ||
	    salen < sizeof(struct sockaddr_in6))
		return 0;

	memset(sap, 0, sizeof(struct sockaddr_in6));

	if (in6_pton(buf, buflen, addr, IPV6_SCOPE_DELIMITER, &delim) == 0)
		return 0;

	if (!rpc_parse_scope_id(buf, buflen, delim, sin6))
		return 0;

	sin6->sin6_family = AF_INET6;
	return sizeof(struct sockaddr_in6);
}
#else
static size_t rpc_pton6(const char *buf, const size_t buflen,
			struct sockaddr *sap, const size_t salen)
{
	return 0;
}
#endif

/**
 * rpc_pton - Construct a sockaddr in @sap
 * @buf: C string containing presentation format IP address
 * @buflen: length of presentation address in bytes
 * @sap: buffer into which to plant socket address
 * @salen: size of buffer in bytes
 *
 * Returns the size of the socket address if successful; otherwise
 * zero is returned.
 *
 * Plants a socket address in @sap and returns the size of the
 * socket address, if successful.  Returns zero if an error
 * occurred.
 */
size_t rpc_pton(const char *buf, const size_t buflen,
		struct sockaddr *sap, const size_t salen)
{
	unsigned int i;

	for (i = 0; i < buflen; i++)
		if (buf[i] == ':')
			return rpc_pton6(buf, buflen, sap, salen);
	return rpc_pton4(buf, buflen, sap, salen);
}
EXPORT_SYMBOL_GPL(rpc_pton);

/**
 * rpc_sockaddr2uaddr - Construct a universal address string from @sap.
 * @sap: socket address
 * @gfp_flags: allocation mode
 *
 * Returns a %NUL-terminated string in dynamically allocated memory;
 * otherwise NULL is returned if an error occurred.  Caller must
 * free the returned string.
 */
char *rpc_sockaddr2uaddr(const struct sockaddr *sap, gfp_t gfp_flags)
{
	char portbuf[RPCBIND_MAXUADDRPLEN];
	char addrbuf[RPCBIND_MAXUADDRLEN];
	unsigned short port;

	switch (sap->sa_family) {
	case AF_INET:
		if (rpc_ntop4(sap, addrbuf, sizeof(addrbuf)) == 0)
			return NULL;
		port = ntohs(((struct sockaddr_in *)sap)->sin_port);
		break;
	case AF_INET6:
		if (rpc_ntop6_noscopeid(sap, addrbuf, sizeof(addrbuf)) == 0)
			return NULL;
		port = ntohs(((struct sockaddr_in6 *)sap)->sin6_port);
		break;
	default:
		return NULL;
	}

	if (snprintf(portbuf, sizeof(portbuf),
		     ".%u.%u", port >> 8, port & 0xff) > (int)sizeof(portbuf))
		return NULL;

	if (strlcat(addrbuf, portbuf, sizeof(addrbuf)) > sizeof(addrbuf))
		return NULL;

	return kstrdup(addrbuf, gfp_flags);
}

/**
 * rpc_uaddr2sockaddr - convert a universal address to a socket address.
 * @uaddr: C string containing universal address to convert
 * @uaddr_len: length of universal address string
 * @sap: buffer into which to plant socket address
 * @salen: size of buffer
 *
 * @uaddr does not have to be '\0'-terminated, but strict_strtoul() and
 * rpc_pton() require proper string termination to be successful.
 *
 * Returns the size of the socket address if successful; otherwise
 * zero is returned.
 */
size_t rpc_uaddr2sockaddr(const char *uaddr, const size_t uaddr_len,
			  struct sockaddr *sap, const size_t salen)
{
	char *c, buf[RPCBIND_MAXUADDRLEN + sizeof('\0')];
	unsigned long portlo, porthi;
	unsigned short port;

	if (uaddr_len > RPCBIND_MAXUADDRLEN)
		return 0;

	memcpy(buf, uaddr, uaddr_len);

	buf[uaddr_len] = '\0';
	c = strrchr(buf, '.');
	if (unlikely(c == NULL))
		return 0;
	if (unlikely(strict_strtoul(c + 1, 10, &portlo) != 0))
		return 0;
	if (unlikely(portlo > 255))
		return 0;

	*c = '\0';
	c = strrchr(buf, '.');
	if (unlikely(c == NULL))
		return 0;
	if (unlikely(strict_strtoul(c + 1, 10, &porthi) != 0))
		return 0;
	if (unlikely(porthi > 255))
		return 0;

	port = (unsigned short)((porthi << 8) | portlo);

	*c = '\0';
	if (rpc_pton(buf, strlen(buf), sap, salen) == 0)
		return 0;

	switch (sap->sa_family) {
	case AF_INET:
		((struct sockaddr_in *)sap)->sin_port = htons(port);
		return sizeof(struct sockaddr_in);
	case AF_INET6:
		((struct sockaddr_in6 *)sap)->sin6_port = htons(port);
		return sizeof(struct sockaddr_in6);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rpc_uaddr2sockaddr);
