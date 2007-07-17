#define MSNFS	/* HACK HACK */
/*
 * linux/fs/nfsd/export.c
 *
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

#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/in.h>
#include <linux/seq_file.h>
#include <linux/syscalls.h>
#include <linux/rwsem.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/hash.h>
#include <linux/module.h>
#include <linux/exportfs.h>

#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/nfsfh.h>
#include <linux/nfsd/syscall.h>
#include <linux/lockd/bind.h>
#include <linux/sunrpc/msg_prot.h>
#include <linux/sunrpc/gss_api.h>

#define NFSDDBG_FACILITY	NFSDDBG_EXPORT

typedef struct auth_domain	svc_client;
typedef struct svc_export	svc_export;

static void		exp_do_unexport(svc_export *unexp);
static int		exp_verify_string(char *cp, int max);

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
	    !test_bit(CACHE_NEGATIVE, &key->h.flags)) {
		dput(key->ek_dentry);
		mntput(key->ek_mnt);
	}
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
	struct svc_expkey *ek;

	if (mesg[mlen-1] != '\n')
		return -EINVAL;
	mesg[mlen-1] = 0;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	err = -ENOMEM;
	if (!buf) goto out;

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
	if ((len=qword_get(&mesg, buf, PAGE_SIZE)) < 0)
		goto out;
	dprintk("Path seems to be <%s>\n", buf);
	err = 0;
	if (len == 0) {
		set_bit(CACHE_NEGATIVE, &key.h.flags);
		ek = svc_expkey_update(&key, ek);
		if (ek)
			cache_put(&ek->h, &svc_expkey_cache);
		else err = -ENOMEM;
	} else {
		struct nameidata nd;
		err = path_lookup(buf, 0, &nd);
		if (err)
			goto out;

		dprintk("Found the path %s\n", buf);
		key.ek_mnt = nd.mnt;
		key.ek_dentry = nd.dentry;
		
		ek = svc_expkey_update(&key, ek);
		if (ek)
			cache_put(&ek->h, &svc_expkey_cache);
		else
			err = -ENOMEM;
		path_release(&nd);
	}
	cache_flush();
 out:
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
		seq_path(m, ek->ek_mnt, ek->ek_dentry, "\\ \t\n");
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

	new->ek_mnt = mntget(item->ek_mnt);
	new->ek_dentry = dget(item->ek_dentry);
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
	.cache_request	= expkey_request,
	.cache_parse	= expkey_parse,
	.cache_show	= expkey_show,
	.match		= expkey_match,
	.init		= expkey_init,
	.update       	= expkey_update,
	.alloc		= expkey_alloc,
};

static struct svc_expkey *
svc_expkey_lookup(struct svc_expkey *item)
{
	struct cache_head *ch;
	int hash = item->ek_fsidtype;
	char * cp = (char*)item->ek_fsid;
	int len = key_len(item->ek_fsidtype);

	hash ^= hash_mem(cp, len, EXPKEY_HASHBITS);
	hash ^= hash_ptr(item->ek_client, EXPKEY_HASHBITS);
	hash &= EXPKEY_HASHMASK;

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
	int hash = new->ek_fsidtype;
	char * cp = (char*)new->ek_fsid;
	int len = key_len(new->ek_fsidtype);

	hash ^= hash_mem(cp, len, EXPKEY_HASHBITS);
	hash ^= hash_ptr(new->ek_client, EXPKEY_HASHBITS);
	hash &= EXPKEY_HASHMASK;

	ch = sunrpc_cache_update(&svc_expkey_cache, &new->h,
				 &old->h, hash);
	if (ch)
		return container_of(ch, struct svc_expkey, h);
	else
		return NULL;
}


#define	EXPORT_HASHBITS		8
#define	EXPORT_HASHMAX		(1<< EXPORT_HASHBITS)
#define	EXPORT_HASHMASK		(EXPORT_HASHMAX -1)

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
	dput(exp->ex_dentry);
	mntput(exp->ex_mnt);
	auth_domain_put(exp->ex_client);
	kfree(exp->ex_path);
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
	pth = d_path(exp->ex_dentry, exp->ex_mnt, *bpp, *blen);
	if (IS_ERR(pth)) {
		/* is this correct? */
		(*bpp)[0] = '\n';
		return;
	}
	qword_add(bpp, blen, pth);
	(*bpp)[-1] = '\n';
}

static struct svc_export *svc_export_update(struct svc_export *new,
					    struct svc_export *old);
static struct svc_export *svc_export_lookup(struct svc_export *);

static int check_export(struct inode *inode, int flags, unsigned char *uuid)
{

	/* We currently export only dirs and regular files.
	 * This is what umountd does.
	 */
	if (!S_ISDIR(inode->i_mode) &&
	    !S_ISREG(inode->i_mode))
		return -ENOTDIR;

	/* There are two requirements on a filesystem to be exportable.
	 * 1:  We must be able to identify the filesystem from a number.
	 *       either a device number (so FS_REQUIRES_DEV needed)
	 *       or an FSID number (so NFSEXP_FSID or ->uuid is needed).
	 * 2:  We must be able to find an inode from a filehandle.
	 *       This means that s_export_op must be set.
	 */
	if (!(inode->i_sb->s_type->fs_flags & FS_REQUIRES_DEV) &&
	    !(flags & NFSEXP_FSID) &&
	    uuid == NULL) {
		dprintk("exp_export: export of non-dev fs without fsid\n");
		return -EINVAL;
	}
	if (!inode->i_sb->s_export_op) {
		dprintk("exp_export: export of invalid fs type.\n");
		return -EINVAL;
	}

	/* Ok, we can export it */;
	if (!inode->i_sb->s_export_op->find_exported_dentry)
		inode->i_sb->s_export_op->find_exported_dentry =
			find_exported_dentry;
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
		 * Just a quick sanity check; we could also try to check
		 * whether this pseudoflavor is supported, but at worst
		 * an unsupported pseudoflavor on the export would just
		 * be a pseudoflavor that won't match the flavor of any
		 * authenticated request.  The administrator will
		 * probably discover the problem when someone fails to
		 * authenticate.
		 */
		if (f->pseudoflavor < 0)
			return -EINVAL;
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
	struct nameidata nd;
	struct svc_export exp, *expp;
	int an_int;

	nd.dentry = NULL;
	exp.ex_path = NULL;

	/* fs locations */
	exp.ex_fslocs.locations = NULL;
	exp.ex_fslocs.locations_count = 0;
	exp.ex_fslocs.migrated = 0;

	exp.ex_uuid = NULL;

	/* secinfo */
	exp.ex_nflavors = 0;

	if (mesg[mlen-1] != '\n')
		return -EINVAL;
	mesg[mlen-1] = 0;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	err = -ENOMEM;
	if (!buf) goto out;

	/* client */
	len = qword_get(&mesg, buf, PAGE_SIZE);
	err = -EINVAL;
	if (len <= 0) goto out;

	err = -ENOENT;
	dom = auth_domain_find(buf);
	if (!dom)
		goto out;

	/* path */
	err = -EINVAL;
	if ((len=qword_get(&mesg, buf, PAGE_SIZE)) <= 0)
		goto out;
	err = path_lookup(buf, 0, &nd);
	if (err) goto out_no_path;

	exp.h.flags = 0;
	exp.ex_client = dom;
	exp.ex_mnt = nd.mnt;
	exp.ex_dentry = nd.dentry;
	exp.ex_path = kstrdup(buf, GFP_KERNEL);
	err = -ENOMEM;
	if (!exp.ex_path)
		goto out;

	/* expiry */
	err = -EINVAL;
	exp.h.expiry_time = get_expiry(&mesg);
	if (exp.h.expiry_time == 0)
		goto out;

	/* flags */
	err = get_int(&mesg, &an_int);
	if (err == -ENOENT)
		set_bit(CACHE_NEGATIVE, &exp.h.flags);
	else {
		if (err || an_int < 0) goto out;	
		exp.ex_flags= an_int;
	
		/* anon uid */
		err = get_int(&mesg, &an_int);
		if (err) goto out;
		exp.ex_anon_uid= an_int;

		/* anon gid */
		err = get_int(&mesg, &an_int);
		if (err) goto out;
		exp.ex_anon_gid= an_int;

		/* fsid */
		err = get_int(&mesg, &an_int);
		if (err) goto out;
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
				goto out;
		}

		err = check_export(nd.dentry->d_inode, exp.ex_flags,
				   exp.ex_uuid);
		if (err) goto out;
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
 out:
	nfsd4_fslocs_free(&exp.ex_fslocs);
	kfree(exp.ex_uuid);
 	kfree(exp.ex_path);
	if (nd.dentry)
		path_release(&nd);
 out_no_path:
	if (dom)
		auth_domain_put(dom);
	kfree(buf);
	return err;
}

static void exp_flags(struct seq_file *m, int flag, int fsid,
		uid_t anonu, uid_t anong, struct nfsd4_fs_locations *fslocs);

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
	seq_path(m, exp->ex_mnt, exp->ex_dentry, " \t\n\\");
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
	}
	seq_puts(m, ")\n");
	return 0;
}
static int svc_export_match(struct cache_head *a, struct cache_head *b)
{
	struct svc_export *orig = container_of(a, struct svc_export, h);
	struct svc_export *new = container_of(b, struct svc_export, h);
	return orig->ex_client == new->ex_client &&
		orig->ex_dentry == new->ex_dentry &&
		orig->ex_mnt == new->ex_mnt;
}

static void svc_export_init(struct cache_head *cnew, struct cache_head *citem)
{
	struct svc_export *new = container_of(cnew, struct svc_export, h);
	struct svc_export *item = container_of(citem, struct svc_export, h);

	kref_get(&item->ex_client->ref);
	new->ex_client = item->ex_client;
	new->ex_dentry = dget(item->ex_dentry);
	new->ex_mnt = mntget(item->ex_mnt);
	new->ex_path = NULL;
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
	new->ex_path = item->ex_path;
	item->ex_path = NULL;
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
	.cache_request	= svc_export_request,
	.cache_parse	= svc_export_parse,
	.cache_show	= svc_export_show,
	.match		= svc_export_match,
	.init		= svc_export_init,
	.update		= export_update,
	.alloc		= svc_export_alloc,
};

static struct svc_export *
svc_export_lookup(struct svc_export *exp)
{
	struct cache_head *ch;
	int hash;
	hash = hash_ptr(exp->ex_client, EXPORT_HASHBITS);
	hash ^= hash_ptr(exp->ex_dentry, EXPORT_HASHBITS);
	hash ^= hash_ptr(exp->ex_mnt, EXPORT_HASHBITS);

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
	int hash;
	hash = hash_ptr(old->ex_client, EXPORT_HASHBITS);
	hash ^= hash_ptr(old->ex_dentry, EXPORT_HASHBITS);
	hash ^= hash_ptr(old->ex_mnt, EXPORT_HASHBITS);

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

static int exp_set_key(svc_client *clp, int fsid_type, u32 *fsidv,
		       struct svc_export *exp)
{
	struct svc_expkey key, *ek;

	key.ek_client = clp;
	key.ek_fsidtype = fsid_type;
	memcpy(key.ek_fsid, fsidv, key_len(fsid_type));
	key.ek_mnt = exp->ex_mnt;
	key.ek_dentry = exp->ex_dentry;
	key.h.expiry_time = NEVER;
	key.h.flags = 0;

	ek = svc_expkey_lookup(&key);
	if (ek)
		ek = svc_expkey_update(&key,ek);
	if (ek) {
		cache_put(&ek->h, &svc_expkey_cache);
		return 0;
	}
	return -ENOMEM;
}

/*
 * Find the client's export entry matching xdev/xino.
 */
static inline struct svc_expkey *
exp_get_key(svc_client *clp, dev_t dev, ino_t ino)
{
	u32 fsidv[3];
	
	if (old_valid_dev(dev)) {
		mk_fsid(FSID_DEV, fsidv, dev, ino, 0, NULL);
		return exp_find_key(clp, FSID_DEV, fsidv, NULL);
	}
	mk_fsid(FSID_ENCODE_DEV, fsidv, dev, ino, 0, NULL);
	return exp_find_key(clp, FSID_ENCODE_DEV, fsidv, NULL);
}

/*
 * Find the client's export entry matching fsid
 */
static inline struct svc_expkey *
exp_get_fsid_key(svc_client *clp, int fsid)
{
	u32 fsidv[2];

	mk_fsid(FSID_NUM, fsidv, 0, 0, fsid, NULL);

	return exp_find_key(clp, FSID_NUM, fsidv, NULL);
}

svc_export *
exp_get_by_name(svc_client *clp, struct vfsmount *mnt, struct dentry *dentry,
		struct cache_req *reqp)
{
	struct svc_export *exp, key;
	int err;
	
	if (!clp)
		return ERR_PTR(-ENOENT);

	key.ex_client = clp;
	key.ex_mnt = mnt;
	key.ex_dentry = dentry;

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
struct svc_export *
exp_parent(svc_client *clp, struct vfsmount *mnt, struct dentry *dentry,
	   struct cache_req *reqp)
{
	svc_export *exp;

	dget(dentry);
	exp = exp_get_by_name(clp, mnt, dentry, reqp);

	while (PTR_ERR(exp) == -ENOENT && !IS_ROOT(dentry)) {
		struct dentry *parent;

		parent = dget_parent(dentry);
		dput(dentry);
		dentry = parent;
		exp = exp_get_by_name(clp, mnt, dentry, reqp);
	}
	dput(dentry);
	return exp;
}

/*
 * Hashtable locking. Write locks are placed only by user processes
 * wanting to modify export information.
 * Write locking only done in this file.  Read locking
 * needed externally.
 */

static DECLARE_RWSEM(hash_sem);

void
exp_readlock(void)
{
	down_read(&hash_sem);
}

static inline void
exp_writelock(void)
{
	down_write(&hash_sem);
}

void
exp_readunlock(void)
{
	up_read(&hash_sem);
}

static inline void
exp_writeunlock(void)
{
	up_write(&hash_sem);
}

static void exp_fsid_unhash(struct svc_export *exp)
{
	struct svc_expkey *ek;

	if ((exp->ex_flags & NFSEXP_FSID) == 0)
		return;

	ek = exp_get_fsid_key(exp->ex_client, exp->ex_fsid);
	if (!IS_ERR(ek)) {
		ek->h.expiry_time = get_seconds()-1;
		cache_put(&ek->h, &svc_expkey_cache);
	}
	svc_expkey_cache.nextcheck = get_seconds();
}

static int exp_fsid_hash(svc_client *clp, struct svc_export *exp)
{
	u32 fsid[2];
 
	if ((exp->ex_flags & NFSEXP_FSID) == 0)
		return 0;

	mk_fsid(FSID_NUM, fsid, 0, 0, exp->ex_fsid, NULL);
	return exp_set_key(clp, FSID_NUM, fsid, exp);
}

static int exp_hash(struct auth_domain *clp, struct svc_export *exp)
{
	u32 fsid[2];
	struct inode *inode = exp->ex_dentry->d_inode;
	dev_t dev = inode->i_sb->s_dev;

	if (old_valid_dev(dev)) {
		mk_fsid(FSID_DEV, fsid, dev, inode->i_ino, 0, NULL);
		return exp_set_key(clp, FSID_DEV, fsid, exp);
	}
	mk_fsid(FSID_ENCODE_DEV, fsid, dev, inode->i_ino, 0, NULL);
	return exp_set_key(clp, FSID_ENCODE_DEV, fsid, exp);
}

static void exp_unhash(struct svc_export *exp)
{
	struct svc_expkey *ek;
	struct inode *inode = exp->ex_dentry->d_inode;

	ek = exp_get_key(exp->ex_client, inode->i_sb->s_dev, inode->i_ino);
	if (!IS_ERR(ek)) {
		ek->h.expiry_time = get_seconds()-1;
		cache_put(&ek->h, &svc_expkey_cache);
	}
	svc_expkey_cache.nextcheck = get_seconds();
}
	
/*
 * Export a file system.
 */
int
exp_export(struct nfsctl_export *nxp)
{
	svc_client	*clp;
	struct svc_export	*exp = NULL;
	struct svc_export	new;
	struct svc_expkey	*fsid_key = NULL;
	struct nameidata nd;
	int		err;

	/* Consistency check */
	err = -EINVAL;
	if (!exp_verify_string(nxp->ex_path, NFS_MAXPATHLEN) ||
	    !exp_verify_string(nxp->ex_client, NFSCLNT_IDMAX))
		goto out;

	dprintk("exp_export called for %s:%s (%x/%ld fl %x).\n",
			nxp->ex_client, nxp->ex_path,
			(unsigned)nxp->ex_dev, (long)nxp->ex_ino,
			nxp->ex_flags);

	/* Try to lock the export table for update */
	exp_writelock();

	/* Look up client info */
	if (!(clp = auth_domain_find(nxp->ex_client)))
		goto out_unlock;


	/* Look up the dentry */
	err = path_lookup(nxp->ex_path, 0, &nd);
	if (err)
		goto out_unlock;
	err = -EINVAL;

	exp = exp_get_by_name(clp, nd.mnt, nd.dentry, NULL);

	memset(&new, 0, sizeof(new));

	/* must make sure there won't be an ex_fsid clash */
	if ((nxp->ex_flags & NFSEXP_FSID) &&
	    (!IS_ERR(fsid_key = exp_get_fsid_key(clp, nxp->ex_dev))) &&
	    fsid_key->ek_mnt &&
	    (fsid_key->ek_mnt != nd.mnt || fsid_key->ek_dentry != nd.dentry) )
		goto finish;

	if (!IS_ERR(exp)) {
		/* just a flags/id/fsid update */

		exp_fsid_unhash(exp);
		exp->ex_flags    = nxp->ex_flags;
		exp->ex_anon_uid = nxp->ex_anon_uid;
		exp->ex_anon_gid = nxp->ex_anon_gid;
		exp->ex_fsid     = nxp->ex_dev;

		err = exp_fsid_hash(clp, exp);
		goto finish;
	}

	err = check_export(nd.dentry->d_inode, nxp->ex_flags, NULL);
	if (err) goto finish;

	err = -ENOMEM;

	dprintk("nfsd: creating export entry %p for client %p\n", exp, clp);

	new.h.expiry_time = NEVER;
	new.h.flags = 0;
	new.ex_path = kstrdup(nxp->ex_path, GFP_KERNEL);
	if (!new.ex_path)
		goto finish;
	new.ex_client = clp;
	new.ex_mnt = nd.mnt;
	new.ex_dentry = nd.dentry;
	new.ex_flags = nxp->ex_flags;
	new.ex_anon_uid = nxp->ex_anon_uid;
	new.ex_anon_gid = nxp->ex_anon_gid;
	new.ex_fsid = nxp->ex_dev;

	exp = svc_export_lookup(&new);
	if (exp)
		exp = svc_export_update(&new, exp);

	if (!exp)
		goto finish;

	if (exp_hash(clp, exp) ||
	    exp_fsid_hash(clp, exp)) {
		/* failed to create at least one index */
		exp_do_unexport(exp);
		cache_flush();
	} else
		err = 0;
finish:
	if (new.ex_path)
		kfree(new.ex_path);
	if (exp)
		exp_put(exp);
	if (fsid_key && !IS_ERR(fsid_key))
		cache_put(&fsid_key->h, &svc_expkey_cache);
	if (clp)
		auth_domain_put(clp);
	path_release(&nd);
out_unlock:
	exp_writeunlock();
out:
	return err;
}

/*
 * Unexport a file system. The export entry has already
 * been removed from the client's list of exported fs's.
 */
static void
exp_do_unexport(svc_export *unexp)
{
	unexp->h.expiry_time = get_seconds()-1;
	svc_export_cache.nextcheck = get_seconds();
	exp_unhash(unexp);
	exp_fsid_unhash(unexp);
}


/*
 * unexport syscall.
 */
int
exp_unexport(struct nfsctl_export *nxp)
{
	struct auth_domain *dom;
	svc_export *exp;
	struct nameidata nd;
	int		err;

	/* Consistency check */
	if (!exp_verify_string(nxp->ex_path, NFS_MAXPATHLEN) ||
	    !exp_verify_string(nxp->ex_client, NFSCLNT_IDMAX))
		return -EINVAL;

	exp_writelock();

	err = -EINVAL;
	dom = auth_domain_find(nxp->ex_client);
	if (!dom) {
		dprintk("nfsd: unexport couldn't find %s\n", nxp->ex_client);
		goto out_unlock;
	}

	err = path_lookup(nxp->ex_path, 0, &nd);
	if (err)
		goto out_domain;

	err = -EINVAL;
	exp = exp_get_by_name(dom, nd.mnt, nd.dentry, NULL);
	path_release(&nd);
	if (IS_ERR(exp))
		goto out_domain;

	exp_do_unexport(exp);
	exp_put(exp);
	err = 0;

out_domain:
	auth_domain_put(dom);
	cache_flush();
out_unlock:
	exp_writeunlock();
	return err;
}

/*
 * Obtain the root fh on behalf of a client.
 * This could be done in user space, but I feel that it adds some safety
 * since its harder to fool a kernel module than a user space program.
 */
int
exp_rootfh(svc_client *clp, char *path, struct knfsd_fh *f, int maxsize)
{
	struct svc_export	*exp;
	struct nameidata	nd;
	struct inode		*inode;
	struct svc_fh		fh;
	int			err;

	err = -EPERM;
	/* NB: we probably ought to check that it's NUL-terminated */
	if (path_lookup(path, 0, &nd)) {
		printk("nfsd: exp_rootfh path not found %s", path);
		return err;
	}
	inode = nd.dentry->d_inode;

	dprintk("nfsd: exp_rootfh(%s [%p] %s:%s/%ld)\n",
		 path, nd.dentry, clp->name,
		 inode->i_sb->s_id, inode->i_ino);
	exp = exp_parent(clp, nd.mnt, nd.dentry, NULL);
	if (IS_ERR(exp)) {
		err = PTR_ERR(exp);
		goto out;
	}

	/*
	 * fh must be initialized before calling fh_compose
	 */
	fh_init(&fh, maxsize);
	if (fh_compose(&fh, exp, nd.dentry, NULL))
		err = -EINVAL;
	else
		err = 0;
	memcpy(f, &fh.fh_handle, sizeof(struct knfsd_fh));
	fh_put(&fh);
	exp_put(exp);
out:
	path_release(&nd);
	return err;
}

struct svc_export *
exp_find(struct auth_domain *clp, int fsid_type, u32 *fsidv,
	 struct cache_req *reqp)
{
	struct svc_export *exp;
	struct svc_expkey *ek = exp_find_key(clp, fsid_type, fsidv, reqp);
	if (IS_ERR(ek))
		return ERR_PTR(PTR_ERR(ek));

	exp = exp_get_by_name(clp, ek->ek_mnt, ek->ek_dentry, reqp);
	cache_put(&ek->h, &svc_expkey_cache);

	if (IS_ERR(exp))
		return ERR_PTR(PTR_ERR(exp));
	return exp;
}

/*
 * Called from functions that handle requests; functions that do work on
 * behalf of mountd are passed a single client name to use, and should
 * use exp_get_by_name() or exp_find().
 */
struct svc_export *
rqst_exp_get_by_name(struct svc_rqst *rqstp, struct vfsmount *mnt,
		struct dentry *dentry)
{
	return exp_get_by_name(rqstp->rq_client, mnt, dentry,
						&rqstp->rq_chandle);
}

struct svc_export *
rqst_exp_find(struct svc_rqst *rqstp, int fsid_type, u32 *fsidv)
{
	return exp_find(rqstp->rq_client, fsid_type, fsidv,
						&rqstp->rq_chandle);
}

struct svc_export *
rqst_exp_parent(struct svc_rqst *rqstp, struct vfsmount *mnt,
		struct dentry *dentry)
{
	return exp_parent(rqstp->rq_client, mnt, dentry, &rqstp->rq_chandle);
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
	u32 fsidv[2];

	mk_fsid(FSID_NUM, fsidv, 0, 0, 0, NULL);

	exp = rqst_exp_find(rqstp, FSID_NUM, fsidv);
	if (PTR_ERR(exp) == -ENOENT)
		return nfserr_perm;
	if (IS_ERR(exp))
		return nfserrno(PTR_ERR(exp));
	rv = fh_compose(fhp, exp, exp->ex_dentry, NULL);
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
	
	exp_readlock();
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
	exp_readunlock();
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
#ifdef MSNFS
	{ NFSEXP_MSNFS, {"msnfs", ""}},
#endif
	{ 0, {"", ""}}
};

static void exp_flags(struct seq_file *m, int flag, int fsid,
		uid_t anonu, uid_t anong, struct nfsd4_fs_locations *fsloc)
{
	int first = 0;
	struct flags *flg;

	for (flg = expflags; flg->flag; flg++) {
		int state = (flg->flag & flag)?0:1;
		if (*flg->name[state])
			seq_printf(m, "%s%s", first++?",":"", flg->name[state]);
	}
	if (flag & NFSEXP_FSID)
		seq_printf(m, "%sfsid=%d", first++?",":"", fsid);
	if (anonu != (uid_t)-2 && anonu != (0x10000-2))
		seq_printf(m, "%sanonuid=%d", first++?",":"", anonu);
	if (anong != (gid_t)-2 && anong != (0x10000-2))
		seq_printf(m, "%sanongid=%d", first++?",":"", anong);
	if (fsloc && fsloc->locations_count > 0) {
		char *loctype = (fsloc->migrated) ? "refer" : "replicas";
		int i;

		seq_printf(m, "%s%s=", first++?",":"", loctype);
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

struct seq_operations nfs_exports_op = {
	.start	= e_start,
	.next	= e_next,
	.stop	= e_stop,
	.show	= e_show,
};

/*
 * Add or modify a client.
 * Change requests may involve the list of host addresses. The list of
 * exports and possibly existing uid maps are left untouched.
 */
int
exp_addclient(struct nfsctl_client *ncp)
{
	struct auth_domain	*dom;
	int			i, err;

	/* First, consistency check. */
	err = -EINVAL;
	if (! exp_verify_string(ncp->cl_ident, NFSCLNT_IDMAX))
		goto out;
	if (ncp->cl_naddr > NFSCLNT_ADDRMAX)
		goto out;

	/* Lock the hashtable */
	exp_writelock();

	dom = unix_domain_find(ncp->cl_ident);

	err = -ENOMEM;
	if (!dom)
		goto out_unlock;

	/* Insert client into hashtable. */
	for (i = 0; i < ncp->cl_naddr; i++)
		auth_unix_add_addr(ncp->cl_addrlist[i], dom);

	auth_unix_forget_old(dom);
	auth_domain_put(dom);

	err = 0;

out_unlock:
	exp_writeunlock();
out:
	return err;
}

/*
 * Delete a client given an identifier.
 */
int
exp_delclient(struct nfsctl_client *ncp)
{
	int		err;
	struct auth_domain *dom;

	err = -EINVAL;
	if (!exp_verify_string(ncp->cl_ident, NFSCLNT_IDMAX))
		goto out;

	/* Lock the hashtable */
	exp_writelock();

	dom = auth_domain_find(ncp->cl_ident);
	/* just make sure that no addresses work 
	 * and that it will expire soon 
	 */
	if (dom) {
		err = auth_unix_forget_old(dom);
		auth_domain_put(dom);
	}

	exp_writeunlock();
out:
	return err;
}

/*
 * Verify that string is non-empty and does not exceed max length.
 */
static int
exp_verify_string(char *cp, int max)
{
	int	i;

	for (i = 0; i < max; i++)
		if (!cp[i])
			return i;
	cp[i] = 0;
	printk(KERN_NOTICE "nfsd: couldn't validate string %s\n", cp);
	return 0;
}

/*
 * Initialize the exports module.
 */
void
nfsd_export_init(void)
{
	dprintk("nfsd: initializing export module.\n");

	cache_register(&svc_export_cache);
	cache_register(&svc_expkey_cache);

}

/*
 * Flush exports table - called when last nfsd thread is killed
 */
void
nfsd_export_flush(void)
{
	exp_writelock();
	cache_purge(&svc_expkey_cache);
	cache_purge(&svc_export_cache);
	exp_writeunlock();
}

/*
 * Shutdown the exports module.
 */
void
nfsd_export_shutdown(void)
{

	dprintk("nfsd: shutting down export module.\n");

	exp_writelock();

	if (cache_unregister(&svc_expkey_cache))
		printk(KERN_ERR "nfsd: failed to unregister expkey cache\n");
	if (cache_unregister(&svc_export_cache))
		printk(KERN_ERR "nfsd: failed to unregister export cache\n");
	svcauth_unix_purge();

	exp_writeunlock();
	dprintk("nfsd: export shutdown complete.\n");
}
