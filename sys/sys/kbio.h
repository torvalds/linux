/*-
 * $FreeBSD$
 */

#ifndef	_SYS_KBIO_H_
#define	_SYS_KBIO_H_

#ifndef _KERNEL
#include <sys/types.h>
#endif
#include <sys/ioccom.h>

/* get/set keyboard I/O mode */
#define K_RAW		0		/* keyboard returns scancodes	*/
#define K_XLATE		1		/* keyboard returns ascii 	*/
#define K_CODE		2		/* keyboard returns keycodes 	*/
#define KDGKBMODE 	_IOR('K', 6, int)
#define KDSKBMODE 	_IOWINT('K', 7)

/* make tone */
#define KDMKTONE	_IOWINT('K', 8)

/* see console.h for the definitions of the following ioctls */
#ifdef notdef
#define KDGETMODE	_IOR('K', 9, int)
#define KDSETMODE	_IOWINT('K', 10)
#define KDSBORDER	_IOWINT('K', 13)
#endif

/* get/set keyboard lock state */
#define CLKED		1		/* Caps locked			*/
#define NLKED		2		/* Num locked			*/
#define SLKED		4		/* Scroll locked		*/
#define ALKED		8		/* AltGr locked			*/
#define LOCK_MASK	(CLKED | NLKED | SLKED | ALKED)
#define KDGKBSTATE	_IOR('K', 19, int)
#define KDSKBSTATE	_IOWINT('K', 20)

/* enable/disable I/O access */
#define KDENABIO	_IO('K', 60)
#define KDDISABIO	_IO('K', 61)

/* make sound */
#define KIOCSOUND	_IOWINT('K', 63)

/* get keyboard model */
#define KB_OTHER	0		/* keyboard not known 		*/
#define KB_84		1		/* 'old' 84 key AT-keyboard	*/
#define KB_101		2		/* MF-101 or MF-102 keyboard	*/
#define KDGKBTYPE	_IOR('K', 64, int)

/* get/set keyboard LED state */
#define LED_CAP		1		/* Caps lock LED 		*/
#define LED_NUM		2		/* Num lock LED 		*/
#define LED_SCR		4		/* Scroll lock LED 		*/
#define LED_MASK	(LED_CAP | LED_NUM | LED_SCR)
#define KDGETLED	_IOR('K', 65, int)
#define KDSETLED	_IOWINT('K', 66)

/* set keyboard repeat rate (obsolete, use KDSETREPEAT below) */
#define KDSETRAD	_IOWINT('K', 67)

struct keyboard_info {
	int		kb_index;	/* kbdio index#			*/
	char		kb_name[16];	/* driver name			*/
	int		kb_unit;	/* unit#			*/
	int		kb_type;	/* KB_84, KB_101, KB_OTHER,...	*/
	int		kb_config;	/* device configuration flags	*/
	int		kb_flags;	/* internal flags		*/
};
typedef struct keyboard_info keyboard_info_t;

/* add/remove keyboard to/from mux */
#define KBADDKBD	_IOW('K', 68, keyboard_info_t)	/* add keyboard */
#define KBRELKBD	_IOW('K', 69, keyboard_info_t)	/* release keyboard */

/* see console.h for the definition of the following ioctl */
#ifdef notdef
#define KDRASTER	_IOW('K', 100, scr_size_t)
#endif

/* get keyboard information */
#define KDGKBINFO	_IOR('K', 101, keyboard_info_t)

/* set/get keyboard repeat rate (new interface) */
struct keyboard_repeat {
	int		kb_repeat[2];
};
typedef struct keyboard_repeat keyboard_repeat_t;
#define KDSETREPEAT	_IOW('K', 102, keyboard_repeat_t)
#define KDGETREPEAT	_IOR('K', 103, keyboard_repeat_t)

/* get/set key map/accent map/function key strings */

#define NUM_KEYS	256		/* number of keys in table	*/
#define NUM_STATES	8		/* states per key		*/
#define ALTGR_OFFSET	128		/* offset for altlock keys	*/

#define NUM_DEADKEYS	15		/* number of accent keys	*/
#define NUM_ACCENTCHARS	52		/* max number of accent chars	*/

#define NUM_FKEYS	96		/* max number of function keys	*/
#define MAXFK		16		/* max length of a function key str */

#ifndef _KEYMAP_DECLARED
#define	_KEYMAP_DECLARED

struct keyent_t {
	u_int		map[NUM_STATES];
	u_char		spcl;
	u_char		flgs;
#define	FLAG_LOCK_O	0
#define	FLAG_LOCK_C	1
#define FLAG_LOCK_N	2
};

struct keymap {
	u_short		n_keys;
	struct keyent_t	key[NUM_KEYS];
};
typedef struct keymap keymap_t;

#ifdef _KERNEL
struct okeyent_t {
	u_char		map[NUM_STATES];
	u_char		spcl;
	u_char		flgs;
};

struct okeymap {
	u_short		n_keys;
	struct okeyent_t key[NUM_KEYS];
};
typedef struct okeymap okeymap_t;
#endif /* _KERNEL */

#endif /* !_KEYMAP_DECLARED */

/* defines for "special" keys (spcl bit set in keymap) */
#define NOP		0x00		/* nothing (dead key)		*/
#define LSH		0x02		/* left shift key		*/
#define RSH		0x03		/* right shift key		*/
#define CLK		0x04		/* caps lock key		*/
#define NLK		0x05		/* num lock key			*/
#define SLK		0x06		/* scroll lock key		*/
#define LALT		0x07		/* left alt key			*/
#define BTAB		0x08		/* backwards tab		*/
#define LCTR		0x09		/* left control key		*/
#define NEXT		0x0a		/* switch to next screen 	*/
#define F_SCR		0x0b		/* switch to first screen 	*/
#define L_SCR		0x1a		/* switch to last screen 	*/
#define F_FN		0x1b		/* first function key 		*/
#define L_FN		0x7a		/* last function key 		*/
/*			0x7b-0x7f	   reserved do not use !	*/
#define RCTR		0x80		/* right control key		*/
#define RALT		0x81		/* right alt (altgr) key	*/
#define ALK		0x82		/* alt lock key			*/
#define ASH		0x83		/* alt shift key		*/
#define META		0x84		/* meta key			*/
#define RBT		0x85		/* boot machine			*/
#define DBG		0x86		/* call debugger		*/
#define SUSP		0x87		/* suspend power (APM)		*/
#define SPSC		0x88		/* toggle splash/text screen	*/

#define F_ACC		DGRA		/* first accent key		*/
#define DGRA		0x89		/* grave			*/
#define DACU		0x8a		/* acute			*/
#define DCIR		0x8b		/* circumflex			*/
#define DTIL		0x8c		/* tilde			*/
#define DMAC		0x8d		/* macron			*/
#define DBRE		0x8e		/* breve			*/
#define DDOT		0x8f		/* dot				*/
#define DUML		0x90		/* umlaut/diaresis		*/
#define DDIA		0x90		/* diaresis			*/
#define DSLA		0x91		/* slash			*/
#define DRIN		0x92		/* ring				*/
#define DCED		0x93		/* cedilla			*/
#define DAPO		0x94		/* apostrophe			*/
#define DDAC		0x95		/* double acute			*/
#define DOGO		0x96		/* ogonek			*/
#define DCAR		0x97		/* caron			*/
#define L_ACC		DCAR		/* last accent key		*/

#define STBY		0x98		/* Go into standby mode (apm)   */
#define PREV		0x99		/* switch to previous screen 	*/
#define PNC		0x9a		/* force system panic */
#define LSHA		0x9b		/* left shift key / alt lock	*/
#define RSHA		0x9c		/* right shift key / alt lock	*/
#define LCTRA		0x9d		/* left ctrl key / alt lock	*/
#define RCTRA		0x9e		/* right ctrl key / alt lock	*/
#define LALTA		0x9f		/* left alt key / alt lock	*/
#define RALTA		0xa0		/* right alt key / alt lock	*/
#define HALT		0xa1		/* halt machine */
#define PDWN		0xa2		/* halt machine and power down */
#define PASTE		0xa3		/* paste from cut-paste buffer */

#define F(x)		((x)+F_FN-1)
#define	S(x)		((x)+F_SCR-1)
#define ACC(x)		((x)+F_ACC)

struct acc_t {
	u_char		accchar;
	u_char		map[NUM_ACCENTCHARS][2];
};

struct accentmap {
	u_short		n_accs;
	struct acc_t	acc[NUM_DEADKEYS];
};
typedef struct accentmap accentmap_t;

struct keyarg {
	u_short		keynum;
	struct keyent_t	key;
};
typedef struct keyarg keyarg_t;

struct fkeytab {
	u_char		str[MAXFK];
	u_char		len;
};
typedef struct fkeytab fkeytab_t;

struct fkeyarg {
	u_short		keynum;
	char		keydef[MAXFK];
	char		flen;
};
typedef struct fkeyarg	fkeyarg_t;

#define GETFKEY		_IOWR('k', 0, fkeyarg_t)
#define SETFKEY		_IOWR('k', 1, fkeyarg_t)
#ifdef notdef		/* see console.h */
#define GIO_SCRNMAP	_IOR('k', 2, scrmap_t)
#define PIO_SCRNMAP	_IOW('k', 3, scrmap_t)
#endif
/* XXX: Should have keymap_t as an argument, but that's too big for ioctl()! */
#define GIO_KEYMAP 	 _IO('k', 6)
#define PIO_KEYMAP 	 _IO('k', 7)
#ifdef _KERNEL
#define OGIO_KEYMAP 	_IOR('k', 6, okeymap_t)
#define OPIO_KEYMAP 	_IOW('k', 7, okeymap_t)
#endif /* _KERNEL */
#define GIO_DEADKEYMAP 	_IOR('k', 8, accentmap_t)
#define PIO_DEADKEYMAP 	_IOW('k', 9, accentmap_t)
#define GIO_KEYMAPENT 	_IOWR('k', 10, keyarg_t)
#define PIO_KEYMAPENT 	_IOW('k', 11, keyarg_t)

/* flags set to the return value in the KD_XLATE mode */

#define	NOKEY		0x01000000	/* no key pressed marker 	*/
#define	FKEY		0x02000000	/* function key marker 		*/
#define	MKEY		0x04000000	/* meta key marker (prepend ESC)*/
#define	BKEY		0x08000000	/* backtab (ESC [ Z)		*/

#define	SPCLKEY		0x80000000	/* special key			*/
#define	RELKEY		0x40000000	/* key released			*/
#define	ERRKEY		0x20000000	/* error			*/

/*
 * The top byte is used to store the flags.  This means there are 24
 * bits left to store the actual character.  Because UTF-8 can encode
 * 2^21 different characters, this is good enough to get Unicode
 * working.
 */
#define KEYCHAR(c)	((c) & 0x00ffffff)
#define KEYFLAGS(c)	((c) & ~0x00ffffff)

#endif /* !_SYS_KBIO_H_ */
