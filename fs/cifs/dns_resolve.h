/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *   DNS Resolver upcall management for CIFS DFS
 *   Handles host name to IP address resolution
 *
 *   Copyright (c) International Business Machines  Corp., 2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 */

#ifndef _DNS_RESOLVE_H
#define _DNS_RESOLVE_H

#ifdef __KERNEL__
extern int dns_resolve_server_name_to_ip(const char *unc, char **ip_addr, time64_t *expiry);
#endif /* KERNEL */

#endif /* _DNS_RESOLVE_H */
