#ifndef _LINUX_VT_H
#define _LINUX_VT_H

/* 0x56 is 'V', to avoid collision with termios and kd */

#define VT_OPENQRY	0x5600	/* find available vt */

struct vt_mode {
	char mode;		/* vt mode */
	char waitv;		/* if set, hang on writes if not active */
	short relsig;		/* signal to raise on release req */
	short acqsig;		/* signal to raise on acquisition */
	short frsig;		/* unused (set to 0) */
};
#define VT_GETMODE	0x5601	/* get mode of active vt */
#define VT_SETMODE	0x5602	/* set mode of active vt */
#define		VT_AUTO		0x00	/* auto vt switching */
#define		VT_PROCESS	0x01	/* process controls switching */
#define		VT_ACKACQ	0x02	/* acknowledge switch */

struct vt_stat {
	unsigned short v_active;	/* active vt */
	unsigned short v_signal;	/* signal to send */
	unsigned short v_state;		/* vt bitmask */
};
#define VT_GETSTATE	0x5603	/* get global vt state info */
#define VT_SENDSIG	0x5604	/* signal to send to bitmask of vts */

#define VT_RELDISP	0x5605	/* release display */

#define VT_ACTIVATE	0x5606	/* make vt active */
#define VT_WAITACTIVE	0x5607	/* wait for vt active */
#define VT_DISALLOCATE	0x5608  /* free memory associated to vt */

struct vt_sizes {
	unsigned short v_rows;		/* number of rows */
	unsigned short v_cols;		/* number of columns */
	unsigned short v_scrollsize;	/* number of lines of scrollback */
};
#define VT_RESIZE	0x5609	/* set kernel's idea of screensize */

struct vt_consize {
	unsigned short v_rows;	/* number of rows */
	unsigned short v_cols;	/* number of columns */
	unsigned short v_vlin;	/* number of pixel rows on screen */
	unsigned short v_clin;	/* number of pixel rows per character */
	unsigned short v_vcol;	/* number of pixel columns on screen */
	unsigned short v_ccol;	/* number of pixel columns per character */
};
#define VT_RESIZEX      0x560A  /* set kernel's idea of screensize + more */
#define VT_LOCKSWITCH   0x560B  /* disallow vt switching */
#define VT_UNLOCKSWITCH 0x560C  /* allow vt switching */

#endif /* _LINUX_VT_H */
