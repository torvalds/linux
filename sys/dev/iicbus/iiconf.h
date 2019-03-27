/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998, 2001 Nicolas Souchu
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
#ifndef __IICONF_H
#define __IICONF_H

#include <sys/queue.h>
#include <dev/iicbus/iic.h>


#define IICPRI (PZERO+8)		/* XXX sleep/wakeup queue priority */

#define LSB 0x1

/*
 * Options affecting iicbus_request_bus()
 */
#define IIC_DONTWAIT	0
#define IIC_NOINTR	0
#define IIC_WAIT	0x1
#define IIC_INTR	0x2
#define IIC_INTRWAIT	(IIC_INTR | IIC_WAIT)
#define IIC_RECURSIVE	0x4

/*
 * i2c modes
 */
#define IIC_MASTER	0x1
#define IIC_SLAVE	0x2
#define IIC_POLLED	0x4

/*
 * i2c speed
 */
#define IIC_UNKNOWN	0x0
#define IIC_SLOW	0x1
#define IIC_FAST	0x2
#define IIC_FASTEST	0x3

#define IIC_LAST_READ	0x1

/*
 * callback index
 */
#define IIC_REQUEST_BUS	0x1
#define IIC_RELEASE_BUS	0x2

/*
 * interrupt events
 */
#define INTR_GENERAL	0x1	/* general call received */
#define INTR_START	0x2	/* the I2C interface is addressed */
#define INTR_STOP	0x3	/* stop condition received */
#define INTR_RECEIVE	0x4	/* character received */
#define INTR_TRANSMIT	0x5	/* character to transmit */
#define INTR_ERROR	0x6	/* error */
#define INTR_NOACK	0x7	/* no ack from master receiver */

/*
 * adapter layer errors
 */
#define	IIC_NOERR	0x0	/* no error occurred */
#define IIC_EBUSERR	0x1	/* bus error (hardware not in expected state) */
#define IIC_ENOACK	0x2	/* ack not received until timeout */
#define IIC_ETIMEOUT	0x3	/* timeout */
#define IIC_EBUSBSY	0x4	/* bus busy (reserved by another client) */
#define IIC_ESTATUS	0x5	/* status error */
#define IIC_EUNDERFLOW	0x6	/* slave ready for more data */
#define IIC_EOVERFLOW	0x7	/* too much data */
#define IIC_ENOTSUPP	0x8	/* request not supported */
#define IIC_ENOADDR	0x9	/* no address assigned to the interface */
#define IIC_ERESOURCE	0xa	/* resources (memory, whatever) unavailable */

/*
 * Note that all iicbus functions return IIC_Exxxxx status values,
 * except iic2errno() (obviously) and iicbus_started() (returns bool).
 */
extern int iic2errno(int);
extern int iicbus_request_bus(device_t, device_t, int);
extern int iicbus_release_bus(device_t, device_t);
extern device_t iicbus_alloc_bus(device_t);

extern void iicbus_intr(device_t, int, char *);

extern int iicbus_null_repeated_start(device_t, u_char);
extern int iicbus_null_callback(device_t, int, caddr_t);

#define iicbus_reset(bus,speed,addr,oldaddr) \
	(IICBUS_RESET(device_get_parent(bus), speed, addr, oldaddr))

/* basic I2C operations */
extern int iicbus_started(device_t);
extern int iicbus_start(device_t, u_char, int);
extern int iicbus_stop(device_t);
extern int iicbus_repeated_start(device_t, u_char, int);
extern int iicbus_write(device_t, const char *, int, int *, int);
extern int iicbus_read(device_t, char *, int, int *, int, int);

/* single byte read/write functions, start/stop not managed */
extern int iicbus_write_byte(device_t, char, int);
extern int iicbus_read_byte(device_t, char *, int);

/* Read/write operations with start/stop conditions managed */
extern int iicbus_block_write(device_t, u_char, char *, int, int *);
extern int iicbus_block_read(device_t, u_char, char *, int, int *);

/* vectors of iic operations to pass to bridge */
int iicbus_transfer(device_t bus, struct iic_msg *msgs, uint32_t nmsgs);
int iicbus_transfer_excl(device_t bus, struct iic_msg *msgs, uint32_t nmsgs,
    int how);
int iicbus_transfer_gen(device_t bus, struct iic_msg *msgs, uint32_t nmsgs);

/*
 * Simple register read/write routines, but the "register" can be any size.
 * The transfers are done with iicbus_transfer_excl().  Reads use a repeat-start
 * between sending the address and reading; writes use a single start/stop.
 */
int iicdev_readfrom(device_t _slavedev, uint8_t _regaddr, void *_buffer,
    uint16_t _buflen, int _waithow);
int iicdev_writeto(device_t _slavedev, uint8_t _regaddr, void *_buffer,
    uint16_t _buflen, int _waithow);

#define IICBUS_MODVER	1
#define IICBUS_MINVER	1
#define IICBUS_MAXVER	1
#define IICBUS_PREFVER	IICBUS_MODVER

extern driver_t iicbb_driver;
extern devclass_t iicbb_devclass;

#define IICBB_MODVER	1
#define IICBB_MINVER	1
#define IICBB_MAXVER	1
#define IICBB_PREFVER	IICBB_MODVER

#endif
