/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_KD_H
#define _UAPI_LINUX_KD_H
#include <linux/types.h>
#include <linux/compiler.h>

/* 0x4B is 'K', to avoid collision with termios and vt */
#define KD_IOCTL_BASE	'K'

#define GIO_FONT	_IO(KD_IOCTL_BASE, 0x60)	/* gets font in expanded form */
#define PIO_FONT	_IO(KD_IOCTL_BASE, 0x61)	/* use font in expanded form */

#define GIO_FONTX	_IO(KD_IOCTL_BASE, 0x6B)	/* get font using struct consolefontdesc */
#define PIO_FONTX	_IO(KD_IOCTL_BASE, 0x6C)	/* set font using struct consolefontdesc */
struct consolefontdesc {
	unsigned short charcount;	/* characters in font (256 or 512) */
	unsigned short charheight;	/* scan lines per character (1-32) */
	char __user *chardata;		/* font data in expanded form */
};

#define PIO_FONTRESET	_IO(KD_IOCTL_BASE, 0x6D)	/* reset to default font */

#define GIO_CMAP	_IO(KD_IOCTL_BASE, 0x70)	/* gets colour palette on VGA+ */
#define PIO_CMAP	_IO(KD_IOCTL_BASE, 0x71)	/* sets colour palette on VGA+ */

#define KIOCSOUND	_IO(KD_IOCTL_BASE, 0x2F)	/* start sound generation (0 for off) */
#define KDMKTONE	_IO(KD_IOCTL_BASE, 0x30)	/* generate tone */

#define KDGETLED	_IO(KD_IOCTL_BASE, 0x31)	/* return current led state */
#define KDSETLED	_IO(KD_IOCTL_BASE, 0x32)	/* set led state [lights, not flags] */
#define 	LED_SCR		0x01	/* scroll lock led */
#define 	LED_NUM		0x02	/* num lock led */
#define 	LED_CAP		0x04	/* caps lock led */

#define KDGKBTYPE	_IO(KD_IOCTL_BASE, 0x33)	/* get keyboard type */
#define 	KB_84		0x01
#define 	KB_101		0x02 	/* this is what we always answer */
#define 	KB_OTHER	0x03

#define KDADDIO		_IO(KD_IOCTL_BASE, 0x34)	/* add i/o port as valid */
#define KDDELIO		_IO(KD_IOCTL_BASE, 0x35)	/* del i/o port as valid */
#define KDENABIO	_IO(KD_IOCTL_BASE, 0x36)	/* enable i/o to video board */
#define KDDISABIO	_IO(KD_IOCTL_BASE, 0x37)	/* disable i/o to video board */

#define KDSETMODE	_IO(KD_IOCTL_BASE, 0x3A)	/* set text/graphics mode */
#define		KD_TEXT		0x00
#define		KD_GRAPHICS	0x01
#define		KD_TEXT0	0x02	/* obsolete */
#define		KD_TEXT1	0x03	/* obsolete */
#define KDGETMODE	_IO(KD_IOCTL_BASE, 0x3B)	/* get current mode */

#define KDMAPDISP	_IO(KD_IOCTL_BASE, 0x3C)	/* map display into address space */
#define KDUNMAPDISP	_IO(KD_IOCTL_BASE, 0x3D)	/* unmap display from address space */

typedef char scrnmap_t;
#define		E_TABSZ		256
#define GIO_SCRNMAP	_IO(KD_IOCTL_BASE, 0x40)	/* get screen mapping from kernel */
#define PIO_SCRNMAP	_IO(KD_IOCTL_BASE, 0x41)	/* put screen mapping table in kernel */
#define GIO_UNISCRNMAP	_IO(KD_IOCTL_BASE, 0x69)	/* get full Unicode screen mapping */
#define PIO_UNISCRNMAP	_IO(KD_IOCTL_BASE, 0x6A)	/* set full Unicode screen mapping */

#define GIO_UNIMAP	_IO(KD_IOCTL_BASE, 0x66)	/* get unicode-to-font mapping from kernel */
struct unipair {
	unsigned short unicode;
	unsigned short fontpos;
};
struct unimapdesc {
	unsigned short entry_ct;
	struct unipair __user *entries;
};
#define PIO_UNIMAP	_IO(KD_IOCTL_BASE, 0x67)	/* put unicode-to-font mapping in kernel */
#define PIO_UNIMAPCLR	_IO(KD_IOCTL_BASE, 0x68)	/* clear table, possibly advise hash algorithm */
struct unimapinit {
	unsigned short advised_hashsize;  /* 0 if no opinion */
	unsigned short advised_hashstep;  /* 0 if no opinion */
	unsigned short advised_hashlevel; /* 0 if no opinion */
};

#define UNI_DIRECT_BASE 0xF000	/* start of Direct Font Region */
#define UNI_DIRECT_MASK 0x01FF	/* Direct Font Region bitmask */

#define		K_RAW		0x00
#define		K_XLATE		0x01
#define		K_MEDIUMRAW	0x02
#define		K_UNICODE	0x03
#define		K_OFF		0x04
#define KDGKBMODE	_IO(KD_IOCTL_BASE, 0x44)	/* gets current keyboard mode */
#define KDSKBMODE	_IO(KD_IOCTL_BASE, 0x45)	/* sets current keyboard mode */

#define		K_METABIT	0x03
#define		K_ESCPREFIX	0x04
#define KDGKBMETA	_IO(KD_IOCTL_BASE, 0x62)	/* gets meta key handling mode */
#define KDSKBMETA	_IO(KD_IOCTL_BASE, 0x63)	/* sets meta key handling mode */

#define		K_SCROLLLOCK	0x01
#define		K_NUMLOCK	0x02
#define		K_CAPSLOCK	0x04
#define	KDGKBLED	_IO(KD_IOCTL_BASE, 0x64)	/* get led flags (not lights) */
#define	KDSKBLED	_IO(KD_IOCTL_BASE, 0x65)	/* set led flags (not lights) */

struct kbentry {
	unsigned char kb_table;
	unsigned char kb_index;
	unsigned short kb_value;
};
#define		K_NORMTAB	0x00
#define		K_SHIFTTAB	0x01
#define		K_ALTTAB	0x02
#define		K_ALTSHIFTTAB	0x03

#define KDGKBENT	_IO(KD_IOCTL_BASE, 0x46)	/* gets one entry in translation table */
#define KDSKBENT	_IO(KD_IOCTL_BASE, 0x47)	/* sets one entry in translation table */

struct kbsentry {
	unsigned char kb_func;
	unsigned char kb_string[512];
};
#define KDGKBSENT	_IO(KD_IOCTL_BASE, 0x48)	/* gets one function key string entry */
#define KDSKBSENT	_IO(KD_IOCTL_BASE, 0x49)	/* sets one function key string entry */

struct kbdiacr {
        unsigned char diacr, base, result;
};
struct kbdiacrs {
        unsigned int kb_cnt;    /* number of entries in following array */
	struct kbdiacr kbdiacr[256];    /* MAX_DIACR from keyboard.h */
};
#define KDGKBDIACR	_IO(KD_IOCTL_BASE, 0x4A)  /* read kernel accent table */
#define KDSKBDIACR	_IO(KD_IOCTL_BASE, 0x4B)  /* write kernel accent table */

struct kbdiacruc {
	unsigned int diacr, base, result;
};
struct kbdiacrsuc {
        unsigned int kb_cnt;    /* number of entries in following array */
	struct kbdiacruc kbdiacruc[256];    /* MAX_DIACR from keyboard.h */
};
#define KDGKBDIACRUC	_IO(KD_IOCTL_BASE, 0xFA)  /* read kernel accent table - UCS */
#define KDSKBDIACRUC	_IO(KD_IOCTL_BASE, 0xFB)  /* write kernel accent table - UCS */

struct kbkeycode {
	unsigned int scancode, keycode;
};
#define KDGETKEYCODE	_IO(KD_IOCTL_BASE, 0x4C)	/* read kernel keycode table entry */
#define KDSETKEYCODE	_IO(KD_IOCTL_BASE, 0x4D)	/* write kernel keycode table entry */

#define KDSIGACCEPT	_IO(KD_IOCTL_BASE, 0x4E)	/* accept kbd generated signals */

struct kbd_repeat {
	int delay;	/* in msec; <= 0: don't change */
	int period;	/* in msec; <= 0: don't change */
			/* earlier this field was misnamed "rate" */
};

#define KDKBDREP	_IO(KD_IOCTL_BASE, 0x52)	/* set keyboard delay/repeat rate;
							 * actually used values are returned
							 */

#define KDFONTOP	_IO(KD_IOCTL_BASE, 0x72)	/* font operations */

struct console_font_op {
	unsigned int op;	/* operation code KD_FONT_OP_* */
	unsigned int flags;	/* KD_FONT_FLAG_* */
	unsigned int width, height;	/* font size */
	unsigned int charcount;
	unsigned char __user *data;	/* font data with vpitch fixed to 32 for
					 * KD_FONT_OP_SET/GET
					 */
};

struct console_font {
	unsigned int width, height;	/* font size */
	unsigned int charcount;
	unsigned char *data;	/* font data with vpitch fixed to 32 for
				 * KD_FONT_OP_SET/GET
				 */
};

#define KD_FONT_OP_SET		0	/* Set font */
#define KD_FONT_OP_GET		1	/* Get font */
#define KD_FONT_OP_SET_DEFAULT	2	/* Set font to default, data points to name / NULL */
#define KD_FONT_OP_COPY		3	/* Obsolete, do not use */
#define KD_FONT_OP_SET_TALL	4	/* Set font with vpitch = height */
#define KD_FONT_OP_GET_TALL	5	/* Get font with vpitch = height */

#define KD_FONT_FLAG_DONT_RECALC 	1	/* Don't recalculate hw charcell size [compat] */

/* note: 0x4B00-0x4B4E all have had a value at some time;
   don't reuse for the time being */
/* note: 0x4B60-0x4B6D, 0x4B70-0x4B72 used above */

#endif /* _UAPI_LINUX_KD_H */
