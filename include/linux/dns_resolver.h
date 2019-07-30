/*
 *   DNS Resolver upcall management for CIFS DFS and AFS
 *   Handles host name to IP address resolution and DNS query for AFSDB RR.
 *
 *   Copyright (c) International Business Machines  Corp., 2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Wang Lei (wang840925@gmail.com)
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef _LINUX_DNS_RESOLVER_H
#define _LINUX_DNS_RESOLVER_H

#include <uapi/linux/dns_resolver.h>

extern int dns_query(const char *type, const char *name, size_t namelen,
		     const char *options, char **_result, time64_t *_expiry,
		     bool invalidate);

#endif /* _LINUX_DNS_RESOLVER_H */
