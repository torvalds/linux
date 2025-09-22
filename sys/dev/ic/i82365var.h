/*	$OpenBSD: i82365var.h,v 1.14 2005/11/23 11:39:37 mickey Exp $	*/
/*	$NetBSD: i82365var.h,v 1.4 1998/05/23 18:32:29 matt Exp $	*/

/*
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

#include <sys/device.h>
#include <sys/timeout.h>

#include <dev/pcmcia/pcmciareg.h>
#include <dev/pcmcia/pcmciachip.h>

#include <dev/ic/i82365reg.h>

struct proc;

struct pcic_event {
	SIMPLEQ_ENTRY(pcic_event) pe_q;
	int	pe_type;
};

/* pe_type */
#define PCIC_EVENT_INSERTION	0
#define PCIC_EVENT_REMOVAL	1

struct pcic_handle {
	struct device *ph_parent;
	bus_space_tag_t ph_bus_t;
	bus_space_handle_t ph_bus_h;
	u_int8_t (*ph_read)(struct pcic_handle *, int);
	void (*ph_write)(struct pcic_handle *, int, int);

	int	vendor;
	int	sock;
	int	flags;
	int	laststate;
	int	memalloc;
	struct {
		bus_addr_t	addr;
		bus_size_t	size;
		long		offset;
		int		kind;
	} mem[PCIC_MEM_WINS];
	int	ioalloc;
	struct {
		bus_addr_t	addr;
		bus_size_t	size;
		int		width;
	} io[PCIC_IO_WINS];
	int	ih_irq;
	struct device *pcmcia;

	int	shutdown;
	struct proc *event_thread;
	SIMPLEQ_HEAD(, pcic_event) events;
};

#define	PCIC_FLAG_SOCKETP	0x0001
#define	PCIC_FLAG_CARDP		0x0002

#define PCIC_LASTSTATE_PRESENT	0x0002
#define PCIC_LASTSTATE_HALF	0x0001
#define PCIC_LASTSTATE_EMPTY	0x0000

#define	C0SA PCIC_CHIP0_BASE+PCIC_SOCKETA_INDEX
#define	C0SB PCIC_CHIP0_BASE+PCIC_SOCKETB_INDEX
#define	C1SA PCIC_CHIP1_BASE+PCIC_SOCKETA_INDEX
#define	C1SB PCIC_CHIP1_BASE+PCIC_SOCKETB_INDEX

/*
 * This is sort of arbitrary.  It merely needs to be "enough". It can be
 * overridden in the conf file, anyway.
 */

#define	PCIC_MEM_PAGES	4
#define	PCIC_MEMSIZE	PCIC_MEM_PAGES*PCIC_MEM_PAGESIZE

#define	PCIC_NSLOTS	4

struct	pcic_ranges {
	u_short	start;
	u_short	len;
};

struct pcic_softc {
	struct device dev;

	bus_space_tag_t memt;
	bus_space_handle_t memh;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;

	/* XXX isa_chipset_tag_t, pci_chipset_tag_t, etc. */
	void *intr_est;

	pcmcia_chipset_tag_t pct;

	/* this needs to be large enough to hold PCIC_MEM_PAGES bits */
	int	subregionmask;
#define PCIC_MAX_MEM_PAGES	(8 * sizeof(int))

	/* used by memory window mapping functions */
	bus_addr_t membase;

	/*
	 * used by io window mapping functions.  These can actually overlap
	 * with another pcic, since the underlying extent mapper will deal
	 * with individual allocations.  This is here to deal with the fact
	 * that different busses have different real widths (different pc
	 * hardware seems to use 10 or 12 bits for the I/O bus).
	 */
	bus_addr_t iobase;
	bus_addr_t iosize;
	struct pcic_ranges *ranges;

	int	irq;
	void	*ih;

	/* used by socket event polling */
	struct timeout poll_timeout;
	int poll_established;

	struct pcic_handle handle[PCIC_NSLOTS];
};


int	pcic_ident_ok(int);
int	pcic_vendor(struct pcic_handle *);

void	pcic_attach(struct pcic_softc *);
void	pcic_attach_sockets(struct pcic_softc *);
int	pcic_intr(void *arg);
void	pcic_poll_intr(void *arg);

int	pcic_chip_mem_alloc(pcmcia_chipset_handle_t, bus_size_t,
	    struct pcmcia_mem_handle *);
void	pcic_chip_mem_free(pcmcia_chipset_handle_t,
	    struct pcmcia_mem_handle *);
int	pcic_chip_mem_map(pcmcia_chipset_handle_t, int, bus_addr_t,
	    bus_size_t, struct pcmcia_mem_handle *, bus_size_t *, int *);
void	pcic_chip_mem_unmap(pcmcia_chipset_handle_t, int);

int	pcic_chip_io_alloc(pcmcia_chipset_handle_t, bus_addr_t,
	    bus_size_t, bus_size_t, struct pcmcia_io_handle *);
void	pcic_chip_io_free(pcmcia_chipset_handle_t,
	    struct pcmcia_io_handle *);
int	pcic_chip_io_map(pcmcia_chipset_handle_t, int, bus_addr_t,
	    bus_size_t, struct pcmcia_io_handle *, int *);
void	pcic_chip_io_unmap(pcmcia_chipset_handle_t, int);

void	pcic_chip_socket_enable(pcmcia_chipset_handle_t);
void	pcic_chip_socket_disable(pcmcia_chipset_handle_t);

void	pcic_power(int, void *);

#define pcic_read(h, idx) \
	(*(h)->ph_read)((h), (idx))

#define pcic_write(h, idx, data) \
	(*(h)->ph_write)((h), (idx), (data))
