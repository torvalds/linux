/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_VT_H
#define _UAPI_LINUX_VT_H

#include <linux/ioctl.h>
#include <linux/types.h>

/*
 * These constants are also useful for user-level apps (e.g., VC
 * resizing).
 */
#define MIN_NR_CONSOLES 1       /* must be at least 1 */
#define MAX_NR_CONSOLES	63	/* serial lines start at 64 */
		/* Note: the ioctl VT_GETSTATE does not work for
		   consoles 16 and higher (since it returns a short) */

/* 'V' to avoid collision with termios and kd */

#define VT_OPENQRY		_IO('V', 0x00)	/* find available vt */

struct vt_mode {
	__u8 mode;		/* vt mode */
	__u8 waitv;		/* if set, hang on writes if not active */
	__s16 relsig;		/* signal to raise on release req */
	__s16 acqsig;		/* signal to raise on acquisition */
	__s16 frsig;		/* unused (set to 0) */
};
#define VT_GETMODE		_IO('V', 0x01)	/* get mode of active vt */
#define VT_SETMODE		_IO('V', 0x02)	/* set mode of active vt */
#define		VT_AUTO		0x00	/* auto vt switching */
#define		VT_PROCESS	0x01	/* process controls switching */
#define		VT_ACKACQ	0x02	/* acknowledge switch */

struct vt_stat {
	__u16 v_active;	/* active vt */
	__u16 v_signal;	/* signal to send */
	__u16 v_state;		/* vt bitmask */
};
#define VT_GETSTATE		_IO('V', 0x03)	/* get global vt state info */
#define VT_SENDSIG		_IO('V', 0x04)	/* signal to send to bitmask of vts */

#define VT_RELDISP		_IO('V', 0x05)	/* release display */

#define VT_ACTIVATE		_IO('V', 0x06)	/* make vt active */
#define VT_WAITACTIVE		_IO('V', 0x07)	/* wait for vt active */
#define VT_DISALLOCATE		_IO('V', 0x08)  /* free memory associated to vt */

struct vt_sizes {
	__u16 v_rows;		/* number of rows */
	__u16 v_cols;		/* number of columns */
	__u16 v_scrollsize;	/* number of lines of scrollback */
};
#define VT_RESIZE		_IO('V', 0x09)	/* set kernel's idea of screensize */

struct vt_consize {
	__u16 v_rows;	/* number of rows */
	__u16 v_cols;	/* number of columns */
	__u16 v_vlin;	/* number of pixel rows on screen */
	__u16 v_clin;	/* number of pixel rows per character */
	__u16 v_vcol;	/* number of pixel columns on screen */
	__u16 v_ccol;	/* number of pixel columns per character */
};
#define VT_RESIZEX		_IO('V', 0x0A)  /* set kernel's idea of screensize + more */
#define VT_LOCKSWITCH		_IO('V', 0x0B)  /* disallow vt switching */
#define VT_UNLOCKSWITCH		_IO('V', 0x0C)  /* allow vt switching */
#define VT_GETHIFONTMASK	_IO('V', 0x0D)  /* return hi font mask */

struct vt_event {
	__u32 event;
#define VT_EVENT_SWITCH		0x0001	/* Console switch */
#define VT_EVENT_BLANK		0x0002	/* Screen blank */
#define VT_EVENT_UNBLANK	0x0004	/* Screen unblank */
#define VT_EVENT_RESIZE		0x0008	/* Resize display */
#define VT_MAX_EVENT		0x000F
	__u32 oldev;		/* Old console */
	__u32 newev;		/* New console (if changing) */
	__u32 pad[4];		/* Padding for expansion */
};

#define VT_WAITEVENT		_IO('V', 0x0E)	/* Wait for an event */

struct vt_setactivate {
	__u32 console;
	struct vt_mode mode;
};

#define VT_SETACTIVATE		_IO('V', 0x0F)	/* Activate and set the mode of a console */

/* get console size and cursor position */
struct vt_consizecsrpos {
	__u16 con_rows;		/* number of console rows */
	__u16 con_cols;		/* number of console columns */
	__u16 csr_row;		/* current cursor's row */
	__u16 csr_col;		/* current cursor's column */
};
#define VT_GETCONSIZECSRPOS	_IOR('V', 0x10, struct vt_consizecsrpos)

#endif /* _UAPI_LINUX_VT_H */
