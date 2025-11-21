// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/super.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/sched/mm.h>
#include <linux/statfs.h>
#include <linux/kthread.h>
#include <linux/parser.h>
#include <linux/mount.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/random.h>
#include <linux/exportfs.h>
#include <linux/blkdev.h>
#include <linux/quotaops.h>
#include <linux/f2fs_fs.h>
#include <linux/sysfs.h>
#include <linux/quota.h>
#include <linux/unicode.h>
#include <linux/part_stat.h>
#include <linux/zstd.h>
#include <linux/lz4.h>
#include <linux/ctype.h>
#include <linux/fs_parser.h>

#include "f2fs.h"
#include "node.h"
#include "segment.h"
#include "xattr.h"
#include "gc.h"
#include "iostat.h"

#define CREATE_TRACE_POINTS
#include <trace/events/f2fs.h>

static struct kmem_cache *f2fs_inode_cachep;

#ifdef CONFIG_F2FS_FAULT_INJECTION

const char *f2fs_fault_name[FAULT_MAX] = {
	[FAULT_KMALLOC]			= "kmalloc",
	[FAULT_KVMALLOC]		= "kvmalloc",
	[FAULT_PAGE_ALLOC]		= "page alloc",
	[FAULT_PAGE_GET]		= "page get",
	[FAULT_ALLOC_BIO]		= "alloc bio(obsolete)",
	[FAULT_ALLOC_NID]		= "alloc nid",
	[FAULT_ORPHAN]			= "orphan",
	[FAULT_BLOCK]			= "no more block",
	[FAULT_DIR_DEPTH]		= "too big dir depth",
	[FAULT_EVICT_INODE]		= "evict_inode fail",
	[FAULT_TRUNCATE]		= "truncate fail",
	[FAULT_READ_IO]			= "read IO error",
	[FAULT_CHECKPOINT]		= "checkpoint error",
	[FAULT_DISCARD]			= "discard error",
	[FAULT_WRITE_IO]		= "write IO error",
	[FAULT_SLAB_ALLOC]		= "slab alloc",
	[FAULT_DQUOT_INIT]		= "dquot initialize",
	[FAULT_LOCK_OP]			= "lock_op",
	[FAULT_BLKADDR_VALIDITY]	= "invalid blkaddr",
	[FAULT_BLKADDR_CONSISTENCE]	= "inconsistent blkaddr",
	[FAULT_NO_SEGMENT]		= "no free segment",
	[FAULT_INCONSISTENT_FOOTER]	= "inconsistent footer",
	[FAULT_TIMEOUT]			= "timeout",
	[FAULT_VMALLOC]			= "vmalloc",
};

int f2fs_build_fault_attr(struct f2fs_sb_info *sbi, unsigned long rate,
				unsigned long type, enum fault_option fo)
{
	struct f2fs_fault_info *ffi = &F2FS_OPTION(sbi).fault_info;

	if (fo & FAULT_ALL) {
		memset(ffi, 0, sizeof(struct f2fs_fault_info));
		return 0;
	}

	if (fo & FAULT_RATE) {
		if (rate > INT_MAX)
			return -EINVAL;
		atomic_set(&ffi->inject_ops, 0);
		ffi->inject_rate = (int)rate;
		f2fs_info(sbi, "build fault injection rate: %lu", rate);
	}

	if (fo & FAULT_TYPE) {
		if (type >= BIT(FAULT_MAX))
			return -EINVAL;
		ffi->inject_type = (unsigned int)type;
		f2fs_info(sbi, "build fault injection type: 0x%lx", type);
	}

	return 0;
}
#endif

/* f2fs-wide shrinker description */
static struct shrinker *f2fs_shrinker_info;

static int __init f2fs_init_shrinker(void)
{
	f2fs_shrinker_info = shrinker_alloc(0, "f2fs-shrinker");
	if (!f2fs_shrinker_info)
		return -ENOMEM;

	f2fs_shrinker_info->count_objects = f2fs_shrink_count;
	f2fs_shrinker_info->scan_objects = f2fs_shrink_scan;

	shrinker_register(f2fs_shrinker_info);

	return 0;
}

static void f2fs_exit_shrinker(void)
{
	shrinker_free(f2fs_shrinker_info);
}

enum {
	Opt_gc_background,
	Opt_disable_roll_forward,
	Opt_norecovery,
	Opt_discard,
	Opt_noheap,
	Opt_heap,
	Opt_user_xattr,
	Opt_acl,
	Opt_active_logs,
	Opt_disable_ext_identify,
	Opt_inline_xattr,
	Opt_inline_xattr_size,
	Opt_inline_data,
	Opt_inline_dentry,
	Opt_flush_merge,
	Opt_barrier,
	Opt_fastboot,
	Opt_extent_cache,
	Opt_data_flush,
	Opt_reserve_root,
	Opt_reserve_node,
	Opt_resgid,
	Opt_resuid,
	Opt_mode,
	Opt_fault_injection,
	Opt_fault_type,
	Opt_lazytime,
	Opt_quota,
	Opt_usrquota,
	Opt_grpquota,
	Opt_prjquota,
	Opt_usrjquota,
	Opt_grpjquota,
	Opt_prjjquota,
	Opt_alloc,
	Opt_fsync,
	Opt_test_dummy_encryption,
	Opt_inlinecrypt,
	Opt_checkpoint_disable,
	Opt_checkpoint_disable_cap,
	Opt_checkpoint_disable_cap_perc,
	Opt_checkpoint_enable,
	Opt_checkpoint_merge,
	Opt_compress_algorithm,
	Opt_compress_log_size,
	Opt_nocompress_extension,
	Opt_compress_extension,
	Opt_compress_chksum,
	Opt_compress_mode,
	Opt_compress_cache,
	Opt_atgc,
	Opt_gc_merge,
	Opt_discard_unit,
	Opt_memory_mode,
	Opt_age_extent_cache,
	Opt_errors,
	Opt_nat_bits,
	Opt_jqfmt,
	Opt_checkpoint,
	Opt_lookup_mode,
	Opt_err,
};

static const struct constant_table f2fs_param_background_gc[] = {
	{"on",		BGGC_MODE_ON},
	{"off",		BGGC_MODE_OFF},
	{"sync",	BGGC_MODE_SYNC},
	{}
};

static const struct constant_table f2fs_param_mode[] = {
	{"adaptive",		FS_MODE_ADAPTIVE},
	{"lfs",			FS_MODE_LFS},
	{"fragment:segment",	FS_MODE_FRAGMENT_SEG},
	{"fragment:block",	FS_MODE_FRAGMENT_BLK},
	{}
};

static const struct constant_table f2fs_param_jqfmt[] = {
	{"vfsold",	QFMT_VFS_OLD},
	{"vfsv0",	QFMT_VFS_V0},
	{"vfsv1",	QFMT_VFS_V1},
	{}
};

static const struct constant_table f2fs_param_alloc_mode[] = {
	{"default",	ALLOC_MODE_DEFAULT},
	{"reuse",	ALLOC_MODE_REUSE},
	{}
};
static const struct constant_table f2fs_param_fsync_mode[] = {
	{"posix",	FSYNC_MODE_POSIX},
	{"strict",	FSYNC_MODE_STRICT},
	{"nobarrier",	FSYNC_MODE_NOBARRIER},
	{}
};

static const struct constant_table f2fs_param_compress_mode[] = {
	{"fs",		COMPR_MODE_FS},
	{"user",	COMPR_MODE_USER},
	{}
};

static const struct constant_table f2fs_param_discard_unit[] = {
	{"block",	DISCARD_UNIT_BLOCK},
	{"segment",	DISCARD_UNIT_SEGMENT},
	{"section",	DISCARD_UNIT_SECTION},
	{}
};

static const struct constant_table f2fs_param_memory_mode[] = {
	{"normal",	MEMORY_MODE_NORMAL},
	{"low",		MEMORY_MODE_LOW},
	{}
};

static const struct constant_table f2fs_param_errors[] = {
	{"remount-ro",	MOUNT_ERRORS_READONLY},
	{"continue",	MOUNT_ERRORS_CONTINUE},
	{"panic",	MOUNT_ERRORS_PANIC},
	{}
};

static const struct constant_table f2fs_param_lookup_mode[] = {
	{"perf",	LOOKUP_PERF},
	{"compat",	LOOKUP_COMPAT},
	{"auto",	LOOKUP_AUTO},
	{}
};

static const struct fs_parameter_spec f2fs_param_specs[] = {
	fsparam_enum("background_gc", Opt_gc_background, f2fs_param_background_gc),
	fsparam_flag("disable_roll_forward", Opt_disable_roll_forward),
	fsparam_flag("norecovery", Opt_norecovery),
	fsparam_flag_no("discard", Opt_discard),
	fsparam_flag("no_heap", Opt_noheap),
	fsparam_flag("heap", Opt_heap),
	fsparam_flag_no("user_xattr", Opt_user_xattr),
	fsparam_flag_no("acl", Opt_acl),
	fsparam_s32("active_logs", Opt_active_logs),
	fsparam_flag("disable_ext_identify", Opt_disable_ext_identify),
	fsparam_flag_no("inline_xattr", Opt_inline_xattr),
	fsparam_s32("inline_xattr_size", Opt_inline_xattr_size),
	fsparam_flag_no("inline_data", Opt_inline_data),
	fsparam_flag_no("inline_dentry", Opt_inline_dentry),
	fsparam_flag_no("flush_merge", Opt_flush_merge),
	fsparam_flag_no("barrier", Opt_barrier),
	fsparam_flag("fastboot", Opt_fastboot),
	fsparam_flag_no("extent_cache", Opt_extent_cache),
	fsparam_flag("data_flush", Opt_data_flush),
	fsparam_u32("reserve_root", Opt_reserve_root),
	fsparam_u32("reserve_node", Opt_reserve_node),
	fsparam_gid("resgid", Opt_resgid),
	fsparam_uid("resuid", Opt_resuid),
	fsparam_enum("mode", Opt_mode, f2fs_param_mode),
	fsparam_s32("fault_injection", Opt_fault_injection),
	fsparam_u32("fault_type", Opt_fault_type),
	fsparam_flag_no("lazytime", Opt_lazytime),
	fsparam_flag_no("quota", Opt_quota),
	fsparam_flag("usrquota", Opt_usrquota),
	fsparam_flag("grpquota", Opt_grpquota),
	fsparam_flag("prjquota", Opt_prjquota),
	fsparam_string_empty("usrjquota", Opt_usrjquota),
	fsparam_string_empty("grpjquota", Opt_grpjquota),
	fsparam_string_empty("prjjquota", Opt_prjjquota),
	fsparam_flag("nat_bits", Opt_nat_bits),
	fsparam_enum("jqfmt", Opt_jqfmt, f2fs_param_jqfmt),
	fsparam_enum("alloc_mode", Opt_alloc, f2fs_param_alloc_mode),
	fsparam_enum("fsync_mode", Opt_fsync, f2fs_param_fsync_mode),
	fsparam_string("test_dummy_encryption", Opt_test_dummy_encryption),
	fsparam_flag("test_dummy_encryption", Opt_test_dummy_encryption),
	fsparam_flag("inlinecrypt", Opt_inlinecrypt),
	fsparam_string("checkpoint", Opt_checkpoint),
	fsparam_flag_no("checkpoint_merge", Opt_checkpoint_merge),
	fsparam_string("compress_algorithm", Opt_compress_algorithm),
	fsparam_u32("compress_log_size", Opt_compress_log_size),
	fsparam_string("compress_extension", Opt_compress_extension),
	fsparam_string("nocompress_extension", Opt_nocompress_extension),
	fsparam_flag("compress_chksum", Opt_compress_chksum),
	fsparam_enum("compress_mode", Opt_compress_mode, f2fs_param_compress_mode),
	fsparam_flag("compress_cache", Opt_compress_cache),
	fsparam_flag("atgc", Opt_atgc),
	fsparam_flag_no("gc_merge", Opt_gc_merge),
	fsparam_enum("discard_unit", Opt_discard_unit, f2fs_param_discard_unit),
	fsparam_enum("memory", Opt_memory_mode, f2fs_param_memory_mode),
	fsparam_flag("age_extent_cache", Opt_age_extent_cache),
	fsparam_enum("errors", Opt_errors, f2fs_param_errors),
	fsparam_enum("lookup_mode", Opt_lookup_mode, f2fs_param_lookup_mode),
	{}
};

/* Resort to a match_table for this interestingly formatted option */
static match_table_t f2fs_checkpoint_tokens = {
	{Opt_checkpoint_disable, "disable"},
	{Opt_checkpoint_disable_cap, "disable:%u"},
	{Opt_checkpoint_disable_cap_perc, "disable:%u%%"},
	{Opt_checkpoint_enable, "enable"},
	{Opt_err, NULL},
};

#define F2FS_SPEC_background_gc			(1 << 0)
#define F2FS_SPEC_inline_xattr_size		(1 << 1)
#define F2FS_SPEC_active_logs			(1 << 2)
#define F2FS_SPEC_reserve_root			(1 << 3)
#define F2FS_SPEC_resgid			(1 << 4)
#define F2FS_SPEC_resuid			(1 << 5)
#define F2FS_SPEC_mode				(1 << 6)
#define F2FS_SPEC_fault_injection		(1 << 7)
#define F2FS_SPEC_fault_type			(1 << 8)
#define F2FS_SPEC_jqfmt				(1 << 9)
#define F2FS_SPEC_alloc_mode			(1 << 10)
#define F2FS_SPEC_fsync_mode			(1 << 11)
#define F2FS_SPEC_checkpoint_disable_cap	(1 << 12)
#define F2FS_SPEC_checkpoint_disable_cap_perc	(1 << 13)
#define F2FS_SPEC_compress_level		(1 << 14)
#define F2FS_SPEC_compress_algorithm		(1 << 15)
#define F2FS_SPEC_compress_log_size		(1 << 16)
#define F2FS_SPEC_compress_extension		(1 << 17)
#define F2FS_SPEC_nocompress_extension		(1 << 18)
#define F2FS_SPEC_compress_chksum		(1 << 19)
#define F2FS_SPEC_compress_mode			(1 << 20)
#define F2FS_SPEC_discard_unit			(1 << 21)
#define F2FS_SPEC_memory_mode			(1 << 22)
#define F2FS_SPEC_errors			(1 << 23)
#define F2FS_SPEC_lookup_mode			(1 << 24)
#define F2FS_SPEC_reserve_node			(1 << 25)

struct f2fs_fs_context {
	struct f2fs_mount_info info;
	unsigned int	opt_mask;	/* Bits changed */
	unsigned int	spec_mask;
	unsigned short	qname_mask;
};

#define F2FS_CTX_INFO(ctx)	((ctx)->info)

static inline void ctx_set_opt(struct f2fs_fs_context *ctx,
			       unsigned int flag)
{
	ctx->info.opt |= flag;
	ctx->opt_mask |= flag;
}

static inline void ctx_clear_opt(struct f2fs_fs_context *ctx,
				 unsigned int flag)
{
	ctx->info.opt &= ~flag;
	ctx->opt_mask |= flag;
}

static inline bool ctx_test_opt(struct f2fs_fs_context *ctx,
				unsigned int flag)
{
	return ctx->info.opt & flag;
}

void f2fs_printk(struct f2fs_sb_info *sbi, bool limit_rate,
					const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	int level;

	va_start(args, fmt);

	level = printk_get_level(fmt);
	vaf.fmt = printk_skip_level(fmt);
	vaf.va = &args;
	if (limit_rate)
		if (sbi)
			printk_ratelimited("%c%cF2FS-fs (%s): %pV\n",
				KERN_SOH_ASCII, level, sbi->sb->s_id, &vaf);
		else
			printk_ratelimited("%c%cF2FS-fs: %pV\n",
				KERN_SOH_ASCII, level, &vaf);
	else
		if (sbi)
			printk("%c%cF2FS-fs (%s): %pV\n",
				KERN_SOH_ASCII, level, sbi->sb->s_id, &vaf);
		else
			printk("%c%cF2FS-fs: %pV\n",
				KERN_SOH_ASCII, level, &vaf);

	va_end(args);
}

#if IS_ENABLED(CONFIG_UNICODE)
static const struct f2fs_sb_encodings {
	__u16 magic;
	char *name;
	unsigned int version;
} f2fs_sb_encoding_map[] = {
	{F2FS_ENC_UTF8_12_1, "utf8", UNICODE_AGE(12, 1, 0)},
};

static const struct f2fs_sb_encodings *
f2fs_sb_read_encoding(const struct f2fs_super_block *sb)
{
	__u16 magic = le16_to_cpu(sb->s_encoding);
	int i;

	for (i = 0; i < ARRAY_SIZE(f2fs_sb_encoding_map); i++)
		if (magic == f2fs_sb_encoding_map[i].magic)
			return &f2fs_sb_encoding_map[i];

	return NULL;
}

struct kmem_cache *f2fs_cf_name_slab;
static int __init f2fs_create_casefold_cache(void)
{
	f2fs_cf_name_slab = f2fs_kmem_cache_create("f2fs_casefolded_name",
						   F2FS_NAME_LEN);
	return f2fs_cf_name_slab ? 0 : -ENOMEM;
}

static void f2fs_destroy_casefold_cache(void)
{
	kmem_cache_destroy(f2fs_cf_name_slab);
}
#else
static int __init f2fs_create_casefold_cache(void) { return 0; }
static void f2fs_destroy_casefold_cache(void) { }
#endif

static inline void limit_reserve_root(struct f2fs_sb_info *sbi)
{
	block_t block_limit = min((sbi->user_block_count >> 3),
			sbi->user_block_count - sbi->reserved_blocks);
	block_t node_limit = sbi->total_node_count >> 3;

	/* limit is 12.5% */
	if (test_opt(sbi, RESERVE_ROOT) &&
			F2FS_OPTION(sbi).root_reserved_blocks > block_limit) {
		F2FS_OPTION(sbi).root_reserved_blocks = block_limit;
		f2fs_info(sbi, "Reduce reserved blocks for root = %u",
			  F2FS_OPTION(sbi).root_reserved_blocks);
	}
	if (test_opt(sbi, RESERVE_NODE) &&
			F2FS_OPTION(sbi).root_reserved_nodes > node_limit) {
		F2FS_OPTION(sbi).root_reserved_nodes = node_limit;
		f2fs_info(sbi, "Reduce reserved nodes for root = %u",
			  F2FS_OPTION(sbi).root_reserved_nodes);
	}
	if (!test_opt(sbi, RESERVE_ROOT) && !test_opt(sbi, RESERVE_NODE) &&
		(!uid_eq(F2FS_OPTION(sbi).s_resuid,
				make_kuid(&init_user_ns, F2FS_DEF_RESUID)) ||
		!gid_eq(F2FS_OPTION(sbi).s_resgid,
				make_kgid(&init_user_ns, F2FS_DEF_RESGID))))
		f2fs_info(sbi, "Ignore s_resuid=%u, s_resgid=%u w/o reserve_root"
				" and reserve_node",
			  from_kuid_munged(&init_user_ns,
					   F2FS_OPTION(sbi).s_resuid),
			  from_kgid_munged(&init_user_ns,
					   F2FS_OPTION(sbi).s_resgid));
}

static inline void adjust_unusable_cap_perc(struct f2fs_sb_info *sbi)
{
	if (!F2FS_OPTION(sbi).unusable_cap_perc)
		return;

	if (F2FS_OPTION(sbi).unusable_cap_perc == 100)
		F2FS_OPTION(sbi).unusable_cap = sbi->user_block_count;
	else
		F2FS_OPTION(sbi).unusable_cap = (sbi->user_block_count / 100) *
					F2FS_OPTION(sbi).unusable_cap_perc;

	f2fs_info(sbi, "Adjust unusable cap for checkpoint=disable = %u / %u%%",
			F2FS_OPTION(sbi).unusable_cap,
			F2FS_OPTION(sbi).unusable_cap_perc);
}

static void init_once(void *foo)
{
	struct f2fs_inode_info *fi = (struct f2fs_inode_info *) foo;

	inode_init_once(&fi->vfs_inode);
#ifdef CONFIG_FS_ENCRYPTION
	fi->i_crypt_info = NULL;
#endif
#ifdef CONFIG_FS_VERITY
	fi->i_verity_info = NULL;
#endif
}

#ifdef CONFIG_QUOTA
static const char * const quotatypes[] = INITQFNAMES;
#define QTYPE2NAME(t) (quotatypes[t])
/*
 * Note the name of the specified quota file.
 */
static int f2fs_note_qf_name(struct fs_context *fc, int qtype,
			     struct fs_parameter *param)
{
	struct f2fs_fs_context *ctx = fc->fs_private;
	char *qname;

	if (param->size < 1) {
		f2fs_err(NULL, "Missing quota name");
		return -EINVAL;
	}
	if (strchr(param->string, '/')) {
		f2fs_err(NULL, "quotafile must be on filesystem root");
		return -EINVAL;
	}
	if (ctx->info.s_qf_names[qtype]) {
		if (strcmp(ctx->info.s_qf_names[qtype], param->string) != 0) {
			f2fs_err(NULL, "Quota file already specified");
			return -EINVAL;
		}
		return 0;
	}

	qname = kmemdup_nul(param->string, param->size, GFP_KERNEL);
	if (!qname) {
		f2fs_err(NULL, "Not enough memory for storing quotafile name");
		return -ENOMEM;
	}
	F2FS_CTX_INFO(ctx).s_qf_names[qtype] = qname;
	ctx->qname_mask |= 1 << qtype;
	return 0;
}

/*
 * Clear the name of the specified quota file.
 */
static int f2fs_unnote_qf_name(struct fs_context *fc, int qtype)
{
	struct f2fs_fs_context *ctx = fc->fs_private;

	kfree(ctx->info.s_qf_names[qtype]);
	ctx->info.s_qf_names[qtype] = NULL;
	ctx->qname_mask |= 1 << qtype;
	return 0;
}

static void f2fs_unnote_qf_name_all(struct fs_context *fc)
{
	int i;

	for (i = 0; i < MAXQUOTAS; i++)
		f2fs_unnote_qf_name(fc, i);
}
#endif

static int f2fs_parse_test_dummy_encryption(const struct fs_parameter *param,
					    struct f2fs_fs_context *ctx)
{
	int err;

	if (!IS_ENABLED(CONFIG_FS_ENCRYPTION)) {
		f2fs_warn(NULL, "test_dummy_encryption option not supported");
		return -EINVAL;
	}
	err = fscrypt_parse_test_dummy_encryption(param,
					&ctx->info.dummy_enc_policy);
	if (err) {
		if (err == -EINVAL)
			f2fs_warn(NULL, "Value of option \"%s\" is unrecognized",
				  param->key);
		else if (err == -EEXIST)
			f2fs_warn(NULL, "Conflicting test_dummy_encryption options");
		else
			f2fs_warn(NULL, "Error processing option \"%s\" [%d]",
				  param->key, err);
		return -EINVAL;
	}
	return 0;
}

#ifdef CONFIG_F2FS_FS_COMPRESSION
static bool is_compress_extension_exist(struct f2fs_mount_info *info,
					const char *new_ext, bool is_ext)
{
	unsigned char (*ext)[F2FS_EXTENSION_LEN];
	int ext_cnt;
	int i;

	if (is_ext) {
		ext = info->extensions;
		ext_cnt = info->compress_ext_cnt;
	} else {
		ext = info->noextensions;
		ext_cnt = info->nocompress_ext_cnt;
	}

	for (i = 0; i < ext_cnt; i++) {
		if (!strcasecmp(new_ext, ext[i]))
			return true;
	}

	return false;
}

/*
 * 1. The same extension name cannot not appear in both compress and non-compress extension
 * at the same time.
 * 2. If the compress extension specifies all files, the types specified by the non-compress
 * extension will be treated as special cases and will not be compressed.
 * 3. Don't allow the non-compress extension specifies all files.
 */
static int f2fs_test_compress_extension(unsigned char (*noext)[F2FS_EXTENSION_LEN],
					int noext_cnt,
					unsigned char (*ext)[F2FS_EXTENSION_LEN],
					int ext_cnt)
{
	int index = 0, no_index = 0;

	if (!noext_cnt)
		return 0;

	for (no_index = 0; no_index < noext_cnt; no_index++) {
		if (strlen(noext[no_index]) == 0)
			continue;
		if (!strcasecmp("*", noext[no_index])) {
			f2fs_info(NULL, "Don't allow the nocompress extension specifies all files");
			return -EINVAL;
		}
		for (index = 0; index < ext_cnt; index++) {
			if (strlen(ext[index]) == 0)
				continue;
			if (!strcasecmp(ext[index], noext[no_index])) {
				f2fs_info(NULL, "Don't allow the same extension %s appear in both compress and nocompress extension",
						ext[index]);
				return -EINVAL;
			}
		}
	}
	return 0;
}

#ifdef CONFIG_F2FS_FS_LZ4
static int f2fs_set_lz4hc_level(struct f2fs_fs_context *ctx, const char *str)
{
#ifdef CONFIG_F2FS_FS_LZ4HC
	unsigned int level;

	if (strlen(str) == 3) {
		F2FS_CTX_INFO(ctx).compress_level = 0;
		ctx->spec_mask |= F2FS_SPEC_compress_level;
		return 0;
	}

	str += 3;

	if (str[0] != ':') {
		f2fs_info(NULL, "wrong format, e.g. <alg_name>:<compr_level>");
		return -EINVAL;
	}
	if (kstrtouint(str + 1, 10, &level))
		return -EINVAL;

	if (!f2fs_is_compress_level_valid(COMPRESS_LZ4, level)) {
		f2fs_info(NULL, "invalid lz4hc compress level: %d", level);
		return -EINVAL;
	}

	F2FS_CTX_INFO(ctx).compress_level = level;
	ctx->spec_mask |= F2FS_SPEC_compress_level;
	return 0;
#else
	if (strlen(str) == 3) {
		F2FS_CTX_INFO(ctx).compress_level = 0;
		ctx->spec_mask |= F2FS_SPEC_compress_level;
		return 0;
	}
	f2fs_info(NULL, "kernel doesn't support lz4hc compression");
	return -EINVAL;
#endif
}
#endif

#ifdef CONFIG_F2FS_FS_ZSTD
static int f2fs_set_zstd_level(struct f2fs_fs_context *ctx, const char *str)
{
	int level;
	int len = 4;

	if (strlen(str) == len) {
		F2FS_CTX_INFO(ctx).compress_level = F2FS_ZSTD_DEFAULT_CLEVEL;
		ctx->spec_mask |= F2FS_SPEC_compress_level;
		return 0;
	}

	str += len;

	if (str[0] != ':') {
		f2fs_info(NULL, "wrong format, e.g. <alg_name>:<compr_level>");
		return -EINVAL;
	}
	if (kstrtoint(str + 1, 10, &level))
		return -EINVAL;

	/* f2fs does not support negative compress level now */
	if (level < 0) {
		f2fs_info(NULL, "do not support negative compress level: %d", level);
		return -ERANGE;
	}

	if (!f2fs_is_compress_level_valid(COMPRESS_ZSTD, level)) {
		f2fs_info(NULL, "invalid zstd compress level: %d", level);
		return -EINVAL;
	}

	F2FS_CTX_INFO(ctx).compress_level = level;
	ctx->spec_mask |= F2FS_SPEC_compress_level;
	return 0;
}
#endif
#endif

static int f2fs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct f2fs_fs_context *ctx = fc->fs_private;
#ifdef CONFIG_F2FS_FS_COMPRESSION
	unsigned char (*ext)[F2FS_EXTENSION_LEN];
	unsigned char (*noext)[F2FS_EXTENSION_LEN];
	int ext_cnt, noext_cnt;
	char *name;
#endif
	substring_t args[MAX_OPT_ARGS];
	struct fs_parse_result result;
	int token, ret, arg;

	token = fs_parse(fc, f2fs_param_specs, param, &result);
	if (token < 0)
		return token;

	switch (token) {
	case Opt_gc_background:
		F2FS_CTX_INFO(ctx).bggc_mode = result.uint_32;
		ctx->spec_mask |= F2FS_SPEC_background_gc;
		break;
	case Opt_disable_roll_forward:
		ctx_set_opt(ctx, F2FS_MOUNT_DISABLE_ROLL_FORWARD);
		break;
	case Opt_norecovery:
		/* requires ro mount, checked in f2fs_validate_options */
		ctx_set_opt(ctx, F2FS_MOUNT_NORECOVERY);
		break;
	case Opt_discard:
		if (result.negated)
			ctx_clear_opt(ctx, F2FS_MOUNT_DISCARD);
		else
			ctx_set_opt(ctx, F2FS_MOUNT_DISCARD);
		break;
	case Opt_noheap:
	case Opt_heap:
		f2fs_warn(NULL, "heap/no_heap options were deprecated");
		break;
#ifdef CONFIG_F2FS_FS_XATTR
	case Opt_user_xattr:
		if (result.negated)
			ctx_clear_opt(ctx, F2FS_MOUNT_XATTR_USER);
		else
			ctx_set_opt(ctx, F2FS_MOUNT_XATTR_USER);
		break;
	case Opt_inline_xattr:
		if (result.negated)
			ctx_clear_opt(ctx, F2FS_MOUNT_INLINE_XATTR);
		else
			ctx_set_opt(ctx, F2FS_MOUNT_INLINE_XATTR);
		break;
	case Opt_inline_xattr_size:
		if (result.int_32 < MIN_INLINE_XATTR_SIZE ||
			result.int_32 > MAX_INLINE_XATTR_SIZE) {
			f2fs_err(NULL, "inline xattr size is out of range: %u ~ %u",
				 (u32)MIN_INLINE_XATTR_SIZE, (u32)MAX_INLINE_XATTR_SIZE);
			return -EINVAL;
		}
		ctx_set_opt(ctx, F2FS_MOUNT_INLINE_XATTR_SIZE);
		F2FS_CTX_INFO(ctx).inline_xattr_size = result.int_32;
		ctx->spec_mask |= F2FS_SPEC_inline_xattr_size;
		break;
#else
	case Opt_user_xattr:
	case Opt_inline_xattr:
	case Opt_inline_xattr_size:
		f2fs_info(NULL, "%s options not supported", param->key);
		break;
#endif
#ifdef CONFIG_F2FS_FS_POSIX_ACL
	case Opt_acl:
		if (result.negated)
			ctx_clear_opt(ctx, F2FS_MOUNT_POSIX_ACL);
		else
			ctx_set_opt(ctx, F2FS_MOUNT_POSIX_ACL);
		break;
#else
	case Opt_acl:
		f2fs_info(NULL, "%s options not supported", param->key);
		break;
#endif
	case Opt_active_logs:
		if (result.int_32 != 2 && result.int_32 != 4 &&
			result.int_32 != NR_CURSEG_PERSIST_TYPE)
			return -EINVAL;
		ctx->spec_mask |= F2FS_SPEC_active_logs;
		F2FS_CTX_INFO(ctx).active_logs = result.int_32;
		break;
	case Opt_disable_ext_identify:
		ctx_set_opt(ctx, F2FS_MOUNT_DISABLE_EXT_IDENTIFY);
		break;
	case Opt_inline_data:
		if (result.negated)
			ctx_clear_opt(ctx, F2FS_MOUNT_INLINE_DATA);
		else
			ctx_set_opt(ctx, F2FS_MOUNT_INLINE_DATA);
		break;
	case Opt_inline_dentry:
		if (result.negated)
			ctx_clear_opt(ctx, F2FS_MOUNT_INLINE_DENTRY);
		else
			ctx_set_opt(ctx, F2FS_MOUNT_INLINE_DENTRY);
		break;
	case Opt_flush_merge:
		if (result.negated)
			ctx_clear_opt(ctx, F2FS_MOUNT_FLUSH_MERGE);
		else
			ctx_set_opt(ctx, F2FS_MOUNT_FLUSH_MERGE);
		break;
	case Opt_barrier:
		if (result.negated)
			ctx_set_opt(ctx, F2FS_MOUNT_NOBARRIER);
		else
			ctx_clear_opt(ctx, F2FS_MOUNT_NOBARRIER);
		break;
	case Opt_fastboot:
		ctx_set_opt(ctx, F2FS_MOUNT_FASTBOOT);
		break;
	case Opt_extent_cache:
		if (result.negated)
			ctx_clear_opt(ctx, F2FS_MOUNT_READ_EXTENT_CACHE);
		else
			ctx_set_opt(ctx, F2FS_MOUNT_READ_EXTENT_CACHE);
		break;
	case Opt_data_flush:
		ctx_set_opt(ctx, F2FS_MOUNT_DATA_FLUSH);
		break;
	case Opt_reserve_root:
		ctx_set_opt(ctx, F2FS_MOUNT_RESERVE_ROOT);
		F2FS_CTX_INFO(ctx).root_reserved_blocks = result.uint_32;
		ctx->spec_mask |= F2FS_SPEC_reserve_root;
		break;
	case Opt_reserve_node:
		ctx_set_opt(ctx, F2FS_MOUNT_RESERVE_NODE);
		F2FS_CTX_INFO(ctx).root_reserved_nodes = result.uint_32;
		ctx->spec_mask |= F2FS_SPEC_reserve_node;
		break;
	case Opt_resuid:
		F2FS_CTX_INFO(ctx).s_resuid = result.uid;
		ctx->spec_mask |= F2FS_SPEC_resuid;
		break;
	case Opt_resgid:
		F2FS_CTX_INFO(ctx).s_resgid = result.gid;
		ctx->spec_mask |= F2FS_SPEC_resgid;
		break;
	case Opt_mode:
		F2FS_CTX_INFO(ctx).fs_mode = result.uint_32;
		ctx->spec_mask |= F2FS_SPEC_mode;
		break;
#ifdef CONFIG_F2FS_FAULT_INJECTION
	case Opt_fault_injection:
		F2FS_CTX_INFO(ctx).fault_info.inject_rate = result.int_32;
		ctx->spec_mask |= F2FS_SPEC_fault_injection;
		ctx_set_opt(ctx, F2FS_MOUNT_FAULT_INJECTION);
		break;

	case Opt_fault_type:
		if (result.uint_32 > BIT(FAULT_MAX))
			return -EINVAL;
		F2FS_CTX_INFO(ctx).fault_info.inject_type = result.uint_32;
		ctx->spec_mask |= F2FS_SPEC_fault_type;
		ctx_set_opt(ctx, F2FS_MOUNT_FAULT_INJECTION);
		break;
#else
	case Opt_fault_injection:
	case Opt_fault_type:
		f2fs_info(NULL, "%s options not supported", param->key);
		break;
#endif
	case Opt_lazytime:
		if (result.negated)
			ctx_clear_opt(ctx, F2FS_MOUNT_LAZYTIME);
		else
			ctx_set_opt(ctx, F2FS_MOUNT_LAZYTIME);
		break;
#ifdef CONFIG_QUOTA
	case Opt_quota:
		if (result.negated) {
			ctx_clear_opt(ctx, F2FS_MOUNT_QUOTA);
			ctx_clear_opt(ctx, F2FS_MOUNT_USRQUOTA);
			ctx_clear_opt(ctx, F2FS_MOUNT_GRPQUOTA);
			ctx_clear_opt(ctx, F2FS_MOUNT_PRJQUOTA);
		} else
			ctx_set_opt(ctx, F2FS_MOUNT_USRQUOTA);
		break;
	case Opt_usrquota:
		ctx_set_opt(ctx, F2FS_MOUNT_USRQUOTA);
		break;
	case Opt_grpquota:
		ctx_set_opt(ctx, F2FS_MOUNT_GRPQUOTA);
		break;
	case Opt_prjquota:
		ctx_set_opt(ctx, F2FS_MOUNT_PRJQUOTA);
		break;
	case Opt_usrjquota:
		if (!*param->string)
			ret = f2fs_unnote_qf_name(fc, USRQUOTA);
		else
			ret = f2fs_note_qf_name(fc, USRQUOTA, param);
		if (ret)
			return ret;
		break;
	case Opt_grpjquota:
		if (!*param->string)
			ret = f2fs_unnote_qf_name(fc, GRPQUOTA);
		else
			ret = f2fs_note_qf_name(fc, GRPQUOTA, param);
		if (ret)
			return ret;
		break;
	case Opt_prjjquota:
		if (!*param->string)
			ret = f2fs_unnote_qf_name(fc, PRJQUOTA);
		else
			ret = f2fs_note_qf_name(fc, PRJQUOTA, param);
		if (ret)
			return ret;
		break;
	case Opt_jqfmt:
		F2FS_CTX_INFO(ctx).s_jquota_fmt = result.int_32;
		ctx->spec_mask |= F2FS_SPEC_jqfmt;
		break;
#else
	case Opt_quota:
	case Opt_usrquota:
	case Opt_grpquota:
	case Opt_prjquota:
	case Opt_usrjquota:
	case Opt_grpjquota:
	case Opt_prjjquota:
		f2fs_info(NULL, "quota operations not supported");
		break;
#endif
	case Opt_alloc:
		F2FS_CTX_INFO(ctx).alloc_mode = result.uint_32;
		ctx->spec_mask |= F2FS_SPEC_alloc_mode;
		break;
	case Opt_fsync:
		F2FS_CTX_INFO(ctx).fsync_mode = result.uint_32;
		ctx->spec_mask |= F2FS_SPEC_fsync_mode;
		break;
	case Opt_test_dummy_encryption:
		ret = f2fs_parse_test_dummy_encryption(param, ctx);
		if (ret)
			return ret;
		break;
	case Opt_inlinecrypt:
#ifdef CONFIG_FS_ENCRYPTION_INLINE_CRYPT
		ctx_set_opt(ctx, F2FS_MOUNT_INLINECRYPT);
#else
		f2fs_info(NULL, "inline encryption not supported");
#endif
		break;
	case Opt_checkpoint:
		/*
		 * Initialize args struct so we know whether arg was
		 * found; some options take optional arguments.
		 */
		args[0].from = args[0].to = NULL;
		arg = 0;

		/* revert to match_table for checkpoint= options */
		token = match_token(param->string, f2fs_checkpoint_tokens, args);
		switch (token) {
		case Opt_checkpoint_disable_cap_perc:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			if (arg < 0 || arg > 100)
				return -EINVAL;
			F2FS_CTX_INFO(ctx).unusable_cap_perc = arg;
			ctx->spec_mask |= F2FS_SPEC_checkpoint_disable_cap_perc;
			ctx_set_opt(ctx, F2FS_MOUNT_DISABLE_CHECKPOINT);
			break;
		case Opt_checkpoint_disable_cap:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			F2FS_CTX_INFO(ctx).unusable_cap = arg;
			ctx->spec_mask |= F2FS_SPEC_checkpoint_disable_cap;
			ctx_set_opt(ctx, F2FS_MOUNT_DISABLE_CHECKPOINT);
			break;
		case Opt_checkpoint_disable:
			ctx_set_opt(ctx, F2FS_MOUNT_DISABLE_CHECKPOINT);
			break;
		case Opt_checkpoint_enable:
			F2FS_CTX_INFO(ctx).unusable_cap_perc = 0;
			ctx->spec_mask |= F2FS_SPEC_checkpoint_disable_cap_perc;
			F2FS_CTX_INFO(ctx).unusable_cap = 0;
			ctx->spec_mask |= F2FS_SPEC_checkpoint_disable_cap;
			ctx_clear_opt(ctx, F2FS_MOUNT_DISABLE_CHECKPOINT);
			break;
		default:
			return -EINVAL;
		}
		break;
	case Opt_checkpoint_merge:
		if (result.negated)
			ctx_clear_opt(ctx, F2FS_MOUNT_MERGE_CHECKPOINT);
		else
			ctx_set_opt(ctx, F2FS_MOUNT_MERGE_CHECKPOINT);
		break;
#ifdef CONFIG_F2FS_FS_COMPRESSION
	case Opt_compress_algorithm:
		name = param->string;
		if (!strcmp(name, "lzo")) {
#ifdef CONFIG_F2FS_FS_LZO
			F2FS_CTX_INFO(ctx).compress_level = 0;
			F2FS_CTX_INFO(ctx).compress_algorithm = COMPRESS_LZO;
			ctx->spec_mask |= F2FS_SPEC_compress_level;
			ctx->spec_mask |= F2FS_SPEC_compress_algorithm;
#else
			f2fs_info(NULL, "kernel doesn't support lzo compression");
#endif
		} else if (!strncmp(name, "lz4", 3)) {
#ifdef CONFIG_F2FS_FS_LZ4
			ret = f2fs_set_lz4hc_level(ctx, name);
			if (ret)
				return -EINVAL;
			F2FS_CTX_INFO(ctx).compress_algorithm = COMPRESS_LZ4;
			ctx->spec_mask |= F2FS_SPEC_compress_algorithm;
#else
			f2fs_info(NULL, "kernel doesn't support lz4 compression");
#endif
		} else if (!strncmp(name, "zstd", 4)) {
#ifdef CONFIG_F2FS_FS_ZSTD
			ret = f2fs_set_zstd_level(ctx, name);
			if (ret)
				return -EINVAL;
			F2FS_CTX_INFO(ctx).compress_algorithm = COMPRESS_ZSTD;
			ctx->spec_mask |= F2FS_SPEC_compress_algorithm;
#else
			f2fs_info(NULL, "kernel doesn't support zstd compression");
#endif
		} else if (!strcmp(name, "lzo-rle")) {
#ifdef CONFIG_F2FS_FS_LZORLE
			F2FS_CTX_INFO(ctx).compress_level = 0;
			F2FS_CTX_INFO(ctx).compress_algorithm = COMPRESS_LZORLE;
			ctx->spec_mask |= F2FS_SPEC_compress_level;
			ctx->spec_mask |= F2FS_SPEC_compress_algorithm;
#else
			f2fs_info(NULL, "kernel doesn't support lzorle compression");
#endif
		} else
			return -EINVAL;
		break;
	case Opt_compress_log_size:
		if (result.uint_32 < MIN_COMPRESS_LOG_SIZE ||
		    result.uint_32 > MAX_COMPRESS_LOG_SIZE) {
			f2fs_err(NULL,
				"Compress cluster log size is out of range");
			return -EINVAL;
		}
		F2FS_CTX_INFO(ctx).compress_log_size = result.uint_32;
		ctx->spec_mask |= F2FS_SPEC_compress_log_size;
		break;
	case Opt_compress_extension:
		name = param->string;
		ext = F2FS_CTX_INFO(ctx).extensions;
		ext_cnt = F2FS_CTX_INFO(ctx).compress_ext_cnt;

		if (strlen(name) >= F2FS_EXTENSION_LEN ||
		    ext_cnt >= COMPRESS_EXT_NUM) {
			f2fs_err(NULL, "invalid extension length/number");
			return -EINVAL;
		}

		if (is_compress_extension_exist(&ctx->info, name, true))
			break;

		ret = strscpy(ext[ext_cnt], name, F2FS_EXTENSION_LEN);
		if (ret < 0)
			return ret;
		F2FS_CTX_INFO(ctx).compress_ext_cnt++;
		ctx->spec_mask |= F2FS_SPEC_compress_extension;
		break;
	case Opt_nocompress_extension:
		name = param->string;
		noext = F2FS_CTX_INFO(ctx).noextensions;
		noext_cnt = F2FS_CTX_INFO(ctx).nocompress_ext_cnt;

		if (strlen(name) >= F2FS_EXTENSION_LEN ||
			noext_cnt >= COMPRESS_EXT_NUM) {
			f2fs_err(NULL, "invalid extension length/number");
			return -EINVAL;
		}

		if (is_compress_extension_exist(&ctx->info, name, false))
			break;

		ret = strscpy(noext[noext_cnt], name, F2FS_EXTENSION_LEN);
		if (ret < 0)
			return ret;
		F2FS_CTX_INFO(ctx).nocompress_ext_cnt++;
		ctx->spec_mask |= F2FS_SPEC_nocompress_extension;
		break;
	case Opt_compress_chksum:
		F2FS_CTX_INFO(ctx).compress_chksum = true;
		ctx->spec_mask |= F2FS_SPEC_compress_chksum;
		break;
	case Opt_compress_mode:
		F2FS_CTX_INFO(ctx).compress_mode = result.uint_32;
		ctx->spec_mask |= F2FS_SPEC_compress_mode;
		break;
	case Opt_compress_cache:
		ctx_set_opt(ctx, F2FS_MOUNT_COMPRESS_CACHE);
		break;
#else
	case Opt_compress_algorithm:
	case Opt_compress_log_size:
	case Opt_compress_extension:
	case Opt_nocompress_extension:
	case Opt_compress_chksum:
	case Opt_compress_mode:
	case Opt_compress_cache:
		f2fs_info(NULL, "compression options not supported");
		break;
#endif
	case Opt_atgc:
		ctx_set_opt(ctx, F2FS_MOUNT_ATGC);
		break;
	case Opt_gc_merge:
		if (result.negated)
			ctx_clear_opt(ctx, F2FS_MOUNT_GC_MERGE);
		else
			ctx_set_opt(ctx, F2FS_MOUNT_GC_MERGE);
		break;
	case Opt_discard_unit:
		F2FS_CTX_INFO(ctx).discard_unit = result.uint_32;
		ctx->spec_mask |= F2FS_SPEC_discard_unit;
		break;
	case Opt_memory_mode:
		F2FS_CTX_INFO(ctx).memory_mode = result.uint_32;
		ctx->spec_mask |= F2FS_SPEC_memory_mode;
		break;
	case Opt_age_extent_cache:
		ctx_set_opt(ctx, F2FS_MOUNT_AGE_EXTENT_CACHE);
		break;
	case Opt_errors:
		F2FS_CTX_INFO(ctx).errors = result.uint_32;
		ctx->spec_mask |= F2FS_SPEC_errors;
		break;
	case Opt_nat_bits:
		ctx_set_opt(ctx, F2FS_MOUNT_NAT_BITS);
		break;
	case Opt_lookup_mode:
		F2FS_CTX_INFO(ctx).lookup_mode = result.uint_32;
		ctx->spec_mask |= F2FS_SPEC_lookup_mode;
		break;
	}
	return 0;
}

/*
 * Check quota settings consistency.
 */
static int f2fs_check_quota_consistency(struct fs_context *fc,
					struct super_block *sb)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
 #ifdef CONFIG_QUOTA
	struct f2fs_fs_context *ctx = fc->fs_private;
	bool quota_feature = f2fs_sb_has_quota_ino(sbi);
	bool quota_turnon = sb_any_quota_loaded(sb);
	char *old_qname, *new_qname;
	bool usr_qf_name, grp_qf_name, prj_qf_name, usrquota, grpquota, prjquota;
	int i;

	/*
	 * We do the test below only for project quotas. 'usrquota' and
	 * 'grpquota' mount options are allowed even without quota feature
	 * to support legacy quotas in quota files.
	 */
	if (ctx_test_opt(ctx, F2FS_MOUNT_PRJQUOTA) &&
			!f2fs_sb_has_project_quota(sbi)) {
		f2fs_err(sbi, "Project quota feature not enabled. Cannot enable project quota enforcement.");
		return -EINVAL;
	}

	if (ctx->qname_mask) {
		for (i = 0; i < MAXQUOTAS; i++) {
			if (!(ctx->qname_mask & (1 << i)))
				continue;

			old_qname = F2FS_OPTION(sbi).s_qf_names[i];
			new_qname = F2FS_CTX_INFO(ctx).s_qf_names[i];
			if (quota_turnon &&
				!!old_qname != !!new_qname)
				goto err_jquota_change;

			if (old_qname) {
				if (!new_qname) {
					f2fs_info(sbi, "remove qf_name %s",
								old_qname);
					continue;
				} else if (strcmp(old_qname, new_qname) == 0) {
					ctx->qname_mask &= ~(1 << i);
					continue;
				}
				goto err_jquota_specified;
			}

			if (quota_feature) {
				f2fs_info(sbi, "QUOTA feature is enabled, so ignore qf_name");
				ctx->qname_mask &= ~(1 << i);
				kfree(F2FS_CTX_INFO(ctx).s_qf_names[i]);
				F2FS_CTX_INFO(ctx).s_qf_names[i] = NULL;
			}
		}
	}

	/* Make sure we don't mix old and new quota format */
	usr_qf_name = F2FS_OPTION(sbi).s_qf_names[USRQUOTA] ||
			F2FS_CTX_INFO(ctx).s_qf_names[USRQUOTA];
	grp_qf_name = F2FS_OPTION(sbi).s_qf_names[GRPQUOTA] ||
			F2FS_CTX_INFO(ctx).s_qf_names[GRPQUOTA];
	prj_qf_name = F2FS_OPTION(sbi).s_qf_names[PRJQUOTA] ||
			F2FS_CTX_INFO(ctx).s_qf_names[PRJQUOTA];
	usrquota = test_opt(sbi, USRQUOTA) ||
			ctx_test_opt(ctx, F2FS_MOUNT_USRQUOTA);
	grpquota = test_opt(sbi, GRPQUOTA) ||
			ctx_test_opt(ctx, F2FS_MOUNT_GRPQUOTA);
	prjquota = test_opt(sbi, PRJQUOTA) ||
			ctx_test_opt(ctx, F2FS_MOUNT_PRJQUOTA);

	if (usr_qf_name) {
		ctx_clear_opt(ctx, F2FS_MOUNT_USRQUOTA);
		usrquota = false;
	}
	if (grp_qf_name) {
		ctx_clear_opt(ctx, F2FS_MOUNT_GRPQUOTA);
		grpquota = false;
	}
	if (prj_qf_name) {
		ctx_clear_opt(ctx, F2FS_MOUNT_PRJQUOTA);
		prjquota = false;
	}
	if (usr_qf_name || grp_qf_name || prj_qf_name) {
		if (grpquota || usrquota || prjquota) {
			f2fs_err(sbi, "old and new quota format mixing");
			return -EINVAL;
		}
		if (!(ctx->spec_mask & F2FS_SPEC_jqfmt ||
				F2FS_OPTION(sbi).s_jquota_fmt)) {
			f2fs_err(sbi, "journaled quota format not specified");
			return -EINVAL;
		}
	}
	return 0;

err_jquota_change:
	f2fs_err(sbi, "Cannot change journaled quota options when quota turned on");
	return -EINVAL;
err_jquota_specified:
	f2fs_err(sbi, "%s quota file already specified",
		 QTYPE2NAME(i));
	return -EINVAL;

#else
	if (f2fs_readonly(sbi->sb))
		return 0;
	if (f2fs_sb_has_quota_ino(sbi)) {
		f2fs_info(sbi, "Filesystem with quota feature cannot be mounted RDWR without CONFIG_QUOTA");
		return -EINVAL;
	}
	if (f2fs_sb_has_project_quota(sbi)) {
		f2fs_err(sbi, "Filesystem with project quota feature cannot be mounted RDWR without CONFIG_QUOTA");
		return -EINVAL;
	}

	return 0;
#endif
}

static int f2fs_check_test_dummy_encryption(struct fs_context *fc,
					    struct super_block *sb)
{
	struct f2fs_fs_context *ctx = fc->fs_private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);

	if (!fscrypt_is_dummy_policy_set(&F2FS_CTX_INFO(ctx).dummy_enc_policy))
		return 0;

	if (!f2fs_sb_has_encrypt(sbi)) {
		f2fs_err(sbi, "Encrypt feature is off");
		return -EINVAL;
	}

	/*
	 * This mount option is just for testing, and it's not worthwhile to
	 * implement the extra complexity (e.g. RCU protection) that would be
	 * needed to allow it to be set or changed during remount.  We do allow
	 * it to be specified during remount, but only if there is no change.
	 */
	if (fc->purpose == FS_CONTEXT_FOR_RECONFIGURE) {
		if (fscrypt_dummy_policies_equal(&F2FS_OPTION(sbi).dummy_enc_policy,
				&F2FS_CTX_INFO(ctx).dummy_enc_policy))
			return 0;
		f2fs_warn(sbi, "Can't set or change test_dummy_encryption on remount");
		return -EINVAL;
	}
	return 0;
}

static inline bool test_compression_spec(unsigned int mask)
{
	return mask & (F2FS_SPEC_compress_algorithm
			| F2FS_SPEC_compress_log_size
			| F2FS_SPEC_compress_extension
			| F2FS_SPEC_nocompress_extension
			| F2FS_SPEC_compress_chksum
			| F2FS_SPEC_compress_mode);
}

static inline void clear_compression_spec(struct f2fs_fs_context *ctx)
{
	ctx->spec_mask &= ~(F2FS_SPEC_compress_algorithm
						| F2FS_SPEC_compress_log_size
						| F2FS_SPEC_compress_extension
						| F2FS_SPEC_nocompress_extension
						| F2FS_SPEC_compress_chksum
						| F2FS_SPEC_compress_mode);
}

static int f2fs_check_compression(struct fs_context *fc,
				  struct super_block *sb)
{
#ifdef CONFIG_F2FS_FS_COMPRESSION
	struct f2fs_fs_context *ctx = fc->fs_private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	int i, cnt;

	if (!f2fs_sb_has_compression(sbi)) {
		if (test_compression_spec(ctx->spec_mask) ||
			ctx_test_opt(ctx, F2FS_MOUNT_COMPRESS_CACHE))
			f2fs_info(sbi, "Image doesn't support compression");
		clear_compression_spec(ctx);
		ctx->opt_mask &= ~F2FS_MOUNT_COMPRESS_CACHE;
		return 0;
	}
	if (ctx->spec_mask & F2FS_SPEC_compress_extension) {
		cnt = F2FS_CTX_INFO(ctx).compress_ext_cnt;
		for (i = 0; i < F2FS_CTX_INFO(ctx).compress_ext_cnt; i++) {
			if (is_compress_extension_exist(&F2FS_OPTION(sbi),
					F2FS_CTX_INFO(ctx).extensions[i], true)) {
				F2FS_CTX_INFO(ctx).extensions[i][0] = '\0';
				cnt--;
			}
		}
		if (F2FS_OPTION(sbi).compress_ext_cnt + cnt > COMPRESS_EXT_NUM) {
			f2fs_err(sbi, "invalid extension length/number");
			return -EINVAL;
		}
	}
	if (ctx->spec_mask & F2FS_SPEC_nocompress_extension) {
		cnt = F2FS_CTX_INFO(ctx).nocompress_ext_cnt;
		for (i = 0; i < F2FS_CTX_INFO(ctx).nocompress_ext_cnt; i++) {
			if (is_compress_extension_exist(&F2FS_OPTION(sbi),
					F2FS_CTX_INFO(ctx).noextensions[i], false)) {
				F2FS_CTX_INFO(ctx).noextensions[i][0] = '\0';
				cnt--;
			}
		}
		if (F2FS_OPTION(sbi).nocompress_ext_cnt + cnt > COMPRESS_EXT_NUM) {
			f2fs_err(sbi, "invalid noextension length/number");
			return -EINVAL;
		}
	}

	if (f2fs_test_compress_extension(F2FS_CTX_INFO(ctx).noextensions,
				F2FS_CTX_INFO(ctx).nocompress_ext_cnt,
				F2FS_CTX_INFO(ctx).extensions,
				F2FS_CTX_INFO(ctx).compress_ext_cnt)) {
		f2fs_err(sbi, "new noextensions conflicts with new extensions");
		return -EINVAL;
	}
	if (f2fs_test_compress_extension(F2FS_CTX_INFO(ctx).noextensions,
				F2FS_CTX_INFO(ctx).nocompress_ext_cnt,
				F2FS_OPTION(sbi).extensions,
				F2FS_OPTION(sbi).compress_ext_cnt)) {
		f2fs_err(sbi, "new noextensions conflicts with old extensions");
		return -EINVAL;
	}
	if (f2fs_test_compress_extension(F2FS_OPTION(sbi).noextensions,
				F2FS_OPTION(sbi).nocompress_ext_cnt,
				F2FS_CTX_INFO(ctx).extensions,
				F2FS_CTX_INFO(ctx).compress_ext_cnt)) {
		f2fs_err(sbi, "new extensions conflicts with old noextensions");
		return -EINVAL;
	}
#endif
	return 0;
}

static int f2fs_check_opt_consistency(struct fs_context *fc,
				      struct super_block *sb)
{
	struct f2fs_fs_context *ctx = fc->fs_private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	int err;

	if (ctx_test_opt(ctx, F2FS_MOUNT_NORECOVERY) && !f2fs_readonly(sb))
		return -EINVAL;

	if (f2fs_hw_should_discard(sbi) &&
			(ctx->opt_mask & F2FS_MOUNT_DISCARD) &&
			!ctx_test_opt(ctx, F2FS_MOUNT_DISCARD)) {
		f2fs_warn(sbi, "discard is required for zoned block devices");
		return -EINVAL;
	}

	if (!f2fs_hw_support_discard(sbi) &&
			(ctx->opt_mask & F2FS_MOUNT_DISCARD) &&
			ctx_test_opt(ctx, F2FS_MOUNT_DISCARD)) {
		f2fs_warn(sbi, "device does not support discard");
		ctx_clear_opt(ctx, F2FS_MOUNT_DISCARD);
		ctx->opt_mask &= ~F2FS_MOUNT_DISCARD;
	}

	if (f2fs_sb_has_device_alias(sbi) &&
			(ctx->opt_mask & F2FS_MOUNT_READ_EXTENT_CACHE) &&
			!ctx_test_opt(ctx, F2FS_MOUNT_READ_EXTENT_CACHE)) {
		f2fs_err(sbi, "device aliasing requires extent cache");
		return -EINVAL;
	}

	if (test_opt(sbi, RESERVE_ROOT) &&
			(ctx->opt_mask & F2FS_MOUNT_RESERVE_ROOT) &&
			ctx_test_opt(ctx, F2FS_MOUNT_RESERVE_ROOT)) {
		f2fs_info(sbi, "Preserve previous reserve_root=%u",
			F2FS_OPTION(sbi).root_reserved_blocks);
		ctx_clear_opt(ctx, F2FS_MOUNT_RESERVE_ROOT);
		ctx->opt_mask &= ~F2FS_MOUNT_RESERVE_ROOT;
	}
	if (test_opt(sbi, RESERVE_NODE) &&
			(ctx->opt_mask & F2FS_MOUNT_RESERVE_NODE) &&
			ctx_test_opt(ctx, F2FS_MOUNT_RESERVE_NODE)) {
		f2fs_info(sbi, "Preserve previous reserve_node=%u",
			F2FS_OPTION(sbi).root_reserved_nodes);
		ctx_clear_opt(ctx, F2FS_MOUNT_RESERVE_NODE);
		ctx->opt_mask &= ~F2FS_MOUNT_RESERVE_NODE;
	}

	err = f2fs_check_test_dummy_encryption(fc, sb);
	if (err)
		return err;

	err = f2fs_check_compression(fc, sb);
	if (err)
		return err;

	err = f2fs_check_quota_consistency(fc, sb);
	if (err)
		return err;

	if (!IS_ENABLED(CONFIG_UNICODE) && f2fs_sb_has_casefold(sbi)) {
		f2fs_err(sbi,
			"Filesystem with casefold feature cannot be mounted without CONFIG_UNICODE");
		return -EINVAL;
	}

	/*
	 * The BLKZONED feature indicates that the drive was formatted with
	 * zone alignment optimization. This is optional for host-aware
	 * devices, but mandatory for host-managed zoned block devices.
	 */
	if (f2fs_sb_has_blkzoned(sbi)) {
		if (F2FS_CTX_INFO(ctx).bggc_mode == BGGC_MODE_OFF) {
			f2fs_warn(sbi, "zoned devices need bggc");
			return -EINVAL;
		}
#ifdef CONFIG_BLK_DEV_ZONED
		if ((ctx->spec_mask & F2FS_SPEC_discard_unit) &&
		F2FS_CTX_INFO(ctx).discard_unit != DISCARD_UNIT_SECTION) {
			f2fs_info(sbi, "Zoned block device doesn't need small discard, set discard_unit=section by default");
			F2FS_CTX_INFO(ctx).discard_unit = DISCARD_UNIT_SECTION;
		}

		if ((ctx->spec_mask & F2FS_SPEC_mode) &&
		F2FS_CTX_INFO(ctx).fs_mode != FS_MODE_LFS) {
			f2fs_info(sbi, "Only lfs mode is allowed with zoned block device feature");
			return -EINVAL;
		}
#else
		f2fs_err(sbi, "Zoned block device support is not enabled");
		return -EINVAL;
#endif
	}

	if (ctx_test_opt(ctx, F2FS_MOUNT_INLINE_XATTR_SIZE)) {
		if (!f2fs_sb_has_extra_attr(sbi) ||
			!f2fs_sb_has_flexible_inline_xattr(sbi)) {
			f2fs_err(sbi, "extra_attr or flexible_inline_xattr feature is off");
			return -EINVAL;
		}
		if (!ctx_test_opt(ctx, F2FS_MOUNT_INLINE_XATTR) && !test_opt(sbi, INLINE_XATTR)) {
			f2fs_err(sbi, "inline_xattr_size option should be set with inline_xattr option");
			return -EINVAL;
		}
	}

	if (ctx_test_opt(ctx, F2FS_MOUNT_ATGC) &&
	    F2FS_CTX_INFO(ctx).fs_mode == FS_MODE_LFS) {
		f2fs_err(sbi, "LFS is not compatible with ATGC");
		return -EINVAL;
	}

	if (f2fs_is_readonly(sbi) && ctx_test_opt(ctx, F2FS_MOUNT_FLUSH_MERGE)) {
		f2fs_err(sbi, "FLUSH_MERGE not compatible with readonly mode");
		return -EINVAL;
	}

	if (f2fs_sb_has_readonly(sbi) && !f2fs_readonly(sbi->sb)) {
		f2fs_err(sbi, "Allow to mount readonly mode only");
		return -EROFS;
	}
	return 0;
}

static void f2fs_apply_quota_options(struct fs_context *fc,
				     struct super_block *sb)
{
#ifdef CONFIG_QUOTA
	struct f2fs_fs_context *ctx = fc->fs_private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	bool quota_feature = f2fs_sb_has_quota_ino(sbi);
	char *qname;
	int i;

	if (quota_feature)
		return;

	for (i = 0; i < MAXQUOTAS; i++) {
		if (!(ctx->qname_mask & (1 << i)))
			continue;

		qname = F2FS_CTX_INFO(ctx).s_qf_names[i];
		if (qname) {
			qname = kstrdup(F2FS_CTX_INFO(ctx).s_qf_names[i],
					GFP_KERNEL | __GFP_NOFAIL);
			set_opt(sbi, QUOTA);
		}
		F2FS_OPTION(sbi).s_qf_names[i] = qname;
	}

	if (ctx->spec_mask & F2FS_SPEC_jqfmt)
		F2FS_OPTION(sbi).s_jquota_fmt = F2FS_CTX_INFO(ctx).s_jquota_fmt;

	if (quota_feature && F2FS_OPTION(sbi).s_jquota_fmt) {
		f2fs_info(sbi, "QUOTA feature is enabled, so ignore jquota_fmt");
		F2FS_OPTION(sbi).s_jquota_fmt = 0;
	}
#endif
}

static void f2fs_apply_test_dummy_encryption(struct fs_context *fc,
					     struct super_block *sb)
{
	struct f2fs_fs_context *ctx = fc->fs_private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);

	if (!fscrypt_is_dummy_policy_set(&F2FS_CTX_INFO(ctx).dummy_enc_policy) ||
		/* if already set, it was already verified to be the same */
		fscrypt_is_dummy_policy_set(&F2FS_OPTION(sbi).dummy_enc_policy))
		return;
	swap(F2FS_OPTION(sbi).dummy_enc_policy, F2FS_CTX_INFO(ctx).dummy_enc_policy);
	f2fs_warn(sbi, "Test dummy encryption mode enabled");
}

static void f2fs_apply_compression(struct fs_context *fc,
				   struct super_block *sb)
{
#ifdef CONFIG_F2FS_FS_COMPRESSION
	struct f2fs_fs_context *ctx = fc->fs_private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	unsigned char (*ctx_ext)[F2FS_EXTENSION_LEN];
	unsigned char (*sbi_ext)[F2FS_EXTENSION_LEN];
	int ctx_cnt, sbi_cnt, i;

	if (ctx->spec_mask & F2FS_SPEC_compress_level)
		F2FS_OPTION(sbi).compress_level =
					F2FS_CTX_INFO(ctx).compress_level;
	if (ctx->spec_mask & F2FS_SPEC_compress_algorithm)
		F2FS_OPTION(sbi).compress_algorithm =
					F2FS_CTX_INFO(ctx).compress_algorithm;
	if (ctx->spec_mask & F2FS_SPEC_compress_log_size)
		F2FS_OPTION(sbi).compress_log_size =
					F2FS_CTX_INFO(ctx).compress_log_size;
	if (ctx->spec_mask & F2FS_SPEC_compress_chksum)
		F2FS_OPTION(sbi).compress_chksum =
					F2FS_CTX_INFO(ctx).compress_chksum;
	if (ctx->spec_mask & F2FS_SPEC_compress_mode)
		F2FS_OPTION(sbi).compress_mode =
					F2FS_CTX_INFO(ctx).compress_mode;
	if (ctx->spec_mask & F2FS_SPEC_compress_extension) {
		ctx_ext = F2FS_CTX_INFO(ctx).extensions;
		ctx_cnt = F2FS_CTX_INFO(ctx).compress_ext_cnt;
		sbi_ext = F2FS_OPTION(sbi).extensions;
		sbi_cnt = F2FS_OPTION(sbi).compress_ext_cnt;
		for (i = 0; i < ctx_cnt; i++) {
			if (strlen(ctx_ext[i]) == 0)
				continue;
			strscpy(sbi_ext[sbi_cnt], ctx_ext[i]);
			sbi_cnt++;
		}
		F2FS_OPTION(sbi).compress_ext_cnt = sbi_cnt;
	}
	if (ctx->spec_mask & F2FS_SPEC_nocompress_extension) {
		ctx_ext = F2FS_CTX_INFO(ctx).noextensions;
		ctx_cnt = F2FS_CTX_INFO(ctx).nocompress_ext_cnt;
		sbi_ext = F2FS_OPTION(sbi).noextensions;
		sbi_cnt = F2FS_OPTION(sbi).nocompress_ext_cnt;
		for (i = 0; i < ctx_cnt; i++) {
			if (strlen(ctx_ext[i]) == 0)
				continue;
			strscpy(sbi_ext[sbi_cnt], ctx_ext[i]);
			sbi_cnt++;
		}
		F2FS_OPTION(sbi).nocompress_ext_cnt = sbi_cnt;
	}
#endif
}

static void f2fs_apply_options(struct fs_context *fc, struct super_block *sb)
{
	struct f2fs_fs_context *ctx = fc->fs_private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);

	F2FS_OPTION(sbi).opt &= ~ctx->opt_mask;
	F2FS_OPTION(sbi).opt |= F2FS_CTX_INFO(ctx).opt;

	if (ctx->spec_mask & F2FS_SPEC_background_gc)
		F2FS_OPTION(sbi).bggc_mode = F2FS_CTX_INFO(ctx).bggc_mode;
	if (ctx->spec_mask & F2FS_SPEC_inline_xattr_size)
		F2FS_OPTION(sbi).inline_xattr_size =
					F2FS_CTX_INFO(ctx).inline_xattr_size;
	if (ctx->spec_mask & F2FS_SPEC_active_logs)
		F2FS_OPTION(sbi).active_logs = F2FS_CTX_INFO(ctx).active_logs;
	if (ctx->spec_mask & F2FS_SPEC_reserve_root)
		F2FS_OPTION(sbi).root_reserved_blocks =
					F2FS_CTX_INFO(ctx).root_reserved_blocks;
	if (ctx->spec_mask & F2FS_SPEC_reserve_node)
		F2FS_OPTION(sbi).root_reserved_nodes =
					F2FS_CTX_INFO(ctx).root_reserved_nodes;
	if (ctx->spec_mask & F2FS_SPEC_resgid)
		F2FS_OPTION(sbi).s_resgid = F2FS_CTX_INFO(ctx).s_resgid;
	if (ctx->spec_mask & F2FS_SPEC_resuid)
		F2FS_OPTION(sbi).s_resuid = F2FS_CTX_INFO(ctx).s_resuid;
	if (ctx->spec_mask & F2FS_SPEC_mode)
		F2FS_OPTION(sbi).fs_mode = F2FS_CTX_INFO(ctx).fs_mode;
#ifdef CONFIG_F2FS_FAULT_INJECTION
	if (ctx->spec_mask & F2FS_SPEC_fault_injection)
		(void)f2fs_build_fault_attr(sbi,
		F2FS_CTX_INFO(ctx).fault_info.inject_rate, 0, FAULT_RATE);
	if (ctx->spec_mask & F2FS_SPEC_fault_type)
		(void)f2fs_build_fault_attr(sbi, 0,
			F2FS_CTX_INFO(ctx).fault_info.inject_type, FAULT_TYPE);
#endif
	if (ctx->spec_mask & F2FS_SPEC_alloc_mode)
		F2FS_OPTION(sbi).alloc_mode = F2FS_CTX_INFO(ctx).alloc_mode;
	if (ctx->spec_mask & F2FS_SPEC_fsync_mode)
		F2FS_OPTION(sbi).fsync_mode = F2FS_CTX_INFO(ctx).fsync_mode;
	if (ctx->spec_mask & F2FS_SPEC_checkpoint_disable_cap)
		F2FS_OPTION(sbi).unusable_cap = F2FS_CTX_INFO(ctx).unusable_cap;
	if (ctx->spec_mask & F2FS_SPEC_checkpoint_disable_cap_perc)
		F2FS_OPTION(sbi).unusable_cap_perc =
					F2FS_CTX_INFO(ctx).unusable_cap_perc;
	if (ctx->spec_mask & F2FS_SPEC_discard_unit)
		F2FS_OPTION(sbi).discard_unit = F2FS_CTX_INFO(ctx).discard_unit;
	if (ctx->spec_mask & F2FS_SPEC_memory_mode)
		F2FS_OPTION(sbi).memory_mode = F2FS_CTX_INFO(ctx).memory_mode;
	if (ctx->spec_mask & F2FS_SPEC_errors)
		F2FS_OPTION(sbi).errors = F2FS_CTX_INFO(ctx).errors;
	if (ctx->spec_mask & F2FS_SPEC_lookup_mode)
		F2FS_OPTION(sbi).lookup_mode = F2FS_CTX_INFO(ctx).lookup_mode;

	f2fs_apply_compression(fc, sb);
	f2fs_apply_test_dummy_encryption(fc, sb);
	f2fs_apply_quota_options(fc, sb);
}

static int f2fs_sanity_check_options(struct f2fs_sb_info *sbi, bool remount)
{
	if (f2fs_sb_has_device_alias(sbi) &&
	    !test_opt(sbi, READ_EXTENT_CACHE)) {
		f2fs_err(sbi, "device aliasing requires extent cache");
		return -EINVAL;
	}

	if (!remount)
		return 0;

#ifdef CONFIG_BLK_DEV_ZONED
	if (f2fs_sb_has_blkzoned(sbi) &&
	    sbi->max_open_zones < F2FS_OPTION(sbi).active_logs) {
		f2fs_err(sbi,
			"zoned: max open zones %u is too small, need at least %u open zones",
				 sbi->max_open_zones, F2FS_OPTION(sbi).active_logs);
		return -EINVAL;
	}
#endif
	if (f2fs_lfs_mode(sbi) && !IS_F2FS_IPU_DISABLE(sbi)) {
		f2fs_warn(sbi, "LFS is not compatible with IPU");
		return -EINVAL;
	}
	return 0;
}

static struct inode *f2fs_alloc_inode(struct super_block *sb)
{
	struct f2fs_inode_info *fi;

	if (time_to_inject(F2FS_SB(sb), FAULT_SLAB_ALLOC))
		return NULL;

	fi = alloc_inode_sb(sb, f2fs_inode_cachep, GFP_F2FS_ZERO);
	if (!fi)
		return NULL;

	init_once((void *) fi);

	/* Initialize f2fs-specific inode info */
	atomic_set(&fi->dirty_pages, 0);
	atomic_set(&fi->i_compr_blocks, 0);
	atomic_set(&fi->open_count, 0);
	init_f2fs_rwsem(&fi->i_sem);
	spin_lock_init(&fi->i_size_lock);
	INIT_LIST_HEAD(&fi->dirty_list);
	INIT_LIST_HEAD(&fi->gdirty_list);
	INIT_LIST_HEAD(&fi->gdonate_list);
	init_f2fs_rwsem(&fi->i_gc_rwsem[READ]);
	init_f2fs_rwsem(&fi->i_gc_rwsem[WRITE]);
	init_f2fs_rwsem(&fi->i_xattr_sem);

	/* Will be used by directory only */
	fi->i_dir_level = F2FS_SB(sb)->dir_level;

	return &fi->vfs_inode;
}

static int f2fs_drop_inode(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	int ret;

	/*
	 * during filesystem shutdown, if checkpoint is disabled,
	 * drop useless meta/node dirty pages.
	 */
	if (unlikely(is_sbi_flag_set(sbi, SBI_CP_DISABLED))) {
		if (inode->i_ino == F2FS_NODE_INO(sbi) ||
			inode->i_ino == F2FS_META_INO(sbi)) {
			trace_f2fs_drop_inode(inode, 1);
			return 1;
		}
	}

	/*
	 * This is to avoid a deadlock condition like below.
	 * writeback_single_inode(inode)
	 *  - f2fs_write_data_page
	 *    - f2fs_gc -> iput -> evict
	 *       - inode_wait_for_writeback(inode)
	 */
	if ((!inode_unhashed(inode) && inode->i_state & I_SYNC)) {
		if (!inode->i_nlink && !is_bad_inode(inode)) {
			/* to avoid evict_inode call simultaneously */
			__iget(inode);
			spin_unlock(&inode->i_lock);

			/* should remain fi->extent_tree for writepage */
			f2fs_destroy_extent_node(inode);

			sb_start_intwrite(inode->i_sb);
			f2fs_i_size_write(inode, 0);

			f2fs_submit_merged_write_cond(F2FS_I_SB(inode),
					inode, NULL, 0, DATA);
			truncate_inode_pages_final(inode->i_mapping);

			if (F2FS_HAS_BLOCKS(inode))
				f2fs_truncate(inode);

			sb_end_intwrite(inode->i_sb);

			spin_lock(&inode->i_lock);
			atomic_dec(&inode->i_count);
		}
		trace_f2fs_drop_inode(inode, 0);
		return 0;
	}
	ret = inode_generic_drop(inode);
	if (!ret)
		ret = fscrypt_drop_inode(inode);
	trace_f2fs_drop_inode(inode, ret);
	return ret;
}

int f2fs_inode_dirtied(struct inode *inode, bool sync)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	int ret = 0;

	spin_lock(&sbi->inode_lock[DIRTY_META]);
	if (is_inode_flag_set(inode, FI_DIRTY_INODE)) {
		ret = 1;
	} else {
		set_inode_flag(inode, FI_DIRTY_INODE);
		stat_inc_dirty_inode(sbi, DIRTY_META);
	}
	if (sync && list_empty(&F2FS_I(inode)->gdirty_list)) {
		list_add_tail(&F2FS_I(inode)->gdirty_list,
				&sbi->inode_list[DIRTY_META]);
		inc_page_count(sbi, F2FS_DIRTY_IMETA);
	}
	spin_unlock(&sbi->inode_lock[DIRTY_META]);

	/* if atomic write is not committed, set inode w/ atomic dirty */
	if (!ret && f2fs_is_atomic_file(inode) &&
			!is_inode_flag_set(inode, FI_ATOMIC_COMMITTED))
		set_inode_flag(inode, FI_ATOMIC_DIRTIED);

	return ret;
}

void f2fs_inode_synced(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);

	spin_lock(&sbi->inode_lock[DIRTY_META]);
	if (!is_inode_flag_set(inode, FI_DIRTY_INODE)) {
		spin_unlock(&sbi->inode_lock[DIRTY_META]);
		return;
	}
	if (!list_empty(&F2FS_I(inode)->gdirty_list)) {
		list_del_init(&F2FS_I(inode)->gdirty_list);
		dec_page_count(sbi, F2FS_DIRTY_IMETA);
	}
	clear_inode_flag(inode, FI_DIRTY_INODE);
	clear_inode_flag(inode, FI_AUTO_RECOVER);
	stat_dec_dirty_inode(F2FS_I_SB(inode), DIRTY_META);
	spin_unlock(&sbi->inode_lock[DIRTY_META]);
}

/*
 * f2fs_dirty_inode() is called from __mark_inode_dirty()
 *
 * We should call set_dirty_inode to write the dirty inode through write_inode.
 */
static void f2fs_dirty_inode(struct inode *inode, int flags)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);

	if (inode->i_ino == F2FS_NODE_INO(sbi) ||
			inode->i_ino == F2FS_META_INO(sbi))
		return;

	if (is_inode_flag_set(inode, FI_AUTO_RECOVER))
		clear_inode_flag(inode, FI_AUTO_RECOVER);

	f2fs_inode_dirtied(inode, false);
}

static void f2fs_free_inode(struct inode *inode)
{
	fscrypt_free_inode(inode);
	kmem_cache_free(f2fs_inode_cachep, F2FS_I(inode));
}

static void destroy_percpu_info(struct f2fs_sb_info *sbi)
{
	percpu_counter_destroy(&sbi->total_valid_inode_count);
	percpu_counter_destroy(&sbi->rf_node_block_count);
	percpu_counter_destroy(&sbi->alloc_valid_block_count);
}

static void destroy_device_list(struct f2fs_sb_info *sbi)
{
	int i;

	for (i = 0; i < sbi->s_ndevs; i++) {
		if (i > 0)
			bdev_fput(FDEV(i).bdev_file);
#ifdef CONFIG_BLK_DEV_ZONED
		kvfree(FDEV(i).blkz_seq);
#endif
	}
	kvfree(sbi->devs);
}

static void f2fs_put_super(struct super_block *sb)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	int i;
	int err = 0;
	bool done;

	/* unregister procfs/sysfs entries in advance to avoid race case */
	f2fs_unregister_sysfs(sbi);

	f2fs_quota_off_umount(sb);

	/* prevent remaining shrinker jobs */
	mutex_lock(&sbi->umount_mutex);

	/*
	 * flush all issued checkpoints and stop checkpoint issue thread.
	 * after then, all checkpoints should be done by each process context.
	 */
	f2fs_stop_ckpt_thread(sbi);

	/*
	 * We don't need to do checkpoint when superblock is clean.
	 * But, the previous checkpoint was not done by umount, it needs to do
	 * clean checkpoint again.
	 */
	if ((is_sbi_flag_set(sbi, SBI_IS_DIRTY) ||
			!is_set_ckpt_flags(sbi, CP_UMOUNT_FLAG))) {
		struct cp_control cpc = {
			.reason = CP_UMOUNT,
		};
		stat_inc_cp_call_count(sbi, TOTAL_CALL);
		err = f2fs_write_checkpoint(sbi, &cpc);
	}

	/* be sure to wait for any on-going discard commands */
	done = f2fs_issue_discard_timeout(sbi);
	if (f2fs_realtime_discard_enable(sbi) && !sbi->discard_blks && done) {
		struct cp_control cpc = {
			.reason = CP_UMOUNT | CP_TRIMMED,
		};
		stat_inc_cp_call_count(sbi, TOTAL_CALL);
		err = f2fs_write_checkpoint(sbi, &cpc);
	}

	/*
	 * normally superblock is clean, so we need to release this.
	 * In addition, EIO will skip do checkpoint, we need this as well.
	 */
	f2fs_release_ino_entry(sbi, true);

	f2fs_leave_shrinker(sbi);
	mutex_unlock(&sbi->umount_mutex);

	/* our cp_error case, we can wait for any writeback page */
	f2fs_flush_merged_writes(sbi);

	f2fs_wait_on_all_pages(sbi, F2FS_WB_CP_DATA);

	if (err || f2fs_cp_error(sbi)) {
		truncate_inode_pages_final(NODE_MAPPING(sbi));
		truncate_inode_pages_final(META_MAPPING(sbi));
	}

	for (i = 0; i < NR_COUNT_TYPE; i++) {
		if (!get_pages(sbi, i))
			continue;
		f2fs_err(sbi, "detect filesystem reference count leak during "
			"umount, type: %d, count: %lld", i, get_pages(sbi, i));
		f2fs_bug_on(sbi, 1);
	}

	f2fs_bug_on(sbi, sbi->fsync_node_num);

	f2fs_destroy_compress_inode(sbi);

	iput(sbi->node_inode);
	sbi->node_inode = NULL;

	iput(sbi->meta_inode);
	sbi->meta_inode = NULL;

	/*
	 * iput() can update stat information, if f2fs_write_checkpoint()
	 * above failed with error.
	 */
	f2fs_destroy_stats(sbi);

	/* destroy f2fs internal modules */
	f2fs_destroy_node_manager(sbi);
	f2fs_destroy_segment_manager(sbi);

	/* flush s_error_work before sbi destroy */
	flush_work(&sbi->s_error_work);

	f2fs_destroy_post_read_wq(sbi);

	kvfree(sbi->ckpt);

	kfree(sbi->raw_super);

	f2fs_destroy_page_array_cache(sbi);
	f2fs_destroy_xattr_caches(sbi);
#ifdef CONFIG_QUOTA
	for (i = 0; i < MAXQUOTAS; i++)
		kfree(F2FS_OPTION(sbi).s_qf_names[i]);
#endif
	fscrypt_free_dummy_policy(&F2FS_OPTION(sbi).dummy_enc_policy);
	destroy_percpu_info(sbi);
	f2fs_destroy_iostat(sbi);
	for (i = 0; i < NR_PAGE_TYPE; i++)
		kfree(sbi->write_io[i]);
#if IS_ENABLED(CONFIG_UNICODE)
	utf8_unload(sb->s_encoding);
#endif
}

int f2fs_sync_fs(struct super_block *sb, int sync)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	int err = 0;

	if (unlikely(f2fs_cp_error(sbi)))
		return 0;
	if (unlikely(is_sbi_flag_set(sbi, SBI_CP_DISABLED)))
		return 0;

	trace_f2fs_sync_fs(sb, sync);

	if (unlikely(is_sbi_flag_set(sbi, SBI_POR_DOING)))
		return -EAGAIN;

	if (sync) {
		stat_inc_cp_call_count(sbi, TOTAL_CALL);
		err = f2fs_issue_checkpoint(sbi);
	}

	return err;
}

static int f2fs_freeze(struct super_block *sb)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);

	if (f2fs_readonly(sb))
		return 0;

	/* IO error happened before */
	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;

	/* must be clean, since sync_filesystem() was already called */
	if (is_sbi_flag_set(sbi, SBI_IS_DIRTY))
		return -EINVAL;

	sbi->umount_lock_holder = current;

	/* Let's flush checkpoints and stop the thread. */
	f2fs_flush_ckpt_thread(sbi);

	sbi->umount_lock_holder = NULL;

	/* to avoid deadlock on f2fs_evict_inode->SB_FREEZE_FS */
	set_sbi_flag(sbi, SBI_IS_FREEZING);
	return 0;
}

static int f2fs_unfreeze(struct super_block *sb)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);

	/*
	 * It will update discard_max_bytes of mounted lvm device to zero
	 * after creating snapshot on this lvm device, let's drop all
	 * remained discards.
	 * We don't need to disable real-time discard because discard_max_bytes
	 * will recover after removal of snapshot.
	 */
	if (test_opt(sbi, DISCARD) && !f2fs_hw_support_discard(sbi))
		f2fs_issue_discard_timeout(sbi);

	clear_sbi_flag(F2FS_SB(sb), SBI_IS_FREEZING);
	return 0;
}

#ifdef CONFIG_QUOTA
static int f2fs_statfs_project(struct super_block *sb,
				kprojid_t projid, struct kstatfs *buf)
{
	struct kqid qid;
	struct dquot *dquot;
	u64 limit;
	u64 curblock;

	qid = make_kqid_projid(projid);
	dquot = dqget(sb, qid);
	if (IS_ERR(dquot))
		return PTR_ERR(dquot);
	spin_lock(&dquot->dq_dqb_lock);

	limit = min_not_zero(dquot->dq_dqb.dqb_bsoftlimit,
					dquot->dq_dqb.dqb_bhardlimit);
	limit >>= sb->s_blocksize_bits;

	if (limit) {
		uint64_t remaining = 0;

		curblock = (dquot->dq_dqb.dqb_curspace +
			    dquot->dq_dqb.dqb_rsvspace) >> sb->s_blocksize_bits;
		if (limit > curblock)
			remaining = limit - curblock;

		buf->f_blocks = min(buf->f_blocks, limit);
		buf->f_bfree = min(buf->f_bfree, remaining);
		buf->f_bavail = min(buf->f_bavail, remaining);
	}

	limit = min_not_zero(dquot->dq_dqb.dqb_isoftlimit,
					dquot->dq_dqb.dqb_ihardlimit);

	if (limit) {
		uint64_t remaining = 0;

		if (limit > dquot->dq_dqb.dqb_curinodes)
			remaining = limit - dquot->dq_dqb.dqb_curinodes;

		buf->f_files = min(buf->f_files, limit);
		buf->f_ffree = min(buf->f_ffree, remaining);
	}

	spin_unlock(&dquot->dq_dqb_lock);
	dqput(dquot);
	return 0;
}
#endif

static int f2fs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);
	block_t total_count, user_block_count, start_count;
	u64 avail_node_count;
	unsigned int total_valid_node_count;

	total_count = le64_to_cpu(sbi->raw_super->block_count);
	start_count = le32_to_cpu(sbi->raw_super->segment0_blkaddr);
	buf->f_type = F2FS_SUPER_MAGIC;
	buf->f_bsize = sbi->blocksize;

	buf->f_blocks = total_count - start_count;

	spin_lock(&sbi->stat_lock);
	if (sbi->carve_out)
		buf->f_blocks -= sbi->current_reserved_blocks;
	user_block_count = sbi->user_block_count;
	total_valid_node_count = valid_node_count(sbi);
	avail_node_count = sbi->total_node_count - F2FS_RESERVED_NODE_NUM;
	buf->f_bfree = user_block_count - valid_user_blocks(sbi) -
						sbi->current_reserved_blocks;

	if (unlikely(buf->f_bfree <= sbi->unusable_block_count))
		buf->f_bfree = 0;
	else
		buf->f_bfree -= sbi->unusable_block_count;
	spin_unlock(&sbi->stat_lock);

	if (buf->f_bfree > F2FS_OPTION(sbi).root_reserved_blocks)
		buf->f_bavail = buf->f_bfree -
				F2FS_OPTION(sbi).root_reserved_blocks;
	else
		buf->f_bavail = 0;

	if (avail_node_count > user_block_count) {
		buf->f_files = user_block_count;
		buf->f_ffree = buf->f_bavail;
	} else {
		buf->f_files = avail_node_count;
		buf->f_ffree = min(avail_node_count - total_valid_node_count,
					buf->f_bavail);
	}

	buf->f_namelen = F2FS_NAME_LEN;
	buf->f_fsid    = u64_to_fsid(id);

#ifdef CONFIG_QUOTA
	if (is_inode_flag_set(d_inode(dentry), FI_PROJ_INHERIT) &&
			sb_has_quota_limits_enabled(sb, PRJQUOTA)) {
		f2fs_statfs_project(sb, F2FS_I(d_inode(dentry))->i_projid, buf);
	}
#endif
	return 0;
}

static inline void f2fs_show_quota_options(struct seq_file *seq,
					   struct super_block *sb)
{
#ifdef CONFIG_QUOTA
	struct f2fs_sb_info *sbi = F2FS_SB(sb);

	if (F2FS_OPTION(sbi).s_jquota_fmt) {
		char *fmtname = "";

		switch (F2FS_OPTION(sbi).s_jquota_fmt) {
		case QFMT_VFS_OLD:
			fmtname = "vfsold";
			break;
		case QFMT_VFS_V0:
			fmtname = "vfsv0";
			break;
		case QFMT_VFS_V1:
			fmtname = "vfsv1";
			break;
		}
		seq_printf(seq, ",jqfmt=%s", fmtname);
	}

	if (F2FS_OPTION(sbi).s_qf_names[USRQUOTA])
		seq_show_option(seq, "usrjquota",
			F2FS_OPTION(sbi).s_qf_names[USRQUOTA]);

	if (F2FS_OPTION(sbi).s_qf_names[GRPQUOTA])
		seq_show_option(seq, "grpjquota",
			F2FS_OPTION(sbi).s_qf_names[GRPQUOTA]);

	if (F2FS_OPTION(sbi).s_qf_names[PRJQUOTA])
		seq_show_option(seq, "prjjquota",
			F2FS_OPTION(sbi).s_qf_names[PRJQUOTA]);
#endif
}

#ifdef CONFIG_F2FS_FS_COMPRESSION
static inline void f2fs_show_compress_options(struct seq_file *seq,
							struct super_block *sb)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	char *algtype = "";
	int i;

	if (!f2fs_sb_has_compression(sbi))
		return;

	switch (F2FS_OPTION(sbi).compress_algorithm) {
	case COMPRESS_LZO:
		algtype = "lzo";
		break;
	case COMPRESS_LZ4:
		algtype = "lz4";
		break;
	case COMPRESS_ZSTD:
		algtype = "zstd";
		break;
	case COMPRESS_LZORLE:
		algtype = "lzo-rle";
		break;
	}
	seq_printf(seq, ",compress_algorithm=%s", algtype);

	if (F2FS_OPTION(sbi).compress_level)
		seq_printf(seq, ":%d", F2FS_OPTION(sbi).compress_level);

	seq_printf(seq, ",compress_log_size=%u",
			F2FS_OPTION(sbi).compress_log_size);

	for (i = 0; i < F2FS_OPTION(sbi).compress_ext_cnt; i++) {
		seq_printf(seq, ",compress_extension=%s",
			F2FS_OPTION(sbi).extensions[i]);
	}

	for (i = 0; i < F2FS_OPTION(sbi).nocompress_ext_cnt; i++) {
		seq_printf(seq, ",nocompress_extension=%s",
			F2FS_OPTION(sbi).noextensions[i]);
	}

	if (F2FS_OPTION(sbi).compress_chksum)
		seq_puts(seq, ",compress_chksum");

	if (F2FS_OPTION(sbi).compress_mode == COMPR_MODE_FS)
		seq_printf(seq, ",compress_mode=%s", "fs");
	else if (F2FS_OPTION(sbi).compress_mode == COMPR_MODE_USER)
		seq_printf(seq, ",compress_mode=%s", "user");

	if (test_opt(sbi, COMPRESS_CACHE))
		seq_puts(seq, ",compress_cache");
}
#endif

static int f2fs_show_options(struct seq_file *seq, struct dentry *root)
{
	struct f2fs_sb_info *sbi = F2FS_SB(root->d_sb);

	if (F2FS_OPTION(sbi).bggc_mode == BGGC_MODE_SYNC)
		seq_printf(seq, ",background_gc=%s", "sync");
	else if (F2FS_OPTION(sbi).bggc_mode == BGGC_MODE_ON)
		seq_printf(seq, ",background_gc=%s", "on");
	else if (F2FS_OPTION(sbi).bggc_mode == BGGC_MODE_OFF)
		seq_printf(seq, ",background_gc=%s", "off");

	if (test_opt(sbi, GC_MERGE))
		seq_puts(seq, ",gc_merge");
	else
		seq_puts(seq, ",nogc_merge");

	if (test_opt(sbi, DISABLE_ROLL_FORWARD))
		seq_puts(seq, ",disable_roll_forward");
	if (test_opt(sbi, NORECOVERY))
		seq_puts(seq, ",norecovery");
	if (test_opt(sbi, DISCARD)) {
		seq_puts(seq, ",discard");
		if (F2FS_OPTION(sbi).discard_unit == DISCARD_UNIT_BLOCK)
			seq_printf(seq, ",discard_unit=%s", "block");
		else if (F2FS_OPTION(sbi).discard_unit == DISCARD_UNIT_SEGMENT)
			seq_printf(seq, ",discard_unit=%s", "segment");
		else if (F2FS_OPTION(sbi).discard_unit == DISCARD_UNIT_SECTION)
			seq_printf(seq, ",discard_unit=%s", "section");
	} else {
		seq_puts(seq, ",nodiscard");
	}
#ifdef CONFIG_F2FS_FS_XATTR
	if (test_opt(sbi, XATTR_USER))
		seq_puts(seq, ",user_xattr");
	else
		seq_puts(seq, ",nouser_xattr");
	if (test_opt(sbi, INLINE_XATTR))
		seq_puts(seq, ",inline_xattr");
	else
		seq_puts(seq, ",noinline_xattr");
	if (test_opt(sbi, INLINE_XATTR_SIZE))
		seq_printf(seq, ",inline_xattr_size=%u",
					F2FS_OPTION(sbi).inline_xattr_size);
#endif
#ifdef CONFIG_F2FS_FS_POSIX_ACL
	if (test_opt(sbi, POSIX_ACL))
		seq_puts(seq, ",acl");
	else
		seq_puts(seq, ",noacl");
#endif
	if (test_opt(sbi, DISABLE_EXT_IDENTIFY))
		seq_puts(seq, ",disable_ext_identify");
	if (test_opt(sbi, INLINE_DATA))
		seq_puts(seq, ",inline_data");
	else
		seq_puts(seq, ",noinline_data");
	if (test_opt(sbi, INLINE_DENTRY))
		seq_puts(seq, ",inline_dentry");
	else
		seq_puts(seq, ",noinline_dentry");
	if (test_opt(sbi, FLUSH_MERGE))
		seq_puts(seq, ",flush_merge");
	else
		seq_puts(seq, ",noflush_merge");
	if (test_opt(sbi, NOBARRIER))
		seq_puts(seq, ",nobarrier");
	else
		seq_puts(seq, ",barrier");
	if (test_opt(sbi, FASTBOOT))
		seq_puts(seq, ",fastboot");
	if (test_opt(sbi, READ_EXTENT_CACHE))
		seq_puts(seq, ",extent_cache");
	else
		seq_puts(seq, ",noextent_cache");
	if (test_opt(sbi, AGE_EXTENT_CACHE))
		seq_puts(seq, ",age_extent_cache");
	if (test_opt(sbi, DATA_FLUSH))
		seq_puts(seq, ",data_flush");

	seq_puts(seq, ",mode=");
	if (F2FS_OPTION(sbi).fs_mode == FS_MODE_ADAPTIVE)
		seq_puts(seq, "adaptive");
	else if (F2FS_OPTION(sbi).fs_mode == FS_MODE_LFS)
		seq_puts(seq, "lfs");
	else if (F2FS_OPTION(sbi).fs_mode == FS_MODE_FRAGMENT_SEG)
		seq_puts(seq, "fragment:segment");
	else if (F2FS_OPTION(sbi).fs_mode == FS_MODE_FRAGMENT_BLK)
		seq_puts(seq, "fragment:block");
	seq_printf(seq, ",active_logs=%u", F2FS_OPTION(sbi).active_logs);
	if (test_opt(sbi, RESERVE_ROOT) || test_opt(sbi, RESERVE_NODE))
		seq_printf(seq, ",reserve_root=%u,reserve_node=%u,resuid=%u,"
				"resgid=%u",
				F2FS_OPTION(sbi).root_reserved_blocks,
				F2FS_OPTION(sbi).root_reserved_nodes,
				from_kuid_munged(&init_user_ns,
					F2FS_OPTION(sbi).s_resuid),
				from_kgid_munged(&init_user_ns,
					F2FS_OPTION(sbi).s_resgid));
#ifdef CONFIG_F2FS_FAULT_INJECTION
	if (test_opt(sbi, FAULT_INJECTION)) {
		seq_printf(seq, ",fault_injection=%u",
				F2FS_OPTION(sbi).fault_info.inject_rate);
		seq_printf(seq, ",fault_type=%u",
				F2FS_OPTION(sbi).fault_info.inject_type);
	}
#endif
#ifdef CONFIG_QUOTA
	if (test_opt(sbi, QUOTA))
		seq_puts(seq, ",quota");
	if (test_opt(sbi, USRQUOTA))
		seq_puts(seq, ",usrquota");
	if (test_opt(sbi, GRPQUOTA))
		seq_puts(seq, ",grpquota");
	if (test_opt(sbi, PRJQUOTA))
		seq_puts(seq, ",prjquota");
#endif
	f2fs_show_quota_options(seq, sbi->sb);

	fscrypt_show_test_dummy_encryption(seq, ',', sbi->sb);

	if (sbi->sb->s_flags & SB_INLINECRYPT)
		seq_puts(seq, ",inlinecrypt");

	if (F2FS_OPTION(sbi).alloc_mode == ALLOC_MODE_DEFAULT)
		seq_printf(seq, ",alloc_mode=%s", "default");
	else if (F2FS_OPTION(sbi).alloc_mode == ALLOC_MODE_REUSE)
		seq_printf(seq, ",alloc_mode=%s", "reuse");

	if (test_opt(sbi, DISABLE_CHECKPOINT))
		seq_printf(seq, ",checkpoint=disable:%u",
				F2FS_OPTION(sbi).unusable_cap);
	if (test_opt(sbi, MERGE_CHECKPOINT))
		seq_puts(seq, ",checkpoint_merge");
	else
		seq_puts(seq, ",nocheckpoint_merge");
	if (F2FS_OPTION(sbi).fsync_mode == FSYNC_MODE_POSIX)
		seq_printf(seq, ",fsync_mode=%s", "posix");
	else if (F2FS_OPTION(sbi).fsync_mode == FSYNC_MODE_STRICT)
		seq_printf(seq, ",fsync_mode=%s", "strict");
	else if (F2FS_OPTION(sbi).fsync_mode == FSYNC_MODE_NOBARRIER)
		seq_printf(seq, ",fsync_mode=%s", "nobarrier");

#ifdef CONFIG_F2FS_FS_COMPRESSION
	f2fs_show_compress_options(seq, sbi->sb);
#endif

	if (test_opt(sbi, ATGC))
		seq_puts(seq, ",atgc");

	if (F2FS_OPTION(sbi).memory_mode == MEMORY_MODE_NORMAL)
		seq_printf(seq, ",memory=%s", "normal");
	else if (F2FS_OPTION(sbi).memory_mode == MEMORY_MODE_LOW)
		seq_printf(seq, ",memory=%s", "low");

	if (F2FS_OPTION(sbi).errors == MOUNT_ERRORS_READONLY)
		seq_printf(seq, ",errors=%s", "remount-ro");
	else if (F2FS_OPTION(sbi).errors == MOUNT_ERRORS_CONTINUE)
		seq_printf(seq, ",errors=%s", "continue");
	else if (F2FS_OPTION(sbi).errors == MOUNT_ERRORS_PANIC)
		seq_printf(seq, ",errors=%s", "panic");

	if (test_opt(sbi, NAT_BITS))
		seq_puts(seq, ",nat_bits");

	if (F2FS_OPTION(sbi).lookup_mode == LOOKUP_PERF)
		seq_show_option(seq, "lookup_mode", "perf");
	else if (F2FS_OPTION(sbi).lookup_mode == LOOKUP_COMPAT)
		seq_show_option(seq, "lookup_mode", "compat");
	else if (F2FS_OPTION(sbi).lookup_mode == LOOKUP_AUTO)
		seq_show_option(seq, "lookup_mode", "auto");

	return 0;
}

static void default_options(struct f2fs_sb_info *sbi, bool remount)
{
	/* init some FS parameters */
	if (!remount) {
		set_opt(sbi, READ_EXTENT_CACHE);
		clear_opt(sbi, DISABLE_CHECKPOINT);

		if (f2fs_hw_support_discard(sbi) || f2fs_hw_should_discard(sbi))
			set_opt(sbi, DISCARD);

		if (f2fs_sb_has_blkzoned(sbi))
			F2FS_OPTION(sbi).discard_unit = DISCARD_UNIT_SECTION;
		else
			F2FS_OPTION(sbi).discard_unit = DISCARD_UNIT_BLOCK;
	}

	if (f2fs_sb_has_readonly(sbi))
		F2FS_OPTION(sbi).active_logs = NR_CURSEG_RO_TYPE;
	else
		F2FS_OPTION(sbi).active_logs = NR_CURSEG_PERSIST_TYPE;

	F2FS_OPTION(sbi).inline_xattr_size = DEFAULT_INLINE_XATTR_ADDRS;
	if (le32_to_cpu(F2FS_RAW_SUPER(sbi)->segment_count_main) <=
							SMALL_VOLUME_SEGMENTS)
		F2FS_OPTION(sbi).alloc_mode = ALLOC_MODE_REUSE;
	else
		F2FS_OPTION(sbi).alloc_mode = ALLOC_MODE_DEFAULT;
	F2FS_OPTION(sbi).fsync_mode = FSYNC_MODE_POSIX;
	F2FS_OPTION(sbi).s_resuid = make_kuid(&init_user_ns, F2FS_DEF_RESUID);
	F2FS_OPTION(sbi).s_resgid = make_kgid(&init_user_ns, F2FS_DEF_RESGID);
	if (f2fs_sb_has_compression(sbi)) {
		F2FS_OPTION(sbi).compress_algorithm = COMPRESS_LZ4;
		F2FS_OPTION(sbi).compress_log_size = MIN_COMPRESS_LOG_SIZE;
		F2FS_OPTION(sbi).compress_ext_cnt = 0;
		F2FS_OPTION(sbi).compress_mode = COMPR_MODE_FS;
	}
	F2FS_OPTION(sbi).bggc_mode = BGGC_MODE_ON;
	F2FS_OPTION(sbi).memory_mode = MEMORY_MODE_NORMAL;
	F2FS_OPTION(sbi).errors = MOUNT_ERRORS_CONTINUE;

	set_opt(sbi, INLINE_XATTR);
	set_opt(sbi, INLINE_DATA);
	set_opt(sbi, INLINE_DENTRY);
	set_opt(sbi, MERGE_CHECKPOINT);
	set_opt(sbi, LAZYTIME);
	F2FS_OPTION(sbi).unusable_cap = 0;
	if (!f2fs_is_readonly(sbi))
		set_opt(sbi, FLUSH_MERGE);
	if (f2fs_sb_has_blkzoned(sbi))
		F2FS_OPTION(sbi).fs_mode = FS_MODE_LFS;
	else
		F2FS_OPTION(sbi).fs_mode = FS_MODE_ADAPTIVE;

#ifdef CONFIG_F2FS_FS_XATTR
	set_opt(sbi, XATTR_USER);
#endif
#ifdef CONFIG_F2FS_FS_POSIX_ACL
	set_opt(sbi, POSIX_ACL);
#endif

	f2fs_build_fault_attr(sbi, 0, 0, FAULT_ALL);

	F2FS_OPTION(sbi).lookup_mode = LOOKUP_PERF;
}

#ifdef CONFIG_QUOTA
static int f2fs_enable_quotas(struct super_block *sb);
#endif

static int f2fs_disable_checkpoint(struct f2fs_sb_info *sbi)
{
	unsigned int s_flags = sbi->sb->s_flags;
	struct cp_control cpc;
	unsigned int gc_mode = sbi->gc_mode;
	int err = 0;
	int ret;
	block_t unusable;

	if (s_flags & SB_RDONLY) {
		f2fs_err(sbi, "checkpoint=disable on readonly fs");
		return -EINVAL;
	}
	sbi->sb->s_flags |= SB_ACTIVE;

	/* check if we need more GC first */
	unusable = f2fs_get_unusable_blocks(sbi);
	if (!f2fs_disable_cp_again(sbi, unusable))
		goto skip_gc;

	f2fs_update_time(sbi, DISABLE_TIME);

	sbi->gc_mode = GC_URGENT_HIGH;

	while (!f2fs_time_over(sbi, DISABLE_TIME)) {
		struct f2fs_gc_control gc_control = {
			.victim_segno = NULL_SEGNO,
			.init_gc_type = FG_GC,
			.should_migrate_blocks = false,
			.err_gc_skipped = true,
			.no_bg_gc = true,
			.nr_free_secs = 1 };

		f2fs_down_write(&sbi->gc_lock);
		stat_inc_gc_call_count(sbi, FOREGROUND);
		err = f2fs_gc(sbi, &gc_control);
		if (err == -ENODATA) {
			err = 0;
			break;
		}
		if (err && err != -EAGAIN)
			break;
	}

	ret = sync_filesystem(sbi->sb);
	if (ret || err) {
		err = ret ? ret : err;
		goto restore_flag;
	}

	unusable = f2fs_get_unusable_blocks(sbi);
	if (f2fs_disable_cp_again(sbi, unusable)) {
		err = -EAGAIN;
		goto restore_flag;
	}

skip_gc:
	f2fs_down_write(&sbi->gc_lock);
	cpc.reason = CP_PAUSE;
	set_sbi_flag(sbi, SBI_CP_DISABLED);
	stat_inc_cp_call_count(sbi, TOTAL_CALL);
	err = f2fs_write_checkpoint(sbi, &cpc);
	if (err)
		goto out_unlock;

	spin_lock(&sbi->stat_lock);
	sbi->unusable_block_count = unusable;
	spin_unlock(&sbi->stat_lock);

out_unlock:
	f2fs_up_write(&sbi->gc_lock);
restore_flag:
	sbi->gc_mode = gc_mode;
	sbi->sb->s_flags = s_flags;	/* Restore SB_RDONLY status */
	f2fs_info(sbi, "f2fs_disable_checkpoint() finish, err:%d", err);
	return err;
}

static void f2fs_enable_checkpoint(struct f2fs_sb_info *sbi)
{
	unsigned int nr_pages = get_pages(sbi, F2FS_DIRTY_DATA) / 16;
	long long start, writeback, end;

	f2fs_info(sbi, "f2fs_enable_checkpoint() starts, meta: %lld, node: %lld, data: %lld",
					get_pages(sbi, F2FS_DIRTY_META),
					get_pages(sbi, F2FS_DIRTY_NODES),
					get_pages(sbi, F2FS_DIRTY_DATA));

	f2fs_update_time(sbi, ENABLE_TIME);

	start = ktime_get();

	/* we should flush all the data to keep data consistency */
	while (get_pages(sbi, F2FS_DIRTY_DATA)) {
		writeback_inodes_sb_nr(sbi->sb, nr_pages, WB_REASON_SYNC);
		f2fs_io_schedule_timeout(DEFAULT_IO_TIMEOUT);

		if (f2fs_time_over(sbi, ENABLE_TIME))
			break;
	}
	writeback = ktime_get();

	sync_inodes_sb(sbi->sb);

	if (unlikely(get_pages(sbi, F2FS_DIRTY_DATA)))
		f2fs_warn(sbi, "checkpoint=enable has some unwritten data: %lld",
					get_pages(sbi, F2FS_DIRTY_DATA));

	f2fs_down_write(&sbi->gc_lock);
	f2fs_dirty_to_prefree(sbi);

	clear_sbi_flag(sbi, SBI_CP_DISABLED);
	set_sbi_flag(sbi, SBI_IS_DIRTY);
	f2fs_up_write(&sbi->gc_lock);

	f2fs_sync_fs(sbi->sb, 1);

	/* Let's ensure there's no pending checkpoint anymore */
	f2fs_flush_ckpt_thread(sbi);

	end = ktime_get();

	f2fs_info(sbi, "f2fs_enable_checkpoint() finishes, writeback:%llu, sync:%llu",
					ktime_ms_delta(writeback, start),
					ktime_ms_delta(end, writeback));
}

static int __f2fs_remount(struct fs_context *fc, struct super_block *sb)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_mount_info org_mount_opt;
	unsigned long old_sb_flags;
	unsigned int flags = fc->sb_flags;
	int err;
	bool need_restart_gc = false, need_stop_gc = false;
	bool need_restart_flush = false, need_stop_flush = false;
	bool need_restart_discard = false, need_stop_discard = false;
	bool need_enable_checkpoint = false, need_disable_checkpoint = false;
	bool no_read_extent_cache = !test_opt(sbi, READ_EXTENT_CACHE);
	bool no_age_extent_cache = !test_opt(sbi, AGE_EXTENT_CACHE);
	bool enable_checkpoint = !test_opt(sbi, DISABLE_CHECKPOINT);
	bool no_atgc = !test_opt(sbi, ATGC);
	bool no_discard = !test_opt(sbi, DISCARD);
	bool no_compress_cache = !test_opt(sbi, COMPRESS_CACHE);
	bool block_unit_discard = f2fs_block_unit_discard(sbi);
	bool no_nat_bits = !test_opt(sbi, NAT_BITS);
#ifdef CONFIG_QUOTA
	int i, j;
#endif

	/*
	 * Save the old mount options in case we
	 * need to restore them.
	 */
	org_mount_opt = sbi->mount_opt;
	old_sb_flags = sb->s_flags;

	sbi->umount_lock_holder = current;

#ifdef CONFIG_QUOTA
	org_mount_opt.s_jquota_fmt = F2FS_OPTION(sbi).s_jquota_fmt;
	for (i = 0; i < MAXQUOTAS; i++) {
		if (F2FS_OPTION(sbi).s_qf_names[i]) {
			org_mount_opt.s_qf_names[i] =
				kstrdup(F2FS_OPTION(sbi).s_qf_names[i],
				GFP_KERNEL);
			if (!org_mount_opt.s_qf_names[i]) {
				for (j = 0; j < i; j++)
					kfree(org_mount_opt.s_qf_names[j]);
				return -ENOMEM;
			}
		} else {
			org_mount_opt.s_qf_names[i] = NULL;
		}
	}
#endif

	/* recover superblocks we couldn't write due to previous RO mount */
	if (!(flags & SB_RDONLY) && is_sbi_flag_set(sbi, SBI_NEED_SB_WRITE)) {
		err = f2fs_commit_super(sbi, false);
		f2fs_info(sbi, "Try to recover all the superblocks, ret: %d",
			  err);
		if (!err)
			clear_sbi_flag(sbi, SBI_NEED_SB_WRITE);
	}

	default_options(sbi, true);

	err = f2fs_check_opt_consistency(fc, sb);
	if (err)
		goto restore_opts;

	f2fs_apply_options(fc, sb);

	err = f2fs_sanity_check_options(sbi, true);
	if (err)
		goto restore_opts;

	/* flush outstanding errors before changing fs state */
	flush_work(&sbi->s_error_work);

	/*
	 * Previous and new state of filesystem is RO,
	 * so skip checking GC and FLUSH_MERGE conditions.
	 */
	if (f2fs_readonly(sb) && (flags & SB_RDONLY))
		goto skip;

	if (f2fs_dev_is_readonly(sbi) && !(flags & SB_RDONLY)) {
		err = -EROFS;
		goto restore_opts;
	}

#ifdef CONFIG_QUOTA
	if (!f2fs_readonly(sb) && (flags & SB_RDONLY)) {
		err = dquot_suspend(sb, -1);
		if (err < 0)
			goto restore_opts;
	} else if (f2fs_readonly(sb) && !(flags & SB_RDONLY)) {
		/* dquot_resume needs RW */
		sb->s_flags &= ~SB_RDONLY;
		if (sb_any_quota_suspended(sb)) {
			dquot_resume(sb, -1);
		} else if (f2fs_sb_has_quota_ino(sbi)) {
			err = f2fs_enable_quotas(sb);
			if (err)
				goto restore_opts;
		}
	}
#endif
	/* disallow enable atgc dynamically */
	if (no_atgc == !!test_opt(sbi, ATGC)) {
		err = -EINVAL;
		f2fs_warn(sbi, "switch atgc option is not allowed");
		goto restore_opts;
	}

	/* disallow enable/disable extent_cache dynamically */
	if (no_read_extent_cache == !!test_opt(sbi, READ_EXTENT_CACHE)) {
		err = -EINVAL;
		f2fs_warn(sbi, "switch extent_cache option is not allowed");
		goto restore_opts;
	}
	/* disallow enable/disable age extent_cache dynamically */
	if (no_age_extent_cache == !!test_opt(sbi, AGE_EXTENT_CACHE)) {
		err = -EINVAL;
		f2fs_warn(sbi, "switch age_extent_cache option is not allowed");
		goto restore_opts;
	}

	if (no_compress_cache == !!test_opt(sbi, COMPRESS_CACHE)) {
		err = -EINVAL;
		f2fs_warn(sbi, "switch compress_cache option is not allowed");
		goto restore_opts;
	}

	if (block_unit_discard != f2fs_block_unit_discard(sbi)) {
		err = -EINVAL;
		f2fs_warn(sbi, "switch discard_unit option is not allowed");
		goto restore_opts;
	}

	if (no_nat_bits == !!test_opt(sbi, NAT_BITS)) {
		err = -EINVAL;
		f2fs_warn(sbi, "switch nat_bits option is not allowed");
		goto restore_opts;
	}

	if ((flags & SB_RDONLY) && test_opt(sbi, DISABLE_CHECKPOINT)) {
		err = -EINVAL;
		f2fs_warn(sbi, "disabling checkpoint not compatible with read-only");
		goto restore_opts;
	}

	/*
	 * We stop the GC thread if FS is mounted as RO
	 * or if background_gc = off is passed in mount
	 * option. Also sync the filesystem.
	 */
	if ((flags & SB_RDONLY) ||
			(F2FS_OPTION(sbi).bggc_mode == BGGC_MODE_OFF &&
			!test_opt(sbi, GC_MERGE))) {
		if (sbi->gc_thread) {
			f2fs_stop_gc_thread(sbi);
			need_restart_gc = true;
		}
	} else if (!sbi->gc_thread) {
		err = f2fs_start_gc_thread(sbi);
		if (err)
			goto restore_opts;
		need_stop_gc = true;
	}

	if (flags & SB_RDONLY) {
		sync_inodes_sb(sb);

		set_sbi_flag(sbi, SBI_IS_DIRTY);
		set_sbi_flag(sbi, SBI_IS_CLOSE);
		f2fs_sync_fs(sb, 1);
		clear_sbi_flag(sbi, SBI_IS_CLOSE);
	}

	/*
	 * We stop issue flush thread if FS is mounted as RO
	 * or if flush_merge is not passed in mount option.
	 */
	if ((flags & SB_RDONLY) || !test_opt(sbi, FLUSH_MERGE)) {
		clear_opt(sbi, FLUSH_MERGE);
		f2fs_destroy_flush_cmd_control(sbi, false);
		need_restart_flush = true;
	} else {
		err = f2fs_create_flush_cmd_control(sbi);
		if (err)
			goto restore_gc;
		need_stop_flush = true;
	}

	if (no_discard == !!test_opt(sbi, DISCARD)) {
		if (test_opt(sbi, DISCARD)) {
			err = f2fs_start_discard_thread(sbi);
			if (err)
				goto restore_flush;
			need_stop_discard = true;
		} else {
			f2fs_stop_discard_thread(sbi);
			f2fs_issue_discard_timeout(sbi);
			need_restart_discard = true;
		}
	}

	adjust_unusable_cap_perc(sbi);
	if (enable_checkpoint == !!test_opt(sbi, DISABLE_CHECKPOINT)) {
		if (test_opt(sbi, DISABLE_CHECKPOINT)) {
			err = f2fs_disable_checkpoint(sbi);
			if (err)
				goto restore_discard;
			need_enable_checkpoint = true;
		} else {
			f2fs_enable_checkpoint(sbi);
			need_disable_checkpoint = true;
		}
	}

	/*
	 * Place this routine at the end, since a new checkpoint would be
	 * triggered while remount and we need to take care of it before
	 * returning from remount.
	 */
	if ((flags & SB_RDONLY) || test_opt(sbi, DISABLE_CHECKPOINT) ||
			!test_opt(sbi, MERGE_CHECKPOINT)) {
		f2fs_stop_ckpt_thread(sbi);
	} else {
		/* Flush if the previous checkpoint, if exists. */
		f2fs_flush_ckpt_thread(sbi);

		err = f2fs_start_ckpt_thread(sbi);
		if (err) {
			f2fs_err(sbi,
			    "Failed to start F2FS issue_checkpoint_thread (%d)",
			    err);
			goto restore_checkpoint;
		}
	}

skip:
#ifdef CONFIG_QUOTA
	/* Release old quota file names */
	for (i = 0; i < MAXQUOTAS; i++)
		kfree(org_mount_opt.s_qf_names[i]);
#endif
	/* Update the POSIXACL Flag */
	sb->s_flags = (sb->s_flags & ~SB_POSIXACL) |
		(test_opt(sbi, POSIX_ACL) ? SB_POSIXACL : 0);

	limit_reserve_root(sbi);
	fc->sb_flags = (flags & ~SB_LAZYTIME) | (sb->s_flags & SB_LAZYTIME);

	sbi->umount_lock_holder = NULL;
	return 0;
restore_checkpoint:
	if (need_enable_checkpoint) {
		f2fs_enable_checkpoint(sbi);
	} else if (need_disable_checkpoint) {
		if (f2fs_disable_checkpoint(sbi))
			f2fs_warn(sbi, "checkpoint has not been disabled");
	}
restore_discard:
	if (need_restart_discard) {
		if (f2fs_start_discard_thread(sbi))
			f2fs_warn(sbi, "discard has been stopped");
	} else if (need_stop_discard) {
		f2fs_stop_discard_thread(sbi);
	}
restore_flush:
	if (need_restart_flush) {
		if (f2fs_create_flush_cmd_control(sbi))
			f2fs_warn(sbi, "background flush thread has stopped");
	} else if (need_stop_flush) {
		clear_opt(sbi, FLUSH_MERGE);
		f2fs_destroy_flush_cmd_control(sbi, false);
	}
restore_gc:
	if (need_restart_gc) {
		if (f2fs_start_gc_thread(sbi))
			f2fs_warn(sbi, "background gc thread has stopped");
	} else if (need_stop_gc) {
		f2fs_stop_gc_thread(sbi);
	}
restore_opts:
#ifdef CONFIG_QUOTA
	F2FS_OPTION(sbi).s_jquota_fmt = org_mount_opt.s_jquota_fmt;
	for (i = 0; i < MAXQUOTAS; i++) {
		kfree(F2FS_OPTION(sbi).s_qf_names[i]);
		F2FS_OPTION(sbi).s_qf_names[i] = org_mount_opt.s_qf_names[i];
	}
#endif
	sbi->mount_opt = org_mount_opt;
	sb->s_flags = old_sb_flags;

	sbi->umount_lock_holder = NULL;
	return err;
}

static void f2fs_shutdown(struct super_block *sb)
{
	f2fs_do_shutdown(F2FS_SB(sb), F2FS_GOING_DOWN_NOSYNC, false, false);
}

#ifdef CONFIG_QUOTA
static bool f2fs_need_recovery(struct f2fs_sb_info *sbi)
{
	/* need to recovery orphan */
	if (is_set_ckpt_flags(sbi, CP_ORPHAN_PRESENT_FLAG))
		return true;
	/* need to recovery data */
	if (test_opt(sbi, DISABLE_ROLL_FORWARD))
		return false;
	if (test_opt(sbi, NORECOVERY))
		return false;
	return !is_set_ckpt_flags(sbi, CP_UMOUNT_FLAG);
}

static bool f2fs_recover_quota_begin(struct f2fs_sb_info *sbi)
{
	bool readonly = f2fs_readonly(sbi->sb);

	if (!f2fs_need_recovery(sbi))
		return false;

	/* it doesn't need to check f2fs_sb_has_readonly() */
	if (f2fs_hw_is_readonly(sbi))
		return false;

	if (readonly) {
		sbi->sb->s_flags &= ~SB_RDONLY;
		set_sbi_flag(sbi, SBI_IS_WRITABLE);
	}

	/*
	 * Turn on quotas which were not enabled for read-only mounts if
	 * filesystem has quota feature, so that they are updated correctly.
	 */
	return f2fs_enable_quota_files(sbi, readonly);
}

static void f2fs_recover_quota_end(struct f2fs_sb_info *sbi,
						bool quota_enabled)
{
	if (quota_enabled)
		f2fs_quota_off_umount(sbi->sb);

	if (is_sbi_flag_set(sbi, SBI_IS_WRITABLE)) {
		clear_sbi_flag(sbi, SBI_IS_WRITABLE);
		sbi->sb->s_flags |= SB_RDONLY;
	}
}

/* Read data from quotafile */
static ssize_t f2fs_quota_read(struct super_block *sb, int type, char *data,
			       size_t len, loff_t off)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	struct address_space *mapping = inode->i_mapping;
	int tocopy;
	size_t toread;
	loff_t i_size = i_size_read(inode);

	if (off > i_size)
		return 0;

	if (off + len > i_size)
		len = i_size - off;
	toread = len;
	while (toread > 0) {
		struct folio *folio;
		size_t offset;

repeat:
		folio = mapping_read_folio_gfp(mapping, off >> PAGE_SHIFT,
				GFP_NOFS);
		if (IS_ERR(folio)) {
			if (PTR_ERR(folio) == -ENOMEM) {
				memalloc_retry_wait(GFP_NOFS);
				goto repeat;
			}
			set_sbi_flag(F2FS_SB(sb), SBI_QUOTA_NEED_REPAIR);
			return PTR_ERR(folio);
		}
		offset = offset_in_folio(folio, off);
		tocopy = min(folio_size(folio) - offset, toread);

		folio_lock(folio);

		if (unlikely(folio->mapping != mapping)) {
			f2fs_folio_put(folio, true);
			goto repeat;
		}

		/*
		 * should never happen, just leave f2fs_bug_on() here to catch
		 * any potential bug.
		 */
		f2fs_bug_on(F2FS_SB(sb), !folio_test_uptodate(folio));

		memcpy_from_folio(data, folio, offset, tocopy);
		f2fs_folio_put(folio, true);

		toread -= tocopy;
		data += tocopy;
		off += tocopy;
	}
	return len;
}

/* Write to quotafile */
static ssize_t f2fs_quota_write(struct super_block *sb, int type,
				const char *data, size_t len, loff_t off)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	struct address_space *mapping = inode->i_mapping;
	const struct address_space_operations *a_ops = mapping->a_ops;
	int offset = off & (sb->s_blocksize - 1);
	size_t towrite = len;
	struct folio *folio;
	void *fsdata = NULL;
	int err = 0;
	int tocopy;

	while (towrite > 0) {
		tocopy = min_t(unsigned long, sb->s_blocksize - offset,
								towrite);
retry:
		err = a_ops->write_begin(NULL, mapping, off, tocopy,
							&folio, &fsdata);
		if (unlikely(err)) {
			if (err == -ENOMEM) {
				f2fs_io_schedule_timeout(DEFAULT_IO_TIMEOUT);
				goto retry;
			}
			set_sbi_flag(F2FS_SB(sb), SBI_QUOTA_NEED_REPAIR);
			break;
		}

		memcpy_to_folio(folio, offset_in_folio(folio, off), data, tocopy);

		a_ops->write_end(NULL, mapping, off, tocopy, tocopy,
						folio, fsdata);
		offset = 0;
		towrite -= tocopy;
		off += tocopy;
		data += tocopy;
		cond_resched();
	}

	if (len == towrite)
		return err;
	inode_set_mtime_to_ts(inode, inode_set_ctime_current(inode));
	f2fs_mark_inode_dirty_sync(inode, false);
	return len - towrite;
}

int f2fs_dquot_initialize(struct inode *inode)
{
	if (time_to_inject(F2FS_I_SB(inode), FAULT_DQUOT_INIT))
		return -ESRCH;

	return dquot_initialize(inode);
}

static struct dquot __rcu **f2fs_get_dquots(struct inode *inode)
{
	return F2FS_I(inode)->i_dquot;
}

static qsize_t *f2fs_get_reserved_space(struct inode *inode)
{
	return &F2FS_I(inode)->i_reserved_quota;
}

static int f2fs_quota_on_mount(struct f2fs_sb_info *sbi, int type)
{
	if (is_set_ckpt_flags(sbi, CP_QUOTA_NEED_FSCK_FLAG)) {
		f2fs_err(sbi, "quota sysfile may be corrupted, skip loading it");
		return 0;
	}

	return dquot_quota_on_mount(sbi->sb, F2FS_OPTION(sbi).s_qf_names[type],
					F2FS_OPTION(sbi).s_jquota_fmt, type);
}

int f2fs_enable_quota_files(struct f2fs_sb_info *sbi, bool rdonly)
{
	int enabled = 0;
	int i, err;

	if (f2fs_sb_has_quota_ino(sbi) && rdonly) {
		err = f2fs_enable_quotas(sbi->sb);
		if (err) {
			f2fs_err(sbi, "Cannot turn on quota_ino: %d", err);
			return 0;
		}
		return 1;
	}

	for (i = 0; i < MAXQUOTAS; i++) {
		if (F2FS_OPTION(sbi).s_qf_names[i]) {
			err = f2fs_quota_on_mount(sbi, i);
			if (!err) {
				enabled = 1;
				continue;
			}
			f2fs_err(sbi, "Cannot turn on quotas: %d on %d",
				 err, i);
		}
	}
	return enabled;
}

static int f2fs_quota_enable(struct super_block *sb, int type, int format_id,
			     unsigned int flags)
{
	struct inode *qf_inode;
	unsigned long qf_inum;
	unsigned long qf_flag = F2FS_QUOTA_DEFAULT_FL;
	int err;

	BUG_ON(!f2fs_sb_has_quota_ino(F2FS_SB(sb)));

	qf_inum = f2fs_qf_ino(sb, type);
	if (!qf_inum)
		return -EPERM;

	qf_inode = f2fs_iget(sb, qf_inum);
	if (IS_ERR(qf_inode)) {
		f2fs_err(F2FS_SB(sb), "Bad quota inode %u:%lu", type, qf_inum);
		return PTR_ERR(qf_inode);
	}

	/* Don't account quota for quota files to avoid recursion */
	inode_lock(qf_inode);
	qf_inode->i_flags |= S_NOQUOTA;

	if ((F2FS_I(qf_inode)->i_flags & qf_flag) != qf_flag) {
		F2FS_I(qf_inode)->i_flags |= qf_flag;
		f2fs_set_inode_flags(qf_inode);
	}
	inode_unlock(qf_inode);

	err = dquot_load_quota_inode(qf_inode, type, format_id, flags);
	iput(qf_inode);
	return err;
}

static int f2fs_enable_quotas(struct super_block *sb)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	int type, err = 0;
	unsigned long qf_inum;
	bool quota_mopt[MAXQUOTAS] = {
		test_opt(sbi, USRQUOTA),
		test_opt(sbi, GRPQUOTA),
		test_opt(sbi, PRJQUOTA),
	};

	if (is_set_ckpt_flags(F2FS_SB(sb), CP_QUOTA_NEED_FSCK_FLAG)) {
		f2fs_err(sbi, "quota file may be corrupted, skip loading it");
		return 0;
	}

	sb_dqopt(sb)->flags |= DQUOT_QUOTA_SYS_FILE;

	for (type = 0; type < MAXQUOTAS; type++) {
		qf_inum = f2fs_qf_ino(sb, type);
		if (qf_inum) {
			err = f2fs_quota_enable(sb, type, QFMT_VFS_V1,
				DQUOT_USAGE_ENABLED |
				(quota_mopt[type] ? DQUOT_LIMITS_ENABLED : 0));
			if (err) {
				f2fs_err(sbi, "Failed to enable quota tracking (type=%d, err=%d). Please run fsck to fix.",
					 type, err);
				for (type--; type >= 0; type--)
					dquot_quota_off(sb, type);
				set_sbi_flag(F2FS_SB(sb),
						SBI_QUOTA_NEED_REPAIR);
				return err;
			}
		}
	}
	return 0;
}

static int f2fs_quota_sync_file(struct f2fs_sb_info *sbi, int type)
{
	struct quota_info *dqopt = sb_dqopt(sbi->sb);
	struct address_space *mapping = dqopt->files[type]->i_mapping;
	int ret = 0;

	ret = dquot_writeback_dquots(sbi->sb, type);
	if (ret)
		goto out;

	ret = filemap_fdatawrite(mapping);
	if (ret)
		goto out;

	/* if we are using journalled quota */
	if (is_journalled_quota(sbi))
		goto out;

	ret = filemap_fdatawait(mapping);

	truncate_inode_pages(&dqopt->files[type]->i_data, 0);
out:
	if (ret)
		set_sbi_flag(sbi, SBI_QUOTA_NEED_REPAIR);
	return ret;
}

int f2fs_do_quota_sync(struct super_block *sb, int type)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct quota_info *dqopt = sb_dqopt(sb);
	int cnt;
	int ret = 0;

	/*
	 * Now when everything is written we can discard the pagecache so
	 * that userspace sees the changes.
	 */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {

		if (type != -1 && cnt != type)
			continue;

		if (!sb_has_quota_active(sb, cnt))
			continue;

		if (!f2fs_sb_has_quota_ino(sbi))
			inode_lock(dqopt->files[cnt]);

		/*
		 * do_quotactl
		 *  f2fs_quota_sync
		 *  f2fs_down_read(quota_sem)
		 *  dquot_writeback_dquots()
		 *  f2fs_dquot_commit
		 *			      block_operation
		 *			      f2fs_down_read(quota_sem)
		 */
		f2fs_lock_op(sbi);
		f2fs_down_read(&sbi->quota_sem);

		ret = f2fs_quota_sync_file(sbi, cnt);

		f2fs_up_read(&sbi->quota_sem);
		f2fs_unlock_op(sbi);

		if (!f2fs_sb_has_quota_ino(sbi))
			inode_unlock(dqopt->files[cnt]);

		if (ret)
			break;
	}
	return ret;
}

static int f2fs_quota_sync(struct super_block *sb, int type)
{
	int ret;

	F2FS_SB(sb)->umount_lock_holder = current;
	ret = f2fs_do_quota_sync(sb, type);
	F2FS_SB(sb)->umount_lock_holder = NULL;
	return ret;
}

static int f2fs_quota_on(struct super_block *sb, int type, int format_id,
							const struct path *path)
{
	struct inode *inode;
	int err = 0;

	/* if quota sysfile exists, deny enabling quota with specific file */
	if (f2fs_sb_has_quota_ino(F2FS_SB(sb))) {
		f2fs_err(F2FS_SB(sb), "quota sysfile already exists");
		return -EBUSY;
	}

	if (path->dentry->d_sb != sb)
		return -EXDEV;

	F2FS_SB(sb)->umount_lock_holder = current;

	err = f2fs_do_quota_sync(sb, type);
	if (err)
		goto out;

	inode = d_inode(path->dentry);

	err = filemap_fdatawrite(inode->i_mapping);
	if (err)
		goto out;

	err = filemap_fdatawait(inode->i_mapping);
	if (err)
		goto out;

	err = dquot_quota_on(sb, type, format_id, path);
	if (err)
		goto out;

	inode_lock(inode);
	F2FS_I(inode)->i_flags |= F2FS_QUOTA_DEFAULT_FL;
	f2fs_set_inode_flags(inode);
	inode_unlock(inode);
	f2fs_mark_inode_dirty_sync(inode, false);
out:
	F2FS_SB(sb)->umount_lock_holder = NULL;
	return err;
}

static int __f2fs_quota_off(struct super_block *sb, int type)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	int err;

	if (!inode || !igrab(inode))
		return dquot_quota_off(sb, type);

	err = f2fs_do_quota_sync(sb, type);
	if (err)
		goto out_put;

	err = dquot_quota_off(sb, type);
	if (err || f2fs_sb_has_quota_ino(F2FS_SB(sb)))
		goto out_put;

	inode_lock(inode);
	F2FS_I(inode)->i_flags &= ~F2FS_QUOTA_DEFAULT_FL;
	f2fs_set_inode_flags(inode);
	inode_unlock(inode);
	f2fs_mark_inode_dirty_sync(inode, false);
out_put:
	iput(inode);
	return err;
}

static int f2fs_quota_off(struct super_block *sb, int type)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	int err;

	F2FS_SB(sb)->umount_lock_holder = current;

	err = __f2fs_quota_off(sb, type);

	/*
	 * quotactl can shutdown journalled quota, result in inconsistence
	 * between quota record and fs data by following updates, tag the
	 * flag to let fsck be aware of it.
	 */
	if (is_journalled_quota(sbi))
		set_sbi_flag(sbi, SBI_QUOTA_NEED_REPAIR);

	F2FS_SB(sb)->umount_lock_holder = NULL;

	return err;
}

void f2fs_quota_off_umount(struct super_block *sb)
{
	int type;
	int err;

	for (type = 0; type < MAXQUOTAS; type++) {
		err = __f2fs_quota_off(sb, type);
		if (err) {
			int ret = dquot_quota_off(sb, type);

			f2fs_err(F2FS_SB(sb), "Fail to turn off disk quota (type: %d, err: %d, ret:%d), Please run fsck to fix it.",
				 type, err, ret);
			set_sbi_flag(F2FS_SB(sb), SBI_QUOTA_NEED_REPAIR);
		}
	}
	/*
	 * In case of checkpoint=disable, we must flush quota blocks.
	 * This can cause NULL exception for node_inode in end_io, since
	 * put_super already dropped it.
	 */
	sync_filesystem(sb);
}

static void f2fs_truncate_quota_inode_pages(struct super_block *sb)
{
	struct quota_info *dqopt = sb_dqopt(sb);
	int type;

	for (type = 0; type < MAXQUOTAS; type++) {
		if (!dqopt->files[type])
			continue;
		f2fs_inode_synced(dqopt->files[type]);
	}
}

static int f2fs_dquot_commit(struct dquot *dquot)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dquot->dq_sb);
	int ret;

	f2fs_down_read_nested(&sbi->quota_sem, SINGLE_DEPTH_NESTING);
	ret = dquot_commit(dquot);
	if (ret < 0)
		set_sbi_flag(sbi, SBI_QUOTA_NEED_REPAIR);
	f2fs_up_read(&sbi->quota_sem);
	return ret;
}

static int f2fs_dquot_acquire(struct dquot *dquot)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dquot->dq_sb);
	int ret;

	f2fs_down_read(&sbi->quota_sem);
	ret = dquot_acquire(dquot);
	if (ret < 0)
		set_sbi_flag(sbi, SBI_QUOTA_NEED_REPAIR);
	f2fs_up_read(&sbi->quota_sem);
	return ret;
}

static int f2fs_dquot_release(struct dquot *dquot)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dquot->dq_sb);
	int ret = dquot_release(dquot);

	if (ret < 0)
		set_sbi_flag(sbi, SBI_QUOTA_NEED_REPAIR);
	return ret;
}

static int f2fs_dquot_mark_dquot_dirty(struct dquot *dquot)
{
	struct super_block *sb = dquot->dq_sb;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	int ret = dquot_mark_dquot_dirty(dquot);

	/* if we are using journalled quota */
	if (is_journalled_quota(sbi))
		set_sbi_flag(sbi, SBI_QUOTA_NEED_FLUSH);

	return ret;
}

static int f2fs_dquot_commit_info(struct super_block *sb, int type)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	int ret = dquot_commit_info(sb, type);

	if (ret < 0)
		set_sbi_flag(sbi, SBI_QUOTA_NEED_REPAIR);
	return ret;
}

static int f2fs_get_projid(struct inode *inode, kprojid_t *projid)
{
	*projid = F2FS_I(inode)->i_projid;
	return 0;
}

static const struct dquot_operations f2fs_quota_operations = {
	.get_reserved_space = f2fs_get_reserved_space,
	.write_dquot	= f2fs_dquot_commit,
	.acquire_dquot	= f2fs_dquot_acquire,
	.release_dquot	= f2fs_dquot_release,
	.mark_dirty	= f2fs_dquot_mark_dquot_dirty,
	.write_info	= f2fs_dquot_commit_info,
	.alloc_dquot	= dquot_alloc,
	.destroy_dquot	= dquot_destroy,
	.get_projid	= f2fs_get_projid,
	.get_next_id	= dquot_get_next_id,
};

static const struct quotactl_ops f2fs_quotactl_ops = {
	.quota_on	= f2fs_quota_on,
	.quota_off	= f2fs_quota_off,
	.quota_sync	= f2fs_quota_sync,
	.get_state	= dquot_get_state,
	.set_info	= dquot_set_dqinfo,
	.get_dqblk	= dquot_get_dqblk,
	.set_dqblk	= dquot_set_dqblk,
	.get_nextdqblk	= dquot_get_next_dqblk,
};
#else
int f2fs_dquot_initialize(struct inode *inode)
{
	return 0;
}

int f2fs_do_quota_sync(struct super_block *sb, int type)
{
	return 0;
}

void f2fs_quota_off_umount(struct super_block *sb)
{
}
#endif

static const struct super_operations f2fs_sops = {
	.alloc_inode	= f2fs_alloc_inode,
	.free_inode	= f2fs_free_inode,
	.drop_inode	= f2fs_drop_inode,
	.write_inode	= f2fs_write_inode,
	.dirty_inode	= f2fs_dirty_inode,
	.show_options	= f2fs_show_options,
#ifdef CONFIG_QUOTA
	.quota_read	= f2fs_quota_read,
	.quota_write	= f2fs_quota_write,
	.get_dquots	= f2fs_get_dquots,
#endif
	.evict_inode	= f2fs_evict_inode,
	.put_super	= f2fs_put_super,
	.sync_fs	= f2fs_sync_fs,
	.freeze_fs	= f2fs_freeze,
	.unfreeze_fs	= f2fs_unfreeze,
	.statfs		= f2fs_statfs,
	.shutdown	= f2fs_shutdown,
};

#ifdef CONFIG_FS_ENCRYPTION
static int f2fs_get_context(struct inode *inode, void *ctx, size_t len)
{
	return f2fs_getxattr(inode, F2FS_XATTR_INDEX_ENCRYPTION,
				F2FS_XATTR_NAME_ENCRYPTION_CONTEXT,
				ctx, len, NULL);
}

static int f2fs_set_context(struct inode *inode, const void *ctx, size_t len,
							void *fs_data)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);

	/*
	 * Encrypting the root directory is not allowed because fsck
	 * expects lost+found directory to exist and remain unencrypted
	 * if LOST_FOUND feature is enabled.
	 *
	 */
	if (f2fs_sb_has_lost_found(sbi) &&
			inode->i_ino == F2FS_ROOT_INO(sbi))
		return -EPERM;

	return f2fs_setxattr(inode, F2FS_XATTR_INDEX_ENCRYPTION,
				F2FS_XATTR_NAME_ENCRYPTION_CONTEXT,
				ctx, len, fs_data, XATTR_CREATE);
}

static const union fscrypt_policy *f2fs_get_dummy_policy(struct super_block *sb)
{
	return F2FS_OPTION(F2FS_SB(sb)).dummy_enc_policy.policy;
}

static bool f2fs_has_stable_inodes(struct super_block *sb)
{
	return true;
}

static struct block_device **f2fs_get_devices(struct super_block *sb,
					      unsigned int *num_devs)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct block_device **devs;
	int i;

	if (!f2fs_is_multi_device(sbi))
		return NULL;

	devs = kmalloc_array(sbi->s_ndevs, sizeof(*devs), GFP_KERNEL);
	if (!devs)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < sbi->s_ndevs; i++)
		devs[i] = FDEV(i).bdev;
	*num_devs = sbi->s_ndevs;
	return devs;
}

static const struct fscrypt_operations f2fs_cryptops = {
	.inode_info_offs	= (int)offsetof(struct f2fs_inode_info, i_crypt_info) -
				  (int)offsetof(struct f2fs_inode_info, vfs_inode),
	.needs_bounce_pages	= 1,
	.has_32bit_inodes	= 1,
	.supports_subblock_data_units = 1,
	.legacy_key_prefix	= "f2fs:",
	.get_context		= f2fs_get_context,
	.set_context		= f2fs_set_context,
	.get_dummy_policy	= f2fs_get_dummy_policy,
	.empty_dir		= f2fs_empty_dir,
	.has_stable_inodes	= f2fs_has_stable_inodes,
	.get_devices		= f2fs_get_devices,
};
#endif /* CONFIG_FS_ENCRYPTION */

static struct inode *f2fs_nfs_get_inode(struct super_block *sb,
		u64 ino, u32 generation)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct inode *inode;

	if (f2fs_check_nid_range(sbi, ino))
		return ERR_PTR(-ESTALE);

	/*
	 * f2fs_iget isn't quite right if the inode is currently unallocated!
	 * However f2fs_iget currently does appropriate checks to handle stale
	 * inodes so everything is OK.
	 */
	inode = f2fs_iget(sb, ino);
	if (IS_ERR(inode))
		return ERR_CAST(inode);
	if (unlikely(generation && inode->i_generation != generation)) {
		/* we didn't find the right inode.. */
		iput(inode);
		return ERR_PTR(-ESTALE);
	}
	return inode;
}

static struct dentry *f2fs_fh_to_dentry(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    f2fs_nfs_get_inode);
}

static struct dentry *f2fs_fh_to_parent(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    f2fs_nfs_get_inode);
}

static const struct export_operations f2fs_export_ops = {
	.encode_fh = generic_encode_ino32_fh,
	.fh_to_dentry = f2fs_fh_to_dentry,
	.fh_to_parent = f2fs_fh_to_parent,
	.get_parent = f2fs_get_parent,
};

loff_t max_file_blocks(struct inode *inode)
{
	loff_t result = 0;
	loff_t leaf_count;

	/*
	 * note: previously, result is equal to (DEF_ADDRS_PER_INODE -
	 * DEFAULT_INLINE_XATTR_ADDRS), but now f2fs try to reserve more
	 * space in inode.i_addr, it will be more safe to reassign
	 * result as zero.
	 */

	if (inode && f2fs_compressed_file(inode))
		leaf_count = ADDRS_PER_BLOCK(inode);
	else
		leaf_count = DEF_ADDRS_PER_BLOCK;

	/* two direct node blocks */
	result += (leaf_count * 2);

	/* two indirect node blocks */
	leaf_count *= NIDS_PER_BLOCK;
	result += (leaf_count * 2);

	/* one double indirect node block */
	leaf_count *= NIDS_PER_BLOCK;
	result += leaf_count;

	/*
	 * For compatibility with FSCRYPT_POLICY_FLAG_IV_INO_LBLK_{64,32} with
	 * a 4K crypto data unit, we must restrict the max filesize to what can
	 * fit within U32_MAX + 1 data units.
	 */

	result = umin(result, F2FS_BYTES_TO_BLK(((loff_t)U32_MAX + 1) * 4096));

	return result;
}

static int __f2fs_commit_super(struct f2fs_sb_info *sbi, struct folio *folio,
						pgoff_t index, bool update)
{
	struct bio *bio;
	/* it's rare case, we can do fua all the time */
	blk_opf_t opf = REQ_OP_WRITE | REQ_SYNC | REQ_PREFLUSH | REQ_FUA;
	int ret;

	folio_lock(folio);
	folio_wait_writeback(folio);
	if (update)
		memcpy(F2FS_SUPER_BLOCK(folio, index), F2FS_RAW_SUPER(sbi),
					sizeof(struct f2fs_super_block));
	folio_mark_dirty(folio);
	folio_clear_dirty_for_io(folio);
	folio_start_writeback(folio);
	folio_unlock(folio);

	bio = bio_alloc(sbi->sb->s_bdev, 1, opf, GFP_NOFS);

	/* it doesn't need to set crypto context for superblock update */
	bio->bi_iter.bi_sector = SECTOR_FROM_BLOCK(folio->index);

	if (!bio_add_folio(bio, folio, folio_size(folio), 0))
		f2fs_bug_on(sbi, 1);

	ret = submit_bio_wait(bio);
	bio_put(bio);
	folio_end_writeback(folio);

	return ret;
}

static inline bool sanity_check_area_boundary(struct f2fs_sb_info *sbi,
					struct folio *folio, pgoff_t index)
{
	struct f2fs_super_block *raw_super = F2FS_SUPER_BLOCK(folio, index);
	struct super_block *sb = sbi->sb;
	u32 segment0_blkaddr = le32_to_cpu(raw_super->segment0_blkaddr);
	u32 cp_blkaddr = le32_to_cpu(raw_super->cp_blkaddr);
	u32 sit_blkaddr = le32_to_cpu(raw_super->sit_blkaddr);
	u32 nat_blkaddr = le32_to_cpu(raw_super->nat_blkaddr);
	u32 ssa_blkaddr = le32_to_cpu(raw_super->ssa_blkaddr);
	u32 main_blkaddr = le32_to_cpu(raw_super->main_blkaddr);
	u32 segment_count_ckpt = le32_to_cpu(raw_super->segment_count_ckpt);
	u32 segment_count_sit = le32_to_cpu(raw_super->segment_count_sit);
	u32 segment_count_nat = le32_to_cpu(raw_super->segment_count_nat);
	u32 segment_count_ssa = le32_to_cpu(raw_super->segment_count_ssa);
	u32 segment_count_main = le32_to_cpu(raw_super->segment_count_main);
	u32 segment_count = le32_to_cpu(raw_super->segment_count);
	u32 log_blocks_per_seg = le32_to_cpu(raw_super->log_blocks_per_seg);
	u64 main_end_blkaddr = main_blkaddr +
				((u64)segment_count_main << log_blocks_per_seg);
	u64 seg_end_blkaddr = segment0_blkaddr +
				((u64)segment_count << log_blocks_per_seg);

	if (segment0_blkaddr != cp_blkaddr) {
		f2fs_info(sbi, "Mismatch start address, segment0(%u) cp_blkaddr(%u)",
			  segment0_blkaddr, cp_blkaddr);
		return true;
	}

	if (cp_blkaddr + (segment_count_ckpt << log_blocks_per_seg) !=
							sit_blkaddr) {
		f2fs_info(sbi, "Wrong CP boundary, start(%u) end(%u) blocks(%u)",
			  cp_blkaddr, sit_blkaddr,
			  segment_count_ckpt << log_blocks_per_seg);
		return true;
	}

	if (sit_blkaddr + (segment_count_sit << log_blocks_per_seg) !=
							nat_blkaddr) {
		f2fs_info(sbi, "Wrong SIT boundary, start(%u) end(%u) blocks(%u)",
			  sit_blkaddr, nat_blkaddr,
			  segment_count_sit << log_blocks_per_seg);
		return true;
	}

	if (nat_blkaddr + (segment_count_nat << log_blocks_per_seg) !=
							ssa_blkaddr) {
		f2fs_info(sbi, "Wrong NAT boundary, start(%u) end(%u) blocks(%u)",
			  nat_blkaddr, ssa_blkaddr,
			  segment_count_nat << log_blocks_per_seg);
		return true;
	}

	if (ssa_blkaddr + (segment_count_ssa << log_blocks_per_seg) !=
							main_blkaddr) {
		f2fs_info(sbi, "Wrong SSA boundary, start(%u) end(%u) blocks(%u)",
			  ssa_blkaddr, main_blkaddr,
			  segment_count_ssa << log_blocks_per_seg);
		return true;
	}

	if (main_end_blkaddr > seg_end_blkaddr) {
		f2fs_info(sbi, "Wrong MAIN_AREA boundary, start(%u) end(%llu) block(%u)",
			  main_blkaddr, seg_end_blkaddr,
			  segment_count_main << log_blocks_per_seg);
		return true;
	} else if (main_end_blkaddr < seg_end_blkaddr) {
		int err = 0;
		char *res;

		/* fix in-memory information all the time */
		raw_super->segment_count = cpu_to_le32((main_end_blkaddr -
				segment0_blkaddr) >> log_blocks_per_seg);

		if (f2fs_readonly(sb) || f2fs_hw_is_readonly(sbi)) {
			set_sbi_flag(sbi, SBI_NEED_SB_WRITE);
			res = "internally";
		} else {
			err = __f2fs_commit_super(sbi, folio, index, false);
			res = err ? "failed" : "done";
		}
		f2fs_info(sbi, "Fix alignment : %s, start(%u) end(%llu) block(%u)",
			  res, main_blkaddr, seg_end_blkaddr,
			  segment_count_main << log_blocks_per_seg);
		if (err)
			return true;
	}
	return false;
}

static int sanity_check_raw_super(struct f2fs_sb_info *sbi,
					struct folio *folio, pgoff_t index)
{
	block_t segment_count, segs_per_sec, secs_per_zone, segment_count_main;
	block_t total_sections, blocks_per_seg;
	struct f2fs_super_block *raw_super = F2FS_SUPER_BLOCK(folio, index);
	size_t crc_offset = 0;
	__u32 crc = 0;

	if (le32_to_cpu(raw_super->magic) != F2FS_SUPER_MAGIC) {
		f2fs_info(sbi, "Magic Mismatch, valid(0x%x) - read(0x%x)",
			  F2FS_SUPER_MAGIC, le32_to_cpu(raw_super->magic));
		return -EINVAL;
	}

	/* Check checksum_offset and crc in superblock */
	if (__F2FS_HAS_FEATURE(raw_super, F2FS_FEATURE_SB_CHKSUM)) {
		crc_offset = le32_to_cpu(raw_super->checksum_offset);
		if (crc_offset !=
			offsetof(struct f2fs_super_block, crc)) {
			f2fs_info(sbi, "Invalid SB checksum offset: %zu",
				  crc_offset);
			return -EFSCORRUPTED;
		}
		crc = le32_to_cpu(raw_super->crc);
		if (crc != f2fs_crc32(raw_super, crc_offset)) {
			f2fs_info(sbi, "Invalid SB checksum value: %u", crc);
			return -EFSCORRUPTED;
		}
	}

	/* only support block_size equals to PAGE_SIZE */
	if (le32_to_cpu(raw_super->log_blocksize) != F2FS_BLKSIZE_BITS) {
		f2fs_info(sbi, "Invalid log_blocksize (%u), supports only %u",
			  le32_to_cpu(raw_super->log_blocksize),
			  F2FS_BLKSIZE_BITS);
		return -EFSCORRUPTED;
	}

	/* check log blocks per segment */
	if (le32_to_cpu(raw_super->log_blocks_per_seg) != 9) {
		f2fs_info(sbi, "Invalid log blocks per segment (%u)",
			  le32_to_cpu(raw_super->log_blocks_per_seg));
		return -EFSCORRUPTED;
	}

	/* Currently, support 512/1024/2048/4096/16K bytes sector size */
	if (le32_to_cpu(raw_super->log_sectorsize) >
				F2FS_MAX_LOG_SECTOR_SIZE ||
		le32_to_cpu(raw_super->log_sectorsize) <
				F2FS_MIN_LOG_SECTOR_SIZE) {
		f2fs_info(sbi, "Invalid log sectorsize (%u)",
			  le32_to_cpu(raw_super->log_sectorsize));
		return -EFSCORRUPTED;
	}
	if (le32_to_cpu(raw_super->log_sectors_per_block) +
		le32_to_cpu(raw_super->log_sectorsize) !=
			F2FS_MAX_LOG_SECTOR_SIZE) {
		f2fs_info(sbi, "Invalid log sectors per block(%u) log sectorsize(%u)",
			  le32_to_cpu(raw_super->log_sectors_per_block),
			  le32_to_cpu(raw_super->log_sectorsize));
		return -EFSCORRUPTED;
	}

	segment_count = le32_to_cpu(raw_super->segment_count);
	segment_count_main = le32_to_cpu(raw_super->segment_count_main);
	segs_per_sec = le32_to_cpu(raw_super->segs_per_sec);
	secs_per_zone = le32_to_cpu(raw_super->secs_per_zone);
	total_sections = le32_to_cpu(raw_super->section_count);

	/* blocks_per_seg should be 512, given the above check */
	blocks_per_seg = BIT(le32_to_cpu(raw_super->log_blocks_per_seg));

	if (segment_count > F2FS_MAX_SEGMENT ||
				segment_count < F2FS_MIN_SEGMENTS) {
		f2fs_info(sbi, "Invalid segment count (%u)", segment_count);
		return -EFSCORRUPTED;
	}

	if (total_sections > segment_count_main || total_sections < 1 ||
			segs_per_sec > segment_count || !segs_per_sec) {
		f2fs_info(sbi, "Invalid segment/section count (%u, %u x %u)",
			  segment_count, total_sections, segs_per_sec);
		return -EFSCORRUPTED;
	}

	if (segment_count_main != total_sections * segs_per_sec) {
		f2fs_info(sbi, "Invalid segment/section count (%u != %u * %u)",
			  segment_count_main, total_sections, segs_per_sec);
		return -EFSCORRUPTED;
	}

	if ((segment_count / segs_per_sec) < total_sections) {
		f2fs_info(sbi, "Small segment_count (%u < %u * %u)",
			  segment_count, segs_per_sec, total_sections);
		return -EFSCORRUPTED;
	}

	if (segment_count > (le64_to_cpu(raw_super->block_count) >> 9)) {
		f2fs_info(sbi, "Wrong segment_count / block_count (%u > %llu)",
			  segment_count, le64_to_cpu(raw_super->block_count));
		return -EFSCORRUPTED;
	}

	if (RDEV(0).path[0]) {
		block_t dev_seg_count = le32_to_cpu(RDEV(0).total_segments);
		int i = 1;

		while (i < MAX_DEVICES && RDEV(i).path[0]) {
			dev_seg_count += le32_to_cpu(RDEV(i).total_segments);
			i++;
		}
		if (segment_count != dev_seg_count) {
			f2fs_info(sbi, "Segment count (%u) mismatch with total segments from devices (%u)",
					segment_count, dev_seg_count);
			return -EFSCORRUPTED;
		}
	} else {
		if (__F2FS_HAS_FEATURE(raw_super, F2FS_FEATURE_BLKZONED) &&
					!bdev_is_zoned(sbi->sb->s_bdev)) {
			f2fs_info(sbi, "Zoned block device path is missing");
			return -EFSCORRUPTED;
		}
	}

	if (secs_per_zone > total_sections || !secs_per_zone) {
		f2fs_info(sbi, "Wrong secs_per_zone / total_sections (%u, %u)",
			  secs_per_zone, total_sections);
		return -EFSCORRUPTED;
	}
	if (le32_to_cpu(raw_super->extension_count) > F2FS_MAX_EXTENSION ||
			raw_super->hot_ext_count > F2FS_MAX_EXTENSION ||
			(le32_to_cpu(raw_super->extension_count) +
			raw_super->hot_ext_count) > F2FS_MAX_EXTENSION) {
		f2fs_info(sbi, "Corrupted extension count (%u + %u > %u)",
			  le32_to_cpu(raw_super->extension_count),
			  raw_super->hot_ext_count,
			  F2FS_MAX_EXTENSION);
		return -EFSCORRUPTED;
	}

	if (le32_to_cpu(raw_super->cp_payload) >=
				(blocks_per_seg - F2FS_CP_PACKS -
				NR_CURSEG_PERSIST_TYPE)) {
		f2fs_info(sbi, "Insane cp_payload (%u >= %u)",
			  le32_to_cpu(raw_super->cp_payload),
			  blocks_per_seg - F2FS_CP_PACKS -
			  NR_CURSEG_PERSIST_TYPE);
		return -EFSCORRUPTED;
	}

	/* check reserved ino info */
	if (le32_to_cpu(raw_super->node_ino) != 1 ||
		le32_to_cpu(raw_super->meta_ino) != 2 ||
		le32_to_cpu(raw_super->root_ino) != 3) {
		f2fs_info(sbi, "Invalid Fs Meta Ino: node(%u) meta(%u) root(%u)",
			  le32_to_cpu(raw_super->node_ino),
			  le32_to_cpu(raw_super->meta_ino),
			  le32_to_cpu(raw_super->root_ino));
		return -EFSCORRUPTED;
	}

	/* check CP/SIT/NAT/SSA/MAIN_AREA area boundary */
	if (sanity_check_area_boundary(sbi, folio, index))
		return -EFSCORRUPTED;

	return 0;
}

int f2fs_sanity_check_ckpt(struct f2fs_sb_info *sbi)
{
	unsigned int total, fsmeta;
	struct f2fs_super_block *raw_super = F2FS_RAW_SUPER(sbi);
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);
	unsigned int ovp_segments, reserved_segments;
	unsigned int main_segs, blocks_per_seg;
	unsigned int sit_segs, nat_segs;
	unsigned int sit_bitmap_size, nat_bitmap_size;
	unsigned int log_blocks_per_seg;
	unsigned int segment_count_main;
	unsigned int cp_pack_start_sum, cp_payload;
	block_t user_block_count, valid_user_blocks;
	block_t avail_node_count, valid_node_count;
	unsigned int nat_blocks, nat_bits_bytes, nat_bits_blocks;
	unsigned int sit_blk_cnt;
	int i, j;

	total = le32_to_cpu(raw_super->segment_count);
	fsmeta = le32_to_cpu(raw_super->segment_count_ckpt);
	sit_segs = le32_to_cpu(raw_super->segment_count_sit);
	fsmeta += sit_segs;
	nat_segs = le32_to_cpu(raw_super->segment_count_nat);
	fsmeta += nat_segs;
	fsmeta += le32_to_cpu(ckpt->rsvd_segment_count);
	fsmeta += le32_to_cpu(raw_super->segment_count_ssa);

	if (unlikely(fsmeta >= total))
		return 1;

	ovp_segments = le32_to_cpu(ckpt->overprov_segment_count);
	reserved_segments = le32_to_cpu(ckpt->rsvd_segment_count);

	if (!f2fs_sb_has_readonly(sbi) &&
			unlikely(fsmeta < F2FS_MIN_META_SEGMENTS ||
			ovp_segments == 0 || reserved_segments == 0)) {
		f2fs_err(sbi, "Wrong layout: check mkfs.f2fs version");
		return 1;
	}
	user_block_count = le64_to_cpu(ckpt->user_block_count);
	segment_count_main = le32_to_cpu(raw_super->segment_count_main) +
			(f2fs_sb_has_readonly(sbi) ? 1 : 0);
	log_blocks_per_seg = le32_to_cpu(raw_super->log_blocks_per_seg);
	if (!user_block_count || user_block_count >=
			segment_count_main << log_blocks_per_seg) {
		f2fs_err(sbi, "Wrong user_block_count: %u",
			 user_block_count);
		return 1;
	}

	valid_user_blocks = le64_to_cpu(ckpt->valid_block_count);
	if (valid_user_blocks > user_block_count) {
		f2fs_err(sbi, "Wrong valid_user_blocks: %u, user_block_count: %u",
			 valid_user_blocks, user_block_count);
		return 1;
	}

	valid_node_count = le32_to_cpu(ckpt->valid_node_count);
	avail_node_count = sbi->total_node_count - F2FS_RESERVED_NODE_NUM;
	if (valid_node_count > avail_node_count) {
		f2fs_err(sbi, "Wrong valid_node_count: %u, avail_node_count: %u",
			 valid_node_count, avail_node_count);
		return 1;
	}

	main_segs = le32_to_cpu(raw_super->segment_count_main);
	blocks_per_seg = BLKS_PER_SEG(sbi);

	for (i = 0; i < NR_CURSEG_NODE_TYPE; i++) {
		if (le32_to_cpu(ckpt->cur_node_segno[i]) >= main_segs ||
			le16_to_cpu(ckpt->cur_node_blkoff[i]) >= blocks_per_seg)
			return 1;

		if (f2fs_sb_has_readonly(sbi))
			goto check_data;

		for (j = i + 1; j < NR_CURSEG_NODE_TYPE; j++) {
			if (le32_to_cpu(ckpt->cur_node_segno[i]) ==
				le32_to_cpu(ckpt->cur_node_segno[j])) {
				f2fs_err(sbi, "Node segment (%u, %u) has the same segno: %u",
					 i, j,
					 le32_to_cpu(ckpt->cur_node_segno[i]));
				return 1;
			}
		}
	}
check_data:
	for (i = 0; i < NR_CURSEG_DATA_TYPE; i++) {
		if (le32_to_cpu(ckpt->cur_data_segno[i]) >= main_segs ||
			le16_to_cpu(ckpt->cur_data_blkoff[i]) >= blocks_per_seg)
			return 1;

		if (f2fs_sb_has_readonly(sbi))
			goto skip_cross;

		for (j = i + 1; j < NR_CURSEG_DATA_TYPE; j++) {
			if (le32_to_cpu(ckpt->cur_data_segno[i]) ==
				le32_to_cpu(ckpt->cur_data_segno[j])) {
				f2fs_err(sbi, "Data segment (%u, %u) has the same segno: %u",
					 i, j,
					 le32_to_cpu(ckpt->cur_data_segno[i]));
				return 1;
			}
		}
	}
	for (i = 0; i < NR_CURSEG_NODE_TYPE; i++) {
		for (j = 0; j < NR_CURSEG_DATA_TYPE; j++) {
			if (le32_to_cpu(ckpt->cur_node_segno[i]) ==
				le32_to_cpu(ckpt->cur_data_segno[j])) {
				f2fs_err(sbi, "Node segment (%u) and Data segment (%u) has the same segno: %u",
					 i, j,
					 le32_to_cpu(ckpt->cur_node_segno[i]));
				return 1;
			}
		}
	}
skip_cross:
	sit_bitmap_size = le32_to_cpu(ckpt->sit_ver_bitmap_bytesize);
	nat_bitmap_size = le32_to_cpu(ckpt->nat_ver_bitmap_bytesize);

	if (sit_bitmap_size != ((sit_segs / 2) << log_blocks_per_seg) / 8 ||
		nat_bitmap_size != ((nat_segs / 2) << log_blocks_per_seg) / 8) {
		f2fs_err(sbi, "Wrong bitmap size: sit: %u, nat:%u",
			 sit_bitmap_size, nat_bitmap_size);
		return 1;
	}

	sit_blk_cnt = DIV_ROUND_UP(main_segs, SIT_ENTRY_PER_BLOCK);
	if (sit_bitmap_size * 8 < sit_blk_cnt) {
		f2fs_err(sbi, "Wrong bitmap size: sit: %u, sit_blk_cnt:%u",
			 sit_bitmap_size, sit_blk_cnt);
		return 1;
	}

	cp_pack_start_sum = __start_sum_addr(sbi);
	cp_payload = __cp_payload(sbi);
	if (cp_pack_start_sum < cp_payload + 1 ||
		cp_pack_start_sum > blocks_per_seg - 1 -
			NR_CURSEG_PERSIST_TYPE) {
		f2fs_err(sbi, "Wrong cp_pack_start_sum: %u",
			 cp_pack_start_sum);
		return 1;
	}

	if (__is_set_ckpt_flags(ckpt, CP_LARGE_NAT_BITMAP_FLAG) &&
		le32_to_cpu(ckpt->checksum_offset) != CP_MIN_CHKSUM_OFFSET) {
		f2fs_warn(sbi, "using deprecated layout of large_nat_bitmap, "
			  "please run fsck v1.13.0 or higher to repair, chksum_offset: %u, "
			  "fixed with patch: \"f2fs-tools: relocate chksum_offset for large_nat_bitmap feature\"",
			  le32_to_cpu(ckpt->checksum_offset));
		return 1;
	}

	nat_blocks = nat_segs << log_blocks_per_seg;
	nat_bits_bytes = nat_blocks / BITS_PER_BYTE;
	nat_bits_blocks = F2FS_BLK_ALIGN((nat_bits_bytes << 1) + 8);
	if (__is_set_ckpt_flags(ckpt, CP_NAT_BITS_FLAG) &&
		(cp_payload + F2FS_CP_PACKS +
		NR_CURSEG_PERSIST_TYPE + nat_bits_blocks >= blocks_per_seg)) {
		f2fs_warn(sbi, "Insane cp_payload: %u, nat_bits_blocks: %u)",
			  cp_payload, nat_bits_blocks);
		return 1;
	}

	if (unlikely(f2fs_cp_error(sbi))) {
		f2fs_err(sbi, "A bug case: need to run fsck");
		return 1;
	}
	return 0;
}

static void init_sb_info(struct f2fs_sb_info *sbi)
{
	struct f2fs_super_block *raw_super = sbi->raw_super;
	int i;

	sbi->log_sectors_per_block =
		le32_to_cpu(raw_super->log_sectors_per_block);
	sbi->log_blocksize = le32_to_cpu(raw_super->log_blocksize);
	sbi->blocksize = BIT(sbi->log_blocksize);
	sbi->log_blocks_per_seg = le32_to_cpu(raw_super->log_blocks_per_seg);
	sbi->blocks_per_seg = BIT(sbi->log_blocks_per_seg);
	sbi->segs_per_sec = le32_to_cpu(raw_super->segs_per_sec);
	sbi->secs_per_zone = le32_to_cpu(raw_super->secs_per_zone);
	sbi->total_sections = le32_to_cpu(raw_super->section_count);
	sbi->total_node_count = SEGS_TO_BLKS(sbi,
			((le32_to_cpu(raw_super->segment_count_nat) / 2) *
			NAT_ENTRY_PER_BLOCK));
	sbi->allocate_section_hint = le32_to_cpu(raw_super->section_count);
	sbi->allocate_section_policy = ALLOCATE_FORWARD_NOHINT;
	F2FS_ROOT_INO(sbi) = le32_to_cpu(raw_super->root_ino);
	F2FS_NODE_INO(sbi) = le32_to_cpu(raw_super->node_ino);
	F2FS_META_INO(sbi) = le32_to_cpu(raw_super->meta_ino);
	sbi->cur_victim_sec = NULL_SECNO;
	sbi->gc_mode = GC_NORMAL;
	sbi->next_victim_seg[BG_GC] = NULL_SEGNO;
	sbi->next_victim_seg[FG_GC] = NULL_SEGNO;
	sbi->max_victim_search = DEF_MAX_VICTIM_SEARCH;
	sbi->migration_granularity = SEGS_PER_SEC(sbi);
	sbi->migration_window_granularity = f2fs_sb_has_blkzoned(sbi) ?
		DEF_MIGRATION_WINDOW_GRANULARITY_ZONED : SEGS_PER_SEC(sbi);
	sbi->seq_file_ra_mul = MIN_RA_MUL;
	sbi->max_fragment_chunk = DEF_FRAGMENT_SIZE;
	sbi->max_fragment_hole = DEF_FRAGMENT_SIZE;
	spin_lock_init(&sbi->gc_remaining_trials_lock);
	atomic64_set(&sbi->current_atomic_write, 0);

	sbi->dir_level = DEF_DIR_LEVEL;
	sbi->interval_time[CP_TIME] = DEF_CP_INTERVAL;
	sbi->interval_time[REQ_TIME] = DEF_IDLE_INTERVAL;
	sbi->interval_time[DISCARD_TIME] = DEF_IDLE_INTERVAL;
	sbi->interval_time[GC_TIME] = DEF_IDLE_INTERVAL;
	sbi->interval_time[DISABLE_TIME] = DEF_DISABLE_INTERVAL;
	sbi->interval_time[ENABLE_TIME] = DEF_ENABLE_INTERVAL;
	sbi->interval_time[UMOUNT_DISCARD_TIMEOUT] =
				DEF_UMOUNT_DISCARD_TIMEOUT;
	clear_sbi_flag(sbi, SBI_NEED_FSCK);

	for (i = 0; i < NR_COUNT_TYPE; i++)
		atomic_set(&sbi->nr_pages[i], 0);

	for (i = 0; i < META; i++)
		atomic_set(&sbi->wb_sync_req[i], 0);

	INIT_LIST_HEAD(&sbi->s_list);
	mutex_init(&sbi->umount_mutex);
	init_f2fs_rwsem(&sbi->io_order_lock);
	spin_lock_init(&sbi->cp_lock);

	sbi->dirty_device = 0;
	spin_lock_init(&sbi->dev_lock);

	init_f2fs_rwsem(&sbi->sb_lock);
	init_f2fs_rwsem(&sbi->pin_sem);
}

static int init_percpu_info(struct f2fs_sb_info *sbi)
{
	int err;

	err = percpu_counter_init(&sbi->alloc_valid_block_count, 0, GFP_KERNEL);
	if (err)
		return err;

	err = percpu_counter_init(&sbi->rf_node_block_count, 0, GFP_KERNEL);
	if (err)
		goto err_valid_block;

	err = percpu_counter_init(&sbi->total_valid_inode_count, 0,
								GFP_KERNEL);
	if (err)
		goto err_node_block;
	return 0;

err_node_block:
	percpu_counter_destroy(&sbi->rf_node_block_count);
err_valid_block:
	percpu_counter_destroy(&sbi->alloc_valid_block_count);
	return err;
}

#ifdef CONFIG_BLK_DEV_ZONED

struct f2fs_report_zones_args {
	struct f2fs_sb_info *sbi;
	struct f2fs_dev_info *dev;
};

static int f2fs_report_zone_cb(struct blk_zone *zone, unsigned int idx,
			      void *data)
{
	struct f2fs_report_zones_args *rz_args = data;
	block_t unusable_blocks = (zone->len - zone->capacity) >>
					F2FS_LOG_SECTORS_PER_BLOCK;

	if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL)
		return 0;

	set_bit(idx, rz_args->dev->blkz_seq);
	if (!rz_args->sbi->unusable_blocks_per_sec) {
		rz_args->sbi->unusable_blocks_per_sec = unusable_blocks;
		return 0;
	}
	if (rz_args->sbi->unusable_blocks_per_sec != unusable_blocks) {
		f2fs_err(rz_args->sbi, "F2FS supports single zone capacity\n");
		return -EINVAL;
	}
	return 0;
}

static int init_blkz_info(struct f2fs_sb_info *sbi, int devi)
{
	struct block_device *bdev = FDEV(devi).bdev;
	sector_t nr_sectors = bdev_nr_sectors(bdev);
	struct f2fs_report_zones_args rep_zone_arg;
	u64 zone_sectors;
	unsigned int max_open_zones;
	int ret;

	if (!f2fs_sb_has_blkzoned(sbi))
		return 0;

	if (bdev_is_zoned(FDEV(devi).bdev)) {
		max_open_zones = bdev_max_open_zones(bdev);
		if (max_open_zones && (max_open_zones < sbi->max_open_zones))
			sbi->max_open_zones = max_open_zones;
		if (sbi->max_open_zones < F2FS_OPTION(sbi).active_logs) {
			f2fs_err(sbi,
				"zoned: max open zones %u is too small, need at least %u open zones",
				sbi->max_open_zones, F2FS_OPTION(sbi).active_logs);
			return -EINVAL;
		}
	}

	zone_sectors = bdev_zone_sectors(bdev);
	if (sbi->blocks_per_blkz && sbi->blocks_per_blkz !=
				SECTOR_TO_BLOCK(zone_sectors))
		return -EINVAL;
	sbi->blocks_per_blkz = SECTOR_TO_BLOCK(zone_sectors);
	FDEV(devi).nr_blkz = div_u64(SECTOR_TO_BLOCK(nr_sectors),
					sbi->blocks_per_blkz);
	if (nr_sectors & (zone_sectors - 1))
		FDEV(devi).nr_blkz++;

	FDEV(devi).blkz_seq = f2fs_kvzalloc(sbi,
					BITS_TO_LONGS(FDEV(devi).nr_blkz)
					* sizeof(unsigned long),
					GFP_KERNEL);
	if (!FDEV(devi).blkz_seq)
		return -ENOMEM;

	rep_zone_arg.sbi = sbi;
	rep_zone_arg.dev = &FDEV(devi);

	ret = blkdev_report_zones(bdev, 0, BLK_ALL_ZONES, f2fs_report_zone_cb,
				  &rep_zone_arg);
	if (ret < 0)
		return ret;
	return 0;
}
#endif

/*
 * Read f2fs raw super block.
 * Because we have two copies of super block, so read both of them
 * to get the first valid one. If any one of them is broken, we pass
 * them recovery flag back to the caller.
 */
static int read_raw_super_block(struct f2fs_sb_info *sbi,
			struct f2fs_super_block **raw_super,
			int *valid_super_block, int *recovery)
{
	struct super_block *sb = sbi->sb;
	int block;
	struct folio *folio;
	struct f2fs_super_block *super;
	int err = 0;

	super = kzalloc(sizeof(struct f2fs_super_block), GFP_KERNEL);
	if (!super)
		return -ENOMEM;

	for (block = 0; block < 2; block++) {
		folio = read_mapping_folio(sb->s_bdev->bd_mapping, block, NULL);
		if (IS_ERR(folio)) {
			f2fs_err(sbi, "Unable to read %dth superblock",
				 block + 1);
			err = PTR_ERR(folio);
			*recovery = 1;
			continue;
		}

		/* sanity checking of raw super */
		err = sanity_check_raw_super(sbi, folio, block);
		if (err) {
			f2fs_err(sbi, "Can't find valid F2FS filesystem in %dth superblock",
				 block + 1);
			folio_put(folio);
			*recovery = 1;
			continue;
		}

		if (!*raw_super) {
			memcpy(super, F2FS_SUPER_BLOCK(folio, block),
							sizeof(*super));
			*valid_super_block = block;
			*raw_super = super;
		}
		folio_put(folio);
	}

	/* No valid superblock */
	if (!*raw_super)
		kfree(super);
	else
		err = 0;

	return err;
}

int f2fs_commit_super(struct f2fs_sb_info *sbi, bool recover)
{
	struct folio *folio;
	pgoff_t index;
	__u32 crc = 0;
	int err;

	if ((recover && f2fs_readonly(sbi->sb)) ||
				f2fs_hw_is_readonly(sbi)) {
		set_sbi_flag(sbi, SBI_NEED_SB_WRITE);
		return -EROFS;
	}

	/* we should update superblock crc here */
	if (!recover && f2fs_sb_has_sb_chksum(sbi)) {
		crc = f2fs_crc32(F2FS_RAW_SUPER(sbi),
				offsetof(struct f2fs_super_block, crc));
		F2FS_RAW_SUPER(sbi)->crc = cpu_to_le32(crc);
	}

	/* write back-up superblock first */
	index = sbi->valid_super_block ? 0 : 1;
	folio = read_mapping_folio(sbi->sb->s_bdev->bd_mapping, index, NULL);
	if (IS_ERR(folio))
		return PTR_ERR(folio);
	err = __f2fs_commit_super(sbi, folio, index, true);
	folio_put(folio);

	/* if we are in recovery path, skip writing valid superblock */
	if (recover || err)
		return err;

	/* write current valid superblock */
	index = sbi->valid_super_block;
	folio = read_mapping_folio(sbi->sb->s_bdev->bd_mapping, index, NULL);
	if (IS_ERR(folio))
		return PTR_ERR(folio);
	err = __f2fs_commit_super(sbi, folio, index, true);
	folio_put(folio);
	return err;
}

static void save_stop_reason(struct f2fs_sb_info *sbi, unsigned char reason)
{
	unsigned long flags;

	spin_lock_irqsave(&sbi->error_lock, flags);
	if (sbi->stop_reason[reason] < GENMASK(BITS_PER_BYTE - 1, 0))
		sbi->stop_reason[reason]++;
	spin_unlock_irqrestore(&sbi->error_lock, flags);
}

static void f2fs_record_stop_reason(struct f2fs_sb_info *sbi)
{
	struct f2fs_super_block *raw_super = F2FS_RAW_SUPER(sbi);
	unsigned long flags;
	int err;

	f2fs_down_write(&sbi->sb_lock);

	spin_lock_irqsave(&sbi->error_lock, flags);
	if (sbi->error_dirty) {
		memcpy(F2FS_RAW_SUPER(sbi)->s_errors, sbi->errors,
							MAX_F2FS_ERRORS);
		sbi->error_dirty = false;
	}
	memcpy(raw_super->s_stop_reason, sbi->stop_reason, MAX_STOP_REASON);
	spin_unlock_irqrestore(&sbi->error_lock, flags);

	err = f2fs_commit_super(sbi, false);

	f2fs_up_write(&sbi->sb_lock);
	if (err)
		f2fs_err_ratelimited(sbi,
			"f2fs_commit_super fails to record stop_reason, err:%d",
			err);
}

void f2fs_save_errors(struct f2fs_sb_info *sbi, unsigned char flag)
{
	unsigned long flags;

	spin_lock_irqsave(&sbi->error_lock, flags);
	if (!test_bit(flag, (unsigned long *)sbi->errors)) {
		set_bit(flag, (unsigned long *)sbi->errors);
		sbi->error_dirty = true;
	}
	spin_unlock_irqrestore(&sbi->error_lock, flags);
}

static bool f2fs_update_errors(struct f2fs_sb_info *sbi)
{
	unsigned long flags;
	bool need_update = false;

	spin_lock_irqsave(&sbi->error_lock, flags);
	if (sbi->error_dirty) {
		memcpy(F2FS_RAW_SUPER(sbi)->s_errors, sbi->errors,
							MAX_F2FS_ERRORS);
		sbi->error_dirty = false;
		need_update = true;
	}
	spin_unlock_irqrestore(&sbi->error_lock, flags);

	return need_update;
}

static void f2fs_record_errors(struct f2fs_sb_info *sbi, unsigned char error)
{
	int err;

	f2fs_down_write(&sbi->sb_lock);

	if (!f2fs_update_errors(sbi))
		goto out_unlock;

	err = f2fs_commit_super(sbi, false);
	if (err)
		f2fs_err_ratelimited(sbi,
			"f2fs_commit_super fails to record errors:%u, err:%d",
			error, err);
out_unlock:
	f2fs_up_write(&sbi->sb_lock);
}

void f2fs_handle_error(struct f2fs_sb_info *sbi, unsigned char error)
{
	f2fs_save_errors(sbi, error);
	f2fs_record_errors(sbi, error);
}

void f2fs_handle_error_async(struct f2fs_sb_info *sbi, unsigned char error)
{
	f2fs_save_errors(sbi, error);

	if (!sbi->error_dirty)
		return;
	if (!test_bit(error, (unsigned long *)sbi->errors))
		return;
	schedule_work(&sbi->s_error_work);
}

static bool system_going_down(void)
{
	return system_state == SYSTEM_HALT || system_state == SYSTEM_POWER_OFF
		|| system_state == SYSTEM_RESTART;
}

void f2fs_handle_critical_error(struct f2fs_sb_info *sbi, unsigned char reason)
{
	struct super_block *sb = sbi->sb;
	bool shutdown = reason == STOP_CP_REASON_SHUTDOWN;
	bool continue_fs = !shutdown &&
			F2FS_OPTION(sbi).errors == MOUNT_ERRORS_CONTINUE;

	set_ckpt_flags(sbi, CP_ERROR_FLAG);

	if (!f2fs_hw_is_readonly(sbi)) {
		save_stop_reason(sbi, reason);

		/*
		 * always create an asynchronous task to record stop_reason
		 * in order to avoid potential deadlock when running into
		 * f2fs_record_stop_reason() synchronously.
		 */
		schedule_work(&sbi->s_error_work);
	}

	/*
	 * We force ERRORS_RO behavior when system is rebooting. Otherwise we
	 * could panic during 'reboot -f' as the underlying device got already
	 * disabled.
	 */
	if (F2FS_OPTION(sbi).errors == MOUNT_ERRORS_PANIC &&
				!shutdown && !system_going_down() &&
				!is_sbi_flag_set(sbi, SBI_IS_SHUTDOWN))
		panic("F2FS-fs (device %s): panic forced after error\n",
							sb->s_id);

	if (shutdown)
		set_sbi_flag(sbi, SBI_IS_SHUTDOWN);
	else
		dump_stack();

	/*
	 * Continue filesystem operators if errors=continue. Should not set
	 * RO by shutdown, since RO bypasses thaw_super which can hang the
	 * system.
	 */
	if (continue_fs || f2fs_readonly(sb) || shutdown) {
		f2fs_warn(sbi, "Stopped filesystem due to reason: %d", reason);
		return;
	}

	f2fs_warn(sbi, "Remounting filesystem read-only");

	/*
	 * We have already set CP_ERROR_FLAG flag to stop all updates
	 * to filesystem, so it doesn't need to set SB_RDONLY flag here
	 * because the flag should be set covered w/ sb->s_umount semaphore
	 * via remount procedure, otherwise, it will confuse code like
	 * freeze_super() which will lead to deadlocks and other problems.
	 */
}

static void f2fs_record_error_work(struct work_struct *work)
{
	struct f2fs_sb_info *sbi = container_of(work,
					struct f2fs_sb_info, s_error_work);

	f2fs_record_stop_reason(sbi);
}

static inline unsigned int get_first_seq_zone_segno(struct f2fs_sb_info *sbi)
{
#ifdef CONFIG_BLK_DEV_ZONED
	unsigned int zoneno, total_zones;
	int devi;

	if (!f2fs_sb_has_blkzoned(sbi))
		return NULL_SEGNO;

	for (devi = 0; devi < sbi->s_ndevs; devi++) {
		if (!bdev_is_zoned(FDEV(devi).bdev))
			continue;

		total_zones = GET_ZONE_FROM_SEG(sbi, FDEV(devi).total_segments);

		for (zoneno = 0; zoneno < total_zones; zoneno++) {
			unsigned int segs, blks;

			if (!f2fs_zone_is_seq(sbi, devi, zoneno))
				continue;

			segs = GET_SEG_FROM_SEC(sbi,
					zoneno * sbi->secs_per_zone);
			blks = SEGS_TO_BLKS(sbi, segs);
			return GET_SEGNO(sbi, FDEV(devi).start_blk + blks);
		}
	}
#endif
	return NULL_SEGNO;
}

static int f2fs_scan_devices(struct f2fs_sb_info *sbi)
{
	struct f2fs_super_block *raw_super = F2FS_RAW_SUPER(sbi);
	unsigned int max_devices = MAX_DEVICES;
	unsigned int logical_blksize;
	blk_mode_t mode = sb_open_mode(sbi->sb->s_flags);
	int i;

	/* Initialize single device information */
	if (!RDEV(0).path[0]) {
		if (!bdev_is_zoned(sbi->sb->s_bdev))
			return 0;
		max_devices = 1;
	}

	/*
	 * Initialize multiple devices information, or single
	 * zoned block device information.
	 */
	sbi->devs = f2fs_kzalloc(sbi,
				 array_size(max_devices,
					    sizeof(struct f2fs_dev_info)),
				 GFP_KERNEL);
	if (!sbi->devs)
		return -ENOMEM;

	logical_blksize = bdev_logical_block_size(sbi->sb->s_bdev);
	sbi->aligned_blksize = true;
	sbi->bggc_io_aware = AWARE_ALL_IO;
#ifdef CONFIG_BLK_DEV_ZONED
	sbi->max_open_zones = UINT_MAX;
	sbi->blkzone_alloc_policy = BLKZONE_ALLOC_PRIOR_SEQ;
	sbi->bggc_io_aware = AWARE_READ_IO;
#endif

	for (i = 0; i < max_devices; i++) {
		if (max_devices == 1) {
			FDEV(i).total_segments =
				le32_to_cpu(raw_super->segment_count_main);
			FDEV(i).start_blk = 0;
			FDEV(i).end_blk = FDEV(i).total_segments *
						BLKS_PER_SEG(sbi);
		}

		if (i == 0)
			FDEV(0).bdev_file = sbi->sb->s_bdev_file;
		else if (!RDEV(i).path[0])
			break;

		if (max_devices > 1) {
			/* Multi-device mount */
			memcpy(FDEV(i).path, RDEV(i).path, MAX_PATH_LEN);
			FDEV(i).total_segments =
				le32_to_cpu(RDEV(i).total_segments);
			if (i == 0) {
				FDEV(i).start_blk = 0;
				FDEV(i).end_blk = FDEV(i).start_blk +
					SEGS_TO_BLKS(sbi,
					FDEV(i).total_segments) - 1 +
					le32_to_cpu(raw_super->segment0_blkaddr);
				sbi->allocate_section_hint = FDEV(i).total_segments /
							SEGS_PER_SEC(sbi);
			} else {
				FDEV(i).start_blk = FDEV(i - 1).end_blk + 1;
				FDEV(i).end_blk = FDEV(i).start_blk +
						SEGS_TO_BLKS(sbi,
						FDEV(i).total_segments) - 1;
				FDEV(i).bdev_file = bdev_file_open_by_path(
					FDEV(i).path, mode, sbi->sb, NULL);
			}
		}
		if (IS_ERR(FDEV(i).bdev_file))
			return PTR_ERR(FDEV(i).bdev_file);

		FDEV(i).bdev = file_bdev(FDEV(i).bdev_file);
		/* to release errored devices */
		sbi->s_ndevs = i + 1;

		if (logical_blksize != bdev_logical_block_size(FDEV(i).bdev))
			sbi->aligned_blksize = false;

#ifdef CONFIG_BLK_DEV_ZONED
		if (bdev_is_zoned(FDEV(i).bdev)) {
			if (!f2fs_sb_has_blkzoned(sbi)) {
				f2fs_err(sbi, "Zoned block device feature not enabled");
				return -EINVAL;
			}
			if (init_blkz_info(sbi, i)) {
				f2fs_err(sbi, "Failed to initialize F2FS blkzone information");
				return -EINVAL;
			}
			if (max_devices == 1)
				break;
			f2fs_info(sbi, "Mount Device [%2d]: %20s, %8u, %8x - %8x (zone: Host-managed)",
				  i, FDEV(i).path,
				  FDEV(i).total_segments,
				  FDEV(i).start_blk, FDEV(i).end_blk);
			continue;
		}
#endif
		f2fs_info(sbi, "Mount Device [%2d]: %20s, %8u, %8x - %8x",
			  i, FDEV(i).path,
			  FDEV(i).total_segments,
			  FDEV(i).start_blk, FDEV(i).end_blk);
	}
	return 0;
}

static int f2fs_setup_casefold(struct f2fs_sb_info *sbi)
{
#if IS_ENABLED(CONFIG_UNICODE)
	if (f2fs_sb_has_casefold(sbi) && !sbi->sb->s_encoding) {
		const struct f2fs_sb_encodings *encoding_info;
		struct unicode_map *encoding;
		__u16 encoding_flags;

		encoding_info = f2fs_sb_read_encoding(sbi->raw_super);
		if (!encoding_info) {
			f2fs_err(sbi,
				 "Encoding requested by superblock is unknown");
			return -EINVAL;
		}

		encoding_flags = le16_to_cpu(sbi->raw_super->s_encoding_flags);
		encoding = utf8_load(encoding_info->version);
		if (IS_ERR(encoding)) {
			f2fs_err(sbi,
				 "can't mount with superblock charset: %s-%u.%u.%u "
				 "not supported by the kernel. flags: 0x%x.",
				 encoding_info->name,
				 unicode_major(encoding_info->version),
				 unicode_minor(encoding_info->version),
				 unicode_rev(encoding_info->version),
				 encoding_flags);
			return PTR_ERR(encoding);
		}
		f2fs_info(sbi, "Using encoding defined by superblock: "
			 "%s-%u.%u.%u with flags 0x%hx", encoding_info->name,
			 unicode_major(encoding_info->version),
			 unicode_minor(encoding_info->version),
			 unicode_rev(encoding_info->version),
			 encoding_flags);

		sbi->sb->s_encoding = encoding;
		sbi->sb->s_encoding_flags = encoding_flags;
	}
#else
	if (f2fs_sb_has_casefold(sbi)) {
		f2fs_err(sbi, "Filesystem with casefold feature cannot be mounted without CONFIG_UNICODE");
		return -EINVAL;
	}
#endif
	return 0;
}

static void f2fs_tuning_parameters(struct f2fs_sb_info *sbi)
{
	/* adjust parameters according to the volume size */
	if (MAIN_SEGS(sbi) <= SMALL_VOLUME_SEGMENTS) {
		if (f2fs_block_unit_discard(sbi))
			SM_I(sbi)->dcc_info->discard_granularity =
						MIN_DISCARD_GRANULARITY;
		if (!f2fs_lfs_mode(sbi))
			SM_I(sbi)->ipu_policy = BIT(F2FS_IPU_FORCE) |
						BIT(F2FS_IPU_HONOR_OPU_WRITE);
	}

	sbi->readdir_ra = true;
}

static int f2fs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct f2fs_fs_context *ctx = fc->fs_private;
	struct f2fs_sb_info *sbi;
	struct f2fs_super_block *raw_super;
	struct inode *root;
	int err;
	bool skip_recovery = false, need_fsck = false;
	int recovery, i, valid_super_block;
	struct curseg_info *seg_i;
	int retry_cnt = 1;
#ifdef CONFIG_QUOTA
	bool quota_enabled = false;
#endif

try_onemore:
	err = -EINVAL;
	raw_super = NULL;
	valid_super_block = -1;
	recovery = 0;

	/* allocate memory for f2fs-specific super block info */
	sbi = kzalloc(sizeof(struct f2fs_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	sbi->sb = sb;

	/* initialize locks within allocated memory */
	init_f2fs_rwsem(&sbi->gc_lock);
	mutex_init(&sbi->writepages);
	init_f2fs_rwsem(&sbi->cp_global_sem);
	init_f2fs_rwsem(&sbi->node_write);
	init_f2fs_rwsem(&sbi->node_change);
	spin_lock_init(&sbi->stat_lock);
	init_f2fs_rwsem(&sbi->cp_rwsem);
	init_f2fs_rwsem(&sbi->quota_sem);
	init_waitqueue_head(&sbi->cp_wait);
	spin_lock_init(&sbi->error_lock);

	for (i = 0; i < NR_INODE_TYPE; i++) {
		INIT_LIST_HEAD(&sbi->inode_list[i]);
		spin_lock_init(&sbi->inode_lock[i]);
	}
	mutex_init(&sbi->flush_lock);

	/* set a block size */
	if (unlikely(!sb_set_blocksize(sb, F2FS_BLKSIZE))) {
		f2fs_err(sbi, "unable to set blocksize");
		goto free_sbi;
	}

	err = read_raw_super_block(sbi, &raw_super, &valid_super_block,
								&recovery);
	if (err)
		goto free_sbi;

	sb->s_fs_info = sbi;
	sbi->raw_super = raw_super;

	INIT_WORK(&sbi->s_error_work, f2fs_record_error_work);
	memcpy(sbi->errors, raw_super->s_errors, MAX_F2FS_ERRORS);
	memcpy(sbi->stop_reason, raw_super->s_stop_reason, MAX_STOP_REASON);

	/* precompute checksum seed for metadata */
	if (f2fs_sb_has_inode_chksum(sbi))
		sbi->s_chksum_seed = f2fs_chksum(~0, raw_super->uuid,
						 sizeof(raw_super->uuid));

	default_options(sbi, false);

	err = f2fs_check_opt_consistency(fc, sb);
	if (err)
		goto free_sb_buf;

	f2fs_apply_options(fc, sb);

	err = f2fs_sanity_check_options(sbi, false);
	if (err)
		goto free_options;

	sb->s_maxbytes = max_file_blocks(NULL) <<
				le32_to_cpu(raw_super->log_blocksize);
	sb->s_max_links = F2FS_LINK_MAX;

	err = f2fs_setup_casefold(sbi);
	if (err)
		goto free_options;

#ifdef CONFIG_QUOTA
	sb->dq_op = &f2fs_quota_operations;
	sb->s_qcop = &f2fs_quotactl_ops;
	sb->s_quota_types = QTYPE_MASK_USR | QTYPE_MASK_GRP | QTYPE_MASK_PRJ;

	if (f2fs_sb_has_quota_ino(sbi)) {
		for (i = 0; i < MAXQUOTAS; i++) {
			if (f2fs_qf_ino(sbi->sb, i))
				sbi->nquota_files++;
		}
	}
#endif

	sb->s_op = &f2fs_sops;
#ifdef CONFIG_FS_ENCRYPTION
	sb->s_cop = &f2fs_cryptops;
#endif
#ifdef CONFIG_FS_VERITY
	sb->s_vop = &f2fs_verityops;
#endif
	sb->s_xattr = f2fs_xattr_handlers;
	sb->s_export_op = &f2fs_export_ops;
	sb->s_magic = F2FS_SUPER_MAGIC;
	sb->s_time_gran = 1;
	sb->s_flags = (sb->s_flags & ~SB_POSIXACL) |
		(test_opt(sbi, POSIX_ACL) ? SB_POSIXACL : 0);
	if (test_opt(sbi, INLINECRYPT))
		sb->s_flags |= SB_INLINECRYPT;

	if (test_opt(sbi, LAZYTIME))
		sb->s_flags |= SB_LAZYTIME;
	else
		sb->s_flags &= ~SB_LAZYTIME;

	super_set_uuid(sb, (void *) raw_super->uuid, sizeof(raw_super->uuid));
	super_set_sysfs_name_bdev(sb);
	sb->s_iflags |= SB_I_CGROUPWB;

	/* init f2fs-specific super block info */
	sbi->valid_super_block = valid_super_block;

	/* disallow all the data/node/meta page writes */
	set_sbi_flag(sbi, SBI_POR_DOING);

	err = f2fs_init_write_merge_io(sbi);
	if (err)
		goto free_bio_info;

	init_sb_info(sbi);

	err = f2fs_init_iostat(sbi);
	if (err)
		goto free_bio_info;

	err = init_percpu_info(sbi);
	if (err)
		goto free_iostat;

	/* init per sbi slab cache */
	err = f2fs_init_xattr_caches(sbi);
	if (err)
		goto free_percpu;
	err = f2fs_init_page_array_cache(sbi);
	if (err)
		goto free_xattr_cache;

	/* get an inode for meta space */
	sbi->meta_inode = f2fs_iget(sb, F2FS_META_INO(sbi));
	if (IS_ERR(sbi->meta_inode)) {
		f2fs_err(sbi, "Failed to read F2FS meta data inode");
		err = PTR_ERR(sbi->meta_inode);
		goto free_page_array_cache;
	}

	err = f2fs_get_valid_checkpoint(sbi);
	if (err) {
		f2fs_err(sbi, "Failed to get valid F2FS checkpoint");
		goto free_meta_inode;
	}

	if (__is_set_ckpt_flags(F2FS_CKPT(sbi), CP_QUOTA_NEED_FSCK_FLAG))
		set_sbi_flag(sbi, SBI_QUOTA_NEED_REPAIR);
	if (__is_set_ckpt_flags(F2FS_CKPT(sbi), CP_DISABLED_QUICK_FLAG)) {
		set_sbi_flag(sbi, SBI_CP_DISABLED_QUICK);
		sbi->interval_time[DISABLE_TIME] = DEF_DISABLE_QUICK_INTERVAL;
	}

	if (__is_set_ckpt_flags(F2FS_CKPT(sbi), CP_FSCK_FLAG))
		set_sbi_flag(sbi, SBI_NEED_FSCK);

	/* Initialize device list */
	err = f2fs_scan_devices(sbi);
	if (err) {
		f2fs_err(sbi, "Failed to find devices");
		goto free_devices;
	}

	err = f2fs_init_post_read_wq(sbi);
	if (err) {
		f2fs_err(sbi, "Failed to initialize post read workqueue");
		goto free_devices;
	}

	sbi->total_valid_node_count =
				le32_to_cpu(sbi->ckpt->valid_node_count);
	percpu_counter_set(&sbi->total_valid_inode_count,
				le32_to_cpu(sbi->ckpt->valid_inode_count));
	sbi->user_block_count = le64_to_cpu(sbi->ckpt->user_block_count);
	sbi->total_valid_block_count =
				le64_to_cpu(sbi->ckpt->valid_block_count);
	sbi->last_valid_block_count = sbi->total_valid_block_count;
	sbi->reserved_blocks = 0;
	sbi->current_reserved_blocks = 0;
	limit_reserve_root(sbi);
	adjust_unusable_cap_perc(sbi);

	f2fs_init_extent_cache_info(sbi);

	f2fs_init_ino_entry_info(sbi);

	f2fs_init_fsync_node_info(sbi);

	/* setup checkpoint request control and start checkpoint issue thread */
	f2fs_init_ckpt_req_control(sbi);
	if (!f2fs_readonly(sb) && !test_opt(sbi, DISABLE_CHECKPOINT) &&
			test_opt(sbi, MERGE_CHECKPOINT)) {
		err = f2fs_start_ckpt_thread(sbi);
		if (err) {
			f2fs_err(sbi,
			    "Failed to start F2FS issue_checkpoint_thread (%d)",
			    err);
			goto stop_ckpt_thread;
		}
	}

	/* setup f2fs internal modules */
	err = f2fs_build_segment_manager(sbi);
	if (err) {
		f2fs_err(sbi, "Failed to initialize F2FS segment manager (%d)",
			 err);
		goto free_sm;
	}
	err = f2fs_build_node_manager(sbi);
	if (err) {
		f2fs_err(sbi, "Failed to initialize F2FS node manager (%d)",
			 err);
		goto free_nm;
	}

	/* For write statistics */
	sbi->sectors_written_start = f2fs_get_sectors_written(sbi);

	/* get segno of first zoned block device */
	sbi->first_seq_zone_segno = get_first_seq_zone_segno(sbi);

	sbi->reserved_pin_section = f2fs_sb_has_blkzoned(sbi) ?
			ZONED_PIN_SEC_REQUIRED_COUNT :
			GET_SEC_FROM_SEG(sbi, overprovision_segments(sbi));

	/* Read accumulated write IO statistics if exists */
	seg_i = CURSEG_I(sbi, CURSEG_HOT_NODE);
	if (__exist_node_summaries(sbi))
		sbi->kbytes_written =
			le64_to_cpu(seg_i->journal->info.kbytes_written);

	f2fs_build_gc_manager(sbi);

	err = f2fs_build_stats(sbi);
	if (err)
		goto free_nm;

	/* get an inode for node space */
	sbi->node_inode = f2fs_iget(sb, F2FS_NODE_INO(sbi));
	if (IS_ERR(sbi->node_inode)) {
		f2fs_err(sbi, "Failed to read node inode");
		err = PTR_ERR(sbi->node_inode);
		goto free_stats;
	}

	/* read root inode and dentry */
	root = f2fs_iget(sb, F2FS_ROOT_INO(sbi));
	if (IS_ERR(root)) {
		f2fs_err(sbi, "Failed to read root inode");
		err = PTR_ERR(root);
		goto free_node_inode;
	}
	if (!S_ISDIR(root->i_mode) || !root->i_blocks ||
			!root->i_size || !root->i_nlink) {
		iput(root);
		err = -EINVAL;
		goto free_node_inode;
	}

	generic_set_sb_d_ops(sb);
	sb->s_root = d_make_root(root); /* allocate root dentry */
	if (!sb->s_root) {
		err = -ENOMEM;
		goto free_node_inode;
	}

	err = f2fs_init_compress_inode(sbi);
	if (err)
		goto free_root_inode;

	err = f2fs_register_sysfs(sbi);
	if (err)
		goto free_compress_inode;

	sbi->umount_lock_holder = current;
#ifdef CONFIG_QUOTA
	/* Enable quota usage during mount */
	if (f2fs_sb_has_quota_ino(sbi) && !f2fs_readonly(sb)) {
		err = f2fs_enable_quotas(sb);
		if (err)
			f2fs_err(sbi, "Cannot turn on quotas: error %d", err);
	}

	quota_enabled = f2fs_recover_quota_begin(sbi);
#endif
	/* if there are any orphan inodes, free them */
	err = f2fs_recover_orphan_inodes(sbi);
	if (err)
		goto free_meta;

	if (unlikely(is_set_ckpt_flags(sbi, CP_DISABLED_FLAG))) {
		skip_recovery = true;
		goto reset_checkpoint;
	}

	/* recover fsynced data */
	if (!test_opt(sbi, DISABLE_ROLL_FORWARD) &&
			!test_opt(sbi, NORECOVERY)) {
		/*
		 * mount should be failed, when device has readonly mode, and
		 * previous checkpoint was not done by clean system shutdown.
		 */
		if (f2fs_hw_is_readonly(sbi)) {
			if (!is_set_ckpt_flags(sbi, CP_UMOUNT_FLAG)) {
				err = f2fs_recover_fsync_data(sbi, true);
				if (err > 0) {
					err = -EROFS;
					f2fs_err(sbi, "Need to recover fsync data, but "
						"write access unavailable, please try "
						"mount w/ disable_roll_forward or norecovery");
				}
				if (err < 0)
					goto free_meta;
			}
			f2fs_info(sbi, "write access unavailable, skipping recovery");
			goto reset_checkpoint;
		}

		if (need_fsck)
			set_sbi_flag(sbi, SBI_NEED_FSCK);

		if (skip_recovery)
			goto reset_checkpoint;

		err = f2fs_recover_fsync_data(sbi, false);
		if (err < 0) {
			if (err != -ENOMEM)
				skip_recovery = true;
			need_fsck = true;
			f2fs_err(sbi, "Cannot recover all fsync data errno=%d",
				 err);
			goto free_meta;
		}
	} else {
		err = f2fs_recover_fsync_data(sbi, true);

		if (!f2fs_readonly(sb) && err > 0) {
			err = -EINVAL;
			f2fs_err(sbi, "Need to recover fsync data");
			goto free_meta;
		}
	}

reset_checkpoint:
#ifdef CONFIG_QUOTA
	f2fs_recover_quota_end(sbi, quota_enabled);
#endif
	/*
	 * If the f2fs is not readonly and fsync data recovery succeeds,
	 * write pointer consistency of cursegs and other zones are already
	 * checked and fixed during recovery. However, if recovery fails,
	 * write pointers are left untouched, and retry-mount should check
	 * them here.
	 */
	if (skip_recovery)
		err = f2fs_check_and_fix_write_pointer(sbi);
	if (err)
		goto free_meta;

	/* f2fs_recover_fsync_data() cleared this already */
	clear_sbi_flag(sbi, SBI_POR_DOING);

	err = f2fs_init_inmem_curseg(sbi);
	if (err)
		goto sync_free_meta;

	if (test_opt(sbi, DISABLE_CHECKPOINT)) {
		err = f2fs_disable_checkpoint(sbi);
		if (err)
			goto sync_free_meta;
	} else if (is_set_ckpt_flags(sbi, CP_DISABLED_FLAG)) {
		f2fs_enable_checkpoint(sbi);
	}

	/*
	 * If filesystem is not mounted as read-only then
	 * do start the gc_thread.
	 */
	if ((F2FS_OPTION(sbi).bggc_mode != BGGC_MODE_OFF ||
		test_opt(sbi, GC_MERGE)) && !f2fs_readonly(sb)) {
		/* After POR, we can run background GC thread.*/
		err = f2fs_start_gc_thread(sbi);
		if (err)
			goto sync_free_meta;
	}

	/* recover broken superblock */
	if (recovery) {
		err = f2fs_commit_super(sbi, true);
		f2fs_info(sbi, "Try to recover %dth superblock, ret: %d",
			  sbi->valid_super_block ? 1 : 2, err);
	}

	f2fs_join_shrinker(sbi);

	f2fs_tuning_parameters(sbi);

	f2fs_notice(sbi, "Mounted with checkpoint version = %llx",
		    cur_cp_version(F2FS_CKPT(sbi)));
	f2fs_update_time(sbi, CP_TIME);
	f2fs_update_time(sbi, REQ_TIME);
	clear_sbi_flag(sbi, SBI_CP_DISABLED_QUICK);

	sbi->umount_lock_holder = NULL;
	return 0;

sync_free_meta:
	/* safe to flush all the data */
	sync_filesystem(sbi->sb);
	retry_cnt = 0;

free_meta:
#ifdef CONFIG_QUOTA
	f2fs_truncate_quota_inode_pages(sb);
	if (f2fs_sb_has_quota_ino(sbi) && !f2fs_readonly(sb))
		f2fs_quota_off_umount(sbi->sb);
#endif
	/*
	 * Some dirty meta pages can be produced by f2fs_recover_orphan_inodes()
	 * failed by EIO. Then, iput(node_inode) can trigger balance_fs_bg()
	 * followed by f2fs_write_checkpoint() through f2fs_write_node_pages(), which
	 * falls into an infinite loop in f2fs_sync_meta_pages().
	 */
	truncate_inode_pages_final(META_MAPPING(sbi));
	/* evict some inodes being cached by GC */
	evict_inodes(sb);
	f2fs_unregister_sysfs(sbi);
free_compress_inode:
	f2fs_destroy_compress_inode(sbi);
free_root_inode:
	dput(sb->s_root);
	sb->s_root = NULL;
free_node_inode:
	f2fs_release_ino_entry(sbi, true);
	truncate_inode_pages_final(NODE_MAPPING(sbi));
	iput(sbi->node_inode);
	sbi->node_inode = NULL;
free_stats:
	f2fs_destroy_stats(sbi);
free_nm:
	/* stop discard thread before destroying node manager */
	f2fs_stop_discard_thread(sbi);
	f2fs_destroy_node_manager(sbi);
free_sm:
	f2fs_destroy_segment_manager(sbi);
stop_ckpt_thread:
	f2fs_stop_ckpt_thread(sbi);
	/* flush s_error_work before sbi destroy */
	flush_work(&sbi->s_error_work);
	f2fs_destroy_post_read_wq(sbi);
free_devices:
	destroy_device_list(sbi);
	kvfree(sbi->ckpt);
free_meta_inode:
	make_bad_inode(sbi->meta_inode);
	iput(sbi->meta_inode);
	sbi->meta_inode = NULL;
free_page_array_cache:
	f2fs_destroy_page_array_cache(sbi);
free_xattr_cache:
	f2fs_destroy_xattr_caches(sbi);
free_percpu:
	destroy_percpu_info(sbi);
free_iostat:
	f2fs_destroy_iostat(sbi);
free_bio_info:
	for (i = 0; i < NR_PAGE_TYPE; i++)
		kfree(sbi->write_io[i]);

#if IS_ENABLED(CONFIG_UNICODE)
	utf8_unload(sb->s_encoding);
	sb->s_encoding = NULL;
#endif
free_options:
#ifdef CONFIG_QUOTA
	for (i = 0; i < MAXQUOTAS; i++)
		kfree(F2FS_OPTION(sbi).s_qf_names[i]);
#endif
	/* no need to free dummy_enc_policy, we just keep it in ctx when failed */
	swap(F2FS_CTX_INFO(ctx).dummy_enc_policy, F2FS_OPTION(sbi).dummy_enc_policy);
free_sb_buf:
	kfree(raw_super);
free_sbi:
	kfree(sbi);
	sb->s_fs_info = NULL;

	/* give only one another chance */
	if (retry_cnt > 0 && skip_recovery) {
		retry_cnt--;
		shrink_dcache_sb(sb);
		goto try_onemore;
	}
	return err;
}

static int f2fs_get_tree(struct fs_context *fc)
{
	return get_tree_bdev(fc, f2fs_fill_super);
}

static int f2fs_reconfigure(struct fs_context *fc)
{
	struct super_block *sb = fc->root->d_sb;

	return __f2fs_remount(fc, sb);
}

static void f2fs_fc_free(struct fs_context *fc)
{
	struct f2fs_fs_context *ctx = fc->fs_private;

	if (!ctx)
		return;

#ifdef CONFIG_QUOTA
	f2fs_unnote_qf_name_all(fc);
#endif
	fscrypt_free_dummy_policy(&F2FS_CTX_INFO(ctx).dummy_enc_policy);
	kfree(ctx);
}

static const struct fs_context_operations f2fs_context_ops = {
	.parse_param	= f2fs_parse_param,
	.get_tree	= f2fs_get_tree,
	.reconfigure = f2fs_reconfigure,
	.free	= f2fs_fc_free,
};

static void kill_f2fs_super(struct super_block *sb)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);

	if (sb->s_root) {
		sbi->umount_lock_holder = current;

		set_sbi_flag(sbi, SBI_IS_CLOSE);
		f2fs_stop_gc_thread(sbi);
		f2fs_stop_discard_thread(sbi);

#ifdef CONFIG_F2FS_FS_COMPRESSION
		/*
		 * latter evict_inode() can bypass checking and invalidating
		 * compress inode cache.
		 */
		if (test_opt(sbi, COMPRESS_CACHE))
			truncate_inode_pages_final(COMPRESS_MAPPING(sbi));
#endif

		if (is_sbi_flag_set(sbi, SBI_IS_DIRTY) ||
				!is_set_ckpt_flags(sbi, CP_UMOUNT_FLAG)) {
			struct cp_control cpc = {
				.reason = CP_UMOUNT,
			};
			stat_inc_cp_call_count(sbi, TOTAL_CALL);
			f2fs_write_checkpoint(sbi, &cpc);
		}

		if (is_sbi_flag_set(sbi, SBI_IS_RECOVERED) && f2fs_readonly(sb))
			sb->s_flags &= ~SB_RDONLY;
	}
	kill_block_super(sb);
	/* Release block devices last, after fscrypt_destroy_keyring(). */
	if (sbi) {
		destroy_device_list(sbi);
		kfree(sbi);
		sb->s_fs_info = NULL;
	}
}

static int f2fs_init_fs_context(struct fs_context *fc)
{
	struct f2fs_fs_context *ctx;

	ctx = kzalloc(sizeof(struct f2fs_fs_context), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	fc->fs_private = ctx;
	fc->ops = &f2fs_context_ops;

	return 0;
}

static struct file_system_type f2fs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "f2fs",
	.init_fs_context = f2fs_init_fs_context,
	.kill_sb	= kill_f2fs_super,
	.fs_flags	= FS_REQUIRES_DEV | FS_ALLOW_IDMAP,
};
MODULE_ALIAS_FS("f2fs");

static int __init init_inodecache(void)
{
	f2fs_inode_cachep = kmem_cache_create("f2fs_inode_cache",
			sizeof(struct f2fs_inode_info), 0,
			SLAB_RECLAIM_ACCOUNT|SLAB_ACCOUNT, NULL);
	return f2fs_inode_cachep ? 0 : -ENOMEM;
}

static void destroy_inodecache(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(f2fs_inode_cachep);
}

static int __init init_f2fs_fs(void)
{
	int err;

	err = init_inodecache();
	if (err)
		goto fail;
	err = f2fs_create_node_manager_caches();
	if (err)
		goto free_inodecache;
	err = f2fs_create_segment_manager_caches();
	if (err)
		goto free_node_manager_caches;
	err = f2fs_create_checkpoint_caches();
	if (err)
		goto free_segment_manager_caches;
	err = f2fs_create_recovery_cache();
	if (err)
		goto free_checkpoint_caches;
	err = f2fs_create_extent_cache();
	if (err)
		goto free_recovery_cache;
	err = f2fs_create_garbage_collection_cache();
	if (err)
		goto free_extent_cache;
	err = f2fs_init_sysfs();
	if (err)
		goto free_garbage_collection_cache;
	err = f2fs_init_shrinker();
	if (err)
		goto free_sysfs;
	f2fs_create_root_stats();
	err = f2fs_init_post_read_processing();
	if (err)
		goto free_root_stats;
	err = f2fs_init_iostat_processing();
	if (err)
		goto free_post_read;
	err = f2fs_init_bio_entry_cache();
	if (err)
		goto free_iostat;
	err = f2fs_init_bioset();
	if (err)
		goto free_bio_entry_cache;
	err = f2fs_init_compress_mempool();
	if (err)
		goto free_bioset;
	err = f2fs_init_compress_cache();
	if (err)
		goto free_compress_mempool;
	err = f2fs_create_casefold_cache();
	if (err)
		goto free_compress_cache;
	err = register_filesystem(&f2fs_fs_type);
	if (err)
		goto free_casefold_cache;
	return 0;
free_casefold_cache:
	f2fs_destroy_casefold_cache();
free_compress_cache:
	f2fs_destroy_compress_cache();
free_compress_mempool:
	f2fs_destroy_compress_mempool();
free_bioset:
	f2fs_destroy_bioset();
free_bio_entry_cache:
	f2fs_destroy_bio_entry_cache();
free_iostat:
	f2fs_destroy_iostat_processing();
free_post_read:
	f2fs_destroy_post_read_processing();
free_root_stats:
	f2fs_destroy_root_stats();
	f2fs_exit_shrinker();
free_sysfs:
	f2fs_exit_sysfs();
free_garbage_collection_cache:
	f2fs_destroy_garbage_collection_cache();
free_extent_cache:
	f2fs_destroy_extent_cache();
free_recovery_cache:
	f2fs_destroy_recovery_cache();
free_checkpoint_caches:
	f2fs_destroy_checkpoint_caches();
free_segment_manager_caches:
	f2fs_destroy_segment_manager_caches();
free_node_manager_caches:
	f2fs_destroy_node_manager_caches();
free_inodecache:
	destroy_inodecache();
fail:
	return err;
}

static void __exit exit_f2fs_fs(void)
{
	unregister_filesystem(&f2fs_fs_type);
	f2fs_destroy_casefold_cache();
	f2fs_destroy_compress_cache();
	f2fs_destroy_compress_mempool();
	f2fs_destroy_bioset();
	f2fs_destroy_bio_entry_cache();
	f2fs_destroy_iostat_processing();
	f2fs_destroy_post_read_processing();
	f2fs_destroy_root_stats();
	f2fs_exit_shrinker();
	f2fs_exit_sysfs();
	f2fs_destroy_garbage_collection_cache();
	f2fs_destroy_extent_cache();
	f2fs_destroy_recovery_cache();
	f2fs_destroy_checkpoint_caches();
	f2fs_destroy_segment_manager_caches();
	f2fs_destroy_node_manager_caches();
	destroy_inodecache();
}

module_init(init_f2fs_fs)
module_exit(exit_f2fs_fs)

MODULE_AUTHOR("Samsung Electronics's Praesto Team");
MODULE_DESCRIPTION("Flash Friendly File System");
MODULE_LICENSE("GPL");
