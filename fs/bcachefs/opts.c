// SPDX-License-Identifier: GPL-2.0

#include <linux/kernel.h>

#include "bcachefs.h"
#include "compress.h"
#include "disk_groups.h"
#include "error.h"
#include "opts.h"
#include "super-io.h"
#include "util.h"

#define x(t, n, ...) [n] = #t,

const char * const bch2_error_actions[] = {
	BCH_ERROR_ACTIONS()
	NULL
};

const char * const bch2_fsck_fix_opts[] = {
	BCH_FIX_ERRORS_OPTS()
	NULL
};

const char * const bch2_version_upgrade_opts[] = {
	BCH_VERSION_UPGRADE_OPTS()
	NULL
};

const char * const bch2_sb_features[] = {
	BCH_SB_FEATURES()
	NULL
};

const char * const bch2_sb_compat[] = {
	BCH_SB_COMPAT()
	NULL
};

const char * const __bch2_btree_ids[] = {
	BCH_BTREE_IDS()
	NULL
};

const char * const bch2_csum_types[] = {
	BCH_CSUM_TYPES()
	NULL
};

const char * const bch2_csum_opts[] = {
	BCH_CSUM_OPTS()
	NULL
};

const char * const __bch2_compression_types[] = {
	BCH_COMPRESSION_TYPES()
	NULL
};

const char * const bch2_compression_opts[] = {
	BCH_COMPRESSION_OPTS()
	NULL
};

const char * const bch2_str_hash_types[] = {
	BCH_STR_HASH_TYPES()
	NULL
};

const char * const bch2_str_hash_opts[] = {
	BCH_STR_HASH_OPTS()
	NULL
};

const char * const __bch2_data_types[] = {
	BCH_DATA_TYPES()
	NULL
};

const char * const bch2_member_states[] = {
	BCH_MEMBER_STATES()
	NULL
};

const char * const bch2_jset_entry_types[] = {
	BCH_JSET_ENTRY_TYPES()
	NULL
};

const char * const bch2_fs_usage_types[] = {
	BCH_FS_USAGE_TYPES()
	NULL
};

#undef x

static int bch2_opt_fix_errors_parse(struct bch_fs *c, const char *val, u64 *res,
				     struct printbuf *err)
{
	if (!val) {
		*res = FSCK_FIX_yes;
	} else {
		int ret = match_string(bch2_fsck_fix_opts, -1, val);

		if (ret < 0 && err)
			prt_str(err, "fix_errors: invalid selection");
		if (ret < 0)
			return ret;
		*res = ret;
	}

	return 0;
}

static void bch2_opt_fix_errors_to_text(struct printbuf *out,
					struct bch_fs *c,
					struct bch_sb *sb,
					u64 v)
{
	prt_str(out, bch2_fsck_fix_opts[v]);
}

#define bch2_opt_fix_errors (struct bch_opt_fn) {	\
	.parse = bch2_opt_fix_errors_parse,		\
	.to_text = bch2_opt_fix_errors_to_text,		\
}

const char * const bch2_d_types[BCH_DT_MAX] = {
	[DT_UNKNOWN]	= "unknown",
	[DT_FIFO]	= "fifo",
	[DT_CHR]	= "chr",
	[DT_DIR]	= "dir",
	[DT_BLK]	= "blk",
	[DT_REG]	= "reg",
	[DT_LNK]	= "lnk",
	[DT_SOCK]	= "sock",
	[DT_WHT]	= "whiteout",
	[DT_SUBVOL]	= "subvol",
};

u64 BCH2_NO_SB_OPT(const struct bch_sb *sb)
{
	BUG();
}

void SET_BCH2_NO_SB_OPT(struct bch_sb *sb, u64 v)
{
	BUG();
}

void bch2_opts_apply(struct bch_opts *dst, struct bch_opts src)
{
#define x(_name, ...)						\
	if (opt_defined(src, _name))					\
		opt_set(*dst, _name, src._name);

	BCH_OPTS()
#undef x
}

bool bch2_opt_defined_by_id(const struct bch_opts *opts, enum bch_opt_id id)
{
	switch (id) {
#define x(_name, ...)						\
	case Opt_##_name:						\
		return opt_defined(*opts, _name);
	BCH_OPTS()
#undef x
	default:
		BUG();
	}
}

u64 bch2_opt_get_by_id(const struct bch_opts *opts, enum bch_opt_id id)
{
	switch (id) {
#define x(_name, ...)						\
	case Opt_##_name:						\
		return opts->_name;
	BCH_OPTS()
#undef x
	default:
		BUG();
	}
}

void bch2_opt_set_by_id(struct bch_opts *opts, enum bch_opt_id id, u64 v)
{
	switch (id) {
#define x(_name, ...)						\
	case Opt_##_name:						\
		opt_set(*opts, _name, v);				\
		break;
	BCH_OPTS()
#undef x
	default:
		BUG();
	}
}

const struct bch_option bch2_opt_table[] = {
#define OPT_BOOL()		.type = BCH_OPT_BOOL, .min = 0, .max = 2
#define OPT_UINT(_min, _max)	.type = BCH_OPT_UINT,			\
				.min = _min, .max = _max
#define OPT_STR(_choices)	.type = BCH_OPT_STR,			\
				.min = 0, .max = ARRAY_SIZE(_choices),	\
				.choices = _choices
#define OPT_FN(_fn)		.type = BCH_OPT_FN, .fn	= _fn

#define x(_name, _bits, _flags, _type, _sb_opt, _default, _hint, _help)	\
	[Opt_##_name] = {						\
		.attr	= {						\
			.name	= #_name,				\
			.mode = (_flags) & OPT_RUNTIME ? 0644 : 0444,	\
		},							\
		.flags	= _flags,					\
		.hint	= _hint,					\
		.help	= _help,					\
		.get_sb = _sb_opt,					\
		.set_sb	= SET_##_sb_opt,				\
		_type							\
	},

	BCH_OPTS()
#undef x
};

int bch2_opt_lookup(const char *name)
{
	const struct bch_option *i;

	for (i = bch2_opt_table;
	     i < bch2_opt_table + ARRAY_SIZE(bch2_opt_table);
	     i++)
		if (!strcmp(name, i->attr.name))
			return i - bch2_opt_table;

	return -1;
}

struct synonym {
	const char	*s1, *s2;
};

static const struct synonym bch_opt_synonyms[] = {
	{ "quota",	"usrquota" },
};

static int bch2_mount_opt_lookup(const char *name)
{
	const struct synonym *i;

	for (i = bch_opt_synonyms;
	     i < bch_opt_synonyms + ARRAY_SIZE(bch_opt_synonyms);
	     i++)
		if (!strcmp(name, i->s1))
			name = i->s2;

	return bch2_opt_lookup(name);
}

int bch2_opt_validate(const struct bch_option *opt, u64 v, struct printbuf *err)
{
	if (v < opt->min) {
		if (err)
			prt_printf(err, "%s: too small (min %llu)",
			       opt->attr.name, opt->min);
		return -BCH_ERR_ERANGE_option_too_small;
	}

	if (opt->max && v >= opt->max) {
		if (err)
			prt_printf(err, "%s: too big (max %llu)",
			       opt->attr.name, opt->max);
		return -BCH_ERR_ERANGE_option_too_big;
	}

	if ((opt->flags & OPT_SB_FIELD_SECTORS) && (v & 511)) {
		if (err)
			prt_printf(err, "%s: not a multiple of 512",
			       opt->attr.name);
		return -BCH_ERR_opt_parse_error;
	}

	if ((opt->flags & OPT_MUST_BE_POW_2) && !is_power_of_2(v)) {
		if (err)
			prt_printf(err, "%s: must be a power of two",
			       opt->attr.name);
		return -BCH_ERR_opt_parse_error;
	}

	if (opt->fn.validate)
		return opt->fn.validate(v, err);

	return 0;
}

int bch2_opt_parse(struct bch_fs *c,
		   const struct bch_option *opt,
		   const char *val, u64 *res,
		   struct printbuf *err)
{
	ssize_t ret;

	switch (opt->type) {
	case BCH_OPT_BOOL:
		if (val) {
			ret = kstrtou64(val, 10, res);
		} else {
			ret = 0;
			*res = 1;
		}

		if (ret < 0 || (*res != 0 && *res != 1)) {
			if (err)
				prt_printf(err, "%s: must be bool", opt->attr.name);
			return ret;
		}
		break;
	case BCH_OPT_UINT:
		if (!val) {
			prt_printf(err, "%s: required value",
				   opt->attr.name);
			return -EINVAL;
		}

		ret = opt->flags & OPT_HUMAN_READABLE
			? bch2_strtou64_h(val, res)
			: kstrtou64(val, 10, res);
		if (ret < 0) {
			if (err)
				prt_printf(err, "%s: must be a number",
					   opt->attr.name);
			return ret;
		}
		break;
	case BCH_OPT_STR:
		if (!val) {
			prt_printf(err, "%s: required value",
				   opt->attr.name);
			return -EINVAL;
		}

		ret = match_string(opt->choices, -1, val);
		if (ret < 0) {
			if (err)
				prt_printf(err, "%s: invalid selection",
					   opt->attr.name);
			return ret;
		}

		*res = ret;
		break;
	case BCH_OPT_FN:
		ret = opt->fn.parse(c, val, res, err);
		if (ret < 0) {
			if (err)
				prt_printf(err, "%s: parse error",
					   opt->attr.name);
			return ret;
		}
	}

	return bch2_opt_validate(opt, *res, err);
}

void bch2_opt_to_text(struct printbuf *out,
		      struct bch_fs *c, struct bch_sb *sb,
		      const struct bch_option *opt, u64 v,
		      unsigned flags)
{
	if (flags & OPT_SHOW_MOUNT_STYLE) {
		if (opt->type == BCH_OPT_BOOL) {
			prt_printf(out, "%s%s",
			       v ? "" : "no",
			       opt->attr.name);
			return;
		}

		prt_printf(out, "%s=", opt->attr.name);
	}

	switch (opt->type) {
	case BCH_OPT_BOOL:
	case BCH_OPT_UINT:
		if (opt->flags & OPT_HUMAN_READABLE)
			prt_human_readable_u64(out, v);
		else
			prt_printf(out, "%lli", v);
		break;
	case BCH_OPT_STR:
		if (flags & OPT_SHOW_FULL_LIST)
			prt_string_option(out, opt->choices, v);
		else
			prt_str(out, opt->choices[v]);
		break;
	case BCH_OPT_FN:
		opt->fn.to_text(out, c, sb, v);
		break;
	default:
		BUG();
	}
}

int bch2_opt_check_may_set(struct bch_fs *c, int id, u64 v)
{
	int ret = 0;

	switch (id) {
	case Opt_compression:
	case Opt_background_compression:
		ret = bch2_check_set_has_compressed_data(c, v);
		break;
	case Opt_erasure_code:
		if (v)
			bch2_check_set_feature(c, BCH_FEATURE_ec);
		break;
	}

	return ret;
}

int bch2_opts_check_may_set(struct bch_fs *c)
{
	unsigned i;
	int ret;

	for (i = 0; i < bch2_opts_nr; i++) {
		ret = bch2_opt_check_may_set(c, i,
				bch2_opt_get_by_id(&c->opts, i));
		if (ret)
			return ret;
	}

	return 0;
}

int bch2_parse_mount_opts(struct bch_fs *c, struct bch_opts *opts,
			  char *options)
{
	char *copied_opts, *copied_opts_start;
	char *opt, *name, *val;
	int ret, id;
	struct printbuf err = PRINTBUF;
	u64 v;

	if (!options)
		return 0;

	/*
	 * sys_fsconfig() is now occasionally providing us with option lists
	 * starting with a comma - weird.
	 */
	if (*options == ',')
		options++;

	copied_opts = kstrdup(options, GFP_KERNEL);
	if (!copied_opts)
		return -1;
	copied_opts_start = copied_opts;

	while ((opt = strsep(&copied_opts, ",")) != NULL) {
		name	= strsep(&opt, "=");
		val	= opt;

		id = bch2_mount_opt_lookup(name);

		/* Check for the form "noopt", negation of a boolean opt: */
		if (id < 0 &&
		    !val &&
		    !strncmp("no", name, 2)) {
			id = bch2_mount_opt_lookup(name + 2);
			val = "0";
		}

		/* Unknown options are ignored: */
		if (id < 0)
			continue;

		if (!(bch2_opt_table[id].flags & OPT_MOUNT))
			goto bad_opt;

		if (id == Opt_acl &&
		    !IS_ENABLED(CONFIG_BCACHEFS_POSIX_ACL))
			goto bad_opt;

		if ((id == Opt_usrquota ||
		     id == Opt_grpquota) &&
		    !IS_ENABLED(CONFIG_BCACHEFS_QUOTA))
			goto bad_opt;

		ret = bch2_opt_parse(c, &bch2_opt_table[id], val, &v, &err);
		if (ret < 0)
			goto bad_val;

		bch2_opt_set_by_id(opts, id, v);
	}

	ret = 0;
	goto out;

bad_opt:
	pr_err("Bad mount option %s", name);
	ret = -1;
	goto out;
bad_val:
	pr_err("Invalid mount option %s", err.buf);
	ret = -1;
	goto out;
out:
	kfree(copied_opts_start);
	printbuf_exit(&err);
	return ret;
}

u64 bch2_opt_from_sb(struct bch_sb *sb, enum bch_opt_id id)
{
	const struct bch_option *opt = bch2_opt_table + id;
	u64 v;

	v = opt->get_sb(sb);

	if (opt->flags & OPT_SB_FIELD_ILOG2)
		v = 1ULL << v;

	if (opt->flags & OPT_SB_FIELD_SECTORS)
		v <<= 9;

	return v;
}

/*
 * Initial options from superblock - here we don't want any options undefined,
 * any options the superblock doesn't specify are set to 0:
 */
int bch2_opts_from_sb(struct bch_opts *opts, struct bch_sb *sb)
{
	unsigned id;

	for (id = 0; id < bch2_opts_nr; id++) {
		const struct bch_option *opt = bch2_opt_table + id;

		if (opt->get_sb == BCH2_NO_SB_OPT)
			continue;

		bch2_opt_set_by_id(opts, id, bch2_opt_from_sb(sb, id));
	}

	return 0;
}

void __bch2_opt_set_sb(struct bch_sb *sb, const struct bch_option *opt, u64 v)
{
	if (opt->set_sb == SET_BCH2_NO_SB_OPT)
		return;

	if (opt->flags & OPT_SB_FIELD_SECTORS)
		v >>= 9;

	if (opt->flags & OPT_SB_FIELD_ILOG2)
		v = ilog2(v);

	opt->set_sb(sb, v);
}

void bch2_opt_set_sb(struct bch_fs *c, const struct bch_option *opt, u64 v)
{
	if (opt->set_sb == SET_BCH2_NO_SB_OPT)
		return;

	mutex_lock(&c->sb_lock);
	__bch2_opt_set_sb(c->disk_sb.sb, opt, v);
	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);
}

/* io opts: */

struct bch_io_opts bch2_opts_to_inode_opts(struct bch_opts src)
{
	return (struct bch_io_opts) {
#define x(_name, _bits)	._name = src._name,
	BCH_INODE_OPTS()
#undef x
	};
}

bool bch2_opt_is_inode_opt(enum bch_opt_id id)
{
	static const enum bch_opt_id inode_opt_list[] = {
#define x(_name, _bits)	Opt_##_name,
	BCH_INODE_OPTS()
#undef x
	};
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(inode_opt_list); i++)
		if (inode_opt_list[i] == id)
			return true;

	return false;
}
