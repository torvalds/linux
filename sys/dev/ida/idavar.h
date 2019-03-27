/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999,2000 Jonathan Lemon
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

/*
 * software structures for the Compaq RAID controller
 */

#ifndef _IDAVAR_H
#define	_IDAVAR_H

#define	ida_inb(ida, port) \
	bus_read_1((ida)->regs, port)
#define	ida_inw(ida, port) \
	bus_read_2((ida)->regs, port)
#define	ida_inl(ida, port) \
	bus_read_4((ida)->regs, port)

#define	ida_outb(ida, port, val) \
	bus_write_1((ida)->regs, port, val)
#define	ida_outw(ida, port, val) \
	bus_write_2((ida)->regs, port, val)
#define	ida_outl(ida, port, val) \
	bus_write_4((ida)->regs, port, val)

struct ida_hdr {
	u_int8_t	drive;		/* logical drive */
	u_int8_t	priority;	/* block priority */
	u_int16_t	size;		/* size of request, in words */
};

struct ida_req {
	u_int16_t	next;		/* offset of next request */
	u_int8_t	command;	/* command */
	u_int8_t	error;		/* return error code */
	u_int32_t	blkno;		/* block number */
	u_int16_t	bcount;		/* block count */
	u_int8_t	sgcount;	/* number of scatter/gather entries */
	u_int8_t	spare;		/* reserved */
};

struct ida_sgb {
	u_int32_t	length;		/* length of S/G segment */
	u_int32_t	addr;		/* physical address of block */
};

#define	IDA_NSEG	32		/* maximum number of segments */

/*
 * right now, this structure totals 276 bytes.
 */
struct ida_hardware_qcb {
	struct 	ida_hdr hdr;			/*   4 */
	struct 	ida_req req;			/*  12 */
	struct 	ida_sgb seg[IDA_NSEG];		/* 256 */
	struct	ida_qcb *qcb;			/*   4 - qcb backpointer */
};

typedef enum {
	QCB_FREE		= 0x0000,
	QCB_ACTIVE		= 0x0001,	/* waiting for completion */
	QCB_TIMEDOUT		= 0x0002,
} qcb_state;

#define	DMA_DATA_IN	0x0001
#define	DMA_DATA_OUT	0x0002
#define	IDA_COMMAND	0x0004
#define	DMA_DATA_TRANSFER	(DMA_DATA_IN | DMA_DATA_OUT)

#define	IDA_QCB_MAX	256
#define	IDA_CONTROLLER	0		/* drive "number" for controller */

struct ida_softc;

struct ida_qcb {
	struct		ida_hardware_qcb *hwqcb;
	struct		ida_softc *ida;
	qcb_state	state;
	short		flags;
	union {
		STAILQ_ENTRY(ida_qcb) stqe;
		SLIST_ENTRY(ida_qcb) sle;
	} link;
	bus_dmamap_t	dmamap;
	bus_addr_t	hwqcb_busaddr;
	struct		bio *buf;		/* bio associated with qcb */
	int		error;
};

struct ida_access {
	int		(*fifo_full)(struct ida_softc *);
	void		(*submit)(struct ida_softc *, struct ida_qcb *);
	bus_addr_t	(*done)(struct ida_softc *);
	int		(*int_pending)(struct ida_softc *);
	void		(*int_enable)(struct ida_softc *, int);
};

/*
 * flags for the controller
 */
#define	IDA_ATTACHED	0x01		/* attached */
#define	IDA_FIRMWARE	0x02		/* firmware must be started */
#define	IDA_INTERRUPTS	0x04		/* interrupts enabled */
#define	IDA_QFROZEN	0x08		/* request queue frozen */

struct ida_softc {
	device_t	dev;

	struct callout	ch;
	struct cdev *ida_dev_t;

	int		regs_res_type;
	int		regs_res_id;
	struct 		resource *regs;

	int		irq_res_type;
	struct		resource *irq;
	void		*ih;

	struct mtx	lock;
	struct intr_config_hook ich;

	/* various DMA tags */
	bus_dma_tag_t	parent_dmat;
	bus_dma_tag_t	buffer_dmat;

	bus_dma_tag_t	hwqcb_dmat;
	bus_dmamap_t	hwqcb_dmamap;
	bus_addr_t	hwqcb_busaddr;

	bus_dma_tag_t	sg_dmat;

	int		flags;

	int		qactive;

	struct		ida_hardware_qcb *hwqcbs;	/* HW QCB array */
	struct		ida_qcb *qcbs;			/* kernel QCB array */
	SLIST_HEAD(, ida_qcb)	free_qcbs;	
	STAILQ_HEAD(, ida_qcb) 	qcb_queue;
	struct		bio_queue_head bio_queue;

	struct		ida_access cmd;
};

/*
 * drive flags
 */
#define	DRV_WRITEPROT		0x0001

struct idad_softc {
	device_t	dev;
	struct 		ida_softc *controller;
	struct		disk *disk;
	int		drive;			/* per controller */
	int		unit;			/* global */
	int		cylinders;
	int		heads;
	int		sectors;
	int		secsize;
	int		secperunit;
	int		flags;
};

struct ida_board {
	u_int32_t	board;
	char 		*desc;
	struct		ida_access *accessor;
	int		flags;
};

extern int ida_detach(device_t dev);
extern struct ida_softc *ida_alloc(device_t dev, struct resource *regs,
	int regs_type, int regs_id, bus_dma_tag_t parent_dmat);
extern void ida_free(struct ida_softc *ida);
extern int ida_setup(struct ida_softc *ida);
extern int ida_command(struct ida_softc *ida, int command, void *data,
	int datasize, int drive, u_int32_t pblkno, int flags);
extern void ida_submit_buf(struct ida_softc *ida, struct bio *bp);
extern void ida_intr(void *data);

extern void idad_intr(struct bio *bp);

#endif /* _IDAVAR_H */
