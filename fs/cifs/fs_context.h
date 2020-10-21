/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2020, Microsoft Corporation.
 *
 *   Author(s): Steve French <stfrench@microsoft.com>
 *              David Howells <dhowells@redhat.com>
 */

#ifndef _FS_CONTEXT_H
#define _FS_CONTEXT_H

#include <linux/parser.h>
#include "cifsglob.h"

enum cifs_sec_param {
	Opt_sec_krb5,
	Opt_sec_krb5i,
	Opt_sec_krb5p,
	Opt_sec_ntlmsspi,
	Opt_sec_ntlmssp,
	Opt_ntlm,
	Opt_sec_ntlmi,
	Opt_sec_ntlmv2,
	Opt_sec_ntlmv2i,
	Opt_sec_lanman,
	Opt_sec_none,

	Opt_sec_err
};

int cifs_parse_security_flavors(char *value, struct smb_vol *vol);

#endif
