/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2004 M. Warner Losh.
 * Copyright (c) 2000-2001 Jonathan Chen.
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
 */

/*-
 * Copyright (c) 1998, 1999 and 2000
 *      HAYAKAWA Koichi.  All rights reserved.
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
 *	This product includes software developed by HAYAKAWA Koichi.
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

/*
 * Driver for PCI to CardBus Bridge chips
 * and PCI to PCMCIA Bridge chips
 * and ISA to PCMCIA host adapters
 * and C Bus to PCMCIA host adapters
 *
 * References:
 *  TI Datasheets:
 *   http://www-s.ti.com/cgi-bin/sc/generic2.cgi?family=PCI+CARDBUS+CONTROLLERS
 *
 * Written by Jonathan Chen <jon@freebsd.org>
 * The author would like to acknowledge:
 *  * HAYAKAWA Koichi: Author of the NetBSD code for the same thing
 *  * Warner Losh: Newbus/newcard guru and author of the pccard side of things
 *  * YAMAMOTO Shigeru: Author of another FreeBSD cardbus driver
 *  * David Cross: Author of the initial ugly hack for a specific cardbus card
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcib_private.h>

#include <dev/pccard/pccardreg.h>
#include <dev/pccard/pccardvar.h>

#include <dev/exca/excareg.h>
#include <dev/exca/excavar.h>

#include <dev/pccbb/pccbbreg.h>
#include <dev/pccbb/pccbbvar.h>

#include "power_if.h"
#include "card_if.h"
#include "pcib_if.h"

#define	DPRINTF(x) do { if (cbb_debug) printf x; } while (0)
#define	DEVPRINTF(x) do { if (cbb_debug) device_printf x; } while (0)

#define	PCI_MASK_CONFIG(DEV,REG,MASK,SIZE)				\
	pci_write_config(DEV, REG, pci_read_config(DEV, REG, SIZE) MASK, SIZE)
#define	PCI_MASK2_CONFIG(DEV,REG,MASK1,MASK2,SIZE)			\
	pci_write_config(DEV, REG, (					\
		pci_read_config(DEV, REG, SIZE) MASK1) MASK2, SIZE)

#define CBB_CARD_PRESENT(s) ((s & CBB_STATE_CD) == 0)

#define CBB_START_MEM	0x88000000
#define CBB_START_32_IO 0x1000
#define CBB_START_16_IO 0x100

devclass_t cbb_devclass;

/* sysctl vars */
static SYSCTL_NODE(_hw, OID_AUTO, cbb, CTLFLAG_RD, 0, "CBB parameters");

/* There's no way to say TUNEABLE_LONG to get the right types */
u_long cbb_start_mem = CBB_START_MEM;
SYSCTL_ULONG(_hw_cbb, OID_AUTO, start_memory, CTLFLAG_RWTUN,
    &cbb_start_mem, CBB_START_MEM,
    "Starting address for memory allocations");

u_long cbb_start_16_io = CBB_START_16_IO;
SYSCTL_ULONG(_hw_cbb, OID_AUTO, start_16_io, CTLFLAG_RWTUN,
    &cbb_start_16_io, CBB_START_16_IO,
    "Starting ioport for 16-bit cards");

u_long cbb_start_32_io = CBB_START_32_IO;
SYSCTL_ULONG(_hw_cbb, OID_AUTO, start_32_io, CTLFLAG_RWTUN,
    &cbb_start_32_io, CBB_START_32_IO,
    "Starting ioport for 32-bit cards");

int cbb_debug = 0;
SYSCTL_INT(_hw_cbb, OID_AUTO, debug, CTLFLAG_RWTUN, &cbb_debug, 0,
    "Verbose cardbus bridge debugging");

static void	cbb_insert(struct cbb_softc *sc);
static void	cbb_removal(struct cbb_softc *sc);
static uint32_t	cbb_detect_voltage(device_t brdev);
static int	cbb_cardbus_reset_power(device_t brdev, device_t child, int on);
static int	cbb_cardbus_io_open(device_t brdev, int win, uint32_t start,
		    uint32_t end);
static int	cbb_cardbus_mem_open(device_t brdev, int win,
		    uint32_t start, uint32_t end);
static void	cbb_cardbus_auto_open(struct cbb_softc *sc, int type);
static int	cbb_cardbus_activate_resource(device_t brdev, device_t child,
		    int type, int rid, struct resource *res);
static int	cbb_cardbus_deactivate_resource(device_t brdev,
		    device_t child, int type, int rid, struct resource *res);
static struct resource	*cbb_cardbus_alloc_resource(device_t brdev,
		    device_t child, int type, int *rid, rman_res_t start,
		    rman_res_t end, rman_res_t count, u_int flags);
static int	cbb_cardbus_release_resource(device_t brdev, device_t child,
		    int type, int rid, struct resource *res);
static int	cbb_cardbus_power_enable_socket(device_t brdev,
		    device_t child);
static int	cbb_cardbus_power_disable_socket(device_t brdev,
		    device_t child);
static int	cbb_func_filt(void *arg);
static void	cbb_func_intr(void *arg);

static void
cbb_remove_res(struct cbb_softc *sc, struct resource *res)
{
	struct cbb_reslist *rle;

	SLIST_FOREACH(rle, &sc->rl, link) {
		if (rle->res == res) {
			SLIST_REMOVE(&sc->rl, rle, cbb_reslist, link);
			free(rle, M_DEVBUF);
			return;
		}
	}
}

static struct resource *
cbb_find_res(struct cbb_softc *sc, int type, int rid)
{
	struct cbb_reslist *rle;
	
	SLIST_FOREACH(rle, &sc->rl, link)
		if (SYS_RES_MEMORY == rle->type && rid == rle->rid)
			return (rle->res);
	return (NULL);
}

static void
cbb_insert_res(struct cbb_softc *sc, struct resource *res, int type,
    int rid)
{
	struct cbb_reslist *rle;

	/*
	 * Need to record allocated resource so we can iterate through
	 * it later.
	 */
	rle = malloc(sizeof(struct cbb_reslist), M_DEVBUF, M_NOWAIT);
	if (rle == NULL)
		panic("cbb_cardbus_alloc_resource: can't record entry!");
	rle->res = res;
	rle->type = type;
	rle->rid = rid;
	SLIST_INSERT_HEAD(&sc->rl, rle, link);
}

static void
cbb_destroy_res(struct cbb_softc *sc)
{
	struct cbb_reslist *rle;

	while ((rle = SLIST_FIRST(&sc->rl)) != NULL) {
		device_printf(sc->dev, "Danger Will Robinson: Resource "
		    "left allocated!  This is a bug... "
		    "(rid=%x, type=%d, addr=%jx)\n", rle->rid, rle->type,
		    rman_get_start(rle->res));
		SLIST_REMOVE_HEAD(&sc->rl, link);
		free(rle, M_DEVBUF);
	}
}

/*
 * Disable function interrupts by telling the bridge to generate IRQ1
 * interrupts.  These interrupts aren't really generated by the chip, since
 * IRQ1 is reserved.  Some chipsets assert INTA# inappropriately during
 * initialization, so this helps to work around the problem.
 *
 * XXX We can't do this workaround for all chipsets, because this
 * XXX causes interference with the keyboard because somechipsets will
 * XXX actually signal IRQ1 over their serial interrupt connections to
 * XXX the south bridge.  Disable it it for now.
 */
void
cbb_disable_func_intr(struct cbb_softc *sc)
{
#if 0
	uint8_t reg;

	reg = (exca_getb(&sc->exca[0], EXCA_INTR) & ~EXCA_INTR_IRQ_MASK) | 
	    EXCA_INTR_IRQ_RESERVED1;
	exca_putb(&sc->exca[0], EXCA_INTR, reg);
#endif
}

/*
 * Enable function interrupts.  We turn on function interrupts when the card
 * requests an interrupt.  The PCMCIA standard says that we should set
 * the lower 4 bits to 0 to route via PCI.  Note: we call this for both
 * CardBus and R2 (PC Card) cases, but it should have no effect on CardBus
 * cards.
 */
static void
cbb_enable_func_intr(struct cbb_softc *sc)
{
	uint8_t reg;

	reg = (exca_getb(&sc->exca[0], EXCA_INTR) & ~EXCA_INTR_IRQ_MASK) | 
	    EXCA_INTR_IRQ_NONE;
	exca_putb(&sc->exca[0], EXCA_INTR, reg);
	PCI_MASK_CONFIG(sc->dev, CBBR_BRIDGECTRL,
	    & ~CBBM_BRIDGECTRL_INTR_IREQ_ISA_EN, 2);
}

int
cbb_detach(device_t brdev)
{
	struct cbb_softc *sc = device_get_softc(brdev);
	device_t *devlist;
	int tmp, tries, error, numdevs;

	/*
	 * Before we delete the children (which we have to do because
	 * attach doesn't check for children busses correctly), we have
	 * to detach the children.  Even if we didn't need to delete the
	 * children, we have to detach them.
	 */
	error = bus_generic_detach(brdev);
	if (error != 0)
		return (error);

	/*
	 * Since the attach routine doesn't search for children before it
	 * attaches them to this device, we must delete them here in order
	 * for the kldload/unload case to work.  If we failed to do that, then
	 * we'd get duplicate devices when cbb.ko was reloaded.
	 */
	tries = 10;
	do {
		error = device_get_children(brdev, &devlist, &numdevs);
		if (error == 0)
			break;
		/*
		 * Try hard to cope with low memory.
		 */
		if (error == ENOMEM) {
			pause("cbbnomem", 1);
			continue;
		}
	} while (tries-- > 0);
	for (tmp = 0; tmp < numdevs; tmp++)
		device_delete_child(brdev, devlist[tmp]);
	free(devlist, M_TEMP);

	/* Turn off the interrupts */
	cbb_set(sc, CBB_SOCKET_MASK, 0);

	/* reset 16-bit pcmcia bus */
	exca_clrb(&sc->exca[0], EXCA_INTR, EXCA_INTR_RESET);

	/* turn off power */
	cbb_power(brdev, CARD_OFF);

	/* Ack the interrupt */
	cbb_set(sc, CBB_SOCKET_EVENT, 0xffffffff);

	/*
	 * Wait for the thread to die.  kproc_exit will do a wakeup
	 * on the event thread's struct proc * so that we know it is
	 * safe to proceed.  IF the thread is running, set the please
	 * die flag and wait for it to comply.  Since the wakeup on
	 * the event thread happens only in kproc_exit, we don't
	 * need to loop here.
	 */
	bus_teardown_intr(brdev, sc->irq_res, sc->intrhand);
	mtx_lock(&sc->mtx);
	sc->flags |= CBB_KTHREAD_DONE;
	while (sc->flags & CBB_KTHREAD_RUNNING) {
		DEVPRINTF((sc->dev, "Waiting for thread to die\n"));
		wakeup(&sc->intrhand);
		msleep(sc->event_thread, &sc->mtx, PWAIT, "cbbun", 0);
	}
	mtx_unlock(&sc->mtx);

	bus_release_resource(brdev, SYS_RES_IRQ, 0, sc->irq_res);
	bus_release_resource(brdev, SYS_RES_MEMORY, CBBR_SOCKBASE,
	    sc->base_res);
	mtx_destroy(&sc->mtx);
	return (0);
}

int
cbb_setup_intr(device_t dev, device_t child, struct resource *irq,
  int flags, driver_filter_t *filt, driver_intr_t *intr, void *arg,
   void **cookiep)
{
	struct cbb_intrhand *ih;
	struct cbb_softc *sc = device_get_softc(dev);
	int err;

	if (filt == NULL && intr == NULL)
		return (EINVAL);
	ih = malloc(sizeof(struct cbb_intrhand), M_DEVBUF, M_NOWAIT);
	if (ih == NULL)
		return (ENOMEM);
	*cookiep = ih;
	ih->filt = filt;
	ih->intr = intr;
	ih->arg = arg;
	ih->sc = sc;
	/*
	 * XXX need to turn on ISA interrupts, if we ever support them, but
	 * XXX for now that's all we need to do.
	 */
	err = BUS_SETUP_INTR(device_get_parent(dev), child, irq, flags,
	    filt ? cbb_func_filt : NULL, intr ? cbb_func_intr : NULL, ih,
	    &ih->cookie);
	if (err != 0) {
		free(ih, M_DEVBUF);
		return (err);
	}
	cbb_enable_func_intr(sc);
	sc->cardok = 1;
	return 0;
}

int
cbb_teardown_intr(device_t dev, device_t child, struct resource *irq,
    void *cookie)
{
	struct cbb_intrhand *ih;
	int err;

	/* XXX Need to do different things for ISA interrupts. */
	ih = (struct cbb_intrhand *) cookie;
	err = BUS_TEARDOWN_INTR(device_get_parent(dev), child, irq,
	    ih->cookie);
	if (err != 0)
		return (err);
	free(ih, M_DEVBUF);
	return (0);
}


void
cbb_driver_added(device_t brdev, driver_t *driver)
{
	struct cbb_softc *sc = device_get_softc(brdev);
	device_t *devlist;
	device_t dev;
	int tmp;
	int numdevs;
	int wake = 0;

	DEVICE_IDENTIFY(driver, brdev);
	tmp = device_get_children(brdev, &devlist, &numdevs);
	if (tmp != 0) {
		device_printf(brdev, "Cannot get children list, no reprobe\n");
		return;
	}
	for (tmp = 0; tmp < numdevs; tmp++) {
		dev = devlist[tmp];
		if (device_get_state(dev) == DS_NOTPRESENT &&
		    device_probe_and_attach(dev) == 0)
			wake++;
	}
	free(devlist, M_TEMP);

	if (wake > 0)
		wakeup(&sc->intrhand);
}

void
cbb_child_detached(device_t brdev, device_t child)
{
	struct cbb_softc *sc = device_get_softc(brdev);

	/* I'm not sure we even need this */
	if (child != sc->cbdev && child != sc->exca[0].pccarddev)
		device_printf(brdev, "Unknown child detached: %s\n",
		    device_get_nameunit(child));
}

/************************************************************************/
/* Kthreads								*/
/************************************************************************/

void
cbb_event_thread(void *arg)
{
	struct cbb_softc *sc = arg;
	uint32_t status;
	int err;
	int not_a_card = 0;

	/*
	 * We need to act as a power sequencer on startup.  Delay 2s/channel
	 * to ensure the other channels have had a chance to come up.  We likely
	 * should add a lock that's shared on a per-slot basis so that only
	 * one power event can happen per slot at a time.
	 */
	pause("cbbstart", hz * device_get_unit(sc->dev) * 2);
	mtx_lock(&sc->mtx);
	sc->flags |= CBB_KTHREAD_RUNNING;
	while ((sc->flags & CBB_KTHREAD_DONE) == 0) {
		mtx_unlock(&sc->mtx);
		status = cbb_get(sc, CBB_SOCKET_STATE);
		DPRINTF(("Status is 0x%x\n", status));
		if (!CBB_CARD_PRESENT(status)) {
			not_a_card = 0;		/* We know card type */
			cbb_removal(sc);
		} else if (status & CBB_STATE_NOT_A_CARD) {
			/*
			 * Up to 10 times, try to rescan the card when we see
			 * NOT_A_CARD.  10 is somehwat arbitrary.  When this
			 * pathology hits, there's a ~40% chance each try will
			 * fail.  10 tries takes about 5s and results in a
			 * 99.99% certainty of the results.
			 */
			if (not_a_card++ < 10) {
				DEVPRINTF((sc->dev,
				    "Not a card bit set, rescanning\n"));
				cbb_setb(sc, CBB_SOCKET_FORCE, CBB_FORCE_CV_TEST);
			} else {
				device_printf(sc->dev,
				    "Can't determine card type\n");
			}
		} else {
			not_a_card = 0;		/* We know card type */
			cbb_insert(sc);
		}

		/*
		 * First time through we need to tell mountroot that we're
		 * done.
		 */
		if (sc->sc_root_token) {
			root_mount_rel(sc->sc_root_token);
			sc->sc_root_token = NULL;
		}

		/*
		 * Wait until it has been 250ms since the last time we
		 * get an interrupt.  We handle the rest of the interrupt
		 * at the top of the loop.  Although we clear the bit in the
		 * ISR, we signal sc->cv from the detach path after we've
		 * set the CBB_KTHREAD_DONE bit, so we can't do a simple
		 * 250ms sleep here.
		 *
		 * In our ISR, we turn off the card changed interrupt.  Turn
		 * them back on here before we wait for them to happen.  We
		 * turn them on/off so that we can tolerate a large latency
		 * between the time we signal cbb_event_thread and it gets
		 * a chance to run.
		 */
		mtx_lock(&sc->mtx);
		cbb_setb(sc, CBB_SOCKET_MASK, CBB_SOCKET_MASK_CD | CBB_SOCKET_MASK_CSTS);
		msleep(&sc->intrhand, &sc->mtx, 0, "-", 0);
		err = 0;
		while (err != EWOULDBLOCK &&
		    (sc->flags & CBB_KTHREAD_DONE) == 0)
			err = msleep(&sc->intrhand, &sc->mtx, 0, "-", hz / 5);
	}
	DEVPRINTF((sc->dev, "Thread terminating\n"));
	sc->flags &= ~CBB_KTHREAD_RUNNING;
	mtx_unlock(&sc->mtx);
	kproc_exit(0);
}

/************************************************************************/
/* Insert/removal							*/
/************************************************************************/

static void
cbb_insert(struct cbb_softc *sc)
{
	uint32_t sockevent, sockstate;

	sockevent = cbb_get(sc, CBB_SOCKET_EVENT);
	sockstate = cbb_get(sc, CBB_SOCKET_STATE);

	DEVPRINTF((sc->dev, "card inserted: event=0x%08x, state=%08x\n",
	    sockevent, sockstate));

	if (sockstate & CBB_STATE_R2_CARD) {
		if (device_is_attached(sc->exca[0].pccarddev)) {
			sc->flags |= CBB_16BIT_CARD;
			exca_insert(&sc->exca[0]);
		} else {
			device_printf(sc->dev,
			    "16-bit card inserted, but no pccard bus.\n");
		}
	} else if (sockstate & CBB_STATE_CB_CARD) {
		if (device_is_attached(sc->cbdev)) {
			sc->flags &= ~CBB_16BIT_CARD;
			CARD_ATTACH_CARD(sc->cbdev);
		} else {
			device_printf(sc->dev,
			    "CardBus card inserted, but no cardbus bus.\n");
		}
	} else {
		/*
		 * We should power the card down, and try again a couple of
		 * times if this happens. XXX
		 */
		device_printf(sc->dev, "Unsupported card type detected\n");
	}
}

static void
cbb_removal(struct cbb_softc *sc)
{
	sc->cardok = 0;
	if (sc->flags & CBB_16BIT_CARD) {
		exca_removal(&sc->exca[0]);
	} else {
		if (device_is_attached(sc->cbdev))
			CARD_DETACH_CARD(sc->cbdev);
	}
	cbb_destroy_res(sc);
}

/************************************************************************/
/* Interrupt Handler							*/
/************************************************************************/

static int
cbb_func_filt(void *arg)
{
	struct cbb_intrhand *ih = (struct cbb_intrhand *)arg;
	struct cbb_softc *sc = ih->sc;

	/*
	 * Make sure that the card is really there.
	 */
	if (!sc->cardok)
		return (FILTER_STRAY);
	if (!CBB_CARD_PRESENT(cbb_get(sc, CBB_SOCKET_STATE))) {
		sc->cardok = 0;
		return (FILTER_HANDLED);
	}

	/*
	 * nb: don't have to check for giant or not, since that's done in the
	 * ISR dispatch and one can't hold Giant in a filter anyway...
	 */
	return ((*ih->filt)(ih->arg));	
}

static void
cbb_func_intr(void *arg)
{
	struct cbb_intrhand *ih = (struct cbb_intrhand *)arg;
	struct cbb_softc *sc = ih->sc;

	/*
	 * While this check may seem redundant, it helps close a race
	 * condition.  If the card is ejected after the filter runs, but
	 * before this ISR can be scheduled, then we need to do the same
	 * filtering to prevent the card's ISR from being called.  One could
	 * argue that the card's ISR should be able to cope, but experience
	 * has shown they can't always.  This mitigates the problem by making
	 * the race quite a bit smaller.  Properly written client ISRs should
	 * cope with the card going away in the middle of the ISR.  We assume
	 * that drivers that are sophisticated enough to use filters don't
	 * need our protection.  This also allows us to ensure they *ARE*
	 * called if their filter said they needed to be called.
	 */
	if (ih->filt == NULL) {
		if (!sc->cardok)
			return;
		if (!CBB_CARD_PRESENT(cbb_get(sc, CBB_SOCKET_STATE))) {
			sc->cardok = 0;
			return;
		}
	}

	/*
	 * Call the registered ithread interrupt handler.  This entire routine
	 * will be called with Giant if this isn't an MP safe driver, or not
	 * if it is.  Either way, we don't have to worry.
	 */
	ih->intr(ih->arg);
}

/************************************************************************/
/* Generic Power functions						*/
/************************************************************************/

static uint32_t
cbb_detect_voltage(device_t brdev)
{
	struct cbb_softc *sc = device_get_softc(brdev);
	uint32_t psr;
	uint32_t vol = CARD_UKN_CARD;

	psr = cbb_get(sc, CBB_SOCKET_STATE);

	if (psr & CBB_STATE_5VCARD && psr & CBB_STATE_5VSOCK)
		vol |= CARD_5V_CARD;
	if (psr & CBB_STATE_3VCARD && psr & CBB_STATE_3VSOCK)
		vol |= CARD_3V_CARD;
	if (psr & CBB_STATE_XVCARD && psr & CBB_STATE_XVSOCK)
		vol |= CARD_XV_CARD;
	if (psr & CBB_STATE_YVCARD && psr & CBB_STATE_YVSOCK)
		vol |= CARD_YV_CARD;

	return (vol);
}

static uint8_t
cbb_o2micro_power_hack(struct cbb_softc *sc)
{
	uint8_t reg;

	/*
	 * Issue #2: INT# not qualified with IRQ Routing Bit.  An
	 * unexpected PCI INT# may be generated during PC Card
	 * initialization even with the IRQ Routing Bit Set with some
	 * PC Cards.
	 *
	 * This is a two part issue.  The first part is that some of
	 * our older controllers have an issue in which the slot's PCI
	 * INT# is NOT qualified by the IRQ routing bit (PCI reg. 3Eh
	 * bit 7).  Regardless of the IRQ routing bit, if NO ISA IRQ
	 * is selected (ExCA register 03h bits 3:0, of the slot, are
	 * cleared) we will generate INT# if IREQ# is asserted.  The
	 * second part is because some PC Cards prematurally assert
	 * IREQ# before the ExCA registers are fully programmed.  This
	 * in turn asserts INT# because ExCA register 03h bits 3:0
	 * (ISA IRQ Select) are not yet programmed.
	 *
	 * The fix for this issue, which will work for any controller
	 * (old or new), is to set ExCA register 03h bits 3:0 = 0001b
	 * (select IRQ1), of the slot, before turning on slot power.
	 * Selecting IRQ1 will result in INT# NOT being asserted
	 * (because IRQ1 is selected), and IRQ1 won't be asserted
	 * because our controllers don't generate IRQ1.
	 *
	 * Other, non O2Micro controllers will generate irq 1 in some
	 * situations, so we can't do this hack for everybody.  Reports of
	 * keyboard controller's interrupts being suppressed occurred when
	 * we did this.
	 */
	reg = exca_getb(&sc->exca[0], EXCA_INTR);
	exca_putb(&sc->exca[0], EXCA_INTR, (reg & 0xf0) | 1);
	return (reg);
}

/*
 * Restore the damage that cbb_o2micro_power_hack does to EXCA_INTR so
 * we don't have an interrupt storm on power on.  This has the effect of
 * disabling card status change interrupts for the duration of poweron.
 */
static void
cbb_o2micro_power_hack2(struct cbb_softc *sc, uint8_t reg)
{
	exca_putb(&sc->exca[0], EXCA_INTR, reg);
}

int
cbb_power(device_t brdev, int volts)
{
	uint32_t status, sock_ctrl, reg_ctrl, mask;
	struct cbb_softc *sc = device_get_softc(brdev);
	int cnt, sane;
	int retval = 0;
	int on = 0;
	uint8_t reg = 0;

	sock_ctrl = cbb_get(sc, CBB_SOCKET_CONTROL);

	sock_ctrl &= ~CBB_SOCKET_CTRL_VCCMASK;
	switch (volts & CARD_VCCMASK) {
	case 5:
		sock_ctrl |= CBB_SOCKET_CTRL_VCC_5V;
		on++;
		break;
	case 3:
		sock_ctrl |= CBB_SOCKET_CTRL_VCC_3V;
		on++;
		break;
	case XV:
		sock_ctrl |= CBB_SOCKET_CTRL_VCC_XV;
		on++;
		break;
	case YV:
		sock_ctrl |= CBB_SOCKET_CTRL_VCC_YV;
		on++;
		break;
	case 0:
		break;
	default:
		return (0);			/* power NEVER changed */
	}

	/* VPP == VCC */
	sock_ctrl &= ~CBB_SOCKET_CTRL_VPPMASK;
	sock_ctrl |= ((sock_ctrl >> 4) & 0x07);

	if (cbb_get(sc, CBB_SOCKET_CONTROL) == sock_ctrl)
		return (1); /* no change necessary */
	DEVPRINTF((sc->dev, "cbb_power: %dV\n", volts));
	if (volts != 0 && sc->chipset == CB_O2MICRO)
		reg = cbb_o2micro_power_hack(sc);

	/*
	 * We have to mask the card change detect interrupt while we're
	 * messing with the power.  It is allowed to bounce while we're
	 * messing with power as things settle down.  In addition, we mask off
	 * the card's function interrupt by routing it via the ISA bus.  This
	 * bit generally only affects 16-bit cards.  Some bridges allow one to
	 * set another bit to have it also affect 32-bit cards.  Since 32-bit
	 * cards are required to be better behaved, we don't bother to get
	 * into those bridge specific features.
	 *
	 * XXX I wonder if we need to enable the READY bit interrupt in the
	 * EXCA CSC register for 16-bit cards, and disable the CD bit?
	 */
	mask = cbb_get(sc, CBB_SOCKET_MASK);
	mask |= CBB_SOCKET_MASK_POWER;
	mask &= ~CBB_SOCKET_MASK_CD;
	cbb_set(sc, CBB_SOCKET_MASK, mask);
	PCI_MASK_CONFIG(brdev, CBBR_BRIDGECTRL,
	    |CBBM_BRIDGECTRL_INTR_IREQ_ISA_EN, 2);
	cbb_set(sc, CBB_SOCKET_CONTROL, sock_ctrl);
	if (on) {
		mtx_lock(&sc->mtx);
		cnt = sc->powerintr;
		/*
		 * We have a shortish timeout of 500ms here.  Some bridges do
		 * not generate a POWER_CYCLE event for 16-bit cards.  In
		 * those cases, we have to cope the best we can, and having
		 * only a short delay is better than the alternatives.  Others
		 * raise the power cycle a smidge before it is really ready.
		 * We deal with those below.
		 */
		sane = 10;
		while (!(cbb_get(sc, CBB_SOCKET_STATE) & CBB_STATE_POWER_CYCLE) &&
		    cnt == sc->powerintr && sane-- > 0)
			msleep(&sc->powerintr, &sc->mtx, 0, "-", hz / 20);
		mtx_unlock(&sc->mtx);

		/*
		 * Relax for 100ms.  Some bridges appear to assert this signal
		 * right away, but before the card has stabilized.  Other
		 * cards need need more time to cope up reliabily.
		 * Experiments with troublesome setups show this to be a
		 * "cheap" way to enhance reliabilty.  We need not do this for
		 * "off" since we don't touch the card after we turn it off.
		 */
		pause("cbbPwr", min(hz / 10, 1));

		/*
		 * The TOPIC95B requires a little bit extra time to get its
		 * act together, so delay for an additional 100ms.  Also as
		 * documented below, it doesn't seem to set the POWER_CYCLE
		 * bit, so don't whine if it never came on.
		 */
		if (sc->chipset == CB_TOPIC95)
			pause("cbb95B", hz / 10);
		else if (sane <= 0)
			device_printf(sc->dev, "power timeout, doom?\n");
	}

	/*
	 * After the power is good, we can turn off the power interrupt.
	 * However, the PC Card standard says that we must delay turning the
	 * CD bit back on for a bit to allow for bouncyness on power down
	 * (recall that we don't wait above for a power down, since we don't
	 * get an interrupt for that).  We're called either from the suspend
	 * code in which case we don't want to turn card change on again, or
	 * we're called from the card insertion code, in which case the cbb
	 * thread will turn it on for us before it waits to be woken by a
	 * change event.
	 *
	 * NB: Topic95B doesn't set the power cycle bit.  we assume that
	 * both it and the TOPIC95 behave the same.
	 */
	cbb_clrb(sc, CBB_SOCKET_MASK, CBB_SOCKET_MASK_POWER);
	status = cbb_get(sc, CBB_SOCKET_STATE);
	if (on && sc->chipset != CB_TOPIC95) {
		if ((status & CBB_STATE_POWER_CYCLE) == 0)
			device_printf(sc->dev, "Power not on?\n");
	}
	if (status & CBB_STATE_BAD_VCC_REQ) {
		device_printf(sc->dev, "Bad Vcc requested\n");	
		/*
		 * Turn off the power, and try again.  Retrigger other
		 * active interrupts via force register.  From NetBSD
		 * PR 36652, coded by me to description there.
		 */
		sock_ctrl &= ~CBB_SOCKET_CTRL_VCCMASK;
		sock_ctrl &= ~CBB_SOCKET_CTRL_VPPMASK;
		cbb_set(sc, CBB_SOCKET_CONTROL, sock_ctrl);
		status &= ~CBB_STATE_BAD_VCC_REQ;
		status &= ~CBB_STATE_DATA_LOST;
		status |= CBB_FORCE_CV_TEST;
		cbb_set(sc, CBB_SOCKET_FORCE, status);
		goto done;
	}
	if (sc->chipset == CB_TOPIC97) {
		reg_ctrl = pci_read_config(sc->dev, TOPIC_REG_CTRL, 4);
		reg_ctrl &= ~TOPIC97_REG_CTRL_TESTMODE;
		if (on)
			reg_ctrl |= TOPIC97_REG_CTRL_CLKRUN_ENA;
		else
			reg_ctrl &= ~TOPIC97_REG_CTRL_CLKRUN_ENA;
		pci_write_config(sc->dev, TOPIC_REG_CTRL, reg_ctrl, 4);
	}
	retval = 1;
done:;
	if (volts != 0 && sc->chipset == CB_O2MICRO)
		cbb_o2micro_power_hack2(sc, reg);
	return (retval);
}

static int
cbb_current_voltage(device_t brdev)
{
	struct cbb_softc *sc = device_get_softc(brdev);
	uint32_t ctrl;
	
	ctrl = cbb_get(sc, CBB_SOCKET_CONTROL);
	switch (ctrl & CBB_SOCKET_CTRL_VCCMASK) {
	case CBB_SOCKET_CTRL_VCC_5V:
		return CARD_5V_CARD;
	case CBB_SOCKET_CTRL_VCC_3V:
		return CARD_3V_CARD;
	case CBB_SOCKET_CTRL_VCC_XV:
		return CARD_XV_CARD;
	case CBB_SOCKET_CTRL_VCC_YV:
		return CARD_YV_CARD;
	}
	return 0;
}

/*
 * detect the voltage for the card, and set it.  Since the power
 * used is the square of the voltage, lower voltages is a big win
 * and what Windows does (and what Microsoft prefers).  The MS paper
 * also talks about preferring the CIS entry as well, but that has
 * to be done elsewhere.  We also optimize power sequencing here
 * and don't change things if we're already powered up at a supported
 * voltage.
 *
 * In addition, we power up with OE disabled.  We'll set it later
 * in the power up sequence.
 */
static int
cbb_do_power(device_t brdev)
{
	struct cbb_softc *sc = device_get_softc(brdev);
	uint32_t voltage, curpwr;
	uint32_t status;

	/* Don't enable OE (output enable) until power stable */
	exca_clrb(&sc->exca[0], EXCA_PWRCTL, EXCA_PWRCTL_OE);

	voltage = cbb_detect_voltage(brdev);
	curpwr = cbb_current_voltage(brdev);
	status = cbb_get(sc, CBB_SOCKET_STATE);
	if ((status & CBB_STATE_POWER_CYCLE) && (voltage & curpwr))
		return 0;
	/* Prefer lowest voltage supported */
	cbb_power(brdev, CARD_OFF);
	if (voltage & CARD_YV_CARD)
		cbb_power(brdev, CARD_VCC(YV));
	else if (voltage & CARD_XV_CARD)
		cbb_power(brdev, CARD_VCC(XV));
	else if (voltage & CARD_3V_CARD)
		cbb_power(brdev, CARD_VCC(3));
	else if (voltage & CARD_5V_CARD)
		cbb_power(brdev, CARD_VCC(5));
	else {
		device_printf(brdev, "Unknown card voltage\n");
		return (ENXIO);
	}
	return (0);
}

/************************************************************************/
/* CardBus power functions						*/
/************************************************************************/

static int
cbb_cardbus_reset_power(device_t brdev, device_t child, int on)
{
	struct cbb_softc *sc = device_get_softc(brdev);
	uint32_t b, h;
	int delay, count, zero_seen, func;

	/*
	 * Asserting reset for 20ms is necessary for most bridges.  For some
	 * reason, the Ricoh RF5C47x bridges need it asserted for 400ms.  The
	 * root cause of this is unknown, and NetBSD does the same thing.
	 */
	delay = sc->chipset == CB_RF5C47X ? 400 : 20;
	PCI_MASK_CONFIG(brdev, CBBR_BRIDGECTRL, |CBBM_BRIDGECTRL_RESET, 2);
	pause("cbbP3", hz * delay / 1000);

	/*
	 * If a card exists and we're turning it on, take it out of reset.
	 * After clearing reset, wait up to 1.1s for the first configuration
	 * register (vendor/product) configuration register of device 0.0 to
	 * become != 0xffffffff.  The PCMCIA PC Card Host System Specification
	 * says that when powering up the card, the PCI Spec v2.1 must be
	 * followed.  In PCI spec v2.2 Table 4-6, Trhfa (Reset High to first
	 * Config Access) is at most 2^25 clocks, or just over 1s.  Section
	 * 2.2.1 states any card not ready to participate in bus transactions
	 * must tristate its outputs.  Therefore, any access to its
	 * configuration registers must be ignored.  In that state, the config
	 * reg will read 0xffffffff.  Section 6.2.1 states a vendor id of
	 * 0xffff is invalid, so this can never match a real card.  Print a
	 * warning if it never returns a real id.  The PCMCIA PC Card
	 * Electrical Spec Section 5.2.7.1 implies only device 0 is present on
	 * a cardbus bus, so that's the only register we check here.
	 */
	if (on && CBB_CARD_PRESENT(cbb_get(sc, CBB_SOCKET_STATE))) {
		PCI_MASK_CONFIG(brdev, CBBR_BRIDGECTRL,
		    &~CBBM_BRIDGECTRL_RESET, 2);
		b = pcib_get_bus(child);
		count = 1100 / 20;
		do {
			pause("cbbP4", hz * 2 / 100);
		} while (PCIB_READ_CONFIG(brdev, b, 0, 0, PCIR_DEVVENDOR, 4) ==
		    0xfffffffful && --count >= 0);
		if (count < 0)
			device_printf(brdev, "Warning: Bus reset timeout\n");

		/*
		 * Some cards (so far just an atheros card I have) seem to
		 * come out of reset in a funky state. They report they are
		 * multi-function cards, but have nonsense for some of the
		 * higher functions.  So if the card claims to be MFDEV, and
		 * any of the higher functions' ID is 0, then we've hit the
		 * bug and we'll try again.
		 */
		h = PCIB_READ_CONFIG(brdev, b, 0, 0, PCIR_HDRTYPE, 1);
		if ((h & PCIM_MFDEV) == 0)
			return 0;
		zero_seen = 0;
		for (func = 1; func < 8; func++) {
			h = PCIB_READ_CONFIG(brdev, b, 0, func,
			    PCIR_DEVVENDOR, 4);
			if (h == 0)
				zero_seen++;
		}
		if (!zero_seen)
			return 0;
		return (EINVAL);
	}
	return 0;
}

static int
cbb_cardbus_power_disable_socket(device_t brdev, device_t child)
{
	cbb_power(brdev, CARD_OFF);
	cbb_cardbus_reset_power(brdev, child, 0);
	return (0);
}

static int
cbb_cardbus_power_enable_socket(device_t brdev, device_t child)
{
	struct cbb_softc *sc = device_get_softc(brdev);
	int err, count;

	if (!CBB_CARD_PRESENT(cbb_get(sc, CBB_SOCKET_STATE)))
		return (ENODEV);

	count = 10;
	do {
		err = cbb_do_power(brdev);
		if (err)
			return (err);
		err = cbb_cardbus_reset_power(brdev, child, 1);
		if (err) {
			device_printf(brdev, "Reset failed, trying again.\n");
			cbb_cardbus_power_disable_socket(brdev, child);
			pause("cbbErr1", hz / 10); /* wait 100ms */
		}
	} while (err != 0 && count-- > 0);
	return (0);
}

/************************************************************************/
/* CardBus Resource							*/
/************************************************************************/

static void
cbb_activate_window(device_t brdev, int type)
{

	PCI_ENABLE_IO(device_get_parent(brdev), brdev, type);
}

static int
cbb_cardbus_io_open(device_t brdev, int win, uint32_t start, uint32_t end)
{
	int basereg;
	int limitreg;

	if ((win < 0) || (win > 1)) {
		DEVPRINTF((brdev,
		    "cbb_cardbus_io_open: window out of range %d\n", win));
		return (EINVAL);
	}

	basereg = win * 8 + CBBR_IOBASE0;
	limitreg = win * 8 + CBBR_IOLIMIT0;

	pci_write_config(brdev, basereg, start, 4);
	pci_write_config(brdev, limitreg, end, 4);
	cbb_activate_window(brdev, SYS_RES_IOPORT);
	return (0);
}

static int
cbb_cardbus_mem_open(device_t brdev, int win, uint32_t start, uint32_t end)
{
	int basereg;
	int limitreg;

	if ((win < 0) || (win > 1)) {
		DEVPRINTF((brdev,
		    "cbb_cardbus_mem_open: window out of range %d\n", win));
		return (EINVAL);
	}

	basereg = win * 8 + CBBR_MEMBASE0;
	limitreg = win * 8 + CBBR_MEMLIMIT0;

	pci_write_config(brdev, basereg, start, 4);
	pci_write_config(brdev, limitreg, end, 4);
	cbb_activate_window(brdev, SYS_RES_MEMORY);
	return (0);
}

#define START_NONE 0xffffffff
#define END_NONE 0

static void
cbb_cardbus_auto_open(struct cbb_softc *sc, int type)
{
	uint32_t starts[2];
	uint32_t ends[2];
	struct cbb_reslist *rle;
	int align, i;
	uint32_t reg;

	starts[0] = starts[1] = START_NONE;
	ends[0] = ends[1] = END_NONE;

	if (type == SYS_RES_MEMORY)
		align = CBB_MEMALIGN;
	else if (type == SYS_RES_IOPORT)
		align = CBB_IOALIGN;
	else
		align = 1;

	SLIST_FOREACH(rle, &sc->rl, link) {
		if (rle->type != type)
			continue;
		if (rle->res == NULL)
			continue;
		if (!(rman_get_flags(rle->res) & RF_ACTIVE))
			continue;
		if (rman_get_flags(rle->res) & RF_PREFETCHABLE)
			i = 1;
		else
			i = 0;
		if (rman_get_start(rle->res) < starts[i])
			starts[i] = rman_get_start(rle->res);
		if (rman_get_end(rle->res) > ends[i])
			ends[i] = rman_get_end(rle->res);
	}
	for (i = 0; i < 2; i++) {
		if (starts[i] == START_NONE)
			continue;
		starts[i] &= ~(align - 1);
		ends[i] = roundup2(ends[i], align) - 1;
	}
	if (starts[0] != START_NONE && starts[1] != START_NONE) {
		if (starts[0] < starts[1]) {
			if (ends[0] > starts[1]) {
				device_printf(sc->dev, "Overlapping ranges"
				    " for prefetch and non-prefetch memory\n");
				return;
			}
		} else {
			if (ends[1] > starts[0]) {
				device_printf(sc->dev, "Overlapping ranges"
				    " for prefetch and non-prefetch memory\n");
				return;
			}
		}
	}

	if (type == SYS_RES_MEMORY) {
		cbb_cardbus_mem_open(sc->dev, 0, starts[0], ends[0]);
		cbb_cardbus_mem_open(sc->dev, 1, starts[1], ends[1]);
		reg = pci_read_config(sc->dev, CBBR_BRIDGECTRL, 2);
		reg &= ~(CBBM_BRIDGECTRL_PREFETCH_0 |
		    CBBM_BRIDGECTRL_PREFETCH_1);
		if (starts[1] != START_NONE)
			reg |= CBBM_BRIDGECTRL_PREFETCH_1;
		pci_write_config(sc->dev, CBBR_BRIDGECTRL, reg, 2);
		if (bootverbose) {
			device_printf(sc->dev, "Opening memory:\n");
			if (starts[0] != START_NONE)
				device_printf(sc->dev, "Normal: %#x-%#x\n",
				    starts[0], ends[0]);
			if (starts[1] != START_NONE)
				device_printf(sc->dev, "Prefetch: %#x-%#x\n",
				    starts[1], ends[1]);
		}
	} else if (type == SYS_RES_IOPORT) {
		cbb_cardbus_io_open(sc->dev, 0, starts[0], ends[0]);
		cbb_cardbus_io_open(sc->dev, 1, starts[1], ends[1]);
		if (bootverbose && starts[0] != START_NONE)
			device_printf(sc->dev, "Opening I/O: %#x-%#x\n",
			    starts[0], ends[0]);
	}
}

static int
cbb_cardbus_activate_resource(device_t brdev, device_t child, int type,
    int rid, struct resource *res)
{
	int ret;

	ret = BUS_ACTIVATE_RESOURCE(device_get_parent(brdev), child,
	    type, rid, res);
	if (ret != 0)
		return (ret);
	cbb_cardbus_auto_open(device_get_softc(brdev), type);
	return (0);
}

static int
cbb_cardbus_deactivate_resource(device_t brdev, device_t child, int type,
    int rid, struct resource *res)
{
	int ret;

	ret = BUS_DEACTIVATE_RESOURCE(device_get_parent(brdev), child,
	    type, rid, res);
	if (ret != 0)
		return (ret);
	cbb_cardbus_auto_open(device_get_softc(brdev), type);
	return (0);
}

static struct resource *
cbb_cardbus_alloc_resource(device_t brdev, device_t child, int type,
    int *rid, rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct cbb_softc *sc = device_get_softc(brdev);
	int tmp;
	struct resource *res;
	rman_res_t align;

	switch (type) {
	case SYS_RES_IRQ:
		tmp = rman_get_start(sc->irq_res);
		if (start > tmp || end < tmp || count != 1) {
			device_printf(child, "requested interrupt %jd-%jd,"
			    "count = %jd not supported by cbb\n",
			    start, end, count);
			return (NULL);
		}
		start = end = tmp;
		flags |= RF_SHAREABLE;
		break;
	case SYS_RES_IOPORT:
		if (start <= cbb_start_32_io)
			start = cbb_start_32_io;
		if (end < start)
			end = start;
		if (count > (1 << RF_ALIGNMENT(flags)))
			flags = (flags & ~RF_ALIGNMENT_MASK) | 
			    rman_make_alignment_flags(count);
		break;
	case SYS_RES_MEMORY:
		if (start <= cbb_start_mem)
			start = cbb_start_mem;
		if (end < start)
			end = start;
		if (count < CBB_MEMALIGN)
			align = CBB_MEMALIGN;
		else
			align = count;
		if (align > (1 << RF_ALIGNMENT(flags)))
			flags = (flags & ~RF_ALIGNMENT_MASK) | 
			    rman_make_alignment_flags(align);
		break;
	}
	res = BUS_ALLOC_RESOURCE(device_get_parent(brdev), child, type, rid,
	    start, end, count, flags & ~RF_ACTIVE);
	if (res == NULL) {
		printf("cbb alloc res fail type %d rid %x\n", type, *rid);
		return (NULL);
	}
	cbb_insert_res(sc, res, type, *rid);
	if (flags & RF_ACTIVE)
		if (bus_activate_resource(child, type, *rid, res) != 0) {
			bus_release_resource(child, type, *rid, res);
			return (NULL);
		}

	return (res);
}

static int
cbb_cardbus_release_resource(device_t brdev, device_t child, int type,
    int rid, struct resource *res)
{
	struct cbb_softc *sc = device_get_softc(brdev);
	int error;

	if (rman_get_flags(res) & RF_ACTIVE) {
		error = bus_deactivate_resource(child, type, rid, res);
		if (error != 0)
			return (error);
	}
	cbb_remove_res(sc, res);
	return (BUS_RELEASE_RESOURCE(device_get_parent(brdev), child,
	    type, rid, res));
}

/************************************************************************/
/* PC Card Power Functions						*/
/************************************************************************/

static int
cbb_pcic_power_enable_socket(device_t brdev, device_t child)
{
	struct cbb_softc *sc = device_get_softc(brdev);
	int err;

	DPRINTF(("cbb_pcic_socket_enable:\n"));

	/* power down/up the socket to reset */
	err = cbb_do_power(brdev);
	if (err)
		return (err);
	exca_reset(&sc->exca[0], child);

	return (0);
}

static int
cbb_pcic_power_disable_socket(device_t brdev, device_t child)
{
	struct cbb_softc *sc = device_get_softc(brdev);

	DPRINTF(("cbb_pcic_socket_disable\n"));

	/* Turn off the card's interrupt and leave it in reset, wait 10ms */
	exca_putb(&sc->exca[0], EXCA_INTR, 0);
	pause("cbbP1", hz / 100);

	/* power down the socket */
	cbb_power(brdev, CARD_OFF);
	exca_putb(&sc->exca[0], EXCA_PWRCTL, 0);

	/* wait 300ms until power fails (Tpf). */
	pause("cbbP2", hz * 300 / 1000);

	/* enable CSC interrupts */
	exca_putb(&sc->exca[0], EXCA_INTR, EXCA_INTR_ENABLE);
	return (0);
}

/************************************************************************/
/* POWER methods							*/
/************************************************************************/

int
cbb_power_enable_socket(device_t brdev, device_t child)
{
	struct cbb_softc *sc = device_get_softc(brdev);

	if (sc->flags & CBB_16BIT_CARD)
		return (cbb_pcic_power_enable_socket(brdev, child));
	return (cbb_cardbus_power_enable_socket(brdev, child));
}

int
cbb_power_disable_socket(device_t brdev, device_t child)
{
	struct cbb_softc *sc = device_get_softc(brdev);
	if (sc->flags & CBB_16BIT_CARD)
		return (cbb_pcic_power_disable_socket(brdev, child));
	return (cbb_cardbus_power_disable_socket(brdev, child));
}

static int
cbb_pcic_activate_resource(device_t brdev, device_t child, int type, int rid,
    struct resource *res)
{
	struct cbb_softc *sc = device_get_softc(brdev);
	int error;

	error = exca_activate_resource(&sc->exca[0], child, type, rid, res);
	if (error == 0)
		cbb_activate_window(brdev, type);
	return (error);
}

static int
cbb_pcic_deactivate_resource(device_t brdev, device_t child, int type,
    int rid, struct resource *res)
{
	struct cbb_softc *sc = device_get_softc(brdev);
	return (exca_deactivate_resource(&sc->exca[0], child, type, rid, res));
}

static struct resource *
cbb_pcic_alloc_resource(device_t brdev, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct resource *res = NULL;
	struct cbb_softc *sc = device_get_softc(brdev);
	int align;
	int tmp;

	switch (type) {
	case SYS_RES_MEMORY:
		if (start < cbb_start_mem)
			start = cbb_start_mem;
		if (end < start)
			end = start;
		if (count < CBB_MEMALIGN)
			align = CBB_MEMALIGN;
		else
			align = count;
		if (align > (1 << RF_ALIGNMENT(flags)))
			flags = (flags & ~RF_ALIGNMENT_MASK) | 
			    rman_make_alignment_flags(align);
		break;
	case SYS_RES_IOPORT:
		if (start < cbb_start_16_io)
			start = cbb_start_16_io;
		if (end < start)
			end = start;
		break;
	case SYS_RES_IRQ:
		tmp = rman_get_start(sc->irq_res);
		if (start > tmp || end < tmp || count != 1) {
			device_printf(child, "requested interrupt %jd-%jd,"
			    "count = %jd not supported by cbb\n",
			    start, end, count);
			return (NULL);
		}
		flags |= RF_SHAREABLE;
		start = end = rman_get_start(sc->irq_res);
		break;
	}
	res = BUS_ALLOC_RESOURCE(device_get_parent(brdev), child, type, rid,
	    start, end, count, flags & ~RF_ACTIVE);
	if (res == NULL)
		return (NULL);
	cbb_insert_res(sc, res, type, *rid);
	if (flags & RF_ACTIVE) {
		if (bus_activate_resource(child, type, *rid, res) != 0) {
			bus_release_resource(child, type, *rid, res);
			return (NULL);
		}
	}

	return (res);
}

static int
cbb_pcic_release_resource(device_t brdev, device_t child, int type,
    int rid, struct resource *res)
{
	struct cbb_softc *sc = device_get_softc(brdev);
	int error;

	if (rman_get_flags(res) & RF_ACTIVE) {
		error = bus_deactivate_resource(child, type, rid, res);
		if (error != 0)
			return (error);
	}
	cbb_remove_res(sc, res);
	return (BUS_RELEASE_RESOURCE(device_get_parent(brdev), child,
	    type, rid, res));
}

/************************************************************************/
/* PC Card methods							*/
/************************************************************************/

int
cbb_pcic_set_res_flags(device_t brdev, device_t child, int type, int rid,
    u_long flags)
{
	struct cbb_softc *sc = device_get_softc(brdev);
	struct resource *res;

	if (type != SYS_RES_MEMORY)
		return (EINVAL);
	res = cbb_find_res(sc, type, rid);
	if (res == NULL) {
		device_printf(brdev,
		    "set_res_flags: specified rid not found\n");
		return (ENOENT);
	}
	return (exca_mem_set_flags(&sc->exca[0], res, flags));
}

int
cbb_pcic_set_memory_offset(device_t brdev, device_t child, int rid,
    uint32_t cardaddr, uint32_t *deltap)
{
	struct cbb_softc *sc = device_get_softc(brdev);
	struct resource *res;

	res = cbb_find_res(sc, SYS_RES_MEMORY, rid);
	if (res == NULL) {
		device_printf(brdev,
		    "set_memory_offset: specified rid not found\n");
		return (ENOENT);
	}
	return (exca_mem_set_offset(&sc->exca[0], res, cardaddr, deltap));
}

/************************************************************************/
/* BUS Methods								*/
/************************************************************************/


int
cbb_activate_resource(device_t brdev, device_t child, int type, int rid,
    struct resource *r)
{
	struct cbb_softc *sc = device_get_softc(brdev);

	if (sc->flags & CBB_16BIT_CARD)
		return (cbb_pcic_activate_resource(brdev, child, type, rid, r));
	else
		return (cbb_cardbus_activate_resource(brdev, child, type, rid,
		    r));
}

int
cbb_deactivate_resource(device_t brdev, device_t child, int type,
    int rid, struct resource *r)
{
	struct cbb_softc *sc = device_get_softc(brdev);

	if (sc->flags & CBB_16BIT_CARD)
		return (cbb_pcic_deactivate_resource(brdev, child, type,
		    rid, r));
	else
		return (cbb_cardbus_deactivate_resource(brdev, child, type,
		    rid, r));
}

struct resource *
cbb_alloc_resource(device_t brdev, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct cbb_softc *sc = device_get_softc(brdev);

	if (sc->flags & CBB_16BIT_CARD)
		return (cbb_pcic_alloc_resource(brdev, child, type, rid,
		    start, end, count, flags));
	else
		return (cbb_cardbus_alloc_resource(brdev, child, type, rid,
		    start, end, count, flags));
}

int
cbb_release_resource(device_t brdev, device_t child, int type, int rid,
    struct resource *r)
{
	struct cbb_softc *sc = device_get_softc(brdev);

	if (sc->flags & CBB_16BIT_CARD)
		return (cbb_pcic_release_resource(brdev, child, type,
		    rid, r));
	else
		return (cbb_cardbus_release_resource(brdev, child, type,
		    rid, r));
}

int
cbb_read_ivar(device_t brdev, device_t child, int which, uintptr_t *result)
{
	struct cbb_softc *sc = device_get_softc(brdev);

	switch (which) {
	case PCIB_IVAR_DOMAIN:
		*result = sc->domain;
		return (0);
	case PCIB_IVAR_BUS:
		*result = sc->bus.sec;
		return (0);
	}
	return (ENOENT);
}

int
cbb_write_ivar(device_t brdev, device_t child, int which, uintptr_t value)
{

	switch (which) {
	case PCIB_IVAR_DOMAIN:
		return (EINVAL);
	case PCIB_IVAR_BUS:
		return (EINVAL);
	}
	return (ENOENT);
}

int
cbb_child_present(device_t parent, device_t child)
{
	struct cbb_softc *sc = (struct cbb_softc *)device_get_softc(parent);
	uint32_t sockstate;

	sockstate = cbb_get(sc, CBB_SOCKET_STATE);
	return (CBB_CARD_PRESENT(sockstate) && sc->cardok);
}
