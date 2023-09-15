// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Copyright (C) 2019-2021 Paragon Software GmbH, All rights reserved.
 *
 *
 *                 terminology
 *
 * cluster - allocation unit     - 512,1K,2K,4K,...,2M
 * vcn - virtual cluster number  - Offset inside the file in clusters.
 * vbo - virtual byte offset     - Offset inside the file in bytes.
 * lcn - logical cluster number  - 0 based cluster in clusters heap.
 * lbo - logical byte offset     - Absolute position inside volume.
 * run - maps VCN to LCN         - Stored in attributes in packed form.
 * attr - attribute segment      - std/name/data etc records inside MFT.
 * mi  - MFT inode               - One MFT record(usually 1024 bytes or 4K), consists of attributes.
 * ni  - NTFS inode              - Extends linux inode. consists of one or more mft inodes.
 * index - unit inside directory - 2K, 4K, <=page size, does not depend on cluster size.
 *
 * WSL - Windows Subsystem for Linux
 * https://docs.microsoft.com/en-us/windows/wsl/file-permissions
 * It stores uid/gid/mode/dev in xattr
 *
 * ntfs allows up to 2^64 clusters per volume.
 * It means you should use 64 bits lcn to operate with ntfs.
 * Implementation of ntfs.sys uses only 32 bits lcn.
 * Default ntfs3 uses 32 bits lcn too.
 * ntfs3 built with CONFIG_NTFS3_64BIT_CLUSTER (ntfs3_64) uses 64 bits per lcn.
 *
 *
 *     ntfs limits, cluster size is 4K (2^12)
 * -----------------------------------------------------------------------------
 * | Volume size   | Clusters | ntfs.sys | ntfs3  | ntfs3_64 | mkntfs | chkdsk |
 * -----------------------------------------------------------------------------
 * | < 16T, 2^44   |  < 2^32  |  yes     |  yes   |   yes    |  yes   |  yes   |
 * | > 16T, 2^44   |  > 2^32  |  no      |  no    |   yes    |  yes   |  yes   |
 * ----------------------------------------------------------|------------------
 *
 * To mount large volumes as ntfs one should use large cluster size (up to 2M)
 * The maximum volume size in this case is 2^32 * 2^21 = 2^53 = 8P
 *
 *     ntfs limits, cluster size is 2M (2^21)
 * -----------------------------------------------------------------------------
 * | < 8P, 2^53    |  < 2^32  |  yes     |  yes   |   yes    |  yes   |  yes   |
 * | > 8P, 2^53    |  > 2^32  |  no      |  no    |   yes    |  yes   |  yes   |
 * ----------------------------------------------------------|------------------
 *
 */

#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/exportfs.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/log2.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/nls.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/statfs.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"
#ifdef CONFIG_NTFS3_LZX_XPRESS
#include "lib/lib.h"
#endif

#ifdef CONFIG_PRINTK
/*
 * ntfs_printk - Trace warnings/notices/errors.
 *
 * Thanks Joe Perches <joe@perches.com> for implementation
 */
void ntfs_printk(const struct super_block *sb, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	int level;
	struct ntfs_sb_info *sbi = sb->s_fs_info;

	/* Should we use different ratelimits for warnings/notices/errors? */
	if (!___ratelimit(&sbi->msg_ratelimit, "ntfs3"))
		return;

	va_start(args, fmt);

	level = printk_get_level(fmt);
	vaf.fmt = printk_skip_level(fmt);
	vaf.va = &args;
	printk("%c%cntfs3: %s: %pV\n", KERN_SOH_ASCII, level, sb->s_id, &vaf);

	va_end(args);
}

static char s_name_buf[512];
static atomic_t s_name_buf_cnt = ATOMIC_INIT(1); // 1 means 'free s_name_buf'.

/*
 * ntfs_inode_printk
 *
 * Print warnings/notices/errors about inode using name or inode number.
 */
void ntfs_inode_printk(struct inode *inode, const char *fmt, ...)
{
	struct super_block *sb = inode->i_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	char *name;
	va_list args;
	struct va_format vaf;
	int level;

	if (!___ratelimit(&sbi->msg_ratelimit, "ntfs3"))
		return;

	/* Use static allocated buffer, if possible. */
	name = atomic_dec_and_test(&s_name_buf_cnt) ?
		       s_name_buf :
		       kmalloc(sizeof(s_name_buf), GFP_NOFS);

	if (name) {
		struct dentry *de = d_find_alias(inode);
		const u32 name_len = ARRAY_SIZE(s_name_buf) - 1;

		if (de) {
			spin_lock(&de->d_lock);
			snprintf(name, name_len, " \"%s\"", de->d_name.name);
			spin_unlock(&de->d_lock);
			name[name_len] = 0; /* To be sure. */
		} else {
			name[0] = 0;
		}
		dput(de); /* Cocci warns if placed in branch "if (de)" */
	}

	va_start(args, fmt);

	level = printk_get_level(fmt);
	vaf.fmt = printk_skip_level(fmt);
	vaf.va = &args;

	printk("%c%cntfs3: %s: ino=%lx,%s %pV\n", KERN_SOH_ASCII, level,
	       sb->s_id, inode->i_ino, name ? name : "", &vaf);

	va_end(args);

	atomic_inc(&s_name_buf_cnt);
	if (name != s_name_buf)
		kfree(name);
}
#endif

/*
 * Shared memory struct.
 *
 * On-disk ntfs's upcase table is created by ntfs formatter.
 * 'upcase' table is 128K bytes of memory.
 * We should read it into memory when mounting.
 * Several ntfs volumes likely use the same 'upcase' table.
 * It is good idea to share in-memory 'upcase' table between different volumes.
 * Unfortunately winxp/vista/win7 use different upcase tables.
 */
static DEFINE_SPINLOCK(s_shared_lock);

static struct {
	void *ptr;
	u32 len;
	int cnt;
} s_shared[8];

/*
 * ntfs_set_shared
 *
 * Return:
 * * @ptr - If pointer was saved in shared memory.
 * * NULL - If pointer was not shared.
 */
void *ntfs_set_shared(void *ptr, u32 bytes)
{
	void *ret = NULL;
	int i, j = -1;

	spin_lock(&s_shared_lock);
	for (i = 0; i < ARRAY_SIZE(s_shared); i++) {
		if (!s_shared[i].cnt) {
			j = i;
		} else if (bytes == s_shared[i].len &&
			   !memcmp(s_shared[i].ptr, ptr, bytes)) {
			s_shared[i].cnt += 1;
			ret = s_shared[i].ptr;
			break;
		}
	}

	if (!ret && j != -1) {
		s_shared[j].ptr = ptr;
		s_shared[j].len = bytes;
		s_shared[j].cnt = 1;
		ret = ptr;
	}
	spin_unlock(&s_shared_lock);

	return ret;
}

/*
 * ntfs_put_shared
 *
 * Return:
 * * @ptr - If pointer is not shared anymore.
 * * NULL - If pointer is still shared.
 */
void *ntfs_put_shared(void *ptr)
{
	void *ret = ptr;
	int i;

	spin_lock(&s_shared_lock);
	for (i = 0; i < ARRAY_SIZE(s_shared); i++) {
		if (s_shared[i].cnt && s_shared[i].ptr == ptr) {
			if (--s_shared[i].cnt)
				ret = NULL;
			break;
		}
	}
	spin_unlock(&s_shared_lock);

	return ret;
}

static inline void put_mount_options(struct ntfs_mount_options *options)
{
	kfree(options->nls_name);
	unload_nls(options->nls);
	kfree(options);
}

enum Opt {
	Opt_uid,
	Opt_gid,
	Opt_umask,
	Opt_dmask,
	Opt_fmask,
	Opt_immutable,
	Opt_discard,
	Opt_force,
	Opt_sparse,
	Opt_nohidden,
	Opt_hide_dot_files,
	Opt_windows_names,
	Opt_showmeta,
	Opt_acl,
	Opt_iocharset,
	Opt_prealloc,
	Opt_nocase,
	Opt_err,
};

// clang-format off
static const struct fs_parameter_spec ntfs_fs_parameters[] = {
	fsparam_u32("uid",			Opt_uid),
	fsparam_u32("gid",			Opt_gid),
	fsparam_u32oct("umask",			Opt_umask),
	fsparam_u32oct("dmask",			Opt_dmask),
	fsparam_u32oct("fmask",			Opt_fmask),
	fsparam_flag_no("sys_immutable",	Opt_immutable),
	fsparam_flag_no("discard",		Opt_discard),
	fsparam_flag_no("force",		Opt_force),
	fsparam_flag_no("sparse",		Opt_sparse),
	fsparam_flag_no("hidden",		Opt_nohidden),
	fsparam_flag_no("hide_dot_files",	Opt_hide_dot_files),
	fsparam_flag_no("windows_names",	Opt_windows_names),
	fsparam_flag_no("showmeta",		Opt_showmeta),
	fsparam_flag_no("acl",			Opt_acl),
	fsparam_string("iocharset",		Opt_iocharset),
	fsparam_flag_no("prealloc",		Opt_prealloc),
	fsparam_flag_no("nocase",		Opt_nocase),
	{}
};
// clang-format on

/*
 * Load nls table or if @nls is utf8 then return NULL.
 *
 * It is good idea to use here "const char *nls".
 * But load_nls accepts "char*".
 */
static struct nls_table *ntfs_load_nls(char *nls)
{
	struct nls_table *ret;

	if (!nls)
		nls = CONFIG_NLS_DEFAULT;

	if (strcmp(nls, "utf8") == 0)
		return NULL;

	if (strcmp(nls, CONFIG_NLS_DEFAULT) == 0)
		return load_nls_default();

	ret = load_nls(nls);
	if (ret)
		return ret;

	return ERR_PTR(-EINVAL);
}

static int ntfs_fs_parse_param(struct fs_context *fc,
			       struct fs_parameter *param)
{
	struct ntfs_mount_options *opts = fc->fs_private;
	struct fs_parse_result result;
	int opt;

	opt = fs_parse(fc, ntfs_fs_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_uid:
		opts->fs_uid = make_kuid(current_user_ns(), result.uint_32);
		if (!uid_valid(opts->fs_uid))
			return invalf(fc, "ntfs3: Invalid value for uid.");
		break;
	case Opt_gid:
		opts->fs_gid = make_kgid(current_user_ns(), result.uint_32);
		if (!gid_valid(opts->fs_gid))
			return invalf(fc, "ntfs3: Invalid value for gid.");
		break;
	case Opt_umask:
		if (result.uint_32 & ~07777)
			return invalf(fc, "ntfs3: Invalid value for umask.");
		opts->fs_fmask_inv = ~result.uint_32;
		opts->fs_dmask_inv = ~result.uint_32;
		opts->fmask = 1;
		opts->dmask = 1;
		break;
	case Opt_dmask:
		if (result.uint_32 & ~07777)
			return invalf(fc, "ntfs3: Invalid value for dmask.");
		opts->fs_dmask_inv = ~result.uint_32;
		opts->dmask = 1;
		break;
	case Opt_fmask:
		if (result.uint_32 & ~07777)
			return invalf(fc, "ntfs3: Invalid value for fmask.");
		opts->fs_fmask_inv = ~result.uint_32;
		opts->fmask = 1;
		break;
	case Opt_immutable:
		opts->sys_immutable = result.negated ? 0 : 1;
		break;
	case Opt_discard:
		opts->discard = result.negated ? 0 : 1;
		break;
	case Opt_force:
		opts->force = result.negated ? 0 : 1;
		break;
	case Opt_sparse:
		opts->sparse = result.negated ? 0 : 1;
		break;
	case Opt_nohidden:
		opts->nohidden = result.negated ? 1 : 0;
		break;
	case Opt_hide_dot_files:
		opts->hide_dot_files = result.negated ? 0 : 1;
		break;
	case Opt_windows_names:
		opts->windows_names = result.negated ? 0 : 1;
		break;
	case Opt_showmeta:
		opts->showmeta = result.negated ? 0 : 1;
		break;
	case Opt_acl:
		if (!result.negated)
#ifdef CONFIG_NTFS3_FS_POSIX_ACL
			fc->sb_flags |= SB_POSIXACL;
#else
			return invalf(
				fc, "ntfs3: Support for ACL not compiled in!");
#endif
		else
			fc->sb_flags &= ~SB_POSIXACL;
		break;
	case Opt_iocharset:
		kfree(opts->nls_name);
		opts->nls_name = param->string;
		param->string = NULL;
		break;
	case Opt_prealloc:
		opts->prealloc = result.negated ? 0 : 1;
		break;
	case Opt_nocase:
		opts->nocase = result.negated ? 1 : 0;
		break;
	default:
		/* Should not be here unless we forget add case. */
		return -EINVAL;
	}
	return 0;
}

static int ntfs_fs_reconfigure(struct fs_context *fc)
{
	struct super_block *sb = fc->root->d_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	struct ntfs_mount_options *new_opts = fc->fs_private;
	int ro_rw;

	ro_rw = sb_rdonly(sb) && !(fc->sb_flags & SB_RDONLY);
	if (ro_rw && (sbi->flags & NTFS_FLAGS_NEED_REPLAY)) {
		errorf(fc,
		       "ntfs3: Couldn't remount rw because journal is not replayed. Please umount/remount instead\n");
		return -EINVAL;
	}

	new_opts->nls = ntfs_load_nls(new_opts->nls_name);
	if (IS_ERR(new_opts->nls)) {
		new_opts->nls = NULL;
		errorf(fc, "ntfs3: Cannot load iocharset %s",
		       new_opts->nls_name);
		return -EINVAL;
	}
	if (new_opts->nls != sbi->options->nls)
		return invalf(
			fc,
			"ntfs3: Cannot use different iocharset when remounting!");

	sync_filesystem(sb);

	if (ro_rw && (sbi->volume.flags & VOLUME_FLAG_DIRTY) &&
	    !new_opts->force) {
		errorf(fc,
		       "ntfs3: Volume is dirty and \"force\" flag is not set!");
		return -EINVAL;
	}

	swap(sbi->options, fc->fs_private);

	return 0;
}

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *proc_info_root;

/*
 * ntfs3_volinfo:
 *
 * The content of /proc/fs/ntfs3/<dev>/volinfo
 *
 * ntfs3.1
 * cluster size
 * number of clusters
*/
static int ntfs3_volinfo(struct seq_file *m, void *o)
{
	struct super_block *sb = m->private;
	struct ntfs_sb_info *sbi = sb->s_fs_info;

	seq_printf(m, "ntfs%d.%d\n%u\n%zu\n", sbi->volume.major_ver,
		   sbi->volume.minor_ver, sbi->cluster_size,
		   sbi->used.bitmap.nbits);

	return 0;
}

static int ntfs3_volinfo_open(struct inode *inode, struct file *file)
{
	return single_open(file, ntfs3_volinfo, pde_data(inode));
}

/* read /proc/fs/ntfs3/<dev>/label */
static int ntfs3_label_show(struct seq_file *m, void *o)
{
	struct super_block *sb = m->private;
	struct ntfs_sb_info *sbi = sb->s_fs_info;

	seq_printf(m, "%s\n", sbi->volume.label);

	return 0;
}

/* write /proc/fs/ntfs3/<dev>/label */
static ssize_t ntfs3_label_write(struct file *file, const char __user *buffer,
				 size_t count, loff_t *ppos)
{
	int err;
	struct super_block *sb = pde_data(file_inode(file));
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	ssize_t ret = count;
	u8 *label = kmalloc(count, GFP_NOFS);

	if (!label)
		return -ENOMEM;

	if (copy_from_user(label, buffer, ret)) {
		ret = -EFAULT;
		goto out;
	}
	while (ret > 0 && label[ret - 1] == '\n')
		ret -= 1;

	err = ntfs_set_label(sbi, label, ret);

	if (err < 0) {
		ntfs_err(sb, "failed (%d) to write label", err);
		ret = err;
		goto out;
	}

	*ppos += count;
	ret = count;
out:
	kfree(label);
	return ret;
}

static int ntfs3_label_open(struct inode *inode, struct file *file)
{
	return single_open(file, ntfs3_label_show, pde_data(inode));
}

static const struct proc_ops ntfs3_volinfo_fops = {
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_open = ntfs3_volinfo_open,
};

static const struct proc_ops ntfs3_label_fops = {
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_open = ntfs3_label_open,
	.proc_write = ntfs3_label_write,
};

#endif

static struct kmem_cache *ntfs_inode_cachep;

static struct inode *ntfs_alloc_inode(struct super_block *sb)
{
	struct ntfs_inode *ni = alloc_inode_sb(sb, ntfs_inode_cachep, GFP_NOFS);

	if (!ni)
		return NULL;

	memset(ni, 0, offsetof(struct ntfs_inode, vfs_inode));
	mutex_init(&ni->ni_lock);
	return &ni->vfs_inode;
}

static void ntfs_free_inode(struct inode *inode)
{
	struct ntfs_inode *ni = ntfs_i(inode);

	mutex_destroy(&ni->ni_lock);
	kmem_cache_free(ntfs_inode_cachep, ni);
}

static void init_once(void *foo)
{
	struct ntfs_inode *ni = foo;

	inode_init_once(&ni->vfs_inode);
}

/*
 * Noinline to reduce binary size.
 */
static noinline void ntfs3_put_sbi(struct ntfs_sb_info *sbi)
{
	wnd_close(&sbi->mft.bitmap);
	wnd_close(&sbi->used.bitmap);

	if (sbi->mft.ni)
		iput(&sbi->mft.ni->vfs_inode);

	if (sbi->security.ni)
		iput(&sbi->security.ni->vfs_inode);

	if (sbi->reparse.ni)
		iput(&sbi->reparse.ni->vfs_inode);

	if (sbi->objid.ni)
		iput(&sbi->objid.ni->vfs_inode);

	if (sbi->volume.ni)
		iput(&sbi->volume.ni->vfs_inode);

	ntfs_update_mftmirr(sbi, 0);

	indx_clear(&sbi->security.index_sii);
	indx_clear(&sbi->security.index_sdh);
	indx_clear(&sbi->reparse.index_r);
	indx_clear(&sbi->objid.index_o);
}

static void ntfs3_free_sbi(struct ntfs_sb_info *sbi)
{
	kfree(sbi->new_rec);
	kvfree(ntfs_put_shared(sbi->upcase));
	kfree(sbi->def_table);
	kfree(sbi->compress.lznt);
#ifdef CONFIG_NTFS3_LZX_XPRESS
	xpress_free_decompressor(sbi->compress.xpress);
	lzx_free_decompressor(sbi->compress.lzx);
#endif
	kfree(sbi);
}

static void ntfs_put_super(struct super_block *sb)
{
	struct ntfs_sb_info *sbi = sb->s_fs_info;

#ifdef CONFIG_PROC_FS
	// Remove /proc/fs/ntfs3/..
	if (sbi->procdir) {
		remove_proc_entry("label", sbi->procdir);
		remove_proc_entry("volinfo", sbi->procdir);
		remove_proc_entry(sb->s_id, proc_info_root);
		sbi->procdir = NULL;
	}
#endif

	/* Mark rw ntfs as clear, if possible. */
	ntfs_set_state(sbi, NTFS_DIRTY_CLEAR);
	ntfs3_put_sbi(sbi);
}

static int ntfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	struct wnd_bitmap *wnd = &sbi->used.bitmap;

	buf->f_type = sb->s_magic;
	buf->f_bsize = sbi->cluster_size;
	buf->f_blocks = wnd->nbits;

	buf->f_bfree = buf->f_bavail = wnd_zeroes(wnd);
	buf->f_fsid.val[0] = sbi->volume.ser_num;
	buf->f_fsid.val[1] = (sbi->volume.ser_num >> 32);
	buf->f_namelen = NTFS_NAME_LEN;

	return 0;
}

static int ntfs_show_options(struct seq_file *m, struct dentry *root)
{
	struct super_block *sb = root->d_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	struct ntfs_mount_options *opts = sbi->options;
	struct user_namespace *user_ns = seq_user_ns(m);

	seq_printf(m, ",uid=%u", from_kuid_munged(user_ns, opts->fs_uid));
	seq_printf(m, ",gid=%u", from_kgid_munged(user_ns, opts->fs_gid));
	if (opts->dmask)
		seq_printf(m, ",dmask=%04o", opts->fs_dmask_inv ^ 0xffff);
	if (opts->fmask)
		seq_printf(m, ",fmask=%04o", opts->fs_fmask_inv ^ 0xffff);
	if (opts->sys_immutable)
		seq_puts(m, ",sys_immutable");
	if (opts->discard)
		seq_puts(m, ",discard");
	if (opts->force)
		seq_puts(m, ",force");
	if (opts->sparse)
		seq_puts(m, ",sparse");
	if (opts->nohidden)
		seq_puts(m, ",nohidden");
	if (opts->hide_dot_files)
		seq_puts(m, ",hide_dot_files");
	if (opts->windows_names)
		seq_puts(m, ",windows_names");
	if (opts->showmeta)
		seq_puts(m, ",showmeta");
	if (sb->s_flags & SB_POSIXACL)
		seq_puts(m, ",acl");
	if (opts->nls)
		seq_printf(m, ",iocharset=%s", opts->nls->charset);
	else
		seq_puts(m, ",iocharset=utf8");
	if (opts->prealloc)
		seq_puts(m, ",prealloc");
	if (opts->nocase)
		seq_puts(m, ",nocase");

	return 0;
}

/*
 * ntfs_sync_fs - super_operations::sync_fs
 */
static int ntfs_sync_fs(struct super_block *sb, int wait)
{
	int err = 0, err2;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	struct ntfs_inode *ni;
	struct inode *inode;

	ni = sbi->security.ni;
	if (ni) {
		inode = &ni->vfs_inode;
		err2 = _ni_write_inode(inode, wait);
		if (err2 && !err)
			err = err2;
	}

	ni = sbi->objid.ni;
	if (ni) {
		inode = &ni->vfs_inode;
		err2 = _ni_write_inode(inode, wait);
		if (err2 && !err)
			err = err2;
	}

	ni = sbi->reparse.ni;
	if (ni) {
		inode = &ni->vfs_inode;
		err2 = _ni_write_inode(inode, wait);
		if (err2 && !err)
			err = err2;
	}

	if (!err)
		ntfs_set_state(sbi, NTFS_DIRTY_CLEAR);

	ntfs_update_mftmirr(sbi, wait);

	return err;
}

static const struct super_operations ntfs_sops = {
	.alloc_inode = ntfs_alloc_inode,
	.free_inode = ntfs_free_inode,
	.evict_inode = ntfs_evict_inode,
	.put_super = ntfs_put_super,
	.statfs = ntfs_statfs,
	.show_options = ntfs_show_options,
	.sync_fs = ntfs_sync_fs,
	.write_inode = ntfs3_write_inode,
};

static struct inode *ntfs_export_get_inode(struct super_block *sb, u64 ino,
					   u32 generation)
{
	struct MFT_REF ref;
	struct inode *inode;

	ref.low = cpu_to_le32(ino);
#ifdef CONFIG_NTFS3_64BIT_CLUSTER
	ref.high = cpu_to_le16(ino >> 32);
#else
	ref.high = 0;
#endif
	ref.seq = cpu_to_le16(generation);

	inode = ntfs_iget5(sb, &ref, NULL);
	if (!IS_ERR(inode) && is_bad_inode(inode)) {
		iput(inode);
		inode = ERR_PTR(-ESTALE);
	}

	return inode;
}

static struct dentry *ntfs_fh_to_dentry(struct super_block *sb, struct fid *fid,
					int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    ntfs_export_get_inode);
}

static struct dentry *ntfs_fh_to_parent(struct super_block *sb, struct fid *fid,
					int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    ntfs_export_get_inode);
}

/* TODO: == ntfs_sync_inode */
static int ntfs_nfs_commit_metadata(struct inode *inode)
{
	return _ni_write_inode(inode, 1);
}

static const struct export_operations ntfs_export_ops = {
	.fh_to_dentry = ntfs_fh_to_dentry,
	.fh_to_parent = ntfs_fh_to_parent,
	.get_parent = ntfs3_get_parent,
	.commit_metadata = ntfs_nfs_commit_metadata,
};

/*
 * format_size_gb - Return Gb,Mb to print with "%u.%02u Gb".
 */
static u32 format_size_gb(const u64 bytes, u32 *mb)
{
	/* Do simple right 30 bit shift of 64 bit value. */
	u64 kbytes = bytes >> 10;
	u32 kbytes32 = kbytes;

	*mb = (100 * (kbytes32 & 0xfffff) + 0x7ffff) >> 20;
	if (*mb >= 100)
		*mb = 99;

	return (kbytes32 >> 20) | (((u32)(kbytes >> 32)) << 12);
}

static u32 true_sectors_per_clst(const struct NTFS_BOOT *boot)
{
	if (boot->sectors_per_clusters <= 0x80)
		return boot->sectors_per_clusters;
	if (boot->sectors_per_clusters >= 0xf4) /* limit shift to 2MB max */
		return 1U << (-(s8)boot->sectors_per_clusters);
	return -EINVAL;
}

/*
 * ntfs_init_from_boot - Init internal info from on-disk boot sector.
 *
 * NTFS mount begins from boot - special formatted 512 bytes.
 * There are two boots: the first and the last 512 bytes of volume.
 * The content of boot is not changed during ntfs life.
 *
 * NOTE: ntfs.sys checks only first (primary) boot.
 * chkdsk checks both boots.
 */
static int ntfs_init_from_boot(struct super_block *sb, u32 sector_size,
			       u64 dev_size, struct NTFS_BOOT **boot2)
{
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	int err;
	u32 mb, gb, boot_sector_size, sct_per_clst, record_size;
	u64 sectors, clusters, mlcn, mlcn2;
	struct NTFS_BOOT *boot;
	struct buffer_head *bh;
	struct MFT_REC *rec;
	u16 fn, ao;
	u8 cluster_bits;
	u32 boot_off = 0;
	const char *hint = "Primary boot";

	sbi->volume.blocks = dev_size >> PAGE_SHIFT;

	bh = ntfs_bread(sb, 0);
	if (!bh)
		return -EIO;

check_boot:
	err = -EINVAL;
	boot = (struct NTFS_BOOT *)Add2Ptr(bh->b_data, boot_off);

	if (memcmp(boot->system_id, "NTFS    ", sizeof("NTFS    ") - 1)) {
		ntfs_err(sb, "%s signature is not NTFS.", hint);
		goto out;
	}

	/* 0x55AA is not mandaroty. Thanks Maxim Suhanov*/
	/*if (0x55 != boot->boot_magic[0] || 0xAA != boot->boot_magic[1])
	 *	goto out;
	 */

	boot_sector_size = ((u32)boot->bytes_per_sector[1] << 8) |
			   boot->bytes_per_sector[0];
	if (boot_sector_size < SECTOR_SIZE ||
	    !is_power_of_2(boot_sector_size)) {
		ntfs_err(sb, "%s: invalid bytes per sector %u.", hint,
			 boot_sector_size);
		goto out;
	}

	/* cluster size: 512, 1K, 2K, 4K, ... 2M */
	sct_per_clst = true_sectors_per_clst(boot);
	if ((int)sct_per_clst < 0 || !is_power_of_2(sct_per_clst)) {
		ntfs_err(sb, "%s: invalid sectors per cluster %u.", hint,
			 sct_per_clst);
		goto out;
	}

	sbi->cluster_size = boot_sector_size * sct_per_clst;
	sbi->cluster_bits = cluster_bits = blksize_bits(sbi->cluster_size);
	sbi->cluster_mask = sbi->cluster_size - 1;
	sbi->cluster_mask_inv = ~(u64)sbi->cluster_mask;

	mlcn = le64_to_cpu(boot->mft_clst);
	mlcn2 = le64_to_cpu(boot->mft2_clst);
	sectors = le64_to_cpu(boot->sectors_per_volume);

	if (mlcn * sct_per_clst >= sectors || mlcn2 * sct_per_clst >= sectors) {
		ntfs_err(
			sb,
			"%s: start of MFT 0x%llx (0x%llx) is out of volume 0x%llx.",
			hint, mlcn, mlcn2, sectors);
		goto out;
	}

	sbi->record_size = record_size =
		boot->record_size < 0 ? 1 << (-boot->record_size) :
					(u32)boot->record_size << cluster_bits;
	sbi->record_bits = blksize_bits(record_size);
	sbi->attr_size_tr = (5 * record_size >> 4); // ~320 bytes

	/* Check MFT record size. */
	if (record_size < SECTOR_SIZE || !is_power_of_2(record_size)) {
		ntfs_err(sb, "%s: invalid bytes per MFT record %u (%d).", hint,
			 record_size, boot->record_size);
		goto out;
	}

	if (record_size > MAXIMUM_BYTES_PER_MFT) {
		ntfs_err(sb, "Unsupported bytes per MFT record %u.",
			 record_size);
		goto out;
	}

	sbi->index_size = boot->index_size < 0 ?
				  1u << (-boot->index_size) :
				  (u32)boot->index_size << cluster_bits;

	/* Check index record size. */
	if (sbi->index_size < SECTOR_SIZE || !is_power_of_2(sbi->index_size)) {
		ntfs_err(sb, "%s: invalid bytes per index %u(%d).", hint,
			 sbi->index_size, boot->index_size);
		goto out;
	}

	if (sbi->index_size > MAXIMUM_BYTES_PER_INDEX) {
		ntfs_err(sb, "%s: unsupported bytes per index %u.", hint,
			 sbi->index_size);
		goto out;
	}

	sbi->volume.size = sectors * boot_sector_size;

	gb = format_size_gb(sbi->volume.size + boot_sector_size, &mb);

	/*
	 * - Volume formatted and mounted with the same sector size.
	 * - Volume formatted 4K and mounted as 512.
	 * - Volume formatted 512 and mounted as 4K.
	 */
	if (boot_sector_size != sector_size) {
		ntfs_warn(
			sb,
			"Different NTFS sector size (%u) and media sector size (%u).",
			boot_sector_size, sector_size);
		dev_size += sector_size - 1;
	}

	sbi->mft.lbo = mlcn << cluster_bits;
	sbi->mft.lbo2 = mlcn2 << cluster_bits;

	/* Compare boot's cluster and sector. */
	if (sbi->cluster_size < boot_sector_size) {
		ntfs_err(sb, "%s: invalid bytes per cluster (%u).", hint,
			 sbi->cluster_size);
		goto out;
	}

	/* Compare boot's cluster and media sector. */
	if (sbi->cluster_size < sector_size) {
		/* No way to use ntfs_get_block in this case. */
		ntfs_err(
			sb,
			"Failed to mount 'cause NTFS's cluster size (%u) is less than media sector size (%u).",
			sbi->cluster_size, sector_size);
		goto out;
	}

	sbi->max_bytes_per_attr =
		record_size - ALIGN(MFTRECORD_FIXUP_OFFSET, 8) -
		ALIGN(((record_size >> SECTOR_SHIFT) * sizeof(short)), 8) -
		ALIGN(sizeof(enum ATTR_TYPE), 8);

	sbi->volume.ser_num = le64_to_cpu(boot->serial_num);

	/* Warning if RAW volume. */
	if (dev_size < sbi->volume.size + boot_sector_size) {
		u32 mb0, gb0;

		gb0 = format_size_gb(dev_size, &mb0);
		ntfs_warn(
			sb,
			"RAW NTFS volume: Filesystem size %u.%02u Gb > volume size %u.%02u Gb. Mount in read-only.",
			gb, mb, gb0, mb0);
		sb->s_flags |= SB_RDONLY;
	}

	clusters = sbi->volume.size >> cluster_bits;
#ifndef CONFIG_NTFS3_64BIT_CLUSTER
	/* 32 bits per cluster. */
	if (clusters >> 32) {
		ntfs_notice(
			sb,
			"NTFS %u.%02u Gb is too big to use 32 bits per cluster.",
			gb, mb);
		goto out;
	}
#elif BITS_PER_LONG < 64
#error "CONFIG_NTFS3_64BIT_CLUSTER incompatible in 32 bit OS"
#endif

	sbi->used.bitmap.nbits = clusters;

	rec = kzalloc(record_size, GFP_NOFS);
	if (!rec) {
		err = -ENOMEM;
		goto out;
	}

	sbi->new_rec = rec;
	rec->rhdr.sign = NTFS_FILE_SIGNATURE;
	rec->rhdr.fix_off = cpu_to_le16(MFTRECORD_FIXUP_OFFSET);
	fn = (sbi->record_size >> SECTOR_SHIFT) + 1;
	rec->rhdr.fix_num = cpu_to_le16(fn);
	ao = ALIGN(MFTRECORD_FIXUP_OFFSET + sizeof(short) * fn, 8);
	rec->attr_off = cpu_to_le16(ao);
	rec->used = cpu_to_le32(ao + ALIGN(sizeof(enum ATTR_TYPE), 8));
	rec->total = cpu_to_le32(sbi->record_size);
	((struct ATTRIB *)Add2Ptr(rec, ao))->type = ATTR_END;

	sb_set_blocksize(sb, min_t(u32, sbi->cluster_size, PAGE_SIZE));

	sbi->block_mask = sb->s_blocksize - 1;
	sbi->blocks_per_cluster = sbi->cluster_size >> sb->s_blocksize_bits;
	sbi->volume.blocks = sbi->volume.size >> sb->s_blocksize_bits;

	/* Maximum size for normal files. */
	sbi->maxbytes = (clusters << cluster_bits) - 1;

#ifdef CONFIG_NTFS3_64BIT_CLUSTER
	if (clusters >= (1ull << (64 - cluster_bits)))
		sbi->maxbytes = -1;
	sbi->maxbytes_sparse = -1;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
#else
	/* Maximum size for sparse file. */
	sbi->maxbytes_sparse = (1ull << (cluster_bits + 32)) - 1;
	sb->s_maxbytes = 0xFFFFFFFFull << cluster_bits;
#endif

	/*
	 * Compute the MFT zone at two steps.
	 * It would be nice if we are able to allocate 1/8 of
	 * total clusters for MFT but not more then 512 MB.
	 */
	sbi->zone_max = min_t(CLST, 0x20000000 >> cluster_bits, clusters >> 3);

	err = 0;

	if (bh->b_blocknr && !sb_rdonly(sb)) {
		/*
	     * Alternative boot is ok but primary is not ok.
	     * Do not update primary boot here 'cause it may be faked boot.
	     * Let ntfs to be mounted and update boot later.
	     */
		*boot2 = kmemdup(boot, sizeof(*boot), GFP_NOFS | __GFP_NOWARN);
	}

out:
	if (err == -EINVAL && !bh->b_blocknr && dev_size > PAGE_SHIFT) {
		u32 block_size = min_t(u32, sector_size, PAGE_SIZE);
		u64 lbo = dev_size - sizeof(*boot);

		/*
	 	 * Try alternative boot (last sector)
		 */
		brelse(bh);

		sb_set_blocksize(sb, block_size);
		bh = ntfs_bread(sb, lbo >> blksize_bits(block_size));
		if (!bh)
			return -EINVAL;

		boot_off = lbo & (block_size - 1);
		hint = "Alternative boot";
		goto check_boot;
	}
	brelse(bh);

	return err;
}

/*
 * ntfs_fill_super - Try to mount.
 */
static int ntfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	int err;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	struct block_device *bdev = sb->s_bdev;
	struct ntfs_mount_options *options;
	struct inode *inode;
	struct ntfs_inode *ni;
	size_t i, tt, bad_len, bad_frags;
	CLST vcn, lcn, len;
	struct ATTRIB *attr;
	const struct VOLUME_INFO *info;
	u32 idx, done, bytes;
	struct ATTR_DEF_ENTRY *t;
	u16 *shared;
	struct MFT_REF ref;
	bool ro = sb_rdonly(sb);
	struct NTFS_BOOT *boot2 = NULL;

	ref.high = 0;

	sbi->sb = sb;
	sbi->options = options = fc->fs_private;
	fc->fs_private = NULL;
	sb->s_flags |= SB_NODIRATIME;
	sb->s_magic = 0x7366746e; // "ntfs"
	sb->s_op = &ntfs_sops;
	sb->s_export_op = &ntfs_export_ops;
	sb->s_time_gran = NTFS_TIME_GRAN; // 100 nsec
	sb->s_xattr = ntfs_xattr_handlers;
	sb->s_d_op = options->nocase ? &ntfs_dentry_ops : NULL;

	options->nls = ntfs_load_nls(options->nls_name);
	if (IS_ERR(options->nls)) {
		options->nls = NULL;
		errorf(fc, "Cannot load nls %s", options->nls_name);
		err = -EINVAL;
		goto out;
	}

	if (bdev_max_discard_sectors(bdev) && bdev_discard_granularity(bdev)) {
		sbi->discard_granularity = bdev_discard_granularity(bdev);
		sbi->discard_granularity_mask_inv =
			~(u64)(sbi->discard_granularity - 1);
	}

	/* Parse boot. */
	err = ntfs_init_from_boot(sb, bdev_logical_block_size(bdev),
				  bdev_nr_bytes(bdev), &boot2);
	if (err)
		goto out;

	/*
	 * Load $Volume. This should be done before $LogFile
	 * 'cause 'sbi->volume.ni' is used 'ntfs_set_state'.
	 */
	ref.low = cpu_to_le32(MFT_REC_VOL);
	ref.seq = cpu_to_le16(MFT_REC_VOL);
	inode = ntfs_iget5(sb, &ref, &NAME_VOLUME);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		ntfs_err(sb, "Failed to load $Volume (%d).", err);
		goto out;
	}

	ni = ntfs_i(inode);

	/* Load and save label (not necessary). */
	attr = ni_find_attr(ni, NULL, NULL, ATTR_LABEL, NULL, 0, NULL, NULL);

	if (!attr) {
		/* It is ok if no ATTR_LABEL */
	} else if (!attr->non_res && !is_attr_ext(attr)) {
		/* $AttrDef allows labels to be up to 128 symbols. */
		err = utf16s_to_utf8s(resident_data(attr),
				      le32_to_cpu(attr->res.data_size) >> 1,
				      UTF16_LITTLE_ENDIAN, sbi->volume.label,
				      sizeof(sbi->volume.label));
		if (err < 0)
			sbi->volume.label[0] = 0;
	} else {
		/* Should we break mounting here? */
		//err = -EINVAL;
		//goto put_inode_out;
	}

	attr = ni_find_attr(ni, attr, NULL, ATTR_VOL_INFO, NULL, 0, NULL, NULL);
	if (!attr || is_attr_ext(attr) ||
	    !(info = resident_data_ex(attr, SIZEOF_ATTRIBUTE_VOLUME_INFO))) {
		ntfs_err(sb, "$Volume is corrupted.");
		err = -EINVAL;
		goto put_inode_out;
	}

	sbi->volume.major_ver = info->major_ver;
	sbi->volume.minor_ver = info->minor_ver;
	sbi->volume.flags = info->flags;
	sbi->volume.ni = ni;
	if (info->flags & VOLUME_FLAG_DIRTY) {
		sbi->volume.real_dirty = true;
		ntfs_info(sb, "It is recommened to use chkdsk.");
	}

	/* Load $MFTMirr to estimate recs_mirr. */
	ref.low = cpu_to_le32(MFT_REC_MIRR);
	ref.seq = cpu_to_le16(MFT_REC_MIRR);
	inode = ntfs_iget5(sb, &ref, &NAME_MIRROR);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		ntfs_err(sb, "Failed to load $MFTMirr (%d).", err);
		goto out;
	}

	sbi->mft.recs_mirr = ntfs_up_cluster(sbi, inode->i_size) >>
			     sbi->record_bits;

	iput(inode);

	/* Load LogFile to replay. */
	ref.low = cpu_to_le32(MFT_REC_LOG);
	ref.seq = cpu_to_le16(MFT_REC_LOG);
	inode = ntfs_iget5(sb, &ref, &NAME_LOGFILE);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		ntfs_err(sb, "Failed to load \x24LogFile (%d).", err);
		goto out;
	}

	ni = ntfs_i(inode);

	err = ntfs_loadlog_and_replay(ni, sbi);
	if (err)
		goto put_inode_out;

	iput(inode);

	if ((sbi->flags & NTFS_FLAGS_NEED_REPLAY) && !ro) {
		ntfs_warn(sb, "failed to replay log file. Can't mount rw!");
		err = -EINVAL;
		goto out;
	}

	if ((sbi->volume.flags & VOLUME_FLAG_DIRTY) && !ro && !options->force) {
		ntfs_warn(sb, "volume is dirty and \"force\" flag is not set!");
		err = -EINVAL;
		goto out;
	}

	/* Load $MFT. */
	ref.low = cpu_to_le32(MFT_REC_MFT);
	ref.seq = cpu_to_le16(1);

	inode = ntfs_iget5(sb, &ref, &NAME_MFT);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		ntfs_err(sb, "Failed to load $MFT (%d).", err);
		goto out;
	}

	ni = ntfs_i(inode);

	sbi->mft.used = ni->i_valid >> sbi->record_bits;
	tt = inode->i_size >> sbi->record_bits;
	sbi->mft.next_free = MFT_REC_USER;

	err = wnd_init(&sbi->mft.bitmap, sb, tt);
	if (err)
		goto put_inode_out;

	err = ni_load_all_mi(ni);
	if (err) {
		ntfs_err(sb, "Failed to load $MFT's subrecords (%d).", err);
		goto put_inode_out;
	}

	sbi->mft.ni = ni;

	/* Load $Bitmap. */
	ref.low = cpu_to_le32(MFT_REC_BITMAP);
	ref.seq = cpu_to_le16(MFT_REC_BITMAP);
	inode = ntfs_iget5(sb, &ref, &NAME_BITMAP);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		ntfs_err(sb, "Failed to load $Bitmap (%d).", err);
		goto out;
	}

#ifndef CONFIG_NTFS3_64BIT_CLUSTER
	if (inode->i_size >> 32) {
		err = -EINVAL;
		goto put_inode_out;
	}
#endif

	/* Check bitmap boundary. */
	tt = sbi->used.bitmap.nbits;
	if (inode->i_size < bitmap_size(tt)) {
		ntfs_err(sb, "$Bitmap is corrupted.");
		err = -EINVAL;
		goto put_inode_out;
	}

	err = wnd_init(&sbi->used.bitmap, sb, tt);
	if (err) {
		ntfs_err(sb, "Failed to initialize $Bitmap (%d).", err);
		goto put_inode_out;
	}

	iput(inode);

	/* Compute the MFT zone. */
	err = ntfs_refresh_zone(sbi);
	if (err) {
		ntfs_err(sb, "Failed to initialize MFT zone (%d).", err);
		goto out;
	}

	/* Load $BadClus. */
	ref.low = cpu_to_le32(MFT_REC_BADCLUST);
	ref.seq = cpu_to_le16(MFT_REC_BADCLUST);
	inode = ntfs_iget5(sb, &ref, &NAME_BADCLUS);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		ntfs_err(sb, "Failed to load $BadClus (%d).", err);
		goto out;
	}

	ni = ntfs_i(inode);
	bad_len = bad_frags = 0;
	for (i = 0; run_get_entry(&ni->file.run, i, &vcn, &lcn, &len); i++) {
		if (lcn == SPARSE_LCN)
			continue;

		bad_len += len;
		bad_frags += 1;
		if (ro)
			continue;

		if (wnd_set_used_safe(&sbi->used.bitmap, lcn, len, &tt) || tt) {
			/* Bad blocks marked as free in bitmap. */
			ntfs_set_state(sbi, NTFS_DIRTY_ERROR);
		}
	}
	if (bad_len) {
		/*
		 * Notice about bad blocks.
		 * In normal cases these blocks are marked as used in bitmap.
		 * And we never allocate space in it.
		 */
		ntfs_notice(sb,
			    "Volume contains %zu bad blocks in %zu fragments.",
			    bad_len, bad_frags);
	}
	iput(inode);

	/* Load $AttrDef. */
	ref.low = cpu_to_le32(MFT_REC_ATTR);
	ref.seq = cpu_to_le16(MFT_REC_ATTR);
	inode = ntfs_iget5(sb, &ref, &NAME_ATTRDEF);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		ntfs_err(sb, "Failed to load $AttrDef (%d)", err);
		goto out;
	}

	/*
	 * Typical $AttrDef contains up to 20 entries.
	 * Check for extremely large/small size.
	 */
	if (inode->i_size < sizeof(struct ATTR_DEF_ENTRY) ||
	    inode->i_size > 100 * sizeof(struct ATTR_DEF_ENTRY)) {
		ntfs_err(sb, "Looks like $AttrDef is corrupted (size=%llu).",
			 inode->i_size);
		err = -EINVAL;
		goto put_inode_out;
	}

	bytes = inode->i_size;
	sbi->def_table = t = kmalloc(bytes, GFP_NOFS | __GFP_NOWARN);
	if (!t) {
		err = -ENOMEM;
		goto put_inode_out;
	}

	for (done = idx = 0; done < bytes; done += PAGE_SIZE, idx++) {
		unsigned long tail = bytes - done;
		struct page *page = ntfs_map_page(inode->i_mapping, idx);

		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			ntfs_err(sb, "Failed to read $AttrDef (%d).", err);
			goto put_inode_out;
		}
		memcpy(Add2Ptr(t, done), page_address(page),
		       min(PAGE_SIZE, tail));
		ntfs_unmap_page(page);

		if (!idx && ATTR_STD != t->type) {
			ntfs_err(sb, "$AttrDef is corrupted.");
			err = -EINVAL;
			goto put_inode_out;
		}
	}

	t += 1;
	sbi->def_entries = 1;
	done = sizeof(struct ATTR_DEF_ENTRY);
	sbi->reparse.max_size = MAXIMUM_REPARSE_DATA_BUFFER_SIZE;
	sbi->ea_max_size = 0x10000; /* default formatter value */

	while (done + sizeof(struct ATTR_DEF_ENTRY) <= bytes) {
		u32 t32 = le32_to_cpu(t->type);
		u64 sz = le64_to_cpu(t->max_sz);

		if ((t32 & 0xF) || le32_to_cpu(t[-1].type) >= t32)
			break;

		if (t->type == ATTR_REPARSE)
			sbi->reparse.max_size = sz;
		else if (t->type == ATTR_EA)
			sbi->ea_max_size = sz;

		done += sizeof(struct ATTR_DEF_ENTRY);
		t += 1;
		sbi->def_entries += 1;
	}
	iput(inode);

	/* Load $UpCase. */
	ref.low = cpu_to_le32(MFT_REC_UPCASE);
	ref.seq = cpu_to_le16(MFT_REC_UPCASE);
	inode = ntfs_iget5(sb, &ref, &NAME_UPCASE);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		ntfs_err(sb, "Failed to load $UpCase (%d).", err);
		goto out;
	}

	if (inode->i_size != 0x10000 * sizeof(short)) {
		err = -EINVAL;
		ntfs_err(sb, "$UpCase is corrupted.");
		goto put_inode_out;
	}

	for (idx = 0; idx < (0x10000 * sizeof(short) >> PAGE_SHIFT); idx++) {
		const __le16 *src;
		u16 *dst = Add2Ptr(sbi->upcase, idx << PAGE_SHIFT);
		struct page *page = ntfs_map_page(inode->i_mapping, idx);

		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			ntfs_err(sb, "Failed to read $UpCase (%d).", err);
			goto put_inode_out;
		}

		src = page_address(page);

#ifdef __BIG_ENDIAN
		for (i = 0; i < PAGE_SIZE / sizeof(u16); i++)
			*dst++ = le16_to_cpu(*src++);
#else
		memcpy(dst, src, PAGE_SIZE);
#endif
		ntfs_unmap_page(page);
	}

	shared = ntfs_set_shared(sbi->upcase, 0x10000 * sizeof(short));
	if (shared && sbi->upcase != shared) {
		kvfree(sbi->upcase);
		sbi->upcase = shared;
	}

	iput(inode);

	if (is_ntfs3(sbi)) {
		/* Load $Secure. */
		err = ntfs_security_init(sbi);
		if (err) {
			ntfs_err(sb, "Failed to initialize $Secure (%d).", err);
			goto out;
		}

		/* Load $Extend. */
		err = ntfs_extend_init(sbi);
		if (err) {
			ntfs_warn(sb, "Failed to initialize $Extend.");
			goto load_root;
		}

		/* Load $Extend/$Reparse. */
		err = ntfs_reparse_init(sbi);
		if (err) {
			ntfs_warn(sb, "Failed to initialize $Extend/$Reparse.");
			goto load_root;
		}

		/* Load $Extend/$ObjId. */
		err = ntfs_objid_init(sbi);
		if (err) {
			ntfs_warn(sb, "Failed to initialize $Extend/$ObjId.");
			goto load_root;
		}
	}

load_root:
	/* Load root. */
	ref.low = cpu_to_le32(MFT_REC_ROOT);
	ref.seq = cpu_to_le16(MFT_REC_ROOT);
	inode = ntfs_iget5(sb, &ref, &NAME_ROOT);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		ntfs_err(sb, "Failed to load root (%d).", err);
		goto out;
	}

	/*
	 * Final check. Looks like this case should never occurs.
	 */
	if (!inode->i_op) {
		err = -EINVAL;
		ntfs_err(sb, "Failed to load root (%d).", err);
		goto put_inode_out;
	}

	sb->s_root = d_make_root(inode);
	if (!sb->s_root) {
		err = -ENOMEM;
		goto put_inode_out;
	}

	if (boot2) {
		/*
	     * Alternative boot is ok but primary is not ok.
	     * Volume is recognized as NTFS. Update primary boot.
	     */
		struct buffer_head *bh0 = sb_getblk(sb, 0);
		if (bh0) {
			if (buffer_locked(bh0))
				__wait_on_buffer(bh0);

			lock_buffer(bh0);
			memcpy(bh0->b_data, boot2, sizeof(*boot2));
			set_buffer_uptodate(bh0);
			mark_buffer_dirty(bh0);
			unlock_buffer(bh0);
			if (!sync_dirty_buffer(bh0))
				ntfs_warn(sb, "primary boot is updated");
			put_bh(bh0);
		}

		kfree(boot2);
	}

#ifdef CONFIG_PROC_FS
	/* Create /proc/fs/ntfs3/.. */
	if (proc_info_root) {
		struct proc_dir_entry *e = proc_mkdir(sb->s_id, proc_info_root);
		static_assert((S_IRUGO | S_IWUSR) == 0644);
		if (e) {
			proc_create_data("volinfo", S_IRUGO, e,
					 &ntfs3_volinfo_fops, sb);
			proc_create_data("label", S_IRUGO | S_IWUSR, e,
					 &ntfs3_label_fops, sb);
			sbi->procdir = e;
		}
	}
#endif

	return 0;

put_inode_out:
	iput(inode);
out:
	kfree(boot2);
	return err;
}

void ntfs_unmap_meta(struct super_block *sb, CLST lcn, CLST len)
{
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	struct block_device *bdev = sb->s_bdev;
	sector_t devblock = (u64)lcn * sbi->blocks_per_cluster;
	unsigned long blocks = (u64)len * sbi->blocks_per_cluster;
	unsigned long cnt = 0;
	unsigned long limit = global_zone_page_state(NR_FREE_PAGES)
			      << (PAGE_SHIFT - sb->s_blocksize_bits);

	if (limit >= 0x2000)
		limit -= 0x1000;
	else if (limit < 32)
		limit = 32;
	else
		limit >>= 1;

	while (blocks--) {
		clean_bdev_aliases(bdev, devblock++, 1);
		if (cnt++ >= limit) {
			sync_blockdev(bdev);
			cnt = 0;
		}
	}
}

/*
 * ntfs_discard - Issue a discard request (trim for SSD).
 */
int ntfs_discard(struct ntfs_sb_info *sbi, CLST lcn, CLST len)
{
	int err;
	u64 lbo, bytes, start, end;
	struct super_block *sb;

	if (sbi->used.next_free_lcn == lcn + len)
		sbi->used.next_free_lcn = lcn;

	if (sbi->flags & NTFS_FLAGS_NODISCARD)
		return -EOPNOTSUPP;

	if (!sbi->options->discard)
		return -EOPNOTSUPP;

	lbo = (u64)lcn << sbi->cluster_bits;
	bytes = (u64)len << sbi->cluster_bits;

	/* Align up 'start' on discard_granularity. */
	start = (lbo + sbi->discard_granularity - 1) &
		sbi->discard_granularity_mask_inv;
	/* Align down 'end' on discard_granularity. */
	end = (lbo + bytes) & sbi->discard_granularity_mask_inv;

	sb = sbi->sb;
	if (start >= end)
		return 0;

	err = blkdev_issue_discard(sb->s_bdev, start >> 9, (end - start) >> 9,
				   GFP_NOFS);

	if (err == -EOPNOTSUPP)
		sbi->flags |= NTFS_FLAGS_NODISCARD;

	return err;
}

static int ntfs_fs_get_tree(struct fs_context *fc)
{
	return get_tree_bdev(fc, ntfs_fill_super);
}

/*
 * ntfs_fs_free - Free fs_context.
 *
 * Note that this will be called after fill_super and reconfigure
 * even when they pass. So they have to take pointers if they pass.
 */
static void ntfs_fs_free(struct fs_context *fc)
{
	struct ntfs_mount_options *opts = fc->fs_private;
	struct ntfs_sb_info *sbi = fc->s_fs_info;

	if (sbi) {
		ntfs3_put_sbi(sbi);
		ntfs3_free_sbi(sbi);
	}

	if (opts)
		put_mount_options(opts);
}

// clang-format off
static const struct fs_context_operations ntfs_context_ops = {
	.parse_param	= ntfs_fs_parse_param,
	.get_tree	= ntfs_fs_get_tree,
	.reconfigure	= ntfs_fs_reconfigure,
	.free		= ntfs_fs_free,
};
// clang-format on

/*
 * ntfs_init_fs_context - Initialize sbi and opts
 *
 * This will called when mount/remount. We will first initialize
 * options so that if remount we can use just that.
 */
static int ntfs_init_fs_context(struct fs_context *fc)
{
	struct ntfs_mount_options *opts;
	struct ntfs_sb_info *sbi;

	opts = kzalloc(sizeof(struct ntfs_mount_options), GFP_NOFS);
	if (!opts)
		return -ENOMEM;

	/* Default options. */
	opts->fs_uid = current_uid();
	opts->fs_gid = current_gid();
	opts->fs_fmask_inv = ~current_umask();
	opts->fs_dmask_inv = ~current_umask();

	if (fc->purpose == FS_CONTEXT_FOR_RECONFIGURE)
		goto ok;

	sbi = kzalloc(sizeof(struct ntfs_sb_info), GFP_NOFS);
	if (!sbi)
		goto free_opts;

	sbi->upcase = kvmalloc(0x10000 * sizeof(short), GFP_KERNEL);
	if (!sbi->upcase)
		goto free_sbi;

	ratelimit_state_init(&sbi->msg_ratelimit, DEFAULT_RATELIMIT_INTERVAL,
			     DEFAULT_RATELIMIT_BURST);

	mutex_init(&sbi->compress.mtx_lznt);
#ifdef CONFIG_NTFS3_LZX_XPRESS
	mutex_init(&sbi->compress.mtx_xpress);
	mutex_init(&sbi->compress.mtx_lzx);
#endif

	fc->s_fs_info = sbi;
ok:
	fc->fs_private = opts;
	fc->ops = &ntfs_context_ops;

	return 0;
free_sbi:
	kfree(sbi);
free_opts:
	kfree(opts);
	return -ENOMEM;
}

static void ntfs3_kill_sb(struct super_block *sb)
{
	struct ntfs_sb_info *sbi = sb->s_fs_info;

	kill_block_super(sb);

	if (sbi->options)
		put_mount_options(sbi->options);
	ntfs3_free_sbi(sbi);
}

// clang-format off
static struct file_system_type ntfs_fs_type = {
	.owner			= THIS_MODULE,
	.name			= "ntfs3",
	.init_fs_context	= ntfs_init_fs_context,
	.parameters		= ntfs_fs_parameters,
	.kill_sb		= ntfs3_kill_sb,
	.fs_flags		= FS_REQUIRES_DEV | FS_ALLOW_IDMAP,
};
// clang-format on

static int __init init_ntfs_fs(void)
{
	int err;

	pr_info("ntfs3: Max link count %u\n", NTFS_LINK_MAX);

	if (IS_ENABLED(CONFIG_NTFS3_FS_POSIX_ACL))
		pr_info("ntfs3: Enabled Linux POSIX ACLs support\n");
	if (IS_ENABLED(CONFIG_NTFS3_64BIT_CLUSTER))
		pr_notice(
			"ntfs3: Warning: Activated 64 bits per cluster. Windows does not support this\n");
	if (IS_ENABLED(CONFIG_NTFS3_LZX_XPRESS))
		pr_info("ntfs3: Read-only LZX/Xpress compression included\n");


#ifdef CONFIG_PROC_FS
	/* Create "/proc/fs/ntfs3" */
	proc_info_root = proc_mkdir("fs/ntfs3", NULL);
#endif

	err = ntfs3_init_bitmap();
	if (err)
		return err;

	ntfs_inode_cachep = kmem_cache_create(
		"ntfs_inode_cache", sizeof(struct ntfs_inode), 0,
		(SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD | SLAB_ACCOUNT),
		init_once);
	if (!ntfs_inode_cachep) {
		err = -ENOMEM;
		goto out1;
	}

	err = register_filesystem(&ntfs_fs_type);
	if (err)
		goto out;

	return 0;
out:
	kmem_cache_destroy(ntfs_inode_cachep);
out1:
	ntfs3_exit_bitmap();
	return err;
}

static void __exit exit_ntfs_fs(void)
{
	rcu_barrier();
	kmem_cache_destroy(ntfs_inode_cachep);
	unregister_filesystem(&ntfs_fs_type);
	ntfs3_exit_bitmap();

#ifdef CONFIG_PROC_FS
	if (proc_info_root)
		remove_proc_entry("fs/ntfs3", NULL);
#endif

}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ntfs3 read/write filesystem");
#ifdef CONFIG_NTFS3_FS_POSIX_ACL
MODULE_INFO(behaviour, "Enabled Linux POSIX ACLs support");
#endif
#ifdef CONFIG_NTFS3_64BIT_CLUSTER
MODULE_INFO(
	cluster,
	"Warning: Activated 64 bits per cluster. Windows does not support this");
#endif
#ifdef CONFIG_NTFS3_LZX_XPRESS
MODULE_INFO(compression, "Read-only lzx/xpress compression included");
#endif

MODULE_AUTHOR("Konstantin Komarov");
MODULE_ALIAS_FS("ntfs3");

module_init(init_ntfs_fs);
module_exit(exit_ntfs_fs);
