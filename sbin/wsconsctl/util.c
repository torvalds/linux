/*	$OpenBSD: util.c,v 1.70 2024/01/19 17:51:15 kettenis Exp $ */
/*	$NetBSD: util.c,v 1.8 2000/03/14 08:11:53 sato Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Juergen Hannken-Illjes.
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

#include <sys/ioctl.h>
#include <sys/time.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsksymdef.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "wsconsctl.h"
#include "mousecfg.h"

#define TABLEN(t)		(sizeof(t)/sizeof(t[0]))

extern struct wskbd_map_data kbmap;	/* from keyboard.c */
extern struct wskbd_map_data newkbmap;	/* from map_parse.y */
extern struct wsmouse_calibcoords wmcoords;	/* from mouse.c */

struct nameint {
	int value;
	char *name;
};

static const struct nameint kbtype_tab[] = {
	{ WSKBD_TYPE_LK201,	"lk201" },
	{ WSKBD_TYPE_LK401,	"lk401" },
	{ WSKBD_TYPE_PC_XT,	"pc-xt" },
	{ WSKBD_TYPE_PC_AT,	"pc-at" },
	{ WSKBD_TYPE_USB,	"usb" },
	{ WSKBD_TYPE_NEXT,	"NeXT" },
	{ WSKBD_TYPE_HPC_KBD,	"hpc-kbd" },
	{ WSKBD_TYPE_HPC_BTN,	"hpc-btn" },
	{ WSKBD_TYPE_ADB,	"adb" },
	{ WSKBD_TYPE_SUN,	"sun" },
	{ WSKBD_TYPE_SUN5,	"sun5" },
	{ WSKBD_TYPE_HIL,	"hil" },
	{ WSKBD_TYPE_GSC,	"hp-ps2" },
	{ WSKBD_TYPE_LUNA,	"luna" },
	{ WSKBD_TYPE_ZAURUS,	"zaurus" },
	{ WSKBD_TYPE_DOMAIN,	"domain" },
	{ WSKBD_TYPE_BLUETOOTH,	"bluetooth" },
	{ WSKBD_TYPE_KPC,	"kpc" },
	{ WSKBD_TYPE_SGI,	"sgi" },
};

static const struct nameint mstype_tab[] = {
	{ WSMOUSE_TYPE_VSXXX,	"dec-tc" },
	{ WSMOUSE_TYPE_PS2,	"ps2" },
	{ WSMOUSE_TYPE_USB,	"usb" },
	{ WSMOUSE_TYPE_LMS,	"lms" },
	{ WSMOUSE_TYPE_MMS,	"mms" },
	{ WSMOUSE_TYPE_TPANEL,	"touch-panel" },
	{ WSMOUSE_TYPE_NEXT,	"NeXT" },
	{ WSMOUSE_TYPE_ARCHIMEDES, "archimedes" },
	{ WSMOUSE_TYPE_ADB,	"adb" },
	{ WSMOUSE_TYPE_HIL,	"hil" },
	{ WSMOUSE_TYPE_LUNA,	"luna" },
	{ WSMOUSE_TYPE_DOMAIN,	"domain" },
	{ WSMOUSE_TYPE_BLUETOOTH, "bluetooth" },
	{ WSMOUSE_TYPE_SUN,	"sun" },
	{ WSMOUSE_TYPE_SYNAPTICS, "synaptics" },
	{ WSMOUSE_TYPE_ALPS,	"alps" },
	{ WSMOUSE_TYPE_SGI,	"sgi" },
	{ WSMOUSE_TYPE_ELANTECH, "elantech" },
	{ WSMOUSE_TYPE_SYNAP_SBTN, "synaptics" },
	{ WSMOUSE_TYPE_TOUCHPAD, "touchpad" },
};

static const struct nameint dpytype_tab[] = {
	{ WSDISPLAY_TYPE_UNKNOWN,	"unknown" },
	{ WSDISPLAY_TYPE_PM_MONO,	"dec-pm-mono" },
	{ WSDISPLAY_TYPE_PM_COLOR,	"dec-pm-color" },
	{ WSDISPLAY_TYPE_CFB,		"dec-cfb" },
	{ WSDISPLAY_TYPE_XCFB,		"dec-xcfb" },
	{ WSDISPLAY_TYPE_MFB,		"dec-mfb" },
	{ WSDISPLAY_TYPE_SFB,		"dec-sfb" },
	{ WSDISPLAY_TYPE_ISAVGA,	"vga-isa" },
	{ WSDISPLAY_TYPE_PCIVGA,	"vga-pci" },
	{ WSDISPLAY_TYPE_TGA,		"dec-tga-pci" },
	{ WSDISPLAY_TYPE_SFBP,		"dec-sfb+" },
	{ WSDISPLAY_TYPE_PCIMISC,	"generic-pci" },
	{ WSDISPLAY_TYPE_NEXTMONO,	"next-mono" },
	{ WSDISPLAY_TYPE_PX,		"dec-px" },
	{ WSDISPLAY_TYPE_PXG,		"dec-pxg" },
	{ WSDISPLAY_TYPE_TX,		"dec-tx" },
	{ WSDISPLAY_TYPE_HPCFB,		"generic-hpc" },
	{ WSDISPLAY_TYPE_VIDC,		"arm-vidc" },
	{ WSDISPLAY_TYPE_SPX,		"dec-spx" },
	{ WSDISPLAY_TYPE_GPX,		"dec-gpx" },
	{ WSDISPLAY_TYPE_LCG,		"dec-lcg" },
	{ WSDISPLAY_TYPE_VAX_MONO,	"dec-mono" },
	{ WSDISPLAY_TYPE_SB_P9100,	"p9100" },
	{ WSDISPLAY_TYPE_EGA,		"ega" },
	{ WSDISPLAY_TYPE_DCPVR,		"powervr" },
	{ WSDISPLAY_TYPE_SUN24,		"sun24" },
	{ WSDISPLAY_TYPE_SUNBW,		"sunbw" },
	{ WSDISPLAY_TYPE_STI,		"hp-sti" },
	{ WSDISPLAY_TYPE_SUNCG3,	"suncg3" },
	{ WSDISPLAY_TYPE_SUNCG6,	"suncg6" },
	{ WSDISPLAY_TYPE_SUNFFB,	"sunffb" },
	{ WSDISPLAY_TYPE_SUNCG14,	"suncg14" },
	{ WSDISPLAY_TYPE_SUNCG2,	"suncg2" },
	{ WSDISPLAY_TYPE_SUNCG4,	"suncg4" },
	{ WSDISPLAY_TYPE_SUNCG8,	"suncg8" },
	{ WSDISPLAY_TYPE_SUNTCX,	"suntcx" },
	{ WSDISPLAY_TYPE_AGTEN,		"agten" },
	{ WSDISPLAY_TYPE_XVIDEO,	"xvideo" },
	{ WSDISPLAY_TYPE_SUNCG12,	"suncg12" },
	{ WSDISPLAY_TYPE_MGX,		"mgx" },
	{ WSDISPLAY_TYPE_SB_P9000,	"p9000" },
	{ WSDISPLAY_TYPE_RFLEX,		"rasterflex" },
	{ WSDISPLAY_TYPE_LUNA,		"luna" },
	{ WSDISPLAY_TYPE_DVBOX,		"davinci" },
	{ WSDISPLAY_TYPE_GBOX,		"gatorbox" },
	{ WSDISPLAY_TYPE_RBOX,		"renaissance" },
	{ WSDISPLAY_TYPE_HYPERION,	"hyperion" },
	{ WSDISPLAY_TYPE_TOPCAT,	"topcat" },
	{ WSDISPLAY_TYPE_PXALCD,	"pxalcd" },
	{ WSDISPLAY_TYPE_MAC68K,	"mac68k" },
	{ WSDISPLAY_TYPE_SUNLEO,	"sunleo" },
	{ WSDISPLAY_TYPE_TVRX,		"tvrx" },
	{ WSDISPLAY_TYPE_CFXGA,		"cfxga" },
	{ WSDISPLAY_TYPE_LCSPX,		"dec-lcspx" },
	{ WSDISPLAY_TYPE_GBE,		"gbe" },
	{ WSDISPLAY_TYPE_LEGSS,		"dec-legss" },
	{ WSDISPLAY_TYPE_IFB,		"ifb" },
	{ WSDISPLAY_TYPE_RAPTOR,	"raptor" },
	{ WSDISPLAY_TYPE_DL,		"displaylink" },
	{ WSDISPLAY_TYPE_MACHFB,	"mach64" },
	{ WSDISPLAY_TYPE_GFXP,		"gfxp" },
	{ WSDISPLAY_TYPE_RADEONFB,	"radeon" },
	{ WSDISPLAY_TYPE_SMFB,		"smfb" },
	{ WSDISPLAY_TYPE_SISFB,		"sisfb" },
	{ WSDISPLAY_TYPE_ODYSSEY,	"odyssey" },
	{ WSDISPLAY_TYPE_IMPACT,	"impact" },
	{ WSDISPLAY_TYPE_GRTWO,		"grtwo" },
	{ WSDISPLAY_TYPE_NEWPORT,	"newport" },
	{ WSDISPLAY_TYPE_LIGHT,		"light" },
	{ WSDISPLAY_TYPE_INTELDRM,	"inteldrm" },
	{ WSDISPLAY_TYPE_RADEONDRM,	"radeondrm" },
	{ WSDISPLAY_TYPE_EFIFB,		"efifb" },
	{ WSDISPLAY_TYPE_KMS,		"kms" },
	{ WSDISPLAY_TYPE_ASTFB,		"astfb" }
};

static const struct nameint kbdenc_tab[] = {
	KB_ENCTAB
};

static const struct nameint kbdvar_tab[] = {
	KB_VARTAB
};

char *int2name(int, int, const struct nameint *, int);
int name2int(char *, const struct nameint *, int);
void print_kmap(struct wskbd_map_data *);
void print_emul(struct wsdisplay_emultype *);
void print_screen(struct wsdisplay_screentype *);

struct field *
field_by_name(struct field *field_tab, char *name)
{
	const char *p = strchr(name, '.');

	if (!p++)
		errx(1, "%s: illegal variable name", name);

	for (; field_tab->name; field_tab++)
		if (strcmp(field_tab->name, p) == 0)
			return (field_tab);

	errx(1, "%s: not found", name);
}

struct field *
field_by_value(struct field *field_tab, void *addr)
{
	for (; field_tab->name; field_tab++)
		if (field_tab->valp == addr)
			return (field_tab);

	errx(1, "internal error: field_by_value: not found");
}

char *
int2name(int val, int uflag, const struct nameint *tab, int len)
{
	static char tmp[20];
	int i;

	for (i = 0; i < len; i++)
		if (tab[i].value == val)
			return(tab[i].name);

	if (uflag) {
		snprintf(tmp, sizeof(tmp), "unknown_%d", val);
		return(tmp);
	} else
		return(NULL);
}

int
name2int(char *val, const struct nameint *tab, int len)
{
	int i;

	for (i = 0; i < len; i++)
		if (strcmp(tab[i].name, val) == 0)
			return(tab[i].value);
	return(-1);
}

void
pr_field(const char *pre, struct field *f, const char *sep)
{
	struct field_pc *pc;
	u_int flags;
	int i, n;
	char *p;

	if (sep)
		printf("%s.%s%s", pre, f->name, sep);

	switch (f->format) {
	case FMT_UINT:
		printf("%u", *((u_int *) f->valp));
		break;
	case FMT_INT:
		printf("%d", *((int *) f->valp));
		break;
	case FMT_BOOL:
		printf("%s", *((u_int *) f->valp)? "on" : "off");
		break;
	case FMT_PC:
		pc = f->valp;
		i = pc->max - pc->min;
		n = pc->cur - pc->min;
		printf("%u.%02u%%", n * 100 / i, ((n * 100) % i) * 100 / i);
		break;
	case FMT_KBDTYPE:
		p = int2name(*((u_int *) f->valp), 1,
			     kbtype_tab, TABLEN(kbtype_tab));
		printf("%s", p);
		break;
	case FMT_MSTYPE:
		p = int2name(*((u_int *) f->valp), 1,
			     mstype_tab, TABLEN(mstype_tab));
		printf("%s", p);
		break;
	case FMT_DPYTYPE:
		p = int2name(*((u_int *) f->valp), 1,
			     dpytype_tab, TABLEN(dpytype_tab));
		printf("%s", p);
		break;
	case FMT_KBDENC:
		p = int2name(KB_ENCODING(*((u_int *) f->valp)), 1,
			     kbdenc_tab, TABLEN(kbdenc_tab));
		printf("%s", p);

		flags = KB_VARIANT(*((u_int *) f->valp));
		for (i = 0; i < 32; i++) {
			if (!(flags & (1 << i)))
				continue;
			p = int2name(flags & (1 << i), 1,
				     kbdvar_tab, TABLEN(kbdvar_tab));
			printf(".%s", p);
		}
		break;
	case FMT_KBMAP:
		print_kmap((struct wskbd_map_data *) f->valp);
		break;
	case FMT_SCALE:
		printf("%d,%d,%d,%d,%d,%d,%d", wmcoords.minx, wmcoords.maxx,
		    wmcoords.miny, wmcoords.maxy, wmcoords.swapxy,
		    wmcoords.resx, wmcoords.resy);
		break;
	case FMT_EMUL:
		print_emul((struct wsdisplay_emultype *) f->valp);
		break;
	case FMT_SCREEN:
		print_screen((struct wsdisplay_screentype *) f->valp);
		break;
	case FMT_STRING:
		printf("%s", (const char *)f->valp);
		break;
	case FMT_CFG:
		mousecfg_pr_field((struct wsmouse_parameters *) f->valp);
		break;
	default:
		errx(1, "internal error: pr_field: no format %d", f->format);
		break;
	}

	printf("\n");
}

void
rd_field(struct field *f, char *val, int merge)
{
	struct wscons_keymap *mp;
	struct field_pc *pc;
	u_int u, r, fr;
	char *p;
	int i;

	switch (f->format) {
	case FMT_UINT:
		if (sscanf(val, "%u", &u) != 1)
			errx(1, "%s: not a number", val);
		if (merge)
			*((u_int *) f->valp) += u;
		else
			*((u_int *) f->valp) = u;
		break;
	case FMT_INT:
		if (sscanf(val, "%d", &i) != 1)
			errx(1, "%s: not a number", val);
		if (merge)
			*((int *) f->valp) += i;
		else
			*((int *) f->valp) = i;
		break;
	case FMT_BOOL:
		if (*val != 'o' || (val[1] != 'n' &&
		    (val[1] != 'f' || val[2] != 'f')))
			errx(1, "%s: invalid value (on/off)", val);
		*((u_int *) f->valp) = val[1] == 'n'? 1 : 0;
		break;
	case FMT_PC:
		fr = 0;
		if ((i = sscanf(val, "%u.%u%%", &u, &fr)) != 2 && i != 1)
			errx(1, "%s: not a valid number", val);
		pc = f->valp;
		r = pc->max - pc->min;
		i = pc->min + (r * u) / 100 + (r * fr) / 100 / 100;
		if (merge == '+')
			pc->cur += i;
		else if (merge == '-')
			pc->cur -= i;
		else
			pc->cur = i;
		if (pc->cur > pc->max)
			pc->cur = pc->max;
		if (pc->cur < pc->min)
			pc->cur = pc->min;
		break;
	case FMT_KBDENC:
		p = strchr(val, '.');
		if (p != NULL)
			*p++ = '\0';

		i = name2int(val, kbdenc_tab, TABLEN(kbdenc_tab));
		if (i == -1)
			errx(1, "%s: not a valid encoding", val);
		*((u_int *) f->valp) = i;

		while (p) {
			val = p;
			p = strchr(p, '.');
			if (p != NULL)
				*p++ = '\0';
			i = name2int(val, kbdvar_tab, TABLEN(kbdvar_tab));
			if (i == -1)
				errx(1, "%s: not a valid variant", val);
			*((u_int *) f->valp) |= i;
		}
		break;
	case FMT_KBMAP:
		if (! merge)
			kbmap.maplen = 0;
		map_scan_setinput(val);
		yyparse();
		if (merge) {
			if (newkbmap.maplen < kbmap.maplen)
				newkbmap.maplen = kbmap.maplen;
			for (i = 0; i < kbmap.maplen; i++) {
				mp = newkbmap.map + i;
				if (mp->command == KS_voidSymbol &&
				    mp->group1[0] == KS_voidSymbol &&
				    mp->group1[1] == KS_voidSymbol &&
				    mp->group2[0] == KS_voidSymbol &&
				    mp->group2[1] == KS_voidSymbol)
					*mp = kbmap.map[i];
			}
		}
		kbmap.maplen = newkbmap.maplen;
		bcopy(newkbmap.map, kbmap.map,
		      kbmap.maplen*sizeof(struct wscons_keymap));
		break;
	case FMT_SCALE:
	{
		const char *errstr = 0;

		/* Unspecified values default to 0. */
		bzero(&wmcoords, sizeof(wmcoords));
		val = (void *)strtok(val, ",");
		if (val != NULL) {
			wmcoords.minx = (int)strtonum(val,
			    -32768, 32767, &errstr);
			val = (void *)strtok(NULL, ",");
		}
		if (!errstr && val != NULL) {
			wmcoords.maxx = (int)strtonum(val,
			    -32768, 32767, &errstr);
			val = (void *)strtok(NULL, ",");
		}
		if (!errstr && val != NULL) {
			wmcoords.miny = (int)strtonum(val,
			    -32768, 32767, &errstr);
			val = (void *)strtok(NULL, ",");
		}
		if (!errstr && val != NULL) {
			wmcoords.maxy = (int)strtonum(val,
			    -32768, 32767, &errstr);
			val = (void *)strtok(NULL, ",");
		}
		if (!errstr && val != NULL) {
			wmcoords.swapxy = (int)strtonum(val,
			    -32768, 32767, &errstr);
			val = (void *)strtok(NULL, ",");
		}
		if (!errstr && val != NULL) {
			wmcoords.resx = (int)strtonum(val,
			    0, 32767, &errstr);
			val = (void *)strtok(NULL, ",");
		}
		if (!errstr && val != NULL) {
			wmcoords.resy = (int)strtonum(val,
			    0, 32767, &errstr);
			val = (void *)strtok(NULL, ",");
		}
		if (errstr)
			errx(1, "calibration value is %s", errstr);
		if (val != NULL)
			errx(1, "too many calibration values");

		break;
	}
	case FMT_STRING:
		strlcpy(f->valp, val, WSFONT_NAME_SIZE);
		break;
	case FMT_CFG:
		mousecfg_rd_field((struct wsmouse_parameters *) f->valp, val);
		break;
	default:
		errx(1, "internal error: rd_field: no format %d", f->format);
		break;
	}
}

void
print_kmap(struct wskbd_map_data *map)
{
	struct wscons_keymap *mp;
	int i;

	for (i = 0; i < map->maplen; i++) {
		mp = map->map + i;

		if (mp->command == KS_voidSymbol &&
		    mp->group1[0] == KS_voidSymbol &&
		    mp->group1[1] == KS_voidSymbol &&
		    mp->group2[0] == KS_voidSymbol &&
		    mp->group2[1] == KS_voidSymbol)
			continue;
		printf("\n");
		printf("keycode %u =", i);
		if (mp->command != KS_voidSymbol)
			printf(" %s", ksym2name(mp->command));
		printf(" %s", ksym2name(mp->group1[0]));
		if (mp->group1[0] != mp->group1[1] ||
		    mp->group1[0] != mp->group2[0] ||
		    mp->group1[0] != mp->group2[1]) {
			printf(" %s", ksym2name(mp->group1[1]));
			if (mp->group1[0] != mp->group2[0] ||
			    mp->group1[1] != mp->group2[1]) {
				printf(" %s", ksym2name(mp->group2[0]));
				printf(" %s", ksym2name(mp->group2[1]));
			}
		}
	}
	ksymenc(0);
}

void
print_emul(struct wsdisplay_emultype *emuls)
{
	struct wsdisplay_emultype e;
	int fd,i;
	char c='\0';

	fd=emuls->idx;
	e.idx=0;
	i = ioctl(fd, WSDISPLAYIO_GETEMULTYPE, &e);
	while(i == 0) {
		if (c != '\0')
			printf("%c", c);
		printf("%s", e.name);
		c=',';
		e.idx++;
		i = ioctl(fd, WSDISPLAYIO_GETEMULTYPE, &e);
	}
}

void
print_screen(struct wsdisplay_screentype *screens)
{
	struct wsdisplay_screentype s;
	int fd,i;
	char c='\0';

	fd=screens->idx;
	s.idx=0;
	i = ioctl(fd, WSDISPLAYIO_GETSCREENTYPE, &s);
	while(i == 0) {
		if (c != '\0')
			printf("%c", c);
		printf("%s", s.name);
		c=',';
		s.idx++;
		i = ioctl(fd, WSDISPLAYIO_GETSCREENTYPE, &s);
	}
}
