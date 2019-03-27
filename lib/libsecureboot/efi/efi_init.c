/*-
 * Copyright (c) 2019 Stormshield.
 * Copyright (c) 2019 Semihalf.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define NEED_BRSSL_H
#include "../libsecureboot-priv.h"
#include <brssl.h>

void
ve_efi_init(void)
{
	br_x509_certificate *xcs;
	hash_data *digests;
	size_t num;
	int result;
	static int once = 0;

	if (once > 0)
		return;

	once = 1;

	result = efi_secure_boot_enabled();
	if (result <= 0)
		return;

	xcs = efi_get_trusted_certs(&num);
	if (num > 0 && xcs != NULL) {
		num = ve_trust_anchors_add(xcs, num);
		free_certificates(xcs, num);
	}
	xcs = efi_get_forbidden_certs(&num);
	if (num > 0 && xcs != NULL) {
		num = ve_forbidden_anchors_add(xcs, num);
		free_certificates(xcs, num);
	}
	digests = efi_get_forbidden_digests(&num);
	if (num > 0 && digests != NULL) {
		ve_forbidden_digest_add(digests, num);
		/*
		 * Don't free the buffors for digests,
		 * since they are shallow copied.
		 */
		xfree(digests);
	}

	return;
}
