// SPDX-License-Identifier: GPL-2.0
/*
 * NFS exporting and validation.
 *
 * We maintain a list of clients, each of which has a list of
 * exports. To export an fs to a given client, you first have
 * to create the client entry with NFSCTL_ADDCLIENT, which
 * creates a client control block and adds it to the hash
 * table. Then, you call NFSCTL_EXPORT for each fs.
 *
 *
 * Copyright (C) 1995, 1996 Olaf Kirch, <okir@monad.swb.de>
 */

#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/module.h>
#include <linux/exportfs.h>
#include <linux/sunrpc/svc_xprt.h>
#include <net/genetlink.h>
#include <uapi/linux/nfsd_netlink.h>

#include "nfsd.h"
#include "nfsfh.h"
#include "netns.h"
#include "pnfs.h"
#include "filecache.h"
#include "trace.h"
#include "netlink.h"

#define NFSDDBG_FACILITY	NFSDDBG_EXPORT

/*
 * We have two caches.
 * One maps client+vfsmnt+dentry to export options - the export map
 * The other maps client+filehandle-fragment to export options. - the expkey map
 *
 * The export options are actually stored in the first map, and the
 * second map contains a reference to the entry in the first map.
 */

#define	EXPKEY_HASHBITS		8
#define	EXPKEY_HASHMAX		(1 << EXPKEY_HASHBITS)
#define	EXPKEY_HASHMASK		(EXPKEY_HASHMAX -1)

static void expkey_put(struct kref *ref)
{
	struct svc_expkey *key = container_of(ref, struct svc_expkey, h.ref);

	if (test_bit(CACHE_VALID, &key->h.flags) &&
	    !test_bit(CACHE_NEGATIVE, &key->h.flags))
		path_put(&key->ek_path);
	auth_domain_put(key->ek_client);
	kfree_rcu(key, ek_rcu);
}

static int expkey_upcall(struct cache_detail *cd, struct cache_head *h)
{
	return sunrpc_cache_upcall(cd, h);
}

static void expkey_request(struct cache_detail *cd,
			   struct cache_head *h,
			   char **bpp, int *blen)
{
	/* client fsidtype \xfsid */
	struct svc_expkey *ek = container_of(h, struct svc_expkey, h);
	char type[5];

	qword_add(bpp, blen, ek->ek_client->name);
	snprintf(type, 5, "%d", ek->ek_fsidtype);
	qword_add(bpp, blen, type);
	qword_addhex(bpp, blen, (char*)ek->ek_fsid, key_len(ek->ek_fsidtype));
	(*bpp)[-1] = '\n';
}

static struct svc_expkey *svc_expkey_update(struct cache_detail *cd, struct svc_expkey *new,
					    struct svc_expkey *old);
static struct svc_expkey *svc_expkey_lookup(struct cache_detail *cd, struct svc_expkey *);

static int expkey_parse(struct cache_detail *cd, char *mesg, int mlen)
{
	/* client fsidtype fsid expiry [path] */
	char *buf;
	int len;
	struct auth_domain *dom = NULL;
	int err;
	u8 fsidtype;
	struct svc_expkey key;
	struct svc_expkey *ek = NULL;

	if (mesg[mlen - 1] != '\n')
		return -EINVAL;
	mesg[mlen-1] = 0;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	err = -ENOMEM;
	if (!buf)
		goto out;

	err = -EINVAL;
	if (qword_get(&mesg, buf, PAGE_SIZE) <= 0)
		goto out;

	err = -ENOENT;
	dom = auth_domain_find(buf);
	if (!dom)
		goto out;
	dprintk("found domain %s\n", buf);

	err = -EINVAL;
	if (qword_get(&mesg, buf, PAGE_SIZE) <= 0)
		goto out;
	if (kstrtou8(buf, 10, &fsidtype))
		goto out;
	dprintk("found fsidtype %u\n", fsidtype);
	if (key_len(fsidtype)==0) /* invalid type */
		goto out;
	if ((len=qword_get(&mesg, buf, PAGE_SIZE)) <= 0)
		goto out;
	dprintk("found fsid length %d\n", len);
	if (len != key_len(fsidtype))
		goto out;

	/* OK, we seem to have a valid key */
	key.h.flags = 0;
	err = get_expiry(&mesg, &key.h.expiry_time);
	if (err)
		goto out;

	key.ek_client = dom;
	key.ek_fsidtype = fsidtype;
	memcpy(key.ek_fsid, buf, len);

	ek = svc_expkey_lookup(cd, &key);
	err = -ENOMEM;
	if (!ek)
		goto out;

	/* now we want a pathname, or empty meaning NEGATIVE  */
	err = -EINVAL;
	len = qword_get(&mesg, buf, PAGE_SIZE);
	if (len < 0)
		goto out;
	dprintk("Path seems to be <%s>\n", buf);
	err = 0;
	if (len == 0) {
		set_bit(CACHE_NEGATIVE, &key.h.flags);
		ek = svc_expkey_update(cd, &key, ek);
		if (ek)
			trace_nfsd_expkey_update(ek, NULL);
		else
			err = -ENOMEM;
	} else {
		err = kern_path(buf, 0, &key.ek_path);
		if (err)
			goto out;

		dprintk("Found the path %s\n", buf);

		ek = svc_expkey_update(cd, &key, ek);
		if (ek)
			trace_nfsd_expkey_update(ek, buf);
		else
			err = -ENOMEM;
		path_put(&key.ek_path);
	}
	cache_flush();
 out:
	if (ek)
		cache_put(&ek->h, cd);
	if (dom)
		auth_domain_put(dom);
	kfree(buf);
	return err;
}

static int expkey_show(struct seq_file *m,
		       struct cache_detail *cd,
		       struct cache_head *h)
{
	struct svc_expkey *ek ;
	int i;

	if (h ==NULL) {
		seq_puts(m, "#domain fsidtype fsid [path]\n");
		return 0;
	}
	ek = container_of(h, struct svc_expkey, h);
	seq_printf(m, "%s %d 0x", ek->ek_client->name,
		   ek->ek_fsidtype);
	for (i=0; i < key_len(ek->ek_fsidtype)/4; i++)
		seq_printf(m, "%08x", ek->ek_fsid[i]);
	if (test_bit(CACHE_VALID, &h->flags) && 
	    !test_bit(CACHE_NEGATIVE, &h->flags)) {
		seq_printf(m, " ");
		seq_path(m, &ek->ek_path, "\\ \t\n");
	}
	seq_printf(m, "\n");
	return 0;
}

static inline int expkey_match (struct cache_head *a, struct cache_head *b)
{
	struct svc_expkey *orig = container_of(a, struct svc_expkey, h);
	struct svc_expkey *new = container_of(b, struct svc_expkey, h);

	if (orig->ek_fsidtype != new->ek_fsidtype ||
	    orig->ek_client != new->ek_client ||
	    memcmp(orig->ek_fsid, new->ek_fsid, key_len(orig->ek_fsidtype)) != 0)
		return 0;
	return 1;
}

static inline void expkey_init(struct cache_head *cnew,
				   struct cache_head *citem)
{
	struct svc_expkey *new = container_of(cnew, struct svc_expkey, h);
	struct svc_expkey *item = container_of(citem, struct svc_expkey, h);

	kref_get(&item->ek_client->ref);
	new->ek_client = item->ek_client;
	new->ek_fsidtype = item->ek_fsidtype;

	memcpy(new->ek_fsid, item->ek_fsid, sizeof(new->ek_fsid));
}

static inline void expkey_update(struct cache_head *cnew,
				   struct cache_head *citem)
{
	struct svc_expkey *new = container_of(cnew, struct svc_expkey, h);
	struct svc_expkey *item = container_of(citem, struct svc_expkey, h);

	new->ek_path = item->ek_path;
	path_get(&item->ek_path);
}

static struct cache_head *expkey_alloc(void)
{
	struct svc_expkey *i = kmalloc_obj(*i);
	if (i)
		return &i->h;
	else
		return NULL;
}

static void expkey_flush(void)
{
	/*
	 * Take the nfsd_mutex here to ensure that the file cache is not
	 * destroyed while we're in the middle of flushing.
	 */
	mutex_lock(&nfsd_mutex);
	nfsd_file_cache_purge(current->nsproxy->net_ns);
	mutex_unlock(&nfsd_mutex);
}

static int expkey_notify(struct cache_detail *cd, struct cache_head *h)
{
	return nfsd_cache_notify(cd, h, NFSD_CACHE_TYPE_EXPKEY);
}

static const struct cache_detail svc_expkey_cache_template = {
	.owner		= THIS_MODULE,
	.hash_size	= EXPKEY_HASHMAX,
	.name		= "nfsd.fh",
	.cache_put	= expkey_put,
	.cache_upcall	= expkey_upcall,
	.cache_notify	= expkey_notify,
	.cache_request	= expkey_request,
	.cache_parse	= expkey_parse,
	.cache_show	= expkey_show,
	.match		= expkey_match,
	.init		= expkey_init,
	.update       	= expkey_update,
	.alloc		= expkey_alloc,
	.flush		= expkey_flush,
};

static int
svc_expkey_hash(struct svc_expkey *item)
{
	int hash = item->ek_fsidtype;
	char * cp = (char*)item->ek_fsid;
	int len = key_len(item->ek_fsidtype);

	hash ^= hash_mem(cp, len, EXPKEY_HASHBITS);
	hash ^= hash_ptr(item->ek_client, EXPKEY_HASHBITS);
	hash &= EXPKEY_HASHMASK;
	return hash;
}

static struct svc_expkey *
svc_expkey_lookup(struct cache_detail *cd, struct svc_expkey *item)
{
	struct cache_head *ch;
	int hash = svc_expkey_hash(item);

	ch = sunrpc_cache_lookup_rcu(cd, &item->h, hash);
	if (ch)
		return container_of(ch, struct svc_expkey, h);
	else
		return NULL;
}

static struct svc_expkey *
svc_expkey_update(struct cache_detail *cd, struct svc_expkey *new,
		  struct svc_expkey *old)
{
	struct cache_head *ch;
	int hash = svc_expkey_hash(new);

	ch = sunrpc_cache_update(cd, &new->h, &old->h, hash);
	if (ch)
		return container_of(ch, struct svc_expkey, h);
	else
		return NULL;
}

/**
 * nfsd_nl_expkey_get_reqs_dumpit - dump pending expkey requests
 * @skb: reply buffer
 * @cb: netlink metadata and command arguments
 *
 * Walk the expkey cache's pending request list and create a netlink
 * message with a nested entry for each cache_request, containing the
 * seqno, client string, fsidtype and fsid.
 *
 * Uses cb->args[0] as a seqno cursor for dump continuation across
 * multiple netlink messages.
 *
 * Returns the size of the reply or a negative errno.
 */
int nfsd_nl_expkey_get_reqs_dumpit(struct sk_buff *skb,
				   struct netlink_callback *cb)
{
	struct nfsd_net *nn;
	struct cache_detail *cd;
	struct cache_head **items;
	u64 *seqnos;
	int cnt, i, emitted;
	void *hdr;
	int ret;

	nn = net_generic(sock_net(skb->sk), nfsd_net_id);

	mutex_lock(&nfsd_mutex);

	cd = nn->svc_expkey_cache;
	if (!cd) {
		ret = -ENODEV;
		goto out_unlock;
	}

	cnt = sunrpc_cache_requests_count(cd);
	if (!cnt) {
		ret = 0;
		goto out_unlock;
	}

	items = kcalloc(cnt, sizeof(*items), GFP_KERNEL);
	seqnos = kcalloc(cnt, sizeof(*seqnos), GFP_KERNEL);
	if (!items || !seqnos) {
		ret = -ENOMEM;
		goto out_alloc;
	}

	cnt = sunrpc_cache_requests_snapshot(cd, items, seqnos, cnt,
					     cb->args[0]);
	if (!cnt) {
		ret = 0;
		goto out_alloc;
	}

	hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid,
			  cb->nlh->nlmsg_seq, &nfsd_nl_family,
			  NLM_F_MULTI, NFSD_CMD_EXPKEY_GET_REQS);
	if (!hdr) {
		ret = -ENOBUFS;
		goto out_put;
	}

	emitted = 0;
	for (i = 0; i < cnt; i++) {
		struct svc_expkey *ek;
		struct nlattr *nest;

		ek = container_of(items[i], struct svc_expkey, h);

		nest = nla_nest_start(skb, NFSD_A_EXPKEY_REQS_REQUESTS);
		if (!nest)
			break;

		if (nla_put_u64_64bit(skb, NFSD_A_EXPKEY_SEQNO,
				      seqnos[i], 0) ||
		    nla_put_string(skb, NFSD_A_EXPKEY_CLIENT,
				   ek->ek_client->name) ||
		    nla_put_u8(skb, NFSD_A_EXPKEY_FSIDTYPE,
			       ek->ek_fsidtype) ||
		    nla_put(skb, NFSD_A_EXPKEY_FSID,
			    key_len(ek->ek_fsidtype), ek->ek_fsid)) {
			nla_nest_cancel(skb, nest);
			break;
		}

		nla_nest_end(skb, nest);
		cb->args[0] = seqnos[i];
		emitted++;
	}

	if (!emitted) {
		genlmsg_cancel(skb, hdr);
		ret = -EMSGSIZE;
		goto out_put;
	}

	genlmsg_end(skb, hdr);
	ret = skb->len;
out_put:
	for (i = 0; i < cnt; i++)
		cache_put(items[i], cd);
out_alloc:
	kfree(seqnos);
	kfree(items);
out_unlock:
	mutex_unlock(&nfsd_mutex);
	return ret;
}

/**
 * nfsd_nl_parse_one_expkey - parse one expkey entry from netlink
 * @cd: cache_detail for the expkey cache
 * @attr: nested attribute containing expkey fields
 *
 * Parses one expkey entry from a netlink message and updates the
 * cache. Mirrors the logic in expkey_parse().
 *
 * Returns 0 on success or a negative errno.
 */
static int nfsd_nl_parse_one_expkey(struct cache_detail *cd,
				    struct nlattr *attr)
{
	struct nlattr *tb[NFSD_A_EXPKEY_PATH + 1];
	struct auth_domain *dom = NULL;
	struct svc_expkey key;
	struct svc_expkey *ek = NULL;
	struct timespec64 boot;
	int err;
	u8 fsidtype;
	int fsid_len;

	err = nla_parse_nested(tb, NFSD_A_EXPKEY_PATH, attr,
			       nfsd_expkey_nl_policy, NULL);
	if (err)
		return err;

	/* client (required) */
	if (!tb[NFSD_A_EXPKEY_CLIENT])
		return -EINVAL;

	dom = auth_domain_find(nla_data(tb[NFSD_A_EXPKEY_CLIENT]));
	if (!dom)
		return -ENOENT;

	/* fsidtype (required) */
	if (!tb[NFSD_A_EXPKEY_FSIDTYPE]) {
		err = -EINVAL;
		goto out_dom;
	}
	fsidtype = nla_get_u8(tb[NFSD_A_EXPKEY_FSIDTYPE]);
	if (key_len(fsidtype) == 0) {
		err = -EINVAL;
		goto out_dom;
	}

	/* fsid (required) */
	if (!tb[NFSD_A_EXPKEY_FSID]) {
		err = -EINVAL;
		goto out_dom;
	}
	fsid_len = nla_len(tb[NFSD_A_EXPKEY_FSID]);
	if (fsid_len != key_len(fsidtype)) {
		err = -EINVAL;
		goto out_dom;
	}

	/* expiry (required, wallclock seconds) */
	if (!tb[NFSD_A_EXPKEY_EXPIRY]) {
		err = -EINVAL;
		goto out_dom;
	}

	key.h.flags = 0;
	getboottime64(&boot);
	key.h.expiry_time = nla_get_u64(tb[NFSD_A_EXPKEY_EXPIRY]) -
			    boot.tv_sec;
	key.ek_client = dom;
	key.ek_fsidtype = fsidtype;
	memcpy(key.ek_fsid, nla_data(tb[NFSD_A_EXPKEY_FSID]), fsid_len);

	ek = svc_expkey_lookup(cd, &key);
	if (!ek) {
		err = -ENOMEM;
		goto out_dom;
	}

	if (tb[NFSD_A_EXPKEY_NEGATIVE]) {
		set_bit(CACHE_NEGATIVE, &key.h.flags);
		ek = svc_expkey_update(cd, &key, ek);
		if (ek)
			trace_nfsd_expkey_update(ek, NULL);
		else
			err = -ENOMEM;
	} else if (tb[NFSD_A_EXPKEY_PATH]) {
		err = kern_path(nla_data(tb[NFSD_A_EXPKEY_PATH]), 0,
				&key.ek_path);
		if (err)
			goto out_ek;
		ek = svc_expkey_update(cd, &key, ek);
		if (ek)
			trace_nfsd_expkey_update(ek,
					nla_data(tb[NFSD_A_EXPKEY_PATH]));
		else
			err = -ENOMEM;
		path_put(&key.ek_path);
	} else {
		err = -EINVAL;
		goto out_ek;
	}

	cache_flush();

out_ek:
	if (ek)
		cache_put(&ek->h, cd);
out_dom:
	auth_domain_put(dom);
	return err;
}

/**
 * nfsd_nl_expkey_set_reqs_doit - respond to expkey requests
 * @skb: reply buffer
 * @info: netlink metadata and command arguments
 *
 * Parse one or more expkey cache responses from userspace and
 * update the expkey cache accordingly.
 *
 * Returns 0 on success or a negative errno.
 */
int nfsd_nl_expkey_set_reqs_doit(struct sk_buff *skb,
				 struct genl_info *info)
{
	struct nfsd_net *nn;
	struct cache_detail *cd;
	const struct nlattr *attr;
	int rem, ret = 0;

	nn = net_generic(genl_info_net(info), nfsd_net_id);

	mutex_lock(&nfsd_mutex);

	cd = nn->svc_expkey_cache;
	if (!cd) {
		ret = -ENODEV;
		goto out_unlock;
	}

	nlmsg_for_each_attr_type(attr, NFSD_A_EXPKEY_REQS_REQUESTS,
				 info->nlhdr, GENL_HDRLEN, rem) {
		ret = nfsd_nl_parse_one_expkey(cd, (struct nlattr *)attr);
		if (ret)
			break;
	}

out_unlock:
	mutex_unlock(&nfsd_mutex);
	return ret;
}

#define	EXPORT_HASHBITS		8
#define	EXPORT_HASHMAX		(1<< EXPORT_HASHBITS)

static void nfsd4_fslocs_free(struct nfsd4_fs_locations *fsloc)
{
	struct nfsd4_fs_location *locations = fsloc->locations;
	int i;

	if (!locations)
		return;

	for (i = 0; i < fsloc->locations_count; i++) {
		kfree(locations[i].path);
		kfree(locations[i].hosts);
	}

	kfree(locations);
	fsloc->locations = NULL;
}

static int export_stats_init(struct export_stats *stats)
{
	stats->start_time = ktime_get_seconds();
	return percpu_counter_init_many(stats->counter, 0, GFP_KERNEL,
					EXP_STATS_COUNTERS_NUM);
}

static void export_stats_reset(struct export_stats *stats)
{
	if (stats) {
		int i;

		for (i = 0; i < EXP_STATS_COUNTERS_NUM; i++)
			percpu_counter_set(&stats->counter[i], 0);
	}
}

static void export_stats_destroy(struct export_stats *stats)
{
	if (stats)
		percpu_counter_destroy_many(stats->counter,
					    EXP_STATS_COUNTERS_NUM);
}

static void svc_export_release(struct rcu_head *rcu_head)
{
	struct svc_export *exp = container_of(rcu_head, struct svc_export,
			ex_rcu);

	nfsd4_fslocs_free(&exp->ex_fslocs);
	export_stats_destroy(exp->ex_stats);
	kfree(exp->ex_stats);
	kfree(exp->ex_uuid);
	kfree(exp);
}

static void svc_export_put(struct kref *ref)
{
	struct svc_export *exp = container_of(ref, struct svc_export, h.ref);

	path_put(&exp->ex_path);
	auth_domain_put(exp->ex_client);
	call_rcu(&exp->ex_rcu, svc_export_release);
}

/**
 * nfsd_nl_svc_export_get_reqs_dumpit - dump pending svc_export requests
 * @skb: reply buffer
 * @cb: netlink metadata and command arguments
 *
 * Walk the svc_export cache's pending request list and create a netlink
 * message with a nested entry for each cache_request, containing the
 * seqno, client string, and path.
 *
 * Uses cb->args[0] as a seqno cursor for dump continuation across
 * multiple netlink messages.
 *
 * Returns the size of the reply or a negative errno.
 */
int nfsd_nl_svc_export_get_reqs_dumpit(struct sk_buff *skb,
				       struct netlink_callback *cb)
{
	struct nfsd_net *nn;
	struct cache_detail *cd;
	struct cache_head **items;
	u64 *seqnos;
	int cnt, i, emitted;
	char *pathbuf;
	void *hdr;
	int ret;

	nn = net_generic(sock_net(skb->sk), nfsd_net_id);

	mutex_lock(&nfsd_mutex);

	cd = nn->svc_export_cache;
	if (!cd) {
		ret = -ENODEV;
		goto out_unlock;
	}

	cnt = sunrpc_cache_requests_count(cd);
	if (!cnt) {
		ret = 0;
		goto out_unlock;
	}

	items = kcalloc(cnt, sizeof(*items), GFP_KERNEL);
	seqnos = kcalloc(cnt, sizeof(*seqnos), GFP_KERNEL);
	pathbuf = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!items || !seqnos || !pathbuf) {
		ret = -ENOMEM;
		goto out_alloc;
	}

	cnt = sunrpc_cache_requests_snapshot(cd, items, seqnos, cnt,
					     cb->args[0]);
	if (!cnt) {
		ret = 0;
		goto out_alloc;
	}

	hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid,
			  cb->nlh->nlmsg_seq, &nfsd_nl_family,
			  NLM_F_MULTI, NFSD_CMD_SVC_EXPORT_GET_REQS);
	if (!hdr) {
		ret = -ENOBUFS;
		goto out_put;
	}

	emitted = 0;
	for (i = 0; i < cnt; i++) {
		struct svc_export *exp;
		struct nlattr *nest;
		char *pth;

		exp = container_of(items[i], struct svc_export, h);

		pth = d_path(&exp->ex_path, pathbuf, PATH_MAX);
		if (IS_ERR(pth))
			continue;

		nest = nla_nest_start(skb,
				      NFSD_A_SVC_EXPORT_REQS_REQUESTS);
		if (!nest)
			break;

		if (nla_put_u64_64bit(skb, NFSD_A_SVC_EXPORT_SEQNO,
				      seqnos[i], 0) ||
		    nla_put_string(skb, NFSD_A_SVC_EXPORT_CLIENT,
				   exp->ex_client->name) ||
		    nla_put_string(skb, NFSD_A_SVC_EXPORT_PATH, pth)) {
			nla_nest_cancel(skb, nest);
			break;
		}

		nla_nest_end(skb, nest);
		cb->args[0] = seqnos[i];
		emitted++;
	}

	if (!emitted) {
		genlmsg_cancel(skb, hdr);
		ret = -EMSGSIZE;
		goto out_put;
	}

	genlmsg_end(skb, hdr);
	ret = skb->len;
out_put:
	for (i = 0; i < cnt; i++)
		cache_put(items[i], cd);
out_alloc:
	kfree(pathbuf);
	kfree(seqnos);
	kfree(items);
out_unlock:
	mutex_unlock(&nfsd_mutex);
	return ret;
}

/**
 * nfsd_nl_parse_fslocations - parse fslocations from netlink
 * @attr: NFSD_A_SVC_EXPORT_FSLOCATIONS nested attribute
 * @fsloc: fslocations struct to fill in
 *
 * Returns 0 on success or a negative errno.
 */
static int nfsd_nl_parse_fslocations(struct nlattr *attr,
				     struct nfsd4_fs_locations *fsloc)
{
	struct nlattr *loc_attr;
	int rem, count = 0;
	int err;

	if (fsloc->locations)
		return -EINVAL;

	/* Count locations first */
	nla_for_each_nested_type(loc_attr, NFSD_A_FSLOCATIONS_LOCATION,
				 attr, rem)
		count++;

	if (count > MAX_FS_LOCATIONS)
		return -EINVAL;
	if (!count)
		return 0;

	fsloc->locations = kcalloc(count, sizeof(struct nfsd4_fs_location),
				   GFP_KERNEL);
	if (!fsloc->locations)
		return -ENOMEM;

	nla_for_each_nested_type(loc_attr, NFSD_A_FSLOCATIONS_LOCATION,
				 attr, rem) {
		struct nlattr *tb[NFSD_A_FSLOCATION_PATH + 1];
		struct nfsd4_fs_location *loc;

		err = nla_parse_nested(tb, NFSD_A_FSLOCATION_PATH, loc_attr,
				       nfsd_fslocation_nl_policy, NULL);
		if (err)
			goto out_free;

		if (!tb[NFSD_A_FSLOCATION_HOST] ||
		    !tb[NFSD_A_FSLOCATION_PATH]) {
			err = -EINVAL;
			goto out_free;
		}

		loc = &fsloc->locations[fsloc->locations_count++];
		loc->hosts = kstrdup(nla_data(tb[NFSD_A_FSLOCATION_HOST]),
				     GFP_KERNEL);
		loc->path = kstrdup(nla_data(tb[NFSD_A_FSLOCATION_PATH]),
				    GFP_KERNEL);
		if (!loc->hosts || !loc->path) {
			err = -ENOMEM;
			goto out_free;
		}
	}

	return 0;
out_free:
	nfsd4_fslocs_free(fsloc);
	return err;
}

static struct svc_export *svc_export_update(struct svc_export *new,
					    struct svc_export *old);
static struct svc_export *svc_export_lookup(struct svc_export *);
static int check_export(const struct path *path, int *flags,
			unsigned char *uuid);

/**
 * nfsd_nl_parse_one_export - parse one svc_export entry from a netlink message
 * @cd: cache_detail for the svc_export cache
 * @attr: nested attribute containing svc-export fields
 *
 * Parses one svc-export entry from a netlink message and updates the
 * cache. Mirrors the logic in svc_export_parse().
 *
 * Returns 0 on success or a negative errno.
 */
static int nfsd_nl_parse_one_export(struct cache_detail *cd,
				    struct nlattr *attr)
{
	struct nlattr *tb[NFSD_A_SVC_EXPORT_FSID + 1];
	struct auth_domain *dom = NULL;
	struct svc_export exp = {}, *expp;
	struct nlattr *secinfo_attr;
	struct timespec64 boot;
	int err, rem;

	err = nla_parse_nested(tb, NFSD_A_SVC_EXPORT_FSID, attr,
			       nfsd_svc_export_nl_policy, NULL);
	if (err)
		return err;

	/* client (required) */
	if (!tb[NFSD_A_SVC_EXPORT_CLIENT])
		return -EINVAL;

	dom = auth_domain_find(nla_data(tb[NFSD_A_SVC_EXPORT_CLIENT]));
	if (!dom)
		return -ENOENT;

	/* path (required) */
	if (!tb[NFSD_A_SVC_EXPORT_PATH]) {
		err = -EINVAL;
		goto out_dom;
	}

	err = kern_path(nla_data(tb[NFSD_A_SVC_EXPORT_PATH]), 0,
			&exp.ex_path);
	if (err)
		goto out_dom;

	exp.ex_client = dom;
	exp.cd = cd;
	exp.ex_devid_map = NULL;
	exp.ex_xprtsec_modes = NFSEXP_XPRTSEC_ALL;

	/* expiry (required, wallclock seconds) */
	if (!tb[NFSD_A_SVC_EXPORT_EXPIRY]) {
		err = -EINVAL;
		goto out_path;
	}
	getboottime64(&boot);
	exp.h.expiry_time = nla_get_u64(tb[NFSD_A_SVC_EXPORT_EXPIRY]) -
			    boot.tv_sec;

	if (tb[NFSD_A_SVC_EXPORT_NEGATIVE]) {
		set_bit(CACHE_NEGATIVE, &exp.h.flags);
	} else {
		/* flags */
		if (tb[NFSD_A_SVC_EXPORT_FLAGS])
			exp.ex_flags = nla_get_u32(tb[NFSD_A_SVC_EXPORT_FLAGS]);

		/* anon uid */
		if (tb[NFSD_A_SVC_EXPORT_ANON_UID]) {
			u32 uid = nla_get_u32(tb[NFSD_A_SVC_EXPORT_ANON_UID]);

			exp.ex_anon_uid = make_kuid(current_user_ns(), uid);
		}

		/* anon gid */
		if (tb[NFSD_A_SVC_EXPORT_ANON_GID]) {
			u32 gid = nla_get_u32(tb[NFSD_A_SVC_EXPORT_ANON_GID]);

			exp.ex_anon_gid = make_kgid(current_user_ns(), gid);
		}

		/* fsid */
		if (tb[NFSD_A_SVC_EXPORT_FSID])
			exp.ex_fsid = nla_get_s32(tb[NFSD_A_SVC_EXPORT_FSID]);

		/* fslocations */
		if (tb[NFSD_A_SVC_EXPORT_FSLOCATIONS]) {
			struct nlattr *fsl = tb[NFSD_A_SVC_EXPORT_FSLOCATIONS];

			err = nfsd_nl_parse_fslocations(fsl,
							&exp.ex_fslocs);
			if (err)
				goto out_path;
		}

		/* uuid */
		if (tb[NFSD_A_SVC_EXPORT_UUID]) {
			if (nla_len(tb[NFSD_A_SVC_EXPORT_UUID]) !=
			    EX_UUID_LEN) {
				err = -EINVAL;
				goto out_fslocs;
			}
			exp.ex_uuid = kmemdup(nla_data(tb[NFSD_A_SVC_EXPORT_UUID]),
					      EX_UUID_LEN, GFP_KERNEL);
			if (!exp.ex_uuid) {
				err = -ENOMEM;
				goto out_fslocs;
			}
		}

		/* secinfo (multi-attr) */
		nla_for_each_nested_type(secinfo_attr,
					 NFSD_A_SVC_EXPORT_SECINFO,
					 attr, rem) {
			struct nlattr *ftb[NFSD_A_AUTH_FLAVOR_FLAGS + 1];
			struct exp_flavor_info *f;

			if (exp.ex_nflavors >= MAX_SECINFO_LIST) {
				err = -EINVAL;
				goto out_uuid;
			}

			err = nla_parse_nested(ftb,
					       NFSD_A_AUTH_FLAVOR_FLAGS,
					       secinfo_attr,
					       nfsd_auth_flavor_nl_policy,
					       NULL);
			if (err)
				goto out_uuid;

			f = &exp.ex_flavors[exp.ex_nflavors++];

			if (ftb[NFSD_A_AUTH_FLAVOR_PSEUDOFLAVOR])
				f->pseudoflavor = nla_get_u32(ftb[NFSD_A_AUTH_FLAVOR_PSEUDOFLAVOR]);

			if (ftb[NFSD_A_AUTH_FLAVOR_FLAGS])
				f->flags = nla_get_u32(ftb[NFSD_A_AUTH_FLAVOR_FLAGS]);

			/* Only some flags are allowed to differ between flavors: */
			if (~NFSEXP_SECINFO_FLAGS & (f->flags ^ exp.ex_flags)) {
				err = -EINVAL;
				goto out_uuid;
			}
		}

		/* xprtsec (multi-attr u32) */
		if (tb[NFSD_A_SVC_EXPORT_XPRTSEC]) {
			struct nlattr *xp_attr;

			exp.ex_xprtsec_modes = 0;
			nla_for_each_nested_type(xp_attr,
						 NFSD_A_SVC_EXPORT_XPRTSEC,
						 attr, rem) {
				u32 mode = nla_get_u32(xp_attr);

				if (mode > NFSEXP_XPRTSEC_MTLS) {
					err = -EINVAL;
					goto out_uuid;
				}
				exp.ex_xprtsec_modes |= mode;
			}
		}

		err = check_export(&exp.ex_path, &exp.ex_flags,
				   exp.ex_uuid);
		if (err)
			goto out_uuid;

		if (exp.h.expiry_time < seconds_since_boot())
			goto out_uuid;

		err = -EINVAL;
		if (!uid_valid(exp.ex_anon_uid))
			goto out_uuid;
		if (!gid_valid(exp.ex_anon_gid))
			goto out_uuid;
		err = 0;

		nfsd4_setup_layout_type(&exp);
	}

	expp = svc_export_lookup(&exp);
	if (!expp) {
		err = -ENOMEM;
		goto out_uuid;
	}
	expp = svc_export_update(&exp, expp);
	if (expp) {
		trace_nfsd_export_update(expp);
		cache_flush();
		exp_put(expp);
	} else {
		err = -ENOMEM;
	}

out_uuid:
	kfree(exp.ex_uuid);
out_fslocs:
	nfsd4_fslocs_free(&exp.ex_fslocs);
out_path:
	path_put(&exp.ex_path);
out_dom:
	auth_domain_put(dom);
	return err;
}

/**
 * nfsd_nl_svc_export_set_reqs_doit - respond to svc_export requests
 * @skb: reply buffer
 * @info: netlink metadata and command arguments
 *
 * Parse one or more svc_export cache responses from userspace and
 * update the export cache accordingly.
 *
 * Returns 0 on success or a negative errno.
 */
int nfsd_nl_svc_export_set_reqs_doit(struct sk_buff *skb,
				     struct genl_info *info)
{
	struct nfsd_net *nn;
	struct cache_detail *cd;
	const struct nlattr *attr;
	int rem, ret = 0;

	nn = net_generic(genl_info_net(info), nfsd_net_id);

	mutex_lock(&nfsd_mutex);

	cd = nn->svc_export_cache;
	if (!cd) {
		ret = -ENODEV;
		goto out_unlock;
	}

	nlmsg_for_each_attr_type(attr, NFSD_A_SVC_EXPORT_REQS_REQUESTS,
				 info->nlhdr, GENL_HDRLEN, rem) {
		ret = nfsd_nl_parse_one_export(cd, (struct nlattr *)attr);
		if (ret)
			break;
	}

out_unlock:
	mutex_unlock(&nfsd_mutex);
	return ret;
}

static int svc_export_upcall(struct cache_detail *cd, struct cache_head *h)
{
	return sunrpc_cache_upcall(cd, h);
}

static int svc_export_notify(struct cache_detail *cd, struct cache_head *h)
{
	return nfsd_cache_notify(cd, h, NFSD_CACHE_TYPE_SVC_EXPORT);
}

static void svc_export_request(struct cache_detail *cd,
			       struct cache_head *h,
			       char **bpp, int *blen)
{
	/*  client path */
	struct svc_export *exp = container_of(h, struct svc_export, h);
	char *pth;

	qword_add(bpp, blen, exp->ex_client->name);
	pth = d_path(&exp->ex_path, *bpp, *blen);
	if (IS_ERR(pth)) {
		/* is this correct? */
		(*bpp)[0] = '\n';
		return;
	}
	qword_add(bpp, blen, pth);
	(*bpp)[-1] = '\n';
}

static int check_export(const struct path *path, int *flags, unsigned char *uuid)
{
	struct inode *inode = d_inode(path->dentry);

	/*
	 * We currently export only dirs, regular files, and (for v4
	 * pseudoroot) symlinks.
	 */
	if (!S_ISDIR(inode->i_mode) &&
	    !S_ISLNK(inode->i_mode) &&
	    !S_ISREG(inode->i_mode))
		return -ENOTDIR;

	/*
	 * Mountd should never pass down a writeable V4ROOT export, but,
	 * just to make sure:
	 */
	if (*flags & NFSEXP_V4ROOT)
		*flags |= NFSEXP_READONLY;

	/* There are two requirements on a filesystem to be exportable.
	 * 1:  We must be able to identify the filesystem from a number.
	 *       either a device number (so FS_REQUIRES_DEV needed)
	 *       or an FSID number (so NFSEXP_FSID or ->uuid is needed).
	 * 2:  We must be able to find an inode from a filehandle.
	 *       This means that s_export_op must be set and comply with
	 *       the requirements for remote filesystem export.
	 * 3: We must not currently be on an idmapped mount.
	 */
	if (!(inode->i_sb->s_type->fs_flags & FS_REQUIRES_DEV) &&
	    !(*flags & NFSEXP_FSID) &&
	    uuid == NULL) {
		dprintk("exp_export: export of non-dev fs without fsid\n");
		return -EINVAL;
	}

	if (!exportfs_may_export(inode->i_sb->s_export_op)) {
		dprintk("exp_export: export of invalid fs type (%s).\n",
			inode->i_sb->s_type->name);
		return -EINVAL;
	}

	if (is_idmapped_mnt(path->mnt)) {
		dprintk("exp_export: export of idmapped mounts not yet supported.\n");
		return -EINVAL;
	}

	if (inode->i_sb->s_export_op->flags & EXPORT_OP_NOSUBTREECHK &&
	    !(*flags & NFSEXP_NOSUBTREECHECK)) {
		dprintk("%s: %s does not support subtree checking!\n",
			__func__, inode->i_sb->s_type->name);
		return -EINVAL;
	}
	return 0;
}

#ifdef CONFIG_NFSD_V4

static int
fsloc_parse(char **mesg, char *buf, struct nfsd4_fs_locations *fsloc)
{
	int len;
	int migrated, i, err;

	/* more than one fsloc */
	if (fsloc->locations)
		return -EINVAL;

	/* listsize */
	err = get_uint(mesg, &fsloc->locations_count);
	if (err)
		return err;
	if (fsloc->locations_count > MAX_FS_LOCATIONS)
		return -EINVAL;
	if (fsloc->locations_count == 0)
		return 0;

	fsloc->locations = kzalloc_objs(struct nfsd4_fs_location,
					fsloc->locations_count);
	if (!fsloc->locations)
		return -ENOMEM;
	for (i=0; i < fsloc->locations_count; i++) {
		/* colon separated host list */
		err = -EINVAL;
		len = qword_get(mesg, buf, PAGE_SIZE);
		if (len <= 0)
			goto out_free_all;
		err = -ENOMEM;
		fsloc->locations[i].hosts = kstrdup(buf, GFP_KERNEL);
		if (!fsloc->locations[i].hosts)
			goto out_free_all;
		err = -EINVAL;
		/* slash separated path component list */
		len = qword_get(mesg, buf, PAGE_SIZE);
		if (len <= 0)
			goto out_free_all;
		err = -ENOMEM;
		fsloc->locations[i].path = kstrdup(buf, GFP_KERNEL);
		if (!fsloc->locations[i].path)
			goto out_free_all;
	}
	/* migrated */
	err = get_int(mesg, &migrated);
	if (err)
		goto out_free_all;
	err = -EINVAL;
	if (migrated < 0 || migrated > 1)
		goto out_free_all;
	fsloc->migrated = migrated;
	return 0;
out_free_all:
	nfsd4_fslocs_free(fsloc);
	return err;
}

static int secinfo_parse(char **mesg, char *buf, struct svc_export *exp)
{
	struct exp_flavor_info *f;
	u32 listsize;
	int err;

	/* more than one secinfo */
	if (exp->ex_nflavors)
		return -EINVAL;

	err = get_uint(mesg, &listsize);
	if (err)
		return err;
	if (listsize > MAX_SECINFO_LIST)
		return -EINVAL;

	for (f = exp->ex_flavors; f < exp->ex_flavors + listsize; f++) {
		err = get_uint(mesg, &f->pseudoflavor);
		if (err)
			return err;
		/*
		 * XXX: It would be nice to also check whether this
		 * pseudoflavor is supported, so we can discover the
		 * problem at export time instead of when a client fails
		 * to authenticate.
		 */
		err = get_uint(mesg, &f->flags);
		if (err)
			return err;
		/* Only some flags are allowed to differ between flavors: */
		if (~NFSEXP_SECINFO_FLAGS & (f->flags ^ exp->ex_flags))
			return -EINVAL;
	}
	exp->ex_nflavors = listsize;
	return 0;
}

#else /* CONFIG_NFSD_V4 */
static inline int
fsloc_parse(char **mesg, char *buf, struct nfsd4_fs_locations *fsloc){return 0;}
static inline int
secinfo_parse(char **mesg, char *buf, struct svc_export *exp) { return 0; }
#endif

static int xprtsec_parse(char **mesg, char *buf, struct svc_export *exp)
{
	unsigned int i, mode, listsize;
	int err;

	err = get_uint(mesg, &listsize);
	if (err)
		return err;
	if (listsize > NFSEXP_XPRTSEC_NUM)
		return -EINVAL;

	exp->ex_xprtsec_modes = 0;
	for (i = 0; i < listsize; i++) {
		err = get_uint(mesg, &mode);
		if (err)
			return err;
		if (mode > NFSEXP_XPRTSEC_MTLS)
			return -EINVAL;
		exp->ex_xprtsec_modes |= mode;
	}
	return 0;
}

static inline int
nfsd_uuid_parse(char **mesg, char *buf, unsigned char **puuid)
{
	int len;

	/* more than one uuid */
	if (*puuid)
		return -EINVAL;

	/* expect a 16 byte uuid encoded as \xXXXX... */
	len = qword_get(mesg, buf, PAGE_SIZE);
	if (len != EX_UUID_LEN)
		return -EINVAL;

	*puuid = kmemdup(buf, EX_UUID_LEN, GFP_KERNEL);
	if (*puuid == NULL)
		return -ENOMEM;

	return 0;
}

static int svc_export_parse(struct cache_detail *cd, char *mesg, int mlen)
{
	/* client path expiry [flags anonuid anongid fsid] */
	char *buf;
	int err;
	struct auth_domain *dom = NULL;
	struct svc_export exp = {}, *expp;
	int an_int;

	if (mesg[mlen-1] != '\n')
		return -EINVAL;
	mesg[mlen-1] = 0;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* client */
	err = -EINVAL;
	if (qword_get(&mesg, buf, PAGE_SIZE) <= 0)
		goto out;

	err = -ENOENT;
	dom = auth_domain_find(buf);
	if (!dom)
		goto out;

	/* path */
	err = -EINVAL;
	if (qword_get(&mesg, buf, PAGE_SIZE) <= 0)
		goto out1;

	err = kern_path(buf, 0, &exp.ex_path);
	if (err)
		goto out1;

	exp.ex_client = dom;
	exp.cd = cd;
	exp.ex_devid_map = NULL;
	exp.ex_xprtsec_modes = NFSEXP_XPRTSEC_ALL;

	/* expiry */
	err = get_expiry(&mesg, &exp.h.expiry_time);
	if (err)
		goto out3;

	/* flags */
	err = get_int(&mesg, &an_int);
	if (err == -ENOENT) {
		err = 0;
		set_bit(CACHE_NEGATIVE, &exp.h.flags);
	} else {
		if (err || an_int < 0)
			goto out3;
		exp.ex_flags= an_int;

		/* anon uid */
		err = get_int(&mesg, &an_int);
		if (err)
			goto out3;
		exp.ex_anon_uid= make_kuid(current_user_ns(), an_int);

		/* anon gid */
		err = get_int(&mesg, &an_int);
		if (err)
			goto out3;
		exp.ex_anon_gid= make_kgid(current_user_ns(), an_int);

		/* fsid */
		err = get_int(&mesg, &an_int);
		if (err)
			goto out3;
		exp.ex_fsid = an_int;

		while (qword_get(&mesg, buf, PAGE_SIZE) > 0) {
			if (strcmp(buf, "fsloc") == 0)
				err = fsloc_parse(&mesg, buf, &exp.ex_fslocs);
			else if (strcmp(buf, "uuid") == 0)
				err = nfsd_uuid_parse(&mesg, buf, &exp.ex_uuid);
			else if (strcmp(buf, "secinfo") == 0)
				err = secinfo_parse(&mesg, buf, &exp);
			else if (strcmp(buf, "xprtsec") == 0)
				err = xprtsec_parse(&mesg, buf, &exp);
			else
				/* quietly ignore unknown words and anything
				 * following. Newer user-space can try to set
				 * new values, then see what the result was.
				 */
				break;
			if (err)
				goto out4;
		}

		err = check_export(&exp.ex_path, &exp.ex_flags, exp.ex_uuid);
		if (err)
			goto out4;

		/*
		 * No point caching this if it would immediately expire.
		 * Also, this protects exportfs's dummy export from the
		 * anon_uid/anon_gid checks:
		 */
		if (exp.h.expiry_time < seconds_since_boot())
			goto out4;
		/*
		 * For some reason exportfs has been passing down an
		 * invalid (-1) uid & gid on the "dummy" export which it
		 * uses to test export support.  To make sure exportfs
		 * sees errors from check_export we therefore need to
		 * delay these checks till after check_export:
		 */
		err = -EINVAL;
		if (!uid_valid(exp.ex_anon_uid))
			goto out4;
		if (!gid_valid(exp.ex_anon_gid))
			goto out4;
		err = 0;

		if (exp.ex_flags & NFSEXP_PNFS)
			nfsd4_setup_layout_type(&exp);
	}

	expp = svc_export_lookup(&exp);
	if (!expp) {
		err = -ENOMEM;
		goto out4;
	}
	expp = svc_export_update(&exp, expp);
	if (expp) {
		trace_nfsd_export_update(expp);
		cache_flush();
		exp_put(expp);
	} else
		err = -ENOMEM;
out4:
	nfsd4_fslocs_free(&exp.ex_fslocs);
	kfree(exp.ex_uuid);
out3:
	path_put(&exp.ex_path);
out1:
	auth_domain_put(dom);
out:
	kfree(buf);
	return err;
}

static void exp_flags(struct seq_file *m, int flag, int fsid,
		kuid_t anonu, kgid_t anong, struct nfsd4_fs_locations *fslocs);
static void show_secinfo(struct seq_file *m, struct svc_export *exp);

static int is_export_stats_file(struct seq_file *m)
{
	/*
	 * The export_stats file uses the same ops as the exports file.
	 * We use the file's name to determine the reported info per export.
	 * There is no rename in nsfdfs, so d_name.name is stable.
	 */
	return !strcmp(m->file->f_path.dentry->d_name.name, "export_stats");
}

static int svc_export_show(struct seq_file *m,
			   struct cache_detail *cd,
			   struct cache_head *h)
{
	struct svc_export *exp;
	bool export_stats = is_export_stats_file(m);

	if (h == NULL) {
		if (export_stats)
			seq_puts(m, "#path domain start-time\n#\tstats\n");
		else
			seq_puts(m, "#path domain(flags)\n");
		return 0;
	}
	exp = container_of(h, struct svc_export, h);
	seq_path(m, &exp->ex_path, " \t\n\\");
	seq_putc(m, '\t');
	seq_escape(m, exp->ex_client->name, " \t\n\\");
	if (export_stats) {
		struct percpu_counter *counter = exp->ex_stats->counter;

		seq_printf(m, "\t%lld\n", exp->ex_stats->start_time);
		seq_printf(m, "\tfh_stale: %lld\n",
			   percpu_counter_sum_positive(&counter[EXP_STATS_FH_STALE]));
		seq_printf(m, "\tio_read: %lld\n",
			   percpu_counter_sum_positive(&counter[EXP_STATS_IO_READ]));
		seq_printf(m, "\tio_write: %lld\n",
			   percpu_counter_sum_positive(&counter[EXP_STATS_IO_WRITE]));
		seq_putc(m, '\n');
		return 0;
	}
	seq_putc(m, '(');
	if (test_bit(CACHE_VALID, &h->flags) &&
	    !test_bit(CACHE_NEGATIVE, &h->flags)) {
		exp_flags(m, exp->ex_flags, exp->ex_fsid,
			  exp->ex_anon_uid, exp->ex_anon_gid, &exp->ex_fslocs);
		if (exp->ex_uuid) {
			int i;
			seq_puts(m, ",uuid=");
			for (i = 0; i < EX_UUID_LEN; i++) {
				if ((i&3) == 0 && i)
					seq_putc(m, ':');
				seq_printf(m, "%02x", exp->ex_uuid[i]);
			}
		}
		show_secinfo(m, exp);
	}
	seq_puts(m, ")\n");
	return 0;
}
static int svc_export_match(struct cache_head *a, struct cache_head *b)
{
	struct svc_export *orig = container_of(a, struct svc_export, h);
	struct svc_export *new = container_of(b, struct svc_export, h);
	return orig->ex_client == new->ex_client &&
		path_equal(&orig->ex_path, &new->ex_path);
}

static void svc_export_init(struct cache_head *cnew, struct cache_head *citem)
{
	struct svc_export *new = container_of(cnew, struct svc_export, h);
	struct svc_export *item = container_of(citem, struct svc_export, h);

	kref_get(&item->ex_client->ref);
	new->ex_client = item->ex_client;
	new->ex_path = item->ex_path;
	path_get(&item->ex_path);
	new->ex_fslocs.locations = NULL;
	new->ex_fslocs.locations_count = 0;
	new->ex_fslocs.migrated = 0;
	new->ex_layout_types = 0;
	new->ex_uuid = NULL;
	new->cd = item->cd;
	export_stats_reset(new->ex_stats);
}

static void export_update(struct cache_head *cnew, struct cache_head *citem)
{
	struct svc_export *new = container_of(cnew, struct svc_export, h);
	struct svc_export *item = container_of(citem, struct svc_export, h);
	int i;

	new->ex_flags = item->ex_flags;
	new->ex_anon_uid = item->ex_anon_uid;
	new->ex_anon_gid = item->ex_anon_gid;
	new->ex_fsid = item->ex_fsid;
	new->ex_devid_map = item->ex_devid_map;
	item->ex_devid_map = NULL;
	new->ex_uuid = item->ex_uuid;
	item->ex_uuid = NULL;
	new->ex_fslocs.locations = item->ex_fslocs.locations;
	item->ex_fslocs.locations = NULL;
	new->ex_fslocs.locations_count = item->ex_fslocs.locations_count;
	item->ex_fslocs.locations_count = 0;
	new->ex_fslocs.migrated = item->ex_fslocs.migrated;
	item->ex_fslocs.migrated = 0;
	new->ex_layout_types = item->ex_layout_types;
	new->ex_nflavors = item->ex_nflavors;
	for (i = 0; i < MAX_SECINFO_LIST; i++) {
		new->ex_flavors[i] = item->ex_flavors[i];
	}
	new->ex_xprtsec_modes = item->ex_xprtsec_modes;
}

static struct cache_head *svc_export_alloc(void)
{
	struct svc_export *i = kmalloc_obj(*i);
	if (!i)
		return NULL;

	i->ex_stats = kmalloc_obj(*(i->ex_stats));
	if (!i->ex_stats) {
		kfree(i);
		return NULL;
	}

	if (export_stats_init(i->ex_stats)) {
		kfree(i->ex_stats);
		kfree(i);
		return NULL;
	}

	return &i->h;
}

static const struct cache_detail svc_export_cache_template = {
	.owner		= THIS_MODULE,
	.hash_size	= EXPORT_HASHMAX,
	.name		= "nfsd.export",
	.cache_put	= svc_export_put,
	.cache_upcall	= svc_export_upcall,
	.cache_notify	= svc_export_notify,
	.cache_request	= svc_export_request,
	.cache_parse	= svc_export_parse,
	.cache_show	= svc_export_show,
	.match		= svc_export_match,
	.init		= svc_export_init,
	.update		= export_update,
	.alloc		= svc_export_alloc,
};

static int
svc_export_hash(struct svc_export *exp)
{
	int hash;

	hash = hash_ptr(exp->ex_client, EXPORT_HASHBITS);
	hash ^= hash_ptr(exp->ex_path.dentry, EXPORT_HASHBITS);
	hash ^= hash_ptr(exp->ex_path.mnt, EXPORT_HASHBITS);
	return hash;
}

static struct svc_export *
svc_export_lookup(struct svc_export *exp)
{
	struct cache_head *ch;
	int hash = svc_export_hash(exp);

	ch = sunrpc_cache_lookup_rcu(exp->cd, &exp->h, hash);
	if (ch)
		return container_of(ch, struct svc_export, h);
	else
		return NULL;
}

static struct svc_export *
svc_export_update(struct svc_export *new, struct svc_export *old)
{
	struct cache_head *ch;
	int hash = svc_export_hash(old);

	ch = sunrpc_cache_update(old->cd, &new->h, &old->h, hash);
	if (ch)
		return container_of(ch, struct svc_export, h);
	else
		return NULL;
}


static struct svc_expkey *
exp_find_key(struct cache_detail *cd, struct auth_domain *clp, int fsid_type,
	     u32 *fsidv, struct cache_req *reqp)
{
	struct svc_expkey key, *ek;
	int err;
	
	if (!clp)
		return ERR_PTR(-ENOENT);

	key.ek_client = clp;
	key.ek_fsidtype = fsid_type;
	memcpy(key.ek_fsid, fsidv, key_len(fsid_type));

	ek = svc_expkey_lookup(cd, &key);
	if (ek == NULL)
		return ERR_PTR(-ENOMEM);
	err = cache_check(cd, &ek->h, reqp);
	if (err) {
		trace_nfsd_exp_find_key(&key, err);
		return ERR_PTR(err);
	}
	return ek;
}

static struct svc_export *
exp_get_by_name(struct cache_detail *cd, struct auth_domain *clp,
		const struct path *path, struct cache_req *reqp)
{
	struct svc_export *exp, key;
	int err;

	if (!clp)
		return ERR_PTR(-ENOENT);

	key.ex_client = clp;
	key.ex_path = *path;
	key.cd = cd;

	exp = svc_export_lookup(&key);
	if (exp == NULL)
		return ERR_PTR(-ENOMEM);
	err = cache_check(cd, &exp->h, reqp);
	if (err) {
		trace_nfsd_exp_get_by_name(&key, err);
		return ERR_PTR(err);
	}
	return exp;
}

/*
 * Find the export entry for a given dentry.
 */
static struct svc_export *
exp_parent(struct cache_detail *cd, struct auth_domain *clp, struct path *path)
{
	struct dentry *saved = dget(path->dentry);
	struct svc_export *exp = exp_get_by_name(cd, clp, path, NULL);

	while (PTR_ERR(exp) == -ENOENT && !IS_ROOT(path->dentry)) {
		struct dentry *parent = dget_parent(path->dentry);
		dput(path->dentry);
		path->dentry = parent;
		exp = exp_get_by_name(cd, clp, path, NULL);
	}
	dput(path->dentry);
	path->dentry = saved;
	return exp;
}



/*
 * Obtain the root fh on behalf of a client.
 * This could be done in user space, but I feel that it adds some safety
 * since its harder to fool a kernel module than a user space program.
 */
int
exp_rootfh(struct net *net, struct auth_domain *clp, char *name,
	   struct knfsd_fh *f, int maxsize)
{
	struct svc_export	*exp;
	struct path		path;
	struct inode		*inode __maybe_unused;
	struct svc_fh		fh;
	int			err;
	struct nfsd_net		*nn = net_generic(net, nfsd_net_id);
	struct cache_detail	*cd = nn->svc_export_cache;

	err = -EPERM;
	/* NB: we probably ought to check that it's NUL-terminated */
	if (kern_path(name, 0, &path)) {
		printk("nfsd: exp_rootfh path not found %s", name);
		return err;
	}
	inode = d_inode(path.dentry);

	dprintk("nfsd: exp_rootfh(%s [%p] %s:%s/%llu)\n",
		 name, path.dentry, clp->name,
		 inode->i_sb->s_id, inode->i_ino);
	exp = exp_parent(cd, clp, &path);
	if (IS_ERR(exp)) {
		err = PTR_ERR(exp);
		goto out;
	}

	/*
	 * fh must be initialized before calling fh_compose
	 */
	fh_init(&fh, maxsize);
	if (fh_compose(&fh, exp, path.dentry, NULL))
		err = -EINVAL;
	else
		err = 0;
	memcpy(f, &fh.fh_handle, sizeof(struct knfsd_fh));
	fh_put(&fh);
	exp_put(exp);
out:
	path_put(&path);
	return err;
}

static struct svc_export *exp_find(struct cache_detail *cd,
				   struct auth_domain *clp, int fsid_type,
				   u32 *fsidv, struct cache_req *reqp)
{
	struct svc_export *exp;
	struct nfsd_net *nn = net_generic(cd->net, nfsd_net_id);
	struct svc_expkey *ek = exp_find_key(nn->svc_expkey_cache, clp, fsid_type, fsidv, reqp);
	if (IS_ERR(ek))
		return ERR_CAST(ek);

	exp = exp_get_by_name(cd, clp, &ek->ek_path, reqp);
	cache_put(&ek->h, nn->svc_expkey_cache);

	if (IS_ERR(exp))
		return ERR_CAST(exp);
	return exp;
}

/**
 * check_xprtsec_policy - check if access to export is allowed by the
 *			  xprtsec policy
 * @exp: svc_export that is being accessed.
 * @rqstp: svc_rqst attempting to access @exp.
 *
 * Helper function for check_nfsd_access().  Note that callers should be
 * using check_nfsd_access() instead of calling this function directly.  The
 * one exception is __fh_verify() since it has logic that may result in one
 * or both of the helpers being skipped.
 *
 * Return values:
 *   %nfs_ok if access is granted, or
 *   %nfserr_wrongsec if access is denied
 */
__be32 check_xprtsec_policy(struct svc_export *exp, struct svc_rqst *rqstp)
{
	struct svc_xprt *xprt = rqstp->rq_xprt;

	if (exp->ex_xprtsec_modes & NFSEXP_XPRTSEC_NONE) {
		if (!test_bit(XPT_TLS_SESSION, &xprt->xpt_flags))
			return nfs_ok;
	}
	if (exp->ex_xprtsec_modes & NFSEXP_XPRTSEC_TLS) {
		if (test_bit(XPT_TLS_SESSION, &xprt->xpt_flags) &&
		    !test_bit(XPT_PEER_AUTH, &xprt->xpt_flags))
			return nfs_ok;
	}
	if (exp->ex_xprtsec_modes & NFSEXP_XPRTSEC_MTLS) {
		if (test_bit(XPT_TLS_SESSION, &xprt->xpt_flags) &&
		    test_bit(XPT_PEER_AUTH, &xprt->xpt_flags))
			return nfs_ok;
	}
	return nfserr_wrongsec;
}

/**
 * check_security_flavor - check if access to export is allowed by the
 *			   security flavor
 * @exp: svc_export that is being accessed.
 * @rqstp: svc_rqst attempting to access @exp.
 * @may_bypass_gss: reduce strictness of authorization check
 *
 * Helper function for check_nfsd_access().  Note that callers should be
 * using check_nfsd_access() instead of calling this function directly.  The
 * one exception is __fh_verify() since it has logic that may result in one
 * or both of the helpers being skipped.
 *
 * Return values:
 *   %nfs_ok if access is granted, or
 *   %nfserr_wrongsec if access is denied
 */
__be32 check_security_flavor(struct svc_export *exp, struct svc_rqst *rqstp,
			     bool may_bypass_gss)
{
	struct exp_flavor_info *f, *end = exp->ex_flavors + exp->ex_nflavors;

	/* legacy gss-only clients are always OK: */
	if (exp->ex_client == rqstp->rq_gssclient)
		return nfs_ok;
	/* ip-address based client; check sec= export option: */
	for (f = exp->ex_flavors; f < end; f++) {
		if (f->pseudoflavor == rqstp->rq_cred.cr_flavor)
			return nfs_ok;
	}
	/* defaults in absence of sec= options: */
	if (exp->ex_nflavors == 0) {
		if (rqstp->rq_cred.cr_flavor == RPC_AUTH_NULL ||
		    rqstp->rq_cred.cr_flavor == RPC_AUTH_UNIX)
			return nfs_ok;
	}

	/* If the compound op contains a spo_must_allowed op,
	 * it will be sent with integrity/protection which
	 * will have to be expressly allowed on mounts that
	 * don't support it
	 */

	if (nfsd4_spo_must_allow(rqstp))
		return nfs_ok;

	/* Some calls may be processed without authentication
	 * on GSS exports. For example NFS2/3 calls on root
	 * directory, see section 2.3.2 of rfc 2623.
	 * For "may_bypass_gss" check that export has really
	 * enabled some flavor with authentication (GSS or any
	 * other) and also check that the used auth flavor is
	 * without authentication (none or sys).
	 */
	if (may_bypass_gss && (
	     rqstp->rq_cred.cr_flavor == RPC_AUTH_NULL ||
	     rqstp->rq_cred.cr_flavor == RPC_AUTH_UNIX)) {
		for (f = exp->ex_flavors; f < end; f++) {
			if (f->pseudoflavor >= RPC_AUTH_DES)
				return 0;
		}
	}

	return nfserr_wrongsec;
}

/**
 * check_nfsd_access - check if access to export is allowed.
 * @exp: svc_export that is being accessed.
 * @rqstp: svc_rqst attempting to access @exp.
 * @may_bypass_gss: reduce strictness of authorization check
 *
 * Return values:
 *   %nfs_ok if access is granted, or
 *   %nfserr_wrongsec if access is denied
 */
__be32 check_nfsd_access(struct svc_export *exp, struct svc_rqst *rqstp,
			 bool may_bypass_gss)
{
	__be32 status;

	status = check_xprtsec_policy(exp, rqstp);
	if (status != nfs_ok)
		return status;
	return check_security_flavor(exp, rqstp, may_bypass_gss);
}

/*
 * Uses rq_client and rq_gssclient to find an export; uses rq_client (an
 * auth_unix client) if it's available and has secinfo information;
 * otherwise, will try to use rq_gssclient.
 *
 * Called from functions that handle requests; functions that do work on
 * behalf of mountd are passed a single client name to use, and should
 * use exp_get_by_name() or exp_find().
 */
struct svc_export *
rqst_exp_get_by_name(struct svc_rqst *rqstp, const struct path *path)
{
	struct svc_export *gssexp, *exp = ERR_PTR(-ENOENT);
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);
	struct cache_detail *cd = nn->svc_export_cache;

	if (rqstp->rq_client == NULL)
		goto gss;

	/* First try the auth_unix client: */
	exp = exp_get_by_name(cd, rqstp->rq_client, path, &rqstp->rq_chandle);
	if (PTR_ERR(exp) == -ENOENT)
		goto gss;
	if (IS_ERR(exp))
		return exp;
	/* If it has secinfo, assume there are no gss/... clients */
	if (exp->ex_nflavors > 0)
		return exp;
gss:
	/* Otherwise, try falling back on gss client */
	if (rqstp->rq_gssclient == NULL)
		return exp;
	gssexp = exp_get_by_name(cd, rqstp->rq_gssclient, path, &rqstp->rq_chandle);
	if (PTR_ERR(gssexp) == -ENOENT)
		return exp;
	if (!IS_ERR(exp))
		exp_put(exp);
	return gssexp;
}

/**
 * rqst_exp_find - Find an svc_export in the context of a rqst or similar
 * @reqp:	The handle to be used to suspend the request if a cache-upcall is needed
 *		If NULL, missing in-cache information will result in failure.
 * @net:	The network namespace in which the request exists
 * @cl:		default auth_domain to use for looking up the export
 * @gsscl:	an alternate auth_domain defined using deprecated gss/krb5 format.
 * @fsid_type:	The type of fsid to look for
 * @fsidv:	The actual fsid to look up in the context of either client.
 *
 * Perform a lookup for @cl/@fsidv in the given @net for an export.  If
 * none found and @gsscl specified, repeat the lookup.
 *
 * Returns an export, or an error pointer.
 */
struct svc_export *
rqst_exp_find(struct cache_req *reqp, struct net *net,
	      struct auth_domain *cl, struct auth_domain *gsscl,
	      int fsid_type, u32 *fsidv)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	struct svc_export *gssexp, *exp = ERR_PTR(-ENOENT);
	struct cache_detail *cd = nn->svc_export_cache;

	if (!cl)
		goto gss;

	/* First try the auth_unix client: */
	exp = exp_find(cd, cl, fsid_type, fsidv, reqp);
	if (PTR_ERR(exp) == -ENOENT)
		goto gss;
	if (IS_ERR(exp))
		return exp;
	/* If it has secinfo, assume there are no gss/... clients */
	if (exp->ex_nflavors > 0)
		return exp;
gss:
	/* Otherwise, try falling back on gss client */
	if (!gsscl)
		return exp;
	gssexp = exp_find(cd, gsscl, fsid_type, fsidv, reqp);
	if (PTR_ERR(gssexp) == -ENOENT)
		return exp;
	if (!IS_ERR(exp))
		exp_put(exp);
	return gssexp;
}

struct svc_export *
rqst_exp_parent(struct svc_rqst *rqstp, struct path *path)
{
	struct dentry *saved = dget(path->dentry);
	struct svc_export *exp = rqst_exp_get_by_name(rqstp, path);

	while (PTR_ERR(exp) == -ENOENT && !IS_ROOT(path->dentry)) {
		struct dentry *parent = dget_parent(path->dentry);
		dput(path->dentry);
		path->dentry = parent;
		exp = rqst_exp_get_by_name(rqstp, path);
	}
	dput(path->dentry);
	path->dentry = saved;
	return exp;
}

struct svc_export *rqst_find_fsidzero_export(struct svc_rqst *rqstp)
{
	u32 fsidv[2];

	mk_fsid(FSID_NUM, fsidv, 0, 0, 0, NULL);

	return rqst_exp_find(&rqstp->rq_chandle, SVC_NET(rqstp),
			     rqstp->rq_client, rqstp->rq_gssclient,
			     FSID_NUM, fsidv);
}

/*
 * Called when we need the filehandle for the root of the pseudofs,
 * for a given NFSv4 client.   The root is defined to be the
 * export point with fsid==0
 */
__be32
exp_pseudoroot(struct svc_rqst *rqstp, struct svc_fh *fhp)
{
	struct svc_export *exp;
	__be32 rv;

	exp = rqst_find_fsidzero_export(rqstp);
	if (IS_ERR(exp))
		return nfserrno(PTR_ERR(exp));
	rv = fh_compose(fhp, exp, exp->ex_path.dentry, NULL);
	exp_put(exp);
	return rv;
}

static struct flags {
	int flag;
	char *name[2];
} expflags[] = {
	{ NFSEXP_READONLY, {"ro", "rw"}},
	{ NFSEXP_INSECURE_PORT, {"insecure", ""}},
	{ NFSEXP_ROOTSQUASH, {"root_squash", "no_root_squash"}},
	{ NFSEXP_ALLSQUASH, {"all_squash", ""}},
	{ NFSEXP_ASYNC, {"async", "sync"}},
	{ NFSEXP_GATHERED_WRITES, {"wdelay", "no_wdelay"}},
	{ NFSEXP_NOREADDIRPLUS, {"nordirplus", ""}},
	{ NFSEXP_SECURITY_LABEL, {"security_label", ""}},
	{ NFSEXP_SIGN_FH, {"sign_fh", ""}},
	{ NFSEXP_NOHIDE, {"nohide", ""}},
	{ NFSEXP_NOSUBTREECHECK, {"no_subtree_check", ""}},
	{ NFSEXP_NOAUTHNLM, {"insecure_locks", ""}},
	{ NFSEXP_CROSSMOUNT, {"crossmnt", ""}},
	{ NFSEXP_V4ROOT, {"v4root", ""}},
	{ NFSEXP_PNFS, {"pnfs", ""}},
	{ 0, {"", ""}}
};

static void show_expflags(struct seq_file *m, int flags, int mask)
{
	struct flags *flg;
	int state, first = 0;

	for (flg = expflags; flg->flag; flg++) {
		if (flg->flag & ~mask)
			continue;
		state = (flg->flag & flags) ? 0 : 1;
		if (*flg->name[state])
			seq_printf(m, "%s%s", first++?",":"", flg->name[state]);
	}
}

static void show_secinfo_flags(struct seq_file *m, int flags)
{
	seq_printf(m, ",");
	show_expflags(m, flags, NFSEXP_SECINFO_FLAGS);
}

static bool secinfo_flags_equal(int f, int g)
{
	f &= NFSEXP_SECINFO_FLAGS;
	g &= NFSEXP_SECINFO_FLAGS;
	return f == g;
}

static int show_secinfo_run(struct seq_file *m, struct exp_flavor_info **fp, struct exp_flavor_info *end)
{
	int flags;

	flags = (*fp)->flags;
	seq_printf(m, ",sec=%d", (*fp)->pseudoflavor);
	(*fp)++;
	while (*fp != end && secinfo_flags_equal(flags, (*fp)->flags)) {
		seq_printf(m, ":%d", (*fp)->pseudoflavor);
		(*fp)++;
	}
	return flags;
}

static void show_secinfo(struct seq_file *m, struct svc_export *exp)
{
	struct exp_flavor_info *f;
	struct exp_flavor_info *end = exp->ex_flavors + exp->ex_nflavors;
	int flags;

	if (exp->ex_nflavors == 0)
		return;
	f = exp->ex_flavors;
	flags = show_secinfo_run(m, &f, end);
	if (!secinfo_flags_equal(flags, exp->ex_flags))
		show_secinfo_flags(m, flags);
	while (f != end) {
		flags = show_secinfo_run(m, &f, end);
		show_secinfo_flags(m, flags);
	}
}

static void exp_flags(struct seq_file *m, int flag, int fsid,
		kuid_t anonu, kgid_t anong, struct nfsd4_fs_locations *fsloc)
{
	struct user_namespace *userns = m->file->f_cred->user_ns;

	show_expflags(m, flag, NFSEXP_ALLFLAGS);
	if (flag & NFSEXP_FSID)
		seq_printf(m, ",fsid=%d", fsid);
	if (!uid_eq(anonu, make_kuid(userns, (uid_t)-2)) &&
	    !uid_eq(anonu, make_kuid(userns, 0x10000-2)))
		seq_printf(m, ",anonuid=%u", from_kuid_munged(userns, anonu));
	if (!gid_eq(anong, make_kgid(userns, (gid_t)-2)) &&
	    !gid_eq(anong, make_kgid(userns, 0x10000-2)))
		seq_printf(m, ",anongid=%u", from_kgid_munged(userns, anong));
	if (fsloc && fsloc->locations_count > 0) {
		char *loctype = (fsloc->migrated) ? "refer" : "replicas";
		int i;

		seq_printf(m, ",%s=", loctype);
		seq_escape(m, fsloc->locations[0].path, ",;@ \t\n\\");
		seq_putc(m, '@');
		seq_escape(m, fsloc->locations[0].hosts, ",;@ \t\n\\");
		for (i = 1; i < fsloc->locations_count; i++) {
			seq_putc(m, ';');
			seq_escape(m, fsloc->locations[i].path, ",;@ \t\n\\");
			seq_putc(m, '@');
			seq_escape(m, fsloc->locations[i].hosts, ",;@ \t\n\\");
		}
	}
}

static int e_show(struct seq_file *m, void *p)
{
	struct cache_head *cp = p;
	struct svc_export *exp = container_of(cp, struct svc_export, h);
	struct cache_detail *cd = m->private;
	bool export_stats = is_export_stats_file(m);

	if (p == SEQ_START_TOKEN) {
		seq_puts(m, "# Version 1.1\n");
		if (export_stats)
			seq_puts(m, "# Path Client Start-time\n#\tStats\n");
		else
			seq_puts(m, "# Path Client(Flags) # IPs\n");
		return 0;
	}

	if (cache_check_rcu(cd, &exp->h, NULL))
		return 0;

	return svc_export_show(m, cd, cp);
}

const struct seq_operations nfs_exports_op = {
	.start	= cache_seq_start_rcu,
	.next	= cache_seq_next_rcu,
	.stop	= cache_seq_stop_rcu,
	.show	= e_show,
};

/*
 * Initialize the exports module.
 */
int
nfsd_export_init(struct net *net)
{
	int rv;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	dprintk("nfsd: initializing export module (net: %x).\n", net->ns.inum);

	nn->svc_export_cache = cache_create_net(&svc_export_cache_template, net);
	if (IS_ERR(nn->svc_export_cache))
		return PTR_ERR(nn->svc_export_cache);
	rv = cache_register_net(nn->svc_export_cache, net);
	if (rv)
		goto destroy_export_cache;

	nn->svc_expkey_cache = cache_create_net(&svc_expkey_cache_template, net);
	if (IS_ERR(nn->svc_expkey_cache)) {
		rv = PTR_ERR(nn->svc_expkey_cache);
		goto unregister_export_cache;
	}
	rv = cache_register_net(nn->svc_expkey_cache, net);
	if (rv)
		goto destroy_expkey_cache;
	return 0;

destroy_expkey_cache:
	cache_destroy_net(nn->svc_expkey_cache, net);
unregister_export_cache:
	cache_unregister_net(nn->svc_export_cache, net);
destroy_export_cache:
	cache_destroy_net(nn->svc_export_cache, net);
	return rv;
}

/*
 * Flush exports table - called when last nfsd thread is killed
 */
void
nfsd_export_flush(struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	cache_purge(nn->svc_expkey_cache);
	cache_purge(nn->svc_export_cache);
}

/*
 * Shutdown the exports module.
 */
void
nfsd_export_shutdown(struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	dprintk("nfsd: shutting down export module (net: %x).\n", net->ns.inum);

	cache_unregister_net(nn->svc_expkey_cache, net);
	cache_unregister_net(nn->svc_export_cache, net);
	cache_destroy_net(nn->svc_expkey_cache, net);
	cache_destroy_net(nn->svc_export_cache, net);
	svcauth_unix_purge(net);

	dprintk("nfsd: export shutdown complete (net: %x).\n", net->ns.inum);
}
