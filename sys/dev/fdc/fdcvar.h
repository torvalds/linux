/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2005 M. Warner Losh.
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
 *
 * $FreeBSD$
 */

/* XXX should audit this file to see if additional copyrights needed */

enum fdc_type {
	FDC_NE765, FDC_ENHANCED, FDC_UNKNOWN = -1
};

/*
 * Per controller structure (softc).
 */
struct fdc_data {
	int	fdcu;		/* our unit number */
	int	dmachan;
	int	flags;
#define FDC_HASDMA	0x01
#define FDC_STAT_VALID	0x08
#define FDC_HAS_FIFO	0x10
#define FDC_NEEDS_RESET	0x20
#define FDC_NODMA	0x40	/* Don't do DMA */
#define FDC_NOFAST	0x80	/* Don't register isr as a fast one */
#define FDC_KTHREAD_EXIT	0x1000 /* request worker thread to stop */
#define FDC_KTHREAD_ALIVE	0x2000 /* worker thread is alive */
	struct	fd_data *fd;	/* The active drive */
	int	retry;
	int	fdout;		/* mirror of the w/o digital output reg */
	u_int	status[7];	/* copy of the registers */
	enum	fdc_type fdct;	/* chip version of FDC */
	int	fdc_errs;	/* number of logged errors */
	struct	bio_queue_head head;
	struct	bio *bp;	/* active buffer */
	struct	resource *res_irq, *res_drq;
	int	rid_irq, rid_drq;
#define FDC_MAXREG	8
	int	ridio[FDC_MAXREG];
	struct	resource *resio[FDC_MAXREG];
	bus_space_tag_t iot;
	bus_space_handle_t ioh[FDC_MAXREG];
	int	ioff[FDC_MAXREG];
	void	*fdc_intr;
	device_t fdc_dev;
	struct mtx fdc_mtx;
	struct proc *fdc_thread;
};

extern devclass_t fdc_devclass;

enum fdc_device_ivars {
	FDC_IVAR_FDUNIT,
	FDC_IVAR_FDTYPE,
};

__BUS_ACCESSOR(fdc, fdunit, FDC, FDUNIT, int);
__BUS_ACCESSOR(fdc, fdtype, FDC, FDTYPE, int);

void fdc_release_resources(struct fdc_data *);
int fdc_attach(device_t);
void fdc_start_worker(device_t);
int fdc_hints_probe(device_t);
int fdc_detach(device_t dev);
device_t fdc_add_child(device_t, const char *, int);
int fdc_initial_reset(device_t, struct fdc_data *);
int fdc_print_child(device_t, device_t);
int fdc_read_ivar(device_t, device_t, int, uintptr_t *);
int fdc_write_ivar(device_t, device_t, int, uintptr_t);
int fdc_isa_alloc_resources(device_t, struct fdc_data *);
