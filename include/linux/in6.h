/*
 *	Types and definitions for AF_INET6 
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	Sources:
 *	IPv6 Program Interfaces for BSD Systems
 *      <draft-ietf-ipngwg-bsd-api-05.txt>
 *
 *	Advanced Sockets API for IPv6
 *	<draft-stevens-advanced-api-00.txt>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_IN6_H
#define _LINUX_IN6_H

#include <uapi/linux/in6.h>

extern const struct in6_addr in6addr_any;
#define IN6ADDR_ANY_INIT { { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 } } }
extern const struct in6_addr in6addr_loopback;
#define IN6ADDR_LOOPBACK_INIT { { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } } }
extern const struct in6_addr in6addr_linklocal_allnodes;
#define IN6ADDR_LINKLOCAL_ALLNODES_INIT	\
		{ { { 0xff,2,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } } }
extern const struct in6_addr in6addr_linklocal_allrouters;
#define IN6ADDR_LINKLOCAL_ALLROUTERS_INIT \
		{ { { 0xff,2,0,0,0,0,0,0,0,0,0,0,0,0,0,2 } } }
#endif
