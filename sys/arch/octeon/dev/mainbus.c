/*	$OpenBSD: mainbus.c,v 1.13 2020/08/28 15:07:55 visa Exp $ */

/*
 * Copyright (c) 2001-2003 Opsycon AB  (www.opsycon.se / www.opsycon.com)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <dev/ofw/openfirm.h>
#include <machine/autoconf.h>
#include <machine/octeonvar.h>

#include <octeon/dev/cn30xxcorereg.h>

int	mainbus_match(struct device *, void *, void *);
void	mainbus_attach(struct device *, struct device *, void *);
int	mainbus_print(void *, const char *);

const struct cfattach mainbus_ca = {
	sizeof(struct device), mainbus_match, mainbus_attach
};

struct cfdriver mainbus_cd = {
	NULL, "mainbus", DV_DULL
};

int
mainbus_match(struct device *parent, void *cfdata, void *aux)
{
	static int mainbus_attached = 0;

	if (mainbus_attached != 0)
		return 0;

	return mainbus_attached = 1;
}

void
mainbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct cpu_attach_args caa;
	char model[128];
	int len, node;
#ifdef MULTIPROCESSOR
	struct cpu_hwinfo hw;
	int cpuid;
#endif

	printf(": board %u rev %u.%u", octeon_boot_info->board_type,
	    octeon_boot_info->board_rev_major,
	    octeon_boot_info->board_rev_minor);

	node = OF_peer(0);
	if (node != 0) {
		len = OF_getprop(node, "model", model, sizeof(model));
		if (len > 0) {
			printf(", model %s", model);
			hw_prod = malloc(len, M_DEVBUF, M_NOWAIT);
			if (hw_prod != NULL)
				strlcpy(hw_prod, model, len);
		}
	}

	printf("\n");

	bzero(&caa, sizeof caa);
	caa.caa_maa.maa_name = "cpu";
	caa.caa_hw = &bootcpu_hwinfo;
	config_found(self, &caa, mainbus_print);

#ifdef MULTIPROCESSOR
	for (cpuid = 1; cpuid < MAXCPUS &&
	    octeon_boot_info->core_mask & (1 << cpuid); cpuid++) {
		bcopy(&bootcpu_hwinfo, &hw, sizeof(struct cpu_hwinfo));
		caa.caa_hw = &hw;
		config_found(self, &caa, mainbus_print);
	}
#endif

	caa.caa_maa.maa_name = "clock";
	config_found(self, &caa.caa_maa, mainbus_print);

	/* on-board I/O */
	caa.caa_maa.maa_name = "iobus";
	config_found(self, &caa.caa_maa, mainbus_print);

	caa.caa_maa.maa_name = "octrtc";
	config_found(self, &caa.caa_maa, mainbus_print);

#ifdef CRYPTO
	if (!ISSET(octeon_get_cvmctl(), COP_0_CVMCTL_NOCRYPTO)) {
		caa.caa_maa.maa_name = "octcrypto";
		config_found(self, &caa.caa_maa, mainbus_print);
	}
#endif
}

int
mainbus_print(void *aux, const char *pnp)
{
	return pnp != NULL ? QUIET : UNCONF;
}
