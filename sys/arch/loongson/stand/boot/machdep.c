/*	$OpenBSD: machdep.c,v 1.9 2023/02/23 19:48:22 miod Exp $	*/

/*
 * Copyright (c) 2010 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Copyright (c) 1998-2004 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <lib/libkern/libkern.h>
#include "libsa.h"
#include <machine/cpu.h>
#include <machine/pmon.h>
#include <stand/boot/cmd.h>

void	gdium_abort(void);
int	is_gdium;
int	boot_rd;

extern int bootprompt;

/*
 * Configuration and device path aerobics
 */

/*
 * Return the default boot device.
 */
void
devboot(dev_t dev, char *path)
{
	const char *bootpath = NULL;
	size_t bootpathlen = 0;	/* gcc -Wall */
	const char *tmp;
	int i;

	/*
	 * If we are booting the initrd image, things are easy...
	 */

	if (dev != 0) {
		strlcpy(path, "rd0a", BOOTDEVLEN);
		return;
	}

	/*
	 * First, try to figure where we have been loaded from; we'll assume
	 * the default device to load the kernel from is the same.
	 *
	 * We may have been loaded in three different ways:
	 * - automatic load from `al' environment variable (similar to a
	 *   `load' and `go' sequence).
	 * - manual `boot' command, with path on the commandline.
	 * - manual `load' and `go' commands, with no path on the commandline.
	 */

	if (pmon_argc > 0) {
		tmp = (const char *)pmon_getarg(0);
		if (tmp[0] != 'g') {
			/* manual load */
			for (i = 1; i < pmon_argc; i++) {
				tmp = (const char *)pmon_getarg(i);
				if (tmp[0] != '-') {
					bootpath = tmp;
					break;
				}
			}
		} else {
			/* possible automatic load */
			bootpath = pmon_getenv("al");
		}
	}

	/*
	 * If the bootblocks have been loaded from the network,
	 * use the default disk.
	 */

	if (bootpath != NULL && strncmp(bootpath, "tftp://", 7) == 0)
		bootpath = NULL;

	/*
	 * Now extract the device name from the bootpath.
	 */

	if (bootpath != NULL) {
		tmp = strchr(bootpath, '@');
		if (tmp == NULL) {
			bootpath = NULL;
		} else {
			bootpath = tmp + 1;
			tmp = strchr(bootpath, '/');
			if (tmp == NULL) {
				bootpath = NULL;
			} else {
				bootpathlen = tmp - bootpath;
			}
		}
	}
		
	if (bootpath != NULL && bootpathlen >= 3) {
		if (bootpathlen >= BOOTDEVLEN)
			bootpathlen = BOOTDEVLEN - 1;
		strncpy(path, bootpath, bootpathlen);
		path[bootpathlen] = '\0';
		/* only add a partition letter if there is none */
		if (bootpath[bootpathlen - 1] >= '0' &&
		    bootpath[bootpathlen - 1] <= '9')
			strlcat(path, "a", BOOTDEVLEN);
	} else {
		strlcpy(path, "wd0a", BOOTDEVLEN);
	}
}

/*
 * Ugly (lack of) clock routines
 */

time_t
getsecs()
{
	return 0;
}

/*
 * Initialization
 */

void
machdep()
{
	const char *envvar;

	/*
	 * Since we can't have non-blocking input, we will try to
	 * autoload the kernel pointed to by the `bsd' environment
	 * variable, and fallback to interactive mode if the variable
	 * is empty or the load fails.
	 */

	if (boot_rd == 0) {
		envvar = pmon_getenv("bsd");
		if (envvar != NULL) {
			bootprompt = 0;
			kernelfile = (char *)envvar;
		} else {
			if (is_gdium)
				gdium_abort();
		}
	}
}

int
main()
{
	const char *envvar;

	cninit();

	/*
	 * Figure out whether we are running on a Gdium system, which
	 * has an horribly castrated PMON. If we do, the best we can do
	 * is boot an initrd image.
	 */
	envvar = pmon_getenv("Version");
	if (envvar != NULL && strncmp(envvar, "Gdium", 5) == 0)
		is_gdium = 1;

	/*
	 * Check if we have a valid initrd loaded.
	 */

	envvar = pmon_getenv("rd");
	if (envvar != NULL && *envvar != '\0')
		boot_rd = rd_isvalid();

	if (boot_rd != 0)
		bootprompt = 0;

	boot(boot_rd);
	return 0;
}

void
gdium_abort()
{
	/* Here's a nickel, kid.  Get yourself a better firmware */
	printf("\n\nSorry, OpenBSD boot blocks do not work on Gdium, "
	    "because of dire firmware limitations.\n"
	    "Also, the firmware has reset the USB controller so you "
	    "will need to power cycle.\n"
	    "We would apologize for this inconvenience, but we have "
	    "no control about the firmware of your machine.\n\n");
	rd_invalidate();
	_rtt();
}
