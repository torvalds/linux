/* $OpenBSD: pckbdreg.h,v 1.3 2023/08/13 21:54:02 miod Exp $ */
/* $NetBSD: pckbdreg.h,v 1.2 1998/04/07 13:43:16 hannken Exp $ */

/*
 * Keyboard definitions
 */

/* keyboard commands */
#define	KBC_RESET	0xFF	/* reset the keyboard */
#define	KBC_RESEND	0xFE	/* request the keyboard resend the last byte */
#define	KBC_SETDEFAULT	0xF6	/* resets keyboard to its power-on defaults */
#define	KBC_DISABLE	0xF5	/* as per KBC_SETDEFAULT, but also disable key scanning */
#define	KBC_ENABLE	0xF4	/* enable key scanning */
#define	KBC_TYPEMATIC	0xF3	/* set typematic rate and delay */
#define	KBC_GETID	0xF2	/* get keyboard ID (not supported on AT kbd) */
#define	KBC_SETTABLE	0xF0	/* set scancode translation table */
#define	KBC_MODEIND	0xED	/* set mode indicators (i.e. LEDs) */
#define	KBC_ECHO	0xEE	/* request an echo from the keyboard */

/* keyboard responses */
#define	KBR_EXTENDED0	0xE0	/* extended key sequence */
#define	KBR_EXTENDED1	0xE1	/* extended key sequence */
#define	KBR_RESEND	0xFE	/* needs resend of command */
#define	KBR_ACK		0xFA	/* received a valid command */
#define	KBR_OVERRUN	0x00	/* flooded */
#define	KBR_FAILURE	0xFD	/* diagnostic failure */
#define	KBR_BREAK	0xF0	/* break code prefix - sent on key release */
#define	KBR_RSTDONE	0xAA	/* reset complete */
#define	KBR_ECHO	0xEE	/* echo response */
