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

static void svcauth_unix_domain_release(struct auth_domain *dom)
{
	struct unix_domain *ud = container_of(dom, struct unix_domain, h);

	kfree(dom->name);
	kfree(ud);
}

struct auth_domain *unix_domain_find(char *name)
{
	struct auth_domain *rv;
	struct unix_domain *new = NULL;

	rv = auth_domain_lookup(name, NULL);
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
};

static void ip_map_put(struct kref *kref)
{
	struct cache_head *item = container_of(kref, struct cache_head, ref);
	struct ip_map *im = container_of(item, struct ip_map,h);

	if (test_bit(CACHE_VALID, &item->flags) &&
	    !test_bit(CACHE_NEGATIVE, &item->flags))
		auth_domain_put(&im->m_client->h);
	kfree(im);
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

static int ip_map_upcall(struct cache_detail *cd, struct cache_head *h)
{
	return sunrpc_cache_pipe_upcall(cd, h, ip_map_request);
}

static struct ip_map *__ip_map_lookup(struct cache_detail *cd, char *class, struct in6_addr *addr);
static int __ip_map_update(struct cache_detail *cd, struct ip_map *ipm, struct unix_domain *udom, time_t expiry);

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
	time_t expiry;

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

	expiry = get_expiry(&mesg);
	if (expiry ==0)
		return -EINVAL;

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
	ch = sunrpc_cache_lookup(cd, &ip.h,
				 hash_str(class, IP_HASHBITS) ^
				 hash_ip6(addr));

	if (ch)
		return container_of(ch, struct ip_map, h);
	else
		return NULL;
}

static inline struct ip_map *ip_map_lookup(struct net *net, char *class,
		struct in6_addr *addr)
{
	struct sunrpc_net *sn;

	sn = net_generic(net, sunrpc_net_id);
	return __ip_map_lookup(sn->ip_map_cache, class, addr);
}

static int __ip_map_update(struct cache_detail *cd, struct ip_map *ipm,
		struct unix_domain *udom, time_t expiry)
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

static inline int ip_map_update(struct net *net, struct ip_map *ipm,
		struct unix_domain *udom, time_t expiry)
{
	struct sunrpc_net *sn;

	sn = net_generic(net, sunrpc_net_id);
	return __ip_map_update(sn->ip_map_cache, ipm, udom, expiry);
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
			if (!cache_valid(&ipm->h)) {
				/*
				 * The entry has been invalidated since it was
				 * remembered, e.g. by a second mount from the
				 * same IP address.
				 */
				sn = net_generic(xprt->xpt_net, sunrpc_net_id);
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
 * because AUTH_UNIX aka AUTH_SYS has a max of 16
 */
#define	GID_HASHBITS	8
#define	GID_HASHMAX	(1<<GID_HASHBITS)

struct unix_gid {
	struct cache_head	h;
	uid_t			uid;
	struct group_info	*gi;
};

static void unix_gid_put(struct kref *kref)
{
	struct cache_head *item = container_of(kref, struct cache_head, ref);
	struct unix_gid *ug = container_of(item, struct unix_gid, h);
	if (test_bit(CACHE_VALID, &item->flags) &&
	    !test_bit(CACHE_NEGATIVE, &item->flags))
		put_group_info(ug->gi);
	kfree(ug);
}

static int unix_gid_match(struct cache_head *corig, struct cache_head *cnew)
{
	struct unix_gid *orig = container_of(corig, struct unix_gid, h);
	struct unix_gid *new = container_of(cnew, struct unix_gid, h);
	return orig->uid == new->uid;
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

static void unix_gid_request(struct cache_detail *cd,
			     struct cache_head *h,
			     char **bpp, int *blen)
{
	char tuid[20];
	struct unix_gid *ug = container_of(h, struct unix_gid, h);

	snprintf(tuid, 20, "%u", ug->uid);
	qword_add(bpp, blen, tuid);
	(*bpp)[-1] = '\n';
}

static int unix_gid_upcall(struct cache_detail *cd, struct cache_head *h)
{
	return sunrpc_cache_pipe_upcall(cd, h, unix_gid_request);
}

static struct unix_gid *unix_gid_lookup(struct cache_detail *cd, uid_t uid);

static int unix_gid_parse(struct cache_detail *cd,
			char *mesg, int mlen)
{
	/* uid expiry Ngid gid0 gid1 ... gidN-1 */
	int uid;
	int gids;
	int rv;
	int i;
	int err;
	time_t expiry;
	struct unix_gid ug, *ugp;

	if (mesg[mlen - 1] != '\n')
		return -EINVAL;
	mesg[mlen-1] = 0;

	rv = get_int(&mesg, &uid);
	if (rv)
		return -EINVAL;
	ug.uid = uid;

	expiry = get_expiry(&mesg);
	if (expiry == 0)
		return -EINVAL;

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
		kgid = make_kgid(&init_user_ns, gid);
		if (!gid_valid(kgid))
			goto out;
		GROUP_AT(ug.gi, i) = kgid;
	}

	ugp = unix_gid_lookup(cd, uid);
	if (ugp) {
		struct cache_head *ch;
		ug.h.flags = 0;
		ug.h.expiry_time = expiry;
		ch = sunrpc_cache_update(cd,
					 &ug.h, &ugp->h,
					 hash_long(uid, GID_HASHBITS));
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
	struct user_namespace *user_ns = current_user_ns();
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

	seq_printf(m, "%u %d:", ug->uid, glen);
	for (i = 0; i < glen; i++)
		seq_printf(m, " %d", from_kgid_munged(user_ns, GROUP_AT(ug->gi, i)));
	seq_printf(m, "\n");
	return 0;
}

static struct cache_detail unix_gid_cache_template = {
	.owner		= THIS_MODULE,
	.hash_size	= GID_HASHMAX,
	.name		= "auth.unix.gid",
	.cache_put	= unix_gid_put,
	.cache_upcall	= unix_gid_upcall,
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

static struct unix_gid *unix_gid_lookup(struct cache_detail *cd, uid_t uid)
{
	struct unix_gid ug;
	struct cache_head *ch;

	ug.uid = uid;
	ch = sunrpc_cache_lookup(cd, &ug.h, hash_long(uid, GID_HASHBITS));
	if (ch)
		return container_of(ch, struct unix_gid, h);
	else
		return NULL;
}

static struct group_info *unix_gid_find(uid_t uid, struct svc_rqst *rqstp)
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

int
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
		return SVC_OK;

	ipm = ip_map_cached_get(xprt);
	if (ipm == NULL)
		ipm = __ip_map_lookup(sn->ip_map_cache, rqstp->rq_server->sv_program->pg_class,
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
	return SVC_OK;
}

EXPORT_SYMBOL_GPL(svcauth_unix_set_client);

static int
svcauth_null_accept(struct svc_rqst *rqstp, __be32 *authp)
{
	struct kvec	*argv = &rqstp->rq_arg.head[0];
	struct kvec	*resv = &rqstp->rq_res.head[0];
	struct svc_cred	*cred = &rqstp->rq_cred;

	cred->cr_group_info = NULL;
	cred->cr_principal = NULL;
	rqstp->rq_client = NULL;

	if (argv->iov_len < 3*4)
		return SVC_GARBAGE;

	if (svc_getu32(argv) != 0) {
		dprintk("svc: bad null cred\n");
		*authp = rpc_autherr_badcred;
		return SVC_DENIED;
	}
	if (svc_getu32(argv) != htonl(RPC_AUTH_NULL) || svc_getu32(argv) != 0) {
		dprintk("svc: bad null verf\n");
		*authp = rpc_autherr_badverf;
		return SVC_DENIED;
	}

	/* Signal that mapping to nobody uid/gid is required */
	cred->cr_uid = (uid_t) -1;
	cred->cr_gid = (gid_t) -1;
	cred->cr_group_info = groups_alloc(0);
	if (cred->cr_group_info == NULL)
		return SVC_CLOSE; /* kmalloc failure - client must retry */

	/* Put NULL verifier */
	svc_putnl(resv, RPC_AUTH_NULL);
	svc_putnl(resv, 0);

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
	.accept 	= svcauth_null_accept,
	.release	= svcauth_null_release,
	.set_client	= svcauth_unix_set_client,
};


static int
svcauth_unix_accept(struct svc_rqst *rqstp, __be32 *authp)
{
	struct kvec	*argv = &rqstp->rq_arg.head[0];
	struct kvec	*resv = &rqstp->rq_res.head[0];
	struct svc_cred	*cred = &rqstp->rq_cred;
	u32		slen, i;
	int		len   = argv->iov_len;

	cred->cr_group_info = NULL;
	cred->cr_principal = NULL;
	rqstp->rq_client = NULL;

	if ((len -= 3*4) < 0)
		return SVC_GARBAGE;

	svc_getu32(argv);			/* length */
	svc_getu32(argv);			/* time stamp */
	slen = XDR_QUADLEN(svc_getnl(argv));	/* machname length */
	if (slen > 64 || (len -= (slen + 3)*4) < 0)
		goto badcred;
	argv->iov_base = (void*)((__be32*)argv->iov_base + slen);	/* skip machname */
	argv->iov_len -= slen*4;

	cred->cr_uid = svc_getnl(argv);		/* uid */
	cred->cr_gid = svc_getnl(argv);		/* gid */
	slen = svc_getnl(argv);			/* gids length */
	if (slen > 16 || (len -= (slen + 2)*4) < 0)
		goto badcred;
	cred->cr_group_info = groups_alloc(slen);
	if (cred->cr_group_info == NULL)
		return SVC_CLOSE;
	for (i = 0; i < slen; i++) {
		kgid_t kgid = make_kgid(&init_user_ns, svc_getnl(argv));
		if (!gid_valid(kgid))
			goto badcred;
		GROUP_AT(cred->cr_group_info, i) = kgid;
	}
	if (svc_getu32(argv) != htonl(RPC_AUTH_NULL) || svc_getu32(argv) != 0) {
		*authp = rpc_autherr_badverf;
		return SVC_DENIED;
	}

	/* Put NULL verifier */
	svc_putnl(resv, RPC_AUTH_NULL);
	svc_putnl(resv, 0);

	rqstp->rq_cred.cr_flavor = RPC_AUTH_UNIX;
	return SVC_OK;

badcred:
	*authp = rpc_autherr_badcred;
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
	.accept 	= svcauth_unix_accept,
	.release	= svcauth_unix_release,
	.domain_release	= svcauth_unix_domain_release,
	.set_client	= svcauth_unix_set_client,
};

static struct cache_detail ip_map_cache_template = {
	.owner		= THIS_MODULE,
	.hash_size	= IP_HASHMAX,
	.name		= "auth.unix.ip",
	.cache_put	= ip_map_put,
	.cache_upcall	= ip_map_upcall,
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
