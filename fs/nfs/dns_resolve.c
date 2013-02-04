/*
 * linux/fs/nfs/dns_resolve.c
 *
 * Copyright (c) 2009 Trond Myklebust <Trond.Myklebust@netapp.com>
 *
 * Resolves DNS hostnames into valid ip addresses
 */

#ifdef CONFIG_NFS_USE_KERNEL_DNS

#include <linux/module.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/addr.h>
#include <linux/dns_resolver.h>
#include "dns_resolve.h"

ssize_t nfs_dns_resolve_name(struct net *net, char *name, size_t namelen,
		struct sockaddr *sa, size_t salen)
{
	ssize_t ret;
	char *ip_addr = NULL;
	int ip_len;

	ip_len = dns_query(NULL, name, namelen, NULL, &ip_addr, NULL);
	if (ip_len > 0)
		ret = rpc_pton(net, ip_addr, ip_len, sa, salen);
	else
		ret = -ESRCH;
	kfree(ip_addr);
	return ret;
}
EXPORT_SYMBOL_GPL(nfs_dns_resolve_name);

#else

#include <linux/module.h>
#include <linux/hash.h>
#include <linux/string.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/seq_file.h>
#include <linux/inet.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/addr.h>
#include <linux/sunrpc/cache.h>
#include <linux/sunrpc/svcauth.h>
#include <linux/sunrpc/rpc_pipe_fs.h>

#include "dns_resolve.h"
#include "cache_lib.h"
#include "netns.h"

#define NFS_DNS_HASHBITS 4
#define NFS_DNS_HASHTBL_SIZE (1 << NFS_DNS_HASHBITS)

struct nfs_dns_ent {
	struct cache_head h;

	char *hostname;
	size_t namelen;

	struct sockaddr_storage addr;
	size_t addrlen;
};


static void nfs_dns_ent_update(struct cache_head *cnew,
		struct cache_head *ckey)
{
	struct nfs_dns_ent *new;
	struct nfs_dns_ent *key;

	new = container_of(cnew, struct nfs_dns_ent, h);
	key = container_of(ckey, struct nfs_dns_ent, h);

	memcpy(&new->addr, &key->addr, key->addrlen);
	new->addrlen = key->addrlen;
}

static void nfs_dns_ent_init(struct cache_head *cnew,
		struct cache_head *ckey)
{
	struct nfs_dns_ent *new;
	struct nfs_dns_ent *key;

	new = container_of(cnew, struct nfs_dns_ent, h);
	key = container_of(ckey, struct nfs_dns_ent, h);

	kfree(new->hostname);
	new->hostname = kstrndup(key->hostname, key->namelen, GFP_KERNEL);
	if (new->hostname) {
		new->namelen = key->namelen;
		nfs_dns_ent_update(cnew, ckey);
	} else {
		new->namelen = 0;
		new->addrlen = 0;
	}
}

static void nfs_dns_ent_put(struct kref *ref)
{
	struct nfs_dns_ent *item;

	item = container_of(ref, struct nfs_dns_ent, h.ref);
	kfree(item->hostname);
	kfree(item);
}

static struct cache_head *nfs_dns_ent_alloc(void)
{
	struct nfs_dns_ent *item = kmalloc(sizeof(*item), GFP_KERNEL);

	if (item != NULL) {
		item->hostname = NULL;
		item->namelen = 0;
		item->addrlen = 0;
		return &item->h;
	}
	return NULL;
};

static unsigned int nfs_dns_hash(const struct nfs_dns_ent *key)
{
	return hash_str(key->hostname, NFS_DNS_HASHBITS);
}

static void nfs_dns_request(struct cache_detail *cd,
		struct cache_head *ch,
		char **bpp, int *blen)
{
	struct nfs_dns_ent *key = container_of(ch, struct nfs_dns_ent, h);

	qword_add(bpp, blen, key->hostname);
	(*bpp)[-1] = '\n';
}

static int nfs_dns_upcall(struct cache_detail *cd,
		struct cache_head *ch)
{
	struct nfs_dns_ent *key = container_of(ch, struct nfs_dns_ent, h);
	int ret;

	ret = nfs_cache_upcall(cd, key->hostname);
	if (ret)
		ret = sunrpc_cache_pipe_upcall(cd, ch, nfs_dns_request);
	return ret;
}

static int nfs_dns_match(struct cache_head *ca,
		struct cache_head *cb)
{
	struct nfs_dns_ent *a;
	struct nfs_dns_ent *b;

	a = container_of(ca, struct nfs_dns_ent, h);
	b = container_of(cb, struct nfs_dns_ent, h);

	if (a->namelen == 0 || a->namelen != b->namelen)
		return 0;
	return memcmp(a->hostname, b->hostname, a->namelen) == 0;
}

static int nfs_dns_show(struct seq_file *m, struct cache_detail *cd,
		struct cache_head *h)
{
	struct nfs_dns_ent *item;
	long ttl;

	if (h == NULL) {
		seq_puts(m, "# ip address      hostname        ttl\n");
		return 0;
	}
	item = container_of(h, struct nfs_dns_ent, h);
	ttl = item->h.expiry_time - seconds_since_boot();
	if (ttl < 0)
		ttl = 0;

	if (!test_bit(CACHE_NEGATIVE, &h->flags)) {
		char buf[INET6_ADDRSTRLEN+IPV6_SCOPE_ID_LEN+1];

		rpc_ntop((struct sockaddr *)&item->addr, buf, sizeof(buf));
		seq_printf(m, "%15s ", buf);
	} else
		seq_puts(m, "<none>          ");
	seq_printf(m, "%15s %ld\n", item->hostname, ttl);
	return 0;
}

static struct nfs_dns_ent *nfs_dns_lookup(struct cache_detail *cd,
		struct nfs_dns_ent *key)
{
	struct cache_head *ch;

	ch = sunrpc_cache_lookup(cd,
			&key->h,
			nfs_dns_hash(key));
	if (!ch)
		return NULL;
	return container_of(ch, struct nfs_dns_ent, h);
}

static struct nfs_dns_ent *nfs_dns_update(struct cache_detail *cd,
		struct nfs_dns_ent *new,
		struct nfs_dns_ent *key)
{
	struct cache_head *ch;

	ch = sunrpc_cache_update(cd,
			&new->h, &key->h,
			nfs_dns_hash(key));
	if (!ch)
		return NULL;
	return container_of(ch, struct nfs_dns_ent, h);
}

static int nfs_dns_parse(struct cache_detail *cd, char *buf, int buflen)
{
	char buf1[NFS_DNS_HOSTNAME_MAXLEN+1];
	struct nfs_dns_ent key, *item;
	unsigned int ttl;
	ssize_t len;
	int ret = -EINVAL;

	if (buf[buflen-1] != '\n')
		goto out;
	buf[buflen-1] = '\0';

	len = qword_get(&buf, buf1, sizeof(buf1));
	if (len <= 0)
		goto out;
	key.addrlen = rpc_pton(cd->net, buf1, len,
			(struct sockaddr *)&key.addr,
			sizeof(key.addr));

	len = qword_get(&buf, buf1, sizeof(buf1));
	if (len <= 0)
		goto out;

	key.hostname = buf1;
	key.namelen = len;
	memset(&key.h, 0, sizeof(key.h));

	if (get_uint(&buf, &ttl) < 0)
		goto out;
	if (ttl == 0)
		goto out;
	key.h.expiry_time = ttl + seconds_since_boot();

	ret = -ENOMEM;
	item = nfs_dns_lookup(cd, &key);
	if (item == NULL)
		goto out;

	if (key.addrlen == 0)
		set_bit(CACHE_NEGATIVE, &key.h.flags);

	item = nfs_dns_update(cd, &key, item);
	if (item == NULL)
		goto out;

	ret = 0;
	cache_put(&item->h, cd);
out:
	return ret;
}

static int do_cache_lookup(struct cache_detail *cd,
		struct nfs_dns_ent *key,
		struct nfs_dns_ent **item,
		struct nfs_cache_defer_req *dreq)
{
	int ret = -ENOMEM;

	*item = nfs_dns_lookup(cd, key);
	if (*item) {
		ret = cache_check(cd, &(*item)->h, &dreq->req);
		if (ret)
			*item = NULL;
	}
	return ret;
}

static int do_cache_lookup_nowait(struct cache_detail *cd,
		struct nfs_dns_ent *key,
		struct nfs_dns_ent **item)
{
	int ret = -ENOMEM;

	*item = nfs_dns_lookup(cd, key);
	if (!*item)
		goto out_err;
	ret = -ETIMEDOUT;
	if (!test_bit(CACHE_VALID, &(*item)->h.flags)
			|| (*item)->h.expiry_time < seconds_since_boot()
			|| cd->flush_time > (*item)->h.last_refresh)
		goto out_put;
	ret = -ENOENT;
	if (test_bit(CACHE_NEGATIVE, &(*item)->h.flags))
		goto out_put;
	return 0;
out_put:
	cache_put(&(*item)->h, cd);
out_err:
	*item = NULL;
	return ret;
}

static int do_cache_lookup_wait(struct cache_detail *cd,
		struct nfs_dns_ent *key,
		struct nfs_dns_ent **item)
{
	struct nfs_cache_defer_req *dreq;
	int ret = -ENOMEM;

	dreq = nfs_cache_defer_req_alloc();
	if (!dreq)
		goto out;
	ret = do_cache_lookup(cd, key, item, dreq);
	if (ret == -EAGAIN) {
		ret = nfs_cache_wait_for_upcall(dreq);
		if (!ret)
			ret = do_cache_lookup_nowait(cd, key, item);
	}
	nfs_cache_defer_req_put(dreq);
out:
	return ret;
}

ssize_t nfs_dns_resolve_name(struct net *net, char *name,
		size_t namelen, struct sockaddr *sa, size_t salen)
{
	struct nfs_dns_ent key = {
		.hostname = name,
		.namelen = namelen,
	};
	struct nfs_dns_ent *item = NULL;
	ssize_t ret;
	struct nfs_net *nn = net_generic(net, nfs_net_id);

	ret = do_cache_lookup_wait(nn->nfs_dns_resolve, &key, &item);
	if (ret == 0) {
		if (salen >= item->addrlen) {
			memcpy(sa, &item->addr, item->addrlen);
			ret = item->addrlen;
		} else
			ret = -EOVERFLOW;
		cache_put(&item->h, nn->nfs_dns_resolve);
	} else if (ret == -ENOENT)
		ret = -ESRCH;
	return ret;
}
EXPORT_SYMBOL_GPL(nfs_dns_resolve_name);

int nfs_dns_resolver_cache_init(struct net *net)
{
	int err = -ENOMEM;
	struct nfs_net *nn = net_generic(net, nfs_net_id);
	struct cache_detail *cd;
	struct cache_head **tbl;

	cd = kzalloc(sizeof(struct cache_detail), GFP_KERNEL);
	if (cd == NULL)
		goto err_cd;

	tbl = kzalloc(NFS_DNS_HASHTBL_SIZE * sizeof(struct cache_head *),
			GFP_KERNEL);
	if (tbl == NULL)
		goto err_tbl;

	cd->owner = THIS_MODULE,
	cd->hash_size = NFS_DNS_HASHTBL_SIZE,
	cd->hash_table = tbl,
	cd->name = "dns_resolve",
	cd->cache_put = nfs_dns_ent_put,
	cd->cache_upcall = nfs_dns_upcall,
	cd->cache_parse = nfs_dns_parse,
	cd->cache_show = nfs_dns_show,
	cd->match = nfs_dns_match,
	cd->init = nfs_dns_ent_init,
	cd->update = nfs_dns_ent_update,
	cd->alloc = nfs_dns_ent_alloc,

	nfs_cache_init(cd);
	err = nfs_cache_register_net(net, cd);
	if (err)
		goto err_reg;
	nn->nfs_dns_resolve = cd;
	return 0;

err_reg:
	nfs_cache_destroy(cd);
	kfree(cd->hash_table);
err_tbl:
	kfree(cd);
err_cd:
	return err;
}

void nfs_dns_resolver_cache_destroy(struct net *net)
{
	struct nfs_net *nn = net_generic(net, nfs_net_id);
	struct cache_detail *cd = nn->nfs_dns_resolve;

	nfs_cache_unregister_net(net, cd);
	nfs_cache_destroy(cd);
	kfree(cd->hash_table);
	kfree(cd);
}

static int rpc_pipefs_event(struct notifier_block *nb, unsigned long event,
			   void *ptr)
{
	struct super_block *sb = ptr;
	struct net *net = sb->s_fs_info;
	struct nfs_net *nn = net_generic(net, nfs_net_id);
	struct cache_detail *cd = nn->nfs_dns_resolve;
	int ret = 0;

	if (cd == NULL)
		return 0;

	if (!try_module_get(THIS_MODULE))
		return 0;

	switch (event) {
	case RPC_PIPEFS_MOUNT:
		ret = nfs_cache_register_sb(sb, cd);
		break;
	case RPC_PIPEFS_UMOUNT:
		nfs_cache_unregister_sb(sb, cd);
		break;
	default:
		ret = -ENOTSUPP;
		break;
	}
	module_put(THIS_MODULE);
	return ret;
}

static struct notifier_block nfs_dns_resolver_block = {
	.notifier_call	= rpc_pipefs_event,
};

int nfs_dns_resolver_init(void)
{
	return rpc_pipefs_notifier_register(&nfs_dns_resolver_block);
}

void nfs_dns_resolver_destroy(void)
{
	rpc_pipefs_notifier_unregister(&nfs_dns_resolver_block);
}
#endif
