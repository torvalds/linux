#ifndef _LINUX_KD_H
#define _LINUX_KD_H
#include <linux/types.h>
#include <linux/compiler.h>

/* 0x4B is 'K', to avoid collision with termios and vt */

#define GIO_FONT	0x4B60	/* gets font in expanded form */
#define PIO_FONT	0x4B61	/* use font in expanded form */

#define GIO_FONTX	0x4B6B	/* get font using struct consolefontdesc */
#define PIO_FONTX	0x4B6C	/* set font using struct consolefontdesc */
struct consolefontdesc {
	unsigned short charcount;	/* characters in font (256 or 512) */
	unsigned short charheight;	/* scan lines per character (1-32) */
	char __user *chardata;		/* font data in expanded form */
};

#define PIO_FONTRESET   0x4B6D	/* reset to default font */

#define GIO_CMAP	0x4B70	/* gets colour palette on VGA+ */
#define PIO_CMAP	0x4B71	/* sets colour palette on VGA+ */

#define KIOCSOUND	0x4B2F	/* start sound generation (0 for off) */
#define KDMKTONE	0x4B30	/* generate tone */

#define KDGETLED	0x4B31	/* return current led state */
#define KDSETLED	0x4B32	/* set led state [lights, not flags] */
#define 	LED_SCR		0x01	/* scroll lock led */
#define 	LED_NUM		0x02	/* num lock led */
#define 	LED_CAP		0x04	/* caps lock led */

#define KDGKBTYPE	0x4B33	/* get keyboard type */
#define 	KB_84		0x01
#define 	KB_101		0x02 	/* this is what we always answer */
#define 	KB_OTHER	0x03

#define KDADDIO		0x4B34	/* add i/o port as valid */
#define KDDELIO		0x4B35	/* del i/o port as valid */
#define KDENABIO	0x4B36	/* enable i/o to video board */
#define KDDISABIO	0x4B37	/* disable i/o to video board */

#define KDSETMODE	0x4B3A	/* set text/graphics mode */
#define		KD_TEXT		0x00
#define		KD_GRAPHICS	0x01
#define		KD_TEXT0	0x02	/* obsolete */
#define		KD_TEXT1	0x03	/* obsolete */
#define KDGETMODE	0x4B3B	/* get current mode */

#define KDMAPDISP	0x4B3C	/* map display into address space */
#define KDUNMAPDISP	0x4B3D	/* unmap display from address space */

typedef char scrnmap_t;
#define		E_TABSZ		256
#define GIO_SCRNMAP	0x4B40	/* get screen mapping from kernel */
#define PIO_SCRNMAP	0x4B41	/* put screen mapping table in kernel */
#define GIO_UNISCRNMAP  0x4B69	/* get full Unicode screen mapping */
#define PIO_UNISCRNMAP  0x4B6A  /* set full Unicode screen mapping */

#define GIO_UNIMAP	0x4B66	/* get unicode-to-font mapping from kernel */
struct unipair {
	unsigned short unicode;
	unsigned short fontpos;
};
struct unimapdesc {
	unsigned short entry_ct;
	struct unipair __user *entries;
};
#define PIO_UNIMAP	0x4B67	/* put unicode-to-font mapping in kernel */
#define PIO_UNIMAPCLR	0x4B68	/* clear table, possibly advise hash algorithm */
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
#define KDGKBMODE	0x4B44	/* gets current keyboard mode */
#define KDSKBMODE	0x4B45	/* sets current keyboard mode */

#define		K_METABIT	0x03
#define		K_ESCPREFIX	0x04
#define KDGKBMETA	0x4B62	/* gets meta key handling mode */
#define KDSKBMETA	0x4B63	/* sets meta key handling mode */

#define		K_SCROLLLOCK	0x01
#define		K_NUMLOCK	0x02
#define		K_CAPSLOCK	0x04
#define	KDGKBLED	0x4B64	/* get led flags (not lights) */
#define	KDSKBLED	0x4B65	/* set led flags (not lights) */

struct kbentry {
	unsigned char kb_table;
	unsigned char kb_index;
	unsigned short kb_value;
};
#define		K_NORMTAB	0x00
#define		K_SHIFTTAB	0x01
#define		K_ALTTAB	0x02
#define		K_ALTSHIFTTAB	0x03

#define KDGKBENT	0x4B46	/* gets one entry in translation table */
#define KDSKBENT	0x4B47	/* sets one entry in translation table */

struct kbsentry {
	unsigned char kb_func;
	unsigned char kb_string[512];
};
#define KDGKBSENT	0x4B48	/* gets one function key string entry */
#define KDSKBSENT	0x4B49	/* sets one function key string entry */

struct kbdiacr {
        unsigned char diacr, base, result;
};
struct kbdiacrs {
        unsigned int kb_cnt;    /* number of entries in following array */
	struct kbdiacr kbdiacr[256];    /* MAX_DIACR from keyboard.h */
};
#define KDGKBDIACR      0x4B4A  /* read kernel accent table */
#define KDSKBDIACR      0x4B4B  /* write kernel accent table */

struct kbkeycode {
	unsigned int scancode, keycode;
};
#define KDGETKEYCODE	0x4B4C	/* read kernel keycode table entry */
#define KDSETKEYCODE	0x4B4D	/* write kernel keycode table entry */

#define KDSIGACCEPT	0x4B4E	/* accept kbd generated signals */

struct kbd_repeat {
	int delay;	/* in msec; <= 0: don't change */
	int period;	/* in msec; <= 0: don't change */
			/* earlier this field was misnamed "rate" */
};

#define KDKBDREP        0x4B52  /* set keyboard delay/repeat rate;
				 * actually used values are returned */

#define KDFONTOP	0x4B72	/* font operations */

struct console_font_op {
	unsigned int op;	/* operation code KD_FONT_OP_* */
	unsigned int flags;	/* KD_FONT_FLAG_* */
	unsigned int width, height;	/* font size */
	unsigned int charcount;
	unsigned char __user *data;	/* font data with height fixed to 32 */
};

struct console_font {
	unsigned int width, height;	/* font size */
	unsigned int charcount;
	unsigned char *data;	/* font data with height fixed to 32 */
};

#define KD_FONT_OP_SET		0	/* Set font */
#define KD_FONT_OP_GET		1	/* Get font */
#define KD_FONT_OP_SET_DEFAULT	2	/* Set font to default, data points to name / NULL */
#define KD_FONT_OP_COPY		3	/* Copy from another console */

#define KD_FONT_FLAG_DONT_RECALC 	1	/* Don't recalculate hw charcell size [compat] */
#ifdef __KERNEL__
#define KD_FONT_FLAG_OLD		0x80000000	/* Invoked via old interface [compat] */
#endif

/* note: 0x4B00-0x4B4E all have had a value at some time;
   don't reuse for the time being */
/* note: 0x4B60-0x4B6D, 0x4B70-0x4B72 used above */

#endif /* _LINUX_KD_H */
