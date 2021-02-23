// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2020, Microsoft Corporation.
 *
 *   Author(s): Steve French <stfrench@microsoft.com>
 *              David Howells <dhowells@redhat.com>
 */

/*
#include <linux/module.h>
#include <linux/nsproxy.h>
#include <linux/slab.h>
#include <linux/magic.h>
#include <linux/security.h>
#include <net/net_namespace.h>
*/

#include <linux/ctype.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/parser.h>
#include <linux/utsname.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include "ntlmssp.h"
#include "nterr.h"
#include "rfc1002pdu.h"
#include "fs_context.h"

static const match_table_t cifs_smb_version_tokens = {
	{ Smb_1, SMB1_VERSION_STRING },
	{ Smb_20, SMB20_VERSION_STRING},
	{ Smb_21, SMB21_VERSION_STRING },
	{ Smb_30, SMB30_VERSION_STRING },
	{ Smb_302, SMB302_VERSION_STRING },
	{ Smb_302, ALT_SMB302_VERSION_STRING },
	{ Smb_311, SMB311_VERSION_STRING },
	{ Smb_311, ALT_SMB311_VERSION_STRING },
	{ Smb_3any, SMB3ANY_VERSION_STRING },
	{ Smb_default, SMBDEFAULT_VERSION_STRING },
	{ Smb_version_err, NULL }
};

static const match_table_t cifs_secflavor_tokens = {
	{ Opt_sec_krb5, "krb5" },
	{ Opt_sec_krb5i, "krb5i" },
	{ Opt_sec_krb5p, "krb5p" },
	{ Opt_sec_ntlmsspi, "ntlmsspi" },
	{ Opt_sec_ntlmssp, "ntlmssp" },
	{ Opt_ntlm, "ntlm" },
	{ Opt_sec_ntlmi, "ntlmi" },
	{ Opt_sec_ntlmv2, "nontlm" },
	{ Opt_sec_ntlmv2, "ntlmv2" },
	{ Opt_sec_ntlmv2i, "ntlmv2i" },
	{ Opt_sec_lanman, "lanman" },
	{ Opt_sec_none, "none" },

	{ Opt_sec_err, NULL }
};

const struct fs_parameter_spec smb3_fs_parameters[] = {
	/* Mount options that take no arguments */
	fsparam_flag_no("user_xattr", Opt_user_xattr),
	fsparam_flag_no("forceuid", Opt_forceuid),
	fsparam_flag_no("multichannel", Opt_multichannel),
	fsparam_flag_no("forcegid", Opt_forcegid),
	fsparam_flag("noblocksend", Opt_noblocksend),
	fsparam_flag("noautotune", Opt_noautotune),
	fsparam_flag("nolease", Opt_nolease),
	fsparam_flag_no("hard", Opt_hard),
	fsparam_flag_no("soft", Opt_soft),
	fsparam_flag_no("perm", Opt_perm),
	fsparam_flag("nodelete", Opt_nodelete),
	fsparam_flag_no("mapposix", Opt_mapposix),
	fsparam_flag("mapchars", Opt_mapchars),
	fsparam_flag("nomapchars", Opt_nomapchars),
	fsparam_flag_no("sfu", Opt_sfu),
	fsparam_flag("nodfs", Opt_nodfs),
	fsparam_flag_no("posixpaths", Opt_posixpaths),
	fsparam_flag_no("unix", Opt_unix),
	fsparam_flag_no("linux", Opt_unix),
	fsparam_flag_no("posix", Opt_unix),
	fsparam_flag("nocase", Opt_nocase),
	fsparam_flag("ignorecase", Opt_nocase),
	fsparam_flag_no("brl", Opt_brl),
	fsparam_flag_no("handlecache", Opt_handlecache),
	fsparam_flag("forcemandatorylock", Opt_forcemandatorylock),
	fsparam_flag("forcemand", Opt_forcemandatorylock),
	fsparam_flag("setuidfromacl", Opt_setuidfromacl),
	fsparam_flag("idsfromsid", Opt_setuidfromacl),
	fsparam_flag_no("setuids", Opt_setuids),
	fsparam_flag_no("dynperm", Opt_dynperm),
	fsparam_flag_no("intr", Opt_intr),
	fsparam_flag_no("strictsync", Opt_strictsync),
	fsparam_flag_no("serverino", Opt_serverino),
	fsparam_flag("rwpidforward", Opt_rwpidforward),
	fsparam_flag("cifsacl", Opt_cifsacl),
	fsparam_flag_no("acl", Opt_acl),
	fsparam_flag("locallease", Opt_locallease),
	fsparam_flag("sign", Opt_sign),
	fsparam_flag("ignore_signature", Opt_ignore_signature),
	fsparam_flag("signloosely", Opt_ignore_signature),
	fsparam_flag("seal", Opt_seal),
	fsparam_flag("noac", Opt_noac),
	fsparam_flag("fsc", Opt_fsc),
	fsparam_flag("mfsymlinks", Opt_mfsymlinks),
	fsparam_flag("multiuser", Opt_multiuser),
	fsparam_flag("sloppy", Opt_sloppy),
	fsparam_flag("nosharesock", Opt_nosharesock),
	fsparam_flag_no("persistenthandles", Opt_persistent),
	fsparam_flag_no("resilienthandles", Opt_resilient),
	fsparam_flag("domainauto", Opt_domainauto),
	fsparam_flag("rdma", Opt_rdma),
	fsparam_flag("modesid", Opt_modesid),
	fsparam_flag("modefromsid", Opt_modesid),
	fsparam_flag("rootfs", Opt_rootfs),
	fsparam_flag("compress", Opt_compress),
	fsparam_flag("witness", Opt_witness),

	/* Mount options which take numeric value */
	fsparam_u32("backupuid", Opt_backupuid),
	fsparam_u32("backupgid", Opt_backupgid),
	fsparam_u32("uid", Opt_uid),
	fsparam_u32("cruid", Opt_cruid),
	fsparam_u32("gid", Opt_gid),
	fsparam_u32("file_mode", Opt_file_mode),
	fsparam_u32("dirmode", Opt_dirmode),
	fsparam_u32("dir_mode", Opt_dirmode),
	fsparam_u32("port", Opt_port),
	fsparam_u32("min_enc_offload", Opt_min_enc_offload),
	fsparam_u32("esize", Opt_min_enc_offload),
	fsparam_u32("bsize", Opt_blocksize),
	fsparam_u32("rsize", Opt_rsize),
	fsparam_u32("wsize", Opt_wsize),
	fsparam_u32("actimeo", Opt_actimeo),
	fsparam_u32("echo_interval", Opt_echo_interval),
	fsparam_u32("max_credits", Opt_max_credits),
	fsparam_u32("handletimeout", Opt_handletimeout),
	fsparam_u32("snapshot", Opt_snapshot),
	fsparam_u32("max_channels", Opt_max_channels),

	/* Mount options which take string value */
	fsparam_string("source", Opt_source),
	fsparam_string("unc", Opt_source),
	fsparam_string("user", Opt_user),
	fsparam_string("username", Opt_user),
	fsparam_string("pass", Opt_pass),
	fsparam_string("password", Opt_pass),
	fsparam_string("ip", Opt_ip),
	fsparam_string("addr", Opt_ip),
	fsparam_string("domain", Opt_domain),
	fsparam_string("dom", Opt_domain),
	fsparam_string("srcaddr", Opt_srcaddr),
	fsparam_string("iocharset", Opt_iocharset),
	fsparam_string("netbiosname", Opt_netbiosname),
	fsparam_string("servern", Opt_servern),
	fsparam_string("ver", Opt_ver),
	fsparam_string("vers", Opt_vers),
	fsparam_string("sec", Opt_sec),
	fsparam_string("cache", Opt_cache),

	/* Arguments that should be ignored */
	fsparam_flag("guest", Opt_ignore),
	fsparam_flag("noatime", Opt_ignore),
	fsparam_flag("relatime", Opt_ignore),
	fsparam_flag("_netdev", Opt_ignore),
	fsparam_flag_no("suid", Opt_ignore),
	fsparam_flag_no("exec", Opt_ignore),
	fsparam_flag_no("dev", Opt_ignore),
	fsparam_flag_no("mand", Opt_ignore),
	fsparam_string("cred", Opt_ignore),
	fsparam_string("credentials", Opt_ignore),
	{}
};

int
cifs_parse_security_flavors(char *value, struct smb3_fs_context *ctx)
{

	substring_t args[MAX_OPT_ARGS];

	/*
	 * With mount options, the last one should win. Reset any existing
	 * settings back to default.
	 */
	ctx->sectype = Unspecified;
	ctx->sign = false;

	switch (match_token(value, cifs_secflavor_tokens, args)) {
	case Opt_sec_krb5p:
		cifs_dbg(VFS, "sec=krb5p is not supported!\n");
		return 1;
	case Opt_sec_krb5i:
		ctx->sign = true;
		fallthrough;
	case Opt_sec_krb5:
		ctx->sectype = Kerberos;
		break;
	case Opt_sec_ntlmsspi:
		ctx->sign = true;
		fallthrough;
	case Opt_sec_ntlmssp:
		ctx->sectype = RawNTLMSSP;
		break;
	case Opt_sec_ntlmi:
		ctx->sign = true;
		fallthrough;
	case Opt_ntlm:
		ctx->sectype = NTLM;
		break;
	case Opt_sec_ntlmv2i:
		ctx->sign = true;
		fallthrough;
	case Opt_sec_ntlmv2:
		ctx->sectype = NTLMv2;
		break;
#ifdef CONFIG_CIFS_WEAK_PW_HASH
	case Opt_sec_lanman:
		ctx->sectype = LANMAN;
		break;
#endif
	case Opt_sec_none:
		ctx->nullauth = 1;
		break;
	default:
		cifs_dbg(VFS, "bad security option: %s\n", value);
		return 1;
	}

	return 0;
}

static const match_table_t cifs_cacheflavor_tokens = {
	{ Opt_cache_loose, "loose" },
	{ Opt_cache_strict, "strict" },
	{ Opt_cache_none, "none" },
	{ Opt_cache_ro, "ro" },
	{ Opt_cache_rw, "singleclient" },
	{ Opt_cache_err, NULL }
};

int
cifs_parse_cache_flavor(char *value, struct smb3_fs_context *ctx)
{
	substring_t args[MAX_OPT_ARGS];

	switch (match_token(value, cifs_cacheflavor_tokens, args)) {
	case Opt_cache_loose:
		ctx->direct_io = false;
		ctx->strict_io = false;
		ctx->cache_ro = false;
		ctx->cache_rw = false;
		break;
	case Opt_cache_strict:
		ctx->direct_io = false;
		ctx->strict_io = true;
		ctx->cache_ro = false;
		ctx->cache_rw = false;
		break;
	case Opt_cache_none:
		ctx->direct_io = true;
		ctx->strict_io = false;
		ctx->cache_ro = false;
		ctx->cache_rw = false;
		break;
	case Opt_cache_ro:
		ctx->direct_io = false;
		ctx->strict_io = false;
		ctx->cache_ro = true;
		ctx->cache_rw = false;
		break;
	case Opt_cache_rw:
		ctx->direct_io = false;
		ctx->strict_io = false;
		ctx->cache_ro = false;
		ctx->cache_rw = true;
		break;
	default:
		cifs_dbg(VFS, "bad cache= option: %s\n", value);
		return 1;
	}
	return 0;
}

#define DUP_CTX_STR(field)						\
do {									\
	if (ctx->field) {						\
		new_ctx->field = kstrdup(ctx->field, GFP_ATOMIC);	\
		if (new_ctx->field == NULL) {				\
			smb3_cleanup_fs_context_contents(new_ctx);	\
			return -ENOMEM;					\
		}							\
	}								\
} while (0)

int
smb3_fs_context_dup(struct smb3_fs_context *new_ctx, struct smb3_fs_context *ctx)
{
	memcpy(new_ctx, ctx, sizeof(*ctx));
	new_ctx->prepath = NULL;
	new_ctx->mount_options = NULL;
	new_ctx->nodename = NULL;
	new_ctx->username = NULL;
	new_ctx->password = NULL;
	new_ctx->domainname = NULL;
	new_ctx->UNC = NULL;
	new_ctx->iocharset = NULL;

	/*
	 * Make sure to stay in sync with smb3_cleanup_fs_context_contents()
	 */
	DUP_CTX_STR(prepath);
	DUP_CTX_STR(mount_options);
	DUP_CTX_STR(username);
	DUP_CTX_STR(password);
	DUP_CTX_STR(UNC);
	DUP_CTX_STR(domainname);
	DUP_CTX_STR(nodename);
	DUP_CTX_STR(iocharset);

	return 0;
}

static int
cifs_parse_smb_version(char *value, struct smb3_fs_context *ctx, bool is_smb3)
{
	substring_t args[MAX_OPT_ARGS];

	switch (match_token(value, cifs_smb_version_tokens, args)) {
#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
	case Smb_1:
		if (disable_legacy_dialects) {
			cifs_dbg(VFS, "mount with legacy dialect disabled\n");
			return 1;
		}
		if (is_smb3) {
			cifs_dbg(VFS, "vers=1.0 (cifs) not permitted when mounting with smb3\n");
			return 1;
		}
		cifs_dbg(VFS, "Use of the less secure dialect vers=1.0 is not recommended unless required for access to very old servers\n");
		ctx->ops = &smb1_operations;
		ctx->vals = &smb1_values;
		break;
	case Smb_20:
		if (disable_legacy_dialects) {
			cifs_dbg(VFS, "mount with legacy dialect disabled\n");
			return 1;
		}
		if (is_smb3) {
			cifs_dbg(VFS, "vers=2.0 not permitted when mounting with smb3\n");
			return 1;
		}
		ctx->ops = &smb20_operations;
		ctx->vals = &smb20_values;
		break;
#else
	case Smb_1:
		cifs_dbg(VFS, "vers=1.0 (cifs) mount not permitted when legacy dialects disabled\n");
		return 1;
	case Smb_20:
		cifs_dbg(VFS, "vers=2.0 mount not permitted when legacy dialects disabled\n");
		return 1;
#endif /* CIFS_ALLOW_INSECURE_LEGACY */
	case Smb_21:
		ctx->ops = &smb21_operations;
		ctx->vals = &smb21_values;
		break;
	case Smb_30:
		ctx->ops = &smb30_operations;
		ctx->vals = &smb30_values;
		break;
	case Smb_302:
		ctx->ops = &smb30_operations; /* currently identical with 3.0 */
		ctx->vals = &smb302_values;
		break;
	case Smb_311:
		ctx->ops = &smb311_operations;
		ctx->vals = &smb311_values;
		break;
	case Smb_3any:
		ctx->ops = &smb30_operations; /* currently identical with 3.0 */
		ctx->vals = &smb3any_values;
		break;
	case Smb_default:
		ctx->ops = &smb30_operations; /* currently identical with 3.0 */
		ctx->vals = &smbdefault_values;
		break;
	default:
		cifs_dbg(VFS, "Unknown vers= option specified: %s\n", value);
		return 1;
	}
	return 0;
}

/*
 * Parse a devname into substrings and populate the ctx->UNC and ctx->prepath
 * fields with the result. Returns 0 on success and an error otherwise
 * (e.g. ENOMEM or EINVAL)
 */
int
smb3_parse_devname(const char *devname, struct smb3_fs_context *ctx)
{
	char *pos;
	const char *delims = "/\\";
	size_t len;

	if (unlikely(!devname || !*devname)) {
		cifs_dbg(VFS, "Device name not specified\n");
		return -EINVAL;
	}

	/* make sure we have a valid UNC double delimiter prefix */
	len = strspn(devname, delims);
	if (len != 2)
		return -EINVAL;

	/* find delimiter between host and sharename */
	pos = strpbrk(devname + 2, delims);
	if (!pos)
		return -EINVAL;

	/* skip past delimiter */
	++pos;

	/* now go until next delimiter or end of string */
	len = strcspn(pos, delims);

	/* move "pos" up to delimiter or NULL */
	pos += len;
	ctx->UNC = kstrndup(devname, pos - devname, GFP_KERNEL);
	if (!ctx->UNC)
		return -ENOMEM;

	convert_delimiter(ctx->UNC, '\\');

	/* skip any delimiter */
	if (*pos == '/' || *pos == '\\')
		pos++;

	/* If pos is NULL then no prepath */
	if (!*pos)
		return 0;

	ctx->prepath = kstrdup(pos, GFP_KERNEL);
	if (!ctx->prepath)
		return -ENOMEM;

	return 0;
}

static void smb3_fs_context_free(struct fs_context *fc);
static int smb3_fs_context_parse_param(struct fs_context *fc,
				       struct fs_parameter *param);
static int smb3_fs_context_parse_monolithic(struct fs_context *fc,
					    void *data);
static int smb3_get_tree(struct fs_context *fc);
static int smb3_reconfigure(struct fs_context *fc);

static const struct fs_context_operations smb3_fs_context_ops = {
	.free			= smb3_fs_context_free,
	.parse_param		= smb3_fs_context_parse_param,
	.parse_monolithic	= smb3_fs_context_parse_monolithic,
	.get_tree		= smb3_get_tree,
	.reconfigure		= smb3_reconfigure,
};

/*
 * Parse a monolithic block of data from sys_mount().
 * smb3_fs_context_parse_monolithic - Parse key[=val][,key[=val]]* mount data
 * @ctx: The superblock configuration to fill in.
 * @data: The data to parse
 *
 * Parse a blob of data that's in key[=val][,key[=val]]* form.  This can be
 * called from the ->monolithic_mount_data() fs_context operation.
 *
 * Returns 0 on success or the error returned by the ->parse_option() fs_context
 * operation on failure.
 */
static int smb3_fs_context_parse_monolithic(struct fs_context *fc,
					   void *data)
{
	struct smb3_fs_context *ctx = smb3_fc2context(fc);
	char *options = data, *key;
	int ret = 0;

	if (!options)
		return 0;

	ctx->mount_options = kstrdup(data, GFP_KERNEL);
	if (ctx->mount_options == NULL)
		return -ENOMEM;

	ret = security_sb_eat_lsm_opts(options, &fc->security);
	if (ret)
		return ret;

	/* BB Need to add support for sep= here TBD */
	while ((key = strsep(&options, ",")) != NULL) {
		if (*key) {
			size_t v_len = 0;
			char *value = strchr(key, '=');

			if (value) {
				if (value == key)
					continue;
				*value++ = 0;
				v_len = strlen(value);
			}
			ret = vfs_parse_fs_string(fc, key, value, v_len);
			if (ret < 0)
				break;
		}
	}

	return ret;
}

/*
 * Validate the preparsed information in the config.
 */
static int smb3_fs_context_validate(struct fs_context *fc)
{
	struct smb3_fs_context *ctx = smb3_fc2context(fc);

	if (ctx->rdma && ctx->vals->protocol_id < SMB30_PROT_ID) {
		cifs_dbg(VFS, "SMB Direct requires Version >=3.0\n");
		return -1;
	}

#ifndef CONFIG_KEYS
	/* Muliuser mounts require CONFIG_KEYS support */
	if (ctx->multiuser) {
		cifs_dbg(VFS, "Multiuser mounts require kernels with CONFIG_KEYS enabled\n");
		return -1;
	}
#endif

	if (ctx->got_version == false)
		pr_warn_once("No dialect specified on mount. Default has changed to a more secure dialect, SMB2.1 or later (e.g. SMB3.1.1), from CIFS (SMB1). To use the less secure SMB1 dialect to access old servers which do not support SMB3.1.1 (or even SMB3 or SMB2.1) specify vers=1.0 on mount.\n");


	if (!ctx->UNC) {
		cifs_dbg(VFS, "CIFS mount error: No usable UNC path provided in device string!\n");
		return -1;
	}

	/* make sure UNC has a share name */
	if (strlen(ctx->UNC) < 3 || !strchr(ctx->UNC + 3, '\\')) {
		cifs_dbg(VFS, "Malformed UNC. Unable to find share name.\n");
		return -1;
	}

	if (!ctx->got_ip) {
		int len;
		const char *slash;

		/* No ip= option specified? Try to get it from UNC */
		/* Use the address part of the UNC. */
		slash = strchr(&ctx->UNC[2], '\\');
		len = slash - &ctx->UNC[2];
		if (!cifs_convert_address((struct sockaddr *)&ctx->dstaddr,
					  &ctx->UNC[2], len)) {
			pr_err("Unable to determine destination address\n");
			return -1;
		}
	}

	/* set the port that we got earlier */
	cifs_set_port((struct sockaddr *)&ctx->dstaddr, ctx->port);

	if (ctx->override_uid && !ctx->uid_specified) {
		ctx->override_uid = 0;
		pr_notice("ignoring forceuid mount option specified with no uid= option\n");
	}

	if (ctx->override_gid && !ctx->gid_specified) {
		ctx->override_gid = 0;
		pr_notice("ignoring forcegid mount option specified with no gid= option\n");
	}

	return 0;
}

static int smb3_get_tree_common(struct fs_context *fc)
{
	struct smb3_fs_context *ctx = smb3_fc2context(fc);
	struct dentry *root;
	int rc = 0;

	root = cifs_smb3_do_mount(fc->fs_type, 0, ctx);
	if (IS_ERR(root))
		return PTR_ERR(root);

	fc->root = root;

	return rc;
}

/*
 * Create an SMB3 superblock from the parameters passed.
 */
static int smb3_get_tree(struct fs_context *fc)
{
	int err = smb3_fs_context_validate(fc);

	if (err)
		return err;
	return smb3_get_tree_common(fc);
}

static void smb3_fs_context_free(struct fs_context *fc)
{
	struct smb3_fs_context *ctx = smb3_fc2context(fc);

	smb3_cleanup_fs_context(ctx);
}

/*
 * Compare the old and new proposed context during reconfigure
 * and check if the changes are compatible.
 */
static int smb3_verify_reconfigure_ctx(struct smb3_fs_context *new_ctx,
				       struct smb3_fs_context *old_ctx)
{
	if (new_ctx->posix_paths != old_ctx->posix_paths) {
		cifs_dbg(VFS, "can not change posixpaths during remount\n");
		return -EINVAL;
	}
	if (new_ctx->sectype != old_ctx->sectype) {
		cifs_dbg(VFS, "can not change sec during remount\n");
		return -EINVAL;
	}
	if (new_ctx->multiuser != old_ctx->multiuser) {
		cifs_dbg(VFS, "can not change multiuser during remount\n");
		return -EINVAL;
	}
	if (new_ctx->UNC &&
	    (!old_ctx->UNC || strcmp(new_ctx->UNC, old_ctx->UNC))) {
		cifs_dbg(VFS, "can not change UNC during remount\n");
		return -EINVAL;
	}
	if (new_ctx->username &&
	    (!old_ctx->username || strcmp(new_ctx->username, old_ctx->username))) {
		cifs_dbg(VFS, "can not change username during remount\n");
		return -EINVAL;
	}
	if (new_ctx->password &&
	    (!old_ctx->password || strcmp(new_ctx->password, old_ctx->password))) {
		cifs_dbg(VFS, "can not change password during remount\n");
		return -EINVAL;
	}
	if (new_ctx->domainname &&
	    (!old_ctx->domainname || strcmp(new_ctx->domainname, old_ctx->domainname))) {
		cifs_dbg(VFS, "can not change domainname during remount\n");
		return -EINVAL;
	}
	if (new_ctx->nodename &&
	    (!old_ctx->nodename || strcmp(new_ctx->nodename, old_ctx->nodename))) {
		cifs_dbg(VFS, "can not change nodename during remount\n");
		return -EINVAL;
	}
	if (new_ctx->iocharset &&
	    (!old_ctx->iocharset || strcmp(new_ctx->iocharset, old_ctx->iocharset))) {
		cifs_dbg(VFS, "can not change iocharset during remount\n");
		return -EINVAL;
	}

	return 0;
}

#define STEAL_STRING(cifs_sb, ctx, field)				\
do {									\
	kfree(ctx->field);						\
	ctx->field = cifs_sb->ctx->field;				\
	cifs_sb->ctx->field = NULL;					\
} while (0)

static int smb3_reconfigure(struct fs_context *fc)
{
	struct smb3_fs_context *ctx = smb3_fc2context(fc);
	struct dentry *root = fc->root;
	struct cifs_sb_info *cifs_sb = CIFS_SB(root->d_sb);
	int rc;

	rc = smb3_verify_reconfigure_ctx(ctx, cifs_sb->ctx);
	if (rc)
		return rc;

	/*
	 * We can not change UNC/username/password/domainname/nodename/iocharset
	 * during reconnect so ignore what we have in the new context and
	 * just use what we already have in cifs_sb->ctx.
	 */
	STEAL_STRING(cifs_sb, ctx, UNC);
	STEAL_STRING(cifs_sb, ctx, username);
	STEAL_STRING(cifs_sb, ctx, password);
	STEAL_STRING(cifs_sb, ctx, domainname);
	STEAL_STRING(cifs_sb, ctx, nodename);
	STEAL_STRING(cifs_sb, ctx, iocharset);

	/* if rsize or wsize not passed in on remount, use previous values */
	if (ctx->rsize == 0)
		ctx->rsize = cifs_sb->ctx->rsize;
	if (ctx->wsize == 0)
		ctx->wsize = cifs_sb->ctx->wsize;


	smb3_cleanup_fs_context_contents(cifs_sb->ctx);
	rc = smb3_fs_context_dup(cifs_sb->ctx, ctx);
	smb3_update_mnt_flags(cifs_sb);

	return rc;
}

static int smb3_fs_context_parse_param(struct fs_context *fc,
				      struct fs_parameter *param)
{
	struct fs_parse_result result;
	struct smb3_fs_context *ctx = smb3_fc2context(fc);
	int i, opt;
	bool is_smb3 = !strcmp(fc->fs_type->name, "smb3");
	bool skip_parsing = false;

	cifs_dbg(FYI, "CIFS: parsing cifs mount option '%s'\n", param->key);

	/*
	 * fs_parse can not handle string options with an empty value so
	 * we will need special handling of them.
	 */
	if (param->type == fs_value_is_string && param->string[0] == 0) {
		if (!strcmp("pass", param->key) || !strcmp("password", param->key)) {
			skip_parsing = true;
			opt = Opt_pass;
		} else if (!strcmp("user", param->key) || !strcmp("username", param->key)) {
			skip_parsing = true;
			opt = Opt_user;
		}
	}

	if (!skip_parsing) {
		opt = fs_parse(fc, smb3_fs_parameters, param, &result);
		if (opt < 0)
			return ctx->sloppy ? 1 : opt;
	}

	switch (opt) {
	case Opt_compress:
		ctx->compression = UNKNOWN_TYPE;
		cifs_dbg(VFS,
			"SMB3 compression support is experimental\n");
		break;
	case Opt_nodfs:
		ctx->nodfs = 1;
		break;
	case Opt_hard:
		if (result.negated)
			ctx->retry = 0;
		else
			ctx->retry = 1;
		break;
	case Opt_soft:
		if (result.negated)
			ctx->retry = 1;
		else
			ctx->retry = 0;
		break;
	case Opt_mapposix:
		if (result.negated)
			ctx->remap = false;
		else {
			ctx->remap = true;
			ctx->sfu_remap = false; /* disable SFU mapping */
		}
		break;
	case Opt_user_xattr:
		if (result.negated)
			ctx->no_xattr = 1;
		else
			ctx->no_xattr = 0;
		break;
	case Opt_forceuid:
		if (result.negated)
			ctx->override_uid = 0;
		else
			ctx->override_uid = 1;
		break;
	case Opt_forcegid:
		if (result.negated)
			ctx->override_gid = 0;
		else
			ctx->override_gid = 1;
		break;
	case Opt_perm:
		if (result.negated)
			ctx->noperm = 1;
		else
			ctx->noperm = 0;
		break;
	case Opt_dynperm:
		if (result.negated)
			ctx->dynperm = 0;
		else
			ctx->dynperm = 1;
		break;
	case Opt_sfu:
		if (result.negated)
			ctx->sfu_emul = 0;
		else
			ctx->sfu_emul = 1;
		break;
	case Opt_noblocksend:
		ctx->noblocksnd = 1;
		break;
	case Opt_noautotune:
		ctx->noautotune = 1;
		break;
	case Opt_nolease:
		ctx->no_lease = 1;
		break;
	case Opt_nodelete:
		ctx->nodelete = 1;
		break;
	case Opt_multichannel:
		if (result.negated) {
			ctx->multichannel = false;
			ctx->max_channels = 1;
		} else {
			ctx->multichannel = true;
			/* if number of channels not specified, default to 2 */
			if (ctx->max_channels < 2)
				ctx->max_channels = 2;
		}
		break;
	case Opt_uid:
		ctx->linux_uid.val = result.uint_32;
		ctx->uid_specified = true;
		break;
	case Opt_cruid:
		ctx->cred_uid.val = result.uint_32;
		break;
	case Opt_backupgid:
		ctx->backupgid.val = result.uint_32;
		ctx->backupgid_specified = true;
		break;
	case Opt_gid:
		ctx->linux_gid.val = result.uint_32;
		ctx->gid_specified = true;
		break;
	case Opt_port:
		ctx->port = result.uint_32;
		break;
	case Opt_file_mode:
		ctx->file_mode = result.uint_32;
		break;
	case Opt_dirmode:
		ctx->dir_mode = result.uint_32;
		break;
	case Opt_min_enc_offload:
		ctx->min_offload = result.uint_32;
		break;
	case Opt_blocksize:
		/*
		 * inode blocksize realistically should never need to be
		 * less than 16K or greater than 16M and default is 1MB.
		 * Note that small inode block sizes (e.g. 64K) can lead
		 * to very poor performance of common tools like cp and scp
		 */
		if ((result.uint_32 < CIFS_MAX_MSGSIZE) ||
		   (result.uint_32 > (4 * SMB3_DEFAULT_IOSIZE))) {
			cifs_dbg(VFS, "%s: Invalid blocksize\n",
				__func__);
			goto cifs_parse_mount_err;
		}
		ctx->bsize = result.uint_32;
		ctx->got_bsize = true;
		break;
	case Opt_rsize:
		ctx->rsize = result.uint_32;
		ctx->got_rsize = true;
		break;
	case Opt_wsize:
		ctx->wsize = result.uint_32;
		ctx->got_wsize = true;
		break;
	case Opt_actimeo:
		ctx->actimeo = HZ * result.uint_32;
		if (ctx->actimeo > CIFS_MAX_ACTIMEO) {
			cifs_dbg(VFS, "attribute cache timeout too large\n");
			goto cifs_parse_mount_err;
		}
		break;
	case Opt_echo_interval:
		ctx->echo_interval = result.uint_32;
		break;
	case Opt_snapshot:
		ctx->snapshot_time = result.uint_32;
		break;
	case Opt_max_credits:
		if (result.uint_32 < 20 || result.uint_32 > 60000) {
			cifs_dbg(VFS, "%s: Invalid max_credits value\n",
				 __func__);
			goto cifs_parse_mount_err;
		}
		ctx->max_credits = result.uint_32;
		break;
	case Opt_max_channels:
		if (result.uint_32 < 1 || result.uint_32 > CIFS_MAX_CHANNELS) {
			cifs_dbg(VFS, "%s: Invalid max_channels value, needs to be 1-%d\n",
				 __func__, CIFS_MAX_CHANNELS);
			goto cifs_parse_mount_err;
		}
		ctx->max_channels = result.uint_32;
		break;
	case Opt_handletimeout:
		ctx->handle_timeout = result.uint_32;
		if (ctx->handle_timeout > SMB3_MAX_HANDLE_TIMEOUT) {
			cifs_dbg(VFS, "Invalid handle cache timeout, longer than 16 minutes\n");
			goto cifs_parse_mount_err;
		}
		break;
	case Opt_source:
		kfree(ctx->UNC);
		ctx->UNC = NULL;
		switch (smb3_parse_devname(param->string, ctx)) {
		case 0:
			break;
		case -ENOMEM:
			cifs_dbg(VFS, "Unable to allocate memory for devname\n");
			goto cifs_parse_mount_err;
		case -EINVAL:
			cifs_dbg(VFS, "Malformed UNC in devname\n");
			goto cifs_parse_mount_err;
		default:
			cifs_dbg(VFS, "Unknown error parsing devname\n");
			goto cifs_parse_mount_err;
		}
		fc->source = kstrdup(param->string, GFP_KERNEL);
		if (fc->source == NULL) {
			cifs_dbg(VFS, "OOM when copying UNC string\n");
			goto cifs_parse_mount_err;
		}
		break;
	case Opt_user:
		kfree(ctx->username);
		ctx->username = NULL;
		if (strlen(param->string) == 0) {
			/* null user, ie. anonymous authentication */
			ctx->nullauth = 1;
			break;
		}

		if (strnlen(param->string, CIFS_MAX_USERNAME_LEN) >
		    CIFS_MAX_USERNAME_LEN) {
			pr_warn("username too long\n");
			goto cifs_parse_mount_err;
		}
		ctx->username = kstrdup(param->string, GFP_KERNEL);
		if (ctx->username == NULL) {
			cifs_dbg(VFS, "OOM when copying username string\n");
			goto cifs_parse_mount_err;
		}
		break;
	case Opt_pass:
		kfree(ctx->password);
		ctx->password = NULL;
		if (strlen(param->string) == 0)
			break;

		ctx->password = kstrdup(param->string, GFP_KERNEL);
		if (ctx->password == NULL) {
			cifs_dbg(VFS, "OOM when copying password string\n");
			goto cifs_parse_mount_err;
		}
		break;
	case Opt_ip:
		if (strlen(param->string) == 0) {
			ctx->got_ip = false;
			break;
		}
		if (!cifs_convert_address((struct sockaddr *)&ctx->dstaddr,
					  param->string,
					  strlen(param->string))) {
			pr_err("bad ip= option (%s)\n", param->string);
			goto cifs_parse_mount_err;
		}
		ctx->got_ip = true;
		break;
	case Opt_domain:
		if (strnlen(param->string, CIFS_MAX_DOMAINNAME_LEN)
				== CIFS_MAX_DOMAINNAME_LEN) {
			pr_warn("domain name too long\n");
			goto cifs_parse_mount_err;
		}

		kfree(ctx->domainname);
		ctx->domainname = kstrdup(param->string, GFP_KERNEL);
		if (ctx->domainname == NULL) {
			cifs_dbg(VFS, "OOM when copying domainname string\n");
			goto cifs_parse_mount_err;
		}
		cifs_dbg(FYI, "Domain name set\n");
		break;
	case Opt_srcaddr:
		if (!cifs_convert_address(
				(struct sockaddr *)&ctx->srcaddr,
				param->string, strlen(param->string))) {
			pr_warn("Could not parse srcaddr: %s\n",
				param->string);
			goto cifs_parse_mount_err;
		}
		break;
	case Opt_iocharset:
		if (strnlen(param->string, 1024) >= 65) {
			pr_warn("iocharset name too long\n");
			goto cifs_parse_mount_err;
		}

		if (strncasecmp(param->string, "default", 7) != 0) {
			kfree(ctx->iocharset);
			ctx->iocharset = kstrdup(param->string, GFP_KERNEL);
			if (ctx->iocharset == NULL) {
				cifs_dbg(VFS, "OOM when copying iocharset string\n");
				goto cifs_parse_mount_err;
			}
		}
		/* if iocharset not set then load_nls_default
		 * is used by caller
		 */
		 cifs_dbg(FYI, "iocharset set to %s\n", ctx->iocharset);
		break;
	case Opt_netbiosname:
		memset(ctx->source_rfc1001_name, 0x20,
			RFC1001_NAME_LEN);
		/*
		 * FIXME: are there cases in which a comma can
		 * be valid in workstation netbios name (and
		 * need special handling)?
		 */
		for (i = 0; i < RFC1001_NAME_LEN; i++) {
			/* don't ucase netbiosname for user */
			if (param->string[i] == 0)
				break;
			ctx->source_rfc1001_name[i] = param->string[i];
		}
		/* The string has 16th byte zero still from
		 * set at top of the function
		 */
		if (i == RFC1001_NAME_LEN && param->string[i] != 0)
			pr_warn("netbiosname longer than 15 truncated\n");
		break;
	case Opt_servern:
		/* last byte, type, is 0x20 for servr type */
		memset(ctx->target_rfc1001_name, 0x20,
			RFC1001_NAME_LEN_WITH_NULL);
		/*
		 * BB are there cases in which a comma can be valid in this
		 * workstation netbios name (and need special handling)?
		 */

		/* user or mount helper must uppercase the netbios name */
		for (i = 0; i < 15; i++) {
			if (param->string[i] == 0)
				break;
			ctx->target_rfc1001_name[i] = param->string[i];
		}

		/* The string has 16th byte zero still from set at top of function */
		if (i == RFC1001_NAME_LEN && param->string[i] != 0)
			pr_warn("server netbiosname longer than 15 truncated\n");
		break;
	case Opt_ver:
		/* version of mount userspace tools, not dialect */
		/* If interface changes in mount.cifs bump to new ver */
		if (strncasecmp(param->string, "1", 1) == 0) {
			if (strlen(param->string) > 1) {
				pr_warn("Bad mount helper ver=%s. Did you want SMB1 (CIFS) dialect and mean to type vers=1.0 instead?\n",
					param->string);
				goto cifs_parse_mount_err;
			}
			/* This is the default */
			break;
		}
		/* For all other value, error */
		pr_warn("Invalid mount helper version specified\n");
		goto cifs_parse_mount_err;
	case Opt_vers:
		/* protocol version (dialect) */
		if (cifs_parse_smb_version(param->string, ctx, is_smb3) != 0)
			goto cifs_parse_mount_err;
		ctx->got_version = true;
		break;
	case Opt_sec:
		if (cifs_parse_security_flavors(param->string, ctx) != 0)
			goto cifs_parse_mount_err;
		break;
	case Opt_cache:
		if (cifs_parse_cache_flavor(param->string, ctx) != 0)
			goto cifs_parse_mount_err;
		break;
	case Opt_witness:
#ifndef CONFIG_CIFS_SWN_UPCALL
		cifs_dbg(VFS, "Witness support needs CONFIG_CIFS_SWN_UPCALL config option\n");
			goto cifs_parse_mount_err;
#endif
		ctx->witness = true;
		pr_warn_once("Witness protocol support is experimental\n");
		break;
	case Opt_rootfs:
#ifdef CONFIG_CIFS_ROOT
		ctx->rootfs = true;
#endif
		break;
	case Opt_posixpaths:
		if (result.negated)
			ctx->posix_paths = 0;
		else
			ctx->posix_paths = 1;
		break;
	case Opt_unix:
		if (result.negated)
			ctx->linux_ext = 0;
		else
			ctx->no_linux_ext = 1;
		break;
	case Opt_nocase:
		ctx->nocase = 1;
		break;
	case Opt_brl:
		if (result.negated) {
			/*
			 * turn off mandatory locking in mode
			 * if remote locking is turned off since the
			 * local vfs will do advisory
			 */
			if (ctx->file_mode ==
				(S_IALLUGO & ~(S_ISUID | S_IXGRP)))
				ctx->file_mode = S_IALLUGO;
			ctx->nobrl =  1;
		} else
			ctx->nobrl =  0;
		break;
	case Opt_handlecache:
		if (result.negated)
			ctx->nohandlecache = 1;
		else
			ctx->nohandlecache = 0;
		break;
	case Opt_forcemandatorylock:
		ctx->mand_lock = 1;
		break;
	case Opt_setuids:
		ctx->setuids = result.negated;
		break;
	case Opt_intr:
		ctx->intr = !result.negated;
		break;
	case Opt_setuidfromacl:
		ctx->setuidfromacl = 1;
		break;
	case Opt_strictsync:
		ctx->nostrictsync = result.negated;
		break;
	case Opt_serverino:
		ctx->server_ino = !result.negated;
		break;
	case Opt_rwpidforward:
		ctx->rwpidforward = 1;
		break;
	case Opt_modesid:
		ctx->mode_ace = 1;
		break;
	case Opt_cifsacl:
		ctx->cifs_acl = !result.negated;
		break;
	case Opt_acl:
		ctx->no_psx_acl = result.negated;
		break;
	case Opt_locallease:
		ctx->local_lease = 1;
		break;
	case Opt_sign:
		ctx->sign = true;
		break;
	case Opt_ignore_signature:
		ctx->sign = true;
		ctx->ignore_signature = true;
		break;
	case Opt_seal:
		/* we do not do the following in secFlags because seal
		 * is a per tree connection (mount) not a per socket
		 * or per-smb connection option in the protocol
		 * vol->secFlg |= CIFSSEC_MUST_SEAL;
		 */
		ctx->seal = 1;
		break;
	case Opt_noac:
		pr_warn("Mount option noac not supported. Instead set /proc/fs/cifs/LookupCacheEnabled to 0\n");
		break;
	case Opt_fsc:
#ifndef CONFIG_CIFS_FSCACHE
		cifs_dbg(VFS, "FS-Cache support needs CONFIG_CIFS_FSCACHE kernel config option set\n");
		goto cifs_parse_mount_err;
#endif
		ctx->fsc = true;
		break;
	case Opt_mfsymlinks:
		ctx->mfsymlinks = true;
		break;
	case Opt_multiuser:
		ctx->multiuser = true;
		break;
	case Opt_sloppy:
		ctx->sloppy = true;
		break;
	case Opt_nosharesock:
		ctx->nosharesock = true;
		break;
	case Opt_persistent:
		if (result.negated) {
			ctx->nopersistent = true;
			if (ctx->persistent) {
				cifs_dbg(VFS,
				  "persistenthandles mount options conflict\n");
				goto cifs_parse_mount_err;
			}
		} else {
			ctx->persistent = true;
			if ((ctx->nopersistent) || (ctx->resilient)) {
				cifs_dbg(VFS,
				  "persistenthandles mount options conflict\n");
				goto cifs_parse_mount_err;
			}
		}
		break;
	case Opt_resilient:
		if (result.negated) {
			ctx->resilient = false; /* already the default */
		} else {
			ctx->resilient = true;
			if (ctx->persistent) {
				cifs_dbg(VFS,
				  "persistenthandles mount options conflict\n");
				goto cifs_parse_mount_err;
			}
		}
		break;
	case Opt_domainauto:
		ctx->domainauto = true;
		break;
	case Opt_rdma:
		ctx->rdma = true;
		break;
	}
	/* case Opt_ignore: - is ignored as expected ... */

	return 0;

 cifs_parse_mount_err:
	return 1;
}

int smb3_init_fs_context(struct fs_context *fc)
{
	struct smb3_fs_context *ctx;
	char *nodename = utsname()->nodename;
	int i;

	ctx = kzalloc(sizeof(struct smb3_fs_context), GFP_KERNEL);
	if (unlikely(!ctx))
		return -ENOMEM;

	/*
	 * does not have to be perfect mapping since field is
	 * informational, only used for servers that do not support
	 * port 445 and it can be overridden at mount time
	 */
	memset(ctx->source_rfc1001_name, 0x20, RFC1001_NAME_LEN);
	for (i = 0; i < strnlen(nodename, RFC1001_NAME_LEN); i++)
		ctx->source_rfc1001_name[i] = toupper(nodename[i]);

	ctx->source_rfc1001_name[RFC1001_NAME_LEN] = 0;
	/*
	 * null target name indicates to use *SMBSERVR default called name
	 *  if we end up sending RFC1001 session initialize
	 */
	ctx->target_rfc1001_name[0] = 0;
	ctx->cred_uid = current_uid();
	ctx->linux_uid = current_uid();
	ctx->linux_gid = current_gid();
	ctx->bsize = 1024 * 1024; /* can improve cp performance significantly */

	/*
	 * default to SFM style remapping of seven reserved characters
	 * unless user overrides it or we negotiate CIFS POSIX where
	 * it is unnecessary.  Can not simultaneously use more than one mapping
	 * since then readdir could list files that open could not open
	 */
	ctx->remap = true;

	/* default to only allowing write access to owner of the mount */
	ctx->dir_mode = ctx->file_mode = S_IRUGO | S_IXUGO | S_IWUSR;

	/* ctx->retry default is 0 (i.e. "soft" limited retry not hard retry) */
	/* default is always to request posix paths. */
	ctx->posix_paths = 1;
	/* default to using server inode numbers where available */
	ctx->server_ino = 1;

	/* default is to use strict cifs caching semantics */
	ctx->strict_io = true;

	ctx->actimeo = CIFS_DEF_ACTIMEO;

	/* Most clients set timeout to 0, allows server to use its default */
	ctx->handle_timeout = 0; /* See MS-SMB2 spec section 2.2.14.2.12 */

	/* offer SMB2.1 and later (SMB3 etc). Secure and widely accepted */
	ctx->ops = &smb30_operations;
	ctx->vals = &smbdefault_values;

	ctx->echo_interval = SMB_ECHO_INTERVAL_DEFAULT;

	/* default to no multichannel (single server connection) */
	ctx->multichannel = false;
	ctx->max_channels = 1;

	ctx->backupuid_specified = false; /* no backup intent for a user */
	ctx->backupgid_specified = false; /* no backup intent for a group */

/*
 *	short int override_uid = -1;
 *	short int override_gid = -1;
 *	char *nodename = strdup(utsname()->nodename);
 *	struct sockaddr *dstaddr = (struct sockaddr *)&vol->dstaddr;
 */

	fc->fs_private = ctx;
	fc->ops = &smb3_fs_context_ops;
	return 0;
}

void
smb3_cleanup_fs_context_contents(struct smb3_fs_context *ctx)
{
	if (ctx == NULL)
		return;

	/*
	 * Make sure this stays in sync with smb3_fs_context_dup()
	 */
	kfree(ctx->mount_options);
	ctx->mount_options = NULL;
	kfree(ctx->username);
	ctx->username = NULL;
	kfree_sensitive(ctx->password);
	ctx->password = NULL;
	kfree(ctx->UNC);
	ctx->UNC = NULL;
	kfree(ctx->domainname);
	ctx->domainname = NULL;
	kfree(ctx->nodename);
	ctx->nodename = NULL;
	kfree(ctx->iocharset);
	ctx->iocharset = NULL;
	kfree(ctx->prepath);
	ctx->prepath = NULL;
}

void
smb3_cleanup_fs_context(struct smb3_fs_context *ctx)
{
	if (!ctx)
		return;
	smb3_cleanup_fs_context_contents(ctx);
	kfree(ctx);
}

void smb3_update_mnt_flags(struct cifs_sb_info *cifs_sb)
{
	struct smb3_fs_context *ctx = cifs_sb->ctx;

	if (ctx->nodfs)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_DFS;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_NO_DFS;

	if (ctx->noperm)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_PERM;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_NO_PERM;

	if (ctx->setuids)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_SET_UID;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_SET_UID;

	if (ctx->setuidfromacl)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_UID_FROM_ACL;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_UID_FROM_ACL;

	if (ctx->server_ino)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_SERVER_INUM;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_SERVER_INUM;

	if (ctx->remap)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_MAP_SFM_CHR;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_MAP_SFM_CHR;

	if (ctx->sfu_remap)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_MAP_SPECIAL_CHR;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_MAP_SPECIAL_CHR;

	if (ctx->no_xattr)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_XATTR;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_NO_XATTR;

	if (ctx->sfu_emul)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_UNX_EMUL;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_UNX_EMUL;

	if (ctx->nobrl)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_BRL;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_NO_BRL;

	if (ctx->nohandlecache)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_HANDLE_CACHE;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_NO_HANDLE_CACHE;

	if (ctx->nostrictsync)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NOSSYNC;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_NOSSYNC;

	if (ctx->mand_lock)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NOPOSIXBRL;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_NOPOSIXBRL;

	if (ctx->rwpidforward)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_RWPIDFORWARD;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_RWPIDFORWARD;

	if (ctx->mode_ace)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_MODE_FROM_SID;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_MODE_FROM_SID;

	if (ctx->cifs_acl)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_CIFS_ACL;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_CIFS_ACL;

	if (ctx->backupuid_specified)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_CIFS_BACKUPUID;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_CIFS_BACKUPUID;

	if (ctx->backupgid_specified)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_CIFS_BACKUPGID;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_CIFS_BACKUPGID;

	if (ctx->override_uid)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_OVERR_UID;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_OVERR_UID;

	if (ctx->override_gid)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_OVERR_GID;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_OVERR_GID;

	if (ctx->dynperm)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_DYNPERM;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_DYNPERM;

	if (ctx->fsc)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_FSCACHE;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_FSCACHE;

	if (ctx->multiuser)
		cifs_sb->mnt_cifs_flags |= (CIFS_MOUNT_MULTIUSER |
					    CIFS_MOUNT_NO_PERM);
	else
		cifs_sb->mnt_cifs_flags &= ~(CIFS_MOUNT_MULTIUSER |
					     CIFS_MOUNT_NO_PERM);

	if (ctx->strict_io)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_STRICT_IO;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_STRICT_IO;

	if (ctx->direct_io)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_DIRECT_IO;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_DIRECT_IO;

	if (ctx->mfsymlinks)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_MF_SYMLINKS;
	else
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_MF_SYMLINKS;
	if (ctx->mfsymlinks) {
		if (ctx->sfu_emul) {
			/*
			 * Our SFU ("Services for Unix" emulation does not allow
			 * creating symlinks but does allow reading existing SFU
			 * symlinks (it does allow both creating and reading SFU
			 * style mknod and FIFOs though). When "mfsymlinks" and
			 * "sfu" are both enabled at the same time, it allows
			 * reading both types of symlinks, but will only create
			 * them with mfsymlinks format. This allows better
			 * Apple compatibility (probably better for Samba too)
			 * while still recognizing old Windows style symlinks.
			 */
			cifs_dbg(VFS, "mount options mfsymlinks and sfu both enabled\n");
		}
	}

	return;
}
