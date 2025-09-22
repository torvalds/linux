/* $OpenBSD: mainbus.c,v 1.18 2025/06/26 20:28:07 miod Exp $ */
/* $NetBSD: mainbus.c,v 1.2 2000/01/07 05:13:08 nisimura Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Tohru Nishimura.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/board.h>
#include <machine/cmmu.h>
#include <machine/cpu.h>

#include "cbus.h"
#include "lcd.h"
#include "xp.h"

static const struct mainbus_attach_args devs[] = {
	{ "clock", 0x45000000, 6,  LUNA_88K|LUNA_88K2 }, /* Mostek/Dallas TimeKeeper */
#if NLCD > 0
	{ "lcd",   0x4d000000, -1, LUNA_88K|LUNA_88K2 }, /* Sharp LM16X212 LCD module */
#endif
	{ "le",	   0xf1000000, 4,  LUNA_88K|LUNA_88K2 }, /* Am7990 */
	{ "sio",   0x51000000, 5,  LUNA_88K|LUNA_88K2 }, /* uPD7201A */
#if NXP > 0
	{ "xp",    0x71000000, 1,  LUNA_88K|LUNA_88K2 }, /* HD647180XP */
#endif
	{ "fb",	   0xc1100000, -1, LUNA_88K|LUNA_88K2 }, /* BrookTree RAMDAC */
	{ "spc",   0xe1000000, 3,  LUNA_88K|LUNA_88K2 }, /* MB89352 */
	{ "spc",   0xe1000040, 3,  LUNA_88K2 },	/* ditto, LUNA-88K2 only */
#if NCBUS > 0
	{ "cbus",  0x91000000, 4,  LUNA_88K2 },	/* PC-9801 extension slot */
#endif
};

void	mainbus_attach(struct device *, struct device *, void *);
int	mainbus_match(struct device *, void *, void *);
int	mainbus_print(void *, const char *);
#ifdef MULTIPROCESSOR
extern void	cpu_setup_secondary_processors(void);	/* in machdep.c */
#endif

const struct cfattach mainbus_ca = {
	sizeof(struct device), mainbus_match, mainbus_attach
};

struct cfdriver mainbus_cd = {
	NULL, "mainbus", DV_DULL,
};

int
mainbus_match(struct device *parent, void *cf, void *args)
{
	static int mainbus_matched;

	if (mainbus_matched)
		return (0);

	return ((mainbus_matched = 1));
}

void
mainbus_attach(struct device *parent, struct device *self, void *args)
{
	int i;
	extern int machtype;
	extern char cpu_model[];

	printf(": %s\n", cpu_model);

	/*
	 * Display cpu/mmu details for the main processor.
	 */
	cpu_configuration_print(1);

#ifdef MULTIPROCESSOR
	/*
	 * Let secondary processors initialize further and print
	 * their configuration information now.
	 */
	cpu_setup_secondary_processors();
#endif

	for (i = 0; i < sizeof(devs)/sizeof(devs[0]); i++)
		if (devs[i].ma_machine & machtype)
			config_found(self, (void *)&devs[i], mainbus_print);
}

int
mainbus_print(void *aux, const char *pnp)
{
	struct mainbus_attach_args *ma = aux;

	if (pnp)
		printf("%s at %s", ma->ma_name, pnp);

	return (UNCONF);
}
