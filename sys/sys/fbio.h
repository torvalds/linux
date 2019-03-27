/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software developed by the Computer Systems
 * Engineering group at Lawrence Berkeley Laboratory under DARPA
 * contract BG 91-66 and contributed to Berkeley.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)fbio.h	8.2 (Berkeley) 10/30/93
 *
 * $FreeBSD$
 */

#ifndef _SYS_FBIO_H_
#define _SYS_FBIO_H_

#ifndef _KERNEL
#include <sys/types.h>
#else
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/eventhandler.h>
#endif
#include <sys/ioccom.h>

/*
 * Frame buffer ioctls (from Sprite, trimmed to essentials for X11).
 */

/*
 * Frame buffer type codes.
 */
#define	FBTYPE_SUN1BW		0	/* multibus mono */
#define	FBTYPE_SUN1COLOR	1	/* multibus color */
#define	FBTYPE_SUN2BW		2	/* memory mono */
#define	FBTYPE_SUN2COLOR	3	/* color w/rasterop chips */
#define	FBTYPE_SUN2GP		4	/* GP1/GP2 */
#define	FBTYPE_SUN5COLOR	5	/* RoadRunner accelerator */
#define	FBTYPE_SUN3COLOR	6	/* memory color */
#define	FBTYPE_MEMCOLOR		7	/* memory 24-bit */
#define	FBTYPE_SUN4COLOR	8	/* memory color w/overlay */

#define	FBTYPE_NOTSUN1		9	/* reserved for customer */
#define	FBTYPE_NOTSUN2		10	/* reserved for customer */
#define	FBTYPE_PCIMISC		11	/* (generic) PCI misc. disp. */

#define	FBTYPE_SUNFAST_COLOR	12	/* accelerated 8bit */
#define	FBTYPE_SUNROP_COLOR	13	/* MEMCOLOR with rop h/w */
#define	FBTYPE_SUNFB_VIDEO	14	/* Simple video mixing */
#define	FBTYPE_RESERVED5	15	/* reserved, do not use */
#define	FBTYPE_RESERVED4	16	/* reserved, do not use */
#define	FBTYPE_SUNGP3		17
#define	FBTYPE_SUNGT		18
#define	FBTYPE_SUNLEO		19	/* zx Leo */

#define	FBTYPE_MDA		20
#define	FBTYPE_HERCULES		21
#define	FBTYPE_CGA		22
#define	FBTYPE_EGA		23
#define	FBTYPE_VGA		24
#define	FBTYPE_TGA		26
#define	FBTYPE_TGA2		27

#define	FBTYPE_MDICOLOR		28	/* cg14 */
#define	FBTYPE_TCXCOLOR		29	/* SUNW,tcx */
#define	FBTYPE_CREATOR		30

#define	FBTYPE_LASTPLUSONE	31	/* max number of fbs (change as add) */

/*
 * Frame buffer descriptor as returned by FBIOGTYPE.
 */
struct fbtype {
	int	fb_type;	/* as defined above */
	int	fb_height;	/* in pixels */
	int	fb_width;	/* in pixels */
	int	fb_depth;	/* bits per pixel */
	int	fb_cmsize;	/* size of color map (entries) */
	int	fb_size;	/* total size in bytes */
};
#define	FBIOGTYPE	_IOR('F', 0, struct fbtype)

#define	FBTYPE_GET_STRIDE(_fb)	((_fb)->fb_size / (_fb)->fb_height)
#define	FBTYPE_GET_BPP(_fb)	((_fb)->fb_bpp)
#define	FBTYPE_GET_BYTESPP(_fb)	((_fb)->fb_bpp / 8)

#ifdef	_KERNEL

struct fb_info;

typedef int fb_enter_t(void *priv);
typedef int fb_leave_t(void *priv);
typedef int fb_setblankmode_t(void *priv, int mode);

struct fb_info {
	/* Raw copy of fbtype. Do not change. */
	int		fb_type;	/* as defined above */
	int		fb_height;	/* in pixels */
	int		fb_width;	/* in pixels */
	int		fb_depth;	/* bits to define color */
	int		fb_cmsize;	/* size of color map (entries) */
	int		fb_size;	/* total size in bytes */

	struct cdev 	*fb_cdev;

	device_t	 fb_fbd_dev;	/* "fbd" device. */
	device_t	 fb_video_dev;	/* Video adapter. */

	fb_enter_t	*enter;
	fb_leave_t	*leave;
	fb_setblankmode_t *setblankmode;

	uintptr_t	fb_pbase;	/* For FB mmap. */
	uintptr_t	fb_vbase;	/* if NULL, use fb_write/fb_read. */
	void		*fb_priv;	/* First argument for read/write. */
	const char	*fb_name;
	uint32_t	fb_flags;
#define	FB_FLAG_NOMMAP		1	/* mmap unsupported. */
#define	FB_FLAG_NOWRITE		2	/* disable writes for the time being */
#define	FB_FLAG_MEMATTR		4	/* override memattr for mmap */
	vm_memattr_t	fb_memattr;
	int		fb_stride;
	int		fb_bpp;		/* bits per pixel */
	uint32_t	fb_cmap[16];
};

int fbd_list(void);
int fbd_register(struct fb_info *);
int fbd_unregister(struct fb_info *);

static inline int
register_framebuffer(struct fb_info *info)
{

	EVENTHANDLER_INVOKE(register_framebuffer, info);
	return (0);
}

static inline int
unregister_framebuffer(struct fb_info *info)
{

	EVENTHANDLER_INVOKE(unregister_framebuffer, info);
	return (0);
}
#endif

#ifdef notdef
/*
 * General purpose structure for passing info in and out of frame buffers
 * (used for gp1) -- unsupported.
 */
struct fbinfo {
	int	fb_physaddr;	/* physical frame buffer address */
	int	fb_hwwidth;	/* fb board width */
	int	fb_hwheight;	/* fb board height */
	int	fb_addrdelta;	/* phys addr diff between boards */
	u_char	*fb_ropaddr;	/* fb virtual addr */
	int	fb_unit;	/* minor devnum of fb */
};
#define	FBIOGINFO	_IOR('F', 2, struct fbinfo)
#endif

/*
 * Color map I/O.
 */
struct fbcmap {
	int	index;		/* first element (0 origin) */
	int	count;		/* number of elements */
	u_char	*red;		/* red color map elements */
	u_char	*green;		/* green color map elements */
	u_char	*blue;		/* blue color map elements */
};
#define	FBIOPUTCMAP	_IOW('F', 3, struct fbcmap)
#define	FBIOGETCMAP	_IOW('F', 4, struct fbcmap)

/*
 * Set/get attributes.
 */
#define	FB_ATTR_NDEVSPECIFIC	8	/* no. of device specific values */
#define	FB_ATTR_NEMUTYPES	4	/* no. of emulation types */

struct fbsattr {
	int	flags;			/* flags; see below */
	int	emu_type;		/* emulation type (-1 if unused) */
	int	dev_specific[FB_ATTR_NDEVSPECIFIC];	/* catchall */
};
#define	FB_ATTR_AUTOINIT	1	/* emulation auto init flag */
#define	FB_ATTR_DEVSPECIFIC	2	/* dev. specific stuff valid flag */

struct fbgattr {
	int	real_type;		/* real device type */
	int	owner;			/* PID of owner, 0 if myself */
	struct	fbtype fbtype;		/* fbtype info for real device */
	struct	fbsattr sattr;		/* see above */
	int	emu_types[FB_ATTR_NEMUTYPES];	/* possible emulations */
						/* (-1 if unused) */
};
#define	FBIOSATTR	_IOW('F', 5, struct fbsattr)
#define	FBIOGATTR	_IOR('F', 6, struct fbgattr)

/*
 * Video control.
 */
#define	FBVIDEO_OFF		0
#define	FBVIDEO_ON		1

#define	FBIOSVIDEO	_IOW('F', 7, int)
#define	FBIOGVIDEO	_IOR('F', 8, int)

/* vertical retrace */
#define	FBIOVERTICAL	_IO('F', 9)

/*
 * Hardware cursor control (for, e.g., CG6).  A rather complex and icky
 * interface that smells like VMS, but there it is....
 */
struct fbcurpos {
	short	x;
	short	y;
};

struct fbcursor {
	short	set;		/* flags; see below */
	short	enable;		/* nonzero => cursor on, 0 => cursor off */
	struct	fbcurpos pos;	/* position on display */
	struct	fbcurpos hot;	/* hot-spot within cursor */
	struct	fbcmap cmap;	/* cursor color map */
	struct	fbcurpos size;	/* number of valid bits in image & mask */
	caddr_t	image;		/* cursor image bits */
	caddr_t	mask;		/* cursor mask bits */
};
#define	FB_CUR_SETCUR	0x01	/* set on/off (i.e., obey fbcursor.enable) */
#define	FB_CUR_SETPOS	0x02	/* set position */
#define	FB_CUR_SETHOT	0x04	/* set hot-spot */
#define	FB_CUR_SETCMAP	0x08	/* set cursor color map */
#define	FB_CUR_SETSHAPE	0x10	/* set size & bits */
#define	FB_CUR_SETALL	(FB_CUR_SETCUR | FB_CUR_SETPOS | FB_CUR_SETHOT | \
			 FB_CUR_SETCMAP | FB_CUR_SETSHAPE)

/* controls for cursor attributes & shape (including position) */
#define	FBIOSCURSOR	_IOW('F', 24, struct fbcursor)
#define	FBIOGCURSOR	_IOWR('F', 25, struct fbcursor)

/* controls for cursor position only */
#define	FBIOSCURPOS	_IOW('F', 26, struct fbcurpos)
#define	FBIOGCURPOS	_IOW('F', 27, struct fbcurpos)

/* get maximum cursor size */
#define	FBIOGCURMAX	_IOR('F', 28, struct fbcurpos)

/*
 * Video board information
 */
struct brd_info {
	u_short		accessible_width; /* accessible bytes in scanline */
	u_short		accessible_height; /* number of accessible scanlines */
	u_short		line_bytes;	/* number of bytes/scanline */
	u_short		hdb_capable;	/* can this thing hardware db? */
	u_short		vmsize;		/* video memory size */
	u_char		boardrev;	/* board revision # */
	u_char		pad0;
	u_long		pad1;
};
#define	FBIOGXINFO	_IOR('F', 39, struct brd_info)

/*
 * Monitor information
 */
struct mon_info {
	u_long		mon_type;	/* bit array */
#define MON_TYPE_STEREO		0x8	/* stereo display */
#define MON_TYPE_0_OFFSET	0x4	/* black level 0 ire instead of 7.5 */
#define MON_TYPE_OVERSCAN	0x2	/* overscan */
#define MON_TYPE_GRAY		0x1	/* greyscale monitor */
	u_long		pixfreq;	/* pixel frequency in Hz */
	u_long		hfreq;		/* horizontal freq in Hz */
	u_long		vfreq;		/* vertical freq in Hz */
	u_long		vsync;		/* vertical sync in scanlines */
	u_long		hsync;		/* horizontal sync in pixels */
	/* these are in pixel units */
	u_short		hfporch;	/* horizontal front porch */
	u_short		hbporch;	/* horizontal back porch */
	u_short		vfporch;	/* vertical front porch */
	u_short		vbporch;	/* vertical back porch */
};
#define	FBIOMONINFO	_IOR('F', 40, struct mon_info)

/*
 * Color map I/O.
 */
struct fbcmap_i {
	unsigned int	flags;
#define	FB_CMAP_BLOCK	(1 << 0)	/* wait for vertical refresh */
#define	FB_CMAP_KERNEL	(1 << 1)	/* called within kernel */
	int		id;		/* color map id */
	int		index;		/* first element (0 origin) */
	int		count;		/* number of elements */
	u_char		*red;		/* red color map elements */
	u_char		*green;		/* green color map elements */
	u_char		*blue;		/* blue color map elements */
};
#define	FBIOPUTCMAPI	_IOW('F', 41, struct fbcmap_i)
#define	FBIOGETCMAPI	_IOW('F', 42, struct fbcmap_i)

/* The new style frame buffer ioctls. */

/* video mode information block */
struct video_info {
    int			vi_mode;	/* mode number, see below */
    int			vi_flags;
#define V_INFO_COLOR	(1 << 0)
#define V_INFO_GRAPHICS	(1 << 1)
#define V_INFO_LINEAR	(1 << 2)
#define V_INFO_VESA	(1 << 3)
#define	V_INFO_NONVGA	(1 << 4)
#define	V_INFO_CWIDTH9	(1 << 5)
    int			vi_width;
    int			vi_height;
    int			vi_cwidth;
    int			vi_cheight;
    int			vi_depth;
    int			vi_planes;
    vm_offset_t		vi_window;	/* physical address */
    size_t		vi_window_size;
    size_t		vi_window_gran;
    vm_offset_t		vi_buffer;	/* physical address */
    size_t		vi_buffer_size;
    int			vi_mem_model;
#define V_INFO_MM_OTHER  (-1)
#define V_INFO_MM_TEXT	 0
#define V_INFO_MM_PLANAR 1
#define V_INFO_MM_PACKED 2
#define V_INFO_MM_DIRECT 3
#define V_INFO_MM_CGA	 100
#define V_INFO_MM_HGC	 101
#define V_INFO_MM_VGAX	 102
    /* for MM_PACKED and MM_DIRECT only */
    int			vi_pixel_size;	/* in bytes */
    /* for MM_DIRECT only */
    int			vi_pixel_fields[4];	/* RGB and reserved fields */
    int			vi_pixel_fsizes[4];
    /* reserved */
    u_char		vi_reserved[64];
    vm_offset_t		vi_registers;	/* physical address */
    vm_offset_t		vi_registers_size;
};
typedef struct video_info video_info_t;

/* adapter infromation block */
struct video_adapter {
    int			va_index;
    int			va_type;
#define KD_OTHER	0		/* unknown */
#define KD_MONO		1		/* monochrome adapter */
#define KD_HERCULES	2		/* hercules adapter */
#define KD_CGA		3		/* color graphics adapter */
#define KD_EGA		4		/* enhanced graphics adapter */
#define KD_VGA		5		/* video graphics adapter */
#define KD_TGA		7		/* TGA */
#define KD_TGA2		8		/* TGA2 */
    char		*va_name;
    int			va_unit;
    int			va_minor;
    int			va_flags;
#define V_ADP_COLOR	(1 << 0)
#define V_ADP_MODECHANGE (1 << 1)
#define V_ADP_STATESAVE	(1 << 2)
#define V_ADP_STATELOAD	(1 << 3)
#define V_ADP_FONT	(1 << 4)
#define V_ADP_PALETTE	(1 << 5)
#define V_ADP_BORDER	(1 << 6)
#define V_ADP_VESA	(1 << 7)
#define V_ADP_BOOTDISPLAY (1 << 8)
#define V_ADP_PROBED	(1 << 16)
#define V_ADP_INITIALIZED (1 << 17)
#define V_ADP_REGISTERED (1 << 18)
#define V_ADP_ATTACHED	(1 << 19)
#define	V_ADP_DAC8	(1 << 20)
#define	V_ADP_CWIDTH9	(1 << 21)
    vm_offset_t		va_io_base;
    int			va_io_size;
    vm_offset_t		va_crtc_addr;
    vm_offset_t		va_mem_base;
    int			va_mem_size;
    vm_offset_t		va_window;	/* virtual address */
    size_t		va_window_size;
    size_t		va_window_gran;
    u_int		va_window_orig;
    vm_offset_t		va_buffer;	/* virtual address */
    size_t		va_buffer_size;
    int			va_initial_mode;
    int			va_initial_bios_mode;
    int			va_mode;
    struct video_info	va_info;
    int			va_line_width;
    struct {
	int		x;
	int		y;
    } 			va_disp_start;
    void		*va_token;
    int			va_model;
    int			va_little_bitian;
    int			va_little_endian;
    int			va_buffer_alias;
    vm_offset_t		va_registers;	/* virtual address */
    vm_offset_t		va_registers_size;
};
typedef struct video_adapter video_adapter_t;

struct video_adapter_info {
    int			va_index;
    int			va_type;
    char		va_name[16];
    int			va_unit;
    int			va_flags;
    vm_offset_t		va_io_base;
    int			va_io_size;
    vm_offset_t		va_crtc_addr;
    vm_offset_t		va_mem_base;
    int			va_mem_size;
    vm_offset_t		va_window;	/* virtual address */
    size_t		va_window_size;
    size_t		va_window_gran;
    vm_offset_t		va_unused0;
    size_t		va_buffer_size;
    int			va_initial_mode;
    int			va_initial_bios_mode;
    int			va_mode;
    int			va_line_width;
    struct {
	int		x;
	int		y;
    } 			va_disp_start;
    u_int		va_window_orig;
    /* reserved */
    u_char		va_reserved[64];
};
typedef struct video_adapter_info video_adapter_info_t;

/* some useful video adapter index */
#define V_ADP_PRIMARY	0
#define V_ADP_SECONDARY	1

/* video mode numbers */

#define M_B40x25	0	/* black & white 40 columns */
#define M_C40x25	1	/* color 40 columns */
#define M_B80x25	2	/* black & white 80 columns */
#define M_C80x25	3	/* color 80 columns */
#define M_BG320		4	/* black & white graphics 320x200 */
#define M_CG320		5	/* color graphics 320x200 */
#define M_BG640		6	/* black & white graphics 640x200 hi-res */
#define M_EGAMONO80x25  7       /* ega-mono 80x25 */
#define M_CG320_D	13	/* ega mode D */
#define M_CG640_E	14	/* ega mode E */
#define M_EGAMONOAPA	15	/* ega mode F */
#define M_CG640x350	16	/* ega mode 10 */
#define M_ENHMONOAPA2	17	/* ega mode F with extended memory */
#define M_ENH_CG640	18	/* ega mode 10* */
#define M_ENH_B40x25    19      /* ega enhanced black & white 40 columns */
#define M_ENH_C40x25    20      /* ega enhanced color 40 columns */
#define M_ENH_B80x25    21      /* ega enhanced black & white 80 columns */
#define M_ENH_C80x25    22      /* ega enhanced color 80 columns */
#define M_VGA_C40x25	23	/* vga 8x16 font on color */
#define M_VGA_C80x25	24	/* vga 8x16 font on color */
#define M_VGA_M80x25	25	/* vga 8x16 font on mono */

#define M_VGA11		26	/* vga 640x480 2 colors */
#define M_BG640x480	26
#define M_VGA12		27	/* vga 640x480 16 colors */
#define M_CG640x480	27
#define M_VGA13		28	/* vga 320x200 256 colors */
#define M_VGA_CG320	28

#define M_VGA_C80x50	30	/* vga 8x8 font on color */
#define M_VGA_M80x50	31	/* vga 8x8 font on color */
#define M_VGA_C80x30	32	/* vga 8x16 font on color */
#define M_VGA_M80x30	33	/* vga 8x16 font on color */
#define M_VGA_C80x60	34	/* vga 8x8 font on color */
#define M_VGA_M80x60	35	/* vga 8x8 font on color */
#define M_VGA_CG640	36	/* vga 640x400 256 color */
#define M_VGA_MODEX	37	/* vga 320x240 256 color */

#define M_VGA_C90x25	40	/* vga 8x16 font on color */
#define M_VGA_M90x25	41	/* vga 8x16 font on mono */
#define M_VGA_C90x30	42	/* vga 8x16 font on color */
#define M_VGA_M90x30	43	/* vga 8x16 font on mono */
#define M_VGA_C90x43	44	/* vga 8x8 font on color */
#define M_VGA_M90x43	45	/* vga 8x8 font on mono */
#define M_VGA_C90x50	46	/* vga 8x8 font on color */
#define M_VGA_M90x50	47	/* vga 8x8 font on mono */
#define M_VGA_C90x60	48	/* vga 8x8 font on color */
#define M_VGA_M90x60	49	/* vga 8x8 font on mono */

#define M_ENH_B80x43	0x70	/* ega black & white 80x43 */
#define M_ENH_C80x43	0x71	/* ega color 80x43 */

#define M_HGC_P0	0xe0	/* hercules graphics - page 0 @ B0000 */
#define M_HGC_P1	0xe1	/* hercules graphics - page 1 @ B8000 */
#define M_MCA_MODE	0xff	/* monochrome adapter mode */

#define M_TEXT_80x25	200	/* generic text modes */
#define M_TEXT_80x30	201
#define M_TEXT_80x43	202
#define M_TEXT_80x50	203
#define M_TEXT_80x60	204
#define M_TEXT_132x25	205
#define M_TEXT_132x30	206
#define M_TEXT_132x43	207
#define M_TEXT_132x50	208
#define M_TEXT_132x60	209

#define M_VESA_BASE		0x100	/* VESA mode number base */
#define M_VESA_CG640x400	0x100	/* 640x400, 256 color */
#define M_VESA_CG640x480	0x101	/* 640x480, 256 color */
#define M_VESA_800x600		0x102	/* 800x600, 16 color */
#define M_VESA_CG800x600	0x103	/* 800x600, 256 color */
#define M_VESA_1024x768		0x104	/* 1024x768, 16 color */
#define M_VESA_CG1024x768	0x105	/* 1024x768, 256 color */
#define M_VESA_1280x1024	0x106	/* 1280x1024, 16 color */
#define M_VESA_CG1280x1024	0x107	/* 1280x1024, 256 color */
#define M_VESA_C80x60		0x108	/* 8x8 font */
#define M_VESA_C132x25		0x109	/* 8x16 font */
#define M_VESA_C132x43		0x10a	/* 8x14 font */
#define M_VESA_C132x50		0x10b	/* 8x8 font */
#define M_VESA_C132x60		0x10c	/* 8x8 font */
#define M_VESA_32K_320		0x10d	/* 320x200, 5:5:5 */
#define M_VESA_64K_320		0x10e	/* 320x200, 5:6:5 */
#define M_VESA_FULL_320		0x10f	/* 320x200, 8:8:8 */
#define M_VESA_32K_640		0x110	/* 640x480, 5:5:5 */
#define M_VESA_64K_640		0x111	/* 640x480, 5:6:5 */
#define M_VESA_FULL_640		0x112	/* 640x480, 8:8:8 */
#define M_VESA_32K_800		0x113	/* 800x600, 5:5:5 */
#define M_VESA_64K_800		0x114	/* 800x600, 5:6:5 */
#define M_VESA_FULL_800		0x115	/* 800x600, 8:8:8 */
#define M_VESA_32K_1024		0x116	/* 1024x768, 5:5:5 */
#define M_VESA_64K_1024		0x117	/* 1024x768, 5:6:5 */
#define M_VESA_FULL_1024	0x118	/* 1024x768, 8:8:8 */
#define M_VESA_32K_1280		0x119	/* 1280x1024, 5:5:5 */
#define M_VESA_64K_1280		0x11a	/* 1280x1024, 5:6:5 */
#define M_VESA_FULL_1280	0x11b	/* 1280x1024, 8:8:8 */
#define M_VESA_MODE_MAX		0x1ff

struct video_display_start {
	int		x;
	int		y;
};
typedef struct video_display_start video_display_start_t;

struct video_color_palette {
	int		index;		/* first element (zero-based) */
	int		count;		/* number of elements */
	u_char		*red;		/* red */
	u_char		*green;		/* green */
	u_char		*blue;		/* blue */
	u_char		*transparent;	/* may be NULL */
};
typedef struct video_color_palette video_color_palette_t;

/* adapter info. */
#define FBIO_ADAPTER	_IOR('F', 100, int)
#define FBIO_ADPTYPE	_IOR('F', 101, int)
#define FBIO_ADPINFO	_IOR('F', 102, struct video_adapter_info)

/* video mode control */
#define FBIO_MODEINFO	_IOWR('F', 103, struct video_info)
#define FBIO_FINDMODE	_IOWR('F', 104, struct video_info)
#define FBIO_GETMODE	_IOR('F', 105, int)
#define FBIO_SETMODE	_IOW('F', 106, int)

/* get/set frame buffer window origin */
#define FBIO_GETWINORG	_IOR('F', 107, u_int)
#define FBIO_SETWINORG	_IOW('F', 108, u_int)

/* get/set display start address */
#define FBIO_GETDISPSTART	_IOR('F', 109, video_display_start_t) 
#define FBIO_SETDISPSTART	_IOW('F', 110, video_display_start_t)

/* get/set scan line width */
#define FBIO_GETLINEWIDTH	_IOR('F', 111, u_int) 
#define FBIO_SETLINEWIDTH	_IOW('F', 112, u_int)

/* color palette control */
#define FBIO_GETPALETTE	_IOW('F', 113, video_color_palette_t)
#define FBIO_SETPALETTE	_IOW('F', 114, video_color_palette_t)

/* blank display */
#define V_DISPLAY_ON		0
#define V_DISPLAY_BLANK		1
#define V_DISPLAY_STAND_BY	2
#define V_DISPLAY_SUSPEND	3

#define FBIO_BLANK	_IOW('F', 115, int)

#endif /* !_SYS_FBIO_H_ */
