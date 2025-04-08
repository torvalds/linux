// SPDX-License-Identifier: GPL-2.0

#include <linux/kernel.h>
#include <linux/fs_parser.h>

#include "bcachefs.h"
#include "compress.h"
#include "disk_groups.h"
#include "error.h"
#include "opts.h"
#include "recovery_passes.h"
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

const char * const __bch2_csum_types[] = {
	BCH_CSUM_TYPES()
	NULL
};

const char * const __bch2_csum_opts[] = {
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

const char * const __bch2_str_hash_types[] = {
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

static const char * const __bch2_jset_entry_types[] = {
	BCH_JSET_ENTRY_TYPES()
	NULL
};

static const char * const __bch2_fs_usage_types[] = {
	BCH_FS_USAGE_TYPES()
	NULL
};

#undef x

static void prt_str_opt_boundscheck(struct printbuf *out, const char * const opts[],
				    unsigned nr, const char *type, unsigned idx)
{
	if (idx < nr)
		prt_str(out, opts[idx]);
	else
		prt_printf(out, "(unknown %s %u)", type, idx);
}

#define PRT_STR_OPT_BOUNDSCHECKED(name, type)					\
void bch2_prt_##name(struct printbuf *out, type t)				\
{										\
	prt_str_opt_boundscheck(out, __bch2_##name##s, ARRAY_SIZE(__bch2_##name##s) - 1, #name, t);\
}

PRT_STR_OPT_BOUNDSCHECKED(jset_entry_type,	enum bch_jset_entry_type);
PRT_STR_OPT_BOUNDSCHECKED(fs_usage_type,	enum bch_fs_usage_type);
PRT_STR_OPT_BOUNDSCHECKED(data_type,		enum bch_data_type);
PRT_STR_OPT_BOUNDSCHECKED(csum_opt,		enum bch_csum_opt);
PRT_STR_OPT_BOUNDSCHECKED(csum_type,		enum bch_csum_type);
PRT_STR_OPT_BOUNDSCHECKED(compression_type,	enum bch_compression_type);
PRT_STR_OPT_BOUNDSCHECKED(str_hash_type,	enum bch_str_hash_type);

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

/* dummy option, for options that aren't stored in the superblock */
typedef u64 (*sb_opt_get_fn)(const struct bch_sb *);
typedef void (*sb_opt_set_fn)(struct bch_sb *, u64);
typedef u64 (*member_opt_get_fn)(const struct bch_member *);
typedef void (*member_opt_set_fn)(struct bch_member *, u64);

__maybe_unused static const sb_opt_get_fn	BCH2_NO_SB_OPT = NULL;
__maybe_unused static const sb_opt_set_fn	SET_BCH2_NO_SB_OPT = NULL;
__maybe_unused static const member_opt_get_fn	BCH2_NO_MEMBER_OPT = NULL;
__maybe_unused static const member_opt_set_fn	SET_BCH2_NO_MEMBER_OPT = NULL;

#define type_compatible_or_null(_p, _type)				\
	__builtin_choose_expr(						\
		__builtin_types_compatible_p(typeof(_p), typeof(_type)), _p, NULL)

const struct bch_option bch2_opt_table[] = {
#define OPT_BOOL()		.type = BCH_OPT_BOOL, .min = 0, .max = 2
#define OPT_UINT(_min, _max)	.type = BCH_OPT_UINT,			\
				.min = _min, .max = _max
#define OPT_STR(_choices)	.type = BCH_OPT_STR,			\
				.min = 0, .max = ARRAY_SIZE(_choices) - 1, \
				.choices = _choices
#define OPT_STR_NOLIMIT(_choices)	.type = BCH_OPT_STR,		\
				.min = 0, .max = U64_MAX,		\
				.choices = _choices
#define OPT_BITFIELD(_choices)	.type = BCH_OPT_BITFIELD,		\
				.choices = _choices
#define OPT_FN(_fn)		.type = BCH_OPT_FN, .fn	= _fn

#define x(_name, _bits, _flags, _type, _sb_opt, _default, _hint, _help)	\
	[Opt_##_name] = {						\
		.attr.name	= #_name,				\
		.attr.mode	= (_flags) & OPT_RUNTIME ? 0644 : 0444,	\
		.flags		= _flags,				\
		.hint		= _hint,				\
		.help		= _help,				\
		.get_sb		= type_compatible_or_null(_sb_opt,	*BCH2_NO_SB_OPT),	\
		.set_sb		= type_compatible_or_null(SET_##_sb_opt,*SET_BCH2_NO_SB_OPT),	\
		.get_member	= type_compatible_or_null(_sb_opt,	*BCH2_NO_MEMBER_OPT),	\
		.set_member	= type_compatible_or_null(SET_##_sb_opt,*SET_BCH2_NO_MEMBER_OPT),\
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
			ret = lookup_constant(bool_names, val, -BCH_ERR_option_not_bool);
			if (ret != -BCH_ERR_option_not_bool) {
				*res = ret;
			} else {
				if (err)
					prt_printf(err, "%s: must be bool", opt->attr.name);
				return ret;
			}
		} else {
			*res = 1;
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
	case BCH_OPT_BITFIELD: {
		s64 v = bch2_read_flag_list(val, opt->choices);
		if (v < 0)
			return v;
		*res = v;
		break;
	}
	case BCH_OPT_FN:
		ret = opt->fn.parse(c, val, res, err);

		if (ret == -BCH_ERR_option_needs_open_fs)
			return ret;

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
		if (v < opt->min || v >= opt->max)
			prt_printf(out, "(invalid option %lli)", v);
		else if (flags & OPT_SHOW_FULL_LIST)
			prt_string_option(out, opt->choices, v);
		else
			prt_str(out, opt->choices[v]);
		break;
	case BCH_OPT_BITFIELD:
		prt_bitflags(out, opt->choices, v);
		break;
	case BCH_OPT_FN:
		opt->fn.to_text(out, c, sb, v);
		break;
	default:
		BUG();
	}
}

void bch2_opts_to_text(struct printbuf *out,
		       struct bch_opts opts,
		       struct bch_fs *c, struct bch_sb *sb,
		       unsigned show_mask, unsigned hide_mask,
		       unsigned flags)
{
	bool first = true;

	for (enum bch_opt_id i = 0; i < bch2_opts_nr; i++) {
		const struct bch_option *opt = &bch2_opt_table[i];

		if ((opt->flags & hide_mask) || !(opt->flags & show_mask))
			continue;

		u64 v = bch2_opt_get_by_id(&opts, i);
		if (v == bch2_opt_get_by_id(&bch2_opts_default, i))
			continue;

		if (!first)
			prt_char(out, ',');
		first = false;

		bch2_opt_to_text(out, c, sb, opt, v, flags);
	}
}

int bch2_opt_check_may_set(struct bch_fs *c, struct bch_dev *ca, int id, u64 v)
{
	int ret = 0;

	switch (id) {
	case Opt_state:
		if (ca)
			return bch2_dev_set_state(c, ca, v, BCH_FORCE_IF_DEGRADED);
		break;

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
	for (unsigned i = 0; i < bch2_opts_nr; i++) {
		int ret = bch2_opt_check_may_set(c, NULL, i, bch2_opt_get_by_id(&c->opts, i));
		if (ret)
			return ret;
	}

	return 0;
}

int bch2_parse_one_mount_opt(struct bch_fs *c, struct bch_opts *opts,
			     struct printbuf *parse_later,
			     const char *name, const char *val)
{
	struct printbuf err = PRINTBUF;
	u64 v;
	int ret, id;

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
		return 0;

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
	if (ret == -BCH_ERR_option_needs_open_fs) {
		ret = 0;

		if (parse_later) {
			prt_printf(parse_later, "%s=%s,", name, val);
			if (parse_later->allocation_failure)
				ret = -ENOMEM;
		}

		goto out;
	}

	if (ret < 0)
		goto bad_val;

	if (opts)
		bch2_opt_set_by_id(opts, id, v);

	ret = 0;
out:
	printbuf_exit(&err);
	return ret;
bad_opt:
	ret = -BCH_ERR_option_name;
	goto out;
bad_val:
	ret = -BCH_ERR_option_value;
	goto out;
}

int bch2_parse_mount_opts(struct bch_fs *c, struct bch_opts *opts,
			  struct printbuf *parse_later, char *options,
			  bool ignore_unknown)
{
	char *copied_opts, *copied_opts_start;
	char *opt, *name, *val;
	int ret = 0;

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
		return -ENOMEM;
	copied_opts_start = copied_opts;

	while ((opt = strsep(&copied_opts, ",")) != NULL) {
		if (!*opt)
			continue;

		name	= strsep(&opt, "=");
		val	= opt;

		ret = bch2_parse_one_mount_opt(c, opts, parse_later, name, val);
		if (ret == -BCH_ERR_option_name && ignore_unknown)
			ret = 0;
		if (ret) {
			pr_err("Error parsing option %s: %s", name, bch2_err_str(ret));
			break;
		}
	}

	kfree(copied_opts_start);
	return ret;
}

u64 bch2_opt_from_sb(struct bch_sb *sb, enum bch_opt_id id, int dev_idx)
{
	const struct bch_option *opt = bch2_opt_table + id;
	u64 v;

	if (dev_idx < 0) {
		v = opt->get_sb(sb);
	} else {
		if (WARN(!bch2_member_exists(sb, dev_idx),
			 "tried to set device option %s on nonexistent device %i",
			 opt->attr.name, dev_idx))
			return 0;

		struct bch_member m = bch2_sb_member_get(sb, dev_idx);
		v = opt->get_member(&m);
	}

	if (opt->flags & OPT_SB_FIELD_ONE_BIAS)
		--v;

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
	for (unsigned id = 0; id < bch2_opts_nr; id++) {
		const struct bch_option *opt = bch2_opt_table + id;

		if (opt->get_sb)
			bch2_opt_set_by_id(opts, id, bch2_opt_from_sb(sb, id, -1));
	}

	return 0;
}

void __bch2_opt_set_sb(struct bch_sb *sb, int dev_idx,
		       const struct bch_option *opt, u64 v)
{
	if (opt->flags & OPT_SB_FIELD_SECTORS)
		v >>= 9;

	if (opt->flags & OPT_SB_FIELD_ILOG2)
		v = ilog2(v);

	if (opt->flags & OPT_SB_FIELD_ONE_BIAS)
		v++;

	if ((opt->flags & OPT_FS) && opt->set_sb && dev_idx < 0)
		opt->set_sb(sb, v);

	if ((opt->flags & OPT_DEVICE) && opt->set_member && dev_idx >= 0) {
		if (WARN(!bch2_member_exists(sb, dev_idx),
			 "tried to set device option %s on nonexistent device %i",
			 opt->attr.name, dev_idx))
			return;

		opt->set_member(bch2_members_v2_get_mut(sb, dev_idx), v);
	}
}

void bch2_opt_set_sb(struct bch_fs *c, struct bch_dev *ca,
		     const struct bch_option *opt, u64 v)
{
	mutex_lock(&c->sb_lock);
	__bch2_opt_set_sb(c->disk_sb.sb, ca ? ca->dev_idx : -1, opt, v);
	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);
}

/* io opts: */

struct bch_io_opts bch2_opts_to_inode_opts(struct bch_opts src)
{
	struct bch_io_opts opts = {
#define x(_name, _bits)	._name = src._name,
	BCH_INODE_OPTS()
#undef x
	};

	bch2_io_opts_fixups(&opts);
	return opts;
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
