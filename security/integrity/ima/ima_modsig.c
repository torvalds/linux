// SPDX-License-Identifier: GPL-2.0+
/*
 * IMA support for appraising module-style appended signatures.
 *
 * Copyright (C) 2019  IBM Corporation
 *
 * Author:
 * Thiago Jung Bauermann <bauerman@linux.ibm.com>
 */

#include <linux/types.h>
#include <linux/module_signature.h>
#include <keys/asymmetric-type.h>
#include <crypto/pkcs7.h>

#include "ima.h"

struct modsig {
	struct pkcs7_message *pkcs7_msg;
};

/**
 * ima_hook_supports_modsig - can the policy allow modsig for this hook?
 *
 * modsig is only supported by hooks using ima_post_read_file(), because only
 * they preload the contents of the file in a buffer. FILE_CHECK does that in
 * some cases, but not when reached from vfs_open(). POLICY_CHECK can support
 * it, but it's not useful in practice because it's a text file so deny.
 */
bool ima_hook_supports_modsig(enum ima_hooks func)
{
	switch (func) {
	case KEXEC_KERNEL_CHECK:
	case KEXEC_INITRAMFS_CHECK:
	case MODULE_CHECK:
		return true;
	default:
		return false;
	}
}

/*
 * ima_read_modsig - Read modsig from buf.
 *
 * Return: 0 on success, error code otherwise.
 */
int ima_read_modsig(enum ima_hooks func, const void *buf, loff_t buf_len,
		    struct modsig **modsig)
{
	const size_t marker_len = strlen(MODULE_SIG_STRING);
	const struct module_signature *sig;
	struct modsig *hdr;
	size_t sig_len;
	const void *p;
	int rc;

	if (buf_len <= marker_len + sizeof(*sig))
		return -ENOENT;

	p = buf + buf_len - marker_len;
	if (memcmp(p, MODULE_SIG_STRING, marker_len))
		return -ENOENT;

	buf_len -= marker_len;
	sig = (const struct module_signature *)(p - sizeof(*sig));

	rc = mod_check_sig(sig, buf_len, func_tokens[func]);
	if (rc)
		return rc;

	sig_len = be32_to_cpu(sig->sig_len);
	buf_len -= sig_len + sizeof(*sig);

	hdr = kmalloc(sizeof(*hdr), GFP_KERNEL);
	if (!hdr)
		return -ENOMEM;

	hdr->pkcs7_msg = pkcs7_parse_message(buf + buf_len, sig_len);
	if (IS_ERR(hdr->pkcs7_msg)) {
		kfree(hdr);
		return PTR_ERR(hdr->pkcs7_msg);
	}

	*modsig = hdr;

	return 0;
}

int ima_modsig_verify(struct key *keyring, const struct modsig *modsig)
{
	return verify_pkcs7_message_sig(NULL, 0, modsig->pkcs7_msg, keyring,
					VERIFYING_MODULE_SIGNATURE, NULL, NULL);
}

void ima_free_modsig(struct modsig *modsig)
{
	if (!modsig)
		return;

	pkcs7_free_message(modsig->pkcs7_msg);
	kfree(modsig);
}
