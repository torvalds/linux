/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *   fs/cifs/dns_resolve.h -- DNS Resolver upcall management for CIFS DFS
 *                            Handles host name to IP address resolution
 *
 *   Copyright (c) International Business Machines  Corp., 2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 */

#ifndef _DNS_RESOLVE_H
#define _DNS_RESOLVE_H

#ifdef __KERNEL__
extern int dns_resolve_server_name_to_ip(const char *unc, char **ip_addr);
#endif /* KERNEL */

#endif /* _DNS_RESOLVE_H */
