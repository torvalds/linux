/*-
 * Copyright (c) 2017 Ian Lepore <ian@freebsd.org>
 * All rights reserved.
 *
 * Development sponsored by Microsemi, Inc.
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
 * Helper code to recover a hung i2c bus by bit-banging a recovery sequence.
 */

#ifndef _IICBUS_IIC_RECOVER_BUS_H_
#define	_IICBUS_IIC_RECOVER_BUS_H_

struct iicrb_pin_access {
	void   *ctx;
	int   (*getsda)(void *ctx);
	void  (*setsda)(void *ctx, int value);
	int   (*getscl)(void *ctx);
	void  (*setscl)(void *ctx, int value);
};

/*
 * Drive the bus-recovery logic by manipulating the line states using the
 * caller-provided functions.  This does not block or sleep or acquire any locks
 * (unless the provided pin access functions do so).  It uses DELAY() to pace
 * bits on the bus.
 *
 * Returns 0 if the bus is functioning properly or IIC_EBUSERR if the recovery
 * attempt failed and some slave device is still driving the bus.
 */
int iic_recover_bus(struct iicrb_pin_access *pins);

#endif
