/* $OpenBSD: tga_conf.c,v 1.6 2025/06/28 16:04:10 miod Exp $ */
/* $NetBSD: tga_conf.c,v 1.5 2001/11/13 07:48:49 lukem Exp $ */

/*
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/device.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/tgareg.h>
#include <dev/pci/tgavar.h>

#include <dev/ic/bt485var.h>
#include <dev/ic/bt463var.h>
#include <dev/ic/ibm561var.h>

#undef KB
#define KB		* 1024
#undef MB
#define	MB		* 1024 * 1024

static const struct tga_conf tga_configs[TGA_TYPE_UNKNOWN] = {
	/* TGA_TYPE_T8_01 */
	{
		"T8-01",
		bt485_funcs,
		8,
		4 MB,
		2 KB,
		1,	{  2 MB,     0 },	{ 1 MB,    0 },
		0,	{     0,     0 },	{    0,    0 },
	},
	/* TGA_TYPE_T8_02 */
	{
		"T8-02",
		bt485_funcs,
		8,
		4 MB,
		4 KB,
		1,	{  2 MB,     0 },	{ 2 MB,    0 },
		0,	{     0,     0 },	{    0,    0 },
	},
	/* TGA_TYPE_T8_22 */
	{
		"T8-22",
		bt485_funcs,
		8,
		8 MB,
		4 KB,
		1,	{  4 MB,     0 },	{ 2 MB,    0 },
		1,	{  6 MB,     0 },	{ 2 MB,    0 },
	},
	/* TGA_TYPE_T8_44 */
	{
		"T8-44",
		bt485_funcs,
		8,
		16 MB,
		4 KB,
		2,	{  8 MB, 12 MB },	{ 2 MB, 2 MB },
		2,	{ 10 MB, 14 MB },	{ 2 MB, 2 MB },
	},
	/* TGA_TYPE_T32_04 */
	{
		"T32-04",
		bt463_funcs,
		32,
		16 MB,
		8 KB,
		1,	{  8 MB,     0 },	{ 4 MB,    0 },
		0,	{     0,     0 },	{    0,    0 },
	},
	/* TGA_TYPE_T32_08 */
	{
		"T32-08",
		bt463_funcs,
		32,
		16 MB,
		16 KB,
		1,	{  8 MB,    0 },	{ 8 MB,    0 },
		0,	{     0,    0 },	{    0,    0 },
	},
	/* TGA_TYPE_T32_88 */
	{
		"T32-88",
		bt463_funcs,
		32,
		32 MB,
		16 KB,
		1,	{ 16 MB,    0 },	{ 8 MB,    0 },
		1,	{ 24 MB,    0 },	{ 8 MB,    0 },
	},
 	/* TGA_TYPE_POWERSTORM_4D20 */
 	/* XXX: These numbers may be incorrect */
 	{
 		"PS4d20",
 		ibm561_funcs,
 		32,
 		32 MB,
 		16 KB,
 		1,	{ 16 MB,    0 },	{ 8 MB,    0 },
 		1,	{ 24 MB,    0 },	{ 8 MB,    0 },
	},
};

#undef KB
#undef MB

int
tga_identify(struct tga_devconfig *dc)
{
	int type;
	int gder;
 	int grev;
	int deep, addrmask, wide;
 	int tga2;

	gder = TGARREG(dc, TGA_REG_GDER);
 	grev = TGARREG(dc, TGA_REG_GREV);

	deep = (gder & 0x1) != 0; /* XXX */
	addrmask = (gder >> 2) & 0x7; /* XXX */
	wide = (gder & 0x200) == 0; /* XXX */
 	tga2 = (grev & 0x20) != 0;


	type = TGA_TYPE_UNKNOWN;

	if (!deep) {
		/* 8bpp frame buffer */

		if (addrmask == 0x0) {
			/* 4MB core map; T8-01 or T8-02 */

			if (!wide)
				type = TGA_TYPE_T8_01;
			else
				type = TGA_TYPE_T8_02;
		} else if (addrmask == 0x1) {
			/* 8MB core map; T8-22 */

			if (wide)			/* sanity */
				type = TGA_TYPE_T8_22;
		} else if (addrmask == 0x3) {
			/* 16MB core map; T8-44 */

			if (wide)			/* sanity */
				type = TGA_TYPE_T8_44;
		}
	} else {
		/* 32bpp frame buffer */
 		if (addrmask == 0x00 && tga2 && wide) {
 			/* My PowerStorm 4d20 shows up this way? */
 			type = TGA_TYPE_POWERSTORM_4D20;
 		}

		if (addrmask == 0x3) {
			/* 16MB core map; T32-04 or T32-08 */

			if (!wide)
				type = TGA_TYPE_T32_04;
			else
				type = TGA_TYPE_T32_08;
		} else if (addrmask == 0x7) {
			/* 32MB core map; T32-88 */

 			if (wide && !tga2)			/* sanity */
				type = TGA_TYPE_T32_88;
		}
	}

	return (type);
}

const struct tga_conf *
tga_getconf(int type)
{

	if (type >= 0 && type < TGA_TYPE_UNKNOWN)
		return &tga_configs[type];

	return (NULL);
}
