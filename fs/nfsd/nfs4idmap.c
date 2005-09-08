/*
 *  fs/nfsd/nfs4idmap.c
 *
 *  Mapping of UID/GIDs to name and vice versa.
 *
 *  Copyright (c) 2002, 2003 The Regents of the University of
 *  Michigan.  All rights reserved.
 *
 *  Marius Aamodt Eriksen <marius@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/mm.h>
#include <linux/utsname.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/smp_lock.h>
#include <linux/sunrpc/cache.h>
#include <linux/nfsd_idmap.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/seq_file.h>
#include <linux/sunrpc/svcauth.h>

/*
 * Cache entry
 */

/*
 * XXX we know that IDMAP_NAMESZ < PAGE_SIZE, but it's ugly to rely on
 * that.
 */

#define IDMAP_TYPE_USER  0
#define IDMAP_TYPE_GROUP 1

struct ent {
	struct cache_head h;
	int               type;		       /* User / Group */
	uid_t             id;
	char              name[IDMAP_NAMESZ];
	char              authname[IDMAP_NAMESZ];
};

#define DefineSimpleCacheLookupMap(STRUCT, FUNC)			\
        DefineCacheLookup(struct STRUCT, h, FUNC##_lookup,		\
        (struct STRUCT *item, int set), /*no setup */,			\
	& FUNC##_cache, FUNC##_hash(item), FUNC##_match(item, tmp),	\
	STRUCT##_init(new, item), STRUCT##_update(tmp, item), 0)

/* Common entry handling */

#define ENT_HASHBITS          8
#define ENT_HASHMAX           (1 << ENT_HASHBITS)
#define ENT_HASHMASK          (ENT_HASHMAX - 1)

static inline void
ent_init(struct ent *new, struct ent *itm)
{
	new->id = itm->id;
	new->type = itm->type;

	strlcpy(new->name, itm->name, sizeof(new->name));
	strlcpy(new->authname, itm->authname, sizeof(new->name));
}

static inline void
ent_update(struct ent *new, struct ent *itm)
{
	ent_init(new, itm);
}

static void
ent_put(struct cache_head *ch, struct cache_detail *cd)
{
	if (cache_put(ch, cd)) {
		struct ent *map = container_of(ch, struct ent, h);
		kfree(map);
	}
}

/*
 * ID -> Name cache
 */

static struct cache_head *idtoname_table[ENT_HASHMAX];

static uint32_t
idtoname_hash(struct ent *ent)
{
	uint32_t hash;

	hash = hash_str(ent->authname, ENT_HASHBITS);
	hash = hash_long(hash ^ ent->id, ENT_HASHBITS);

	/* Flip LSB for user/group */
	if (ent->type == IDMAP_TYPE_GROUP)
		hash ^= 1;

	return hash;
}

static void
idtoname_request(struct cache_detail *cd, struct cache_head *ch, char **bpp,
    int *blen)
{
 	struct ent *ent = container_of(ch, struct ent, h);
	char idstr[11];

	qword_add(bpp, blen, ent->authname);
	snprintf(idstr, sizeof(idstr), "%d", ent->id);
	qword_add(bpp, blen, ent->type == IDMAP_TYPE_GROUP ? "group" : "user");
	qword_add(bpp, blen, idstr);

	(*bpp)[-1] = '\n';
}

static inline int
idtoname_match(struct ent *a, struct ent *b)
{
	return (a->id == b->id && a->type == b->type &&
	    strcmp(a->authname, b->authname) == 0);
}

static int
idtoname_show(struct seq_file *m, struct cache_detail *cd, struct cache_head *h)
{
	struct ent *ent;

	if (h == NULL) {
		seq_puts(m, "#domain type id [name]\n");
		return 0;
	}
	ent = container_of(h, struct ent, h);
	seq_printf(m, "%s %s %d", ent->authname,
			ent->type == IDMAP_TYPE_GROUP ? "group" : "user",
			ent->id);
	if (test_bit(CACHE_VALID, &h->flags))
		seq_printf(m, " %s", ent->name);
	seq_printf(m, "\n");
	return 0;
}

static void
warn_no_idmapd(struct cache_detail *detail)
{
	printk("nfsd: nfsv4 idmapping failing: has idmapd %s?\n",
			detail->last_close? "died" : "not been started");
}


static int         idtoname_parse(struct cache_detail *, char *, int);
static struct ent *idtoname_lookup(struct ent *, int);

static struct cache_detail idtoname_cache = {
	.owner		= THIS_MODULE,
	.hash_size	= ENT_HASHMAX,
	.hash_table	= idtoname_table,
	.name		= "nfs4.idtoname",
	.cache_put	= ent_put,
	.cache_request	= idtoname_request,
	.cache_parse	= idtoname_parse,
	.cache_show	= idtoname_show,
	.warn_no_listener = warn_no_idmapd,
};

int
idtoname_parse(struct cache_detail *cd, char *buf, int buflen)
{
	struct ent ent, *res;
	char *buf1, *bp;
	int error = -EINVAL;

	if (buf[buflen - 1] != '\n')
		return (-EINVAL);
	buf[buflen - 1]= '\0';

	buf1 = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (buf1 == NULL)
		return (-ENOMEM);

	memset(&ent, 0, sizeof(ent));

	/* Authentication name */
	if (qword_get(&buf, buf1, PAGE_SIZE) <= 0)
		goto out;
	memcpy(ent.authname, buf1, sizeof(ent.authname));

	/* Type */
	if (qword_get(&buf, buf1, PAGE_SIZE) <= 0)
		goto out;
	ent.type = strcmp(buf1, "user") == 0 ?
		IDMAP_TYPE_USER : IDMAP_TYPE_GROUP;

	/* ID */
	if (qword_get(&buf, buf1, PAGE_SIZE) <= 0)
		goto out;
	ent.id = simple_strtoul(buf1, &bp, 10);
	if (bp == buf1)
		goto out;

	/* expiry */
	ent.h.expiry_time = get_expiry(&buf);
	if (ent.h.expiry_time == 0)
		goto out;

	/* Name */
	error = qword_get(&buf, buf1, PAGE_SIZE);
	if (error == -EINVAL)
		goto out;
	if (error == -ENOENT)
		set_bit(CACHE_NEGATIVE, &ent.h.flags);
	else {
		if (error >= IDMAP_NAMESZ) {
			error = -EINVAL;
			goto out;
		}
		memcpy(ent.name, buf1, sizeof(ent.name));
	}
	error = -ENOMEM;
	if ((res = idtoname_lookup(&ent, 1)) == NULL)
		goto out;

	ent_put(&res->h, &idtoname_cache);

	error = 0;
out:
	kfree(buf1);

	return error;
}

static DefineSimpleCacheLookupMap(ent, idtoname);

/*
 * Name -> ID cache
 */

static struct cache_head *nametoid_table[ENT_HASHMAX];

static inline int
nametoid_hash(struct ent *ent)
{
	return hash_str(ent->name, ENT_HASHBITS);
}

static void
nametoid_request(struct cache_detail *cd, struct cache_head *ch, char **bpp,
    int *blen)
{
 	struct ent *ent = container_of(ch, struct ent, h);

	qword_add(bpp, blen, ent->authname);
	qword_add(bpp, blen, ent->type == IDMAP_TYPE_GROUP ? "group" : "user");
	qword_add(bpp, blen, ent->name);

	(*bpp)[-1] = '\n';
}

static inline int
nametoid_match(struct ent *a, struct ent *b)
{
	return (a->type == b->type && strcmp(a->name, b->name) == 0 &&
	    strcmp(a->authname, b->authname) == 0);
}

static int
nametoid_show(struct seq_file *m, struct cache_detail *cd, struct cache_head *h)
{
	struct ent *ent;

	if (h == NULL) {
		seq_puts(m, "#domain type name [id]\n");
		return 0;
	}
	ent = container_of(h, struct ent, h);
	seq_printf(m, "%s %s %s", ent->authname,
			ent->type == IDMAP_TYPE_GROUP ? "group" : "user",
			ent->name);
	if (test_bit(CACHE_VALID, &h->flags))
		seq_printf(m, " %d", ent->id);
	seq_printf(m, "\n");
	return 0;
}

static struct ent *nametoid_lookup(struct ent *, int);
static int         nametoid_parse(struct cache_detail *, char *, int);

static struct cache_detail nametoid_cache = {
	.owner		= THIS_MODULE,
	.hash_size	= ENT_HASHMAX,
	.hash_table	= nametoid_table,
	.name		= "nfs4.nametoid",
	.cache_put	= ent_put,
	.cache_request	= nametoid_request,
	.cache_parse	= nametoid_parse,
	.cache_show	= nametoid_show,
	.warn_no_listener = warn_no_idmapd,
};

static int
nametoid_parse(struct cache_detail *cd, char *buf, int buflen)
{
	struct ent ent, *res;
	char *buf1;
	int error = -EINVAL;

	if (buf[buflen - 1] != '\n')
		return (-EINVAL);
	buf[buflen - 1]= '\0';

	buf1 = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (buf1 == NULL)
		return (-ENOMEM);

	memset(&ent, 0, sizeof(ent));

	/* Authentication name */
	if (qword_get(&buf, buf1, PAGE_SIZE) <= 0)
		goto out;
	memcpy(ent.authname, buf1, sizeof(ent.authname));

	/* Type */
	if (qword_get(&buf, buf1, PAGE_SIZE) <= 0)
		goto out;
	ent.type = strcmp(buf1, "user") == 0 ?
		IDMAP_TYPE_USER : IDMAP_TYPE_GROUP;

	/* Name */
	error = qword_get(&buf, buf1, PAGE_SIZE);
	if (error <= 0 || error >= IDMAP_NAMESZ)
		goto out;
	memcpy(ent.name, buf1, sizeof(ent.name));

	/* expiry */
	ent.h.expiry_time = get_expiry(&buf);
	if (ent.h.expiry_time == 0)
		goto out;

	/* ID */
	error = get_int(&buf, &ent.id);
	if (error == -EINVAL)
		goto out;
	if (error == -ENOENT)
		set_bit(CACHE_NEGATIVE, &ent.h.flags);

	error = -ENOMEM;
	if ((res = nametoid_lookup(&ent, 1)) == NULL)
		goto out;

	ent_put(&res->h, &nametoid_cache);
	error = 0;
out:
	kfree(buf1);

	return (error);
}

static DefineSimpleCacheLookupMap(ent, nametoid);

/*
 * Exported API
 */

void
nfsd_idmap_init(void)
{
	cache_register(&idtoname_cache);
	cache_register(&nametoid_cache);
}

void
nfsd_idmap_shutdown(void)
{
	if (cache_unregister(&idtoname_cache))
		printk(KERN_ERR "nfsd: failed to unregister idtoname cache\n");
	if (cache_unregister(&nametoid_cache))
		printk(KERN_ERR "nfsd: failed to unregister nametoid cache\n");
}

/*
 * Deferred request handling
 */

struct idmap_defer_req {
       struct cache_req		req;
       struct cache_deferred_req deferred_req;
       wait_queue_head_t	waitq;
       atomic_t			count;
};

static inline void
put_mdr(struct idmap_defer_req *mdr)
{
	if (atomic_dec_and_test(&mdr->count))
		kfree(mdr);
}

static inline void
get_mdr(struct idmap_defer_req *mdr)
{
	atomic_inc(&mdr->count);
}

static void
idmap_revisit(struct cache_deferred_req *dreq, int toomany)
{
	struct idmap_defer_req *mdr =
		container_of(dreq, struct idmap_defer_req, deferred_req);

	wake_up(&mdr->waitq);
	put_mdr(mdr);
}

static struct cache_deferred_req *
idmap_defer(struct cache_req *req)
{
	struct idmap_defer_req *mdr =
		container_of(req, struct idmap_defer_req, req);

	mdr->deferred_req.revisit = idmap_revisit;
	get_mdr(mdr);
	return (&mdr->deferred_req);
}

static inline int
do_idmap_lookup(struct ent *(*lookup_fn)(struct ent *, int), struct ent *key,
		struct cache_detail *detail, struct ent **item,
		struct idmap_defer_req *mdr)
{
	*item = lookup_fn(key, 0);
	if (!*item)
		return -ENOMEM;
	return cache_check(detail, &(*item)->h, &mdr->req);
}

static inline int
do_idmap_lookup_nowait(struct ent *(*lookup_fn)(struct ent *, int),
			struct ent *key, struct cache_detail *detail,
			struct ent **item)
{
	int ret = -ENOMEM;

	*item = lookup_fn(key, 0);
	if (!*item)
		goto out_err;
	ret = -ETIMEDOUT;
	if (!test_bit(CACHE_VALID, &(*item)->h.flags)
			|| (*item)->h.expiry_time < get_seconds()
			|| detail->flush_time > (*item)->h.last_refresh)
		goto out_put;
	ret = -ENOENT;
	if (test_bit(CACHE_NEGATIVE, &(*item)->h.flags))
		goto out_put;
	return 0;
out_put:
	ent_put(&(*item)->h, detail);
out_err:
	*item = NULL;
	return ret;
}

static int
idmap_lookup(struct svc_rqst *rqstp,
		struct ent *(*lookup_fn)(struct ent *, int), struct ent *key,
		struct cache_detail *detail, struct ent **item)
{
	struct idmap_defer_req *mdr;
	int ret;

	mdr = kmalloc(sizeof(*mdr), GFP_KERNEL);
	if (!mdr)
		return -ENOMEM;
	memset(mdr, 0, sizeof(*mdr));
	atomic_set(&mdr->count, 1);
	init_waitqueue_head(&mdr->waitq);
	mdr->req.defer = idmap_defer;
	ret = do_idmap_lookup(lookup_fn, key, detail, item, mdr);
	if (ret == -EAGAIN) {
		wait_event_interruptible_timeout(mdr->waitq,
			test_bit(CACHE_VALID, &(*item)->h.flags), 1 * HZ);
		ret = do_idmap_lookup_nowait(lookup_fn, key, detail, item);
	}
	put_mdr(mdr);
	return ret;
}

static int
idmap_name_to_id(struct svc_rqst *rqstp, int type, const char *name, u32 namelen,
		uid_t *id)
{
	struct ent *item, key = {
		.type = type,
	};
	int ret;

	if (namelen + 1 > sizeof(key.name))
		return -EINVAL;
	memcpy(key.name, name, namelen);
	key.name[namelen] = '\0';
	strlcpy(key.authname, rqstp->rq_client->name, sizeof(key.authname));
	ret = idmap_lookup(rqstp, nametoid_lookup, &key, &nametoid_cache, &item);
	if (ret == -ENOENT)
		ret = -ESRCH; /* nfserr_badname */
	if (ret)
		return ret;
	*id = item->id;
	ent_put(&item->h, &nametoid_cache);
	return 0;
}

static int
idmap_id_to_name(struct svc_rqst *rqstp, int type, uid_t id, char *name)
{
	struct ent *item, key = {
		.id = id,
		.type = type,
	};
	int ret;

	strlcpy(key.authname, rqstp->rq_client->name, sizeof(key.authname));
	ret = idmap_lookup(rqstp, idtoname_lookup, &key, &idtoname_cache, &item);
	if (ret == -ENOENT)
		return sprintf(name, "%u", id);
	if (ret)
		return ret;
	ret = strlen(item->name);
	BUG_ON(ret > IDMAP_NAMESZ);
	memcpy(name, item->name, ret);
	ent_put(&item->h, &idtoname_cache);
	return ret;
}

int
nfsd_map_name_to_uid(struct svc_rqst *rqstp, const char *name, size_t namelen,
		__u32 *id)
{
	return idmap_name_to_id(rqstp, IDMAP_TYPE_USER, name, namelen, id);
}

int
nfsd_map_name_to_gid(struct svc_rqst *rqstp, const char *name, size_t namelen,
		__u32 *id)
{
	return idmap_name_to_id(rqstp, IDMAP_TYPE_GROUP, name, namelen, id);
}

int
nfsd_map_uid_to_name(struct svc_rqst *rqstp, __u32 id, char *name)
{
	return idmap_id_to_name(rqstp, IDMAP_TYPE_USER, id, name);
}

int
nfsd_map_gid_to_name(struct svc_rqst *rqstp, __u32 id, char *name)
{
	return idmap_id_to_name(rqstp, IDMAP_TYPE_GROUP, id, name);
}
