// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfsplus/options.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 *
 * Option parsing
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/nls.h>
#include <linux/mount.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include "hfsplus_fs.h"

enum {
	opt_creator, opt_type,
	opt_umask, opt_uid, opt_gid,
	opt_part, opt_session, opt_nls,
	opt_decompose, opt_barrier,
	opt_force,
};

static const struct fs_parameter_spec hfs_param_spec[] = {
	fsparam_string	("creator",	opt_creator),
	fsparam_string	("type",	opt_type),
	fsparam_u32oct	("umask",	opt_umask),
	fsparam_u32	("uid",		opt_uid),
	fsparam_u32	("gid",		opt_gid),
	fsparam_u32	("part",	opt_part),
	fsparam_u32	("session",	opt_session),
	fsparam_string	("nls",		opt_nls),
	fsparam_flag_no	("decompose",	opt_decompose),
	fsparam_flag_no	("barrier",	opt_barrier),
	fsparam_flag	("force",	opt_force),
	{}
};

/* Initialize an options object to reasonable defaults */
void hfsplus_fill_defaults(struct hfsplus_sb_info *opts)
{
	if (!opts)
		return;

	opts->creator = HFSPLUS_DEF_CR_TYPE;
	opts->type = HFSPLUS_DEF_CR_TYPE;
	opts->umask = current_umask();
	opts->uid = current_uid();
	opts->gid = current_gid();
	opts->part = -1;
	opts->session = -1;
}

/* Parse options from mount. Returns nonzero errno on failure */
int hfsplus_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct hfsplus_sb_info *sbi = fc->s_fs_info;
	struct fs_parse_result result;
	int opt;

	/*
	 * Only the force option is examined during remount, all others
	 * are ignored.
	 */
	if (fc->purpose == FS_CONTEXT_FOR_RECONFIGURE &&
	    strncmp(param->key, "force", 5))
		return 0;

	opt = fs_parse(fc, hfs_param_spec, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case opt_creator:
		if (strlen(param->string) != 4) {
			pr_err("creator requires a 4 character value\n");
			return -EINVAL;
		}
		memcpy(&sbi->creator, param->string, 4);
		break;
	case opt_type:
		if (strlen(param->string) != 4) {
			pr_err("type requires a 4 character value\n");
			return -EINVAL;
		}
		memcpy(&sbi->type, param->string, 4);
		break;
	case opt_umask:
		sbi->umask = (umode_t)result.uint_32;
		break;
	case opt_uid:
		sbi->uid = result.uid;
		set_bit(HFSPLUS_SB_UID, &sbi->flags);
		break;
	case opt_gid:
		sbi->gid = result.gid;
		set_bit(HFSPLUS_SB_GID, &sbi->flags);
		break;
	case opt_part:
		sbi->part = result.uint_32;
		break;
	case opt_session:
		sbi->session = result.uint_32;
		break;
	case opt_nls:
		if (sbi->nls) {
			pr_err("unable to change nls mapping\n");
			return -EINVAL;
		}
		sbi->nls = load_nls(param->string);
		if (!sbi->nls) {
			pr_err("unable to load nls mapping \"%s\"\n",
			       param->string);
			return -EINVAL;
		}
		break;
	case opt_decompose:
		if (result.negated)
			set_bit(HFSPLUS_SB_NODECOMPOSE, &sbi->flags);
		else
			clear_bit(HFSPLUS_SB_NODECOMPOSE, &sbi->flags);
		break;
	case opt_barrier:
		if (result.negated)
			set_bit(HFSPLUS_SB_NOBARRIER, &sbi->flags);
		else
			clear_bit(HFSPLUS_SB_NOBARRIER, &sbi->flags);
		break;
	case opt_force:
		set_bit(HFSPLUS_SB_FORCE, &sbi->flags);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int hfsplus_show_options(struct seq_file *seq, struct dentry *root)
{
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(root->d_sb);

	if (sbi->creator != HFSPLUS_DEF_CR_TYPE)
		seq_show_option_n(seq, "creator", (char *)&sbi->creator, 4);
	if (sbi->type != HFSPLUS_DEF_CR_TYPE)
		seq_show_option_n(seq, "type", (char *)&sbi->type, 4);
	seq_printf(seq, ",umask=%o,uid=%u,gid=%u", sbi->umask,
			from_kuid_munged(&init_user_ns, sbi->uid),
			from_kgid_munged(&init_user_ns, sbi->gid));
	if (sbi->part >= 0)
		seq_printf(seq, ",part=%u", sbi->part);
	if (sbi->session >= 0)
		seq_printf(seq, ",session=%u", sbi->session);
	if (sbi->nls)
		seq_printf(seq, ",nls=%s", sbi->nls->charset);
	if (test_bit(HFSPLUS_SB_NODECOMPOSE, &sbi->flags))
		seq_puts(seq, ",nodecompose");
	if (test_bit(HFSPLUS_SB_NOBARRIER, &sbi->flags))
		seq_puts(seq, ",nobarrier");
	return 0;
}
