/*	$OpenBSD: mpuvar.h,v 1.7 2022/03/21 19:22:40 miod Exp $	*/
/*	$NetBSD: mpu401var.h,v 1.3 1998/11/25 22:17:06 augustss Exp $	*/

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (augustss@netbsd.org).
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

struct mpu_softc {
	bus_space_tag_t iot;		/* tag */
	bus_space_handle_t ioh;		/* handle */
	int	iobase;
	int	open;
	void	(*intr)(void *, int);	/* midi input intr handler */
	void	*arg;			/* arg for intr() */
};

extern const struct midi_hw_if mpu_midi_hw_if;

int	mpu_intr(void *);
int	mpu_find(void *);
int	mpu_open(void *, int,
		 void (*iintr)(void *, int),
		 void (*ointr)(void *), void *arg);
void	mpu_close(void *);
int	mpu_output(void *, int);
void	mpu_getinfo(void *addr, struct midi_info *mi);

#define MPU401_NPORT		2
#define MPU_DATA		0
#define MPU_COMMAND		1
#define  MPU_RESET		0xff
#define  MPU_UART_MODE		0x3f
#define  MPU_ACK		0xfe
#define MPU_STATUS		1
#define  MPU_OUTPUT_BUSY	0x40
#define  MPU_INPUT_EMPTY	0x80

#define MPU_MAXWAIT	10000	/* usec/10 to wait */
