/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Ryuichiro Imura
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/iconv.h>
#include <sys/sysctl.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int
kiconv_lookupconv(const char *drvname)
{
	size_t size;

	if (sysctlbyname("kern.iconv.drvlist", NULL, &size, NULL, 0) == -1)
		return (errno);
	if (size > 0) {
		char *drivers, *drvp;

		drivers = malloc(size);
		if (drivers == NULL)
			return (ENOMEM);
		if (sysctlbyname("kern.iconv.drvlist", drivers, &size, NULL, 0) == -1) {
			free(drivers);
			return (errno);
		}
		for (drvp = drivers; *drvp != '\0'; drvp += strlen(drvp) + 1)
			if (strcmp(drvp, drvname) == 0) {
				free(drivers);
				return (0);
			}
	}
	return (ENOENT);
}

int
kiconv_lookupcs(const char *tocode, const char *fromcode)
{
	size_t i, size;
	struct iconv_cspair_info *csi, *csip;

	if (sysctlbyname("kern.iconv.cslist", NULL, &size, NULL, 0) == -1)
		return (errno);
	if (size > 0) {
		csi = malloc(size);
		if (csi == NULL)
			return (ENOMEM);
		if (sysctlbyname("kern.iconv.cslist", csi, &size, NULL, 0) == -1) {
			free(csi);
			return (errno);
		}
		for (i = 0, csip = csi; i < (size/sizeof(*csi)); i++, csip++){
			if (strcmp(csip->cs_to, tocode) == 0 &&
			    strcmp(csip->cs_from, fromcode) == 0) {
				free(csi);
				return (0);
			}
		}
	}
	return (ENOENT);
}
