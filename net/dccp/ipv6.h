#ifndef _DCCP_IPV6_H
#define _DCCP_IPV6_H
/*
 *  net/dccp/ipv6.h
 *
 *  An implementation of the DCCP protocol
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@ghostprotocols.net>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/dccp.h>
#include <linux/ipv6.h>

struct dccp6_sock {
	struct dccp_sock  dccp;
	/*
	 * ipv6_pinfo has to be the last member of dccp6_sock,
	 * see inet6_sk_generic.
	 */
	struct ipv6_pinfo inet6;
};

struct dccp6_request_sock {
	struct dccp_request_sock  dccp;
	struct inet6_request_sock inet6;
};

struct dccp6_timewait_sock {
	struct inet_timewait_sock   inet;
	struct inet6_timewait_sock  tw6;
};

#endif /* _DCCP_IPV6_H */
