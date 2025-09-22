/*	$OpenBSD: tcic2var.h,v 1.5 2021/03/07 06:21:38 jsg Exp $	*/
/*	$NetBSD: tcic2var.h,v 1.1 1999/03/23 20:04:14 bad Exp $	*/

/*
 * Copyright (c) 1998, 1999 Christoph Badura.  All rights reserved.
 * Copyright (c) 1997 Marc Horowitz.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Marc Horowitz.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 */

#ifndef _TCIC2VAR_H
#define _TCIC2VAR_H

#include <sys/device.h>

#include <dev/pcmcia/pcmciareg.h>
#include <dev/pcmcia/pcmciachip.h>

#include <dev/ic/tcic2reg.h>

struct proc;

struct tcic_event {
	SIMPLEQ_ENTRY(tcic_event) pe_q;
	int pe_type;
};

/* pe_type */
#define	TCIC_EVENT_INSERTION	0
#define	TCIC_EVENT_REMOVAL	1


struct tcic_handle {
	struct tcic_softc *sc;
	int	sock;				/* socket number */
	int	flags;
	int	sstat;				/* last value of R_SSTAT */
	int	memalloc;
	int	memwins;
	struct {
		bus_addr_t	addr;
		bus_size_t	size;
		int		size2;		/* size as 2^n scaled by 4K */
		long		offset;
		int		speed;		/* in ns */
		int		kind;
	} mem[TCIC_MAX_MEM_WINS];
	int	ioalloc;
	struct {
		bus_addr_t	addr;
		bus_size_t	size;
		int		width;
		int		speed;		/* in ns */
	} io[TCIC_IO_WINS];
	int	ih_irq;
	struct device *pcmcia;

	int shutdown;
	struct proc *event_thread;
	SIMPLEQ_HEAD(, tcic_event) events;
};

#define	TCIC_FLAG_SOCKETP	0x0001
#define	TCIC_FLAG_CARDP		0x0002

/*
 * This is sort of arbitrary.  It merely needs to be "enough". It can be
 * overridden in the conf file, anyway.
 */

#define	TCIC_MEM_PAGES	4
#define	TCIC_MEMSIZE	TCIC_MEM_PAGES*TCIC_MEM_PAGESIZE

#define	TCIC_NSLOTS	2

struct tcic_softc {
	struct device dev;

	bus_space_tag_t memt;
	bus_space_handle_t memh;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;

	int	chipid;
	int	validirqs;
	int	pwrena;		/* holds TCIC_PWR_ENA on'084 and successors */

	/* XXX isa_chipset_tag_t, pci_chipset_tag_t, etc. */
	void	*intr_est;

	pcmcia_chipset_tag_t pct;

	/* this needs to be large enough to hold TCIC_MEM_PAGES bits */
	int	subregionmask;

	/* used by memory window mapping functions */
	bus_addr_t membase;
	int	memsize2;		/* int(log2(memsize)) */

	/*
	 * used by io window mapping functions.  These can actually overlap
	 * with another tcic, since the underlying extent mapper will deal
	 * with individual allocations.  This is here to deal with the fact
	 * that different busses have different real widths (different pc
	 * hardware seems to use 10 or 12 bits for the I/O bus).
	 */
	bus_addr_t iobase;
	bus_size_t iosize;

	int	irq;
	void	*ih;

	struct tcic_handle handle[TCIC_NSLOTS];
};

int	tcic_log2(u_int);
int	tcic_ns2wscnt(int);

int	tcic_check_reserved_bits(bus_space_tag_t, bus_space_handle_t);
int	tcic_chipid(bus_space_tag_t, bus_space_handle_t);
int	tcic_chipid_known(int);
char	*tcic_chipid_to_string(int);
int	tcic_validirqs(int);

void	tcic_attach(struct tcic_softc *);
void	tcic_attach_sockets(struct tcic_softc *);
int	tcic_intr(void *arg);

static __inline__ int tcic_read_1(struct tcic_handle *, int);
static __inline__ int tcic_read_2(struct tcic_handle *, int);
static __inline__ int tcic_read_4(struct tcic_handle *, int);
static __inline__ void tcic_write_1(struct tcic_handle *, int, int);
static __inline__ void tcic_write_2(struct tcic_handle *, int, int);
static __inline__ void tcic_write_4(struct tcic_handle *, int, int);
static __inline__ int tcic_read_ind_2(struct tcic_handle *, int);
static __inline__ void tcic_write_ind_2(struct tcic_handle *, int, int);
static __inline__ void tcic_sel_sock(struct tcic_handle *);
static __inline__ void tcic_wait_ready(struct tcic_handle *);
static __inline__ int tcic_read_aux_1(bus_space_tag_t, bus_space_handle_t, int, int);
static __inline__ int tcic_read_aux_2(bus_space_tag_t, bus_space_handle_t, int);
static __inline__ void tcic_write_aux_1(bus_space_tag_t, bus_space_handle_t, int, int, int);
static __inline__ void tcic_write_aux_2(bus_space_tag_t, bus_space_handle_t, int, int);

int	tcic_chip_mem_alloc(pcmcia_chipset_handle_t, bus_size_t,
	    struct pcmcia_mem_handle *);
void	tcic_chip_mem_free(pcmcia_chipset_handle_t,
	    struct pcmcia_mem_handle *);
int	tcic_chip_mem_map(pcmcia_chipset_handle_t, int, bus_addr_t,
	    bus_size_t, struct pcmcia_mem_handle *, bus_size_t *, int *);
void	tcic_chip_mem_unmap(pcmcia_chipset_handle_t, int);

int	tcic_chip_io_alloc(pcmcia_chipset_handle_t, bus_addr_t,
	    bus_size_t, bus_size_t, struct pcmcia_io_handle *);
void	tcic_chip_io_free(pcmcia_chipset_handle_t,
	    struct pcmcia_io_handle *);
int	tcic_chip_io_map(pcmcia_chipset_handle_t, int, bus_addr_t,
	    bus_size_t, struct pcmcia_io_handle *, int *);
void	tcic_chip_io_unmap(pcmcia_chipset_handle_t, int);

void	tcic_chip_socket_enable(pcmcia_chipset_handle_t);
void	tcic_chip_socket_disable(pcmcia_chipset_handle_t);

static __inline__ int tcic_read_1(struct tcic_handle *, int);
static __inline__ int
tcic_read_1(struct tcic_handle *h, int reg)
{
	return (bus_space_read_1(h->sc->iot, h->sc->ioh, reg));
}

static __inline__ int tcic_read_2(struct tcic_handle *, int);
static __inline__ int
tcic_read_2(struct tcic_handle *h, int reg)
{
	return (bus_space_read_2(h->sc->iot, h->sc->ioh, reg));
}

static __inline__ int tcic_read_4(struct tcic_handle *, int);
static __inline__ int
tcic_read_4(struct tcic_handle *h, int reg)
{
	int val;
	val = bus_space_read_2(h->sc->iot, h->sc->ioh, reg);
	val |= bus_space_read_2(h->sc->iot, h->sc->ioh, reg+2) << 16;
	return val;
}

static __inline__ void tcic_write_1(struct tcic_handle *, int, int);
static __inline__ void
tcic_write_1(struct tcic_handle *h, int reg, int data)
{
	bus_space_write_1(h->sc->iot, h->sc->ioh, reg, (data));
}

static __inline__ void tcic_write_2(struct tcic_handle *, int, int);
static __inline__ void
tcic_write_2(struct tcic_handle *h, int reg, int data)
{
	bus_space_write_2(h->sc->iot, h->sc->ioh, reg, (data));
}

static __inline__ void tcic_write_4(struct tcic_handle *, int, int);
static __inline__ void
tcic_write_4(struct tcic_handle *h, int reg, int data)
{
	bus_space_write_2(h->sc->iot, h->sc->ioh, reg, (data));
	bus_space_write_2(h->sc->iot, h->sc->ioh, reg+2, (data)>>16);
}

static __inline__ int tcic_read_ind_2(struct tcic_handle *, int);
static __inline__ int
tcic_read_ind_2(struct tcic_handle *h, int reg)
{
	int r_addr, val;
	r_addr = tcic_read_4(h, TCIC_R_ADDR);
	tcic_write_4(h, TCIC_R_ADDR, reg|TCIC_ADDR_INDREG);
	val = bus_space_read_2(h->sc->iot, h->sc->ioh, TCIC_R_DATA);
	tcic_write_4(h, TCIC_R_ADDR, r_addr);
	return val;
}

static __inline__ void tcic_write_ind_2(struct tcic_handle *, int, int);
static __inline__ void
tcic_write_ind_2(struct tcic_handle *h, int reg, int data)
{
	int r_addr;
	r_addr = tcic_read_4(h, TCIC_R_ADDR);
	tcic_write_4(h, TCIC_R_ADDR, reg|TCIC_ADDR_INDREG);
	bus_space_write_2(h->sc->iot, h->sc->ioh, TCIC_R_DATA, (data));
	tcic_write_4(h, TCIC_R_ADDR, r_addr);
}

static __inline__ void tcic_sel_sock(struct tcic_handle *);
static __inline__ void
tcic_sel_sock(struct tcic_handle *h)
{
	int r_addr;
	r_addr = tcic_read_2(h, TCIC_R_ADDR2);
	tcic_write_2(h, TCIC_R_ADDR2,
	    (h->sock<<TCIC_ADDR2_SS_SHFT)|(r_addr & ~TCIC_ADDR2_SS_MASK));
}

static __inline__ void tcic_wait_ready(struct tcic_handle *);
static __inline__ void
tcic_wait_ready(struct tcic_handle *h)
{
	int i;

	/* XXX appropriate socket must have been selected already. */
	for (i = 0; i < 10000; i++) {
		if (tcic_read_1(h, TCIC_R_SSTAT) & TCIC_SSTAT_RDY)
			return;
		delay(500);
	}

#ifdef DIAGNOSTIC
	printf("tcic_wait_ready ready never happened\n");
#endif
}

static __inline__ int tcic_read_aux_1(bus_space_tag_t, bus_space_handle_t, int, int);
static __inline__ int
tcic_read_aux_1(bus_space_tag_t iot, bus_space_handle_t ioh, int auxreg,
    int reg)
{
	int mode, val;
	mode = bus_space_read_1(iot, ioh, TCIC_R_MODE);
	bus_space_write_1(iot, ioh, TCIC_R_MODE, (mode & ~TCIC_AR_MASK)|auxreg);
	val = bus_space_read_1(iot, ioh, reg);
	return val;
}

static __inline__ int tcic_read_aux_2(bus_space_tag_t, bus_space_handle_t, int);
static __inline__ int
tcic_read_aux_2(bus_space_tag_t iot, bus_space_handle_t ioh, int auxreg)
{
	int mode, val;
	mode = bus_space_read_1(iot, ioh, TCIC_R_MODE);
	bus_space_write_1(iot, ioh, TCIC_R_MODE, (mode & ~TCIC_AR_MASK)|auxreg);
	val = bus_space_read_2(iot, ioh, TCIC_R_AUX);
	return val;
}

static __inline__ void tcic_write_aux_1(bus_space_tag_t, bus_space_handle_t, int, int, int);
static __inline__ void
tcic_write_aux_1(bus_space_tag_t iot, bus_space_handle_t ioh, int auxreg,
    int reg, int val)
{
	int mode;
	mode = bus_space_read_1(iot, ioh, TCIC_R_MODE);
	bus_space_write_1(iot, ioh, TCIC_R_MODE, (mode & ~TCIC_AR_MASK)|auxreg);
	bus_space_write_1(iot, ioh, reg, val);
}

static __inline__ void tcic_write_aux_2(bus_space_tag_t, bus_space_handle_t, int, int);
static __inline__ void
tcic_write_aux_2(bus_space_tag_t iot, bus_space_handle_t ioh, int auxreg,
    int val)
{
	int mode;
	mode = bus_space_read_1(iot, ioh, TCIC_R_MODE);
	bus_space_write_1(iot, ioh, TCIC_R_MODE, (mode & ~TCIC_AR_MASK)|auxreg);
	bus_space_write_2(iot, ioh, TCIC_R_AUX, val);
}

#endif	/* _TCIC2VAR_H */
