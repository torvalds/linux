/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
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

#ifndef _USB_DEV_H_
#define	_USB_DEV_H_

#ifndef USB_GLOBAL_INCLUDE_FILE
#include <sys/file.h>
#include <sys/selinfo.h>
#include <sys/poll.h>
#include <sys/signalvar.h>
#include <sys/proc.h>
#endif

struct usb_fifo;
struct usb_mbuf;

struct usb_symlink {
	TAILQ_ENTRY(usb_symlink) sym_entry;
	char	src_path[32];		/* Source path - including terminating
					 * zero */
	char	dst_path[32];		/* Destination path - including
					 * terminating zero */
	uint8_t	src_len;		/* String length */
	uint8_t	dst_len;		/* String length */
};

/*
 * Private per-device information.
 */
struct usb_cdev_privdata {
	struct usb_bus		*bus;
	struct usb_device	*udev;
	struct usb_interface	*iface;
	int			bus_index;	/* bus index */
	int			dev_index;	/* device index */
	int			ep_addr;	/* endpoint address */
	int			fflags;
	uint8_t			fifo_index;	/* FIFO index */
};

/*
 * The following structure defines a minimum re-implementation of the
 * ifqueue structure in the kernel.
 */
struct usb_ifqueue {
	struct usb_mbuf *ifq_head;
	struct usb_mbuf *ifq_tail;

	usb_size_t ifq_len;
	usb_size_t ifq_maxlen;
};

/*
 * Private per-device and per-thread reference information
 */
struct usb_cdev_refdata {
	struct usb_fifo		*rxfifo;
	struct usb_fifo		*txfifo;
	uint8_t			is_read;	/* location has read access */
	uint8_t			is_write;	/* location has write access */
	uint8_t			is_uref;	/* USB refcount decr. needed */
	uint8_t			is_usbfs;	/* USB-FS is active */
	uint8_t			do_unlock;	/* USB enum unlock needed */
};

struct usb_fs_privdata {
	int bus_index;
	int dev_index;
	int ep_addr;
	int mode;
	int fifo_index;
	struct cdev *cdev;

	LIST_ENTRY(usb_fs_privdata) pd_next;
};

/*
 * Most of the fields in the "usb_fifo" structure are used by the
 * generic USB access layer.
 */
struct usb_fifo {
	struct usb_ifqueue free_q;
	struct usb_ifqueue used_q;
	struct selinfo selinfo;
	struct cv cv_io;
	struct cv cv_drain;
	struct usb_fifo_methods *methods;
	struct usb_symlink *symlink[2];/* our symlinks */
	struct proc *async_p;		/* process that wants SIGIO */
	struct usb_fs_endpoint *fs_ep_ptr;
	struct usb_device *udev;
	struct usb_xfer *xfer[2];
	struct usb_xfer **fs_xfer;
	struct mtx *priv_mtx;		/* client data */
	/* set if FIFO is opened by a FILE: */
	struct usb_cdev_privdata *curr_cpd;
	void   *priv_sc0;		/* client data */
	void   *priv_sc1;		/* client data */
	void   *queue_data;
	usb_timeout_t timeout;		/* timeout in milliseconds */
	usb_frlength_t bufsize;		/* BULK and INTERRUPT buffer size */
	usb_frcount_t nframes;		/* for isochronous mode */
	uint16_t dev_ep_index;		/* our device endpoint index */
	uint8_t	flag_sleeping;		/* set if FIFO is sleeping */
	uint8_t	flag_iscomplete;	/* set if a USB transfer is complete */
	uint8_t	flag_iserror;		/* set if FIFO error happened */
	uint8_t	flag_isselect;		/* set if FIFO is selected */
	uint8_t	flag_flushing;		/* set if FIFO is flushing data */
	uint8_t	flag_short;		/* set if short_ok or force_short
					 * transfer flags should be set */
	uint8_t	flag_stall;		/* set if clear stall should be run */
	uint8_t	flag_write_defrag;	/* set to defrag written data */
	uint8_t	flag_have_fragment;	/* set if defragging */
	uint8_t	iface_index;		/* set to the interface we belong to */
	uint8_t	fifo_index;		/* set to the FIFO index in "struct
					 * usb_device" */
	uint8_t	fs_ep_max;
	uint8_t	fifo_zlp;		/* zero length packet count */
	uint8_t	refcount;
#define	USB_FIFO_REF_MAX 0xFF
};

extern struct cdevsw usb_devsw;

int	usb_fifo_wait(struct usb_fifo *fifo);
void	usb_fifo_signal(struct usb_fifo *fifo);
uint8_t	usb_fifo_opened(struct usb_fifo *fifo);
struct usb_symlink *usb_alloc_symlink(const char *target);
void	usb_free_symlink(struct usb_symlink *ps);
int	usb_read_symlink(uint8_t *user_ptr, uint32_t startentry,
	    uint32_t user_len);

#endif					/* _USB_DEV_H_ */
