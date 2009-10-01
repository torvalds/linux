#include <linux/types.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/sunrpc/types.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/svcauth.h>
#include <linux/sunrpc/gss_api.h>
#include <linux/err.h>
#include <linux/seq_file.h>
#include <linux/hash.h>
#include <linux/string.h>
#include <net/sock.h>
#include <net/ipv6.h>
#include <linux/kernel.h>
#define RPCDBG_FACILITY	RPCDBG_AUTH


/*
 * AUTHUNIX and AUTHNULL credentials are both handled here.
 * AUTHNULL is treated just like AUTHUNIX except that the uid/gid
 * are always nobody (-2).  i.e. we do the same IP address checks for
 * AUTHNULL as for AUTHUNIX, and that is done here.
 */


struct unix_domain {
	struct auth_domain	h;
	int	addr_changes;
	/* other stuff later */
};

extern struct auth_ops svcauth_unix;

struct auth_domain *unix_domain_find(char *name)
{
	struct auth_domain *rv;
	struct unix_domain *new = NULL;

	rv = auth_domain_lookup(name, NULL);
	while(1) {
		if (rv) {
			if (new && rv != &new->h)
				auth_domain_put(&new->h);

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
		new->addr_changes = 0;
		rv = auth_domain_lookup(name, &new->h);
	}
}
EXPORT_SYMBOL_GPL(unix_domain_find);

static void svcauth_unix_domain_release(struct auth_domain *dom)
{
	struct unix_domain *ud = container_of(dom, struct unix_domain, h);

	kfree(dom->name);
	kfree(ud);
}


/**************************************************
 * cache for IP address to unix_domain
 * as needed by AUTH_UNIX
 */
#define	IP_HASHBITS	8
#define	IP_HASHMAX	(1<<IP_HASHBITS)
#define	IP_HASHMASK	(IP_HASHMAX-1)

struct ip_map {
	struct cache_head	h;
	char			m_class[8]; /* e.g. "nfsd" */
	struct in6_addr		m_addr;
	struct unix_domain	*m_client;
	int			m_add_change;
};
static struct cache_head	*ip_table[IP_HASHMAX];

static void ip_map_put(struct kref *kref)
{
	struct cache_head *item = container_of(kref, struct cache_head, ref);
	struct ip_map *im = container_of(item, struct ip_map,h);

	if (test_bit(CACHE_VALID, &item->flags) &&
	    !test_bit(CACHE_NEGATIVE, &item->flags))
		auth_domain_put(&im->m_client->h);
	kfree(im);
}

#if IP_HASHBITS == 8
/* hash_long on a 64 bit machine is currently REALLY BAD for
 * IP addresses in reverse-endian (i.e. on a little-endian machine).
 * So use a trivial but reliable hash instead
 */
static inline int hash_ip(__be32 ip)
{
	int hash = (__force u32)ip ^ ((__force u32)ip>>16);
	return (hash ^ (hash>>8)) & 0xff;
}
#endif
static inline int hash_ip6(struct in6_addr ip)
{
	return (hash_ip(ip.s6_addr32[0]) ^
		hash_ip(ip.s6_addr32[1]) ^
		hash_ip(ip.s6_addr32[2]) ^
		hash_ip(ip.s6_addr32[3]));
}
static int ip_map_match(struct cache_head *corig, struct cache_head *cnew)
{
	struct ip_map *orig = container_of(corig, struct ip_map, h);
	struct ip_map *new = container_of(cnew, struct ip_map, h);
	return strcmp(orig->m_class, new->m_class) == 0
		&& ipv6_addr_equal(&orig->m_addr, &new->m_addr);
}
static void ip_map_init(struct cache_head *cnew, struct cache_head *citem)
{
	struct ip_map *new = container_of(cnew, struct ip_map, h);
	struct ip_map *item = container_of(citem, struct ip_map, h);

	strcpy(new->m_class, item->m_class);
	ipv6_addr_copy(&new->m_addr, &item->m_addr);
}
static void update(struct cache_head *cnew, struct cache_head *citem)
{
	struct ip_map *new = container_of(cnew, struct ip_map, h);
	struct ip_map *item = container_of(citem, struct ip_map, h);

	kref_get(&item->m_client->h.ref);
	new->m_client = item->m_client;
	new->m_add_change = item->m_add_change;
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

static struct ip_map *ip_map_lookup(char *class, struct in6_addr *addr);
static int ip_map_update(struct ip_map *ipm, struct unix_domain *udom, time_t expiry);

static int ip_map_parse(struct cache_detail *cd,
			  char *mesg, int mlen)
{
	/* class ipaddress [domainname] */
	/* should be safe just to use the start of the input buffer
	 * for scratch: */
	char *buf = mesg;
	int len;
	int b1, b2, b3, b4, b5, b6, b7, b8;
	char c;
	char class[8];
	struct in6_addr addr;
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

	if (sscanf(buf, "%u.%u.%u.%u%c", &b1, &b2, &b3, &b4, &c) == 4) {
		addr.s6_addr32[0] = 0;
		addr.s6_addr32[1] = 0;
		addr.s6_addr32[2] = htonl(0xffff);
		addr.s6_addr32[3] =
			htonl((((((b1<<8)|b2)<<8)|b3)<<8)|b4);
       } else if (sscanf(buf, "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x%c",
			&b1, &b2, &b3, &b4, &b5, &b6, &b7, &b8, &c) == 8) {
		addr.s6_addr16[0] = htons(b1);
		addr.s6_addr16[1] = htons(b2);
		addr.s6_addr16[2] = htons(b3);
		addr.s6_addr16[3] = htons(b4);
		addr.s6_addr16[4] = htons(b5);
		addr.s6_addr16[5] = htons(b6);
		addr.s6_addr16[6] = htons(b7);
		addr.s6_addr16[7] = htons(b8);
       } else
		return -EINVAL;

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

	ipmp = ip_map_lookup(class, &addr);
	if (ipmp) {
		err = ip_map_update(ipmp,
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
	ipv6_addr_copy(&addr, &im->m_addr);

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


struct cache_detail ip_map_cache = {
	.owner		= THIS_MODULE,
	.hash_size	= IP_HASHMAX,
	.hash_table	= ip_table,
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

static struct ip_map *ip_map_lookup(char *class, struct in6_addr *addr)
{
	struct ip_map ip;
	struct cache_head *ch;

	strcpy(ip.m_class, class);
	ipv6_addr_copy(&ip.m_addr, addr);
	ch = sunrpc_cache_lookup(&ip_map_cache, &ip.h,
				 hash_str(class, IP_HASHBITS) ^
				 hash_ip6(*addr));

	if (ch)
		return container_of(ch, struct ip_map, h);
	else
		return NULL;
}

static int ip_map_update(struct ip_map *ipm, struct unix_domain *udom, time_t expiry)
{
	struct ip_map ip;
	struct cache_head *ch;

	ip.m_client = udom;
	ip.h.flags = 0;
	if (!udom)
		set_bit(CACHE_NEGATIVE, &ip.h.flags);
	else {
		ip.m_add_change = udom->addr_changes;
		/* if this is from the legacy set_client system call,
		 * we need m_add_change to be one higher
		 */
		if (expiry == NEVER)
			ip.m_add_change++;
	}
	ip.h.expiry_time = expiry;
	ch = sunrpc_cache_update(&ip_map_cache,
				 &ip.h, &ipm->h,
				 hash_str(ipm->m_class, IP_HASHBITS) ^
				 hash_ip6(ipm->m_addr));
	if (!ch)
		return -ENOMEM;
	cache_put(ch, &ip_map_cache);
	return 0;
}

int auth_unix_add_addr(struct in6_addr *addr, struct auth_domain *dom)
{
	struct unix_domain *udom;
	struct ip_map *ipmp;

	if (dom->flavour != &svcauth_unix)
		return -EINVAL;
	udom = container_of(dom, struct unix_domain, h);
	ipmp = ip_map_lookup("nfsd", addr);

	if (ipmp)
		return ip_map_update(ipmp, udom, NEVER);
	else
		return -ENOMEM;
}
EXPORT_SYMBOL_GPL(auth_unix_add_addr);

int auth_unix_forget_old(struct auth_domain *dom)
{
	struct unix_domain *udom;

	if (dom->flavour != &svcauth_unix)
		return -EINVAL;
	udom = container_of(dom, struct unix_domain, h);
	udom->addr_changes++;
	return 0;
}
EXPORT_SYMBOL_GPL(auth_unix_forget_old);

struct auth_domain *auth_unix_lookup(struct in6_addr *addr)
{
	struct ip_map *ipm;
	struct auth_domain *rv;

	ipm = ip_map_lookup("nfsd", addr);

	if (!ipm)
		return NULL;
	if (cache_check(&ip_map_cache, &ipm->h, NULL))
		return NULL;

	if ((ipm->m_client->addr_changes - ipm->m_add_change) >0) {
		if (test_and_set_bit(CACHE_NEGATIVE, &ipm->h.flags) == 0)
			auth_domain_put(&ipm->m_client->h);
		rv = NULL;
	} else {
		rv = &ipm->m_client->h;
		kref_get(&rv->ref);
	}
	cache_put(&ipm->h, &ip_map_cache);
	return rv;
}
EXPORT_SYMBOL_GPL(auth_unix_lookup);

void svcauth_unix_purge(void)
{
	cache_purge(&ip_map_cache);
}
EXPORT_SYMBOL_GPL(svcauth_unix_purge);

static inline struct ip_map *
ip_map_cached_get(struct svc_rqst *rqstp)
{
	struct ip_map *ipm = NULL;
	struct svc_xprt *xprt = rqstp->rq_xprt;

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
				xprt->xpt_auth_cache = NULL;
				spin_unlock(&xprt->xpt_lock);
				cache_put(&ipm->h, &ip_map_cache);
				return NULL;
			}
			cache_get(&ipm->h);
		}
		spin_unlock(&xprt->xpt_lock);
	}
	return ipm;
}

static inline void
ip_map_cached_put(struct svc_rqst *rqstp, struct ip_map *ipm)
{
	struct svc_xprt *xprt = rqstp->rq_xprt;

	if (test_bit(XPT_CACHE_AUTH, &xprt->xpt_flags)) {
		spin_lock(&xprt->xpt_lock);
		if (xprt->xpt_auth_cache == NULL) {
			/* newly cached, keep the reference */
			xprt->xpt_auth_cache = ipm;
			ipm = NULL;
		}
		spin_unlock(&xprt->xpt_lock);
	}
	if (ipm)
		cache_put(&ipm->h, &ip_map_cache);
}

void
svcauth_unix_info_release(void *info)
{
	struct ip_map *ipm = info;
	cache_put(&ipm->h, &ip_map_cache);
}

/****************************************************************************
 * auth.unix.gid cache
 * simple cache to map a UID to a list of GIDs
 * because AUTH_UNIX aka AUTH_SYS has a max of 16
 */
#define	GID_HASHBITS	8
#define	GID_HASHMAX	(1<<GID_HASHBITS)
#define	GID_HASHMASK	(GID_HASHMAX - 1)

struct unix_gid {
	struct cache_head	h;
	uid_t			uid;
	struct group_info	*gi;
};
static struct cache_head	*gid_table[GID_HASHMAX];

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

static struct unix_gid *unix_gid_lookup(uid_t uid);
extern struct cache_detail unix_gid_cache;

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

	if (mlen <= 0 || mesg[mlen-1] != '\n')
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
		rv = get_int(&mesg, &gid);
		err = -EINVAL;
		if (rv)
			goto out;
		GROUP_AT(ug.gi, i) = gid;
	}

	ugp = unix_gid_lookup(uid);
	if (ugp) {
		struct cache_head *ch;
		ug.h.flags = 0;
		ug.h.expiry_time = expiry;
		ch = sunrpc_cache_update(&unix_gid_cache,
					 &ug.h, &ugp->h,
					 hash_long(uid, GID_HASHBITS));
		if (!ch)
			err = -ENOMEM;
		else {
			err = 0;
			cache_put(ch, &unix_gid_cache);
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

	seq_printf(m, "%d %d:", ug->uid, glen);
	for (i = 0; i < glen; i++)
		seq_printf(m, " %d", GROUP_AT(ug->gi, i));
	seq_printf(m, "\n");
	return 0;
}

struct cache_detail unix_gid_cache = {
	.owner		= THIS_MODULE,
	.hash_size	= GID_HASHMAX,
	.hash_table	= gid_table,
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

static struct unix_gid *unix_gid_lookup(uid_t uid)
{
	struct unix_gid ug;
	struct cache_head *ch;

	ug.uid = uid;
	ch = sunrpc_cache_lookup(&unix_gid_cache, &ug.h,
				 hash_long(uid, GID_HASHBITS));
	if (ch)
		return container_of(ch, struct unix_gid, h);
	else
		return NULL;
}

static int unix_gid_find(uid_t uid, struct group_info **gip,
			 struct svc_rqst *rqstp)
{
	struct unix_gid *ug = unix_gid_lookup(uid);
	if (!ug)
		return -EAGAIN;
	switch (cache_check(&unix_gid_cache, &ug->h, &rqstp->rq_chandle)) {
	case -ENOENT:
		*gip = NULL;
		return 0;
	case 0:
		*gip = ug->gi;
		get_group_info(*gip);
		cache_put(&ug->h, &unix_gid_cache);
		return 0;
	default:
		return -EAGAIN;
	}
}

int
svcauth_unix_set_client(struct svc_rqst *rqstp)
{
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6, sin6_storage;
	struct ip_map *ipm;

	switch (rqstp->rq_addr.ss_family) {
	case AF_INET:
		sin = svc_addr_in(rqstp);
		sin6 = &sin6_storage;
		ipv6_addr_set(&sin6->sin6_addr, 0, 0,
				htonl(0x0000FFFF), sin->sin_addr.s_addr);
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

	ipm = ip_map_cached_get(rqstp);
	if (ipm == NULL)
		ipm = ip_map_lookup(rqstp->rq_server->sv_program->pg_class,
				    &sin6->sin6_addr);

	if (ipm == NULL)
		return SVC_DENIED;

	switch (cache_check(&ip_map_cache, &ipm->h, &rqstp->rq_chandle)) {
		default:
			BUG();
		case -EAGAIN:
		case -ETIMEDOUT:
			return SVC_DROP;
		case -ENOENT:
			return SVC_DENIED;
		case 0:
			rqstp->rq_client = &ipm->m_client->h;
			kref_get(&rqstp->rq_client->ref);
			ip_map_cached_put(rqstp, ipm);
			break;
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
		return SVC_DROP; /* kmalloc failure - client must retry */

	/* Put NULL verifier */
	svc_putnl(resv, RPC_AUTH_NULL);
	svc_putnl(resv, 0);

	rqstp->rq_flavor = RPC_AUTH_NULL;
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
	if (unix_gid_find(cred->cr_uid, &cred->cr_group_info, rqstp)
	    == -EAGAIN)
		return SVC_DROP;
	if (cred->cr_group_info == NULL) {
		cred->cr_group_info = groups_alloc(slen);
		if (cred->cr_group_info == NULL)
			return SVC_DROP;
		for (i = 0; i < slen; i++)
			GROUP_AT(cred->cr_group_info, i) = svc_getnl(argv);
	} else {
		for (i = 0; i < slen ; i++)
			svc_getnl(argv);
	}
	if (svc_getu32(argv) != htonl(RPC_AUTH_NULL) || svc_getu32(argv) != 0) {
		*authp = rpc_autherr_badverf;
		return SVC_DENIED;
	}

	/* Put NULL verifier */
	svc_putnl(resv, RPC_AUTH_NULL);
	svc_putnl(resv, 0);

	rqstp->rq_flavor = RPC_AUTH_UNIX;
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

