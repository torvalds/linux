/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Resolve DNS hostnames into valid ip addresses
 */
#ifndef __LINUX_FS_NFS_DNS_RESOLVE_H
#define __LINUX_FS_NFS_DNS_RESOLVE_H

#define NFS_DNS_HOSTNAME_MAXLEN	(128)


#ifdef CONFIG_NFS_USE_KERNEL_DNS
static inline int nfs_dns_resolver_init(void)
{
	return 0;
}

static inline void nfs_dns_resolver_destroy(void)
{}

static inline int nfs_dns_resolver_cache_init(struct net *net)
{
	return 0;
}

static inline void nfs_dns_resolver_cache_destroy(struct net *net)
{}
#else
extern int nfs_dns_resolver_init(void);
extern void nfs_dns_resolver_destroy(void);
extern int nfs_dns_resolver_cache_init(struct net *net);
extern void nfs_dns_resolver_cache_destroy(struct net *net);
#endif

extern ssize_t nfs_dns_resolve_name(struct net *net, char *name,
		size_t namelen,	struct sockaddr *sa, size_t salen);

#endif
