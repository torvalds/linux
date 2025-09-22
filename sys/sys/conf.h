/*	$OpenBSD: conf.h,v 1.168 2025/09/08 17:25:46 helg Exp $	*/
/*	$NetBSD: conf.h,v 1.33 1996/05/03 20:03:32 christos Exp $	*/

/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)conf.h	8.3 (Berkeley) 1/21/94
 */


#ifndef _SYS_CONF_H_
#define _SYS_CONF_H_

/*
 * Definitions of device driver entry switches
 */

struct buf;
struct proc;
struct tty;
struct uio;
struct knote;

/*
 * Types for d_type
 */
#define	D_DISK	1
#define	D_TTY	2

/*
 * Flags for d_flags
 */
#define D_CLONE		0x0001		/* clone upon open */

#ifdef _KERNEL

#define	dev_type_open(n)	int n(dev_t, int, int, struct proc *)
#define	dev_type_close(n)	int n(dev_t, int, int, struct proc *)
#define	dev_type_strategy(n)	void n(struct buf *)
#define	dev_type_ioctl(n) \
	int n(dev_t, u_long, caddr_t, int, struct proc *)

#define	dev_decl(n,t)	__CONCAT(dev_type_,t)(__CONCAT(n,t))
#define	dev_init(c,n,t) \
	((c) > 0 ? __CONCAT(n,t) : (__CONCAT(dev_type_,t)((*))) enxio)

#endif /* _KERNEL */

/*
 * Block device switch table
 */
struct bdevsw {
	int	(*d_open)(dev_t dev, int oflags, int devtype,
				     struct proc *p);
	int	(*d_close)(dev_t dev, int fflag, int devtype,
				     struct proc *p);
	void	(*d_strategy)(struct buf *bp);
	int	(*d_ioctl)(dev_t dev, u_long cmd, caddr_t data,
				     int fflag, struct proc *p);
	int	(*d_dump)(dev_t dev, daddr_t blkno, caddr_t va,
				    size_t size);
	daddr_t (*d_psize)(dev_t dev);
	u_int	d_type;
	/* u_int	d_flags; */
};

#ifdef _KERNEL

extern struct bdevsw bdevsw[];

/* bdevsw-specific types */
#define	dev_type_dump(n)	int n(dev_t, daddr_t, caddr_t, size_t)
#define	dev_type_size(n)	daddr_t n(dev_t)

/* bdevsw-specific initializations */
#define	dev_size_init(c,n)	(c > 0 ? __CONCAT(n,size) : 0)

#define	bdev_decl(n) \
	dev_decl(n,open); dev_decl(n,close); dev_decl(n,strategy); \
	dev_decl(n,ioctl); dev_decl(n,dump); dev_decl(n,size)

#define	bdev_disk_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), \
	dev_init(c,n,strategy), dev_init(c,n,ioctl), \
	dev_init(c,n,dump), dev_size_init(c,n), D_DISK }

#define	bdev_swap_init(c,n) { \
	(dev_type_open((*))) enodev, (dev_type_close((*))) enodev, \
	dev_init(c,n,strategy), (dev_type_ioctl((*))) enodev, \
	(dev_type_dump((*))) enodev, 0 }

#define	bdev_notdef() { \
	(dev_type_open((*))) enodev, (dev_type_close((*))) enodev, \
	(dev_type_strategy((*))) enodev, (dev_type_ioctl((*))) enodev, \
	(dev_type_dump((*))) enodev, 0 }

#endif

/*
 * Character device switch table
 */
struct cdevsw {
	int	(*d_open)(dev_t dev, int oflags, int devtype,
				     struct proc *p);
	int	(*d_close)(dev_t dev, int fflag, int devtype,
				     struct proc *);
	int	(*d_read)(dev_t dev, struct uio *uio, int ioflag);
	int	(*d_write)(dev_t dev, struct uio *uio, int ioflag);
	int	(*d_ioctl)(dev_t dev, u_long cmd, caddr_t data,
				     int fflag, struct proc *p);
	int	(*d_stop)(struct tty *tp, int rw);
	struct tty *
		(*d_tty)(dev_t dev);
	paddr_t	(*d_mmap)(dev_t, off_t, int);
	u_int	d_type;
	u_int	d_flags;
	int	(*d_kqfilter)(dev_t dev, struct knote *kn);
};

#ifdef _KERNEL

extern struct cdevsw cdevsw[];

/* cdevsw-specific types */
#define	dev_type_read(n)	int n(dev_t, struct uio *, int)
#define	dev_type_write(n)	int n(dev_t, struct uio *, int)
#define	dev_type_stop(n)	int n(struct tty *, int)
#define	dev_type_tty(n)		struct tty *n(dev_t)
#define	dev_type_mmap(n)	paddr_t n(dev_t, off_t, int)
#define dev_type_kqfilter(n)	int n(dev_t, struct knote *)

#define	cdev_decl(n) \
	dev_decl(n,open); dev_decl(n,close); dev_decl(n,read); \
	dev_decl(n,write); dev_decl(n,ioctl); dev_decl(n,stop); \
	dev_decl(n,tty); dev_decl(n,mmap); \
	dev_decl(n,kqfilter)

/* open, close, read, write, ioctl */
#define	cdev_disk_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	dev_init(c,n,write), dev_init(c,n,ioctl), (dev_type_stop((*))) enodev, \
	0, (dev_type_mmap((*))) enodev, \
	D_DISK, 0, seltrue_kqfilter }

/* open, close, read, write, ioctl */
#define	cdev_tape_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	dev_init(c,n,write), dev_init(c,n,ioctl), (dev_type_stop((*))) enodev, \
	0, (dev_type_mmap((*))) enodev, \
	0, 0, seltrue_kqfilter }

/* open, close, read, write, ioctl, stop, tty */
#define	cdev_tty_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	dev_init(c,n,write), dev_init(c,n,ioctl), dev_init(c,n,stop), \
	dev_init(c,n,tty), (dev_type_mmap((*))) enodev, \
	D_TTY, 0, ttkqfilter }

/* open, close, read, ioctl, kqfilter */
#define	cdev_mouse_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev , 0, 0, dev_init(c,n,kqfilter) }

#define	cdev_notdef() { \
	(dev_type_open((*))) enodev, (dev_type_close((*))) enodev, \
	(dev_type_read((*))) enodev, (dev_type_write((*))) enodev, \
	(dev_type_ioctl((*))) enodev, (dev_type_stop((*))) enodev, \
	0, (dev_type_mmap((*))) enodev, 0, 0, seltrue_kqfilter }

/* open, close, read, write, ioctl, kqfilter -- XXX should be a tty */
#define	cdev_cn_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	dev_init(c,n,write), dev_init(c,n,ioctl), dev_init(c,n,stop), \
	0, (dev_type_mmap((*))) enodev, \
	D_TTY, 0, dev_init(c,n,kqfilter) }

/* open, read, write, ioctl, kqfilter -- XXX should be a tty */
#define cdev_ctty_init(c,n) { \
	dev_init(c,n,open), (dev_type_close((*))) nullop, dev_init(c,n,read), \
	dev_init(c,n,write), dev_init(c,n,ioctl), (dev_type_stop((*))) nullop, \
	0, (dev_type_mmap((*))) enodev, \
	D_TTY, 0, dev_init(c,n,kqfilter) }

/* open, close, read, write, ioctl, mmap */
#define cdev_mm_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	dev_init(c,n,write), dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, dev_init(c,n,mmap), \
	0, 0, seltrue_kqfilter }

/* open, close, read, write, ioctl, tty, kqfilter */
#define cdev_ptc_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	dev_init(c,n,write), dev_init(c,n,ioctl), (dev_type_stop((*))) nullop, \
	dev_init(c,n,tty), (dev_type_mmap((*))) enodev, \
	D_TTY, 0, dev_init(c,n,kqfilter) }

/* open, close, read, write, ioctl, mmap */
#define cdev_ptm_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, (dev_type_mmap((*))) enodev }

/* open, close, read, ioctl, kqfilter XXX should be a generic device */
#define cdev_log_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev, 0, 0, dev_init(c,n,kqfilter) }

/* open */
#define cdev_fd_init(c,n) { \
	dev_init(c,n,open), (dev_type_close((*))) enodev, \
	(dev_type_read((*))) enodev, (dev_type_write((*))) enodev, \
	(dev_type_ioctl((*))) enodev, (dev_type_stop((*))) enodev, \
	0, (dev_type_mmap((*))) enodev }

/* open, close, read, write, ioctl, kqfilter -- XXX should be generic device */
#define cdev_tun_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	dev_init(c,n,write), dev_init(c,n,ioctl), (dev_type_stop((*))) enodev, \
	0, (dev_type_mmap((*))) enodev, \
	0, 0, dev_init(c,n,kqfilter) }

/* open, close, ioctl, kqfilter -- XXX should be generic device */
#define cdev_vscsi_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), \
	(dev_type_read((*))) enodev, (dev_type_write((*))) enodev, \
	dev_init(c,n,ioctl), (dev_type_stop((*))) enodev, \
	0, (dev_type_mmap((*))) enodev, \
	0, 0, dev_init(c,n,kqfilter) }

/* open, close, read, write, ioctl, kqfilter -- XXX should be generic device */
#define cdev_pppx_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	dev_init(c,n,write), dev_init(c,n,ioctl), (dev_type_stop((*))) enodev, \
	0, (dev_type_mmap((*))) enodev, \
	0, 0, dev_init(c,n,kqfilter) }

/* open, close, read, write, ioctl, kqfilter, cloning -- XXX should be generic device */
#define cdev_bpf_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	dev_init(c,n,write), dev_init(c,n,ioctl), (dev_type_stop((*))) enodev, \
	0, (dev_type_mmap((*))) enodev, \
	0, D_CLONE, dev_init(c,n,kqfilter) }

/* open, close, ioctl */
#define	cdev_ch_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev }

/* open, close, ioctl */
#define       cdev_uk_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev }

/* open, close, read, write, ioctl, kqfilter */
#define cdev_audio_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	dev_init(c,n,write), dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev, 0, 0, dev_init(c,n,kqfilter) }

/* open, close, read, write, ioctl, kqfilter */
#define cdev_midi_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	dev_init(c,n,write), dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev, 0, 0, dev_init(c,n,kqfilter) }

/* open, close, read */
#define cdev_ksyms_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	(dev_type_write((*))) enodev, (dev_type_ioctl((*))) enodev, \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev, 0, 0, seltrue_kqfilter }

/* open, close, ioctl */
#define cdev_kstat_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev }

/* open, close, read, write, ioctl, stop, tty, mmap, kqfilter */
#define	cdev_wsdisplay_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	dev_init(c,n,write), dev_init(c,n,ioctl), dev_init(c,n,stop), \
	dev_init(c,n,tty), dev_init(c,n,mmap), \
	D_TTY, 0, dev_init(c,n,kqfilter) }

/* open, close, read, write, ioctl, kqfilter */
#define	cdev_random_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	dev_init(c,n,write), dev_init(c,n,ioctl), (dev_type_stop((*))) enodev, \
	0, (dev_type_mmap((*))) enodev, \
	0, 0, dev_init(c,n,kqfilter) }

/* open, close, ioctl, nokqfilter */
#define	cdev_usb_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev }

/* open, close, write */
#define cdev_ulpt_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	dev_init(c,n,write), (dev_type_ioctl((*))) enodev, \
	(dev_type_stop((*))) enodev, 0, (dev_type_mmap((*))) enodev }

/* open, close, ioctl */
#define cdev_pf_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev, 0, D_CLONE }

/* open, close, read, write, ioctl, kqfilter */
#define	cdev_usbdev_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	dev_init(c,n,write), dev_init(c,n,ioctl), (dev_type_stop((*))) enodev, \
	0, (dev_type_mmap((*))) enodev, 0, 0, \
	dev_init(c,n,kqfilter) }

/* open, close, read, write, ioctl, kqfilter */
#define	cdev_fido_init(c,n) { \
	dev_init(c,n,open), dev_init(c,uhid,close), dev_init(c,uhid,read), \
	dev_init(c,uhid,write), dev_init(c,fido,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev, 0, 0, dev_init(c,uhid,kqfilter) }

/* open, close, read, write, ioctl, kqfilter */
#define	cdev_ujoy_init(c,n) { \
	dev_init(c,n,open), dev_init(c,uhid,close), dev_init(c,uhid,read), \
	dev_init(c,uhid,write), dev_init(c,ujoy,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev, 0, 0, dev_init(c,uhid,kqfilter) }

/* open, close, init */
#define cdev_pci_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev }

/* open, close, ioctl */
#define cdev_radio_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev }

/* open, close, ioctl, read, mmap, kqfilter */
#define cdev_video_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	dev_init(c,n,mmap), 0, 0, dev_init(c,n,kqfilter) }

/* open, close, write, ioctl */
#define cdev_spkr_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	dev_init(c,n,write), dev_init(c,n,ioctl), (dev_type_stop((*))) enodev, \
	0, (dev_type_mmap((*))) enodev, \
	0, 0, seltrue_kqfilter }

/* open, close, write */
#define cdev_lpt_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	dev_init(c,n,write), (dev_type_ioctl((*))) enodev, \
	(dev_type_stop((*))) enodev, 0, (dev_type_mmap((*))) enodev, \
	0, 0, seltrue_kqfilter }

/* open, close, read, ioctl, mmap */
#define cdev_bktr_init(c, n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, dev_init(c,n,mmap), \
	0, 0, seltrue_kqfilter }

/* open, close, read, ioctl, kqfilter */
#define cdev_hotplug_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev, 0, 0, dev_init(c,n,kqfilter) }

/* open, close, ioctl */
#define cdev_gpio_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev }

/* open, close, ioctl */
#define       cdev_bio_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev }

/* open, close, read, ioctl, mmap, nokqfilter */
#define      cdev_drm_init(c,n)        { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c, n, read), \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	dev_init(c,n,mmap), 0, D_CLONE, dev_init(c,n,kqfilter) }

/* open, close, ioctl */
#define cdev_amdmsr_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev }

/* open, close, read, write, ioctl */
#define cdev_fuse_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	dev_init(c,n,write), (dev_type_ioctl((*))) enodev, \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev, 0, D_CLONE, dev_init(c,n,kqfilter) }

/* open, close, ioctl */
#define cdev_pvbus_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), \
	(dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, \
	 dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev }

/* open, close, ioctl */
#define cdev_ipmi_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, (dev_type_mmap((*))) enodev, \
	0, 0, seltrue_kqfilter }

/* open, close, ioctl */
#define cdev_efi_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev }

/* open, close, ioctl, mmap */
#define cdev_kcov_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_init(c,n,mmap)), 0, D_CLONE }

/* open, close, read, ioctl */
#define cdev_dt_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	(dev_type_write((*))) enodev, dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, \
	(dev_type_mmap((*))) enodev, 0, D_CLONE }

#endif

/*
 * Line discipline switch table
 */
struct linesw {
	int	(*l_open)(dev_t dev, struct tty *tp, struct proc *p);
	int	(*l_close)(struct tty *tp, int flags, struct proc *p);
	int	(*l_read)(struct tty *tp, struct uio *uio,
				     int flag);
	int	(*l_write)(struct tty *tp, struct uio *uio,
				     int flag);
	int	(*l_ioctl)(struct tty *tp, u_long cmd, caddr_t data,
				     int flag, struct proc *p);
	int	(*l_rint)(int c, struct tty *tp);
	int	(*l_start)(struct tty *tp);
	int	(*l_modem)(struct tty *tp, int flag);
};

#ifdef _KERNEL
extern struct linesw linesw[];
extern dev_t swdevt[];		/* Swap device table */
extern const int chrtoblktbl[];
extern const int nchrtoblktbl;

struct bdevsw *bdevsw_lookup(dev_t);
dev_t	chrtoblk(dev_t);
dev_t	blktochr(dev_t);
int	iskmemdev(dev_t);
int	iszerodev(dev_t);
dev_t	getnulldev(void);

cdev_decl(filedesc);

cdev_decl(log);

#define	ptstty		ptytty
#define	ptsioctl	ptyioctl
cdev_decl(pts);

#define	ptctty		ptytty
#define	ptcioctl	ptyioctl
cdev_decl(ptc);

cdev_decl(ptm);

cdev_decl(ctty);

cdev_decl(audio);
cdev_decl(drm);
cdev_decl(midi);
cdev_decl(radio);
cdev_decl(video);
cdev_decl(cn);

bdev_decl(sw);

bdev_decl(vnd);
cdev_decl(vnd);

cdev_decl(ch);

bdev_decl(sd);
cdev_decl(sd);

cdev_decl(st);

bdev_decl(cd);
cdev_decl(cd);

bdev_decl(rd);
cdev_decl(rd);

bdev_decl(uk);
cdev_decl(uk);

cdev_decl(dt);

cdev_decl(diskmap);

cdev_decl(bpf);

cdev_decl(pf);

cdev_decl(tun);
cdev_decl(tap);
cdev_decl(pppx);
cdev_decl(pppac);

cdev_decl(random);

cdev_decl(wsdisplay);
cdev_decl(wskbd);
cdev_decl(wsmouse);
cdev_decl(wsmux);

cdev_decl(ksyms);
cdev_decl(kstat);

cdev_decl(bio);
cdev_decl(vscsi);

cdev_decl(bktr);

cdev_decl(usb);
cdev_decl(ugen);
cdev_decl(uhid);
cdev_decl(fido);
cdev_decl(ujoy);
cdev_decl(ucom);
cdev_decl(ulpt);

cdev_decl(hotplug);
cdev_decl(gpio);
cdev_decl(amdmsr);
cdev_decl(fuse);
cdev_decl(pvbus);
cdev_decl(ipmi);
cdev_decl(efi);
cdev_decl(kcov);

#endif

#endif /* _SYS_CONF_H_ */
