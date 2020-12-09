// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2020, Microsoft Corporation.
 *
 *   Author(s): Steve French <stfrench@microsoft.com>
 *              David Howells <dhowells@redhat.com>
 */

#include "cifsglob.h"
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
cifs_parse_smb_version(char *value, struct smb_vol *vol, bool is_smb3)
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
		vol->ops = &smb1_operations;
		vol->vals = &smb1_values;
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
		vol->ops = &smb20_operations;
		vol->vals = &smb20_values;
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
		vol->ops = &smb21_operations;
		vol->vals = &smb21_values;
		break;
	case Smb_30:
		vol->ops = &smb30_operations;
		vol->vals = &smb30_values;
		break;
	case Smb_302:
		vol->ops = &smb30_operations; /* currently identical with 3.0 */
		vol->vals = &smb302_values;
		break;
	case Smb_311:
		vol->ops = &smb311_operations;
		vol->vals = &smb311_values;
		break;
	case Smb_3any:
		vol->ops = &smb30_operations; /* currently identical with 3.0 */
		vol->vals = &smb3any_values;
		break;
	case Smb_default:
		vol->ops = &smb30_operations; /* currently identical with 3.0 */
		vol->vals = &smbdefault_values;
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

int cifs_parse_security_flavors(char *value, struct smb_vol *vol)
{

	substring_t args[MAX_OPT_ARGS];

	/*
	 * With mount options, the last one should win. Reset any existing
	 * settings back to default.
	 */
	vol->sectype = Unspecified;
	vol->sign = false;

	switch (match_token(value, cifs_secflavor_tokens, args)) {
	case Opt_sec_krb5p:
		cifs_dbg(VFS, "sec=krb5p is not supported!\n");
		return 1;
	case Opt_sec_krb5i:
		vol->sign = true;
		fallthrough;
	case Opt_sec_krb5:
		vol->sectype = Kerberos;
		break;
	case Opt_sec_ntlmsspi:
		vol->sign = true;
		fallthrough;
	case Opt_sec_ntlmssp:
		vol->sectype = RawNTLMSSP;
		break;
	case Opt_sec_ntlmi:
		vol->sign = true;
		fallthrough;
	case Opt_ntlm:
		vol->sectype = NTLM;
		break;
	case Opt_sec_ntlmv2i:
		vol->sign = true;
		fallthrough;
	case Opt_sec_ntlmv2:
		vol->sectype = NTLMv2;
		break;
#ifdef CONFIG_CIFS_WEAK_PW_HASH
	case Opt_sec_lanman:
		vol->sectype = LANMAN;
		break;
#endif
	case Opt_sec_none:
		vol->nullauth = 1;
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
cifs_parse_cache_flavor(char *value, struct smb_vol *vol)
{
	substring_t args[MAX_OPT_ARGS];

	switch (match_token(value, cifs_cacheflavor_tokens, args)) {
	case Opt_cache_loose:
		vol->direct_io = false;
		vol->strict_io = false;
		vol->cache_ro = false;
		vol->cache_rw = false;
		break;
	case Opt_cache_strict:
		vol->direct_io = false;
		vol->strict_io = true;
		vol->cache_ro = false;
		vol->cache_rw = false;
		break;
	case Opt_cache_none:
		vol->direct_io = true;
		vol->strict_io = false;
		vol->cache_ro = false;
		vol->cache_rw = false;
		break;
	case Opt_cache_ro:
		vol->direct_io = false;
		vol->strict_io = false;
		vol->cache_ro = true;
		vol->cache_rw = false;
		break;
	case Opt_cache_rw:
		vol->direct_io = false;
		vol->strict_io = false;
		vol->cache_ro = false;
		vol->cache_rw = true;
		break;
	default:
		cifs_dbg(VFS, "bad cache= option: %s\n", value);
		return 1;
	}
	return 0;
}
