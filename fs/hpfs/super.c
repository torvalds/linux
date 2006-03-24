/*
 *  linux/fs/hpfs/super.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  mounting, unmounting, error handling
 */

#include "hpfs_fn.h"
#include <linux/module.h>
#include <linux/parser.h>
#include <linux/init.h>
#include <linux/statfs.h>

/* Mark the filesystem dirty, so that chkdsk checks it when os/2 booted */

static void mark_dirty(struct super_block *s)
{
	if (hpfs_sb(s)->sb_chkdsk && !(s->s_flags & MS_RDONLY)) {
		struct buffer_head *bh;
		struct hpfs_spare_block *sb;
		if ((sb = hpfs_map_sector(s, 17, &bh, 0))) {
			sb->dirty = 1;
			sb->old_wrote = 0;
			mark_buffer_dirty(bh);
			brelse(bh);
		}
	}
}

/* Mark the filesystem clean (mark it dirty for chkdsk if chkdsk==2 or if there
   were errors) */

static void unmark_dirty(struct super_block *s)
{
	struct buffer_head *bh;
	struct hpfs_spare_block *sb;
	if (s->s_flags & MS_RDONLY) return;
	if ((sb = hpfs_map_sector(s, 17, &bh, 0))) {
		sb->dirty = hpfs_sb(s)->sb_chkdsk > 1 - hpfs_sb(s)->sb_was_error;
		sb->old_wrote = hpfs_sb(s)->sb_chkdsk >= 2 && !hpfs_sb(s)->sb_was_error;
		mark_buffer_dirty(bh);
		brelse(bh);
	}
}

/* Filesystem error... */

#define ERR_BUF_SIZE 1024

void hpfs_error(struct super_block *s, char *m,...)
{
	char *buf;
	va_list l;
	va_start(l, m);
	if (!(buf = kmalloc(ERR_BUF_SIZE, GFP_KERNEL)))
		printk("HPFS: No memory for error message '%s'\n",m);
	else if (vsprintf(buf, m, l) >= ERR_BUF_SIZE)
		printk("HPFS: Grrrr... Kernel memory corrupted ... going on, but it'll crash very soon :-(\n");
	printk("HPFS: filesystem error: ");
	if (buf) printk("%s", buf);
	else printk("%s\n",m);
	if (!hpfs_sb(s)->sb_was_error) {
		if (hpfs_sb(s)->sb_err == 2) {
			printk("; crashing the system because you wanted it\n");
			mark_dirty(s);
			panic("HPFS panic");
		} else if (hpfs_sb(s)->sb_err == 1) {
			if (s->s_flags & MS_RDONLY) printk("; already mounted read-only\n");
			else {
				printk("; remounting read-only\n");
				mark_dirty(s);
				s->s_flags |= MS_RDONLY;
			}
		} else if (s->s_flags & MS_RDONLY) printk("; going on - but anything won't be destroyed because it's read-only\n");
		else printk("; corrupted filesystem mounted read/write - your computer will explode within 20 seconds ... but you wanted it so!\n");
	} else printk("\n");
	kfree(buf);
	hpfs_sb(s)->sb_was_error = 1;
}

/* 
 * A little trick to detect cycles in many hpfs structures and don't let the
 * kernel crash on corrupted filesystem. When first called, set c2 to 0.
 *
 * BTW. chkdsk doesn't detect cycles correctly. When I had 2 lost directories
 * nested each in other, chkdsk locked up happilly.
 */

int hpfs_stop_cycles(struct super_block *s, int key, int *c1, int *c2,
		char *msg)
{
	if (*c2 && *c1 == key) {
		hpfs_error(s, "cycle detected on key %08x in %s", key, msg);
		return 1;
	}
	(*c2)++;
	if (!((*c2 - 1) & *c2)) *c1 = key;
	return 0;
}

static void hpfs_put_super(struct super_block *s)
{
	struct hpfs_sb_info *sbi = hpfs_sb(s);
	kfree(sbi->sb_cp_table);
	kfree(sbi->sb_bmp_dir);
	unmark_dirty(s);
	s->s_fs_info = NULL;
	kfree(sbi);
}

unsigned hpfs_count_one_bitmap(struct super_block *s, secno secno)
{
	struct quad_buffer_head qbh;
	unsigned *bits;
	unsigned i, count;
	if (!(bits = hpfs_map_4sectors(s, secno, &qbh, 4))) return 0;
	count = 0;
	for (i = 0; i < 2048 / sizeof(unsigned); i++) {
		unsigned b; 
		if (!bits[i]) continue;
		for (b = bits[i]; b; b>>=1) count += b & 1;
	}
	hpfs_brelse4(&qbh);
	return count;
}

static unsigned count_bitmaps(struct super_block *s)
{
	unsigned n, count, n_bands;
	n_bands = (hpfs_sb(s)->sb_fs_size + 0x3fff) >> 14;
	count = 0;
	for (n = 0; n < n_bands; n++)
		count += hpfs_count_one_bitmap(s, hpfs_sb(s)->sb_bmp_dir[n]);
	return count;
}

static int hpfs_statfs(struct super_block *s, struct kstatfs *buf)
{
	struct hpfs_sb_info *sbi = hpfs_sb(s);
	lock_kernel();

	/*if (sbi->sb_n_free == -1) {*/
		sbi->sb_n_free = count_bitmaps(s);
		sbi->sb_n_free_dnodes = hpfs_count_one_bitmap(s, sbi->sb_dmap);
	/*}*/
	buf->f_type = s->s_magic;
	buf->f_bsize = 512;
	buf->f_blocks = sbi->sb_fs_size;
	buf->f_bfree = sbi->sb_n_free;
	buf->f_bavail = sbi->sb_n_free;
	buf->f_files = sbi->sb_dirband_size / 4;
	buf->f_ffree = sbi->sb_n_free_dnodes;
	buf->f_namelen = 254;

	unlock_kernel();

	return 0;
}

static kmem_cache_t * hpfs_inode_cachep;

static struct inode *hpfs_alloc_inode(struct super_block *sb)
{
	struct hpfs_inode_info *ei;
	ei = (struct hpfs_inode_info *)kmem_cache_alloc(hpfs_inode_cachep, SLAB_NOFS);
	if (!ei)
		return NULL;
	ei->vfs_inode.i_version = 1;
	return &ei->vfs_inode;
}

static void hpfs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(hpfs_inode_cachep, hpfs_i(inode));
}

static void init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct hpfs_inode_info *ei = (struct hpfs_inode_info *) foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		mutex_init(&ei->i_mutex);
		mutex_init(&ei->i_parent_mutex);
		inode_init_once(&ei->vfs_inode);
	}
}
 
static int init_inodecache(void)
{
	hpfs_inode_cachep = kmem_cache_create("hpfs_inode_cache",
					     sizeof(struct hpfs_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     init_once, NULL);
	if (hpfs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	if (kmem_cache_destroy(hpfs_inode_cachep))
		printk(KERN_INFO "hpfs_inode_cache: not all structures were freed\n");
}

/*
 * A tiny parser for option strings, stolen from dosfs.
 * Stolen again from read-only hpfs.
 * And updated for table-driven option parsing.
 */

enum {
	Opt_help, Opt_uid, Opt_gid, Opt_umask, Opt_case_lower, Opt_case_asis,
	Opt_conv_binary, Opt_conv_text, Opt_conv_auto,
	Opt_check_none, Opt_check_normal, Opt_check_strict,
	Opt_err_cont, Opt_err_ro, Opt_err_panic,
	Opt_eas_no, Opt_eas_ro, Opt_eas_rw,
	Opt_chkdsk_no, Opt_chkdsk_errors, Opt_chkdsk_always,
	Opt_timeshift, Opt_err,
};

static match_table_t tokens = {
	{Opt_help, "help"},
	{Opt_uid, "uid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_umask, "umask=%o"},
	{Opt_case_lower, "case=lower"},
	{Opt_case_asis, "case=asis"},
	{Opt_conv_binary, "conv=binary"},
	{Opt_conv_text, "conv=text"},
	{Opt_conv_auto, "conv=auto"},
	{Opt_check_none, "check=none"},
	{Opt_check_normal, "check=normal"},
	{Opt_check_strict, "check=strict"},
	{Opt_err_cont, "errors=continue"},
	{Opt_err_ro, "errors=remount-ro"},
	{Opt_err_panic, "errors=panic"},
	{Opt_eas_no, "eas=no"},
	{Opt_eas_ro, "eas=ro"},
	{Opt_eas_rw, "eas=rw"},
	{Opt_chkdsk_no, "chkdsk=no"},
	{Opt_chkdsk_errors, "chkdsk=errors"},
	{Opt_chkdsk_always, "chkdsk=always"},
	{Opt_timeshift, "timeshift=%d"},
	{Opt_err, NULL},
};

static int parse_opts(char *opts, uid_t *uid, gid_t *gid, umode_t *umask,
		      int *lowercase, int *conv, int *eas, int *chk, int *errs,
		      int *chkdsk, int *timeshift)
{
	char *p;
	int option;

	if (!opts)
		return 1;

	/*printk("Parsing opts: '%s'\n",opts);*/

	while ((p = strsep(&opts, ",")) != NULL) {
		substring_t args[MAX_OPT_ARGS];
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_help:
			return 2;
		case Opt_uid:
			if (match_int(args, &option))
				return 0;
			*uid = option;
			break;
		case Opt_gid:
			if (match_int(args, &option))
				return 0;
			*gid = option;
			break;
		case Opt_umask:
			if (match_octal(args, &option))
				return 0;
			*umask = option;
			break;
		case Opt_case_lower:
			*lowercase = 1;
			break;
		case Opt_case_asis:
			*lowercase = 0;
			break;
		case Opt_conv_binary:
			*conv = CONV_BINARY;
			break;
		case Opt_conv_text:
			*conv = CONV_TEXT;
			break;
		case Opt_conv_auto:
			*conv = CONV_AUTO;
			break;
		case Opt_check_none:
			*chk = 0;
			break;
		case Opt_check_normal:
			*chk = 1;
			break;
		case Opt_check_strict:
			*chk = 2;
			break;
		case Opt_err_cont:
			*errs = 0;
			break;
		case Opt_err_ro:
			*errs = 1;
			break;
		case Opt_err_panic:
			*errs = 2;
			break;
		case Opt_eas_no:
			*eas = 0;
			break;
		case Opt_eas_ro:
			*eas = 1;
			break;
		case Opt_eas_rw:
			*eas = 2;
			break;
		case Opt_chkdsk_no:
			*chkdsk = 0;
			break;
		case Opt_chkdsk_errors:
			*chkdsk = 1;
			break;
		case Opt_chkdsk_always:
			*chkdsk = 2;
			break;
		case Opt_timeshift:
		{
			int m = 1;
			char *rhs = args[0].from;
			if (!rhs || !*rhs)
				return 0;
			if (*rhs == '-') m = -1;
			if (*rhs == '+' || *rhs == '-') rhs++;
			*timeshift = simple_strtoul(rhs, &rhs, 0) * m;
			if (*rhs)
				return 0;
			break;
		}
		default:
			return 0;
		}
	}
	return 1;
}

static inline void hpfs_help(void)
{
	printk("\n\
HPFS filesystem options:\n\
      help              do not mount and display this text\n\
      uid=xxx           set uid of files that don't have uid specified in eas\n\
      gid=xxx           set gid of files that don't have gid specified in eas\n\
      umask=xxx         set mode of files that don't have mode specified in eas\n\
      case=lower        lowercase all files\n\
      case=asis         do not lowercase files (default)\n\
      conv=binary       do not convert CR/LF -> LF (default)\n\
      conv=auto         convert only files with known text extensions\n\
      conv=text         convert all files\n\
      check=none        no fs checks - kernel may crash on corrupted filesystem\n\
      check=normal      do some checks - it should not crash (default)\n\
      check=strict      do extra time-consuming checks, used for debugging\n\
      errors=continue   continue on errors\n\
      errors=remount-ro remount read-only if errors found (default)\n\
      errors=panic      panic on errors\n\
      chkdsk=no         do not mark fs for chkdsking even if there were errors\n\
      chkdsk=errors     mark fs dirty if errors found (default)\n\
      chkdsk=always     always mark fs dirty - used for debugging\n\
      eas=no            ignore extended attributes\n\
      eas=ro            read but do not write extended attributes\n\
      eas=rw            r/w eas => enables chmod, chown, mknod, ln -s (default)\n\
      timeshift=nnn	add nnn seconds to file times\n\
\n");
}

static int hpfs_remount_fs(struct super_block *s, int *flags, char *data)
{
	uid_t uid;
	gid_t gid;
	umode_t umask;
	int lowercase, conv, eas, chk, errs, chkdsk, timeshift;
	int o;
	struct hpfs_sb_info *sbi = hpfs_sb(s);
	
	*flags |= MS_NOATIME;
	
	uid = sbi->sb_uid; gid = sbi->sb_gid;
	umask = 0777 & ~sbi->sb_mode;
	lowercase = sbi->sb_lowercase; conv = sbi->sb_conv;
	eas = sbi->sb_eas; chk = sbi->sb_chk; chkdsk = sbi->sb_chkdsk;
	errs = sbi->sb_err; timeshift = sbi->sb_timeshift;

	if (!(o = parse_opts(data, &uid, &gid, &umask, &lowercase, &conv,
	    &eas, &chk, &errs, &chkdsk, &timeshift))) {
		printk("HPFS: bad mount options.\n");
	    	return 1;
	}
	if (o == 2) {
		hpfs_help();
		return 1;
	}
	if (timeshift != sbi->sb_timeshift) {
		printk("HPFS: timeshift can't be changed using remount.\n");
		return 1;
	}

	unmark_dirty(s);

	sbi->sb_uid = uid; sbi->sb_gid = gid;
	sbi->sb_mode = 0777 & ~umask;
	sbi->sb_lowercase = lowercase; sbi->sb_conv = conv;
	sbi->sb_eas = eas; sbi->sb_chk = chk; sbi->sb_chkdsk = chkdsk;
	sbi->sb_err = errs; sbi->sb_timeshift = timeshift;

	if (!(*flags & MS_RDONLY)) mark_dirty(s);

	return 0;
}

/* Super operations */

static struct super_operations hpfs_sops =
{
	.alloc_inode	= hpfs_alloc_inode,
	.destroy_inode	= hpfs_destroy_inode,
	.delete_inode	= hpfs_delete_inode,
	.put_super	= hpfs_put_super,
	.statfs		= hpfs_statfs,
	.remount_fs	= hpfs_remount_fs,
};

static int hpfs_fill_super(struct super_block *s, void *options, int silent)
{
	struct buffer_head *bh0, *bh1, *bh2;
	struct hpfs_boot_block *bootblock;
	struct hpfs_super_block *superblock;
	struct hpfs_spare_block *spareblock;
	struct hpfs_sb_info *sbi;
	struct inode *root;

	uid_t uid;
	gid_t gid;
	umode_t umask;
	int lowercase, conv, eas, chk, errs, chkdsk, timeshift;

	dnode_secno root_dno;
	struct hpfs_dirent *de = NULL;
	struct quad_buffer_head qbh;

	int o;

	sbi = kmalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	s->s_fs_info = sbi;
	memset(sbi, 0, sizeof(*sbi));

	sbi->sb_bmp_dir = NULL;
	sbi->sb_cp_table = NULL;

	init_MUTEX(&sbi->hpfs_creation_de);

	uid = current->uid;
	gid = current->gid;
	umask = current->fs->umask;
	lowercase = 0;
	conv = CONV_BINARY;
	eas = 2;
	chk = 1;
	errs = 1;
	chkdsk = 1;
	timeshift = 0;

	if (!(o = parse_opts(options, &uid, &gid, &umask, &lowercase, &conv,
	    &eas, &chk, &errs, &chkdsk, &timeshift))) {
		printk("HPFS: bad mount options.\n");
		goto bail0;
	}
	if (o==2) {
		hpfs_help();
		goto bail0;
	}

	/*sbi->sb_mounting = 1;*/
	sb_set_blocksize(s, 512);
	sbi->sb_fs_size = -1;
	if (!(bootblock = hpfs_map_sector(s, 0, &bh0, 0))) goto bail1;
	if (!(superblock = hpfs_map_sector(s, 16, &bh1, 1))) goto bail2;
	if (!(spareblock = hpfs_map_sector(s, 17, &bh2, 0))) goto bail3;

	/* Check magics */
	if (/*bootblock->magic != BB_MAGIC
	    ||*/ superblock->magic != SB_MAGIC
	    || spareblock->magic != SP_MAGIC) {
		if (!silent) printk("HPFS: Bad magic ... probably not HPFS\n");
		goto bail4;
	}

	/* Check version */
	if (!(s->s_flags & MS_RDONLY) &&
	      superblock->funcversion != 2 && superblock->funcversion != 3) {
		printk("HPFS: Bad version %d,%d. Mount readonly to go around\n",
			(int)superblock->version, (int)superblock->funcversion);
		printk("HPFS: please try recent version of HPFS driver at http://artax.karlin.mff.cuni.cz/~mikulas/vyplody/hpfs/index-e.cgi and if it still can't understand this format, contact author - mikulas@artax.karlin.mff.cuni.cz\n");
		goto bail4;
	}

	s->s_flags |= MS_NOATIME;

	/* Fill superblock stuff */
	s->s_magic = HPFS_SUPER_MAGIC;
	s->s_op = &hpfs_sops;

	sbi->sb_root = superblock->root;
	sbi->sb_fs_size = superblock->n_sectors;
	sbi->sb_bitmaps = superblock->bitmaps;
	sbi->sb_dirband_start = superblock->dir_band_start;
	sbi->sb_dirband_size = superblock->n_dir_band;
	sbi->sb_dmap = superblock->dir_band_bitmap;
	sbi->sb_uid = uid;
	sbi->sb_gid = gid;
	sbi->sb_mode = 0777 & ~umask;
	sbi->sb_n_free = -1;
	sbi->sb_n_free_dnodes = -1;
	sbi->sb_lowercase = lowercase;
	sbi->sb_conv = conv;
	sbi->sb_eas = eas;
	sbi->sb_chk = chk;
	sbi->sb_chkdsk = chkdsk;
	sbi->sb_err = errs;
	sbi->sb_timeshift = timeshift;
	sbi->sb_was_error = 0;
	sbi->sb_cp_table = NULL;
	sbi->sb_c_bitmap = -1;
	sbi->sb_max_fwd_alloc = 0xffffff;
	
	/* Load bitmap directory */
	if (!(sbi->sb_bmp_dir = hpfs_load_bitmap_directory(s, superblock->bitmaps)))
		goto bail4;
	
	/* Check for general fs errors*/
	if (spareblock->dirty && !spareblock->old_wrote) {
		if (errs == 2) {
			printk("HPFS: Improperly stopped, not mounted\n");
			goto bail4;
		}
		hpfs_error(s, "improperly stopped");
	}

	if (!(s->s_flags & MS_RDONLY)) {
		spareblock->dirty = 1;
		spareblock->old_wrote = 0;
		mark_buffer_dirty(bh2);
	}

	if (spareblock->hotfixes_used || spareblock->n_spares_used) {
		if (errs >= 2) {
			printk("HPFS: Hotfixes not supported here, try chkdsk\n");
			mark_dirty(s);
			goto bail4;
		}
		hpfs_error(s, "hotfixes not supported here, try chkdsk");
		if (errs == 0) printk("HPFS: Proceeding, but your filesystem will be probably corrupted by this driver...\n");
		else printk("HPFS: This driver may read bad files or crash when operating on disk with hotfixes.\n");
	}
	if (spareblock->n_dnode_spares != spareblock->n_dnode_spares_free) {
		if (errs >= 2) {
			printk("HPFS: Spare dnodes used, try chkdsk\n");
			mark_dirty(s);
			goto bail4;
		}
		hpfs_error(s, "warning: spare dnodes used, try chkdsk");
		if (errs == 0) printk("HPFS: Proceeding, but your filesystem could be corrupted if you delete files or directories\n");
	}
	if (chk) {
		unsigned a;
		if (superblock->dir_band_end - superblock->dir_band_start + 1 != superblock->n_dir_band ||
		    superblock->dir_band_end < superblock->dir_band_start || superblock->n_dir_band > 0x4000) {
			hpfs_error(s, "dir band size mismatch: dir_band_start==%08x, dir_band_end==%08x, n_dir_band==%08x",
				superblock->dir_band_start, superblock->dir_band_end, superblock->n_dir_band);
			goto bail4;
		}
		a = sbi->sb_dirband_size;
		sbi->sb_dirband_size = 0;
		if (hpfs_chk_sectors(s, superblock->dir_band_start, superblock->n_dir_band, "dir_band") ||
		    hpfs_chk_sectors(s, superblock->dir_band_bitmap, 4, "dir_band_bitmap") ||
		    hpfs_chk_sectors(s, superblock->bitmaps, 4, "bitmaps")) {
			mark_dirty(s);
			goto bail4;
		}
		sbi->sb_dirband_size = a;
	} else printk("HPFS: You really don't want any checks? You are crazy...\n");

	/* Load code page table */
	if (spareblock->n_code_pages)
		if (!(sbi->sb_cp_table = hpfs_load_code_page(s, spareblock->code_page_dir)))
			printk("HPFS: Warning: code page support is disabled\n");

	brelse(bh2);
	brelse(bh1);
	brelse(bh0);

	root = iget_locked(s, sbi->sb_root);
	if (!root)
		goto bail0;
	hpfs_init_inode(root);
	hpfs_read_inode(root);
	unlock_new_inode(root);
	s->s_root = d_alloc_root(root);
	if (!s->s_root) {
		iput(root);
		goto bail0;
	}
	hpfs_set_dentry_operations(s->s_root);

	/*
	 * find the root directory's . pointer & finish filling in the inode
	 */

	root_dno = hpfs_fnode_dno(s, sbi->sb_root);
	if (root_dno)
		de = map_dirent(root, root_dno, "\001\001", 2, NULL, &qbh);
	if (!de)
		hpfs_error(s, "unable to find root dir");
	else {
		root->i_atime.tv_sec = local_to_gmt(s, de->read_date);
		root->i_atime.tv_nsec = 0;
		root->i_mtime.tv_sec = local_to_gmt(s, de->write_date);
		root->i_mtime.tv_nsec = 0;
		root->i_ctime.tv_sec = local_to_gmt(s, de->creation_date);
		root->i_ctime.tv_nsec = 0;
		hpfs_i(root)->i_ea_size = de->ea_size;
		hpfs_i(root)->i_parent_dir = root->i_ino;
		if (root->i_size == -1)
			root->i_size = 2048;
		if (root->i_blocks == -1)
			root->i_blocks = 5;
		hpfs_brelse4(&qbh);
	}
	return 0;

bail4:	brelse(bh2);
bail3:	brelse(bh1);
bail2:	brelse(bh0);
bail1:
bail0:
	kfree(sbi->sb_bmp_dir);
	kfree(sbi->sb_cp_table);
	s->s_fs_info = NULL;
	kfree(sbi);
	return -EINVAL;
}

static struct super_block *hpfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, hpfs_fill_super);
}

static struct file_system_type hpfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "hpfs",
	.get_sb		= hpfs_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static int __init init_hpfs_fs(void)
{
	int err = init_inodecache();
	if (err)
		goto out1;
	err = register_filesystem(&hpfs_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_inodecache();
out1:
	return err;
}

static void __exit exit_hpfs_fs(void)
{
	unregister_filesystem(&hpfs_fs_type);
	destroy_inodecache();
}

module_init(init_hpfs_fs)
module_exit(exit_hpfs_fs)
MODULE_LICENSE("GPL");
