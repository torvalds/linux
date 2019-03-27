/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2004 M. Warner Losh.
 * Copyright (c) 2000,2001 Jonathan Chen.
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
 * Structure definitions for the Cardbus Bridge driver
 */

struct cbb_intrhand {
	driver_filter_t	*filt;
	driver_intr_t	*intr;
	void 		*arg;
	struct cbb_softc *sc;
	void		*cookie;
};

struct cbb_reslist {
	SLIST_ENTRY(cbb_reslist) link;
	struct	resource *res;
	int	type;
	int	rid;
		/* note: unlike the regular resource list, there can be
		 * duplicate rid's in the same list.  However, the
		 * combination of rid and res->r_dev should be unique.
		 */
	bus_addr_t cardaddr; /* for 16-bit pccard memory */
};

#define	CBB_AUTO_OPEN_SMALLHOLE 0x100
#define CBB_NSLOTS		4

struct cbb_softc {
	device_t	dev;
	struct exca_softc exca[CBB_NSLOTS];
	struct		resource *base_res;
	struct		resource *irq_res;
	void		*intrhand;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	uint32_t	domain;
	unsigned int	pribus;
	struct pcib_secbus bus;
	struct mtx	mtx;
	int		cardok;
	u_int32_t	flags;
#define	CBB_16BIT_CARD		0x20000000
#define	CBB_KTHREAD_RUNNING	0x40000000
#define	CBB_KTHREAD_DONE	0x80000000
	int		chipset;		/* chipset id */
#define	CB_UNKNOWN	0		/* NOT Cardbus-PCI bridge */
#define	CB_TI113X	1		/* TI PCI1130/1131 */
#define	CB_TI12XX	2		/* TI PCI12xx/14xx/44xx/15xx/45xx */
#define	CB_TI125X	3		/* TI PCI1250/1251(B)/1450 */
#define	CB_RF5C47X	4		/* RICOH RF5C475/476/477 */
#define	CB_RF5C46X	5		/* RICOH RF5C465/466/467 */
#define	CB_CIRRUS	6		/* Cirrus Logic CLPD683x */
#define	CB_TOPIC95	7		/* Toshiba ToPIC95 */
#define	CB_TOPIC97	8		/* Toshiba ToPIC97/100 */
#define	CB_O2MICRO	9		/* O2Micro chips */
	SLIST_HEAD(, cbb_reslist) rl;
	device_t	cbdev;
	struct proc	*event_thread;
	void (*chipinit)(struct cbb_softc *);
	int	powerintr;
	struct root_hold_token *sc_root_token;
};

/* result of detect_card */
#define	CARD_UKN_CARD	0x00
#define	CARD_5V_CARD	0x01
#define	CARD_3V_CARD	0x02
#define	CARD_XV_CARD	0x04
#define	CARD_YV_CARD	0x08

/* for power_socket */
#define	CARD_VCC(X)	(X)
#define CARD_VPP_VCC	0xf0
#define CARD_VCCMASK	0xf
#define CARD_VCCSHIFT	0
#define XV		2
#define YV		1

#define CARD_OFF	(CARD_VCC(0))

extern int cbb_debug;
extern devclass_t cbb_devclass;

int	cbb_activate_resource(device_t brdev, device_t child,
	    int type, int rid, struct resource *r);
struct resource	*cbb_alloc_resource(device_t brdev, device_t child,
	    int type, int *rid, rman_res_t start, rman_res_t end, rman_res_t count,
	    u_int flags);
void	cbb_child_detached(device_t brdev, device_t child);
int	cbb_child_present(device_t parent, device_t child);
int	cbb_deactivate_resource(device_t brdev, device_t child,
	    int type, int rid, struct resource *r);
int	cbb_detach(device_t brdev);
void	cbb_disable_func_intr(struct cbb_softc *sc);
void	cbb_driver_added(device_t brdev, driver_t *driver);
void	cbb_event_thread(void *arg);
int	cbb_pcic_set_memory_offset(device_t brdev, device_t child, int rid,
	    uint32_t cardaddr, uint32_t *deltap);
int	cbb_pcic_set_res_flags(device_t brdev, device_t child, int type,
	    int rid, u_long flags);
int	cbb_power(device_t brdev, int volts);
int	cbb_power_enable_socket(device_t brdev, device_t child);
int	cbb_power_disable_socket(device_t brdev, device_t child);
int	cbb_read_ivar(device_t brdev, device_t child, int which,
	    uintptr_t *result);
int	cbb_release_resource(device_t brdev, device_t child,
	    int type, int rid, struct resource *r);
int	cbb_setup_intr(device_t dev, device_t child, struct resource *irq,
	    int flags, driver_filter_t *filt, driver_intr_t *intr, void *arg,
	    void **cookiep);
int	cbb_teardown_intr(device_t dev, device_t child, struct resource *irq,
	    void *cookie);
int	cbb_write_ivar(device_t brdev, device_t child, int which,
	    uintptr_t value);

/*
 */
static __inline void
cbb_set(struct cbb_softc *sc, uint32_t reg, uint32_t val)
{
	bus_space_write_4(sc->bst, sc->bsh, reg, val);
}

static __inline uint32_t
cbb_get(struct cbb_softc *sc, uint32_t reg)
{
	return (bus_space_read_4(sc->bst, sc->bsh, reg));
}

static __inline void
cbb_setb(struct cbb_softc *sc, uint32_t reg, uint32_t bits)
{
	cbb_set(sc, reg, cbb_get(sc, reg) | bits);
}

static __inline void
cbb_clrb(struct cbb_softc *sc, uint32_t reg, uint32_t bits)
{
	cbb_set(sc, reg, cbb_get(sc, reg) & ~bits);
}
