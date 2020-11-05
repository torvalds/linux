// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2020, Microsoft Corporation.
 *
 *   Author(s): Steve French <stfrench@microsoft.com>
 *              David Howells <dhowells@redhat.com>
 */

#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
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

int
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

int cifs_parse_security_flavors(char *value, struct smb3_fs_context *ctx)
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
			cifs_cleanup_volume_info_contents(new_ctx);	\
			return -ENOMEM;					\
		}							\
	}								\
} while (0)

int
smb3_fs_context_dup(struct smb3_fs_context *new_ctx, struct smb3_fs_context *ctx)
{
	int rc = 0;

	memcpy(new_ctx, ctx, sizeof(*ctx));
	new_ctx->prepath = NULL;
	new_ctx->local_nls = NULL;
	new_ctx->nodename = NULL;
	new_ctx->username = NULL;
	new_ctx->password = NULL;
	new_ctx->domainname = NULL;
	new_ctx->UNC = NULL;
	new_ctx->iocharset = NULL;

	/*
	 * Make sure to stay in sync with cifs_cleanup_volume_info_contents()
	 */
	DUP_CTX_STR(prepath);
	DUP_CTX_STR(username);
	DUP_CTX_STR(password);
	DUP_CTX_STR(UNC);
	DUP_CTX_STR(domainname);
	DUP_CTX_STR(nodename);
	DUP_CTX_STR(iocharset);

	return rc;
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
