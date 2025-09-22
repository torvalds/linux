/*	$OpenBSD: pcdisplay.h,v 1.2 2001/02/02 20:25:39 aaron Exp $ */
/*	$NetBSD: pcdisplay.h,v 1.1 1998/05/14 23:11:03 drochner Exp $	*/

/*
 * IBM PC display definitions
 */

/* Color attributes for foreground text */

#define	FG_BLACK		   0
#define	FG_BLUE			   1
#define	FG_GREEN		   2
#define	FG_CYAN			   3
#define	FG_RED			   4
#define	FG_MAGENTA		   5
#define	FG_BROWN		   6
#define	FG_LIGHTGREY		   7
#define	FG_DARKGREY		   8
#define	FG_LIGHTBLUE		   9
#define	FG_LIGHTGREEN		  10
#define	FG_LIGHTCYAN		  11
#define	FG_LIGHTRED		  12
#define	FG_LIGHTMAGENTA		  13
#define	FG_YELLOW		  14
#define	FG_WHITE		  15
#define	FG_BLINK		0x80
#define	FG_MASK			0x8f

/* Color attributes for text background */

#define	BG_BLACK		0x00
#define	BG_BLUE			0x10
#define	BG_GREEN		0x20
#define	BG_CYAN			0x30
#define	BG_RED			0x40
#define	BG_MAGENTA		0x50
#define	BG_BROWN		0x60
#define	BG_LIGHTGREY		0x70
#define	BG_MASK			0x70

/* Monochrome attributes for foreground text */

#define	FG_UNDERLINE		0x01
#define	FG_INTENSE		0x08

/* Monochrome attributes for text background */

#define	BG_INTENSE		0x10
