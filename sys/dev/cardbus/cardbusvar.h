/*	$OpenBSD: cardbusvar.h,v 1.20 2014/12/18 10:51:35 mpi Exp $	*/
/*	$NetBSD: cardbusvar.h,v 1.17 2000/04/02 19:11:37 mycroft Exp $	*/

/*
 * Copyright (c) 1998, 1999 and 2000
 *       HAYAKAWA Koichi.  All rights reserved.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_CARDBUS_CARDBUSVAR_H_
#define _DEV_CARDBUS_CARDBUSVAR_H_

#include <dev/pci/pcivar.h>	/* for pcitag_t */
#include <dev/cardbus/rbus.h>

typedef void *cardbus_chipset_tag_t;
typedef int cardbus_intr_handle_t;

/* Base Registers */
#define CARDBUS_BASE0_REG  0x10
#define CARDBUS_BASE1_REG  0x14
#define CARDBUS_BASE2_REG  0x18
#define CARDBUS_BASE3_REG  0x1C
#define CARDBUS_BASE4_REG  0x20
#define CARDBUS_BASE5_REG  0x24
#define CARDBUS_CIS_REG    0x28
#define CARDBUS_ROM_REG	   0x30
#  define CARDBUS_CIS_ASIMASK 0x07
#    define CARDBUS_CIS_ASI(x) (CARDBUS_CIS_ASIMASK & (x))
#  define CARDBUS_CIS_ASI_TUPLE 0x00
#  define CARDBUS_CIS_ASI_BAR0  0x01
#  define CARDBUS_CIS_ASI_BAR1  0x02
#  define CARDBUS_CIS_ASI_BAR2  0x03
#  define CARDBUS_CIS_ASI_BAR3  0x04
#  define CARDBUS_CIS_ASI_BAR4  0x05
#  define CARDBUS_CIS_ASI_BAR5  0x06
#  define CARDBUS_CIS_ASI_ROM   0x07
#  define CARDBUS_CIS_ADDRMASK 0x0ffffff8
#    define CARDBUS_CIS_ADDR(x) (CARDBUS_CIS_ADDRMASK & (x))
#    define CARDBUS_CIS_ASI_BAR(x) (((CARDBUS_CIS_ASIMASK & (x))-1)*4+0x10)
#    define CARDBUS_CIS_ASI_ROM_IMAGE(x) (((x) >> 28) & 0xf)

/* XXX end */

typedef struct cardbus_functions {
	int (*cardbus_space_alloc)(cardbus_chipset_tag_t, rbus_tag_t,
	    bus_addr_t, bus_size_t, bus_addr_t, bus_size_t, int, bus_addr_t *,
	    bus_space_handle_t *);
	int (*cardbus_space_free)(cardbus_chipset_tag_t, rbus_tag_t,
	    bus_space_handle_t, bus_size_t);
	void *(*cardbus_intr_establish)(cardbus_chipset_tag_t, int, int,
	    int (*)(void *), void *, const char *);
	void (*cardbus_intr_disestablish)(cardbus_chipset_tag_t, void *);
	int (*cardbus_ctrl)(cardbus_chipset_tag_t, int);
	int (*cardbus_power)(cardbus_chipset_tag_t, int);
} cardbus_function_t, *cardbus_function_tag_t;

/*
 * struct cbslot_attach_args is the attach argument for cardbus card.
 */
struct cbslot_attach_args {
	char *cba_busname;
	bus_space_tag_t cba_iot;	/* cardbus i/o space tag */
	bus_space_tag_t cba_memt;	/* cardbus mem space tag */
	bus_dma_tag_t cba_dmat;		/* DMA tag */

	int cba_bus;			/* cardbus bus number */

	cardbus_chipset_tag_t cba_cc;	/* cardbus chipset */
	pci_chipset_tag_t cba_pc;	/* pci chipset */
	cardbus_function_tag_t cba_cf;	/* cardbus functions */
	int cba_intrline;		/* interrupt line */

	rbus_tag_t cba_rbus_iot;	/* CardBus i/o rbus tag */
	rbus_tag_t cba_rbus_memt;	/* CardBus mem rbus tag */

	int cba_cacheline;		/* cache line size */
	int cba_lattimer;		/* latency timer */
};


#define cbslotcf_dev  cf_loc[0]
#define cbslotcf_func cf_loc[1]
#define CBSLOT_UNK_DEV -1
#define CBSLOT_UNK_FUNC -1


struct cardbus_devfunc;

/*
 * struct cardbus_softc is the softc for cardbus card.
 */
struct cardbus_softc {
	struct device sc_dev;		/* fundamental device structure */

	int sc_bus;			/* cardbus bus number */
	int sc_device;			/* cardbus device number */
	int sc_intrline;		/* CardBus intrline */

	bus_space_tag_t sc_iot;		/* CardBus I/O space tag */
	bus_space_tag_t sc_memt;	/* CardBus MEM space tag */
	bus_dma_tag_t sc_dmat;		/* DMA tag */

	cardbus_chipset_tag_t sc_cc;	/* CardBus chipset */
	pci_chipset_tag_t sc_pc;	/* PCI chipset */
	cardbus_function_tag_t sc_cf;	/* CardBus function */

	rbus_tag_t sc_rbus_iot;		/* CardBus i/o rbus tag */
	rbus_tag_t sc_rbus_memt;	/* CardBus mem rbus tag */

	int sc_cacheline;		/* cache line size */
	int sc_lattimer;		/* latency timer */
	int sc_volt;			/* applied Vcc voltage */
#define PCCARD_33V  0x02
#define PCCARD_XXV  0x04
#define PCCARD_YYV  0x08
	int sc_poweron_func;
	struct cardbus_devfunc *sc_funcs[8];	/* cardbus device functions */
};


/*
 * struct cardbus_devfunc:
 *
 *   This is the data deposit for each function of a CardBus device.
 *   This structure is used for memory or i/o space allocation and
 *   disallocation.
 */
typedef struct cardbus_devfunc {
	cardbus_chipset_tag_t ct_cc;
	cardbus_function_tag_t ct_cf;
	struct cardbus_softc *ct_sc;	/* pointer to the parent */
	int ct_bus;			/* bus number */
	int ct_dev;			/* device number */
	int ct_func;			/* function number */

	rbus_tag_t ct_rbus_iot;		/* CardBus i/o rbus tag */
	rbus_tag_t ct_rbus_memt;	/* CardBus mem rbus tag */

	u_int32_t ct_lc;		/* Latency timer and cache line size */
	/* u_int32_t ct_cisreg; */	/* CIS reg: is it needed??? */

	struct device *ct_device;	/* pointer to the device */

	struct cardbus_devfunc *ct_next;

  /* some data structure needed for tuple??? */
} *cardbus_devfunc_t;


/* XXX various things extracted from CIS */
struct cardbus_cis_info {
	int32_t		manufacturer;
	int32_t		product;
	char		cis1_info_buf[256];
	char	       *cis1_info[4];
	struct cb_bar_info {
		unsigned int flags;
		unsigned int size;
	} bar[7];
	unsigned int	funcid;
	union {
		struct {
			int uart_type;
			int uart_present;
		} serial;
		struct {
			char netid[6];
			char netid_present;
			char __filler;
		} network;
	} funce;
};

struct cardbus_attach_args {
	int ca_unit;
	cardbus_devfunc_t ca_ct;
	pci_chipset_tag_t ca_pc;	/* PCI chipset */

	bus_space_tag_t ca_iot;		/* CardBus I/O space tag */
	bus_space_tag_t ca_memt;	/* CardBus MEM space tag */
	bus_dma_tag_t ca_dmat;		/* DMA tag */

	u_int ca_bus;
	u_int ca_device;
	u_int ca_function;
	pcitag_t ca_tag;
	pcireg_t ca_id;
	pcireg_t ca_class;

	/* Interrupt information */
	pci_intr_line_t ca_intrline;

	rbus_tag_t ca_rbus_iot;		/* CardBus i/o rbus tag */
	rbus_tag_t ca_rbus_memt;	/* CardBus mem rbus tag */

	struct cardbus_cis_info ca_cis;
};


#define CARDBUS_ENABLE  1	/* enable the channel */
#define CARDBUS_DISABLE 2	/* disable the channel */
#define CARDBUS_RESET   4
#define CARDBUS_CD	7
#  define CARDBUS_NOCARD 0
#  define CARDBUS_5V_CARD 0x01	/* XXX: It must not exist */
#  define CARDBUS_3V_CARD 0x02
#  define CARDBUS_XV_CARD 0x04
#  define CARDBUS_YV_CARD 0x08
#define CARDBUS_IO_ENABLE    100
#define CARDBUS_IO_DISABLE   101
#define CARDBUS_MEM_ENABLE   102
#define CARDBUS_MEM_DISABLE  103
#define CARDBUS_BM_ENABLE    104 /* bus master */
#define CARDBUS_BM_DISABLE   105

#define CARDBUS_VCC_UC  0x0000
#define CARDBUS_VCC_3V  0x0001
#define CARDBUS_VCC_XV  0x0002
#define CARDBUS_VCC_YV  0x0003
#define CARDBUS_VCC_0V  0x0004
#define CARDBUS_VCC_5V  0x0005	/* ??? */
#define CARDBUS_VCCMASK 0x000f
#define CARDBUS_VPP_UC  0x0000
#define CARDBUS_VPP_VCC 0x0010
#define CARDBUS_VPP_12V 0x0030
#define CARDBUS_VPP_0V  0x0040
#define CARDBUS_VPPMASK 0x00f0

#define CARDBUSCF_DEV			0
#define CARDBUSCF_DEV_DEFAULT		-1
#define CARDBUSCF_FUNCTION		1
#define CARDBUSCF_FUNCTION_DEFAULT	-1

/*
 * Locators devies that attach to 'cardbus', as specified to config.
 */
#define cardbuscf_dev cf_loc[CARDBUSCF_DEV]
#define CARDBUS_UNK_DEV CARDBUSCF_DEV_DEFAULT

#define cardbuscf_function cf_loc[CARDBUSCF_FUNCTION]
#define CARDBUS_UNK_FUNCTION CARDBUSCF_FUNCTION_DEFAULT

int	cardbus_attach_card(struct cardbus_softc *);
void	cardbus_detach_card(struct cardbus_softc *);
void   *cardbus_intr_establish(cardbus_chipset_tag_t, cardbus_function_tag_t,
	    cardbus_intr_handle_t irq, int level, int (*func) (void *),
	    void *arg, const char *);
void	cardbus_intr_disestablish(cardbus_chipset_tag_t,
	    cardbus_function_tag_t, void *handler);

int	cardbus_mapreg_map(struct cardbus_softc *, int, int, pcireg_t,
	    int, bus_space_tag_t *, bus_space_handle_t *, bus_addr_t *,
	    bus_size_t *);
int	cardbus_mapreg_unmap(struct cardbus_softc *, int, int,
	    bus_space_tag_t, bus_space_handle_t, bus_size_t);

int	cardbus_function_enable(struct cardbus_softc *, int function);
int	cardbus_function_disable(struct cardbus_softc *, int function);

int	cardbus_matchbyid(struct cardbus_attach_args *,
	    const struct pci_matchid *, int);

#define Cardbus_function_enable(ct)			\
    cardbus_function_enable((ct)->ct_sc, (ct)->ct_func)
#define Cardbus_function_disable(ct)			\
    cardbus_function_disable((ct)->ct_sc, (ct)->ct_func)

#define Cardbus_mapreg_map(ct, reg, type, busflags, tagp, handlep, basep, sizep) \
    cardbus_mapreg_map((ct)->ct_sc, (ct->ct_func), 	\
    (reg), (type), (busflags), (tagp), (handlep), (basep), (sizep))
#define Cardbus_mapreg_unmap(ct, reg, tag, handle, size)\
    cardbus_mapreg_unmap((ct)->ct_sc, (ct->ct_func), 	\
    (reg), (tag), (handle), (size))

#endif /* !_DEV_CARDBUS_CARDBUSVAR_H_ */
