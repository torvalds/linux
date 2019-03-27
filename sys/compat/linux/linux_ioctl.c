/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1994-1995 Søren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "opt_compat.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/capsicum.h>
#include <sys/cdio.h>
#include <sys/dvdio.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/consio.h>
#include <sys/ctype.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/filio.h>
#include <sys/jail.h>
#include <sys/kbio.h>
#include <sys/kernel.h>
#include <sys/linker_set.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/soundcard.h>
#include <sys/stdint.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/tty.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/resourcevar.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_types.h>

#include <dev/evdev/input.h>
#include <dev/usb/usb_ioctl.h>

#ifdef COMPAT_LINUX32
#include <machine/../linux32/linux.h>
#include <machine/../linux32/linux32_proto.h>
#else
#include <machine/../linux/linux.h>
#include <machine/../linux/linux_proto.h>
#endif

#include <compat/linux/linux_ioctl.h>
#include <compat/linux/linux_mib.h>
#include <compat/linux/linux_socket.h>
#include <compat/linux/linux_timer.h>
#include <compat/linux/linux_util.h>

#include <contrib/v4l/videodev.h>
#include <compat/linux/linux_videodev_compat.h>

#include <contrib/v4l/videodev2.h>
#include <compat/linux/linux_videodev2_compat.h>

#include <cam/scsi/scsi_sg.h>

CTASSERT(LINUX_IFNAMSIZ == IFNAMSIZ);

static linux_ioctl_function_t linux_ioctl_cdrom;
static linux_ioctl_function_t linux_ioctl_vfat;
static linux_ioctl_function_t linux_ioctl_console;
static linux_ioctl_function_t linux_ioctl_hdio;
static linux_ioctl_function_t linux_ioctl_disk;
static linux_ioctl_function_t linux_ioctl_socket;
static linux_ioctl_function_t linux_ioctl_sound;
static linux_ioctl_function_t linux_ioctl_termio;
static linux_ioctl_function_t linux_ioctl_private;
static linux_ioctl_function_t linux_ioctl_drm;
static linux_ioctl_function_t linux_ioctl_sg;
static linux_ioctl_function_t linux_ioctl_v4l;
static linux_ioctl_function_t linux_ioctl_v4l2;
static linux_ioctl_function_t linux_ioctl_special;
static linux_ioctl_function_t linux_ioctl_fbsd_usb;
static linux_ioctl_function_t linux_ioctl_evdev;

static struct linux_ioctl_handler cdrom_handler =
{ linux_ioctl_cdrom, LINUX_IOCTL_CDROM_MIN, LINUX_IOCTL_CDROM_MAX };
static struct linux_ioctl_handler vfat_handler =
{ linux_ioctl_vfat, LINUX_IOCTL_VFAT_MIN, LINUX_IOCTL_VFAT_MAX };
static struct linux_ioctl_handler console_handler =
{ linux_ioctl_console, LINUX_IOCTL_CONSOLE_MIN, LINUX_IOCTL_CONSOLE_MAX };
static struct linux_ioctl_handler hdio_handler =
{ linux_ioctl_hdio, LINUX_IOCTL_HDIO_MIN, LINUX_IOCTL_HDIO_MAX };
static struct linux_ioctl_handler disk_handler =
{ linux_ioctl_disk, LINUX_IOCTL_DISK_MIN, LINUX_IOCTL_DISK_MAX };
static struct linux_ioctl_handler socket_handler =
{ linux_ioctl_socket, LINUX_IOCTL_SOCKET_MIN, LINUX_IOCTL_SOCKET_MAX };
static struct linux_ioctl_handler sound_handler =
{ linux_ioctl_sound, LINUX_IOCTL_SOUND_MIN, LINUX_IOCTL_SOUND_MAX };
static struct linux_ioctl_handler termio_handler =
{ linux_ioctl_termio, LINUX_IOCTL_TERMIO_MIN, LINUX_IOCTL_TERMIO_MAX };
static struct linux_ioctl_handler private_handler =
{ linux_ioctl_private, LINUX_IOCTL_PRIVATE_MIN, LINUX_IOCTL_PRIVATE_MAX };
static struct linux_ioctl_handler drm_handler =
{ linux_ioctl_drm, LINUX_IOCTL_DRM_MIN, LINUX_IOCTL_DRM_MAX };
static struct linux_ioctl_handler sg_handler =
{ linux_ioctl_sg, LINUX_IOCTL_SG_MIN, LINUX_IOCTL_SG_MAX };
static struct linux_ioctl_handler video_handler =
{ linux_ioctl_v4l, LINUX_IOCTL_VIDEO_MIN, LINUX_IOCTL_VIDEO_MAX };
static struct linux_ioctl_handler video2_handler =
{ linux_ioctl_v4l2, LINUX_IOCTL_VIDEO2_MIN, LINUX_IOCTL_VIDEO2_MAX };
static struct linux_ioctl_handler fbsd_usb =
{ linux_ioctl_fbsd_usb, FBSD_LUSB_MIN, FBSD_LUSB_MAX };
static struct linux_ioctl_handler evdev_handler =
{ linux_ioctl_evdev, LINUX_IOCTL_EVDEV_MIN, LINUX_IOCTL_EVDEV_MAX };

DATA_SET(linux_ioctl_handler_set, cdrom_handler);
DATA_SET(linux_ioctl_handler_set, vfat_handler);
DATA_SET(linux_ioctl_handler_set, console_handler);
DATA_SET(linux_ioctl_handler_set, hdio_handler);
DATA_SET(linux_ioctl_handler_set, disk_handler);
DATA_SET(linux_ioctl_handler_set, socket_handler);
DATA_SET(linux_ioctl_handler_set, sound_handler);
DATA_SET(linux_ioctl_handler_set, termio_handler);
DATA_SET(linux_ioctl_handler_set, private_handler);
DATA_SET(linux_ioctl_handler_set, drm_handler);
DATA_SET(linux_ioctl_handler_set, sg_handler);
DATA_SET(linux_ioctl_handler_set, video_handler);
DATA_SET(linux_ioctl_handler_set, video2_handler);
DATA_SET(linux_ioctl_handler_set, fbsd_usb);
DATA_SET(linux_ioctl_handler_set, evdev_handler);

#ifdef __i386__
static TAILQ_HEAD(, linux_ioctl_handler_element) linux_ioctl_handlers =
    TAILQ_HEAD_INITIALIZER(linux_ioctl_handlers);
static struct sx linux_ioctl_sx;
SX_SYSINIT(linux_ioctl, &linux_ioctl_sx, "Linux ioctl handlers");
#else
extern TAILQ_HEAD(, linux_ioctl_handler_element) linux_ioctl_handlers;
extern struct sx linux_ioctl_sx;
#endif
#ifdef COMPAT_LINUX32
static TAILQ_HEAD(, linux_ioctl_handler_element) linux32_ioctl_handlers =
    TAILQ_HEAD_INITIALIZER(linux32_ioctl_handlers);
#endif

/*
 * hdio related ioctls for VMWare support
 */

struct linux_hd_geometry {
	u_int8_t	heads;
	u_int8_t	sectors;
	u_int16_t	cylinders;
	u_int32_t	start;
};

struct linux_hd_big_geometry {
	u_int8_t	heads;
	u_int8_t	sectors;
	u_int32_t	cylinders;
	u_int32_t	start;
};

static int
linux_ioctl_hdio(struct thread *td, struct linux_ioctl_args *args)
{
	struct file *fp;
	int error;
	u_int sectorsize, fwcylinders, fwheads, fwsectors;
	off_t mediasize, bytespercyl;

	error = fget(td, args->fd, &cap_ioctl_rights, &fp);
	if (error != 0)
		return (error);
	switch (args->cmd & 0xffff) {
	case LINUX_HDIO_GET_GEO:
	case LINUX_HDIO_GET_GEO_BIG:
		error = fo_ioctl(fp, DIOCGMEDIASIZE,
			(caddr_t)&mediasize, td->td_ucred, td);
		if (!error)
			error = fo_ioctl(fp, DIOCGSECTORSIZE,
				(caddr_t)&sectorsize, td->td_ucred, td);
		if (!error)
			error = fo_ioctl(fp, DIOCGFWHEADS,
				(caddr_t)&fwheads, td->td_ucred, td);
		if (!error)
			error = fo_ioctl(fp, DIOCGFWSECTORS,
				(caddr_t)&fwsectors, td->td_ucred, td);
		/*
		 * XXX: DIOCGFIRSTOFFSET is not yet implemented, so
		 * so pretend that GEOM always says 0. This is NOT VALID
		 * for slices or partitions, only the per-disk raw devices.
		 */

		fdrop(fp, td);
		if (error)
			return (error);
		/*
		 * 1. Calculate the number of bytes in a cylinder,
		 *    given the firmware's notion of heads and sectors
		 *    per cylinder.
		 * 2. Calculate the number of cylinders, given the total
		 *    size of the media.
		 * All internal calculations should have 64-bit precision.
		 */
		bytespercyl = (off_t) sectorsize * fwheads * fwsectors;
		fwcylinders = mediasize / bytespercyl;
#if defined(DEBUG)
		linux_msg(td, "HDIO_GET_GEO: mediasize %jd, c/h/s %d/%d/%d, "
			  "bpc %jd",
			  (intmax_t)mediasize, fwcylinders, fwheads, fwsectors,
			  (intmax_t)bytespercyl);
#endif
		if ((args->cmd & 0xffff) == LINUX_HDIO_GET_GEO) {
			struct linux_hd_geometry hdg;

			hdg.cylinders = fwcylinders;
			hdg.heads = fwheads;
			hdg.sectors = fwsectors;
			hdg.start = 0;
			error = copyout(&hdg, (void *)args->arg, sizeof(hdg));
		} else if ((args->cmd & 0xffff) == LINUX_HDIO_GET_GEO_BIG) {
			struct linux_hd_big_geometry hdbg;

			memset(&hdbg, 0, sizeof(hdbg));
			hdbg.cylinders = fwcylinders;
			hdbg.heads = fwheads;
			hdbg.sectors = fwsectors;
			hdbg.start = 0;
			error = copyout(&hdbg, (void *)args->arg, sizeof(hdbg));
		}
		return (error);
		break;
	default:
		/* XXX */
		linux_msg(td,
			"ioctl fd=%d, cmd=0x%x ('%c',%d) is not implemented",
			args->fd, (int)(args->cmd & 0xffff),
			(int)(args->cmd & 0xff00) >> 8,
			(int)(args->cmd & 0xff));
		break;
	}
	fdrop(fp, td);
	return (ENOIOCTL);
}

static int
linux_ioctl_disk(struct thread *td, struct linux_ioctl_args *args)
{
	struct file *fp;
	int error;
	u_int sectorsize;
	off_t mediasize;

	error = fget(td, args->fd, &cap_ioctl_rights, &fp);
	if (error != 0)
		return (error);
	switch (args->cmd & 0xffff) {
	case LINUX_BLKGETSIZE:
		error = fo_ioctl(fp, DIOCGSECTORSIZE,
		    (caddr_t)&sectorsize, td->td_ucred, td);
		if (!error)
			error = fo_ioctl(fp, DIOCGMEDIASIZE,
			    (caddr_t)&mediasize, td->td_ucred, td);
		fdrop(fp, td);
		if (error)
			return (error);
		sectorsize = mediasize / sectorsize;
		/*
		 * XXX: How do we know we return the right size of integer ?
		 */
		return (copyout(&sectorsize, (void *)args->arg,
		    sizeof(sectorsize)));
		break;
	case LINUX_BLKSSZGET:
		error = fo_ioctl(fp, DIOCGSECTORSIZE,
		    (caddr_t)&sectorsize, td->td_ucred, td);
		fdrop(fp, td);
		if (error)
			return (error);
		return (copyout(&sectorsize, (void *)args->arg,
		    sizeof(sectorsize)));
		break;
	}
	fdrop(fp, td);
	return (ENOIOCTL);
}

/*
 * termio related ioctls
 */

struct linux_termio {
	unsigned short c_iflag;
	unsigned short c_oflag;
	unsigned short c_cflag;
	unsigned short c_lflag;
	unsigned char c_line;
	unsigned char c_cc[LINUX_NCC];
};

struct linux_termios {
	unsigned int c_iflag;
	unsigned int c_oflag;
	unsigned int c_cflag;
	unsigned int c_lflag;
	unsigned char c_line;
	unsigned char c_cc[LINUX_NCCS];
};

struct linux_winsize {
	unsigned short ws_row, ws_col;
	unsigned short ws_xpixel, ws_ypixel;
};

struct speedtab {
	int sp_speed;			/* Speed. */
	int sp_code;			/* Code. */
};

static struct speedtab sptab[] = {
	{ B0, LINUX_B0 }, { B50, LINUX_B50 },
	{ B75, LINUX_B75 }, { B110, LINUX_B110 },
	{ B134, LINUX_B134 }, { B150, LINUX_B150 },
	{ B200, LINUX_B200 }, { B300, LINUX_B300 },
	{ B600, LINUX_B600 }, { B1200, LINUX_B1200 },
	{ B1800, LINUX_B1800 }, { B2400, LINUX_B2400 },
	{ B4800, LINUX_B4800 }, { B9600, LINUX_B9600 },
	{ B19200, LINUX_B19200 }, { B38400, LINUX_B38400 },
	{ B57600, LINUX_B57600 }, { B115200, LINUX_B115200 },
	{-1, -1 }
};

struct linux_serial_struct {
	int	type;
	int	line;
	int	port;
	int	irq;
	int	flags;
	int	xmit_fifo_size;
	int	custom_divisor;
	int	baud_base;
	unsigned short close_delay;
	char	reserved_char[2];
	int	hub6;
	unsigned short closing_wait;
	unsigned short closing_wait2;
	int	reserved[4];
};

static int
linux_to_bsd_speed(int code, struct speedtab *table)
{
	for ( ; table->sp_code != -1; table++)
		if (table->sp_code == code)
			return (table->sp_speed);
	return (-1);
}

static int
bsd_to_linux_speed(int speed, struct speedtab *table)
{
	for ( ; table->sp_speed != -1; table++)
		if (table->sp_speed == speed)
			return (table->sp_code);
	return (-1);
}

static void
bsd_to_linux_termios(struct termios *bios, struct linux_termios *lios)
{
	int i;

#ifdef DEBUG
	if (ldebug(ioctl)) {
		printf("LINUX: BSD termios structure (input):\n");
		printf("i=%08x o=%08x c=%08x l=%08x ispeed=%d ospeed=%d\n",
		    bios->c_iflag, bios->c_oflag, bios->c_cflag, bios->c_lflag,
		    bios->c_ispeed, bios->c_ospeed);
		printf("c_cc ");
		for (i=0; i<NCCS; i++)
			printf("%02x ", bios->c_cc[i]);
		printf("\n");
	}
#endif

	lios->c_iflag = 0;
	if (bios->c_iflag & IGNBRK)
		lios->c_iflag |= LINUX_IGNBRK;
	if (bios->c_iflag & BRKINT)
		lios->c_iflag |= LINUX_BRKINT;
	if (bios->c_iflag & IGNPAR)
		lios->c_iflag |= LINUX_IGNPAR;
	if (bios->c_iflag & PARMRK)
		lios->c_iflag |= LINUX_PARMRK;
	if (bios->c_iflag & INPCK)
		lios->c_iflag |= LINUX_INPCK;
	if (bios->c_iflag & ISTRIP)
		lios->c_iflag |= LINUX_ISTRIP;
	if (bios->c_iflag & INLCR)
		lios->c_iflag |= LINUX_INLCR;
	if (bios->c_iflag & IGNCR)
		lios->c_iflag |= LINUX_IGNCR;
	if (bios->c_iflag & ICRNL)
		lios->c_iflag |= LINUX_ICRNL;
	if (bios->c_iflag & IXON)
		lios->c_iflag |= LINUX_IXON;
	if (bios->c_iflag & IXANY)
		lios->c_iflag |= LINUX_IXANY;
	if (bios->c_iflag & IXOFF)
		lios->c_iflag |= LINUX_IXOFF;
	if (bios->c_iflag & IMAXBEL)
		lios->c_iflag |= LINUX_IMAXBEL;

	lios->c_oflag = 0;
	if (bios->c_oflag & OPOST)
		lios->c_oflag |= LINUX_OPOST;
	if (bios->c_oflag & ONLCR)
		lios->c_oflag |= LINUX_ONLCR;
	if (bios->c_oflag & TAB3)
		lios->c_oflag |= LINUX_XTABS;

	lios->c_cflag = bsd_to_linux_speed(bios->c_ispeed, sptab);
	lios->c_cflag |= (bios->c_cflag & CSIZE) >> 4;
	if (bios->c_cflag & CSTOPB)
		lios->c_cflag |= LINUX_CSTOPB;
	if (bios->c_cflag & CREAD)
		lios->c_cflag |= LINUX_CREAD;
	if (bios->c_cflag & PARENB)
		lios->c_cflag |= LINUX_PARENB;
	if (bios->c_cflag & PARODD)
		lios->c_cflag |= LINUX_PARODD;
	if (bios->c_cflag & HUPCL)
		lios->c_cflag |= LINUX_HUPCL;
	if (bios->c_cflag & CLOCAL)
		lios->c_cflag |= LINUX_CLOCAL;
	if (bios->c_cflag & CRTSCTS)
		lios->c_cflag |= LINUX_CRTSCTS;

	lios->c_lflag = 0;
	if (bios->c_lflag & ISIG)
		lios->c_lflag |= LINUX_ISIG;
	if (bios->c_lflag & ICANON)
		lios->c_lflag |= LINUX_ICANON;
	if (bios->c_lflag & ECHO)
		lios->c_lflag |= LINUX_ECHO;
	if (bios->c_lflag & ECHOE)
		lios->c_lflag |= LINUX_ECHOE;
	if (bios->c_lflag & ECHOK)
		lios->c_lflag |= LINUX_ECHOK;
	if (bios->c_lflag & ECHONL)
		lios->c_lflag |= LINUX_ECHONL;
	if (bios->c_lflag & NOFLSH)
		lios->c_lflag |= LINUX_NOFLSH;
	if (bios->c_lflag & TOSTOP)
		lios->c_lflag |= LINUX_TOSTOP;
	if (bios->c_lflag & ECHOCTL)
		lios->c_lflag |= LINUX_ECHOCTL;
	if (bios->c_lflag & ECHOPRT)
		lios->c_lflag |= LINUX_ECHOPRT;
	if (bios->c_lflag & ECHOKE)
		lios->c_lflag |= LINUX_ECHOKE;
	if (bios->c_lflag & FLUSHO)
		lios->c_lflag |= LINUX_FLUSHO;
	if (bios->c_lflag & PENDIN)
		lios->c_lflag |= LINUX_PENDIN;
	if (bios->c_lflag & IEXTEN)
		lios->c_lflag |= LINUX_IEXTEN;

	for (i=0; i<LINUX_NCCS; i++)
		lios->c_cc[i] = LINUX_POSIX_VDISABLE;
	lios->c_cc[LINUX_VINTR] = bios->c_cc[VINTR];
	lios->c_cc[LINUX_VQUIT] = bios->c_cc[VQUIT];
	lios->c_cc[LINUX_VERASE] = bios->c_cc[VERASE];
	lios->c_cc[LINUX_VKILL] = bios->c_cc[VKILL];
	lios->c_cc[LINUX_VEOF] = bios->c_cc[VEOF];
	lios->c_cc[LINUX_VEOL] = bios->c_cc[VEOL];
	lios->c_cc[LINUX_VMIN] = bios->c_cc[VMIN];
	lios->c_cc[LINUX_VTIME] = bios->c_cc[VTIME];
	lios->c_cc[LINUX_VEOL2] = bios->c_cc[VEOL2];
	lios->c_cc[LINUX_VSUSP] = bios->c_cc[VSUSP];
	lios->c_cc[LINUX_VSTART] = bios->c_cc[VSTART];
	lios->c_cc[LINUX_VSTOP] = bios->c_cc[VSTOP];
	lios->c_cc[LINUX_VREPRINT] = bios->c_cc[VREPRINT];
	lios->c_cc[LINUX_VDISCARD] = bios->c_cc[VDISCARD];
	lios->c_cc[LINUX_VWERASE] = bios->c_cc[VWERASE];
	lios->c_cc[LINUX_VLNEXT] = bios->c_cc[VLNEXT];

	for (i=0; i<LINUX_NCCS; i++) {
		if (i != LINUX_VMIN && i != LINUX_VTIME &&
		    lios->c_cc[i] == _POSIX_VDISABLE)
			lios->c_cc[i] = LINUX_POSIX_VDISABLE;
	}
	lios->c_line = 0;

#ifdef DEBUG
	if (ldebug(ioctl)) {
		printf("LINUX: LINUX termios structure (output):\n");
		printf("i=%08x o=%08x c=%08x l=%08x line=%d\n",
		    lios->c_iflag, lios->c_oflag, lios->c_cflag,
		    lios->c_lflag, (int)lios->c_line);
		printf("c_cc ");
		for (i=0; i<LINUX_NCCS; i++)
			printf("%02x ", lios->c_cc[i]);
		printf("\n");
	}
#endif
}

static void
linux_to_bsd_termios(struct linux_termios *lios, struct termios *bios)
{
	int i;

#ifdef DEBUG
	if (ldebug(ioctl)) {
		printf("LINUX: LINUX termios structure (input):\n");
		printf("i=%08x o=%08x c=%08x l=%08x line=%d\n",
		    lios->c_iflag, lios->c_oflag, lios->c_cflag,
		    lios->c_lflag, (int)lios->c_line);
		printf("c_cc ");
		for (i=0; i<LINUX_NCCS; i++)
			printf("%02x ", lios->c_cc[i]);
		printf("\n");
	}
#endif

	bios->c_iflag = 0;
	if (lios->c_iflag & LINUX_IGNBRK)
		bios->c_iflag |= IGNBRK;
	if (lios->c_iflag & LINUX_BRKINT)
		bios->c_iflag |= BRKINT;
	if (lios->c_iflag & LINUX_IGNPAR)
		bios->c_iflag |= IGNPAR;
	if (lios->c_iflag & LINUX_PARMRK)
		bios->c_iflag |= PARMRK;
	if (lios->c_iflag & LINUX_INPCK)
		bios->c_iflag |= INPCK;
	if (lios->c_iflag & LINUX_ISTRIP)
		bios->c_iflag |= ISTRIP;
	if (lios->c_iflag & LINUX_INLCR)
		bios->c_iflag |= INLCR;
	if (lios->c_iflag & LINUX_IGNCR)
		bios->c_iflag |= IGNCR;
	if (lios->c_iflag & LINUX_ICRNL)
		bios->c_iflag |= ICRNL;
	if (lios->c_iflag & LINUX_IXON)
		bios->c_iflag |= IXON;
	if (lios->c_iflag & LINUX_IXANY)
		bios->c_iflag |= IXANY;
	if (lios->c_iflag & LINUX_IXOFF)
		bios->c_iflag |= IXOFF;
	if (lios->c_iflag & LINUX_IMAXBEL)
		bios->c_iflag |= IMAXBEL;

	bios->c_oflag = 0;
	if (lios->c_oflag & LINUX_OPOST)
		bios->c_oflag |= OPOST;
	if (lios->c_oflag & LINUX_ONLCR)
		bios->c_oflag |= ONLCR;
	if (lios->c_oflag & LINUX_XTABS)
		bios->c_oflag |= TAB3;

	bios->c_cflag = (lios->c_cflag & LINUX_CSIZE) << 4;
	if (lios->c_cflag & LINUX_CSTOPB)
		bios->c_cflag |= CSTOPB;
	if (lios->c_cflag & LINUX_CREAD)
		bios->c_cflag |= CREAD;
	if (lios->c_cflag & LINUX_PARENB)
		bios->c_cflag |= PARENB;
	if (lios->c_cflag & LINUX_PARODD)
		bios->c_cflag |= PARODD;
	if (lios->c_cflag & LINUX_HUPCL)
		bios->c_cflag |= HUPCL;
	if (lios->c_cflag & LINUX_CLOCAL)
		bios->c_cflag |= CLOCAL;
	if (lios->c_cflag & LINUX_CRTSCTS)
		bios->c_cflag |= CRTSCTS;

	bios->c_lflag = 0;
	if (lios->c_lflag & LINUX_ISIG)
		bios->c_lflag |= ISIG;
	if (lios->c_lflag & LINUX_ICANON)
		bios->c_lflag |= ICANON;
	if (lios->c_lflag & LINUX_ECHO)
		bios->c_lflag |= ECHO;
	if (lios->c_lflag & LINUX_ECHOE)
		bios->c_lflag |= ECHOE;
	if (lios->c_lflag & LINUX_ECHOK)
		bios->c_lflag |= ECHOK;
	if (lios->c_lflag & LINUX_ECHONL)
		bios->c_lflag |= ECHONL;
	if (lios->c_lflag & LINUX_NOFLSH)
		bios->c_lflag |= NOFLSH;
	if (lios->c_lflag & LINUX_TOSTOP)
		bios->c_lflag |= TOSTOP;
	if (lios->c_lflag & LINUX_ECHOCTL)
		bios->c_lflag |= ECHOCTL;
	if (lios->c_lflag & LINUX_ECHOPRT)
		bios->c_lflag |= ECHOPRT;
	if (lios->c_lflag & LINUX_ECHOKE)
		bios->c_lflag |= ECHOKE;
	if (lios->c_lflag & LINUX_FLUSHO)
		bios->c_lflag |= FLUSHO;
	if (lios->c_lflag & LINUX_PENDIN)
		bios->c_lflag |= PENDIN;
	if (lios->c_lflag & LINUX_IEXTEN)
		bios->c_lflag |= IEXTEN;

	for (i=0; i<NCCS; i++)
		bios->c_cc[i] = _POSIX_VDISABLE;
	bios->c_cc[VINTR] = lios->c_cc[LINUX_VINTR];
	bios->c_cc[VQUIT] = lios->c_cc[LINUX_VQUIT];
	bios->c_cc[VERASE] = lios->c_cc[LINUX_VERASE];
	bios->c_cc[VKILL] = lios->c_cc[LINUX_VKILL];
	bios->c_cc[VEOF] = lios->c_cc[LINUX_VEOF];
	bios->c_cc[VEOL] = lios->c_cc[LINUX_VEOL];
	bios->c_cc[VMIN] = lios->c_cc[LINUX_VMIN];
	bios->c_cc[VTIME] = lios->c_cc[LINUX_VTIME];
	bios->c_cc[VEOL2] = lios->c_cc[LINUX_VEOL2];
	bios->c_cc[VSUSP] = lios->c_cc[LINUX_VSUSP];
	bios->c_cc[VSTART] = lios->c_cc[LINUX_VSTART];
	bios->c_cc[VSTOP] = lios->c_cc[LINUX_VSTOP];
	bios->c_cc[VREPRINT] = lios->c_cc[LINUX_VREPRINT];
	bios->c_cc[VDISCARD] = lios->c_cc[LINUX_VDISCARD];
	bios->c_cc[VWERASE] = lios->c_cc[LINUX_VWERASE];
	bios->c_cc[VLNEXT] = lios->c_cc[LINUX_VLNEXT];

	for (i=0; i<NCCS; i++) {
		if (i != VMIN && i != VTIME &&
		    bios->c_cc[i] == LINUX_POSIX_VDISABLE)
			bios->c_cc[i] = _POSIX_VDISABLE;
	}

	bios->c_ispeed = bios->c_ospeed =
	    linux_to_bsd_speed(lios->c_cflag & LINUX_CBAUD, sptab);

#ifdef DEBUG
	if (ldebug(ioctl)) {
		printf("LINUX: BSD termios structure (output):\n");
		printf("i=%08x o=%08x c=%08x l=%08x ispeed=%d ospeed=%d\n",
		    bios->c_iflag, bios->c_oflag, bios->c_cflag, bios->c_lflag,
		    bios->c_ispeed, bios->c_ospeed);
		printf("c_cc ");
		for (i=0; i<NCCS; i++)
			printf("%02x ", bios->c_cc[i]);
		printf("\n");
	}
#endif
}

static void
bsd_to_linux_termio(struct termios *bios, struct linux_termio *lio)
{
	struct linux_termios lios;

	memset(lio, 0, sizeof(*lio));
	bsd_to_linux_termios(bios, &lios);
	lio->c_iflag = lios.c_iflag;
	lio->c_oflag = lios.c_oflag;
	lio->c_cflag = lios.c_cflag;
	lio->c_lflag = lios.c_lflag;
	lio->c_line  = lios.c_line;
	memcpy(lio->c_cc, lios.c_cc, LINUX_NCC);
}

static void
linux_to_bsd_termio(struct linux_termio *lio, struct termios *bios)
{
	struct linux_termios lios;
	int i;

	lios.c_iflag = lio->c_iflag;
	lios.c_oflag = lio->c_oflag;
	lios.c_cflag = lio->c_cflag;
	lios.c_lflag = lio->c_lflag;
	for (i=LINUX_NCC; i<LINUX_NCCS; i++)
		lios.c_cc[i] = LINUX_POSIX_VDISABLE;
	memcpy(lios.c_cc, lio->c_cc, LINUX_NCC);
	linux_to_bsd_termios(&lios, bios);
}

static int
linux_ioctl_termio(struct thread *td, struct linux_ioctl_args *args)
{
	struct termios bios;
	struct linux_termios lios;
	struct linux_termio lio;
	struct file *fp;
	int error;

	error = fget(td, args->fd, &cap_ioctl_rights, &fp);
	if (error != 0)
		return (error);

	switch (args->cmd & 0xffff) {

	case LINUX_TCGETS:
		error = fo_ioctl(fp, TIOCGETA, (caddr_t)&bios, td->td_ucred,
		    td);
		if (error)
			break;
		bsd_to_linux_termios(&bios, &lios);
		error = copyout(&lios, (void *)args->arg, sizeof(lios));
		break;

	case LINUX_TCSETS:
		error = copyin((void *)args->arg, &lios, sizeof(lios));
		if (error)
			break;
		linux_to_bsd_termios(&lios, &bios);
		error = (fo_ioctl(fp, TIOCSETA, (caddr_t)&bios, td->td_ucred,
		    td));
		break;

	case LINUX_TCSETSW:
		error = copyin((void *)args->arg, &lios, sizeof(lios));
		if (error)
			break;
		linux_to_bsd_termios(&lios, &bios);
		error = (fo_ioctl(fp, TIOCSETAW, (caddr_t)&bios, td->td_ucred,
		    td));
		break;

	case LINUX_TCSETSF:
		error = copyin((void *)args->arg, &lios, sizeof(lios));
		if (error)
			break;
		linux_to_bsd_termios(&lios, &bios);
		error = (fo_ioctl(fp, TIOCSETAF, (caddr_t)&bios, td->td_ucred,
		    td));
		break;

	case LINUX_TCGETA:
		error = fo_ioctl(fp, TIOCGETA, (caddr_t)&bios, td->td_ucred,
		    td);
		if (error)
			break;
		bsd_to_linux_termio(&bios, &lio);
		error = (copyout(&lio, (void *)args->arg, sizeof(lio)));
		break;

	case LINUX_TCSETA:
		error = copyin((void *)args->arg, &lio, sizeof(lio));
		if (error)
			break;
		linux_to_bsd_termio(&lio, &bios);
		error = (fo_ioctl(fp, TIOCSETA, (caddr_t)&bios, td->td_ucred,
		    td));
		break;

	case LINUX_TCSETAW:
		error = copyin((void *)args->arg, &lio, sizeof(lio));
		if (error)
			break;
		linux_to_bsd_termio(&lio, &bios);
		error = (fo_ioctl(fp, TIOCSETAW, (caddr_t)&bios, td->td_ucred,
		    td));
		break;

	case LINUX_TCSETAF:
		error = copyin((void *)args->arg, &lio, sizeof(lio));
		if (error)
			break;
		linux_to_bsd_termio(&lio, &bios);
		error = (fo_ioctl(fp, TIOCSETAF, (caddr_t)&bios, td->td_ucred,
		    td));
		break;

	/* LINUX_TCSBRK */

	case LINUX_TCXONC: {
		switch (args->arg) {
		case LINUX_TCOOFF:
			args->cmd = TIOCSTOP;
			break;
		case LINUX_TCOON:
			args->cmd = TIOCSTART;
			break;
		case LINUX_TCIOFF:
		case LINUX_TCION: {
			int c;
			struct write_args wr;
			error = fo_ioctl(fp, TIOCGETA, (caddr_t)&bios,
			    td->td_ucred, td);
			if (error)
				break;
			fdrop(fp, td);
			c = (args->arg == LINUX_TCIOFF) ? VSTOP : VSTART;
			c = bios.c_cc[c];
			if (c != _POSIX_VDISABLE) {
				wr.fd = args->fd;
				wr.buf = &c;
				wr.nbyte = sizeof(c);
				return (sys_write(td, &wr));
			} else
				return (0);
		}
		default:
			fdrop(fp, td);
			return (EINVAL);
		}
		args->arg = 0;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;
	}

	case LINUX_TCFLSH: {
		int val;
		switch (args->arg) {
		case LINUX_TCIFLUSH:
			val = FREAD;
			break;
		case LINUX_TCOFLUSH:
			val = FWRITE;
			break;
		case LINUX_TCIOFLUSH:
			val = FREAD | FWRITE;
			break;
		default:
			fdrop(fp, td);
			return (EINVAL);
		}
		error = (fo_ioctl(fp,TIOCFLUSH,(caddr_t)&val,td->td_ucred,td));
		break;
	}

	case LINUX_TIOCEXCL:
		args->cmd = TIOCEXCL;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_TIOCNXCL:
		args->cmd = TIOCNXCL;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_TIOCSCTTY:
		args->cmd = TIOCSCTTY;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_TIOCGPGRP:
		args->cmd = TIOCGPGRP;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_TIOCSPGRP:
		args->cmd = TIOCSPGRP;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	/* LINUX_TIOCOUTQ */
	/* LINUX_TIOCSTI */

	case LINUX_TIOCGWINSZ:
		args->cmd = TIOCGWINSZ;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_TIOCSWINSZ:
		args->cmd = TIOCSWINSZ;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_TIOCMGET:
		args->cmd = TIOCMGET;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_TIOCMBIS:
		args->cmd = TIOCMBIS;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_TIOCMBIC:
		args->cmd = TIOCMBIC;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_TIOCMSET:
		args->cmd = TIOCMSET;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	/* TIOCGSOFTCAR */
	/* TIOCSSOFTCAR */

	case LINUX_FIONREAD: /* LINUX_TIOCINQ */
		args->cmd = FIONREAD;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	/* LINUX_TIOCLINUX */

	case LINUX_TIOCCONS:
		args->cmd = TIOCCONS;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_TIOCGSERIAL: {
		struct linux_serial_struct lss;

		bzero(&lss, sizeof(lss));
		lss.type = LINUX_PORT_16550A;
		lss.flags = 0;
		lss.close_delay = 0;
		error = copyout(&lss, (void *)args->arg, sizeof(lss));
		break;
	}

	case LINUX_TIOCSSERIAL: {
		struct linux_serial_struct lss;
		error = copyin((void *)args->arg, &lss, sizeof(lss));
		if (error)
			break;
		/* XXX - It really helps to have an implementation that
		 * does nothing. NOT!
		 */
		error = 0;
		break;
	}

	case LINUX_TIOCPKT:
		args->cmd = TIOCPKT;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_FIONBIO:
		args->cmd = FIONBIO;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_TIOCNOTTY:
		args->cmd = TIOCNOTTY;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_TIOCSETD: {
		int line;
		switch (args->arg) {
		case LINUX_N_TTY:
			line = TTYDISC;
			break;
		case LINUX_N_SLIP:
			line = SLIPDISC;
			break;
		case LINUX_N_PPP:
			line = PPPDISC;
			break;
		default:
			fdrop(fp, td);
			return (EINVAL);
		}
		error = (fo_ioctl(fp, TIOCSETD, (caddr_t)&line, td->td_ucred,
		    td));
		break;
	}

	case LINUX_TIOCGETD: {
		int linux_line;
		int bsd_line = TTYDISC;
		error = fo_ioctl(fp, TIOCGETD, (caddr_t)&bsd_line,
		    td->td_ucred, td);
		if (error)
			break;
		switch (bsd_line) {
		case TTYDISC:
			linux_line = LINUX_N_TTY;
			break;
		case SLIPDISC:
			linux_line = LINUX_N_SLIP;
			break;
		case PPPDISC:
			linux_line = LINUX_N_PPP;
			break;
		default:
			fdrop(fp, td);
			return (EINVAL);
		}
		error = (copyout(&linux_line, (void *)args->arg, sizeof(int)));
		break;
	}

	/* LINUX_TCSBRKP */
	/* LINUX_TIOCTTYGSTRUCT */

	case LINUX_FIONCLEX:
		args->cmd = FIONCLEX;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_FIOCLEX:
		args->cmd = FIOCLEX;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_FIOASYNC:
		args->cmd = FIOASYNC;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	/* LINUX_TIOCSERCONFIG */
	/* LINUX_TIOCSERGWILD */
	/* LINUX_TIOCSERSWILD */
	/* LINUX_TIOCGLCKTRMIOS */
	/* LINUX_TIOCSLCKTRMIOS */

	case LINUX_TIOCSBRK:
		args->cmd = TIOCSBRK;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_TIOCCBRK:
		args->cmd = TIOCCBRK;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;
	case LINUX_TIOCGPTN: {
		int nb;

		error = fo_ioctl(fp, TIOCGPTN, (caddr_t)&nb, td->td_ucred, td);
		if (!error)
			error = copyout(&nb, (void *)args->arg,
			    sizeof(int));
		break;
	}
	case LINUX_TIOCSPTLCK:
		/* Our unlockpt() does nothing. */
		error = 0;
		break;
	default:
		error = ENOIOCTL;
		break;
	}

	fdrop(fp, td);
	return (error);
}

/*
 * CDROM related ioctls
 */

struct linux_cdrom_msf
{
	u_char	cdmsf_min0;
	u_char	cdmsf_sec0;
	u_char	cdmsf_frame0;
	u_char	cdmsf_min1;
	u_char	cdmsf_sec1;
	u_char	cdmsf_frame1;
};

struct linux_cdrom_tochdr
{
	u_char	cdth_trk0;
	u_char	cdth_trk1;
};

union linux_cdrom_addr
{
	struct {
		u_char	minute;
		u_char	second;
		u_char	frame;
	} msf;
	int	lba;
};

struct linux_cdrom_tocentry
{
	u_char	cdte_track;
	u_char	cdte_adr:4;
	u_char	cdte_ctrl:4;
	u_char	cdte_format;
	union linux_cdrom_addr cdte_addr;
	u_char	cdte_datamode;
};

struct linux_cdrom_subchnl
{
	u_char	cdsc_format;
	u_char	cdsc_audiostatus;
	u_char	cdsc_adr:4;
	u_char	cdsc_ctrl:4;
	u_char	cdsc_trk;
	u_char	cdsc_ind;
	union linux_cdrom_addr cdsc_absaddr;
	union linux_cdrom_addr cdsc_reladdr;
};

struct l_cdrom_read_audio {
	union linux_cdrom_addr addr;
	u_char		addr_format;
	l_int		nframes;
	u_char		*buf;
};

struct l_dvd_layer {
	u_char		book_version:4;
	u_char		book_type:4;
	u_char		min_rate:4;
	u_char		disc_size:4;
	u_char		layer_type:4;
	u_char		track_path:1;
	u_char		nlayers:2;
	u_char		track_density:4;
	u_char		linear_density:4;
	u_char		bca:1;
	u_int32_t	start_sector;
	u_int32_t	end_sector;
	u_int32_t	end_sector_l0;
};

struct l_dvd_physical {
	u_char		type;
	u_char		layer_num;
	struct l_dvd_layer layer[4];
};

struct l_dvd_copyright {
	u_char		type;
	u_char		layer_num;
	u_char		cpst;
	u_char		rmi;
};

struct l_dvd_disckey {
	u_char		type;
	l_uint		agid:2;
	u_char		value[2048];
};

struct l_dvd_bca {
	u_char		type;
	l_int		len;
	u_char		value[188];
};

struct l_dvd_manufact {
	u_char		type;
	u_char		layer_num;
	l_int		len;
	u_char		value[2048];
};

typedef union {
	u_char			type;
	struct l_dvd_physical	physical;
	struct l_dvd_copyright	copyright;
	struct l_dvd_disckey	disckey;
	struct l_dvd_bca	bca;
	struct l_dvd_manufact	manufact;
} l_dvd_struct;

typedef u_char l_dvd_key[5];
typedef u_char l_dvd_challenge[10];

struct l_dvd_lu_send_agid {
	u_char		type;
	l_uint		agid:2;
};

struct l_dvd_host_send_challenge {
	u_char		type;
	l_uint		agid:2;
	l_dvd_challenge	chal;
};

struct l_dvd_send_key {
	u_char		type;
	l_uint		agid:2;
	l_dvd_key	key;
};

struct l_dvd_lu_send_challenge {
	u_char		type;
	l_uint		agid:2;
	l_dvd_challenge	chal;
};

struct l_dvd_lu_send_title_key {
	u_char		type;
	l_uint		agid:2;
	l_dvd_key	title_key;
	l_int		lba;
	l_uint		cpm:1;
	l_uint		cp_sec:1;
	l_uint		cgms:2;
};

struct l_dvd_lu_send_asf {
	u_char		type;
	l_uint		agid:2;
	l_uint		asf:1;
};

struct l_dvd_host_send_rpcstate {
	u_char		type;
	u_char		pdrc;
};

struct l_dvd_lu_send_rpcstate {
	u_char		type:2;
	u_char		vra:3;
	u_char		ucca:3;
	u_char		region_mask;
	u_char		rpc_scheme;
};

typedef union {
	u_char				type;
	struct l_dvd_lu_send_agid	lsa;
	struct l_dvd_host_send_challenge hsc;
	struct l_dvd_send_key		lsk;
	struct l_dvd_lu_send_challenge	lsc;
	struct l_dvd_send_key		hsk;
	struct l_dvd_lu_send_title_key	lstk;
	struct l_dvd_lu_send_asf	lsasf;
	struct l_dvd_host_send_rpcstate	hrpcs;
	struct l_dvd_lu_send_rpcstate	lrpcs;
} l_dvd_authinfo;

static void
bsd_to_linux_msf_lba(u_char af, union msf_lba *bp, union linux_cdrom_addr *lp)
{
	if (af == CD_LBA_FORMAT)
		lp->lba = bp->lba;
	else {
		lp->msf.minute = bp->msf.minute;
		lp->msf.second = bp->msf.second;
		lp->msf.frame = bp->msf.frame;
	}
}

static void
set_linux_cdrom_addr(union linux_cdrom_addr *addr, int format, int lba)
{
	if (format == LINUX_CDROM_MSF) {
		addr->msf.frame = lba % 75;
		lba /= 75;
		lba += 2;
		addr->msf.second = lba % 60;
		addr->msf.minute = lba / 60;
	} else
		addr->lba = lba;
}

static int
linux_to_bsd_dvd_struct(l_dvd_struct *lp, struct dvd_struct *bp)
{
	bp->format = lp->type;
	switch (bp->format) {
	case DVD_STRUCT_PHYSICAL:
		if (bp->layer_num >= 4)
			return (EINVAL);
		bp->layer_num = lp->physical.layer_num;
		break;
	case DVD_STRUCT_COPYRIGHT:
		bp->layer_num = lp->copyright.layer_num;
		break;
	case DVD_STRUCT_DISCKEY:
		bp->agid = lp->disckey.agid;
		break;
	case DVD_STRUCT_BCA:
	case DVD_STRUCT_MANUFACT:
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

static int
bsd_to_linux_dvd_struct(struct dvd_struct *bp, l_dvd_struct *lp)
{
	switch (bp->format) {
	case DVD_STRUCT_PHYSICAL: {
		struct dvd_layer *blp = (struct dvd_layer *)bp->data;
		struct l_dvd_layer *llp = &lp->physical.layer[bp->layer_num];
		memset(llp, 0, sizeof(*llp));
		llp->book_version = blp->book_version;
		llp->book_type = blp->book_type;
		llp->min_rate = blp->max_rate;
		llp->disc_size = blp->disc_size;
		llp->layer_type = blp->layer_type;
		llp->track_path = blp->track_path;
		llp->nlayers = blp->nlayers;
		llp->track_density = blp->track_density;
		llp->linear_density = blp->linear_density;
		llp->bca = blp->bca;
		llp->start_sector = blp->start_sector;
		llp->end_sector = blp->end_sector;
		llp->end_sector_l0 = blp->end_sector_l0;
		break;
	}
	case DVD_STRUCT_COPYRIGHT:
		lp->copyright.cpst = bp->cpst;
		lp->copyright.rmi = bp->rmi;
		break;
	case DVD_STRUCT_DISCKEY:
		memcpy(lp->disckey.value, bp->data, sizeof(lp->disckey.value));
		break;
	case DVD_STRUCT_BCA:
		lp->bca.len = bp->length;
		memcpy(lp->bca.value, bp->data, sizeof(lp->bca.value));
		break;
	case DVD_STRUCT_MANUFACT:
		lp->manufact.len = bp->length;
		memcpy(lp->manufact.value, bp->data,
		    sizeof(lp->manufact.value));
		/* lp->manufact.layer_num is unused in Linux (redhat 7.0). */
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

static int
linux_to_bsd_dvd_authinfo(l_dvd_authinfo *lp, int *bcode,
    struct dvd_authinfo *bp)
{
	switch (lp->type) {
	case LINUX_DVD_LU_SEND_AGID:
		*bcode = DVDIOCREPORTKEY;
		bp->format = DVD_REPORT_AGID;
		bp->agid = lp->lsa.agid;
		break;
	case LINUX_DVD_HOST_SEND_CHALLENGE:
		*bcode = DVDIOCSENDKEY;
		bp->format = DVD_SEND_CHALLENGE;
		bp->agid = lp->hsc.agid;
		memcpy(bp->keychal, lp->hsc.chal, 10);
		break;
	case LINUX_DVD_LU_SEND_KEY1:
		*bcode = DVDIOCREPORTKEY;
		bp->format = DVD_REPORT_KEY1;
		bp->agid = lp->lsk.agid;
		break;
	case LINUX_DVD_LU_SEND_CHALLENGE:
		*bcode = DVDIOCREPORTKEY;
		bp->format = DVD_REPORT_CHALLENGE;
		bp->agid = lp->lsc.agid;
		break;
	case LINUX_DVD_HOST_SEND_KEY2:
		*bcode = DVDIOCSENDKEY;
		bp->format = DVD_SEND_KEY2;
		bp->agid = lp->hsk.agid;
		memcpy(bp->keychal, lp->hsk.key, 5);
		break;
	case LINUX_DVD_LU_SEND_TITLE_KEY:
		*bcode = DVDIOCREPORTKEY;
		bp->format = DVD_REPORT_TITLE_KEY;
		bp->agid = lp->lstk.agid;
		bp->lba = lp->lstk.lba;
		break;
	case LINUX_DVD_LU_SEND_ASF:
		*bcode = DVDIOCREPORTKEY;
		bp->format = DVD_REPORT_ASF;
		bp->agid = lp->lsasf.agid;
		break;
	case LINUX_DVD_INVALIDATE_AGID:
		*bcode = DVDIOCREPORTKEY;
		bp->format = DVD_INVALIDATE_AGID;
		bp->agid = lp->lsa.agid;
		break;
	case LINUX_DVD_LU_SEND_RPC_STATE:
		*bcode = DVDIOCREPORTKEY;
		bp->format = DVD_REPORT_RPC;
		break;
	case LINUX_DVD_HOST_SEND_RPC_STATE:
		*bcode = DVDIOCSENDKEY;
		bp->format = DVD_SEND_RPC;
		bp->region = lp->hrpcs.pdrc;
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

static int
bsd_to_linux_dvd_authinfo(struct dvd_authinfo *bp, l_dvd_authinfo *lp)
{
	switch (lp->type) {
	case LINUX_DVD_LU_SEND_AGID:
		lp->lsa.agid = bp->agid;
		break;
	case LINUX_DVD_HOST_SEND_CHALLENGE:
		lp->type = LINUX_DVD_LU_SEND_KEY1;
		break;
	case LINUX_DVD_LU_SEND_KEY1:
		memcpy(lp->lsk.key, bp->keychal, sizeof(lp->lsk.key));
		break;
	case LINUX_DVD_LU_SEND_CHALLENGE:
		memcpy(lp->lsc.chal, bp->keychal, sizeof(lp->lsc.chal));
		break;
	case LINUX_DVD_HOST_SEND_KEY2:
		lp->type = LINUX_DVD_AUTH_ESTABLISHED;
		break;
	case LINUX_DVD_LU_SEND_TITLE_KEY:
		memcpy(lp->lstk.title_key, bp->keychal,
		    sizeof(lp->lstk.title_key));
		lp->lstk.cpm = bp->cpm;
		lp->lstk.cp_sec = bp->cp_sec;
		lp->lstk.cgms = bp->cgms;
		break;
	case LINUX_DVD_LU_SEND_ASF:
		lp->lsasf.asf = bp->asf;
		break;
	case LINUX_DVD_INVALIDATE_AGID:
		break;
	case LINUX_DVD_LU_SEND_RPC_STATE:
		lp->lrpcs.type = bp->reg_type;
		lp->lrpcs.vra = bp->vend_rsts;
		lp->lrpcs.ucca = bp->user_rsts;
		lp->lrpcs.region_mask = bp->region;
		lp->lrpcs.rpc_scheme = bp->rpc_scheme;
		break;
	case LINUX_DVD_HOST_SEND_RPC_STATE:
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

static int
linux_ioctl_cdrom(struct thread *td, struct linux_ioctl_args *args)
{
	struct file *fp;
	int error;

	error = fget(td, args->fd, &cap_ioctl_rights, &fp);
	if (error != 0)
		return (error);
	switch (args->cmd & 0xffff) {

	case LINUX_CDROMPAUSE:
		args->cmd = CDIOCPAUSE;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_CDROMRESUME:
		args->cmd = CDIOCRESUME;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_CDROMPLAYMSF:
		args->cmd = CDIOCPLAYMSF;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_CDROMPLAYTRKIND:
		args->cmd = CDIOCPLAYTRACKS;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_CDROMREADTOCHDR: {
		struct ioc_toc_header th;
		struct linux_cdrom_tochdr lth;
		error = fo_ioctl(fp, CDIOREADTOCHEADER, (caddr_t)&th,
		    td->td_ucred, td);
		if (!error) {
			lth.cdth_trk0 = th.starting_track;
			lth.cdth_trk1 = th.ending_track;
			copyout(&lth, (void *)args->arg, sizeof(lth));
		}
		break;
	}

	case LINUX_CDROMREADTOCENTRY: {
		struct linux_cdrom_tocentry lte;
		struct ioc_read_toc_single_entry irtse;

		error = copyin((void *)args->arg, &lte, sizeof(lte));
		if (error)
			break;
		irtse.address_format = lte.cdte_format;
		irtse.track = lte.cdte_track;
		error = fo_ioctl(fp, CDIOREADTOCENTRY, (caddr_t)&irtse,
		    td->td_ucred, td);
		if (!error) {
			lte.cdte_ctrl = irtse.entry.control;
			lte.cdte_adr = irtse.entry.addr_type;
			bsd_to_linux_msf_lba(irtse.address_format,
			    &irtse.entry.addr, &lte.cdte_addr);
			error = copyout(&lte, (void *)args->arg, sizeof(lte));
		}
		break;
	}

	case LINUX_CDROMSTOP:
		args->cmd = CDIOCSTOP;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_CDROMSTART:
		args->cmd = CDIOCSTART;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_CDROMEJECT:
		args->cmd = CDIOCEJECT;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	/* LINUX_CDROMVOLCTRL */

	case LINUX_CDROMSUBCHNL: {
		struct linux_cdrom_subchnl sc;
		struct ioc_read_subchannel bsdsc;
		struct cd_sub_channel_info bsdinfo;

		bsdsc.address_format = CD_LBA_FORMAT;
		bsdsc.data_format = CD_CURRENT_POSITION;
		bsdsc.track = 0;
		bsdsc.data_len = sizeof(bsdinfo);
		bsdsc.data = &bsdinfo;
		error = fo_ioctl(fp, CDIOCREADSUBCHANNEL_SYSSPACE,
		    (caddr_t)&bsdsc, td->td_ucred, td);
		if (error)
			break;
		error = copyin((void *)args->arg, &sc, sizeof(sc));
		if (error)
			break;
		sc.cdsc_audiostatus = bsdinfo.header.audio_status;
		sc.cdsc_adr = bsdinfo.what.position.addr_type;
		sc.cdsc_ctrl = bsdinfo.what.position.control;
		sc.cdsc_trk = bsdinfo.what.position.track_number;
		sc.cdsc_ind = bsdinfo.what.position.index_number;
		set_linux_cdrom_addr(&sc.cdsc_absaddr, sc.cdsc_format,
		    bsdinfo.what.position.absaddr.lba);
		set_linux_cdrom_addr(&sc.cdsc_reladdr, sc.cdsc_format,
		    bsdinfo.what.position.reladdr.lba);
		error = copyout(&sc, (void *)args->arg, sizeof(sc));
		break;
	}

	/* LINUX_CDROMREADMODE2 */
	/* LINUX_CDROMREADMODE1 */
	/* LINUX_CDROMREADAUDIO */
	/* LINUX_CDROMEJECT_SW */
	/* LINUX_CDROMMULTISESSION */
	/* LINUX_CDROM_GET_UPC */

	case LINUX_CDROMRESET:
		args->cmd = CDIOCRESET;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	/* LINUX_CDROMVOLREAD */
	/* LINUX_CDROMREADRAW */
	/* LINUX_CDROMREADCOOKED */
	/* LINUX_CDROMSEEK */
	/* LINUX_CDROMPLAYBLK */
	/* LINUX_CDROMREADALL */
	/* LINUX_CDROMCLOSETRAY */
	/* LINUX_CDROMLOADFROMSLOT */
	/* LINUX_CDROMGETSPINDOWN */
	/* LINUX_CDROMSETSPINDOWN */
	/* LINUX_CDROM_SET_OPTIONS */
	/* LINUX_CDROM_CLEAR_OPTIONS */
	/* LINUX_CDROM_SELECT_SPEED */
	/* LINUX_CDROM_SELECT_DISC */
	/* LINUX_CDROM_MEDIA_CHANGED */
	/* LINUX_CDROM_DRIVE_STATUS */
	/* LINUX_CDROM_DISC_STATUS */
	/* LINUX_CDROM_CHANGER_NSLOTS */
	/* LINUX_CDROM_LOCKDOOR */
	/* LINUX_CDROM_DEBUG */
	/* LINUX_CDROM_GET_CAPABILITY */
	/* LINUX_CDROMAUDIOBUFSIZ */

	case LINUX_DVD_READ_STRUCT: {
		l_dvd_struct *lds;
		struct dvd_struct *bds;

		lds = malloc(sizeof(*lds), M_LINUX, M_WAITOK);
		bds = malloc(sizeof(*bds), M_LINUX, M_WAITOK);
		error = copyin((void *)args->arg, lds, sizeof(*lds));
		if (error)
			goto out;
		error = linux_to_bsd_dvd_struct(lds, bds);
		if (error)
			goto out;
		error = fo_ioctl(fp, DVDIOCREADSTRUCTURE, (caddr_t)bds,
		    td->td_ucred, td);
		if (error)
			goto out;
		error = bsd_to_linux_dvd_struct(bds, lds);
		if (error)
			goto out;
		error = copyout(lds, (void *)args->arg, sizeof(*lds));
	out:
		free(bds, M_LINUX);
		free(lds, M_LINUX);
		break;
	}

	/* LINUX_DVD_WRITE_STRUCT */

	case LINUX_DVD_AUTH: {
		l_dvd_authinfo lda;
		struct dvd_authinfo bda;
		int bcode;

		error = copyin((void *)args->arg, &lda, sizeof(lda));
		if (error)
			break;
		error = linux_to_bsd_dvd_authinfo(&lda, &bcode, &bda);
		if (error)
			break;
		error = fo_ioctl(fp, bcode, (caddr_t)&bda, td->td_ucred,
		    td);
		if (error) {
			if (lda.type == LINUX_DVD_HOST_SEND_KEY2) {
				lda.type = LINUX_DVD_AUTH_FAILURE;
				copyout(&lda, (void *)args->arg, sizeof(lda));
			}
			break;
		}
		error = bsd_to_linux_dvd_authinfo(&bda, &lda);
		if (error)
			break;
		error = copyout(&lda, (void *)args->arg, sizeof(lda));
		break;
	}

	case LINUX_SCSI_GET_BUS_NUMBER:
	{
		struct sg_scsi_id id;

		error = fo_ioctl(fp, SG_GET_SCSI_ID, (caddr_t)&id,
		    td->td_ucred, td);
		if (error)
			break;
		error = copyout(&id.channel, (void *)args->arg, sizeof(int));
		break;
	}

	case LINUX_SCSI_GET_IDLUN:
	{
		struct sg_scsi_id id;
		struct scsi_idlun idl;

		error = fo_ioctl(fp, SG_GET_SCSI_ID, (caddr_t)&id,
		    td->td_ucred, td);
		if (error)
			break;
		idl.dev_id = (id.scsi_id & 0xff) + ((id.lun & 0xff) << 8) +
		    ((id.channel & 0xff) << 16) + ((id.host_no & 0xff) << 24);
		idl.host_unique_id = id.host_no;
		error = copyout(&idl, (void *)args->arg, sizeof(idl));
		break;
	}

	/* LINUX_CDROM_SEND_PACKET */
	/* LINUX_CDROM_NEXT_WRITABLE */
	/* LINUX_CDROM_LAST_WRITTEN */

	default:
		error = ENOIOCTL;
		break;
	}

	fdrop(fp, td);
	return (error);
}

static int
linux_ioctl_vfat(struct thread *td, struct linux_ioctl_args *args)
{

	return (ENOTTY);
}

/*
 * Sound related ioctls
 */

struct linux_old_mixer_info {
	char	id[16];
	char	name[32];
};

static u_int32_t dirbits[4] = { IOC_VOID, IOC_IN, IOC_OUT, IOC_INOUT };

#define	SETDIR(c)	(((c) & ~IOC_DIRMASK) | dirbits[args->cmd >> 30])

static int
linux_ioctl_sound(struct thread *td, struct linux_ioctl_args *args)
{

	switch (args->cmd & 0xffff) {

	case LINUX_SOUND_MIXER_WRITE_VOLUME:
		args->cmd = SETDIR(SOUND_MIXER_WRITE_VOLUME);
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_WRITE_BASS:
		args->cmd = SETDIR(SOUND_MIXER_WRITE_BASS);
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_WRITE_TREBLE:
		args->cmd = SETDIR(SOUND_MIXER_WRITE_TREBLE);
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_WRITE_SYNTH:
		args->cmd = SETDIR(SOUND_MIXER_WRITE_SYNTH);
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_WRITE_PCM:
		args->cmd = SETDIR(SOUND_MIXER_WRITE_PCM);
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_WRITE_SPEAKER:
		args->cmd = SETDIR(SOUND_MIXER_WRITE_SPEAKER);
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_WRITE_LINE:
		args->cmd = SETDIR(SOUND_MIXER_WRITE_LINE);
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_WRITE_MIC:
		args->cmd = SETDIR(SOUND_MIXER_WRITE_MIC);
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_WRITE_CD:
		args->cmd = SETDIR(SOUND_MIXER_WRITE_CD);
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_WRITE_IMIX:
		args->cmd = SETDIR(SOUND_MIXER_WRITE_IMIX);
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_WRITE_ALTPCM:
		args->cmd = SETDIR(SOUND_MIXER_WRITE_ALTPCM);
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_WRITE_RECLEV:
		args->cmd = SETDIR(SOUND_MIXER_WRITE_RECLEV);
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_WRITE_IGAIN:
		args->cmd = SETDIR(SOUND_MIXER_WRITE_IGAIN);
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_WRITE_OGAIN:
		args->cmd = SETDIR(SOUND_MIXER_WRITE_OGAIN);
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_WRITE_LINE1:
		args->cmd = SETDIR(SOUND_MIXER_WRITE_LINE1);
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_WRITE_LINE2:
		args->cmd = SETDIR(SOUND_MIXER_WRITE_LINE2);
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_WRITE_LINE3:
		args->cmd = SETDIR(SOUND_MIXER_WRITE_LINE3);
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_INFO: {
		/* Key on encoded length */
		switch ((args->cmd >> 16) & 0x1fff) {
		case 0x005c: {	/* SOUND_MIXER_INFO */
			args->cmd = SOUND_MIXER_INFO;
			return (sys_ioctl(td, (struct ioctl_args *)args));
		}
		case 0x0030: {	/* SOUND_OLD_MIXER_INFO */
			struct linux_old_mixer_info info;
			bzero(&info, sizeof(info));
			strncpy(info.id, "OSS", sizeof(info.id) - 1);
			strncpy(info.name, "FreeBSD OSS Mixer", sizeof(info.name) - 1);
			copyout(&info, (void *)args->arg, sizeof(info));
			return (0);
		}
		default:
			return (ENOIOCTL);
		}
		break;
	}

	case LINUX_OSS_GETVERSION: {
		int version = linux_get_oss_version(td);
		return (copyout(&version, (void *)args->arg, sizeof(int)));
	}

	case LINUX_SOUND_MIXER_READ_STEREODEVS:
		args->cmd = SOUND_MIXER_READ_STEREODEVS;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_READ_CAPS:
		args->cmd = SOUND_MIXER_READ_CAPS;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_READ_RECMASK:
		args->cmd = SOUND_MIXER_READ_RECMASK;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_READ_DEVMASK:
		args->cmd = SOUND_MIXER_READ_DEVMASK;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_MIXER_WRITE_RECSRC:
		args->cmd = SETDIR(SOUND_MIXER_WRITE_RECSRC);
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_DSP_RESET:
		args->cmd = SNDCTL_DSP_RESET;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_DSP_SYNC:
		args->cmd = SNDCTL_DSP_SYNC;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_DSP_SPEED:
		args->cmd = SNDCTL_DSP_SPEED;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_DSP_STEREO:
		args->cmd = SNDCTL_DSP_STEREO;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_DSP_GETBLKSIZE: /* LINUX_SNDCTL_DSP_SETBLKSIZE */
		args->cmd = SNDCTL_DSP_GETBLKSIZE;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_DSP_SETFMT:
		args->cmd = SNDCTL_DSP_SETFMT;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_PCM_WRITE_CHANNELS:
		args->cmd = SOUND_PCM_WRITE_CHANNELS;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SOUND_PCM_WRITE_FILTER:
		args->cmd = SOUND_PCM_WRITE_FILTER;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_DSP_POST:
		args->cmd = SNDCTL_DSP_POST;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_DSP_SUBDIVIDE:
		args->cmd = SNDCTL_DSP_SUBDIVIDE;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_DSP_SETFRAGMENT:
		args->cmd = SNDCTL_DSP_SETFRAGMENT;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_DSP_GETFMTS:
		args->cmd = SNDCTL_DSP_GETFMTS;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_DSP_GETOSPACE:
		args->cmd = SNDCTL_DSP_GETOSPACE;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_DSP_GETISPACE:
		args->cmd = SNDCTL_DSP_GETISPACE;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_DSP_NONBLOCK:
		args->cmd = SNDCTL_DSP_NONBLOCK;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_DSP_GETCAPS:
		args->cmd = SNDCTL_DSP_GETCAPS;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_DSP_SETTRIGGER: /* LINUX_SNDCTL_GETTRIGGER */
		args->cmd = SNDCTL_DSP_SETTRIGGER;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_DSP_GETIPTR:
		args->cmd = SNDCTL_DSP_GETIPTR;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_DSP_GETOPTR:
		args->cmd = SNDCTL_DSP_GETOPTR;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_DSP_SETDUPLEX:
		args->cmd = SNDCTL_DSP_SETDUPLEX;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_DSP_GETODELAY:
		args->cmd = SNDCTL_DSP_GETODELAY;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_SEQ_RESET:
		args->cmd = SNDCTL_SEQ_RESET;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_SEQ_SYNC:
		args->cmd = SNDCTL_SEQ_SYNC;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_SYNTH_INFO:
		args->cmd = SNDCTL_SYNTH_INFO;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_SEQ_CTRLRATE:
		args->cmd = SNDCTL_SEQ_CTRLRATE;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_SEQ_GETOUTCOUNT:
		args->cmd = SNDCTL_SEQ_GETOUTCOUNT;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_SEQ_GETINCOUNT:
		args->cmd = SNDCTL_SEQ_GETINCOUNT;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_SEQ_PERCMODE:
		args->cmd = SNDCTL_SEQ_PERCMODE;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_FM_LOAD_INSTR:
		args->cmd = SNDCTL_FM_LOAD_INSTR;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_SEQ_TESTMIDI:
		args->cmd = SNDCTL_SEQ_TESTMIDI;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_SEQ_RESETSAMPLES:
		args->cmd = SNDCTL_SEQ_RESETSAMPLES;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_SEQ_NRSYNTHS:
		args->cmd = SNDCTL_SEQ_NRSYNTHS;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_SEQ_NRMIDIS:
		args->cmd = SNDCTL_SEQ_NRMIDIS;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_MIDI_INFO:
		args->cmd = SNDCTL_MIDI_INFO;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_SEQ_TRESHOLD:
		args->cmd = SNDCTL_SEQ_TRESHOLD;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	case LINUX_SNDCTL_SYNTH_MEMAVL:
		args->cmd = SNDCTL_SYNTH_MEMAVL;
		return (sys_ioctl(td, (struct ioctl_args *)args));

	}

	return (ENOIOCTL);
}

/*
 * Console related ioctls
 */

static int
linux_ioctl_console(struct thread *td, struct linux_ioctl_args *args)
{
	struct file *fp;
	int error;

	error = fget(td, args->fd, &cap_ioctl_rights, &fp);
	if (error != 0)
		return (error);
	switch (args->cmd & 0xffff) {

	case LINUX_KIOCSOUND:
		args->cmd = KIOCSOUND;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_KDMKTONE:
		args->cmd = KDMKTONE;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_KDGETLED:
		args->cmd = KDGETLED;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_KDSETLED:
		args->cmd = KDSETLED;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_KDSETMODE:
		args->cmd = KDSETMODE;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_KDGETMODE:
		args->cmd = KDGETMODE;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_KDGKBMODE:
		args->cmd = KDGKBMODE;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_KDSKBMODE: {
		int kbdmode;
		switch (args->arg) {
		case LINUX_KBD_RAW:
			kbdmode = K_RAW;
			break;
		case LINUX_KBD_XLATE:
			kbdmode = K_XLATE;
			break;
		case LINUX_KBD_MEDIUMRAW:
			kbdmode = K_RAW;
			break;
		default:
			fdrop(fp, td);
			return (EINVAL);
		}
		error = (fo_ioctl(fp, KDSKBMODE, (caddr_t)&kbdmode,
		    td->td_ucred, td));
		break;
	}

	case LINUX_VT_OPENQRY:
		args->cmd = VT_OPENQRY;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_VT_GETMODE:
		args->cmd = VT_GETMODE;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_VT_SETMODE: {
		struct vt_mode mode;
		if ((error = copyin((void *)args->arg, &mode, sizeof(mode))))
			break;
		if (LINUX_SIG_VALID(mode.relsig))
			mode.relsig = linux_to_bsd_signal(mode.relsig);
		else
			mode.relsig = 0;
		if (LINUX_SIG_VALID(mode.acqsig))
			mode.acqsig = linux_to_bsd_signal(mode.acqsig);
		else
			mode.acqsig = 0;
		/* XXX. Linux ignores frsig and set it to 0. */
		mode.frsig = 0;
		if ((error = copyout(&mode, (void *)args->arg, sizeof(mode))))
			break;
		args->cmd = VT_SETMODE;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;
	}

	case LINUX_VT_GETSTATE:
		args->cmd = VT_GETACTIVE;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_VT_RELDISP:
		args->cmd = VT_RELDISP;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_VT_ACTIVATE:
		args->cmd = VT_ACTIVATE;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	case LINUX_VT_WAITACTIVE:
		args->cmd = VT_WAITACTIVE;
		error = (sys_ioctl(td, (struct ioctl_args *)args));
		break;

	default:
		error = ENOIOCTL;
		break;
	}

	fdrop(fp, td);
	return (error);
}

/*
 * Criteria for interface name translation
 */
#define IFP_IS_ETH(ifp) (ifp->if_type == IFT_ETHER)

/*
 * Translate a Linux interface name to a FreeBSD interface name,
 * and return the associated ifnet structure
 * bsdname and lxname need to be least IFNAMSIZ bytes long, but
 * can point to the same buffer.
 */

static struct ifnet *
ifname_linux_to_bsd(struct thread *td, const char *lxname, char *bsdname)
{
	struct ifnet *ifp;
	int len, unit;
	char *ep;
	int is_eth, index;

	for (len = 0; len < LINUX_IFNAMSIZ; ++len)
		if (!isalpha(lxname[len]))
			break;
	if (len == 0 || len == LINUX_IFNAMSIZ)
		return (NULL);
	unit = (int)strtoul(lxname + len, &ep, 10);
	if (ep == NULL || ep == lxname + len || ep >= lxname + LINUX_IFNAMSIZ)
		return (NULL);
	index = 0;
	is_eth = (len == 3 && !strncmp(lxname, "eth", len)) ? 1 : 0;
	CURVNET_SET(TD_TO_VNET(td));
	IFNET_RLOCK();
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		/*
		 * Allow Linux programs to use FreeBSD names. Don't presume
		 * we never have an interface named "eth", so don't make
		 * the test optional based on is_eth.
		 */
		if (strncmp(ifp->if_xname, lxname, LINUX_IFNAMSIZ) == 0)
			break;
		if (is_eth && IFP_IS_ETH(ifp) && unit == index++)
			break;
	}
	IFNET_RUNLOCK();
	CURVNET_RESTORE();
	if (ifp != NULL)
		strlcpy(bsdname, ifp->if_xname, IFNAMSIZ);
	return (ifp);
}

/*
 * Implement the SIOCGIFNAME ioctl
 */

static int
linux_ioctl_ifname(struct thread *td, struct l_ifreq *uifr)
{
	struct l_ifreq ifr;
	struct ifnet *ifp;
	int error, ethno, index;

	error = copyin(uifr, &ifr, sizeof(ifr));
	if (error != 0)
		return (error);

	CURVNET_SET(TD_TO_VNET(curthread));
	IFNET_RLOCK();
	index = 1;	/* ifr.ifr_ifindex starts from 1 */
	ethno = 0;
	error = ENODEV;
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		if (ifr.ifr_ifindex == index) {
			if (IFP_IS_ETH(ifp))
				snprintf(ifr.ifr_name, LINUX_IFNAMSIZ,
				    "eth%d", ethno);
			else
				strlcpy(ifr.ifr_name, ifp->if_xname,
				    LINUX_IFNAMSIZ);
			error = 0;
			break;
		}
		if (IFP_IS_ETH(ifp))
			ethno++;
		index++;
	}
	IFNET_RUNLOCK();
	if (error == 0)
		error = copyout(&ifr, uifr, sizeof(ifr));
	CURVNET_RESTORE();

	return (error);
}

/*
 * Implement the SIOCGIFCONF ioctl
 */

static int
linux_ifconf(struct thread *td, struct ifconf *uifc)
{
#ifdef COMPAT_LINUX32
	struct l_ifconf ifc;
#else
	struct ifconf ifc;
#endif
	struct l_ifreq ifr;
	struct ifnet *ifp;
	struct ifaddr *ifa;
	struct sbuf *sb;
	int error, ethno, full = 0, valid_len, max_len;

	error = copyin(uifc, &ifc, sizeof(ifc));
	if (error != 0)
		return (error);

	max_len = MAXPHYS - 1;

	CURVNET_SET(TD_TO_VNET(td));
	/* handle the 'request buffer size' case */
	if ((l_uintptr_t)ifc.ifc_buf == PTROUT(NULL)) {
		ifc.ifc_len = 0;
		IFNET_RLOCK();
		CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
			CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
				struct sockaddr *sa = ifa->ifa_addr;
				if (sa->sa_family == AF_INET)
					ifc.ifc_len += sizeof(ifr);
			}
		}
		IFNET_RUNLOCK();
		error = copyout(&ifc, uifc, sizeof(ifc));
		CURVNET_RESTORE();
		return (error);
	}

	if (ifc.ifc_len <= 0) {
		CURVNET_RESTORE();
		return (EINVAL);
	}

again:
	/* Keep track of eth interfaces */
	ethno = 0;
	if (ifc.ifc_len <= max_len) {
		max_len = ifc.ifc_len;
		full = 1;
	}
	sb = sbuf_new(NULL, NULL, max_len + 1, SBUF_FIXEDLEN);
	max_len = 0;
	valid_len = 0;

	/* Return all AF_INET addresses of all interfaces */
	IFNET_RLOCK();
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link) {
		int addrs = 0;

		bzero(&ifr, sizeof(ifr));
		if (IFP_IS_ETH(ifp))
			snprintf(ifr.ifr_name, LINUX_IFNAMSIZ, "eth%d",
			    ethno++);
		else
			strlcpy(ifr.ifr_name, ifp->if_xname, LINUX_IFNAMSIZ);

		/* Walk the address list */
		CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
			struct sockaddr *sa = ifa->ifa_addr;

			if (sa->sa_family == AF_INET) {
				ifr.ifr_addr.sa_family = LINUX_AF_INET;
				memcpy(ifr.ifr_addr.sa_data, sa->sa_data,
				    sizeof(ifr.ifr_addr.sa_data));
				sbuf_bcat(sb, &ifr, sizeof(ifr));
				max_len += sizeof(ifr);
				addrs++;
			}

			if (sbuf_error(sb) == 0)
				valid_len = sbuf_len(sb);
		}
		if (addrs == 0) {
			bzero((caddr_t)&ifr.ifr_addr, sizeof(ifr.ifr_addr));
			sbuf_bcat(sb, &ifr, sizeof(ifr));
			max_len += sizeof(ifr);

			if (sbuf_error(sb) == 0)
				valid_len = sbuf_len(sb);
		}
	}
	IFNET_RUNLOCK();

	if (valid_len != max_len && !full) {
		sbuf_delete(sb);
		goto again;
	}

	ifc.ifc_len = valid_len;
	sbuf_finish(sb);
	error = copyout(sbuf_data(sb), PTRIN(ifc.ifc_buf), ifc.ifc_len);
	if (error == 0)
		error = copyout(&ifc, uifc, sizeof(ifc));
	sbuf_delete(sb);
	CURVNET_RESTORE();

	return (error);
}

static int
linux_gifflags(struct thread *td, struct ifnet *ifp, struct l_ifreq *ifr)
{
	l_short flags;

	flags = (ifp->if_flags | ifp->if_drv_flags) & 0xffff;
	/* these flags have no Linux equivalent */
	flags &= ~(IFF_DRV_OACTIVE|IFF_SIMPLEX|
	    IFF_LINK0|IFF_LINK1|IFF_LINK2);
	/* Linux' multicast flag is in a different bit */
	if (flags & IFF_MULTICAST) {
		flags &= ~IFF_MULTICAST;
		flags |= 0x1000;
	}

	return (copyout(&flags, &ifr->ifr_flags, sizeof(flags)));
}

#define ARPHRD_ETHER	1
#define ARPHRD_LOOPBACK	772

static int
linux_gifhwaddr(struct ifnet *ifp, struct l_ifreq *ifr)
{
	struct ifaddr *ifa;
	struct sockaddr_dl *sdl;
	struct l_sockaddr lsa;

	if (ifp->if_type == IFT_LOOP) {
		bzero(&lsa, sizeof(lsa));
		lsa.sa_family = ARPHRD_LOOPBACK;
		return (copyout(&lsa, &ifr->ifr_hwaddr, sizeof(lsa)));
	}

	if (ifp->if_type != IFT_ETHER)
		return (ENOENT);

	CK_STAILQ_FOREACH(ifa, &ifp->if_addrhead, ifa_link) {
		sdl = (struct sockaddr_dl*)ifa->ifa_addr;
		if (sdl != NULL && (sdl->sdl_family == AF_LINK) &&
		    (sdl->sdl_type == IFT_ETHER)) {
			bzero(&lsa, sizeof(lsa));
			lsa.sa_family = ARPHRD_ETHER;
			bcopy(LLADDR(sdl), lsa.sa_data, LINUX_IFHWADDRLEN);
			return (copyout(&lsa, &ifr->ifr_hwaddr, sizeof(lsa)));
		}
	}

	return (ENOENT);
}


 /*
* If we fault in bsd_to_linux_ifreq() then we will fault when we call
* the native ioctl().  Thus, we don't really need to check the return
* value of this function.
*/
static int
bsd_to_linux_ifreq(struct ifreq *arg)
{
	struct ifreq ifr;
	size_t ifr_len = sizeof(struct ifreq);
	int error;

	if ((error = copyin(arg, &ifr, ifr_len)))
		return (error);

	*(u_short *)&ifr.ifr_addr = ifr.ifr_addr.sa_family;

	error = copyout(&ifr, arg, ifr_len);

	return (error);
}

/*
 * Socket related ioctls
 */

static int
linux_ioctl_socket(struct thread *td, struct linux_ioctl_args *args)
{
	char lifname[LINUX_IFNAMSIZ], ifname[IFNAMSIZ];
	struct ifnet *ifp;
	struct file *fp;
	int error, type;

	ifp = NULL;
	error = 0;

	error = fget(td, args->fd, &cap_ioctl_rights, &fp);
	if (error != 0)
		return (error);
	type = fp->f_type;
	fdrop(fp, td);
	if (type != DTYPE_SOCKET) {
		/* not a socket - probably a tap / vmnet device */
		switch (args->cmd) {
		case LINUX_SIOCGIFADDR:
		case LINUX_SIOCSIFADDR:
		case LINUX_SIOCGIFFLAGS:
			return (linux_ioctl_special(td, args));
		default:
			return (ENOIOCTL);
		}
	}

	switch (args->cmd & 0xffff) {

	case LINUX_FIOGETOWN:
	case LINUX_FIOSETOWN:
	case LINUX_SIOCADDMULTI:
	case LINUX_SIOCATMARK:
	case LINUX_SIOCDELMULTI:
	case LINUX_SIOCGIFNAME:
	case LINUX_SIOCGIFCONF:
	case LINUX_SIOCGPGRP:
	case LINUX_SIOCSPGRP:
	case LINUX_SIOCGIFCOUNT:
		/* these ioctls don't take an interface name */
#ifdef DEBUG
		printf("%s(): ioctl %d\n", __func__,
		    args->cmd & 0xffff);
#endif
		break;

	case LINUX_SIOCGIFFLAGS:
	case LINUX_SIOCGIFADDR:
	case LINUX_SIOCSIFADDR:
	case LINUX_SIOCGIFDSTADDR:
	case LINUX_SIOCGIFBRDADDR:
	case LINUX_SIOCGIFNETMASK:
	case LINUX_SIOCSIFNETMASK:
	case LINUX_SIOCGIFMTU:
	case LINUX_SIOCSIFMTU:
	case LINUX_SIOCSIFNAME:
	case LINUX_SIOCGIFHWADDR:
	case LINUX_SIOCSIFHWADDR:
	case LINUX_SIOCDEVPRIVATE:
	case LINUX_SIOCDEVPRIVATE+1:
	case LINUX_SIOCGIFINDEX:
		/* copy in the interface name and translate it. */
		error = copyin((void *)args->arg, lifname, LINUX_IFNAMSIZ);
		if (error != 0)
			return (error);
#ifdef DEBUG
		printf("%s(): ioctl %d on %.*s\n", __func__,
		    args->cmd & 0xffff, LINUX_IFNAMSIZ, lifname);
#endif
		memset(ifname, 0, sizeof(ifname));
		ifp = ifname_linux_to_bsd(td, lifname, ifname);
		if (ifp == NULL)
			return (EINVAL);
		/*
		 * We need to copy it back out in case we pass the
		 * request on to our native ioctl(), which will expect
		 * the ifreq to be in user space and have the correct
		 * interface name.
		 */
		error = copyout(ifname, (void *)args->arg, IFNAMSIZ);
		if (error != 0)
			return (error);
#ifdef DEBUG
		printf("%s(): %s translated to %s\n", __func__,
		    lifname, ifname);
#endif
		break;

	default:
		return (ENOIOCTL);
	}

	switch (args->cmd & 0xffff) {

	case LINUX_FIOSETOWN:
		args->cmd = FIOSETOWN;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		break;

	case LINUX_SIOCSPGRP:
		args->cmd = SIOCSPGRP;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		break;

	case LINUX_FIOGETOWN:
		args->cmd = FIOGETOWN;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		break;

	case LINUX_SIOCGPGRP:
		args->cmd = SIOCGPGRP;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		break;

	case LINUX_SIOCATMARK:
		args->cmd = SIOCATMARK;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		break;

	/* LINUX_SIOCGSTAMP */

	case LINUX_SIOCGIFNAME:
		error = linux_ioctl_ifname(td, (struct l_ifreq *)args->arg);
		break;

	case LINUX_SIOCGIFCONF:
		error = linux_ifconf(td, (struct ifconf *)args->arg);
		break;

	case LINUX_SIOCGIFFLAGS:
		args->cmd = SIOCGIFFLAGS;
		error = linux_gifflags(td, ifp, (struct l_ifreq *)args->arg);
		break;

	case LINUX_SIOCGIFADDR:
		args->cmd = SIOCGIFADDR;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		bsd_to_linux_ifreq((struct ifreq *)args->arg);
		break;

	case LINUX_SIOCSIFADDR:
		/* XXX probably doesn't work, included for completeness */
		args->cmd = SIOCSIFADDR;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		break;

	case LINUX_SIOCGIFDSTADDR:
		args->cmd = SIOCGIFDSTADDR;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		bsd_to_linux_ifreq((struct ifreq *)args->arg);
		break;

	case LINUX_SIOCGIFBRDADDR:
		args->cmd = SIOCGIFBRDADDR;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		bsd_to_linux_ifreq((struct ifreq *)args->arg);
		break;

	case LINUX_SIOCGIFNETMASK:
		args->cmd = SIOCGIFNETMASK;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		bsd_to_linux_ifreq((struct ifreq *)args->arg);
		break;

	case LINUX_SIOCSIFNETMASK:
		error = ENOIOCTL;
		break;

	case LINUX_SIOCGIFMTU:
		args->cmd = SIOCGIFMTU;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		break;

	case LINUX_SIOCSIFMTU:
		args->cmd = SIOCSIFMTU;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		break;

	case LINUX_SIOCSIFNAME:
		error = ENOIOCTL;
		break;

	case LINUX_SIOCGIFHWADDR:
		error = linux_gifhwaddr(ifp, (struct l_ifreq *)args->arg);
		break;

	case LINUX_SIOCSIFHWADDR:
		error = ENOIOCTL;
		break;

	case LINUX_SIOCADDMULTI:
		args->cmd = SIOCADDMULTI;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		break;

	case LINUX_SIOCDELMULTI:
		args->cmd = SIOCDELMULTI;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		break;

	case LINUX_SIOCGIFINDEX:
		args->cmd = SIOCGIFINDEX;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		break;

	case LINUX_SIOCGIFCOUNT:
		error = 0;
		break;

	/*
	 * XXX This is slightly bogus, but these ioctls are currently
	 * XXX only used by the aironet (if_an) network driver.
	 */
	case LINUX_SIOCDEVPRIVATE:
		args->cmd = SIOCGPRIVATE_0;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		break;

	case LINUX_SIOCDEVPRIVATE+1:
		args->cmd = SIOCGPRIVATE_1;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		break;
	}

	if (ifp != NULL)
		/* restore the original interface name */
		copyout(lifname, (void *)args->arg, LINUX_IFNAMSIZ);

#ifdef DEBUG
	printf("%s(): returning %d\n", __func__, error);
#endif
	return (error);
}

/*
 * Device private ioctl handler
 */
static int
linux_ioctl_private(struct thread *td, struct linux_ioctl_args *args)
{
	struct file *fp;
	int error, type;

	error = fget(td, args->fd, &cap_ioctl_rights, &fp);
	if (error != 0)
		return (error);
	type = fp->f_type;
	fdrop(fp, td);
	if (type == DTYPE_SOCKET)
		return (linux_ioctl_socket(td, args));
	return (ENOIOCTL);
}

/*
 * DRM ioctl handler (sys/dev/drm)
 */
static int
linux_ioctl_drm(struct thread *td, struct linux_ioctl_args *args)
{
	args->cmd = SETDIR(args->cmd);
	return (sys_ioctl(td, (struct ioctl_args *)args));
}

#ifdef COMPAT_LINUX32
#define CP(src,dst,fld) do { (dst).fld = (src).fld; } while (0)
#define PTRIN_CP(src,dst,fld) \
	do { (dst).fld = PTRIN((src).fld); } while (0)
#define PTROUT_CP(src,dst,fld) \
	do { (dst).fld = PTROUT((src).fld); } while (0)

static int
linux_ioctl_sg_io(struct thread *td, struct linux_ioctl_args *args)
{
	struct sg_io_hdr io;
	struct sg_io_hdr32 io32;
	struct file *fp;
	int error;

	error = fget(td, args->fd, &cap_ioctl_rights, &fp);
	if (error != 0) {
		printf("sg_linux_ioctl: fget returned %d\n", error);
		return (error);
	}

	if ((error = copyin((void *)args->arg, &io32, sizeof(io32))) != 0)
		goto out;

	CP(io32, io, interface_id);
	CP(io32, io, dxfer_direction);
	CP(io32, io, cmd_len);
	CP(io32, io, mx_sb_len);
	CP(io32, io, iovec_count);
	CP(io32, io, dxfer_len);
	PTRIN_CP(io32, io, dxferp);
	PTRIN_CP(io32, io, cmdp);
	PTRIN_CP(io32, io, sbp);
	CP(io32, io, timeout);
	CP(io32, io, flags);
	CP(io32, io, pack_id);
	PTRIN_CP(io32, io, usr_ptr);
	CP(io32, io, status);
	CP(io32, io, masked_status);
	CP(io32, io, msg_status);
	CP(io32, io, sb_len_wr);
	CP(io32, io, host_status);
	CP(io32, io, driver_status);
	CP(io32, io, resid);
	CP(io32, io, duration);
	CP(io32, io, info);

	if ((error = fo_ioctl(fp, SG_IO, (caddr_t)&io, td->td_ucred, td)) != 0)
		goto out;

	CP(io, io32, interface_id);
	CP(io, io32, dxfer_direction);
	CP(io, io32, cmd_len);
	CP(io, io32, mx_sb_len);
	CP(io, io32, iovec_count);
	CP(io, io32, dxfer_len);
	PTROUT_CP(io, io32, dxferp);
	PTROUT_CP(io, io32, cmdp);
	PTROUT_CP(io, io32, sbp);
	CP(io, io32, timeout);
	CP(io, io32, flags);
	CP(io, io32, pack_id);
	PTROUT_CP(io, io32, usr_ptr);
	CP(io, io32, status);
	CP(io, io32, masked_status);
	CP(io, io32, msg_status);
	CP(io, io32, sb_len_wr);
	CP(io, io32, host_status);
	CP(io, io32, driver_status);
	CP(io, io32, resid);
	CP(io, io32, duration);
	CP(io, io32, info);

	error = copyout(&io32, (void *)args->arg, sizeof(io32));

out:
	fdrop(fp, td);
	return (error);
}
#endif

static int
linux_ioctl_sg(struct thread *td, struct linux_ioctl_args *args)
{

	switch (args->cmd) {
	case LINUX_SG_GET_VERSION_NUM:
		args->cmd = SG_GET_VERSION_NUM;
		break;
	case LINUX_SG_SET_TIMEOUT:
		args->cmd = SG_SET_TIMEOUT;
		break;
	case LINUX_SG_GET_TIMEOUT:
		args->cmd = SG_GET_TIMEOUT;
		break;
	case LINUX_SG_IO:
		args->cmd = SG_IO;
#ifdef COMPAT_LINUX32
		return (linux_ioctl_sg_io(td, args));
#endif
		break;
	case LINUX_SG_GET_RESERVED_SIZE:
		args->cmd = SG_GET_RESERVED_SIZE;
		break;
	case LINUX_SG_GET_SCSI_ID:
		args->cmd = SG_GET_SCSI_ID;
		break;
	case LINUX_SG_GET_SG_TABLESIZE:
		args->cmd = SG_GET_SG_TABLESIZE;
		break;
	default:
		return (ENODEV);
	}
	return (sys_ioctl(td, (struct ioctl_args *)args));
}

/*
 * Video4Linux (V4L) ioctl handler
 */
static int
linux_to_bsd_v4l_tuner(struct l_video_tuner *lvt, struct video_tuner *vt)
{
	vt->tuner = lvt->tuner;
	strlcpy(vt->name, lvt->name, LINUX_VIDEO_TUNER_NAME_SIZE);
	vt->rangelow = lvt->rangelow;	/* possible long size conversion */
	vt->rangehigh = lvt->rangehigh;	/* possible long size conversion */
	vt->flags = lvt->flags;
	vt->mode = lvt->mode;
	vt->signal = lvt->signal;
	return (0);
}

static int
bsd_to_linux_v4l_tuner(struct video_tuner *vt, struct l_video_tuner *lvt)
{
	lvt->tuner = vt->tuner;
	strlcpy(lvt->name, vt->name, LINUX_VIDEO_TUNER_NAME_SIZE);
	lvt->rangelow = vt->rangelow;	/* possible long size conversion */
	lvt->rangehigh = vt->rangehigh;	/* possible long size conversion */
	lvt->flags = vt->flags;
	lvt->mode = vt->mode;
	lvt->signal = vt->signal;
	return (0);
}

#ifdef COMPAT_LINUX_V4L_CLIPLIST
static int
linux_to_bsd_v4l_clip(struct l_video_clip *lvc, struct video_clip *vc)
{
	vc->x = lvc->x;
	vc->y = lvc->y;
	vc->width = lvc->width;
	vc->height = lvc->height;
	vc->next = PTRIN(lvc->next);	/* possible pointer size conversion */
	return (0);
}
#endif

static int
linux_to_bsd_v4l_window(struct l_video_window *lvw, struct video_window *vw)
{
	vw->x = lvw->x;
	vw->y = lvw->y;
	vw->width = lvw->width;
	vw->height = lvw->height;
	vw->chromakey = lvw->chromakey;
	vw->flags = lvw->flags;
	vw->clips = PTRIN(lvw->clips);	/* possible pointer size conversion */
	vw->clipcount = lvw->clipcount;
	return (0);
}

static int
bsd_to_linux_v4l_window(struct video_window *vw, struct l_video_window *lvw)
{
	memset(lvw, 0, sizeof(*lvw));

	lvw->x = vw->x;
	lvw->y = vw->y;
	lvw->width = vw->width;
	lvw->height = vw->height;
	lvw->chromakey = vw->chromakey;
	lvw->flags = vw->flags;
	lvw->clips = PTROUT(vw->clips);	/* possible pointer size conversion */
	lvw->clipcount = vw->clipcount;
	return (0);
}

static int
linux_to_bsd_v4l_buffer(struct l_video_buffer *lvb, struct video_buffer *vb)
{
	vb->base = PTRIN(lvb->base);	/* possible pointer size conversion */
	vb->height = lvb->height;
	vb->width = lvb->width;
	vb->depth = lvb->depth;
	vb->bytesperline = lvb->bytesperline;
	return (0);
}

static int
bsd_to_linux_v4l_buffer(struct video_buffer *vb, struct l_video_buffer *lvb)
{
	lvb->base = PTROUT(vb->base);	/* possible pointer size conversion */
	lvb->height = vb->height;
	lvb->width = vb->width;
	lvb->depth = vb->depth;
	lvb->bytesperline = vb->bytesperline;
	return (0);
}

static int
linux_to_bsd_v4l_code(struct l_video_code *lvc, struct video_code *vc)
{
	strlcpy(vc->loadwhat, lvc->loadwhat, LINUX_VIDEO_CODE_LOADWHAT_SIZE);
	vc->datasize = lvc->datasize;
	vc->data = PTRIN(lvc->data);	/* possible pointer size conversion */
	return (0);
}

#ifdef COMPAT_LINUX_V4L_CLIPLIST
static int
linux_v4l_clip_copy(void *lvc, struct video_clip **ppvc)
{
	int error;
	struct video_clip vclip;
	struct l_video_clip l_vclip;

	error = copyin(lvc, &l_vclip, sizeof(l_vclip));
	if (error) return (error);
	linux_to_bsd_v4l_clip(&l_vclip, &vclip);
	/* XXX: If there can be no concurrency: s/M_NOWAIT/M_WAITOK/ */
	if ((*ppvc = malloc(sizeof(**ppvc), M_LINUX, M_NOWAIT)) == NULL)
		return (ENOMEM);    /* XXX: Linux has no ENOMEM here. */
	memcpy(*ppvc, &vclip, sizeof(vclip));
	(*ppvc)->next = NULL;
	return (0);
}

static int
linux_v4l_cliplist_free(struct video_window *vw)
{
	struct video_clip **ppvc;
	struct video_clip **ppvc_next;

	for (ppvc = &(vw->clips); *ppvc != NULL; ppvc = ppvc_next) {
		ppvc_next = &((*ppvc)->next);
		free(*ppvc, M_LINUX);
	}
	vw->clips = NULL;

	return (0);
}

static int
linux_v4l_cliplist_copy(struct l_video_window *lvw, struct video_window *vw)
{
	int error;
	int clipcount;
	void *plvc;
	struct video_clip **ppvc;

	/*
	 * XXX: The cliplist is used to pass in a list of clipping
	 *	rectangles or, if clipcount == VIDEO_CLIP_BITMAP, a
	 *	clipping bitmap.  Some Linux apps, however, appear to
	 *	leave cliplist and clips uninitialized.  In any case,
	 *	the cliplist is not used by pwc(4), at the time of
	 *	writing, FreeBSD's only V4L driver.  When a driver
	 *	that uses the cliplist is developed, this code may
	 *	need re-examiniation.
	 */
	error = 0;
	clipcount = vw->clipcount;
	if (clipcount == VIDEO_CLIP_BITMAP) {
		/*
		 * In this case, the pointer (clips) is overloaded
		 * to be a "void *" to a bitmap, therefore there
		 * is no struct video_clip to copy now.
		 */
	} else if (clipcount > 0 && clipcount <= 16384) {
		/*
		 * Clips points to list of clip rectangles, so
		 * copy the list.
		 *
		 * XXX: Upper limit of 16384 was used here to try to
		 *	avoid cases when clipcount and clips pointer
		 *	are uninitialized and therefore have high random
		 *	values, as is the case in the Linux Skype
		 *	application.  The value 16384 was chosen as that
		 *	is what is used in the Linux stradis(4) MPEG
		 *	decoder driver, the only place we found an
		 *	example of cliplist use.
		 */
		plvc = PTRIN(lvw->clips);
		vw->clips = NULL;
		ppvc = &(vw->clips);
		while (clipcount-- > 0) {
			if (plvc == NULL) {
				error = EFAULT;
				break;
			} else {
				error = linux_v4l_clip_copy(plvc, ppvc);
				if (error) {
					linux_v4l_cliplist_free(vw);
					break;
				}
			}
			ppvc = &((*ppvc)->next);
			plvc = PTRIN(((struct l_video_clip *) plvc)->next);
		}
	} else {
		/*
		 * clipcount == 0 or negative (but not VIDEO_CLIP_BITMAP)
		 * Force cliplist to null.
		 */
		vw->clipcount = 0;
		vw->clips = NULL;
	}
	return (error);
}
#endif

static int
linux_ioctl_v4l(struct thread *td, struct linux_ioctl_args *args)
{
	struct file *fp;
	int error;
	struct video_tuner vtun;
	struct video_window vwin;
	struct video_buffer vbuf;
	struct video_code vcode;
	struct l_video_tuner l_vtun;
	struct l_video_window l_vwin;
	struct l_video_buffer l_vbuf;
	struct l_video_code l_vcode;

	switch (args->cmd & 0xffff) {
	case LINUX_VIDIOCGCAP:		args->cmd = VIDIOCGCAP; break;
	case LINUX_VIDIOCGCHAN:		args->cmd = VIDIOCGCHAN; break;
	case LINUX_VIDIOCSCHAN:		args->cmd = VIDIOCSCHAN; break;

	case LINUX_VIDIOCGTUNER:
		error = fget(td, args->fd,
		    &cap_ioctl_rights, &fp);
		if (error != 0)
			return (error);
		error = copyin((void *) args->arg, &l_vtun, sizeof(l_vtun));
		if (error) {
			fdrop(fp, td);
			return (error);
		}
		linux_to_bsd_v4l_tuner(&l_vtun, &vtun);
		error = fo_ioctl(fp, VIDIOCGTUNER, &vtun, td->td_ucred, td);
		if (!error) {
			bsd_to_linux_v4l_tuner(&vtun, &l_vtun);
			error = copyout(&l_vtun, (void *) args->arg,
			    sizeof(l_vtun));
		}
		fdrop(fp, td);
		return (error);

	case LINUX_VIDIOCSTUNER:
		error = fget(td, args->fd,
		    &cap_ioctl_rights, &fp);
		if (error != 0)
			return (error);
		error = copyin((void *) args->arg, &l_vtun, sizeof(l_vtun));
		if (error) {
			fdrop(fp, td);
			return (error);
		}
		linux_to_bsd_v4l_tuner(&l_vtun, &vtun);
		error = fo_ioctl(fp, VIDIOCSTUNER, &vtun, td->td_ucred, td);
		fdrop(fp, td);
		return (error);

	case LINUX_VIDIOCGPICT:		args->cmd = VIDIOCGPICT; break;
	case LINUX_VIDIOCSPICT:		args->cmd = VIDIOCSPICT; break;
	case LINUX_VIDIOCCAPTURE:	args->cmd = VIDIOCCAPTURE; break;

	case LINUX_VIDIOCGWIN:
		error = fget(td, args->fd,
		    &cap_ioctl_rights, &fp);
		if (error != 0)
			return (error);
		error = fo_ioctl(fp, VIDIOCGWIN, &vwin, td->td_ucred, td);
		if (!error) {
			bsd_to_linux_v4l_window(&vwin, &l_vwin);
			error = copyout(&l_vwin, (void *) args->arg,
			    sizeof(l_vwin));
		}
		fdrop(fp, td);
		return (error);

	case LINUX_VIDIOCSWIN:
		error = fget(td, args->fd,
		    &cap_ioctl_rights, &fp);
		if (error != 0)
			return (error);
		error = copyin((void *) args->arg, &l_vwin, sizeof(l_vwin));
		if (error) {
			fdrop(fp, td);
			return (error);
		}
		linux_to_bsd_v4l_window(&l_vwin, &vwin);
#ifdef COMPAT_LINUX_V4L_CLIPLIST
		error = linux_v4l_cliplist_copy(&l_vwin, &vwin);
		if (error) {
			fdrop(fp, td);
			return (error);
		}
#endif
		error = fo_ioctl(fp, VIDIOCSWIN, &vwin, td->td_ucred, td);
		fdrop(fp, td);
#ifdef COMPAT_LINUX_V4L_CLIPLIST
		linux_v4l_cliplist_free(&vwin);
#endif
		return (error);

	case LINUX_VIDIOCGFBUF:
		error = fget(td, args->fd,
		    &cap_ioctl_rights, &fp);
		if (error != 0)
			return (error);
		error = fo_ioctl(fp, VIDIOCGFBUF, &vbuf, td->td_ucred, td);
		if (!error) {
			bsd_to_linux_v4l_buffer(&vbuf, &l_vbuf);
			error = copyout(&l_vbuf, (void *) args->arg,
			    sizeof(l_vbuf));
		}
		fdrop(fp, td);
		return (error);

	case LINUX_VIDIOCSFBUF:
		error = fget(td, args->fd,
		    &cap_ioctl_rights, &fp);
		if (error != 0)
			return (error);
		error = copyin((void *) args->arg, &l_vbuf, sizeof(l_vbuf));
		if (error) {
			fdrop(fp, td);
			return (error);
		}
		linux_to_bsd_v4l_buffer(&l_vbuf, &vbuf);
		error = fo_ioctl(fp, VIDIOCSFBUF, &vbuf, td->td_ucred, td);
		fdrop(fp, td);
		return (error);

	case LINUX_VIDIOCKEY:		args->cmd = VIDIOCKEY; break;
	case LINUX_VIDIOCGFREQ:		args->cmd = VIDIOCGFREQ; break;
	case LINUX_VIDIOCSFREQ:		args->cmd = VIDIOCSFREQ; break;
	case LINUX_VIDIOCGAUDIO:	args->cmd = VIDIOCGAUDIO; break;
	case LINUX_VIDIOCSAUDIO:	args->cmd = VIDIOCSAUDIO; break;
	case LINUX_VIDIOCSYNC:		args->cmd = VIDIOCSYNC; break;
	case LINUX_VIDIOCMCAPTURE:	args->cmd = VIDIOCMCAPTURE; break;
	case LINUX_VIDIOCGMBUF:		args->cmd = VIDIOCGMBUF; break;
	case LINUX_VIDIOCGUNIT:		args->cmd = VIDIOCGUNIT; break;
	case LINUX_VIDIOCGCAPTURE:	args->cmd = VIDIOCGCAPTURE; break;
	case LINUX_VIDIOCSCAPTURE:	args->cmd = VIDIOCSCAPTURE; break;
	case LINUX_VIDIOCSPLAYMODE:	args->cmd = VIDIOCSPLAYMODE; break;
	case LINUX_VIDIOCSWRITEMODE:	args->cmd = VIDIOCSWRITEMODE; break;
	case LINUX_VIDIOCGPLAYINFO:	args->cmd = VIDIOCGPLAYINFO; break;

	case LINUX_VIDIOCSMICROCODE:
		error = fget(td, args->fd,
		    &cap_ioctl_rights, &fp);
		if (error != 0)
			return (error);
		error = copyin((void *) args->arg, &l_vcode, sizeof(l_vcode));
		if (error) {
			fdrop(fp, td);
			return (error);
		}
		linux_to_bsd_v4l_code(&l_vcode, &vcode);
		error = fo_ioctl(fp, VIDIOCSMICROCODE, &vcode, td->td_ucred, td);
		fdrop(fp, td);
		return (error);

	case LINUX_VIDIOCGVBIFMT:	args->cmd = VIDIOCGVBIFMT; break;
	case LINUX_VIDIOCSVBIFMT:	args->cmd = VIDIOCSVBIFMT; break;
	default:			return (ENOIOCTL);
	}

	error = sys_ioctl(td, (struct ioctl_args *)args);
	return (error);
}

/*
 * Special ioctl handler
 */
static int
linux_ioctl_special(struct thread *td, struct linux_ioctl_args *args)
{
	int error;

	switch (args->cmd) {
	case LINUX_SIOCGIFADDR:
		args->cmd = SIOCGIFADDR;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		break;
	case LINUX_SIOCSIFADDR:
		args->cmd = SIOCSIFADDR;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		break;
	case LINUX_SIOCGIFFLAGS:
		args->cmd = SIOCGIFFLAGS;
		error = sys_ioctl(td, (struct ioctl_args *)args);
		break;
	default:
		error = ENOIOCTL;
	}

	return (error);
}

static int
linux_to_bsd_v4l2_standard(struct l_v4l2_standard *lvstd, struct v4l2_standard *vstd)
{
	vstd->index = lvstd->index;
	vstd->id = lvstd->id;
	CTASSERT(sizeof(vstd->name) == sizeof(lvstd->name));
	memcpy(vstd->name, lvstd->name, sizeof(vstd->name));
	vstd->frameperiod = lvstd->frameperiod;
	vstd->framelines = lvstd->framelines;
	CTASSERT(sizeof(vstd->reserved) == sizeof(lvstd->reserved));
	memcpy(vstd->reserved, lvstd->reserved, sizeof(vstd->reserved));
	return (0);
}

static int
bsd_to_linux_v4l2_standard(struct v4l2_standard *vstd, struct l_v4l2_standard *lvstd)
{
	lvstd->index = vstd->index;
	lvstd->id = vstd->id;
	CTASSERT(sizeof(vstd->name) == sizeof(lvstd->name));
	memcpy(lvstd->name, vstd->name, sizeof(lvstd->name));
	lvstd->frameperiod = vstd->frameperiod;
	lvstd->framelines = vstd->framelines;
	CTASSERT(sizeof(vstd->reserved) == sizeof(lvstd->reserved));
	memcpy(lvstd->reserved, vstd->reserved, sizeof(lvstd->reserved));
	return (0);
}

static int
linux_to_bsd_v4l2_buffer(struct l_v4l2_buffer *lvb, struct v4l2_buffer *vb)
{
	vb->index = lvb->index;
	vb->type = lvb->type;
	vb->bytesused = lvb->bytesused;
	vb->flags = lvb->flags;
	vb->field = lvb->field;
	vb->timestamp.tv_sec = lvb->timestamp.tv_sec;
	vb->timestamp.tv_usec = lvb->timestamp.tv_usec;
	memcpy(&vb->timecode, &lvb->timecode, sizeof (lvb->timecode));
	vb->sequence = lvb->sequence;
	vb->memory = lvb->memory;
	if (lvb->memory == V4L2_MEMORY_USERPTR)
		/* possible pointer size conversion */
		vb->m.userptr = (unsigned long)PTRIN(lvb->m.userptr);
	else
		vb->m.offset = lvb->m.offset;
	vb->length = lvb->length;
	vb->input = lvb->input;
	vb->reserved = lvb->reserved;
	return (0);
}

static int
bsd_to_linux_v4l2_buffer(struct v4l2_buffer *vb, struct l_v4l2_buffer *lvb)
{
	lvb->index = vb->index;
	lvb->type = vb->type;
	lvb->bytesused = vb->bytesused;
	lvb->flags = vb->flags;
	lvb->field = vb->field;
	lvb->timestamp.tv_sec = vb->timestamp.tv_sec;
	lvb->timestamp.tv_usec = vb->timestamp.tv_usec;
	memcpy(&lvb->timecode, &vb->timecode, sizeof (vb->timecode));
	lvb->sequence = vb->sequence;
	lvb->memory = vb->memory;
	if (vb->memory == V4L2_MEMORY_USERPTR)
		/* possible pointer size conversion */
		lvb->m.userptr = PTROUT(vb->m.userptr);
	else
		lvb->m.offset = vb->m.offset;
	lvb->length = vb->length;
	lvb->input = vb->input;
	lvb->reserved = vb->reserved;
	return (0);
}

static int
linux_to_bsd_v4l2_format(struct l_v4l2_format *lvf, struct v4l2_format *vf)
{
	vf->type = lvf->type;
	if (lvf->type == V4L2_BUF_TYPE_VIDEO_OVERLAY
#ifdef V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY
	    || lvf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY
#endif
	    )
		/*
		 * XXX TODO - needs 32 -> 64 bit conversion:
		 * (unused by webcams?)
		 */
		return (EINVAL);
	memcpy(&vf->fmt, &lvf->fmt, sizeof(vf->fmt));
	return (0);
}

static int
bsd_to_linux_v4l2_format(struct v4l2_format *vf, struct l_v4l2_format *lvf)
{
	lvf->type = vf->type;
	if (vf->type == V4L2_BUF_TYPE_VIDEO_OVERLAY
#ifdef V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY
	    || vf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY
#endif
	    )
		/*
		 * XXX TODO - needs 32 -> 64 bit conversion:
		 * (unused by webcams?)
		 */
		return (EINVAL);
	memcpy(&lvf->fmt, &vf->fmt, sizeof(vf->fmt));
	return (0);
}
static int
linux_ioctl_v4l2(struct thread *td, struct linux_ioctl_args *args)
{
	struct file *fp;
	int error;
	struct v4l2_format vformat;
	struct l_v4l2_format l_vformat;
	struct v4l2_standard vstd;
	struct l_v4l2_standard l_vstd;
	struct l_v4l2_buffer l_vbuf;
	struct v4l2_buffer vbuf;
	struct v4l2_input vinp;

	switch (args->cmd & 0xffff) {
	case LINUX_VIDIOC_RESERVED:
	case LINUX_VIDIOC_LOG_STATUS:
		if ((args->cmd & IOC_DIRMASK) != LINUX_IOC_VOID)
			return (ENOIOCTL);
		args->cmd = (args->cmd & 0xffff) | IOC_VOID;
		break;

	case LINUX_VIDIOC_OVERLAY:
	case LINUX_VIDIOC_STREAMON:
	case LINUX_VIDIOC_STREAMOFF:
	case LINUX_VIDIOC_S_STD:
	case LINUX_VIDIOC_S_TUNER:
	case LINUX_VIDIOC_S_AUDIO:
	case LINUX_VIDIOC_S_AUDOUT:
	case LINUX_VIDIOC_S_MODULATOR:
	case LINUX_VIDIOC_S_FREQUENCY:
	case LINUX_VIDIOC_S_CROP:
	case LINUX_VIDIOC_S_JPEGCOMP:
	case LINUX_VIDIOC_S_PRIORITY:
	case LINUX_VIDIOC_DBG_S_REGISTER:
	case LINUX_VIDIOC_S_HW_FREQ_SEEK:
	case LINUX_VIDIOC_SUBSCRIBE_EVENT:
	case LINUX_VIDIOC_UNSUBSCRIBE_EVENT:
		args->cmd = (args->cmd & ~IOC_DIRMASK) | IOC_IN;
		break;

	case LINUX_VIDIOC_QUERYCAP:
	case LINUX_VIDIOC_G_STD:
	case LINUX_VIDIOC_G_AUDIO:
	case LINUX_VIDIOC_G_INPUT:
	case LINUX_VIDIOC_G_OUTPUT:
	case LINUX_VIDIOC_G_AUDOUT:
	case LINUX_VIDIOC_G_JPEGCOMP:
	case LINUX_VIDIOC_QUERYSTD:
	case LINUX_VIDIOC_G_PRIORITY:
	case LINUX_VIDIOC_QUERY_DV_PRESET:
		args->cmd = (args->cmd & ~IOC_DIRMASK) | IOC_OUT;
		break;

	case LINUX_VIDIOC_ENUM_FMT:
	case LINUX_VIDIOC_REQBUFS:
	case LINUX_VIDIOC_G_PARM:
	case LINUX_VIDIOC_S_PARM:
	case LINUX_VIDIOC_G_CTRL:
	case LINUX_VIDIOC_S_CTRL:
	case LINUX_VIDIOC_G_TUNER:
	case LINUX_VIDIOC_QUERYCTRL:
	case LINUX_VIDIOC_QUERYMENU:
	case LINUX_VIDIOC_S_INPUT:
	case LINUX_VIDIOC_S_OUTPUT:
	case LINUX_VIDIOC_ENUMOUTPUT:
	case LINUX_VIDIOC_G_MODULATOR:
	case LINUX_VIDIOC_G_FREQUENCY:
	case LINUX_VIDIOC_CROPCAP:
	case LINUX_VIDIOC_G_CROP:
	case LINUX_VIDIOC_ENUMAUDIO:
	case LINUX_VIDIOC_ENUMAUDOUT:
	case LINUX_VIDIOC_G_SLICED_VBI_CAP:
#ifdef VIDIOC_ENUM_FRAMESIZES
	case LINUX_VIDIOC_ENUM_FRAMESIZES:
	case LINUX_VIDIOC_ENUM_FRAMEINTERVALS:
	case LINUX_VIDIOC_ENCODER_CMD:
	case LINUX_VIDIOC_TRY_ENCODER_CMD:
#endif
	case LINUX_VIDIOC_DBG_G_REGISTER:
	case LINUX_VIDIOC_DBG_G_CHIP_IDENT:
	case LINUX_VIDIOC_ENUM_DV_PRESETS:
	case LINUX_VIDIOC_S_DV_PRESET:
	case LINUX_VIDIOC_G_DV_PRESET:
	case LINUX_VIDIOC_S_DV_TIMINGS:
	case LINUX_VIDIOC_G_DV_TIMINGS:
		args->cmd = (args->cmd & ~IOC_DIRMASK) | IOC_INOUT;
		break;

	case LINUX_VIDIOC_G_FMT:
	case LINUX_VIDIOC_S_FMT:
	case LINUX_VIDIOC_TRY_FMT:
		error = copyin((void *)args->arg, &l_vformat, sizeof(l_vformat));
		if (error)
			return (error);
		error = fget(td, args->fd,
		    &cap_ioctl_rights, &fp);
		if (error)
			return (error);
		if (linux_to_bsd_v4l2_format(&l_vformat, &vformat) != 0)
			error = EINVAL;
		else if ((args->cmd & 0xffff) == LINUX_VIDIOC_G_FMT)
			error = fo_ioctl(fp, VIDIOC_G_FMT, &vformat,
			    td->td_ucred, td);
		else if ((args->cmd & 0xffff) == LINUX_VIDIOC_S_FMT)
			error = fo_ioctl(fp, VIDIOC_S_FMT, &vformat,
			    td->td_ucred, td);
		else
			error = fo_ioctl(fp, VIDIOC_TRY_FMT, &vformat,
			    td->td_ucred, td);
		bsd_to_linux_v4l2_format(&vformat, &l_vformat);
		copyout(&l_vformat, (void *)args->arg, sizeof(l_vformat));
		fdrop(fp, td);
		return (error);

	case LINUX_VIDIOC_ENUMSTD:
		error = copyin((void *)args->arg, &l_vstd, sizeof(l_vstd));
		if (error)
			return (error);
		linux_to_bsd_v4l2_standard(&l_vstd, &vstd);
		error = fget(td, args->fd,
		    &cap_ioctl_rights, &fp);
		if (error)
			return (error);
		error = fo_ioctl(fp, VIDIOC_ENUMSTD, (caddr_t)&vstd,
		    td->td_ucred, td);
		if (error) {
			fdrop(fp, td);
			return (error);
		}
		bsd_to_linux_v4l2_standard(&vstd, &l_vstd);
		error = copyout(&l_vstd, (void *)args->arg, sizeof(l_vstd));
		fdrop(fp, td);
		return (error);

	case LINUX_VIDIOC_ENUMINPUT:
		/*
		 * The Linux struct l_v4l2_input differs only in size,
		 * it has no padding at the end.
		 */
		error = copyin((void *)args->arg, &vinp,
				sizeof(struct l_v4l2_input));
		if (error != 0)
			return (error);
		error = fget(td, args->fd,
		    &cap_ioctl_rights, &fp);
		if (error != 0)
			return (error);
		error = fo_ioctl(fp, VIDIOC_ENUMINPUT, (caddr_t)&vinp,
		    td->td_ucred, td);
		if (error) {
			fdrop(fp, td);
			return (error);
		}
		error = copyout(&vinp, (void *)args->arg,
				sizeof(struct l_v4l2_input));
		fdrop(fp, td);
		return (error);

	case LINUX_VIDIOC_QUERYBUF:
	case LINUX_VIDIOC_QBUF:
	case LINUX_VIDIOC_DQBUF:
		error = copyin((void *)args->arg, &l_vbuf, sizeof(l_vbuf));
		if (error)
			return (error);
		error = fget(td, args->fd,
		    &cap_ioctl_rights, &fp);
		if (error)
			return (error);
		linux_to_bsd_v4l2_buffer(&l_vbuf, &vbuf);
		if ((args->cmd & 0xffff) == LINUX_VIDIOC_QUERYBUF)
			error = fo_ioctl(fp, VIDIOC_QUERYBUF, &vbuf,
			    td->td_ucred, td);
		else if ((args->cmd & 0xffff) == LINUX_VIDIOC_QBUF)
			error = fo_ioctl(fp, VIDIOC_QBUF, &vbuf,
			    td->td_ucred, td);
		else
			error = fo_ioctl(fp, VIDIOC_DQBUF, &vbuf,
			    td->td_ucred, td);
		bsd_to_linux_v4l2_buffer(&vbuf, &l_vbuf);
		copyout(&l_vbuf, (void *)args->arg, sizeof(l_vbuf));
		fdrop(fp, td);
		return (error);

	/*
	 * XXX TODO - these need 32 -> 64 bit conversion:
	 * (are any of them needed for webcams?)
	 */
	case LINUX_VIDIOC_G_FBUF:
	case LINUX_VIDIOC_S_FBUF:

	case LINUX_VIDIOC_G_EXT_CTRLS:
	case LINUX_VIDIOC_S_EXT_CTRLS:
	case LINUX_VIDIOC_TRY_EXT_CTRLS:

	case LINUX_VIDIOC_DQEVENT:

	default:			return (ENOIOCTL);
	}

	error = sys_ioctl(td, (struct ioctl_args *)args);
	return (error);
}

/*
 * Support for emulators/linux-libusb. This port uses FBSD_LUSB* macros
 * instead of USB* ones. This lets us to provide correct values for cmd.
 * 0xffffffe0 -- 0xffffffff range seemed to be the least collision-prone.
 */
static int
linux_ioctl_fbsd_usb(struct thread *td, struct linux_ioctl_args *args)
{
	int error;

	error = 0;
	switch (args->cmd) {
	case FBSD_LUSB_DEVICEENUMERATE:
		args->cmd = USB_DEVICEENUMERATE;
		break;
	case FBSD_LUSB_DEV_QUIRK_ADD:
		args->cmd = USB_DEV_QUIRK_ADD;
		break;
	case FBSD_LUSB_DEV_QUIRK_GET:
		args->cmd = USB_DEV_QUIRK_GET;
		break;
	case FBSD_LUSB_DEV_QUIRK_REMOVE:
		args->cmd = USB_DEV_QUIRK_REMOVE;
		break;
	case FBSD_LUSB_DO_REQUEST:
		args->cmd = USB_DO_REQUEST;
		break;
	case FBSD_LUSB_FS_CLEAR_STALL_SYNC:
		args->cmd = USB_FS_CLEAR_STALL_SYNC;
		break;
	case FBSD_LUSB_FS_CLOSE:
		args->cmd = USB_FS_CLOSE;
		break;
	case FBSD_LUSB_FS_COMPLETE:
		args->cmd = USB_FS_COMPLETE;
		break;
	case FBSD_LUSB_FS_INIT:
		args->cmd = USB_FS_INIT;
		break;
	case FBSD_LUSB_FS_OPEN:
		args->cmd = USB_FS_OPEN;
		break;
	case FBSD_LUSB_FS_START:
		args->cmd = USB_FS_START;
		break;
	case FBSD_LUSB_FS_STOP:
		args->cmd = USB_FS_STOP;
		break;
	case FBSD_LUSB_FS_UNINIT:
		args->cmd = USB_FS_UNINIT;
		break;
	case FBSD_LUSB_GET_CONFIG:
		args->cmd = USB_GET_CONFIG;
		break;
	case FBSD_LUSB_GET_DEVICEINFO:
		args->cmd = USB_GET_DEVICEINFO;
		break;
	case FBSD_LUSB_GET_DEVICE_DESC:
		args->cmd = USB_GET_DEVICE_DESC;
		break;
	case FBSD_LUSB_GET_FULL_DESC:
		args->cmd = USB_GET_FULL_DESC;
		break;
	case FBSD_LUSB_GET_IFACE_DRIVER:
		args->cmd = USB_GET_IFACE_DRIVER;
		break;
	case FBSD_LUSB_GET_PLUGTIME:
		args->cmd = USB_GET_PLUGTIME;
		break;
	case FBSD_LUSB_GET_POWER_MODE:
		args->cmd = USB_GET_POWER_MODE;
		break;
	case FBSD_LUSB_GET_REPORT_DESC:
		args->cmd = USB_GET_REPORT_DESC;
		break;
	case FBSD_LUSB_GET_REPORT_ID:
		args->cmd = USB_GET_REPORT_ID;
		break;
	case FBSD_LUSB_GET_TEMPLATE:
		args->cmd = USB_GET_TEMPLATE;
		break;
	case FBSD_LUSB_IFACE_DRIVER_ACTIVE:
		args->cmd = USB_IFACE_DRIVER_ACTIVE;
		break;
	case FBSD_LUSB_IFACE_DRIVER_DETACH:
		args->cmd = USB_IFACE_DRIVER_DETACH;
		break;
	case FBSD_LUSB_QUIRK_NAME_GET:
		args->cmd = USB_QUIRK_NAME_GET;
		break;
	case FBSD_LUSB_READ_DIR:
		args->cmd = USB_READ_DIR;
		break;
	case FBSD_LUSB_SET_ALTINTERFACE:
		args->cmd = USB_SET_ALTINTERFACE;
		break;
	case FBSD_LUSB_SET_CONFIG:
		args->cmd = USB_SET_CONFIG;
		break;
	case FBSD_LUSB_SET_IMMED:
		args->cmd = USB_SET_IMMED;
		break;
	case FBSD_LUSB_SET_POWER_MODE:
		args->cmd = USB_SET_POWER_MODE;
		break;
	case FBSD_LUSB_SET_TEMPLATE:
		args->cmd = USB_SET_TEMPLATE;
		break;
	case FBSD_LUSB_FS_OPEN_STREAM:
		args->cmd = USB_FS_OPEN_STREAM;
		break;
	case FBSD_LUSB_GET_DEV_PORT_PATH:
		args->cmd = USB_GET_DEV_PORT_PATH;
		break;
	case FBSD_LUSB_GET_POWER_USAGE:
		args->cmd = USB_GET_POWER_USAGE;
		break;
	default:
		error = ENOIOCTL;
	}
	if (error != ENOIOCTL)
		error = sys_ioctl(td, (struct ioctl_args *)args);
	return (error);
}

/*
 * Some evdev ioctls must be translated.
 *  - EVIOCGMTSLOTS is a IOC_READ ioctl on Linux although it has input data
 *    (must be IOC_INOUT on FreeBSD).
 *  - On Linux, EVIOCGRAB, EVIOCREVOKE and EVIOCRMFF are defined as _IOW with
 *    an int argument. You don't pass an int pointer to the ioctl(), however,
 *    but just the int directly. On FreeBSD, they are defined as _IOWINT for
 *    this to work.
 */
static int
linux_ioctl_evdev(struct thread *td, struct linux_ioctl_args *args)
{
	struct file *fp;
	clockid_t clock;
	int error;

	args->cmd = SETDIR(args->cmd);

	switch (args->cmd) {
	case (EVIOCGRAB & ~IOC_DIRMASK) | IOC_IN:
		args->cmd = EVIOCGRAB;
		break;
	case (EVIOCREVOKE & ~IOC_DIRMASK) | IOC_IN:
		args->cmd = EVIOCREVOKE;
		break;
	case (EVIOCRMFF & ~IOC_DIRMASK) | IOC_IN:
		args->cmd = EVIOCRMFF;
		break;
	case EVIOCSCLOCKID: {
		error = copyin(PTRIN(args->arg), &clock, sizeof(clock));
		if (error != 0)
			return (error);
		if (clock & ~(LINUX_IOCTL_EVDEV_CLK))
			return (EINVAL);
		error = linux_to_native_clockid(&clock, clock);
		if (error != 0)
			return (error);

		error = fget(td, args->fd,
		    &cap_ioctl_rights, &fp);
		if (error != 0)
			return (error);

		error = fo_ioctl(fp, EVIOCSCLOCKID, &clock, td->td_ucred, td);
		fdrop(fp, td);
		return (error);
	}
	default:
		break;
	}

	if (IOCBASECMD(args->cmd) ==
	    ((EVIOCGMTSLOTS(0) & ~IOC_DIRMASK) | IOC_OUT))
		args->cmd = (args->cmd & ~IOC_DIRMASK) | IOC_INOUT;

	return (sys_ioctl(td, (struct ioctl_args *)args));
}

/*
 * main ioctl syscall function
 */

int
linux_ioctl(struct thread *td, struct linux_ioctl_args *args)
{
	struct file *fp;
	struct linux_ioctl_handler_element *he;
	int error, cmd;

#ifdef DEBUG
	if (ldebug(ioctl))
		printf(ARGS(ioctl, "%d, %04lx, *"), args->fd,
		    (unsigned long)args->cmd);
#endif

	error = fget(td, args->fd, &cap_ioctl_rights, &fp);
	if (error != 0)
		return (error);
	if ((fp->f_flag & (FREAD|FWRITE)) == 0) {
		fdrop(fp, td);
		return (EBADF);
	}

	/* Iterate over the ioctl handlers */
	cmd = args->cmd & 0xffff;
	sx_slock(&linux_ioctl_sx);
	mtx_lock(&Giant);
#ifdef COMPAT_LINUX32
	TAILQ_FOREACH(he, &linux32_ioctl_handlers, list) {
		if (cmd >= he->low && cmd <= he->high) {
			error = (*he->func)(td, args);
			if (error != ENOIOCTL) {
				mtx_unlock(&Giant);
				sx_sunlock(&linux_ioctl_sx);
				fdrop(fp, td);
				return (error);
			}
		}
	}
#endif
	TAILQ_FOREACH(he, &linux_ioctl_handlers, list) {
		if (cmd >= he->low && cmd <= he->high) {
			error = (*he->func)(td, args);
			if (error != ENOIOCTL) {
				mtx_unlock(&Giant);
				sx_sunlock(&linux_ioctl_sx);
				fdrop(fp, td);
				return (error);
			}
		}
	}
	mtx_unlock(&Giant);
	sx_sunlock(&linux_ioctl_sx);
	fdrop(fp, td);

	switch (args->cmd & 0xffff) {
	case LINUX_BTRFS_IOC_CLONE:
		return (ENOTSUP);

	default:
		linux_msg(td, "ioctl fd=%d, cmd=0x%x ('%c',%d) is not implemented",
		    args->fd, (int)(args->cmd & 0xffff),
		    (int)(args->cmd & 0xff00) >> 8, (int)(args->cmd & 0xff));
		break;
	}

	return (EINVAL);
}

int
linux_ioctl_register_handler(struct linux_ioctl_handler *h)
{
	struct linux_ioctl_handler_element *he, *cur;

	if (h == NULL || h->func == NULL)
		return (EINVAL);

	/*
	 * Reuse the element if the handler is already on the list, otherwise
	 * create a new element.
	 */
	sx_xlock(&linux_ioctl_sx);
	TAILQ_FOREACH(he, &linux_ioctl_handlers, list) {
		if (he->func == h->func)
			break;
	}
	if (he == NULL) {
		he = malloc(sizeof(*he),
		    M_LINUX, M_WAITOK);
		he->func = h->func;
	} else
		TAILQ_REMOVE(&linux_ioctl_handlers, he, list);

	/* Initialize range information. */
	he->low = h->low;
	he->high = h->high;
	he->span = h->high - h->low + 1;

	/* Add the element to the list, sorted on span. */
	TAILQ_FOREACH(cur, &linux_ioctl_handlers, list) {
		if (cur->span > he->span) {
			TAILQ_INSERT_BEFORE(cur, he, list);
			sx_xunlock(&linux_ioctl_sx);
			return (0);
		}
	}
	TAILQ_INSERT_TAIL(&linux_ioctl_handlers, he, list);
	sx_xunlock(&linux_ioctl_sx);

	return (0);
}

int
linux_ioctl_unregister_handler(struct linux_ioctl_handler *h)
{
	struct linux_ioctl_handler_element *he;

	if (h == NULL || h->func == NULL)
		return (EINVAL);

	sx_xlock(&linux_ioctl_sx);
	TAILQ_FOREACH(he, &linux_ioctl_handlers, list) {
		if (he->func == h->func) {
			TAILQ_REMOVE(&linux_ioctl_handlers, he, list);
			sx_xunlock(&linux_ioctl_sx);
			free(he, M_LINUX);
			return (0);
		}
	}
	sx_xunlock(&linux_ioctl_sx);

	return (EINVAL);
}

#ifdef COMPAT_LINUX32
int
linux32_ioctl_register_handler(struct linux_ioctl_handler *h)
{
	struct linux_ioctl_handler_element *he, *cur;

	if (h == NULL || h->func == NULL)
		return (EINVAL);

	/*
	 * Reuse the element if the handler is already on the list, otherwise
	 * create a new element.
	 */
	sx_xlock(&linux_ioctl_sx);
	TAILQ_FOREACH(he, &linux32_ioctl_handlers, list) {
		if (he->func == h->func)
			break;
	}
	if (he == NULL) {
		he = malloc(sizeof(*he), M_LINUX, M_WAITOK);
		he->func = h->func;
	} else
		TAILQ_REMOVE(&linux32_ioctl_handlers, he, list);

	/* Initialize range information. */
	he->low = h->low;
	he->high = h->high;
	he->span = h->high - h->low + 1;

	/* Add the element to the list, sorted on span. */
	TAILQ_FOREACH(cur, &linux32_ioctl_handlers, list) {
		if (cur->span > he->span) {
			TAILQ_INSERT_BEFORE(cur, he, list);
			sx_xunlock(&linux_ioctl_sx);
			return (0);
		}
	}
	TAILQ_INSERT_TAIL(&linux32_ioctl_handlers, he, list);
	sx_xunlock(&linux_ioctl_sx);

	return (0);
}

int
linux32_ioctl_unregister_handler(struct linux_ioctl_handler *h)
{
	struct linux_ioctl_handler_element *he;

	if (h == NULL || h->func == NULL)
		return (EINVAL);

	sx_xlock(&linux_ioctl_sx);
	TAILQ_FOREACH(he, &linux32_ioctl_handlers, list) {
		if (he->func == h->func) {
			TAILQ_REMOVE(&linux32_ioctl_handlers, he, list);
			sx_xunlock(&linux_ioctl_sx);
			free(he, M_LINUX);
			return (0);
		}
	}
	sx_xunlock(&linux_ioctl_sx);

	return (EINVAL);
}
#endif
