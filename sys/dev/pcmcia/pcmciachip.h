/*	$OpenBSD: pcmciachip.h,v 1.8 2005/11/23 11:39:37 mickey Exp $ */
/*	$NetBSD: pcmciachip.h,v 1.5 2000/01/13 08:58:51 joda Exp $	*/

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

#ifndef _PCMCIA_PCMCIACHIP_H_
#define	_PCMCIA_PCMCIACHIP_H_

#include <machine/bus.h>

struct pcmcia_function;
struct pcmcia_mem_handle;
struct pcmcia_io_handle;

/* interfaces for pcmcia to call the chipset */

typedef struct pcmcia_chip_functions *pcmcia_chipset_tag_t;
typedef void *pcmcia_chipset_handle_t;
typedef int pcmcia_mem_handle_t;

#define	PCMCIA_MEM_ATTR		1
#define	PCMCIA_MEM_COMMON	2

#define	PCMCIA_WIDTH_MEM8	8
#define	PCMCIA_WIDTH_MEM16	16

#define	PCMCIA_WIDTH_MEM_MASK	24

#define	PCMCIA_WIDTH_AUTO	0
#define	PCMCIA_WIDTH_IO8	1
#define	PCMCIA_WIDTH_IO16	2

struct pcmcia_chip_functions {
	/* memory space allocation */
	int	(*mem_alloc)(pcmcia_chipset_handle_t, bus_size_t,
		    struct pcmcia_mem_handle *);
	void	(*mem_free)(pcmcia_chipset_handle_t,
		    struct pcmcia_mem_handle *);

	/* memory space window mapping */
	int	(*mem_map)(pcmcia_chipset_handle_t, int, bus_addr_t,
		    bus_size_t, struct pcmcia_mem_handle *,
		    bus_size_t *, int *);
	void	(*mem_unmap)(pcmcia_chipset_handle_t, int);

	/* I/O space allocation */
	int	(*io_alloc)(pcmcia_chipset_handle_t, bus_addr_t,
		    bus_size_t, bus_size_t, struct pcmcia_io_handle *);
	void	(*io_free)(pcmcia_chipset_handle_t,
		    struct pcmcia_io_handle *);

	/* I/O space window mapping */
	int	(*io_map)(pcmcia_chipset_handle_t, int, bus_addr_t,
		    bus_size_t, struct pcmcia_io_handle *, int *);
	void	(*io_unmap)(pcmcia_chipset_handle_t, int);

	/* interrupt glue */
	void	*(*intr_establish)(pcmcia_chipset_handle_t,
		    struct pcmcia_function *, int, int (*)(void *), void *, char *);
	void	(*intr_disestablish)(pcmcia_chipset_handle_t, void *);
	const char *(*intr_string)(pcmcia_chipset_handle_t, void *);

	/* card enable/disable */
	void	(*socket_enable)(pcmcia_chipset_handle_t);
	void	(*socket_disable)(pcmcia_chipset_handle_t);

	/* card detection */
	int (*card_detect)(pcmcia_chipset_handle_t);  
};

/* Memory space functions. */
#define pcmcia_chip_mem_alloc(tag, handle, size, pcmhp)			\
	((*(tag)->mem_alloc)((handle), (size), (pcmhp)))

#define pcmcia_chip_mem_free(tag, handle, pcmhp)			\
	((*(tag)->mem_free)((handle), (pcmhp)))

#define pcmcia_chip_mem_map(tag, handle, kind, card_addr, size, pcmhp,	\
	    offsetp, windowp)						\
	((*(tag)->mem_map)((handle), (kind), (card_addr), (size), (pcmhp), \
	    (offsetp), (windowp)))

#define pcmcia_chip_mem_unmap(tag, handle, window)			\
	((*(tag)->mem_unmap)((handle), (window)))

/* I/O space functions. */
#define pcmcia_chip_io_alloc(tag, handle, start, size, align, pcihp)	\
	((*(tag)->io_alloc)((handle), (start), (size), (align), (pcihp)))

#define pcmcia_chip_io_free(tag, handle, pcihp)				\
	((*(tag)->io_free)((handle), (pcihp)))

#define pcmcia_chip_io_map(tag, handle, width, card_addr, size, pcihp,	\
	    windowp) \
	((*(tag)->io_map)((handle), (width), (card_addr), (size), (pcihp), \
	    (windowp)))

#define pcmcia_chip_io_unmap(tag, handle, window)			\
	((*(tag)->io_unmap)((handle), (window)))

/* Interrupt functions. */
#define pcmcia_chip_intr_establish(tag, handle, pf, ipl, fct, arg, xname)	\
	((*(tag)->intr_establish)((handle), (pf), (ipl), (fct), (arg), (xname)))

#define pcmcia_chip_intr_disestablish(tag, handle, ih)			\
	((*(tag)->intr_disestablish)((handle), (ih)))

#define pcmcia_chip_intr_string(tag, handle, ih)			\
	((*(tag)->intr_string)((handle), (ih)))

/* Socket functions. */
#define	pcmcia_chip_socket_enable(tag, handle)				\
	((*(tag)->socket_enable)((handle)))
#define	pcmcia_chip_socket_disable(tag, handle)				\
	((*(tag)->socket_disable)((handle)))

struct pcmciabus_attach_args {
	char *paa_busname;	/* Bus name */
	pcmcia_chipset_tag_t pct;
	pcmcia_chipset_handle_t pch;
	bus_addr_t iobase;		/* start i/o space allocation here */
	bus_size_t iosize;		/* size of the i/o space range */
};

/* interfaces for the chipset to call pcmcia */

int	pcmcia_card_attach(struct device *);
void	pcmcia_card_detach(struct device *, int);
void	pcmcia_card_deactivate(struct device *);
int	pcmcia_card_gettype(struct device *);

#endif /* _PCMCIA_PCMCIACHIP_H_ */
