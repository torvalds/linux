#include <linux/types.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/sunrpc/types.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/svcauth.h>
#include <linux/err.h>
#include <linux/seq_file.h>
#include <linux/hash.h>
#include <linux/string.h>

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

struct auth_domain *unix_domain_find(char *name)
{
	struct auth_domain *rv, ud;
	struct unix_domain *new;

	ud.name = name;
	
	rv = auth_domain_lookup(&ud, 0);

 foundit:
	if (rv && rv->flavour != RPC_AUTH_UNIX) {
		auth_domain_put(rv);
		return NULL;
	}
	if (rv)
		return rv;

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (new == NULL)
		return NULL;
	cache_init(&new->h.h);
	new->h.name = kstrdup(name, GFP_KERNEL);
	new->h.flavour = RPC_AUTH_UNIX;
	new->addr_changes = 0;
	new->h.h.expiry_time = NEVER;

	rv = auth_domain_lookup(&new->h, 2);
	if (rv == &new->h) {
		if (atomic_dec_and_test(&new->h.h.refcnt)) BUG();
	} else {
		auth_domain_put(&new->h);
		goto foundit;
	}

	return rv;
}

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
	struct in_addr		m_addr;
	struct unix_domain	*m_client;
	int			m_add_change;
};
static struct cache_head	*ip_table[IP_HASHMAX];

static void ip_map_put(struct cache_head *item, struct cache_detail *cd)
{
	struct ip_map *im = container_of(item, struct ip_map,h);
	if (cache_put(item, cd)) {
		if (test_bit(CACHE_VALID, &item->flags) &&
		    !test_bit(CACHE_NEGATIVE, &item->flags))
			auth_domain_put(&im->m_client->h);
		kfree(im);
	}
}

static inline int ip_map_hash(struct ip_map *item)
{
	return hash_str(item->m_class, IP_HASHBITS) ^ 
		hash_long((unsigned long)item->m_addr.s_addr, IP_HASHBITS);
}
static inline int ip_map_match(struct ip_map *item, struct ip_map *tmp)
{
	return strcmp(tmp->m_class, item->m_class) == 0
		&& tmp->m_addr.s_addr == item->m_addr.s_addr;
}
static inline void ip_map_init(struct ip_map *new, struct ip_map *item)
{
	strcpy(new->m_class, item->m_class);
	new->m_addr.s_addr = item->m_addr.s_addr;
}
static inline void ip_map_update(struct ip_map *new, struct ip_map *item)
{
	cache_get(&item->m_client->h.h);
	new->m_client = item->m_client;
	new->m_add_change = item->m_add_change;
}

static void ip_map_request(struct cache_detail *cd,
				  struct cache_head *h,
				  char **bpp, int *blen)
{
	char text_addr[20];
	struct ip_map *im = container_of(h, struct ip_map, h);
	__u32 addr = im->m_addr.s_addr;
	
	snprintf(text_addr, 20, "%u.%u.%u.%u",
		 ntohl(addr) >> 24 & 0xff,
		 ntohl(addr) >> 16 & 0xff,
		 ntohl(addr) >>  8 & 0xff,
		 ntohl(addr) >>  0 & 0xff);

	qword_add(bpp, blen, im->m_class);
	qword_add(bpp, blen, text_addr);
	(*bpp)[-1] = '\n';
}

static struct ip_map *ip_map_lookup(struct ip_map *, int);

static int ip_map_parse(struct cache_detail *cd,
			  char *mesg, int mlen)
{
	/* class ipaddress [domainname] */
	/* should be safe just to use the start of the input buffer
	 * for scratch: */
	char *buf = mesg;
	int len;
	int b1,b2,b3,b4;
	char c;
	struct ip_map ipm, *ipmp;
	struct auth_domain *dom;
	time_t expiry;

	if (mesg[mlen-1] != '\n')
		return -EINVAL;
	mesg[mlen-1] = 0;

	/* class */
	len = qword_get(&mesg, ipm.m_class, sizeof(ipm.m_class));
	if (len <= 0) return -EINVAL;

	/* ip address */
	len = qword_get(&mesg, buf, mlen);
	if (len <= 0) return -EINVAL;

	if (sscanf(buf, "%u.%u.%u.%u%c", &b1, &b2, &b3, &b4, &c) != 4)
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

	ipm.m_addr.s_addr =
		htonl((((((b1<<8)|b2)<<8)|b3)<<8)|b4);
	ipm.h.flags = 0;
	if (dom) {
		ipm.m_client = container_of(dom, struct unix_domain, h);
		ipm.m_add_change = ipm.m_client->addr_changes;
	} else
		set_bit(CACHE_NEGATIVE, &ipm.h.flags);
	ipm.h.expiry_time = expiry;

	ipmp = ip_map_lookup(&ipm, 1);
	if (ipmp)
		ip_map_put(&ipmp->h, &ip_map_cache);
	if (dom)
		auth_domain_put(dom);
	if (!ipmp)
		return -ENOMEM;
	cache_flush();
	return 0;
}

static int ip_map_show(struct seq_file *m,
		       struct cache_detail *cd,
		       struct cache_head *h)
{
	struct ip_map *im;
	struct in_addr addr;
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

	seq_printf(m, "%s %d.%d.%d.%d %s\n",
		   im->m_class,
		   htonl(addr.s_addr) >> 24 & 0xff,
		   htonl(addr.s_addr) >> 16 & 0xff,
		   htonl(addr.s_addr) >>  8 & 0xff,
		   htonl(addr.s_addr) >>  0 & 0xff,
		   dom
		   );
	return 0;
}
	

struct cache_detail ip_map_cache = {
	.owner		= THIS_MODULE,
	.hash_size	= IP_HASHMAX,
	.hash_table	= ip_table,
	.name		= "auth.unix.ip",
	.cache_put	= ip_map_put,
	.cache_request	= ip_map_request,
	.cache_parse	= ip_map_parse,
	.cache_show	= ip_map_show,
};

static DefineSimpleCacheLookup(ip_map, 0)


int auth_unix_add_addr(struct in_addr addr, struct auth_domain *dom)
{
	struct unix_domain *udom;
	struct ip_map ip, *ipmp;

	if (dom->flavour != RPC_AUTH_UNIX)
		return -EINVAL;
	udom = container_of(dom, struct unix_domain, h);
	strcpy(ip.m_class, "nfsd");
	ip.m_addr = addr;
	ip.m_client = udom;
	ip.m_add_change = udom->addr_changes+1;
	ip.h.flags = 0;
	ip.h.expiry_time = NEVER;
	
	ipmp = ip_map_lookup(&ip, 1);

	if (ipmp) {
		ip_map_put(&ipmp->h, &ip_map_cache);
		return 0;
	} else
		return -ENOMEM;
}

int auth_unix_forget_old(struct auth_domain *dom)
{
	struct unix_domain *udom;
	
	if (dom->flavour != RPC_AUTH_UNIX)
		return -EINVAL;
	udom = container_of(dom, struct unix_domain, h);
	udom->addr_changes++;
	return 0;
}

struct auth_domain *auth_unix_lookup(struct in_addr addr)
{
	struct ip_map key, *ipm;
	struct auth_domain *rv;

	strcpy(key.m_class, "nfsd");
	key.m_addr = addr;

	ipm = ip_map_lookup(&key, 0);

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
		cache_get(&rv->h);
	}
	ip_map_put(&ipm->h, &ip_map_cache);
	return rv;
}

void svcauth_unix_purge(void)
{
	cache_purge(&ip_map_cache);
	cache_purge(&auth_domain_cache);
}

static int
svcauth_unix_set_client(struct svc_rqst *rqstp)
{
	struct ip_map key, *ipm;

	rqstp->rq_client = NULL;
	if (rqstp->rq_proc == 0)
		return SVC_OK;

	strcpy(key.m_class, rqstp->rq_server->sv_program->pg_class);
	key.m_addr = rqstp->rq_addr.sin_addr;

	ipm = ip_map_lookup(&key, 0);

	if (ipm == NULL)
		return SVC_DENIED;

	switch (cache_check(&ip_map_cache, &ipm->h, &rqstp->rq_chandle)) {
		default:
			BUG();
		case -EAGAIN:
			return SVC_DROP;
		case -ENOENT:
			return SVC_DENIED;
		case 0:
			rqstp->rq_client = &ipm->m_client->h;
			cache_get(&rqstp->rq_client->h);
			ip_map_put(&ipm->h, &ip_map_cache);
			break;
	}
	return SVC_OK;
}

static int
svcauth_null_accept(struct svc_rqst *rqstp, u32 *authp)
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
	if (svc_getu32(argv) != RPC_AUTH_NULL || svc_getu32(argv) != 0) {
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
	svc_putu32(resv, RPC_AUTH_NULL);
	svc_putu32(resv, 0);

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
svcauth_unix_accept(struct svc_rqst *rqstp, u32 *authp)
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
	slen = XDR_QUADLEN(ntohl(svc_getu32(argv)));	/* machname length */
	if (slen > 64 || (len -= (slen + 3)*4) < 0)
		goto badcred;
	argv->iov_base = (void*)((u32*)argv->iov_base + slen);	/* skip machname */
	argv->iov_len -= slen*4;

	cred->cr_uid = ntohl(svc_getu32(argv));		/* uid */
	cred->cr_gid = ntohl(svc_getu32(argv));		/* gid */
	slen = ntohl(svc_getu32(argv));			/* gids length */
	if (slen > 16 || (len -= (slen + 2)*4) < 0)
		goto badcred;
	cred->cr_group_info = groups_alloc(slen);
	if (cred->cr_group_info == NULL)
		return SVC_DROP;
	for (i = 0; i < slen; i++)
		GROUP_AT(cred->cr_group_info, i) = ntohl(svc_getu32(argv));

	if (svc_getu32(argv) != RPC_AUTH_NULL || svc_getu32(argv) != 0) {
		*authp = rpc_autherr_badverf;
		return SVC_DENIED;
	}

	/* Put NULL verifier */
	svc_putu32(resv, RPC_AUTH_NULL);
	svc_putu32(resv, 0);

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

