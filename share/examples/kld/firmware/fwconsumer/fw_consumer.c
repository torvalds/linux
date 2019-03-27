/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2006, Max Laier <mlaier@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/linker.h>
#include <sys/firmware.h>
#include <sys/proc.h>
#include <sys/module.h>

static const struct firmware *fp;

static int
fw_consumer_modevent(module_t mod, int type, void *unused)
{
	switch (type) {
	case MOD_LOAD:
		fp = firmware_get("beastie");

		if (fp == NULL)
			return (ENOENT);

		if (((const char *)fp->data)[fp->datasize - 1] != '\0') {
			firmware_put(fp, FIRMWARE_UNLOAD);
			return (EINVAL);
		}
		printf("%s", (const char *)fp->data);

		return (0);
	case MOD_UNLOAD:
		printf("Bye!\n");

		if (fp != NULL) {
			printf("%s", (const char *)fp->data);
			firmware_put(fp, FIRMWARE_UNLOAD);
		}

		return (0);
	}
	return (EINVAL);
}

static moduledata_t fw_consumer_mod = {
	"fw_consumer",
	fw_consumer_modevent,
	0
};
DECLARE_MODULE(fw_consumer, fw_consumer_mod, SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(fw_consumer, 1);
MODULE_DEPEND(fw_consumer, firmware, 1, 1, 1);
