/*	$OpenBSD: pccbbvar.h,v 1.16 2010/09/06 18:34:34 kettenis Exp $	*/
/*	$NetBSD: pccbbvar.h,v 1.13 2000/06/08 10:28:29 haya Exp $	*/
/*
 * Copyright (c) 1999 HAYAKAWA Koichi.  All rights reserved.
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

/* require sys/device.h */
/* require sys/queue.h */
/* require sys/callout.h */
/* require dev/ic/i82365reg.h */
/* require dev/ic/i82365var.h */

#ifndef _DEV_PCI_PCCBBVAR_H_
#define	_DEV_PCI_PCCBBVAR_H_

#include <sys/timeout.h>

#define	PCIC_FLAG_SOCKETP	0x0001
#define	PCIC_FLAG_CARDP		0x0002

/* Chipset ID */
#define	CB_UNKNOWN	0	/* NOT Cardbus-PCI bridge */
#define	CB_TI113X	1	/* TI PCI1130/1131 */
#define	CB_TI12XX	2	/* TI PCI1250/1220 */
#define	CB_RX5C47X	3	/* RICOH RX5C475/476/477 */
#define	CB_RX5C46X	4	/* RICOH RX5C465/466/467 */
#define	CB_TOPIC95	5	/* Toshiba ToPIC95 */
#define	CB_TOPIC95B	6	/* Toshiba ToPIC95B */
#define	CB_TOPIC97	7	/* Toshiba ToPIC97 */
#define	CB_CIRRUS	8	/* Cirrus Logic CL-PD683X */
#define	CB_TI125X	9	/* TI PCI1250/1251(B)/1450 */
#define	CB_OLDO2MICRO	10	/* O2Micro */
#define	CB_CHIPS_LAST	11	/* Sentinel */

#define PCCARD_VCC_UKN		0x00	/* Unknown */
#define PCCARD_VCC_5V		0x01
#define PCCARD_VCC_3V		0x02
#define PCCARD_VCC_XV		0x04
#define PCCARD_VCC_YV		0x08

#if 0
static char *cb_chipset_name[CB_CHIPS_LAST] = {
	"unknown", "TI 113X", "TI 12XX", "RF5C47X", "RF5C46X", "ToPIC95",
	"ToPIC95B", "ToPIC97", "CL-PD 683X", "TI 125X",
};
#endif

struct pccbb_softc;
struct pccbb_intrhand_list;


struct cbb_pcic_handle {
	struct device *ph_parent;
	bus_space_tag_t ph_base_t;
	bus_space_handle_t ph_base_h;
	u_int8_t (*ph_read)(struct cbb_pcic_handle *, int);
	void (*ph_write)(struct cbb_pcic_handle *, int, u_int8_t);
	int sock;

	int vendor;
	int flags;
	int memalloc;
	struct {
		bus_addr_t addr;
		bus_size_t size;
		long offset;
		int kind;
	} mem[PCIC_MEM_WINS];
	int ioalloc;
	struct {
		bus_addr_t addr;
		bus_size_t size;
		int width;
	} io[PCIC_IO_WINS];
	int ih_irq;
	struct device *pcmcia;

	int shutdown;
};

struct pccbb_win_chain {
	bus_addr_t wc_start;		/* Caution: region [start, end], */
	bus_addr_t wc_end;		/* instead of [start, end). */
	int wc_flags;
	bus_space_handle_t wc_handle;
	TAILQ_ENTRY(pccbb_win_chain) wc_list;
};
#define	PCCBB_MEM_CACHABLE	1

TAILQ_HEAD(pccbb_win_chain_head, pccbb_win_chain);

struct pccbb_softc {
	struct device sc_dev;
	bus_space_tag_t sc_iot;
	bus_space_tag_t sc_memt;
	bus_dma_tag_t sc_dmat;

	rbus_tag_t sc_rbus_iot;		/* rbus for i/o donated from parent */
	rbus_tag_t sc_rbus_memt;	/* rbus for mem donated from parent */

	bus_space_tag_t sc_base_memt;
	bus_space_handle_t sc_base_memh;

	struct timeout sc_ins_tmo;
	void *sc_ih;			/* interrupt handler */
	int sc_intrline;		/* interrupt line */
	pcitag_t sc_intrtag;		/* copy of pa->pa_intrtag */
	pci_intr_pin_t sc_intrpin;	/* copy of pa->pa_intrpin */
	int sc_function;
	u_int32_t sc_flags;
#define	CBB_CARDEXIST	0x01
#define	CBB_INSERTING	0x01000000
#define	CBB_16BITCARD	0x04
#define	CBB_32BITCARD	0x08

	pci_chipset_tag_t sc_pc;
	pcitag_t sc_tag;
	pcireg_t sc_id;
	int sc_chipset;			/* chipset id */
	int sc_ints_on;

	pcireg_t sc_csr;
	pcireg_t sc_bhlcr;
	pcireg_t sc_int;

	pcireg_t sc_sockbase;		/* Socket base register */
	pcireg_t sc_busnum;		/* bus number */

	pcireg_t sc_sysctrl;
	pcireg_t sc_cbctrl;
	pcireg_t sc_mfunc;

	/* CardBus stuff */
	struct cardslot_softc *sc_csc;

	struct pccbb_win_chain_head sc_memwindow;
	struct pccbb_win_chain_head sc_iowindow;

	pcireg_t sc_membase[2];
	pcireg_t sc_memlimit[2];
	pcireg_t sc_iobase[2];
	pcireg_t sc_iolimit[2];

	/* pcmcia stuff */
	struct pcic_handle sc_pcmcia_h;
	pcmcia_chipset_tag_t sc_pct;
	int sc_pcmcia_flags;
#define	PCCBB_PCMCIA_IO_RELOC	0x01	/* IO addr relocatable stuff exists */
#define	PCCBB_PCMCIA_MEM_32	0x02	/* 32-bit memory address ready */
#define	PCCBB_PCMCIA_16BITONLY	0x04	/* 32-bit mode disable */

	struct proc *sc_event_thread;
	SIMPLEQ_HEAD(, pcic_event) sc_events;

	/* interrupt handler list on the bridge */
	struct pccbb_intrhand_list *sc_pil;
	int sc_pil_intr_enable;	/* can i call intr handler for child device? */
};

/*
 * struct pccbb_intrhand_list holds interrupt handler and argument for
 * child devices.
 */

struct pccbb_intrhand_list {
	int (*pil_func)(void *);
	void *pil_arg;
	int pil_level;
	struct evcount pil_count;
	struct pccbb_intrhand_list *pil_next;
};

#endif /* _DEV_PCI_PCCBBREG_H_ */
