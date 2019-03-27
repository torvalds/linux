/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Nicolas Souchu, Marc Bouget
 * Copyright (c) 2004 Joerg Wunsch
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

#ifndef __PCFVAR_H__
#define	__PCFVAR_H__

#define IO_PCFSIZE	2

#define TIMEOUT	9999					/* XXX */

/* Status bits of S1 register (read only) */
#define nBB	0x01		/* busy when low set/reset by STOP/START*/
#define LAB	0x02		/* lost arbitration bit in multi-master mode */
#define AAS	0x04		/* addressed as slave */
#define LRB	0x08		/* last received byte when not AAS */
#define AD0	0x08		/* general call received when AAS */
#define BER	0x10		/* bus error, misplaced START or STOP */
#define STS	0x20		/* STOP detected in slave receiver mode */
#define PIN	0x80		/* pending interrupt not (r/w) */

/* Control bits of S1 register (write only) */
#define ACK	0x01
#define STO	0x02
#define STA	0x04
#define ENI	0x08
#define ES2	0x10
#define ES1	0x20
#define ESO	0x40

#define BUFSIZE 2048

#define SLAVE_TRANSMITTER	0x1
#define SLAVE_RECEIVER		0x2

#define PCF_DEFAULT_ADDR	0xaa

struct pcf_softc {
	u_char	pcf_addr;		/* interface I2C address */
	int	pcf_flags;		/* IIC_POLLED? */
	int	pcf_slave_mode;		/* receiver or transmitter */
	int	pcf_started;		/* 1 if start condition sent */

	struct mtx pcf_lock;
	device_t iicbus;		/* the corresponding iicbus */

	/* Resource handling stuff. */
	struct resource		*res_ioport;
	int			rid_ioport;
	struct resource		*res_irq;
	int			rid_irq;
	void			*intr_cookie;
};
#define DEVTOSOFTC(dev) ((struct pcf_softc *)device_get_softc(dev))

#define	PCF_LOCK(sc)		mtx_lock(&(sc)->pcf_lock)
#define	PCF_UNLOCK(sc)		mtx_unlock(&(sc)->pcf_lock)
#define	PCF_ASSERT_LOCKED(sc)	mtx_assert(&(sc)->pcf_lock, MA_OWNED)

/*
 * PCF8584 datasheet : when operate at 8 MHz or more, a minimun time of
 * 6 clocks cycles must be left between two consecutives access
 */
#define pcf_nops()	DELAY(10)

#define dummy_read(sc)	pcf_get_S0(sc)
#define dummy_write(sc)	pcf_set_S0(sc, 0)

/*
 * Specific register access to PCF8584
 */
static __inline void
pcf_set_S0(struct pcf_softc *sc, int data)
{

	bus_write_1(sc->res_ioport, 0, data);
	pcf_nops();
}

static __inline void
pcf_set_S1(struct pcf_softc *sc, int data)
{

	bus_write_1(sc->res_ioport, 1, data);
	pcf_nops();
}

static __inline char
pcf_get_S0(struct pcf_softc *sc)
{
	char data;

	data = bus_read_1(sc->res_ioport, 0);
	pcf_nops();

	return (data);
}

static __inline char
pcf_get_S1(struct pcf_softc *sc)
{
	char data;

	data = bus_read_1(sc->res_ioport, 1);
	pcf_nops();

	return (data);
}

extern int pcf_repeated_start(device_t, u_char, int);
extern int pcf_start(device_t, u_char, int);
extern int pcf_stop(device_t);
extern int pcf_write(device_t, const char *, int, int *, int);
extern int pcf_read(device_t, char *, int, int *, int, int);
extern int pcf_rst_card(device_t, u_char, u_char, u_char *);
extern driver_intr_t pcf_intr;

#define PCF_MODVER	1
#define PCF_MINVER	1
#define PCF_MAXVER	1
#define PCF_PREFVER	PCF_MODVER

#endif /* !__PCFVAR_H__ */
