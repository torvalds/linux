// SPDX-License-Identifier: GPL-2.0-only
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
#include <linux/magic.h>
#include <linux/sched.h>
#include <linux/bitmap.h>
#include <linux/slab.h>
#include <linux/seq_file.h>

/* Mark the filesystem dirty, so that chkdsk checks it when os/2 booted */

static void mark_dirty(struct super_block *s, int remount)
{
	if (hpfs_sb(s)->sb_chkdsk && (remount || !sb_rdonly(s))) {
		struct buffer_head *bh;
		struct hpfs_spare_block *sb;
		if ((sb = hpfs_map_sector(s, 17, &bh, 0))) {
			sb->dirty = 1;
			sb->old_wrote = 0;
			mark_buffer_dirty(bh);
			sync_dirty_buffer(bh);
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
	if (sb_rdonly(s)) return;
	sync_blockdev(s->s_bdev);
	if ((sb = hpfs_map_sector(s, 17, &bh, 0))) {
		sb->dirty = hpfs_sb(s)->sb_chkdsk > 1 - hpfs_sb(s)->sb_was_error;
		sb->old_wrote = hpfs_sb(s)->sb_chkdsk >= 2 && !hpfs_sb(s)->sb_was_error;
		mark_buffer_dirty(bh);
		sync_dirty_buffer(bh);
		brelse(bh);
	}
}

/* Filesystem error... */
void hpfs_error(struct super_block *s, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	pr_err("filesystem error: %pV", &vaf);

	va_end(args);

	if (!hpfs_sb(s)->sb_was_error) {
		if (hpfs_sb(s)->sb_err == 2) {
			pr_cont("; crashing the system because you wanted it\n");
			mark_dirty(s, 0);
			panic("HPFS panic");
		} else if (hpfs_sb(s)->sb_err == 1) {
			if (sb_rdonly(s))
				pr_cont("; already mounted read-only\n");
			else {
				pr_cont("; remounting read-only\n");
				mark_dirty(s, 0);
				s->s_flags |= SB_RDONLY;
			}
		} else if (sb_rdonly(s))
				pr_cont("; going on - but anything won't be destroyed because it's read-only\n");
		else
			pr_cont("; corrupted filesystem mounted read/write - your computer will explode within 20 seconds ... but you wanted it so!\n");
	} else
		pr_cont("\n");
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

static void free_sbi(struct hpfs_sb_info *sbi)
{
	kfree(sbi->sb_cp_table);
	kfree(sbi->sb_bmp_dir);
	kfree(sbi);
}

static void lazy_free_sbi(struct rcu_head *rcu)
{
	free_sbi(container_of(rcu, struct hpfs_sb_info, rcu));
}

static void hpfs_put_super(struct super_block *s)
{
	hpfs_lock(s);
	unmark_dirty(s);
	hpfs_unlock(s);
	call_rcu(&hpfs_sb(s)->rcu, lazy_free_sbi);
}

static unsigned hpfs_count_one_bitmap(struct super_block *s, secno secno)
{
	struct quad_buffer_head qbh;
	unsigned long *bits;
	unsigned count;

	bits = hpfs_map_4sectors(s, secno, &qbh, 0);
	if (!bits)
		return (unsigned)-1;
	count = bitmap_weight(bits, 2048 * BITS_PER_BYTE);
	hpfs_brelse4(&qbh);
	return count;
}

static unsigned count_bitmaps(struct super_block *s)
{
	unsigned n, count, n_bands;
	n_bands = (hpfs_sb(s)->sb_fs_size + 0x3fff) >> 14;
	count = 0;
	for (n = 0; n < COUNT_RD_AHEAD; n++) {
		hpfs_prefetch_bitmap(s, n);
	}
	for (n = 0; n < n_bands; n++) {
		unsigned c;
		hpfs_prefetch_bitmap(s, n + COUNT_RD_AHEAD);
		c = hpfs_count_one_bitmap(s, le32_to_cpu(hpfs_sb(s)->sb_bmp_dir[n]));
		if (c != (unsigned)-1)
			count += c;
	}
	return count;
}

unsigned hpfs_get_free_dnodes(struct super_block *s)
{
	struct hpfs_sb_info *sbi = hpfs_sb(s);
	if (sbi->sb_n_free_dnodes == (unsigned)-1) {
		unsigned c = hpfs_count_one_bitmap(s, sbi->sb_dmap);
		if (c == (unsigned)-1)
			return 0;
		sbi->sb_n_free_dnodes = c;
	}
	return sbi->sb_n_free_dnodes;
}

static int hpfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *s = dentry->d_sb;
	struct hpfs_sb_info *sbi = hpfs_sb(s);
	u64 id = huge_encode_dev(s->s_bdev->bd_dev);

	hpfs_lock(s);

	if (sbi->sb_n_free == (unsigned)-1)
		sbi->sb_n_free = count_bitmaps(s);

	buf->f_type = s->s_magic;
	buf->f_bsize = 512;
	buf->f_blocks = sbi->sb_fs_size;
	buf->f_bfree = sbi->sb_n_free;
	buf->f_bavail = sbi->sb_n_free;
	buf->f_files = sbi->sb_dirband_size / 4;
	buf->f_ffree = hpfs_get_free_dnodes(s);
	buf->f_fsid = u64_to_fsid(id);
	buf->f_namelen = 254;

	hpfs_unlock(s);

	return 0;
}


long hpfs_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	switch (cmd) {
		case FITRIM: {
			struct fstrim_range range;
			secno n_trimmed;
			int r;
			if (!capable(CAP_SYS_ADMIN))
				return -EPERM;
			if (copy_from_user(&range, (struct fstrim_range __user *)arg, sizeof(range)))
				return -EFAULT;
			r = hpfs_trim_fs(file_inode(file)->i_sb, range.start >> 9, (range.start + range.len) >> 9, (range.minlen + 511) >> 9, &n_trimmed);
			if (r)
				return r;
			range.len = (u64)n_trimmed << 9;
			if (copy_to_user((struct fstrim_range __user *)arg, &range, sizeof(range)))
				return -EFAULT;
			return 0;
		}
		default: {
			return -ENOIOCTLCMD;
		}
	}
}


static struct kmem_cache * hpfs_inode_cachep;

static struct inode *hpfs_alloc_inode(struct super_block *sb)
{
	struct hpfs_inode_info *ei;
	ei = alloc_inode_sb(sb, hpfs_inode_cachep, GFP_NOFS);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void hpfs_free_inode(struct inode *inode)
{
	kmem_cache_free(hpfs_inode_cachep, hpfs_i(inode));
}

static void init_once(void *foo)
{
	struct hpfs_inode_info *ei = (struct hpfs_inode_info *) foo;

	inode_init_once(&ei->vfs_inode);
}

static int init_inodecache(void)
{
	hpfs_inode_cachep = kmem_cache_create("hpfs_inode_cache",
					     sizeof(struct hpfs_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_ACCOUNT),
					     init_once);
	if (hpfs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(hpfs_inode_cachep);
}

/*
 * A tiny parser for option strings, stolen from dosfs.
 * Stolen again from read-only hpfs.
 * And updated for table-driven option parsing.
 */

enum {
	Opt_help, Opt_uid, Opt_gid, Opt_umask, Opt_case_lower, Opt_case_asis,
	Opt_check_none, Opt_check_normal, Opt_check_strict,
	Opt_err_cont, Opt_err_ro, Opt_err_panic,
	Opt_eas_no, Opt_eas_ro, Opt_eas_rw,
	Opt_chkdsk_no, Opt_chkdsk_errors, Opt_chkdsk_always,
	Opt_timeshift, Opt_err,
};

static const match_table_t tokens = {
	{Opt_help, "help"},
	{Opt_uid, "uid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_umask, "umask=%o"},
	{Opt_case_lower, "case=lower"},
	{Opt_case_asis, "case=asis"},
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

static int parse_opts(char *opts, kuid_t *uid, kgid_t *gid, umode_t *umask,
		      int *lowercase, int *eas, int *chk, int *errs,
		      int *chkdsk, int *timeshift)
{
	char *p;
	int option;

	if (!opts)
		return 1;

	/*pr_info("Parsing opts: '%s'\n",opts);*/

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
			*uid = make_kuid(current_user_ns(), option);
			if (!uid_valid(*uid))
				return 0;
			break;
		case Opt_gid:
			if (match_int(args, &option))
				return 0;
			*gid = make_kgid(current_user_ns(), option);
			if (!gid_valid(*gid))
				return 0;
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
	pr_info("\n\
HPFS filesystem options:\n\
      help              do not mount and display this text\n\
      uid=xxx           set uid of files that don't have uid specified in eas\n\
      gid=xxx           set gid of files that don't have gid specified in eas\n\
      umask=xxx         set mode of files that don't have mode specified in eas\n\
      case=lower        lowercase all files\n\
      case=asis         do not lowercase files (default)\n\
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
	kuid_t uid;
	kgid_t gid;
	umode_t umask;
	int lowercase, eas, chk, errs, chkdsk, timeshift;
	int o;
	struct hpfs_sb_info *sbi = hpfs_sb(s);

	sync_filesystem(s);

	*flags |= SB_NOATIME;

	hpfs_lock(s);
	uid = sbi->sb_uid; gid = sbi->sb_gid;
	umask = 0777 & ~sbi->sb_mode;
	lowercase = sbi->sb_lowercase;
	eas = sbi->sb_eas; chk = sbi->sb_chk; chkdsk = sbi->sb_chkdsk;
	errs = sbi->sb_err; timeshift = sbi->sb_timeshift;

	if (!(o = parse_opts(data, &uid, &gid, &umask, &lowercase,
	    &eas, &chk, &errs, &chkdsk, &timeshift))) {
		pr_err("bad mount options.\n");
		goto out_err;
	}
	if (o == 2) {
		hpfs_help();
		goto out_err;
	}
	if (timeshift != sbi->sb_timeshift) {
		pr_err("timeshift can't be changed using remount.\n");
		goto out_err;
	}

	unmark_dirty(s);

	sbi->sb_uid = uid; sbi->sb_gid = gid;
	sbi->sb_mode = 0777 & ~umask;
	sbi->sb_lowercase = lowercase;
	sbi->sb_eas = eas; sbi->sb_chk = chk; sbi->sb_chkdsk = chkdsk;
	sbi->sb_err = errs; sbi->sb_timeshift = timeshift;

	if (!(*flags & SB_RDONLY)) mark_dirty(s, 1);

	hpfs_unlock(s);
	return 0;

out_err:
	hpfs_unlock(s);
	return -EINVAL;
}

static int hpfs_show_options(struct seq_file *seq, struct dentry *root)
{
	struct hpfs_sb_info *sbi = hpfs_sb(root->d_sb);

	seq_printf(seq, ",uid=%u", from_kuid_munged(&init_user_ns, sbi->sb_uid));
	seq_printf(seq, ",gid=%u", from_kgid_munged(&init_user_ns, sbi->sb_gid));
	seq_printf(seq, ",umask=%03o", (~sbi->sb_mode & 0777));
	if (sbi->sb_lowercase)
		seq_printf(seq, ",case=lower");
	if (!sbi->sb_chk)
		seq_printf(seq, ",check=none");
	if (sbi->sb_chk == 2)
		seq_printf(seq, ",check=strict");
	if (!sbi->sb_err)
		seq_printf(seq, ",errors=continue");
	if (sbi->sb_err == 2)
		seq_printf(seq, ",errors=panic");
	if (!sbi->sb_chkdsk)
		seq_printf(seq, ",chkdsk=no");
	if (sbi->sb_chkdsk == 2)
		seq_printf(seq, ",chkdsk=always");
	if (!sbi->sb_eas)
		seq_printf(seq, ",eas=no");
	if (sbi->sb_eas == 1)
		seq_printf(seq, ",eas=ro");
	if (sbi->sb_timeshift)
		seq_printf(seq, ",timeshift=%d", sbi->sb_timeshift);
	return 0;
}

/* Super operations */

static const struct super_operations hpfs_sops =
{
	.alloc_inode	= hpfs_alloc_inode,
	.free_inode	= hpfs_free_inode,
	.evict_inode	= hpfs_evict_inode,
	.put_super	= hpfs_put_super,
	.statfs		= hpfs_statfs,
	.remount_fs	= hpfs_remount_fs,
	.show_options	= hpfs_show_options,
};

static int hpfs_fill_super(struct super_block *s, void *options, int silent)
{
	struct buffer_head *bh0, *bh1, *bh2;
	struct hpfs_boot_block *bootblock;
	struct hpfs_super_block *superblock;
	struct hpfs_spare_block *spareblock;
	struct hpfs_sb_info *sbi;
	struct inode *root;

	kuid_t uid;
	kgid_t gid;
	umode_t umask;
	int lowercase, eas, chk, errs, chkdsk, timeshift;

	dnode_secno root_dno;
	struct hpfs_dirent *de = NULL;
	struct quad_buffer_head qbh;

	int o;

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi) {
		return -ENOMEM;
	}
	s->s_fs_info = sbi;

	mutex_init(&sbi->hpfs_mutex);
	hpfs_lock(s);

	uid = current_uid();
	gid = current_gid();
	umask = current_umask();
	lowercase = 0;
	eas = 2;
	chk = 1;
	errs = 1;
	chkdsk = 1;
	timeshift = 0;

	if (!(o = parse_opts(options, &uid, &gid, &umask, &lowercase,
	    &eas, &chk, &errs, &chkdsk, &timeshift))) {
		pr_err("bad mount options.\n");
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
	if (/*le16_to_cpu(bootblock->magic) != BB_MAGIC
	    ||*/ le32_to_cpu(superblock->magic) != SB_MAGIC
	    || le32_to_cpu(spareblock->magic) != SP_MAGIC) {
		if (!silent)
			pr_err("Bad magic ... probably not HPFS\n");
		goto bail4;
	}

	/* Check version */
	if (!sb_rdonly(s) && superblock->funcversion != 2 && superblock->funcversion != 3) {
		pr_err("Bad version %d,%d. Mount readonly to go around\n",
			(int)superblock->version, (int)superblock->funcversion);
		pr_err("please try recent version of HPFS driver at http://artax.karlin.mff.cuni.cz/~mikulas/vyplody/hpfs/index-e.cgi and if it still can't understand this format, contact author - mikulas@artax.karlin.mff.cuni.cz\n");
		goto bail4;
	}

	s->s_flags |= SB_NOATIME;

	/* Fill superblock stuff */
	s->s_magic = HPFS_SUPER_MAGIC;
	s->s_op = &hpfs_sops;
	s->s_d_op = &hpfs_dentry_operations;
	s->s_time_min =  local_to_gmt(s, 0);
	s->s_time_max =  local_to_gmt(s, U32_MAX);

	sbi->sb_root = le32_to_cpu(superblock->root);
	sbi->sb_fs_size = le32_to_cpu(superblock->n_sectors);
	sbi->sb_bitmaps = le32_to_cpu(superblock->bitmaps);
	sbi->sb_dirband_start = le32_to_cpu(superblock->dir_band_start);
	sbi->sb_dirband_size = le32_to_cpu(superblock->n_dir_band);
	sbi->sb_dmap = le32_to_cpu(superblock->dir_band_bitmap);
	sbi->sb_uid = uid;
	sbi->sb_gid = gid;
	sbi->sb_mode = 0777 & ~umask;
	sbi->sb_n_free = -1;
	sbi->sb_n_free_dnodes = -1;
	sbi->sb_lowercase = lowercase;
	sbi->sb_eas = eas;
	sbi->sb_chk = chk;
	sbi->sb_chkdsk = chkdsk;
	sbi->sb_err = errs;
	sbi->sb_timeshift = timeshift;
	sbi->sb_was_error = 0;
	sbi->sb_cp_table = NULL;
	sbi->sb_c_bitmap = -1;
	sbi->sb_max_fwd_alloc = 0xffffff;

	if (sbi->sb_fs_size >= 0x80000000) {
		hpfs_error(s, "invalid size in superblock: %08x",
			(unsigned)sbi->sb_fs_size);
		goto bail4;
	}

	if (spareblock->n_spares_used)
		hpfs_load_hotfix_map(s, spareblock);

	/* Load bitmap directory */
	if (!(sbi->sb_bmp_dir = hpfs_load_bitmap_directory(s, le32_to_cpu(superblock->bitmaps))))
		goto bail4;
	
	/* Check for general fs errors*/
	if (spareblock->dirty && !spareblock->old_wrote) {
		if (errs == 2) {
			pr_err("Improperly stopped, not mounted\n");
			goto bail4;
		}
		hpfs_error(s, "improperly stopped");
	}

	if (!sb_rdonly(s)) {
		spareblock->dirty = 1;
		spareblock->old_wrote = 0;
		mark_buffer_dirty(bh2);
	}

	if (le32_to_cpu(spareblock->n_dnode_spares) != le32_to_cpu(spareblock->n_dnode_spares_free)) {
		if (errs >= 2) {
			pr_err("Spare dnodes used, try chkdsk\n");
			mark_dirty(s, 0);
			goto bail4;
		}
		hpfs_error(s, "warning: spare dnodes used, try chkdsk");
		if (errs == 0)
			pr_err("Proceeding, but your filesystem could be corrupted if you delete files or directories\n");
	}
	if (chk) {
		unsigned a;
		if (le32_to_cpu(superblock->dir_band_end) - le32_to_cpu(superblock->dir_band_start) + 1 != le32_to_cpu(superblock->n_dir_band) ||
		    le32_to_cpu(superblock->dir_band_end) < le32_to_cpu(superblock->dir_band_start) || le32_to_cpu(superblock->n_dir_band) > 0x4000) {
			hpfs_error(s, "dir band size mismatch: dir_band_start==%08x, dir_band_end==%08x, n_dir_band==%08x",
				le32_to_cpu(superblock->dir_band_start), le32_to_cpu(superblock->dir_band_end), le32_to_cpu(superblock->n_dir_band));
			goto bail4;
		}
		a = sbi->sb_dirband_size;
		sbi->sb_dirband_size = 0;
		if (hpfs_chk_sectors(s, le32_to_cpu(superblock->dir_band_start), le32_to_cpu(superblock->n_dir_band), "dir_band") ||
		    hpfs_chk_sectors(s, le32_to_cpu(superblock->dir_band_bitmap), 4, "dir_band_bitmap") ||
		    hpfs_chk_sectors(s, le32_to_cpu(superblock->bitmaps), 4, "bitmaps")) {
			mark_dirty(s, 0);
			goto bail4;
		}
		sbi->sb_dirband_size = a;
	} else
		pr_err("You really don't want any checks? You are crazy...\n");

	/* Load code page table */
	if (le32_to_cpu(spareblock->n_code_pages))
		if (!(sbi->sb_cp_table = hpfs_load_code_page(s, le32_to_cpu(spareblock->code_page_dir))))
			pr_err("code page support is disabled\n");

	brelse(bh2);
	brelse(bh1);
	brelse(bh0);

	root = iget_locked(s, sbi->sb_root);
	if (!root)
		goto bail0;
	hpfs_init_inode(root);
	hpfs_read_inode(root);
	unlock_new_inode(root);
	s->s_root = d_make_root(root);
	if (!s->s_root)
		goto bail0;

	/*
	 * find the root directory's . pointer & finish filling in the inode
	 */

	root_dno = hpfs_fnode_dno(s, sbi->sb_root);
	if (root_dno)
		de = map_dirent(root, root_dno, "\001\001", 2, NULL, &qbh);
	if (!de)
		hpfs_error(s, "unable to find root dir");
	else {
		inode_set_atime(root,
				local_to_gmt(s, le32_to_cpu(de->read_date)),
				0);
		inode_set_mtime(root,
				local_to_gmt(s, le32_to_cpu(de->write_date)),
				0);
		inode_set_ctime(root,
				local_to_gmt(s, le32_to_cpu(de->creation_date)),
				0);
		hpfs_i(root)->i_ea_size = le32_to_cpu(de->ea_size);
		hpfs_i(root)->i_parent_dir = root->i_ino;
		if (root->i_size == -1)
			root->i_size = 2048;
		if (root->i_blocks == -1)
			root->i_blocks = 5;
		hpfs_brelse4(&qbh);
	}
	hpfs_unlock(s);
	return 0;

bail4:	brelse(bh2);
bail3:	brelse(bh1);
bail2:	brelse(bh0);
bail1:
bail0:
	hpfs_unlock(s);
	free_sbi(sbi);
	return -EINVAL;
}

static struct dentry *hpfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, hpfs_fill_super);
}

static struct file_system_type hpfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "hpfs",
	.mount		= hpfs_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("hpfs");

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
MODULE_DESCRIPTION("OS/2 HPFS file system support");
MODULE_LICENSE("GPL");
