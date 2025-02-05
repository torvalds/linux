// SPDX-License-Identifier: GPL-2.0-only
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/sunrpc/types.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/svcauth.h>
#include <linux/sunrpc/gss_api.h>
#include <linux/sunrpc/addr.h>
#include <linux/err.h>
#include <linux/seq_file.h>
#include <linux/hash.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <net/ipv6.h>
#include <linux/kernel.h>
#include <linux/user_namespace.h>
#include <trace/events/sunrpc.h>

#define RPCDBG_FACILITY	RPCDBG_AUTH

#include "netns.h"

/*
 * AUTHUNIX and AUTHNULL credentials are both handled here.
 * AUTHNULL is treated just like AUTHUNIX except that the uid/gid
 * are always nobody (-2).  i.e. we do the same IP address checks for
 * AUTHNULL as for AUTHUNIX, and that is done here.
 */


struct unix_domain {
	struct auth_domain	h;
	/* other stuff later */
};

extern struct auth_ops svcauth_null;
extern struct auth_ops svcauth_unix;
extern struct auth_ops svcauth_tls;

static void svcauth_unix_domain_release_rcu(struct rcu_head *head)
{
	struct auth_domain *dom = container_of(head, struct auth_domain, rcu_head);
	struct unix_domain *ud = container_of(dom, struct unix_domain, h);

	kfree(dom->name);
	kfree(ud);
}

static void svcauth_unix_domain_release(struct auth_domain *dom)
{
	call_rcu(&dom->rcu_head, svcauth_unix_domain_release_rcu);
}

struct auth_domain *unix_domain_find(char *name)
{
	struct auth_domain *rv;
	struct unix_domain *new = NULL;

	rv = auth_domain_find(name);
	while(1) {
		if (rv) {
			if (new && rv != &new->h)
				svcauth_unix_domain_release(&new->h);

			if (rv->flavour != &svcauth_unix) {
				auth_domain_put(rv);
				return NULL;
			}
			return rv;
		}

		new = kmalloc(sizeof(*new), GFP_KERNEL);
		if (new == NULL)
			return NULL;
		kref_init(&new->h.ref);
		new->h.name = kstrdup(name, GFP_KERNEL);
		if (new->h.name == NULL) {
			kfree(new);
			return NULL;
		}
		new->h.flavour = &svcauth_unix;
		rv = auth_domain_lookup(name, &new->h);
	}
}
EXPORT_SYMBOL_GPL(unix_domain_find);


/**************************************************
 * cache for IP address to unix_domain
 * as needed by AUTH_UNIX
 */
#define	IP_HASHBITS	8
#define	IP_HASHMAX	(1<<IP_HASHBITS)

struct ip_map {
	struct cache_head	h;
	char			m_class[8]; /* e.g. "nfsd" */
	struct in6_addr		m_addr;
	struct unix_domain	*m_client;
	struct rcu_head		m_rcu;
};

static void ip_map_put(struct kref *kref)
{
	struct cache_head *item = container_of(kref, struct cache_head, ref);
	struct ip_map *im = container_of(item, struct ip_map,h);

	if (test_bit(CACHE_VALID, &item->flags) &&
	    !test_bit(CACHE_NEGATIVE, &item->flags))
		auth_domain_put(&im->m_client->h);
	kfree_rcu(im, m_rcu);
}

static inline int hash_ip6(const struct in6_addr *ip)
{
	return hash_32(ipv6_addr_hash(ip), IP_HASHBITS);
}
static int ip_map_match(struct cache_head *corig, struct cache_head *cnew)
{
	struct ip_map *orig = container_of(corig, struct ip_map, h);
	struct ip_map *new = container_of(cnew, struct ip_map, h);
	return strcmp(orig->m_class, new->m_class) == 0 &&
	       ipv6_addr_equal(&orig->m_addr, &new->m_addr);
}
static void ip_map_init(struct cache_head *cnew, struct cache_head *citem)
{
	struct ip_map *new = container_of(cnew, struct ip_map, h);
	struct ip_map *item = container_of(citem, struct ip_map, h);

	strcpy(new->m_class, item->m_class);
	new->m_addr = item->m_addr;
}
static void update(struct cache_head *cnew, struct cache_head *citem)
{
	struct ip_map *new = container_of(cnew, struct ip_map, h);
	struct ip_map *item = container_of(citem, struct ip_map, h);

	kref_get(&item->m_client->h.ref);
	new->m_client = item->m_client;
}
static struct cache_head *ip_map_alloc(void)
{
	struct ip_map *i = kmalloc(sizeof(*i), GFP_KERNEL);
	if (i)
		return &i->h;
	else
		return NULL;
}

static int ip_map_upcall(struct cache_detail *cd, struct cache_head *h)
{
	return sunrpc_cache_pipe_upcall(cd, h);
}

static void ip_map_request(struct cache_detail *cd,
				  struct cache_head *h,
				  char **bpp, int *blen)
{
	char text_addr[40];
	struct ip_map *im = container_of(h, struct ip_map, h);

	if (ipv6_addr_v4mapped(&(im->m_addr))) {
		snprintf(text_addr, 20, "%pI4", &im->m_addr.s6_addr32[3]);
	} else {
		snprintf(text_addr, 40, "%pI6", &im->m_addr);
	}
	qword_add(bpp, blen, im->m_class);
	qword_add(bpp, blen, text_addr);
	(*bpp)[-1] = '\n';
}

static struct ip_map *__ip_map_lookup(struct cache_detail *cd, char *class, struct in6_addr *addr);
static int __ip_map_update(struct cache_detail *cd, struct ip_map *ipm, struct unix_domain *udom, time64_t expiry);

static int ip_map_parse(struct cache_detail *cd,
			  char *mesg, int mlen)
{
	/* class ipaddress [domainname] */
	/* should be safe just to use the start of the input buffer
	 * for scratch: */
	char *buf = mesg;
	int len;
	char class[8];
	union {
		struct sockaddr		sa;
		struct sockaddr_in	s4;
		struct sockaddr_in6	s6;
	} address;
	struct sockaddr_in6 sin6;
	int err;

	struct ip_map *ipmp;
	struct auth_domain *dom;
	time64_t expiry;

	if (mesg[mlen-1] != '\n')
		return -EINVAL;
	mesg[mlen-1] = 0;

	/* class */
	len = qword_get(&mesg, class, sizeof(class));
	if (len <= 0) return -EINVAL;

	/* ip address */
	len = qword_get(&mesg, buf, mlen);
	if (len <= 0) return -EINVAL;

	if (rpc_pton(cd->net, buf, len, &address.sa, sizeof(address)) == 0)
		return -EINVAL;
	switch (address.sa.sa_family) {
	case AF_INET:
		/* Form a mapped IPv4 address in sin6 */
		sin6.sin6_family = AF_INET6;
		ipv6_addr_set_v4mapped(address.s4.sin_addr.s_addr,
				&sin6.sin6_addr);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		memcpy(&sin6, &address.s6, sizeof(sin6));
		break;
#endif
	default:
		return -EINVAL;
	}

	err = get_expiry(&mesg, &expiry);
	if (err)
		return err;

	/* domainname, or empty for NEGATIVE */
	len = qword_get(&mesg, buf, mlen);
	if (len < 0) return -EINVAL;

	if (len) {
		dom = unix_domain_find(buf);
		if (dom == NULL)
			return -ENOENT;
	} else
		dom = NULL;

	/* IPv6 scope IDs are ignored for now */
	ipmp = __ip_map_lookup(cd, class, &sin6.sin6_addr);
	if (ipmp) {
		err = __ip_map_update(cd, ipmp,
			     container_of(dom, struct unix_domain, h),
			     expiry);
	} else
		err = -ENOMEM;

	if (dom)
		auth_domain_put(dom);

	cache_flush();
	return err;
}

static int ip_map_show(struct seq_file *m,
		       struct cache_detail *cd,
		       struct cache_head *h)
{
	struct ip_map *im;
	struct in6_addr addr;
	char *dom = "-no-domain-";

	if (h == NULL) {
		seq_puts(m, "#class IP domain\n");
		return 0;
	}
	im = container_of(h, struct ip_map, h);
	/* class addr domain */
	addr = im->m_addr;

	if (test_bit(CACHE_VALID, &h->flags) &&
	    !test_bit(CACHE_NEGATIVE, &h->flags))
		dom = im->m_client->h.name;

	if (ipv6_addr_v4mapped(&addr)) {
		seq_printf(m, "%s %pI4 %s\n",
			im->m_class, &addr.s6_addr32[3], dom);
	} else {
		seq_printf(m, "%s %pI6 %s\n", im->m_class, &addr, dom);
	}
	return 0;
}


static struct ip_map *__ip_map_lookup(struct cache_detail *cd, char *class,
		struct in6_addr *addr)
{
	struct ip_map ip;
	struct cache_head *ch;

	strcpy(ip.m_class, class);
	ip.m_addr = *addr;
	ch = sunrpc_cache_lookup_rcu(cd, &ip.h,
				     hash_str(class, IP_HASHBITS) ^
				     hash_ip6(addr));

	if (ch)
		return container_of(ch, struct ip_map, h);
	else
		return NULL;
}

static int __ip_map_update(struct cache_detail *cd, struct ip_map *ipm,
		struct unix_domain *udom, time64_t expiry)
{
	struct ip_map ip;
	struct cache_head *ch;

	ip.m_client = udom;
	ip.h.flags = 0;
	if (!udom)
		set_bit(CACHE_NEGATIVE, &ip.h.flags);
	ip.h.expiry_time = expiry;
	ch = sunrpc_cache_update(cd, &ip.h, &ipm->h,
				 hash_str(ipm->m_class, IP_HASHBITS) ^
				 hash_ip6(&ipm->m_addr));
	if (!ch)
		return -ENOMEM;
	cache_put(ch, cd);
	return 0;
}

void svcauth_unix_purge(struct net *net)
{
	struct sunrpc_net *sn;

	sn = net_generic(net, sunrpc_net_id);
	cache_purge(sn->ip_map_cache);
}
EXPORT_SYMBOL_GPL(svcauth_unix_purge);

static inline struct ip_map *
ip_map_cached_get(struct svc_xprt *xprt)
{
	struct ip_map *ipm = NULL;
	struct sunrpc_net *sn;

	if (test_bit(XPT_CACHE_AUTH, &xprt->xpt_flags)) {
		spin_lock(&xprt->xpt_lock);
		ipm = xprt->xpt_auth_cache;
		if (ipm != NULL) {
			sn = net_generic(xprt->xpt_net, sunrpc_net_id);
			if (cache_is_expired(sn->ip_map_cache, &ipm->h)) {
				/*
				 * The entry has been invalidated since it was
				 * remembered, e.g. by a second mount from the
				 * same IP address.
				 */
				xprt->xpt_auth_cache = NULL;
				spin_unlock(&xprt->xpt_lock);
				cache_put(&ipm->h, sn->ip_map_cache);
				return NULL;
			}
			cache_get(&ipm->h);
		}
		spin_unlock(&xprt->xpt_lock);
	}
	return ipm;
}

static inline void
ip_map_cached_put(struct svc_xprt *xprt, struct ip_map *ipm)
{
	if (test_bit(XPT_CACHE_AUTH, &xprt->xpt_flags)) {
		spin_lock(&xprt->xpt_lock);
		if (xprt->xpt_auth_cache == NULL) {
			/* newly cached, keep the reference */
			xprt->xpt_auth_cache = ipm;
			ipm = NULL;
		}
		spin_unlock(&xprt->xpt_lock);
	}
	if (ipm) {
		struct sunrpc_net *sn;

		sn = net_generic(xprt->xpt_net, sunrpc_net_id);
		cache_put(&ipm->h, sn->ip_map_cache);
	}
}

void
svcauth_unix_info_release(struct svc_xprt *xpt)
{
	struct ip_map *ipm;

	ipm = xpt->xpt_auth_cache;
	if (ipm != NULL) {
		struct sunrpc_net *sn;

		sn = net_generic(xpt->xpt_net, sunrpc_net_id);
		cache_put(&ipm->h, sn->ip_map_cache);
	}
}

/****************************************************************************
 * auth.unix.gid cache
 * simple cache to map a UID to a list of GIDs
 * because AUTH_UNIX aka AUTH_SYS has a max of UNX_NGROUPS
 */
#define	GID_HASHBITS	8
#define	GID_HASHMAX	(1<<GID_HASHBITS)

struct unix_gid {
	struct cache_head	h;
	kuid_t			uid;
	struct group_info	*gi;
	struct rcu_head		rcu;
};

static int unix_gid_hash(kuid_t uid)
{
	return hash_long(from_kuid(&init_user_ns, uid), GID_HASHBITS);
}

static void unix_gid_free(struct rcu_head *rcu)
{
	struct unix_gid *ug = container_of(rcu, struct unix_gid, rcu);
	struct cache_head *item = &ug->h;

	if (test_bit(CACHE_VALID, &item->flags) &&
	    !test_bit(CACHE_NEGATIVE, &item->flags))
		put_group_info(ug->gi);
	kfree(ug);
}

static void unix_gid_put(struct kref *kref)
{
	struct cache_head *item = container_of(kref, struct cache_head, ref);
	struct unix_gid *ug = container_of(item, struct unix_gid, h);

	call_rcu(&ug->rcu, unix_gid_free);
}

static int unix_gid_match(struct cache_head *corig, struct cache_head *cnew)
{
	struct unix_gid *orig = container_of(corig, struct unix_gid, h);
	struct unix_gid *new = container_of(cnew, struct unix_gid, h);
	return uid_eq(orig->uid, new->uid);
}
static void unix_gid_init(struct cache_head *cnew, struct cache_head *citem)
{
	struct unix_gid *new = container_of(cnew, struct unix_gid, h);
	struct unix_gid *item = container_of(citem, struct unix_gid, h);
	new->uid = item->uid;
}
static void unix_gid_update(struct cache_head *cnew, struct cache_head *citem)
{
	struct unix_gid *new = container_of(cnew, struct unix_gid, h);
	struct unix_gid *item = container_of(citem, struct unix_gid, h);

	get_group_info(item->gi);
	new->gi = item->gi;
}
static struct cache_head *unix_gid_alloc(void)
{
	struct unix_gid *g = kmalloc(sizeof(*g), GFP_KERNEL);
	if (g)
		return &g->h;
	else
		return NULL;
}

static int unix_gid_upcall(struct cache_detail *cd, struct cache_head *h)
{
	return sunrpc_cache_pipe_upcall_timeout(cd, h);
}

static void unix_gid_request(struct cache_detail *cd,
			     struct cache_head *h,
			     char **bpp, int *blen)
{
	char tuid[20];
	struct unix_gid *ug = container_of(h, struct unix_gid, h);

	snprintf(tuid, 20, "%u", from_kuid(&init_user_ns, ug->uid));
	qword_add(bpp, blen, tuid);
	(*bpp)[-1] = '\n';
}

static struct unix_gid *unix_gid_lookup(struct cache_detail *cd, kuid_t uid);

static int unix_gid_parse(struct cache_detail *cd,
			char *mesg, int mlen)
{
	/* uid expiry Ngid gid0 gid1 ... gidN-1 */
	int id;
	kuid_t uid;
	int gids;
	int rv;
	int i;
	int err;
	time64_t expiry;
	struct unix_gid ug, *ugp;

	if (mesg[mlen - 1] != '\n')
		return -EINVAL;
	mesg[mlen-1] = 0;

	rv = get_int(&mesg, &id);
	if (rv)
		return -EINVAL;
	uid = make_kuid(current_user_ns(), id);
	ug.uid = uid;

	err = get_expiry(&mesg, &expiry);
	if (err)
		return err;

	rv = get_int(&mesg, &gids);
	if (rv || gids < 0 || gids > 8192)
		return -EINVAL;

	ug.gi = groups_alloc(gids);
	if (!ug.gi)
		return -ENOMEM;

	for (i = 0 ; i < gids ; i++) {
		int gid;
		kgid_t kgid;
		rv = get_int(&mesg, &gid);
		err = -EINVAL;
		if (rv)
			goto out;
		kgid = make_kgid(current_user_ns(), gid);
		if (!gid_valid(kgid))
			goto out;
		ug.gi->gid[i] = kgid;
	}

	groups_sort(ug.gi);
	ugp = unix_gid_lookup(cd, uid);
	if (ugp) {
		struct cache_head *ch;
		ug.h.flags = 0;
		ug.h.expiry_time = expiry;
		ch = sunrpc_cache_update(cd,
					 &ug.h, &ugp->h,
					 unix_gid_hash(uid));
		if (!ch)
			err = -ENOMEM;
		else {
			err = 0;
			cache_put(ch, cd);
		}
	} else
		err = -ENOMEM;
 out:
	if (ug.gi)
		put_group_info(ug.gi);
	return err;
}

static int unix_gid_show(struct seq_file *m,
			 struct cache_detail *cd,
			 struct cache_head *h)
{
	struct user_namespace *user_ns = m->file->f_cred->user_ns;
	struct unix_gid *ug;
	int i;
	int glen;

	if (h == NULL) {
		seq_puts(m, "#uid cnt: gids...\n");
		return 0;
	}
	ug = container_of(h, struct unix_gid, h);
	if (test_bit(CACHE_VALID, &h->flags) &&
	    !test_bit(CACHE_NEGATIVE, &h->flags))
		glen = ug->gi->ngroups;
	else
		glen = 0;

	seq_printf(m, "%u %d:", from_kuid_munged(user_ns, ug->uid), glen);
	for (i = 0; i < glen; i++)
		seq_printf(m, " %d", from_kgid_munged(user_ns, ug->gi->gid[i]));
	seq_printf(m, "\n");
	return 0;
}

static const struct cache_detail unix_gid_cache_template = {
	.owner		= THIS_MODULE,
	.hash_size	= GID_HASHMAX,
	.name		= "auth.unix.gid",
	.cache_put	= unix_gid_put,
	.cache_upcall	= unix_gid_upcall,
	.cache_request	= unix_gid_request,
	.cache_parse	= unix_gid_parse,
	.cache_show	= unix_gid_show,
	.match		= unix_gid_match,
	.init		= unix_gid_init,
	.update		= unix_gid_update,
	.alloc		= unix_gid_alloc,
};

int unix_gid_cache_create(struct net *net)
{
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);
	struct cache_detail *cd;
	int err;

	cd = cache_create_net(&unix_gid_cache_template, net);
	if (IS_ERR(cd))
		return PTR_ERR(cd);
	err = cache_register_net(cd, net);
	if (err) {
		cache_destroy_net(cd, net);
		return err;
	}
	sn->unix_gid_cache = cd;
	return 0;
}

void unix_gid_cache_destroy(struct net *net)
{
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);
	struct cache_detail *cd = sn->unix_gid_cache;

	sn->unix_gid_cache = NULL;
	cache_purge(cd);
	cache_unregister_net(cd, net);
	cache_destroy_net(cd, net);
}

static struct unix_gid *unix_gid_lookup(struct cache_detail *cd, kuid_t uid)
{
	struct unix_gid ug;
	struct cache_head *ch;

	ug.uid = uid;
	ch = sunrpc_cache_lookup_rcu(cd, &ug.h, unix_gid_hash(uid));
	if (ch)
		return container_of(ch, struct unix_gid, h);
	else
		return NULL;
}

static struct group_info *unix_gid_find(kuid_t uid, struct svc_rqst *rqstp)
{
	struct unix_gid *ug;
	struct group_info *gi;
	int ret;
	struct sunrpc_net *sn = net_generic(rqstp->rq_xprt->xpt_net,
					    sunrpc_net_id);

	ug = unix_gid_lookup(sn->unix_gid_cache, uid);
	if (!ug)
		return ERR_PTR(-EAGAIN);
	ret = cache_check(sn->unix_gid_cache, &ug->h, &rqstp->rq_chandle);
	switch (ret) {
	case -ENOENT:
		return ERR_PTR(-ENOENT);
	case -ETIMEDOUT:
		return ERR_PTR(-ESHUTDOWN);
	case 0:
		gi = get_group_info(ug->gi);
		cache_put(&ug->h, sn->unix_gid_cache);
		return gi;
	default:
		return ERR_PTR(-EAGAIN);
	}
}

enum svc_auth_status
svcauth_unix_set_client(struct svc_rqst *rqstp)
{
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6, sin6_storage;
	struct ip_map *ipm;
	struct group_info *gi;
	struct svc_cred *cred = &rqstp->rq_cred;
	struct svc_xprt *xprt = rqstp->rq_xprt;
	struct net *net = xprt->xpt_net;
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);

	switch (rqstp->rq_addr.ss_family) {
	case AF_INET:
		sin = svc_addr_in(rqstp);
		sin6 = &sin6_storage;
		ipv6_addr_set_v4mapped(sin->sin_addr.s_addr, &sin6->sin6_addr);
		break;
	case AF_INET6:
		sin6 = svc_addr_in6(rqstp);
		break;
	default:
		BUG();
	}

	rqstp->rq_client = NULL;
	if (rqstp->rq_proc == 0)
		goto out;

	rqstp->rq_auth_stat = rpc_autherr_badcred;
	ipm = ip_map_cached_get(xprt);
	if (ipm == NULL)
		ipm = __ip_map_lookup(sn->ip_map_cache,
				      rqstp->rq_server->sv_programs->pg_class,
				    &sin6->sin6_addr);

	if (ipm == NULL)
		return SVC_DENIED;

	switch (cache_check(sn->ip_map_cache, &ipm->h, &rqstp->rq_chandle)) {
		default:
			BUG();
		case -ETIMEDOUT:
			return SVC_CLOSE;
		case -EAGAIN:
			return SVC_DROP;
		case -ENOENT:
			return SVC_DENIED;
		case 0:
			rqstp->rq_client = &ipm->m_client->h;
			kref_get(&rqstp->rq_client->ref);
			ip_map_cached_put(xprt, ipm);
			break;
	}

	gi = unix_gid_find(cred->cr_uid, rqstp);
	switch (PTR_ERR(gi)) {
	case -EAGAIN:
		return SVC_DROP;
	case -ESHUTDOWN:
		return SVC_CLOSE;
	case -ENOENT:
		break;
	default:
		put_group_info(cred->cr_group_info);
		cred->cr_group_info = gi;
	}

out:
	rqstp->rq_auth_stat = rpc_auth_ok;
	return SVC_OK;
}
EXPORT_SYMBOL_GPL(svcauth_unix_set_client);

/**
 * svcauth_null_accept - Decode and validate incoming RPC_AUTH_NULL credential
 * @rqstp: RPC transaction
 *
 * Return values:
 *   %SVC_OK: Both credential and verifier are valid
 *   %SVC_DENIED: Credential or verifier is not valid
 *   %SVC_GARBAGE: Failed to decode credential or verifier
 *   %SVC_CLOSE: Temporary failure
 *
 * rqstp->rq_auth_stat is set as mandated by RFC 5531.
 */
static enum svc_auth_status
svcauth_null_accept(struct svc_rqst *rqstp)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	struct svc_cred	*cred = &rqstp->rq_cred;
	u32 flavor, len;
	void *body;

	/* Length of Call's credential body field: */
	if (xdr_stream_decode_u32(xdr, &len) < 0)
		return SVC_GARBAGE;
	if (len != 0) {
		rqstp->rq_auth_stat = rpc_autherr_badcred;
		return SVC_DENIED;
	}

	/* Call's verf field: */
	if (xdr_stream_decode_opaque_auth(xdr, &flavor, &body, &len) < 0)
		return SVC_GARBAGE;
	if (flavor != RPC_AUTH_NULL || len != 0) {
		rqstp->rq_auth_stat = rpc_autherr_badverf;
		return SVC_DENIED;
	}

	/* Signal that mapping to nobody uid/gid is required */
	cred->cr_uid = INVALID_UID;
	cred->cr_gid = INVALID_GID;
	cred->cr_group_info = groups_alloc(0);
	if (cred->cr_group_info == NULL)
		return SVC_CLOSE; /* kmalloc failure - client must retry */

	if (xdr_stream_encode_opaque_auth(&rqstp->rq_res_stream,
					  RPC_AUTH_NULL, NULL, 0) < 0)
		return SVC_CLOSE;
	if (!svcxdr_set_accept_stat(rqstp))
		return SVC_CLOSE;

	rqstp->rq_cred.cr_flavor = RPC_AUTH_NULL;
	return SVC_OK;
}

static int
svcauth_null_release(struct svc_rqst *rqstp)
{
	if (rqstp->rq_client)
		auth_domain_put(rqstp->rq_client);
	rqstp->rq_client = NULL;
	if (rqstp->rq_cred.cr_group_info)
		put_group_info(rqstp->rq_cred.cr_group_info);
	rqstp->rq_cred.cr_group_info = NULL;

	return 0; /* don't drop */
}


struct auth_ops svcauth_null = {
	.name		= "null",
	.owner		= THIS_MODULE,
	.flavour	= RPC_AUTH_NULL,
	.accept		= svcauth_null_accept,
	.release	= svcauth_null_release,
	.set_client	= svcauth_unix_set_client,
};


/**
 * svcauth_tls_accept - Decode and validate incoming RPC_AUTH_TLS credential
 * @rqstp: RPC transaction
 *
 * Return values:
 *   %SVC_OK: Both credential and verifier are valid
 *   %SVC_DENIED: Credential or verifier is not valid
 *   %SVC_GARBAGE: Failed to decode credential or verifier
 *   %SVC_CLOSE: Temporary failure
 *
 * rqstp->rq_auth_stat is set as mandated by RFC 5531.
 */
static enum svc_auth_status
svcauth_tls_accept(struct svc_rqst *rqstp)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	struct svc_cred	*cred = &rqstp->rq_cred;
	struct svc_xprt *xprt = rqstp->rq_xprt;
	u32 flavor, len;
	void *body;
	__be32 *p;

	/* Length of Call's credential body field: */
	if (xdr_stream_decode_u32(xdr, &len) < 0)
		return SVC_GARBAGE;
	if (len != 0) {
		rqstp->rq_auth_stat = rpc_autherr_badcred;
		return SVC_DENIED;
	}

	/* Call's verf field: */
	if (xdr_stream_decode_opaque_auth(xdr, &flavor, &body, &len) < 0)
		return SVC_GARBAGE;
	if (flavor != RPC_AUTH_NULL || len != 0) {
		rqstp->rq_auth_stat = rpc_autherr_badverf;
		return SVC_DENIED;
	}

	/* AUTH_TLS is not valid on non-NULL procedures */
	if (rqstp->rq_proc != 0) {
		rqstp->rq_auth_stat = rpc_autherr_badcred;
		return SVC_DENIED;
	}

	/* Signal that mapping to nobody uid/gid is required */
	cred->cr_uid = INVALID_UID;
	cred->cr_gid = INVALID_GID;
	cred->cr_group_info = groups_alloc(0);
	if (cred->cr_group_info == NULL)
		return SVC_CLOSE;

	if (xprt->xpt_ops->xpo_handshake) {
		p = xdr_reserve_space(&rqstp->rq_res_stream, XDR_UNIT * 2 + 8);
		if (!p)
			return SVC_CLOSE;
		trace_svc_tls_start(xprt);
		*p++ = rpc_auth_null;
		*p++ = cpu_to_be32(8);
		memcpy(p, "STARTTLS", 8);

		set_bit(XPT_HANDSHAKE, &xprt->xpt_flags);
		svc_xprt_enqueue(xprt);
	} else {
		trace_svc_tls_unavailable(xprt);
		if (xdr_stream_encode_opaque_auth(&rqstp->rq_res_stream,
						  RPC_AUTH_NULL, NULL, 0) < 0)
			return SVC_CLOSE;
	}
	if (!svcxdr_set_accept_stat(rqstp))
		return SVC_CLOSE;

	rqstp->rq_cred.cr_flavor = RPC_AUTH_TLS;
	return SVC_OK;
}

struct auth_ops svcauth_tls = {
	.name		= "tls",
	.owner		= THIS_MODULE,
	.flavour	= RPC_AUTH_TLS,
	.accept		= svcauth_tls_accept,
	.release	= svcauth_null_release,
	.set_client	= svcauth_unix_set_client,
};


/**
 * svcauth_unix_accept - Decode and validate incoming RPC_AUTH_SYS credential
 * @rqstp: RPC transaction
 *
 * Return values:
 *   %SVC_OK: Both credential and verifier are valid
 *   %SVC_DENIED: Credential or verifier is not valid
 *   %SVC_GARBAGE: Failed to decode credential or verifier
 *   %SVC_CLOSE: Temporary failure
 *
 * rqstp->rq_auth_stat is set as mandated by RFC 5531.
 */
static enum svc_auth_status
svcauth_unix_accept(struct svc_rqst *rqstp)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	struct svc_cred	*cred = &rqstp->rq_cred;
	struct user_namespace *userns;
	u32 flavor, len, i;
	void *body;
	__be32 *p;

	/*
	 * This implementation ignores the length of the Call's
	 * credential body field and the timestamp and machinename
	 * fields.
	 */
	p = xdr_inline_decode(xdr, XDR_UNIT * 3);
	if (!p)
		return SVC_GARBAGE;
	len = be32_to_cpup(p + 2);
	if (len > RPC_MAX_MACHINENAME)
		return SVC_GARBAGE;
	if (!xdr_inline_decode(xdr, len))
		return SVC_GARBAGE;

	/*
	 * Note: we skip uid_valid()/gid_valid() checks here for
	 * backwards compatibility with clients that use -1 id's.
	 * Instead, -1 uid or gid is later mapped to the
	 * (export-specific) anonymous id by nfsd_setuser.
	 * Supplementary gid's will be left alone.
	 */
	userns = (rqstp->rq_xprt && rqstp->rq_xprt->xpt_cred) ?
		rqstp->rq_xprt->xpt_cred->user_ns : &init_user_ns;
	if (xdr_stream_decode_u32(xdr, &i) < 0)
		return SVC_GARBAGE;
	cred->cr_uid = make_kuid(userns, i);
	if (xdr_stream_decode_u32(xdr, &i) < 0)
		return SVC_GARBAGE;
	cred->cr_gid = make_kgid(userns, i);

	if (xdr_stream_decode_u32(xdr, &len) < 0)
		return SVC_GARBAGE;
	if (len > UNX_NGROUPS)
		goto badcred;
	p = xdr_inline_decode(xdr, XDR_UNIT * len);
	if (!p)
		return SVC_GARBAGE;
	cred->cr_group_info = groups_alloc(len);
	if (cred->cr_group_info == NULL)
		return SVC_CLOSE;
	for (i = 0; i < len; i++) {
		kgid_t kgid = make_kgid(userns, be32_to_cpup(p++));
		cred->cr_group_info->gid[i] = kgid;
	}
	groups_sort(cred->cr_group_info);

	/* Call's verf field: */
	if (xdr_stream_decode_opaque_auth(xdr, &flavor, &body, &len) < 0)
		return SVC_GARBAGE;
	if (flavor != RPC_AUTH_NULL || len != 0) {
		rqstp->rq_auth_stat = rpc_autherr_badverf;
		return SVC_DENIED;
	}

	if (xdr_stream_encode_opaque_auth(&rqstp->rq_res_stream,
					  RPC_AUTH_NULL, NULL, 0) < 0)
		return SVC_CLOSE;
	if (!svcxdr_set_accept_stat(rqstp))
		return SVC_CLOSE;

	rqstp->rq_cred.cr_flavor = RPC_AUTH_UNIX;
	return SVC_OK;

badcred:
	rqstp->rq_auth_stat = rpc_autherr_badcred;
	return SVC_DENIED;
}

static int
svcauth_unix_release(struct svc_rqst *rqstp)
{
	/* Verifier (such as it is) is already in place.
	 */
	if (rqstp->rq_client)
		auth_domain_put(rqstp->rq_client);
	rqstp->rq_client = NULL;
	if (rqstp->rq_cred.cr_group_info)
		put_group_info(rqstp->rq_cred.cr_group_info);
	rqstp->rq_cred.cr_group_info = NULL;

	return 0;
}


struct auth_ops svcauth_unix = {
	.name		= "unix",
	.owner		= THIS_MODULE,
	.flavour	= RPC_AUTH_UNIX,
	.accept		= svcauth_unix_accept,
	.release	= svcauth_unix_release,
	.domain_release	= svcauth_unix_domain_release,
	.set_client	= svcauth_unix_set_client,
};

static const struct cache_detail ip_map_cache_template = {
	.owner		= THIS_MODULE,
	.hash_size	= IP_HASHMAX,
	.name		= "auth.unix.ip",
	.cache_put	= ip_map_put,
	.cache_upcall	= ip_map_upcall,
	.cache_request	= ip_map_request,
	.cache_parse	= ip_map_parse,
	.cache_show	= ip_map_show,
	.match		= ip_map_match,
	.init		= ip_map_init,
	.update		= update,
	.alloc		= ip_map_alloc,
};

int ip_map_cache_create(struct net *net)
{
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);
	struct cache_detail *cd;
	int err;

	cd = cache_create_net(&ip_map_cache_template, net);
	if (IS_ERR(cd))
		return PTR_ERR(cd);
	err = cache_register_net(cd, net);
	if (err) {
		cache_destroy_net(cd, net);
		return err;
	}
	sn->ip_map_cache = cd;
	return 0;
}

void ip_map_cache_destroy(struct net *net)
{
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);
	struct cache_detail *cd = sn->ip_map_cache;

	sn->ip_map_cache = NULL;
	cache_purge(cd);
	cache_unregister_net(cd, net);
	cache_destroy_net(cd, net);
}
