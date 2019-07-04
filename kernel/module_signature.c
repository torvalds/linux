// SPDX-License-Identifier: GPL-2.0+
/*
 * Module signature checker
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/module_signature.h>
#include <asm/byteorder.h>

/**
 * mod_check_sig - check that the given signature is sane
 *
 * @ms:		Signature to check.
 * @file_len:	Size of the file to which @ms is appended.
 * @name:	What is being checked. Used for error messages.
 */
int mod_check_sig(const struct module_signature *ms, size_t file_len,
		  const char *name)
{
	if (be32_to_cpu(ms->sig_len) >= file_len - sizeof(*ms))
		return -EBADMSG;

	if (ms->id_type != PKEY_ID_PKCS7) {
		pr_err("%s: Module is not signed with expected PKCS#7 message\n",
		       name);
		return -ENOPKG;
	}

	if (ms->algo != 0 ||
	    ms->hash != 0 ||
	    ms->signer_len != 0 ||
	    ms->key_id_len != 0 ||
	    ms->__pad[0] != 0 ||
	    ms->__pad[1] != 0 ||
	    ms->__pad[2] != 0) {
		pr_err("%s: PKCS#7 signature info has unexpected non-zero params\n",
		       name);
		return -EBADMSG;
	}

	return 0;
}
