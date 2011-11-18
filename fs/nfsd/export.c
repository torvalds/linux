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

#include <net/ipv6.h>

#include "nfsd.h"
#include "nfsfh.h"

#define NFSDDBG_FACILITY	NFSDDBG_EXPORT

typedef struct auth_domain	svc_client;
typedef struct svc_export	svc_export;

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
static struct cache_head *expkey_table[EXPKEY_HASHMAX];

static void expkey_put(struct kref *ref)
{
	struct svc_expkey *key = container_of(ref, struct svc_expkey, h.ref);

	if (test_bit(CACHE_VALID, &key->h.flags) &&
	    !test_bit(CACHE_NEGATIVE, &key->h.flags))
		path_put(&key->ek_path);
	auth_domain_put(key->ek_client);
	kfree(key);
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

static int expkey_upcall(struct cache_detail *cd, struct cache_head *h)
{
	return sunrpc_cache_pipe_upcall(cd, h, expkey_request);
}

static struct svc_expkey *svc_expkey_update(struct svc_expkey *new, struct svc_expkey *old);
static struct svc_expkey *svc_expkey_lookup(struct svc_expkey *);
static struct cache_detail svc_expkey_cache;

static int expkey_parse(struct cache_detail *cd, char *mesg, int mlen)
{
	/* client fsidtype fsid [path] */
	char *buf;
	int len;
	struct auth_domain *dom = NULL;
	int err;
	int fsidtype;
	char *ep;
	struct svc_expkey key;
	struct svc_expkey *ek = NULL;

	if (mlen < 1 || mesg[mlen-1] != '\n')
		return -EINVAL;
	mesg[mlen-1] = 0;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	err = -ENOMEM;
	if (!buf)
		goto out;

	err = -EINVAL;
	if ((len=qword_get(&mesg, buf, PAGE_SIZE)) <= 0)
		goto out;

	err = -ENOENT;
	dom = auth_domain_find(buf);
	if (!dom)
		goto out;
	dprintk("found domain %s\n", buf);

	err = -EINVAL;
	if ((len=qword_get(&mesg, buf, PAGE_SIZE)) <= 0)
		goto out;
	fsidtype = simple_strtoul(buf, &ep, 10);
	if (*ep)
		goto out;
	dprintk("found fsidtype %d\n", fsidtype);
	if (key_len(fsidtype)==0) /* invalid type */
		goto out;
	if ((len=qword_get(&mesg, buf, PAGE_SIZE)) <= 0)
		goto out;
	dprintk("found fsid length %d\n", len);
	if (len != key_len(fsidtype))
		goto out;

	/* OK, we seem to have a valid key */
	key.h.flags = 0;
	key.h.expiry_time = get_expiry(&mesg);
	if (key.h.expiry_time == 0)
		goto out;

	key.ek_client = dom;	
	key.ek_fsidtype = fsidtype;
	memcpy(key.ek_fsid, buf, len);

	ek = svc_expkey_lookup(&key);
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
		ek = svc_expkey_update(&key, ek);
		if (!ek)
			err = -ENOMEM;
	} else {
		err = kern_path(buf, 0, &key.ek_path);
		if (err)
			goto out;

		dprintk("Found the path %s\n", buf);

		ek = svc_expkey_update(&key, ek);
		if (!ek)
			err = -ENOMEM;
		path_put(&key.ek_path);
	}
	cache_flush();
 out:
	if (ek)
		cache_put(&ek->h, &svc_expkey_cache);
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
	struct svc_expkey *i = kmalloc(sizeof(*i), GFP_KERNEL);
	if (i)
		return &i->h;
	else
		return NULL;
}

static struct cache_detail svc_expkey_cache = {
	.owner		= THIS_MODULE,
	.hash_size	= EXPKEY_HASHMAX,
	.hash_table	= expkey_table,
	.name		= "nfsd.fh",
	.cache_put	= expkey_put,
	.cache_upcall	= expkey_upcall,
	.cache_parse	= expkey_parse,
	.cache_show	= expkey_show,
	.match		= expkey_match,
	.init		= expkey_init,
	.update       	= expkey_update,
	.alloc		= expkey_alloc,
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
svc_expkey_lookup(struct svc_expkey *item)
{
	struct cache_head *ch;
	int hash = svc_expkey_hash(item);

	ch = sunrpc_cache_lookup(&svc_expkey_cache, &item->h,
				 hash);
	if (ch)
		return container_of(ch, struct svc_expkey, h);
	else
		return NULL;
}

static struct svc_expkey *
svc_expkey_update(struct svc_expkey *new, struct svc_expkey *old)
{
	struct cache_head *ch;
	int hash = svc_expkey_hash(new);

	ch = sunrpc_cache_update(&svc_expkey_cache, &new->h,
				 &old->h, hash);
	if (ch)
		return container_of(ch, struct svc_expkey, h);
	else
		return NULL;
}


#define	EXPORT_HASHBITS		8
#define	EXPORT_HASHMAX		(1<< EXPORT_HASHBITS)

static struct cache_head *export_table[EXPORT_HASHMAX];

static void nfsd4_fslocs_free(struct nfsd4_fs_locations *fsloc)
{
	int i;

	for (i = 0; i < fsloc->locations_count; i++) {
		kfree(fsloc->locations[i].path);
		kfree(fsloc->locations[i].hosts);
	}
	kfree(fsloc->locations);
}

static void svc_export_put(struct kref *ref)
{
	struct svc_export *exp = container_of(ref, struct svc_export, h.ref);
	path_put(&exp->ex_path);
	auth_domain_put(exp->ex_client);
	nfsd4_fslocs_free(&exp->ex_fslocs);
	kfree(exp);
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

static int svc_export_upcall(struct cache_detail *cd, struct cache_head *h)
{
	return sunrpc_cache_pipe_upcall(cd, h, svc_export_request);
}

static struct svc_export *svc_export_update(struct svc_export *new,
					    struct svc_export *old);
static struct svc_export *svc_export_lookup(struct svc_export *);

static int check_export(struct inode *inode, int *flags, unsigned char *uuid)
{

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
	 *       This means that s_export_op must be set.
	 */
	if (!(inode->i_sb->s_type->fs_flags & FS_REQUIRES_DEV) &&
	    !(*flags & NFSEXP_FSID) &&
	    uuid == NULL) {
		dprintk("exp_export: export of non-dev fs without fsid\n");
		return -EINVAL;
	}

	if (!inode->i_sb->s_export_op ||
	    !inode->i_sb->s_export_op->fh_to_dentry) {
		dprintk("exp_export: export of invalid fs type.\n");
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

	/* listsize */
	err = get_int(mesg, &fsloc->locations_count);
	if (err)
		return err;
	if (fsloc->locations_count > MAX_FS_LOCATIONS)
		return -EINVAL;
	if (fsloc->locations_count == 0)
		return 0;

	fsloc->locations = kzalloc(fsloc->locations_count
			* sizeof(struct nfsd4_fs_location), GFP_KERNEL);
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
	int listsize, err;
	struct exp_flavor_info *f;

	err = get_int(mesg, &listsize);
	if (err)
		return err;
	if (listsize < 0 || listsize > MAX_SECINFO_LIST)
		return -EINVAL;

	for (f = exp->ex_flavors; f < exp->ex_flavors + listsize; f++) {
		err = get_int(mesg, &f->pseudoflavor);
		if (err)
			return err;
		/*
		 * XXX: It would be nice to also check whether this
		 * pseudoflavor is supported, so we can discover the
		 * problem at export time instead of when a client fails
		 * to authenticate.
		 */
		err = get_int(mesg, &f->flags);
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

static int svc_export_parse(struct cache_detail *cd, char *mesg, int mlen)
{
	/* client path expiry [flags anonuid anongid fsid] */
	char *buf;
	int len;
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
	len = qword_get(&mesg, buf, PAGE_SIZE);
	if (len <= 0)
		goto out;

	err = -ENOENT;
	dom = auth_domain_find(buf);
	if (!dom)
		goto out;

	/* path */
	err = -EINVAL;
	if ((len = qword_get(&mesg, buf, PAGE_SIZE)) <= 0)
		goto out1;

	err = kern_path(buf, 0, &exp.ex_path);
	if (err)
		goto out1;

	exp.ex_client = dom;

	/* expiry */
	err = -EINVAL;
	exp.h.expiry_time = get_expiry(&mesg);
	if (exp.h.expiry_time == 0)
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
		exp.ex_anon_uid= an_int;

		/* anon gid */
		err = get_int(&mesg, &an_int);
		if (err)
			goto out3;
		exp.ex_anon_gid= an_int;

		/* fsid */
		err = get_int(&mesg, &an_int);
		if (err)
			goto out3;
		exp.ex_fsid = an_int;

		while ((len = qword_get(&mesg, buf, PAGE_SIZE)) > 0) {
			if (strcmp(buf, "fsloc") == 0)
				err = fsloc_parse(&mesg, buf, &exp.ex_fslocs);
			else if (strcmp(buf, "uuid") == 0) {
				/* expect a 16 byte uuid encoded as \xXXXX... */
				len = qword_get(&mesg, buf, PAGE_SIZE);
				if (len != 16)
					err  = -EINVAL;
				else {
					exp.ex_uuid =
						kmemdup(buf, 16, GFP_KERNEL);
					if (exp.ex_uuid == NULL)
						err = -ENOMEM;
				}
			} else if (strcmp(buf, "secinfo") == 0)
				err = secinfo_parse(&mesg, buf, &exp);
			else
				/* quietly ignore unknown words and anything
				 * following. Newer user-space can try to set
				 * new values, then see what the result was.
				 */
				break;
			if (err)
				goto out4;
		}

		err = check_export(exp.ex_path.dentry->d_inode, &exp.ex_flags,
				   exp.ex_uuid);
		if (err)
			goto out4;
	}

	expp = svc_export_lookup(&exp);
	if (expp)
		expp = svc_export_update(&exp, expp);
	else
		err = -ENOMEM;
	cache_flush();
	if (expp == NULL)
		err = -ENOMEM;
	else
		exp_put(expp);
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
		uid_t anonu, uid_t anong, struct nfsd4_fs_locations *fslocs);
static void show_secinfo(struct seq_file *m, struct svc_export *exp);

static int svc_export_show(struct seq_file *m,
			   struct cache_detail *cd,
			   struct cache_head *h)
{
	struct svc_export *exp ;

	if (h ==NULL) {
		seq_puts(m, "#path domain(flags)\n");
		return 0;
	}
	exp = container_of(h, struct svc_export, h);
	seq_path(m, &exp->ex_path, " \t\n\\");
	seq_putc(m, '\t');
	seq_escape(m, exp->ex_client->name, " \t\n\\");
	seq_putc(m, '(');
	if (test_bit(CACHE_VALID, &h->flags) && 
	    !test_bit(CACHE_NEGATIVE, &h->flags)) {
		exp_flags(m, exp->ex_flags, exp->ex_fsid,
			  exp->ex_anon_uid, exp->ex_anon_gid, &exp->ex_fslocs);
		if (exp->ex_uuid) {
			int i;
			seq_puts(m, ",uuid=");
			for (i=0; i<16; i++) {
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
		orig->ex_path.dentry == new->ex_path.dentry &&
		orig->ex_path.mnt == new->ex_path.mnt;
}

static void svc_export_init(struct cache_head *cnew, struct cache_head *citem)
{
	struct svc_export *new = container_of(cnew, struct svc_export, h);
	struct svc_export *item = container_of(citem, struct svc_export, h);

	kref_get(&item->ex_client->ref);
	new->ex_client = item->ex_client;
	new->ex_path.dentry = dget(item->ex_path.dentry);
	new->ex_path.mnt = mntget(item->ex_path.mnt);
	new->ex_fslocs.locations = NULL;
	new->ex_fslocs.locations_count = 0;
	new->ex_fslocs.migrated = 0;
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
	new->ex_uuid = item->ex_uuid;
	item->ex_uuid = NULL;
	new->ex_fslocs.locations = item->ex_fslocs.locations;
	item->ex_fslocs.locations = NULL;
	new->ex_fslocs.locations_count = item->ex_fslocs.locations_count;
	item->ex_fslocs.locations_count = 0;
	new->ex_fslocs.migrated = item->ex_fslocs.migrated;
	item->ex_fslocs.migrated = 0;
	new->ex_nflavors = item->ex_nflavors;
	for (i = 0; i < MAX_SECINFO_LIST; i++) {
		new->ex_flavors[i] = item->ex_flavors[i];
	}
}

static struct cache_head *svc_export_alloc(void)
{
	struct svc_export *i = kmalloc(sizeof(*i), GFP_KERNEL);
	if (i)
		return &i->h;
	else
		return NULL;
}

struct cache_detail svc_export_cache = {
	.owner		= THIS_MODULE,
	.hash_size	= EXPORT_HASHMAX,
	.hash_table	= export_table,
	.name		= "nfsd.export",
	.cache_put	= svc_export_put,
	.cache_upcall	= svc_export_upcall,
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

	ch = sunrpc_cache_lookup(&svc_export_cache, &exp->h,
				 hash);
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

	ch = sunrpc_cache_update(&svc_export_cache, &new->h,
				 &old->h,
				 hash);
	if (ch)
		return container_of(ch, struct svc_export, h);
	else
		return NULL;
}


static struct svc_expkey *
exp_find_key(svc_client *clp, int fsid_type, u32 *fsidv, struct cache_req *reqp)
{
	struct svc_expkey key, *ek;
	int err;
	
	if (!clp)
		return ERR_PTR(-ENOENT);

	key.ek_client = clp;
	key.ek_fsidtype = fsid_type;
	memcpy(key.ek_fsid, fsidv, key_len(fsid_type));

	ek = svc_expkey_lookup(&key);
	if (ek == NULL)
		return ERR_PTR(-ENOMEM);
	err = cache_check(&svc_expkey_cache, &ek->h, reqp);
	if (err)
		return ERR_PTR(err);
	return ek;
}


static svc_export *exp_get_by_name(svc_client *clp, const struct path *path,
				     struct cache_req *reqp)
{
	struct svc_export *exp, key;
	int err;

	if (!clp)
		return ERR_PTR(-ENOENT);

	key.ex_client = clp;
	key.ex_path = *path;

	exp = svc_export_lookup(&key);
	if (exp == NULL)
		return ERR_PTR(-ENOMEM);
	err = cache_check(&svc_export_cache, &exp->h, reqp);
	if (err)
		return ERR_PTR(err);
	return exp;
}

/*
 * Find the export entry for a given dentry.
 */
static struct svc_export *exp_parent(svc_client *clp, struct path *path)
{
	struct dentry *saved = dget(path->dentry);
	svc_export *exp = exp_get_by_name(clp, path, NULL);

	while (PTR_ERR(exp) == -ENOENT && !IS_ROOT(path->dentry)) {
		struct dentry *parent = dget_parent(path->dentry);
		dput(path->dentry);
		path->dentry = parent;
		exp = exp_get_by_name(clp, path, NULL);
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
exp_rootfh(svc_client *clp, char *name, struct knfsd_fh *f, int maxsize)
{
	struct svc_export	*exp;
	struct path		path;
	struct inode		*inode;
	struct svc_fh		fh;
	int			err;

	err = -EPERM;
	/* NB: we probably ought to check that it's NUL-terminated */
	if (kern_path(name, 0, &path)) {
		printk("nfsd: exp_rootfh path not found %s", name);
		return err;
	}
	inode = path.dentry->d_inode;

	dprintk("nfsd: exp_rootfh(%s [%p] %s:%s/%ld)\n",
		 name, path.dentry, clp->name,
		 inode->i_sb->s_id, inode->i_ino);
	exp = exp_parent(clp, &path);
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

static struct svc_export *exp_find(struct auth_domain *clp, int fsid_type,
				   u32 *fsidv, struct cache_req *reqp)
{
	struct svc_export *exp;
	struct svc_expkey *ek = exp_find_key(clp, fsid_type, fsidv, reqp);
	if (IS_ERR(ek))
		return ERR_CAST(ek);

	exp = exp_get_by_name(clp, &ek->ek_path, reqp);
	cache_put(&ek->h, &svc_expkey_cache);

	if (IS_ERR(exp))
		return ERR_CAST(exp);
	return exp;
}

__be32 check_nfsd_access(struct svc_export *exp, struct svc_rqst *rqstp)
{
	struct exp_flavor_info *f;
	struct exp_flavor_info *end = exp->ex_flavors + exp->ex_nflavors;

	/* legacy gss-only clients are always OK: */
	if (exp->ex_client == rqstp->rq_gssclient)
		return 0;
	/* ip-address based client; check sec= export option: */
	for (f = exp->ex_flavors; f < end; f++) {
		if (f->pseudoflavor == rqstp->rq_flavor)
			return 0;
	}
	/* defaults in absence of sec= options: */
	if (exp->ex_nflavors == 0) {
		if (rqstp->rq_flavor == RPC_AUTH_NULL ||
		    rqstp->rq_flavor == RPC_AUTH_UNIX)
			return 0;
	}
	return nfserr_wrongsec;
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
rqst_exp_get_by_name(struct svc_rqst *rqstp, struct path *path)
{
	struct svc_export *gssexp, *exp = ERR_PTR(-ENOENT);

	if (rqstp->rq_client == NULL)
		goto gss;

	/* First try the auth_unix client: */
	exp = exp_get_by_name(rqstp->rq_client, path, &rqstp->rq_chandle);
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
	gssexp = exp_get_by_name(rqstp->rq_gssclient, path, &rqstp->rq_chandle);
	if (PTR_ERR(gssexp) == -ENOENT)
		return exp;
	if (!IS_ERR(exp))
		exp_put(exp);
	return gssexp;
}

struct svc_export *
rqst_exp_find(struct svc_rqst *rqstp, int fsid_type, u32 *fsidv)
{
	struct svc_export *gssexp, *exp = ERR_PTR(-ENOENT);

	if (rqstp->rq_client == NULL)
		goto gss;

	/* First try the auth_unix client: */
	exp = exp_find(rqstp->rq_client, fsid_type, fsidv, &rqstp->rq_chandle);
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
	gssexp = exp_find(rqstp->rq_gssclient, fsid_type, fsidv,
						&rqstp->rq_chandle);
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

	return rqst_exp_find(rqstp, FSID_NUM, fsidv);
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

/* Iterator */

static void *e_start(struct seq_file *m, loff_t *pos)
	__acquires(svc_export_cache.hash_lock)
{
	loff_t n = *pos;
	unsigned hash, export;
	struct cache_head *ch;
	
	read_lock(&svc_export_cache.hash_lock);
	if (!n--)
		return SEQ_START_TOKEN;
	hash = n >> 32;
	export = n & ((1LL<<32) - 1);

	
	for (ch=export_table[hash]; ch; ch=ch->next)
		if (!export--)
			return ch;
	n &= ~((1LL<<32) - 1);
	do {
		hash++;
		n += 1LL<<32;
	} while(hash < EXPORT_HASHMAX && export_table[hash]==NULL);
	if (hash >= EXPORT_HASHMAX)
		return NULL;
	*pos = n+1;
	return export_table[hash];
}

static void *e_next(struct seq_file *m, void *p, loff_t *pos)
{
	struct cache_head *ch = p;
	int hash = (*pos >> 32);

	if (p == SEQ_START_TOKEN)
		hash = 0;
	else if (ch->next == NULL) {
		hash++;
		*pos += 1LL<<32;
	} else {
		++*pos;
		return ch->next;
	}
	*pos &= ~((1LL<<32) - 1);
	while (hash < EXPORT_HASHMAX && export_table[hash] == NULL) {
		hash++;
		*pos += 1LL<<32;
	}
	if (hash >= EXPORT_HASHMAX)
		return NULL;
	++*pos;
	return export_table[hash];
}

static void e_stop(struct seq_file *m, void *p)
	__releases(svc_export_cache.hash_lock)
{
	read_unlock(&svc_export_cache.hash_lock);
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
	{ NFSEXP_NOHIDE, {"nohide", ""}},
	{ NFSEXP_CROSSMOUNT, {"crossmnt", ""}},
	{ NFSEXP_NOSUBTREECHECK, {"no_subtree_check", ""}},
	{ NFSEXP_NOAUTHNLM, {"insecure_locks", ""}},
	{ NFSEXP_V4ROOT, {"v4root", ""}},
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
		uid_t anonu, uid_t anong, struct nfsd4_fs_locations *fsloc)
{
	show_expflags(m, flag, NFSEXP_ALLFLAGS);
	if (flag & NFSEXP_FSID)
		seq_printf(m, ",fsid=%d", fsid);
	if (anonu != (uid_t)-2 && anonu != (0x10000-2))
		seq_printf(m, ",anonuid=%u", anonu);
	if (anong != (gid_t)-2 && anong != (0x10000-2))
		seq_printf(m, ",anongid=%u", anong);
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

	if (p == SEQ_START_TOKEN) {
		seq_puts(m, "# Version 1.1\n");
		seq_puts(m, "# Path Client(Flags) # IPs\n");
		return 0;
	}

	cache_get(&exp->h);
	if (cache_check(&svc_export_cache, &exp->h, NULL))
		return 0;
	cache_put(&exp->h, &svc_export_cache);
	return svc_export_show(m, &svc_export_cache, cp);
}

const struct seq_operations nfs_exports_op = {
	.start	= e_start,
	.next	= e_next,
	.stop	= e_stop,
	.show	= e_show,
};


/*
 * Initialize the exports module.
 */
int
nfsd_export_init(void)
{
	int rv;
	dprintk("nfsd: initializing export module.\n");

	rv = cache_register(&svc_export_cache);
	if (rv)
		return rv;
	rv = cache_register(&svc_expkey_cache);
	if (rv)
		cache_unregister(&svc_export_cache);
	return rv;

}

/*
 * Flush exports table - called when last nfsd thread is killed
 */
void
nfsd_export_flush(void)
{
	cache_purge(&svc_expkey_cache);
	cache_purge(&svc_export_cache);
}

/*
 * Shutdown the exports module.
 */
void
nfsd_export_shutdown(void)
{

	dprintk("nfsd: shutting down export module.\n");

	cache_unregister(&svc_expkey_cache);
	cache_unregister(&svc_export_cache);
	svcauth_unix_purge();

	dprintk("nfsd: export shutdown complete.\n");
}
