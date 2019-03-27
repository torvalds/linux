/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991-1996 Søren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_SYS_CONSIO_H_
#define	_SYS_CONSIO_H_

#ifndef _KERNEL
#include <sys/types.h>
#endif
#include <sys/ioccom.h>

/*
 * Console ioctl commands.  Some commands are named as KDXXXX, GIO_XXX, and
 * PIO_XXX, rather than CONS_XXX, for historical and compatibility reasons.
 * Some other CONS_XXX commands are works as wrapper around frame buffer 
 * ioctl commands FBIO_XXX.  Do not try to change all these commands, 
 * otherwise we shall have compatibility problems.
 */

/* get/set video mode */
#define KD_TEXT		0		/* set text mode restore fonts  */
#define KD_TEXT0	0		/* ditto			*/
#define KD_GRAPHICS	1		/* set graphics mode 		*/
#define KD_TEXT1	2		/* set text mode !restore fonts */
#define KD_PIXEL	3		/* set pixel mode		*/
#define KDGETMODE	_IOR('K', 9, int)
#define KDSETMODE	_IOWINT('K', 10)

/* set border color */
#define KDSBORDER	_IOWINT('K', 13)

/* set up raster(pixel) text mode */
struct _scr_size {
	int		scr_size[3];
};
typedef struct _scr_size	scr_size_t;

#define KDRASTER	_IOW('K', 100, scr_size_t)

/* get/set screen char map */
struct _scrmap {
	char		scrmap[256];
};
typedef struct _scrmap	scrmap_t;

#define GIO_SCRNMAP	_IOR('k', 2, scrmap_t)
#define PIO_SCRNMAP	_IOW('k', 3, scrmap_t)

/* get the current text attribute */
#define GIO_ATTR	_IOR('a', 0, int)

/* get the current text color */
#define GIO_COLOR	_IOR('c', 0, int)

/* get the adapter type (equivalent to FBIO_ADPTYPE) */
#define CONS_CURRENT	_IOR('c', 1, int)

/* get the current video mode (equivalent to FBIO_GETMODE) */
#define CONS_GET	_IOR('c', 2, int)

/* not supported? */
#define CONS_IO		_IO('c', 3)

/* set blank time interval */
#define CONS_BLANKTIME	_IOW('c', 4, int)

/* set/get the screen saver (these ioctls are current noop) */
struct ssaver	{
#define MAXSSAVER	16
	char		name[MAXSSAVER];
	int		num;
	long		time;
};
typedef struct ssaver	ssaver_t;

#define CONS_SSAVER	_IOW('c', 5, ssaver_t)
#define CONS_GSAVER	_IOWR('c', 6, ssaver_t)

/*
 * Set the text cursor type.
 *
 * This is an old interface extended to support the CONS_HIDDEN_CURSOR bit.
 * New code should use CONS_CURSORSHAPE.  CONS_CURSOR_ATTRS gives the 3
 * bits supported by the (extended) old interface.  The old interface is
 * especially unusable for hiding the cursor (even with its extension)
 * since it changes the cursor on all vtys.
 */
#define CONS_CURSORTYPE	_IOW('c', 7, int)

/* set the bell type to audible or visual */
#define CONS_VISUAL_BELL (1 << 0)
#define CONS_QUIET_BELL	(1 << 1)
#define CONS_BELLTYPE	_IOW('c', 8, int)

/* set the history (scroll back) buffer size (in lines) */
#define CONS_HISTORY	_IOW('c', 9, int)

/* clear the history (scroll back) buffer */
#define CONS_CLRHIST	_IO('c', 10)

/* mouse cursor ioctl */
struct mouse_data {
	int		x;
	int 		y;
	int 		z;
	int 		buttons;
};
typedef struct mouse_data mouse_data_t;

struct mouse_mode {
	int		mode;
	int		signal;
};
typedef struct mouse_mode mouse_mode_t;

struct mouse_event {
	int		id;			/* one based */
	int		value;
};
typedef struct mouse_event mouse_event_t;

struct mouse_info {
	int		operation;
#define MOUSE_SHOW	0x01
#define MOUSE_HIDE	0x02
#define MOUSE_MOVEABS	0x03
#define MOUSE_MOVEREL	0x04
#define MOUSE_GETINFO	0x05
#define MOUSE_MODE	0x06
#define MOUSE_ACTION	0x07
#define MOUSE_MOTION_EVENT	0x08
#define MOUSE_BUTTON_EVENT	0x09
#define MOUSE_MOUSECHAR	0x0a
	union {
		mouse_data_t	data;
		mouse_mode_t	mode;
		mouse_event_t	event;
		int		mouse_char;
	}		u;
};
typedef struct mouse_info mouse_info_t;

#define CONS_MOUSECTL	_IOWR('c', 10, mouse_info_t)

/* see if the vty has been idle */
#define CONS_IDLE	_IOR('c', 11, int)

/* set the screen saver mode */
#define CONS_NO_SAVER	(-1)
#define CONS_LKM_SAVER	0
#define CONS_USR_SAVER	1
#define CONS_SAVERMODE	_IOW('c', 12, int)

/* start the screen saver */
#define CONS_SAVERSTART	_IOW('c', 13, int)

/* set the text cursor shape (see also CONS_CURSORTYPE above) */
#define CONS_BLINK_CURSOR	(1 << 0)
#define CONS_CHAR_CURSOR	(1 << 1)
#define CONS_HIDDEN_CURSOR	(1 << 2)
#define CONS_CURSOR_ATTRS	(CONS_BLINK_CURSOR | CONS_CHAR_CURSOR |	\
				 CONS_HIDDEN_CURSOR)
#define CONS_CHARCURSOR_COLORS	(1 << 26)
#define CONS_MOUSECURSOR_COLORS	(1 << 27)
#define CONS_DEFAULT_CURSOR	(1 << 28)
#define CONS_SHAPEONLY_CURSOR	(1 << 29)
#define CONS_RESET_CURSOR	(1 << 30)
#define CONS_LOCAL_CURSOR	(1U << 31)
struct cshape {
	/* shape[0]: flags, shape[1]: base, shape[2]: height */
	int		shape[3];
};
#define CONS_GETCURSORSHAPE _IOWR('c', 14, struct cshape)
#define CONS_SETCURSORSHAPE _IOW('c', 15, struct cshape)

/* set/get font data */
struct fnt8 {
	char		fnt8x8[8*256];
};
typedef struct fnt8	fnt8_t;

struct fnt14 {
	char		fnt8x14[14*256];
};
typedef struct fnt14	fnt14_t;

struct fnt16 {
	char		fnt8x16[16*256];
};
typedef struct fnt16	fnt16_t;

struct vfnt_map {
	uint32_t	src;
	uint16_t	dst;
	uint16_t	len;
};
typedef struct vfnt_map	vfnt_map_t;

#define VFNT_MAP_NORMAL		0
#define VFNT_MAP_NORMAL_RIGHT	1
#define VFNT_MAP_BOLD		2
#define VFNT_MAP_BOLD_RIGHT	3
#define VFNT_MAPS		4
struct vfnt {
	vfnt_map_t	*map[VFNT_MAPS];
	uint8_t		*glyphs;
	unsigned int	map_count[VFNT_MAPS];
	unsigned int	glyph_count;
	unsigned int	width;
	unsigned int	height;
};
typedef struct vfnt	vfnt_t;

#define PIO_FONT8x8	_IOW('c', 64, fnt8_t)
#define GIO_FONT8x8	_IOR('c', 65, fnt8_t)
#define PIO_FONT8x14	_IOW('c', 66, fnt14_t)
#define GIO_FONT8x14	_IOR('c', 67, fnt14_t)
#define PIO_FONT8x16	_IOW('c', 68, fnt16_t)
#define GIO_FONT8x16	_IOR('c', 69, fnt16_t)
#define PIO_VFONT	_IOW('c', 70, vfnt_t)
#define GIO_VFONT	_IOR('c', 71, vfnt_t)
#define PIO_VFONT_DEFAULT _IO('c', 72)

/* get video mode information */
struct colors	{
	char		fore;
	char		back;
};

struct vid_info {
	short		size;
	short		m_num;
	u_short		font_size;
	u_short		mv_row, mv_col;
	u_short		mv_rsz, mv_csz;
	u_short		mv_hsz;
	struct colors	mv_norm,
			mv_rev,
			mv_grfc;
	u_char		mv_ovscan;
	u_char		mk_keylock;
};
typedef struct vid_info vid_info_t;

#define CONS_GETINFO    _IOWR('c', 73, vid_info_t)

/* get version */
#define CONS_GETVERS	_IOR('c', 74, int)

/* get the video adapter index (equivalent to FBIO_ADAPTER) */
#define CONS_CURRENTADP	_IOR('c', 100, int)

/* get the video adapter information (equivalent to FBIO_ADPINFO) */
#define CONS_ADPINFO	_IOWR('c', 101, video_adapter_info_t)

/* get the video mode information (equivalent to FBIO_MODEINFO) */
#define CONS_MODEINFO	_IOWR('c', 102, video_info_t)

/* find a video mode (equivalent to FBIO_FINDMODE) */
#define CONS_FINDMODE	_IOWR('c', 103, video_info_t)

/* set the frame buffer window origin (equivalent to FBIO_SETWINORG) */
#define CONS_SETWINORG	_IOWINT('c', 104)

/* use the specified keyboard */
#define CONS_SETKBD	_IOWINT('c', 110)

/* release the current keyboard */
#define CONS_RELKBD	_IO('c', 111)

struct scrshot {
	int		x;
	int		y;
	int		xsize;
	int		ysize;
	u_int16_t*	buf;
};
typedef struct scrshot scrshot_t;

/* Snapshot the current video buffer */
#define CONS_SCRSHOT	_IOWR('c', 105, scrshot_t)

/* get/set the current terminal emulator info. */
#define TI_NAME_LEN	32
#define TI_DESC_LEN	64

struct term_info {
	int		ti_index;
	int		ti_flags;
	u_char		ti_name[TI_NAME_LEN];
	u_char		ti_desc[TI_DESC_LEN];
};
typedef struct term_info term_info_t;

#define CONS_GETTERM	_IOWR('c', 112, term_info_t)
#define CONS_SETTERM	_IOW('c', 113, term_info_t)

/*
 * Vty switching ioctl commands.
 */

/* get the next available vty */
#define VT_OPENQRY	_IOR('v', 1, int)

/* set/get vty switching mode */
#ifndef _VT_MODE_DECLARED
#define	_VT_MODE_DECLARED
struct vt_mode {
	char		mode;
#define VT_AUTO		0		/* switching is automatic 	*/
#define VT_PROCESS	1		/* switching controlled by prog */
#define VT_KERNEL	255		/* switching controlled in kernel */
	char		waitv;		/* not implemented yet 	SOS	*/
	short		relsig;
	short		acqsig;
	short		frsig;		/* not implemented yet	SOS	*/
};
typedef struct vt_mode vtmode_t;
#endif /* !_VT_MODE_DECLARED */

#define VT_SETMODE	_IOW('v', 2, vtmode_t)
#define VT_GETMODE	_IOR('v', 3, vtmode_t)

/* acknowledge release or acquisition of a vty */
#define VT_FALSE	0
#define VT_TRUE		1
#define VT_ACKACQ	2
#define VT_RELDISP	_IOWINT('v', 4)

/* activate the specified vty */
#define VT_ACTIVATE	_IOWINT('v', 5)

/* wait until the specified vty is activate */
#define VT_WAITACTIVE	_IOWINT('v', 6)

/* get the currently active vty */
#define VT_GETACTIVE	_IOR('v', 7, int)

/* get the index of the vty */
#define VT_GETINDEX	_IOR('v', 8, int)

/* prevent switching vtys */
#define VT_LOCKSWITCH	_IOW('v', 9, int)

/*
 * Video mode switching ioctl.  See sys/fbio.h for mode numbers.
 */

#define SW_B40x25 	_IO('S', M_B40x25)
#define SW_C40x25  	_IO('S', M_C40x25)
#define SW_B80x25  	_IO('S', M_B80x25)
#define SW_C80x25  	_IO('S', M_C80x25)
#define SW_BG320   	_IO('S', M_BG320)
#define SW_CG320   	_IO('S', M_CG320)
#define SW_BG640   	_IO('S', M_BG640)
#define SW_EGAMONO80x25 _IO('S', M_EGAMONO80x25)
#define SW_CG320_D    	_IO('S', M_CG320_D)
#define SW_CG640_E    	_IO('S', M_CG640_E)
#define SW_EGAMONOAPA 	_IO('S', M_EGAMONOAPA)
#define SW_CG640x350  	_IO('S', M_CG640x350)
#define SW_ENH_MONOAPA2 _IO('S', M_ENHMONOAPA2)
#define SW_ENH_CG640  	_IO('S', M_ENH_CG640)
#define SW_ENH_B40x25  	_IO('S', M_ENH_B40x25)
#define SW_ENH_C40x25  	_IO('S', M_ENH_C40x25)
#define SW_ENH_B80x25  	_IO('S', M_ENH_B80x25)
#define SW_ENH_C80x25  	_IO('S', M_ENH_C80x25)
#define SW_ENH_B80x43  	_IO('S', M_ENH_B80x43)
#define SW_ENH_C80x43  	_IO('S', M_ENH_C80x43)
#define SW_MCAMODE    	_IO('S', M_MCA_MODE)
#define SW_VGA_C40x25	_IO('S', M_VGA_C40x25)
#define SW_VGA_C80x25	_IO('S', M_VGA_C80x25)
#define SW_VGA_C80x30	_IO('S', M_VGA_C80x30)
#define SW_VGA_C80x50	_IO('S', M_VGA_C80x50)
#define SW_VGA_C80x60	_IO('S', M_VGA_C80x60)
#define SW_VGA_M80x25	_IO('S', M_VGA_M80x25)
#define SW_VGA_M80x30	_IO('S', M_VGA_M80x30)
#define SW_VGA_M80x50	_IO('S', M_VGA_M80x50)
#define SW_VGA_M80x60	_IO('S', M_VGA_M80x60)
#define SW_VGA11	_IO('S', M_VGA11)
#define SW_BG640x480	_IO('S', M_VGA11)
#define SW_VGA12	_IO('S', M_VGA12)
#define SW_CG640x480	_IO('S', M_VGA12)
#define SW_VGA13	_IO('S', M_VGA13)
#define SW_VGA_CG320	_IO('S', M_VGA13)
#define SW_VGA_CG640	_IO('S', M_VGA_CG640)
#define SW_VGA_MODEX	_IO('S', M_VGA_MODEX)

#define SW_VGA_C90x25	_IO('S', M_VGA_C90x25)
#define SW_VGA_M90x25	_IO('S', M_VGA_M90x25)
#define SW_VGA_C90x30	_IO('S', M_VGA_C90x30)
#define SW_VGA_M90x30	_IO('S', M_VGA_M90x30)
#define SW_VGA_C90x43	_IO('S', M_VGA_C90x43)
#define SW_VGA_M90x43	_IO('S', M_VGA_M90x43)
#define SW_VGA_C90x50	_IO('S', M_VGA_C90x50)
#define SW_VGA_M90x50	_IO('S', M_VGA_M90x50)
#define SW_VGA_C90x60	_IO('S', M_VGA_C90x60)
#define SW_VGA_M90x60	_IO('S', M_VGA_M90x60)

#define SW_TEXT_80x25	_IO('S', M_TEXT_80x25)
#define SW_TEXT_80x30	_IO('S', M_TEXT_80x30)
#define SW_TEXT_80x43	_IO('S', M_TEXT_80x43)
#define SW_TEXT_80x50	_IO('S', M_TEXT_80x50)
#define SW_TEXT_80x60	_IO('S', M_TEXT_80x60)
#define SW_TEXT_132x25	_IO('S', M_TEXT_132x25)
#define SW_TEXT_132x30	_IO('S', M_TEXT_132x30)
#define SW_TEXT_132x43	_IO('S', M_TEXT_132x43)
#define SW_TEXT_132x50	_IO('S', M_TEXT_132x50)
#define SW_TEXT_132x60	_IO('S', M_TEXT_132x60)

#define SW_VESA_CG640x400	_IO('V', M_VESA_CG640x400 - M_VESA_BASE)
#define SW_VESA_CG640x480	_IO('V', M_VESA_CG640x480 - M_VESA_BASE)
#define SW_VESA_800x600		_IO('V', M_VESA_800x600 - M_VESA_BASE)
#define SW_VESA_CG800x600	_IO('V', M_VESA_CG800x600 - M_VESA_BASE)
#define SW_VESA_1024x768	_IO('V', M_VESA_1024x768 - M_VESA_BASE)
#define SW_VESA_CG1024x768	_IO('V', M_VESA_CG1024x768 - M_VESA_BASE)
#define SW_VESA_1280x1024	_IO('V', M_VESA_1280x1024 - M_VESA_BASE)
#define SW_VESA_CG1280x1024	_IO('V', M_VESA_CG1280x1024 - M_VESA_BASE)
#define SW_VESA_C80x60		_IO('V', M_VESA_C80x60 - M_VESA_BASE)
#define SW_VESA_C132x25		_IO('V', M_VESA_C132x25 - M_VESA_BASE)
#define SW_VESA_C132x43		_IO('V', M_VESA_C132x43 - M_VESA_BASE)
#define SW_VESA_C132x50		_IO('V', M_VESA_C132x50 - M_VESA_BASE)
#define SW_VESA_C132x60		_IO('V', M_VESA_C132x60 - M_VESA_BASE)
#define SW_VESA_32K_320		_IO('V', M_VESA_32K_320 - M_VESA_BASE)
#define SW_VESA_64K_320		_IO('V', M_VESA_64K_320 - M_VESA_BASE)
#define SW_VESA_FULL_320	_IO('V', M_VESA_FULL_320 - M_VESA_BASE)
#define SW_VESA_32K_640		_IO('V', M_VESA_32K_640 - M_VESA_BASE)
#define SW_VESA_64K_640		_IO('V', M_VESA_64K_640 - M_VESA_BASE)
#define SW_VESA_FULL_640	_IO('V', M_VESA_FULL_640 - M_VESA_BASE)
#define SW_VESA_32K_800		_IO('V', M_VESA_32K_800 - M_VESA_BASE)
#define SW_VESA_64K_800		_IO('V', M_VESA_64K_800 - M_VESA_BASE)
#define SW_VESA_FULL_800	_IO('V', M_VESA_FULL_800 - M_VESA_BASE)
#define SW_VESA_32K_1024	_IO('V', M_VESA_32K_1024 - M_VESA_BASE)
#define SW_VESA_64K_1024	_IO('V', M_VESA_64K_1024 - M_VESA_BASE)
#define SW_VESA_FULL_1024	_IO('V', M_VESA_FULL_1024 - M_VESA_BASE)
#define SW_VESA_32K_1280	_IO('V', M_VESA_32K_1280 - M_VESA_BASE)
#define SW_VESA_64K_1280	_IO('V', M_VESA_64K_1280 - M_VESA_BASE)
#define SW_VESA_FULL_1280	_IO('V', M_VESA_FULL_1280 - M_VESA_BASE)

#endif /* !_SYS_CONSIO_H_ */
