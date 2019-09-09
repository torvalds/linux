// SPDX-License-Identifier: GPL-2.0-or-later
/* Filesystem parameter parser.
 *
 * Copyright (C) 2018 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/export.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/slab.h>
#include <linux/security.h>
#include <linux/namei.h>
#include "internal.h"

static const struct constant_table bool_names[] = {
	{ "0",		false },
	{ "1",		true },
	{ "false",	false },
	{ "no",		false },
	{ "true",	true },
	{ "yes",	true },
};

/**
 * lookup_constant - Look up a constant by name in an ordered table
 * @tbl: The table of constants to search.
 * @tbl_size: The size of the table.
 * @name: The name to look up.
 * @not_found: The value to return if the name is not found.
 */
int __lookup_constant(const struct constant_table *tbl, size_t tbl_size,
		      const char *name, int not_found)
{
	unsigned int i;

	for (i = 0; i < tbl_size; i++)
		if (strcmp(name, tbl[i].name) == 0)
			return tbl[i].value;

	return not_found;
}
EXPORT_SYMBOL(__lookup_constant);

static const struct fs_parameter_spec *fs_lookup_key(
	const struct fs_parameter_description *desc,
	const char *name)
{
	const struct fs_parameter_spec *p;

	if (!desc->specs)
		return NULL;

	for (p = desc->specs; p->name; p++)
		if (strcmp(p->name, name) == 0)
			return p;

	return NULL;
}

/*
 * fs_parse - Parse a filesystem configuration parameter
 * @fc: The filesystem context to log errors through.
 * @desc: The parameter description to use.
 * @param: The parameter.
 * @result: Where to place the result of the parse
 *
 * Parse a filesystem configuration parameter and attempt a conversion for a
 * simple parameter for which this is requested.  If successful, the determined
 * parameter ID is placed into @result->key, the desired type is indicated in
 * @result->t and any converted value is placed into an appropriate member of
 * the union in @result.
 *
 * The function returns the parameter number if the parameter was matched,
 * -ENOPARAM if it wasn't matched and @desc->ignore_unknown indicated that
 * unknown parameters are okay and -EINVAL if there was a conversion issue or
 * the parameter wasn't recognised and unknowns aren't okay.
 */
int fs_parse(struct fs_context *fc,
	     const struct fs_parameter_description *desc,
	     struct fs_parameter *param,
	     struct fs_parse_result *result)
{
	const struct fs_parameter_spec *p;
	const struct fs_parameter_enum *e;
	int ret = -ENOPARAM, b;

	result->has_value = !!param->string;
	result->negated = false;
	result->uint_64 = 0;

	p = fs_lookup_key(desc, param->key);
	if (!p) {
		/* If we didn't find something that looks like "noxxx", see if
		 * "xxx" takes the "no"-form negative - but only if there
		 * wasn't an value.
		 */
		if (result->has_value)
			goto unknown_parameter;
		if (param->key[0] != 'n' || param->key[1] != 'o' || !param->key[2])
			goto unknown_parameter;

		p = fs_lookup_key(desc, param->key + 2);
		if (!p)
			goto unknown_parameter;
		if (!(p->flags & fs_param_neg_with_no))
			goto unknown_parameter;
		result->boolean = false;
		result->negated = true;
	}

	if (p->flags & fs_param_deprecated)
		warnf(fc, "%s: Deprecated parameter '%s'",
		      desc->name, param->key);

	if (result->negated)
		goto okay;

	/* Certain parameter types only take a string and convert it. */
	switch (p->type) {
	case __fs_param_wasnt_defined:
		return -EINVAL;
	case fs_param_is_u32:
	case fs_param_is_u32_octal:
	case fs_param_is_u32_hex:
	case fs_param_is_s32:
	case fs_param_is_u64:
	case fs_param_is_enum:
	case fs_param_is_string:
		if (param->type != fs_value_is_string)
			goto bad_value;
		if (!result->has_value) {
			if (p->flags & fs_param_v_optional)
				goto okay;
			goto bad_value;
		}
		/* Fall through */
	default:
		break;
	}

	/* Try to turn the type we were given into the type desired by the
	 * parameter and give an error if we can't.
	 */
	switch (p->type) {
	case fs_param_is_flag:
		if (param->type != fs_value_is_flag &&
		    (param->type != fs_value_is_string || result->has_value))
			return invalf(fc, "%s: Unexpected value for '%s'",
				      desc->name, param->key);
		result->boolean = true;
		goto okay;

	case fs_param_is_bool:
		switch (param->type) {
		case fs_value_is_flag:
			result->boolean = true;
			goto okay;
		case fs_value_is_string:
			if (param->size == 0) {
				result->boolean = true;
				goto okay;
			}
			b = lookup_constant(bool_names, param->string, -1);
			if (b == -1)
				goto bad_value;
			result->boolean = b;
			goto okay;
		default:
			goto bad_value;
		}

	case fs_param_is_u32:
		ret = kstrtouint(param->string, 0, &result->uint_32);
		goto maybe_okay;
	case fs_param_is_u32_octal:
		ret = kstrtouint(param->string, 8, &result->uint_32);
		goto maybe_okay;
	case fs_param_is_u32_hex:
		ret = kstrtouint(param->string, 16, &result->uint_32);
		goto maybe_okay;
	case fs_param_is_s32:
		ret = kstrtoint(param->string, 0, &result->int_32);
		goto maybe_okay;
	case fs_param_is_u64:
		ret = kstrtoull(param->string, 0, &result->uint_64);
		goto maybe_okay;

	case fs_param_is_enum:
		for (e = desc->enums; e->name[0]; e++) {
			if (e->opt == p->opt &&
			    strcmp(e->name, param->string) == 0) {
				result->uint_32 = e->value;
				goto okay;
			}
		}
		goto bad_value;

	case fs_param_is_string:
		goto okay;
	case fs_param_is_blob:
		if (param->type != fs_value_is_blob)
			goto bad_value;
		goto okay;

	case fs_param_is_fd: {
		if (param->type != fs_value_is_file)
			goto bad_value;
		goto okay;
	}

	case fs_param_is_blockdev:
	case fs_param_is_path:
		goto okay;
	default:
		BUG();
	}

maybe_okay:
	if (ret < 0)
		goto bad_value;
okay:
	return p->opt;

bad_value:
	return invalf(fc, "%s: Bad value for '%s'", desc->name, param->key);
unknown_parameter:
	return -ENOPARAM;
}
EXPORT_SYMBOL(fs_parse);

/**
 * fs_lookup_param - Look up a path referred to by a parameter
 * @fc: The filesystem context to log errors through.
 * @param: The parameter.
 * @want_bdev: T if want a blockdev
 * @_path: The result of the lookup
 */
int fs_lookup_param(struct fs_context *fc,
		    struct fs_parameter *param,
		    bool want_bdev,
		    struct path *_path)
{
	struct filename *f;
	unsigned int flags = 0;
	bool put_f;
	int ret;

	switch (param->type) {
	case fs_value_is_string:
		f = getname_kernel(param->string);
		if (IS_ERR(f))
			return PTR_ERR(f);
		put_f = true;
		break;
	case fs_value_is_filename_empty:
		flags = LOOKUP_EMPTY;
		/* Fall through */
	case fs_value_is_filename:
		f = param->name;
		put_f = false;
		break;
	default:
		return invalf(fc, "%s: not usable as path", param->key);
	}

	f->refcnt++; /* filename_lookup() drops our ref. */
	ret = filename_lookup(param->dirfd, f, flags, _path, NULL);
	if (ret < 0) {
		errorf(fc, "%s: Lookup failure for '%s'", param->key, f->name);
		goto out;
	}

	if (want_bdev &&
	    !S_ISBLK(d_backing_inode(_path->dentry)->i_mode)) {
		path_put(_path);
		_path->dentry = NULL;
		_path->mnt = NULL;
		errorf(fc, "%s: Non-blockdev passed as '%s'",
		       param->key, f->name);
		ret = -ENOTBLK;
	}

out:
	if (put_f)
		putname(f);
	return ret;
}
EXPORT_SYMBOL(fs_lookup_param);

#ifdef CONFIG_VALIDATE_FS_PARSER
/**
 * validate_constant_table - Validate a constant table
 * @name: Name to use in reporting
 * @tbl: The constant table to validate.
 * @tbl_size: The size of the table.
 * @low: The lowest permissible value.
 * @high: The highest permissible value.
 * @special: One special permissible value outside of the range.
 */
bool validate_constant_table(const struct constant_table *tbl, size_t tbl_size,
			     int low, int high, int special)
{
	size_t i;
	bool good = true;

	if (tbl_size == 0) {
		pr_warn("VALIDATE C-TBL: Empty\n");
		return true;
	}

	for (i = 0; i < tbl_size; i++) {
		if (!tbl[i].name) {
			pr_err("VALIDATE C-TBL[%zu]: Null\n", i);
			good = false;
		} else if (i > 0 && tbl[i - 1].name) {
			int c = strcmp(tbl[i-1].name, tbl[i].name);

			if (c == 0) {
				pr_err("VALIDATE C-TBL[%zu]: Duplicate %s\n",
				       i, tbl[i].name);
				good = false;
			}
			if (c > 0) {
				pr_err("VALIDATE C-TBL[%zu]: Missorted %s>=%s\n",
				       i, tbl[i-1].name, tbl[i].name);
				good = false;
			}
		}

		if (tbl[i].value != special &&
		    (tbl[i].value < low || tbl[i].value > high)) {
			pr_err("VALIDATE C-TBL[%zu]: %s->%d const out of range (%d-%d)\n",
			       i, tbl[i].name, tbl[i].value, low, high);
			good = false;
		}
	}

	return good;
}

/**
 * fs_validate_description - Validate a parameter description
 * @desc: The parameter description to validate.
 */
bool fs_validate_description(const struct fs_parameter_description *desc)
{
	const struct fs_parameter_spec *param, *p2;
	const struct fs_parameter_enum *e;
	const char *name = desc->name;
	unsigned int nr_params = 0;
	bool good = true, enums = false;

	pr_notice("*** VALIDATE %s ***\n", name);

	if (!name[0]) {
		pr_err("VALIDATE Parser: No name\n");
		name = "Unknown";
		good = false;
	}

	if (desc->specs) {
		for (param = desc->specs; param->name; param++) {
			enum fs_parameter_type t = param->type;

			/* Check that the type is in range */
			if (t == __fs_param_wasnt_defined ||
			    t >= nr__fs_parameter_type) {
				pr_err("VALIDATE %s: PARAM[%s] Bad type %u\n",
				       name, param->name, t);
				good = false;
			} else if (t == fs_param_is_enum) {
				enums = true;
			}

			/* Check for duplicate parameter names */
			for (p2 = desc->specs; p2 < param; p2++) {
				if (strcmp(param->name, p2->name) == 0) {
					pr_err("VALIDATE %s: PARAM[%s]: Duplicate\n",
					       name, param->name);
					good = false;
				}
			}
		}

		nr_params = param - desc->specs;
	}

	if (desc->enums) {
		if (!nr_params) {
			pr_err("VALIDATE %s: Enum table but no parameters\n",
			       name);
			good = false;
			goto no_enums;
		}
		if (!enums) {
			pr_err("VALIDATE %s: Enum table but no enum-type values\n",
			       name);
			good = false;
			goto no_enums;
		}

		for (e = desc->enums; e->name[0]; e++) {
			/* Check that all entries in the enum table have at
			 * least one parameter that uses them.
			 */
			for (param = desc->specs; param->name; param++) {
				if (param->opt == e->opt &&
				    param->type != fs_param_is_enum) {
					pr_err("VALIDATE %s: e[%tu] enum val for %s\n",
					       name, e - desc->enums, param->name);
					good = false;
				}
			}
		}

		/* Check that all enum-type parameters have at least one enum
		 * value in the enum table.
		 */
		for (param = desc->specs; param->name; param++) {
			if (param->type != fs_param_is_enum)
				continue;
			for (e = desc->enums; e->name[0]; e++)
				if (e->opt == param->opt)
					break;
			if (!e->name[0]) {
				pr_err("VALIDATE %s: PARAM[%s] enum with no values\n",
				       name, param->name);
				good = false;
			}
		}
	} else {
		if (enums) {
			pr_err("VALIDATE %s: enum-type values, but no enum table\n",
			       name);
			good = false;
			goto no_enums;
		}
	}

no_enums:
	return good;
}
#endif /* CONFIG_VALIDATE_FS_PARSER */
