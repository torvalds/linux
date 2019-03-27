/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#ifndef _DEV_FB_VGAREG_H_
#define _DEV_FB_VGAREG_H_

/* physical addresses */
#define MDA_BUF_BASE		0xb0000
#define MDA_BUF_SIZE		0x08000
#define MDA_BUF			BIOS_PADDRTOVADDR(MDA_BUF_BASE)
#define CGA_BUF_BASE		0xb8000
#define CGA_BUF_SIZE		0x08000
#define CGA_BUF			BIOS_PADDRTOVADDR(CGA_BUF_BASE)
#define EGA_BUF_BASE		0xa0000
#define EGA_BUF_SIZE		0x20000
#define EGA_BUF			BIOS_PADDRTOVADDR(EGA_BUF_BASE)
#define GRAPHICS_BUF_BASE	0xa0000
#define GRAPHICS_BUF_SIZE	0x10000
#define GRAPHICS_BUF		BIOS_PADDRTOVADDR(GRAPHICS_BUF_BASE)
#define FONT_BUF		BIOS_PADDRTOVADDR(GRAPHICS_BUF_BASE)
#define VIDEO_BUF_BASE		0xa0000
#define VIDEO_BUF_SIZE		0x20000

/* I/O port addresses */
#define MONO_CRTC	(IO_MDA + 0x04)		/* crt controller base mono */
#define COLOR_CRTC	(IO_CGA + 0x04)		/* crt controller base color */
#define MISC		(IO_VGA + 0x02)		/* misc output register */
#define ATC		(IO_VGA + 0x00)		/* attribute controller */
#define TSIDX		(IO_VGA + 0x04)		/* timing sequencer idx */
#define TSREG		(IO_VGA + 0x05)		/* timing sequencer data */
#define PIXMASK		(IO_VGA + 0x06)		/* pixel write mask */
#define PALRADR		(IO_VGA + 0x07)		/* palette read address */
#define PALWADR		(IO_VGA + 0x08)		/* palette write address */
#define PALDATA		(IO_VGA + 0x09)		/* palette data register */
#define GDCIDX		(IO_VGA + 0x0E)		/* graph data controller idx */
#define GDCREG		(IO_VGA + 0x0F)		/* graph data controller data */

#define VGA_DRIVER_NAME		"vga"
#define VGA_UNIT(dev)		dev2unit(dev)
#define VGA_MKMINOR(unit)	(unit)

#ifdef _KERNEL

struct video_adapter;
typedef struct vga_softc {
	struct video_adapter	*adp;
	void			*state_buf;
	void			*pal_buf;
#ifdef FB_INSTALL_CDEV
	genfb_softc_t		gensc;
#endif
} vga_softc_t;

int		vga_probe_unit(int unit, struct video_adapter *adp, int flags);
int		vga_attach_unit(int unit, vga_softc_t *sc, int flags);

#ifdef FB_INSTALL_CDEV
int		vga_open(struct cdev *dev, vga_softc_t *sc, int flag, int mode,
			 struct thread *td);
int		vga_close(struct cdev *dev, vga_softc_t *sc, int flag, int mode,
			  struct thread *td);
int		vga_read(struct cdev *dev, vga_softc_t *sc, struct uio *uio, int flag);
int		vga_write(struct cdev *dev, vga_softc_t *sc, struct uio *uio, int flag);
int		vga_ioctl(struct cdev *dev, vga_softc_t *sc, u_long cmd, caddr_t arg,
			  int flag, struct thread *td);
int		vga_mmap(struct cdev *dev, vga_softc_t *sc, vm_ooffset_t offset,
			 vm_paddr_t *paddr, int prot, vm_memattr_t *memattr);
#endif

extern int	(*vga_sub_configure)(int flags);

#endif /* _KERNEL */

#endif /* _DEV_FB_VGAREG_H_ */
