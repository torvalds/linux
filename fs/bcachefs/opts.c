// SPDX-License-Identifier: GPL-2.0

#include <linux/kernel.h>

#include "bcachefs.h"
#include "compress.h"
#include "disk_groups.h"
#include "opts.h"
#include "super-io.h"
#include "util.h"

#define x(t, n) #t,

const char * const bch2_error_actions[] = {
	BCH_ERROR_ACTIONS()
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

const char * const bch2_btree_ids[] = {
	BCH_BTREE_IDS()
	NULL
};

const char * const bch2_csum_opts[] = {
	BCH_CSUM_OPTS()
	NULL
};

const char * const bch2_compression_opts[] = {
	BCH_COMPRESSION_OPTS()
	NULL
};

const char * const bch2_str_hash_types[] = {
	BCH_STR_HASH_OPTS()
	NULL
};

const char * const bch2_data_types[] = {
	BCH_DATA_TYPES()
	NULL
};

const char * const bch2_cache_replacement_policies[] = {
	BCH_CACHE_REPLACEMENT_POLICIES()
	NULL
};

const char * const bch2_member_states[] = {
	BCH_MEMBER_STATES()
	NULL
};

#undef x

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

/*
 * Initial options from superblock - here we don't want any options undefined,
 * any options the superblock doesn't specify are set to 0:
 */
struct bch_opts bch2_opts_from_sb(struct bch_sb *sb)
{
	struct bch_opts opts = bch2_opts_empty();

#define x(_name, _bits, _mode, _type, _sb_opt, ...)			\
	if (_sb_opt != NO_SB_OPT)					\
		opt_set(opts, _name, _sb_opt(sb));
	BCH_OPTS()
#undef x

	return opts;
}

const struct bch_option bch2_opt_table[] = {
#define OPT_BOOL()		.type = BCH_OPT_BOOL
#define OPT_UINT(_min, _max)	.type = BCH_OPT_UINT, .min = _min, .max = _max
#define OPT_SECTORS(_min, _max)	.type = BCH_OPT_SECTORS, .min = _min, .max = _max
#define OPT_STR(_choices)	.type = BCH_OPT_STR, .choices = _choices
#define OPT_FN(_fn)		.type = BCH_OPT_FN,			\
				.parse = _fn##_parse,			\
				.to_text = _fn##_to_text

#define x(_name, _bits, _mode, _type, _sb_opt, _default, _hint, _help)	\
	[Opt_##_name] = {						\
		.attr	= {						\
			.name	= #_name,				\
			.mode = (_mode) & OPT_RUNTIME ? 0644 : 0444,	\
		},							\
		.mode	= _mode,					\
		.hint	= _hint,					\
		.help	= _help,					\
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

int bch2_opt_parse(struct bch_fs *c, const struct bch_option *opt,
		   const char *val, u64 *res)
{
	ssize_t ret;

	switch (opt->type) {
	case BCH_OPT_BOOL:
		ret = kstrtou64(val, 10, res);
		if (ret < 0)
			return ret;

		if (*res > 1)
			return -ERANGE;
		break;
	case BCH_OPT_UINT:
		ret = kstrtou64(val, 10, res);
		if (ret < 0)
			return ret;

		if (*res < opt->min || *res >= opt->max)
			return -ERANGE;
		break;
	case BCH_OPT_SECTORS:
		ret = bch2_strtou64_h(val, res);
		if (ret < 0)
			return ret;

		if (*res & 511)
			return -EINVAL;

		*res >>= 9;

		if (*res < opt->min || *res >= opt->max)
			return -ERANGE;
		break;
	case BCH_OPT_STR:
		ret = match_string(opt->choices, -1, val);
		if (ret < 0)
			return ret;

		*res = ret;
		break;
	case BCH_OPT_FN:
		if (!c)
			return 0;

		return opt->parse(c, val, res);
	}

	return 0;
}

void bch2_opt_to_text(struct printbuf *out, struct bch_fs *c,
		      const struct bch_option *opt, u64 v,
		      unsigned flags)
{
	if (flags & OPT_SHOW_MOUNT_STYLE) {
		if (opt->type == BCH_OPT_BOOL) {
			pr_buf(out, "%s%s",
			       v ? "" : "no",
			       opt->attr.name);
			return;
		}

		pr_buf(out, "%s=", opt->attr.name);
	}

	switch (opt->type) {
	case BCH_OPT_BOOL:
	case BCH_OPT_UINT:
		pr_buf(out, "%lli", v);
		break;
	case BCH_OPT_SECTORS:
		bch2_hprint(out, v);
		break;
	case BCH_OPT_STR:
		if (flags & OPT_SHOW_FULL_LIST)
			bch2_string_opt_to_text(out, opt->choices, v);
		else
			pr_buf(out, opt->choices[v]);
		break;
	case BCH_OPT_FN:
		opt->to_text(out, c, v);
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
	u64 v;

	if (!options)
		return 0;

	copied_opts = kstrdup(options, GFP_KERNEL);
	if (!copied_opts)
		return -1;
	copied_opts_start = copied_opts;

	while ((opt = strsep(&copied_opts, ",")) != NULL) {
		name	= strsep(&opt, "=");
		val	= opt;

		if (val) {
			id = bch2_mount_opt_lookup(name);
			if (id < 0)
				goto bad_opt;

			ret = bch2_opt_parse(c, &bch2_opt_table[id], val, &v);
			if (ret < 0)
				goto bad_val;
		} else {
			id = bch2_mount_opt_lookup(name);
			v = 1;

			if (id < 0 &&
			    !strncmp("no", name, 2)) {
				id = bch2_mount_opt_lookup(name + 2);
				v = 0;
			}

			if (id < 0)
				goto bad_opt;

			if (bch2_opt_table[id].type != BCH_OPT_BOOL)
				goto no_val;
		}

		if (!(bch2_opt_table[id].mode & OPT_MOUNT))
			goto bad_opt;

		if (id == Opt_acl &&
		    !IS_ENABLED(CONFIG_BCACHEFS_POSIX_ACL))
			goto bad_opt;

		if ((id == Opt_usrquota ||
		     id == Opt_grpquota) &&
		    !IS_ENABLED(CONFIG_BCACHEFS_QUOTA))
			goto bad_opt;

		bch2_opt_set_by_id(opts, id, v);
	}

	ret = 0;
	goto out;

bad_opt:
	pr_err("Bad mount option %s", name);
	ret = -1;
	goto out;
bad_val:
	pr_err("Invalid value %s for mount option %s", val, name);
	ret = -1;
	goto out;
no_val:
	pr_err("Mount option %s requires a value", name);
	ret = -1;
	goto out;
out:
	kfree(copied_opts_start);
	return ret;
}

/* io opts: */

struct bch_io_opts bch2_opts_to_inode_opts(struct bch_opts src)
{
	struct bch_io_opts ret = { 0 };
#define x(_name, _bits)					\
	if (opt_defined(src, _name))					\
		opt_set(ret, _name, src._name);
	BCH_INODE_OPTS()
#undef x
	return ret;
}

struct bch_opts bch2_inode_opts_to_opts(struct bch_io_opts src)
{
	struct bch_opts ret = { 0 };
#define x(_name, _bits)					\
	if (opt_defined(src, _name))					\
		opt_set(ret, _name, src._name);
	BCH_INODE_OPTS()
#undef x
	return ret;
}

void bch2_io_opts_apply(struct bch_io_opts *dst, struct bch_io_opts src)
{
#define x(_name, _bits)					\
	if (opt_defined(src, _name))					\
		opt_set(*dst, _name, src._name);
	BCH_INODE_OPTS()
#undef x
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
