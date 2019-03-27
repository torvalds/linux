/*
 * Copyright (c) 2008, 2009 Michael Shalayeff
 * Copyright (c) 2009, 2010 Hans-Joerg Hoexer
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* #define	TPM_DEBUG */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>

#ifdef __FreeBSD__
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <machine/md_var.h>

#include <isa/isareg.h>
#include <isa/isavar.h>
#else
#include <sys/device.h>

#include <machine/cpu.h>
#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/conf.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>
#endif
#include <dev/tpm/tpmvar.h>

#ifndef __FreeBSD__
/* XXX horrible hack for tcsd (-lpthread) workaround on OpenBSD */
#undef PCATCH
#define PCATCH	0
#endif

#define	TPM_BUFSIZ	1024

#define TPM_HDRSIZE	10

#define TPM_PARAM_SIZE	0x0001

#ifdef __FreeBSD__
#define IRQUNK	-1
#endif

#define	TPM_ACCESS			0x0000	/* access register */
#define	TPM_ACCESS_ESTABLISHMENT	0x01	/* establishment */
#define	TPM_ACCESS_REQUEST_USE		0x02	/* request using locality */
#define	TPM_ACCESS_REQUEST_PENDING	0x04	/* pending request */
#define	TPM_ACCESS_SEIZE		0x08	/* request locality seize */
#define	TPM_ACCESS_SEIZED		0x10	/* locality has been seized */
#define	TPM_ACCESS_ACTIVE_LOCALITY	0x20	/* locality is active */
#define	TPM_ACCESS_VALID		0x80	/* bits are valid */
#define	TPM_ACCESS_BITS	\
    "\020\01EST\02REQ\03PEND\04SEIZE\05SEIZED\06ACT\010VALID"

#define	TPM_INTERRUPT_ENABLE	0x0008
#define	TPM_GLOBAL_INT_ENABLE	0x80000000	/* enable ints */
#define	TPM_CMD_READY_INT	0x00000080	/* cmd ready enable */
#define	TPM_INT_EDGE_FALLING	0x00000018
#define	TPM_INT_EDGE_RISING	0x00000010
#define	TPM_INT_LEVEL_LOW	0x00000008
#define	TPM_INT_LEVEL_HIGH	0x00000000
#define	TPM_LOCALITY_CHANGE_INT	0x00000004	/* locality change enable */
#define	TPM_STS_VALID_INT	0x00000002	/* int on TPM_STS_VALID is set */
#define	TPM_DATA_AVAIL_INT	0x00000001	/* int on TPM_STS_DATA_AVAIL is set */
#define	TPM_INTERRUPT_ENABLE_BITS \
    "\020\040ENA\010RDY\03LOCH\02STSV\01DRDY"

#define	TPM_INT_VECTOR		0x000c	/* 8 bit reg for 4 bit irq vector */
#define	TPM_INT_STATUS		0x0010	/* bits are & 0x87 from TPM_INTERRUPT_ENABLE */

#define	TPM_INTF_CAPABILITIES		0x0014	/* capability register */
#define	TPM_INTF_BURST_COUNT_STATIC	0x0100	/* TPM_STS_BMASK static */
#define	TPM_INTF_CMD_READY_INT		0x0080	/* int on ready supported */
#define	TPM_INTF_INT_EDGE_FALLING	0x0040	/* falling edge ints supported */
#define	TPM_INTF_INT_EDGE_RISING	0x0020	/* rising edge ints supported */
#define	TPM_INTF_INT_LEVEL_LOW		0x0010	/* level-low ints supported */
#define	TPM_INTF_INT_LEVEL_HIGH		0x0008	/* level-high ints supported */
#define	TPM_INTF_LOCALITY_CHANGE_INT	0x0004	/* locality-change int (mb 1) */
#define	TPM_INTF_STS_VALID_INT		0x0002	/* TPM_STS_VALID int supported */
#define	TPM_INTF_DATA_AVAIL_INT		0x0001	/* TPM_STS_DATA_AVAIL int supported (mb 1) */
#define	TPM_CAPSREQ \
  (TPM_INTF_DATA_AVAIL_INT|TPM_INTF_LOCALITY_CHANGE_INT|TPM_INTF_INT_LEVEL_LOW)
#define	TPM_CAPBITS \
  "\020\01IDRDY\02ISTSV\03ILOCH\04IHIGH\05ILOW\06IEDGE\07IFALL\010IRDY\011BCST"

#define	TPM_STS			0x0018		/* status register */
#define TPM_STS_MASK		0x000000ff	/* status bits */
#define	TPM_STS_BMASK		0x00ffff00	/* ro io burst size */
#define	TPM_STS_VALID		0x00000080	/* ro other bits are valid */
#define	TPM_STS_CMD_READY	0x00000040	/* rw chip/signal ready */
#define	TPM_STS_GO		0x00000020	/* wo start the command */
#define	TPM_STS_DATA_AVAIL	0x00000010	/* ro data available */
#define	TPM_STS_DATA_EXPECT	0x00000008	/* ro more data to be written */
#define	TPM_STS_RESP_RETRY	0x00000002	/* wo resend the response */
#define	TPM_STS_BITS	"\020\010VALID\07RDY\06GO\05DRDY\04EXPECT\02RETRY"

#define	TPM_DATA	0x0024
#define	TPM_ID		0x0f00
#define	TPM_REV		0x0f04
#define	TPM_SIZE	0x5000		/* five pages of the above */

#define	TPM_ACCESS_TMO	2000		/* 2sec */
#define	TPM_READY_TMO	2000		/* 2sec */
#define	TPM_READ_TMO	120000		/* 2 minutes */
#define TPM_BURST_TMO	2000		/* 2sec */

#define	TPM_LEGACY_BUSY	0x01
#define	TPM_LEGACY_ABRT	0x01
#define	TPM_LEGACY_DA	0x02
#define	TPM_LEGACY_RE	0x04
#define	TPM_LEGACY_LAST	0x04
#define	TPM_LEGACY_BITS	"\020\01BUSY\2DA\3RE\4LAST"
#define	TPM_LEGACY_TMO		(2*60)	/* sec */
#define	TPM_LEGACY_SLEEP	5	/* ticks */
#define	TPM_LEGACY_DELAY	100

/* Set when enabling legacy interface in host bridge. */
int tpm_enabled;


#ifdef __FreeBSD__
#define	TPMSOFTC(dev) \
	((struct tpm_softc *)dev->si_drv1)

d_open_t	tpmopen;
d_close_t	tpmclose;
d_read_t	tpmread;
d_write_t	tpmwrite;
d_ioctl_t	tpmioctl;

static struct cdevsw tpm_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_open =	tpmopen,
	.d_close =	tpmclose,
	.d_read =	tpmread,
	.d_write =	tpmwrite,
	.d_ioctl =	tpmioctl,
	.d_name =	"tpm",
};
#else
#define	TPMSOFTC(dev) \
    (struct tpm_softc *)device_lookup(&tpm_cd, minor(dev))

struct cfdriver tpm_cd = {
	NULL, "tpm", DV_DULL
};

int	tpm_match(device_t , void *, void *);
void	tpm_attach(device_t , device_t , void *);

struct cfattach tpm_ca = {
	sizeof(struct tpm_softc), tpm_match, tpm_attach
};
#endif

const struct {
	u_int32_t devid;
	char name[32];
	int flags;
#define TPM_DEV_NOINTS	0x0001
} tpm_devs[] = {
	{ 0x000615d1, "IFX SLD 9630 TT 1.1", 0 },
	{ 0x000b15d1, "IFX SLB 9635 TT 1.2", 0 },
	{ 0x100214e4, "Broadcom BCM0102", TPM_DEV_NOINTS },
	{ 0x00fe1050, "WEC WPCT200", 0 },
	{ 0x687119fa, "SNS SSX35", 0 },
	{ 0x2e4d5453, "STM ST19WP18", 0 },
	{ 0x32021114, "ATML 97SC3203", TPM_DEV_NOINTS },
	{ 0x10408086, "INTEL INTC0102", 0 },
	{ 0, "", TPM_DEV_NOINTS },
};

int tpm_tis12_irqinit(struct tpm_softc *, int, int);
int tpm_tis12_init(struct tpm_softc *, int, const char *);
int tpm_tis12_start(struct tpm_softc *, int);
int tpm_tis12_read(struct tpm_softc *, void *, int, size_t *, int);
int tpm_tis12_write(struct tpm_softc *, void *, int);
int tpm_tis12_end(struct tpm_softc *, int, int);

#ifdef __FreeBSD__
void tpm_intr(void *);
#else
int tpm_intr(void *);
void tpm_powerhook(int, void *);
int tpm_suspend(struct tpm_softc *, int);
int tpm_resume(struct tpm_softc *, int);
#endif

int tpm_waitfor_poll(struct tpm_softc *, u_int8_t, int, void *);
int tpm_waitfor_int(struct tpm_softc *, u_int8_t, int, void *, int);
int tpm_waitfor(struct tpm_softc *, u_int8_t, int, void *);
int tpm_request_locality(struct tpm_softc *, int);
int tpm_getburst(struct tpm_softc *);
u_int8_t tpm_status(struct tpm_softc *);
int tpm_tmotohz(int);

int tpm_legacy_probe(bus_space_tag_t, bus_addr_t);
int tpm_legacy_init(struct tpm_softc *, int, const char *);
int tpm_legacy_start(struct tpm_softc *, int);
int tpm_legacy_read(struct tpm_softc *, void *, int, size_t *, int);
int tpm_legacy_write(struct tpm_softc *, void *, int);
int tpm_legacy_end(struct tpm_softc *, int, int);

#ifdef __FreeBSD__

/*
 * FreeBSD specific code for probing and attaching TPM to device tree.
 */
#if 0
static void
tpm_identify(driver_t *driver, device_t parent)
{
	BUS_ADD_CHILD(parent, ISA_ORDER_SPECULATIVE, "tpm", 0);
}
#endif


int
tpm_attach(device_t dev)
{
	struct tpm_softc *sc = device_get_softc(dev);
	int irq;

	sc->mem_rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->mem_rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL)
		return ENXIO;

	sc->sc_bt = rman_get_bustag(sc->mem_res);
	sc->sc_bh = rman_get_bushandle(sc->mem_res);

	sc->irq_rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sc->irq_res != NULL)
		irq = rman_get_start(sc->irq_res);
	else
		irq = IRQUNK;

	/* In case PnP probe this may contain some initialization. */
	tpm_tis12_probe(sc->sc_bt, sc->sc_bh);
	
	if (tpm_legacy_probe(sc->sc_bt, sc->sc_bh)) {
		sc->sc_init = tpm_legacy_init;
		sc->sc_start = tpm_legacy_start;
		sc->sc_read = tpm_legacy_read;
		sc->sc_write = tpm_legacy_write;
		sc->sc_end = tpm_legacy_end;
	} else {
		sc->sc_init = tpm_tis12_init;
		sc->sc_start = tpm_tis12_start;
		sc->sc_read = tpm_tis12_read;
		sc->sc_write = tpm_tis12_write;
		sc->sc_end = tpm_tis12_end;
	}

	printf("%s", device_get_name(dev));
	if ((sc->sc_init)(sc, irq, "tpm")) {
		tpm_detach(dev);
		return ENXIO;
	}

	if (sc->sc_init == tpm_tis12_init && sc->irq_res != NULL &&
	    bus_setup_intr(dev, sc->irq_res, INTR_TYPE_TTY, NULL,
	    tpm_intr, sc, &sc->intr_cookie) != 0) {
		tpm_detach(dev);
		printf(": cannot establish interrupt\n");
		return 1;
	}

	sc->sc_cdev = make_dev(&tpm_cdevsw, device_get_unit(dev), 
			    UID_ROOT, GID_WHEEL, 0600, "tpm");
	sc->sc_cdev->si_drv1 = sc;

	return 0;
}

int
tpm_detach(device_t dev)
{
	struct tpm_softc * sc = device_get_softc(dev);

	if(sc->intr_cookie){
		bus_teardown_intr(dev, sc->irq_res, sc->intr_cookie);
	}

	if(sc->mem_res){
		bus_release_resource(dev, SYS_RES_MEMORY, 
				     sc->mem_rid, sc->mem_res);
	}

	if(sc->irq_res){
		bus_release_resource(dev, SYS_RES_IRQ,
				     sc->irq_rid, sc->irq_res);
	}
	if(sc->sc_cdev){
		destroy_dev(sc->sc_cdev);
	}

	return 0;
}


#else
/*
 * OpenBSD specific code for probing and attaching TPM to device tree.
 */
int
tpm_match(device_t parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	struct cfdata *cf = match;
	bus_space_tag_t bt = ia->ia_memt;
	bus_space_handle_t bh;
	int rv;

	/* There can be only one. */
	if (cf->cf_unit)
		return 0;

	if (tpm_legacy_probe(ia->ia_iot, ia->ia_iobase)) {
		ia->ia_iosize = 2;
		return 1;
	}

	if (ia->ia_maddr == -1)
		return 0;

	if (bus_space_map(bt, ia->ia_maddr, TPM_SIZE, 0, &bh))
		return 0;

	if ((rv = tpm_tis12_probe(bt, bh))) {
		ia->ia_iosize = 0;
		ia->ia_msize = TPM_SIZE;
	}

	bus_space_unmap(bt, bh, TPM_SIZE);
	return rv;
}

void
tpm_attach(device_t parent, device_t self, void *aux)
{
	struct tpm_softc *sc = (struct tpm_softc *)self;
	struct isa_attach_args *ia = aux;
	bus_addr_t iobase;
	bus_size_t size;
	int rv;

	if (tpm_legacy_probe(ia->ia_iot, ia->ia_iobase)) {
		sc->sc_bt = ia->ia_iot;
		iobase = ia->ia_iobase;
		size = ia->ia_iosize;
		sc->sc_batm = ia->ia_iot;
		sc->sc_init = tpm_legacy_init;
		sc->sc_start = tpm_legacy_start;
		sc->sc_read = tpm_legacy_read;
		sc->sc_write = tpm_legacy_write;
		sc->sc_end = tpm_legacy_end;
	} else {
		sc->sc_bt = ia->ia_memt;
		iobase = ia->ia_maddr;
		size = TPM_SIZE;
		sc->sc_init = tpm_tis12_init;
		sc->sc_start = tpm_tis12_start;
		sc->sc_read = tpm_tis12_read;
		sc->sc_write = tpm_tis12_write;
		sc->sc_end = tpm_tis12_end;
	}

	if (bus_space_map(sc->sc_bt, iobase, size, 0, &sc->sc_bh)) {
		printf(": cannot map registers\n");
		return;
	}

	if ((rv = (sc->sc_init)(sc, ia->ia_irq, sc->sc_dev.dv_xname))) {
		bus_space_unmap(sc->sc_bt, sc->sc_bh, size);
		return;
	}

	/*
	 * Only setup interrupt handler when we have a vector and the
	 * chip is TIS 1.2 compliant.
	 */
	if (sc->sc_init == tpm_tis12_init && ia->ia_irq != IRQUNK &&
	    (sc->sc_ih = isa_intr_establish(ia->ia_ic, ia->ia_irq, IST_EDGE,
	    IPL_TTY, tpm_intr, sc, sc->sc_dev.dv_xname)) == NULL) {
		bus_space_unmap(sc->sc_bt, sc->sc_bh, TPM_SIZE);
		printf("%s: cannot establish interrupt\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	sc->sc_suspend = PWR_RESUME;
	sc->sc_powerhook = powerhook_establish(tpm_powerhook, sc);
}
#endif

/* Probe TPM using TIS 1.2 interface. */
int
tpm_tis12_probe(bus_space_tag_t bt, bus_space_handle_t bh)
{
	u_int32_t r;
	u_int8_t save, reg;

	r = bus_space_read_4(bt, bh, TPM_INTF_CAPABILITIES);
	if (r == 0xffffffff)
		return 0;

#ifdef TPM_DEBUG
	printf("tpm: caps=%b\n", r, TPM_CAPBITS);
#endif
	if ((r & TPM_CAPSREQ) != TPM_CAPSREQ ||
	    !(r & (TPM_INTF_INT_EDGE_RISING | TPM_INTF_INT_LEVEL_LOW))) {
#ifdef TPM_DEBUG
		printf("tpm: caps too low (caps=%b)\n", r, TPM_CAPBITS);
#endif
		return 0;
	}

	save = bus_space_read_1(bt, bh, TPM_ACCESS);
	bus_space_write_1(bt, bh, TPM_ACCESS, TPM_ACCESS_REQUEST_USE);
	reg = bus_space_read_1(bt, bh, TPM_ACCESS);
	if ((reg & TPM_ACCESS_VALID) && (reg & TPM_ACCESS_ACTIVE_LOCALITY) &&
	    bus_space_read_4(bt, bh, TPM_ID) != 0xffffffff)
		return 1;

	bus_space_write_1(bt, bh, TPM_ACCESS, save);
	return 0;
}

/*
 * Setup interrupt vector if one is provided and interrupts are know to
 * work on that particular chip.
 */
int
tpm_tis12_irqinit(struct tpm_softc *sc, int irq, int idx)
{
	u_int32_t r;

	if ((irq == IRQUNK) || (tpm_devs[idx].flags & TPM_DEV_NOINTS)) {
		sc->sc_vector = IRQUNK;
		return 0;
	}

	/* Ack and disable all interrupts. */
	bus_space_write_4(sc->sc_bt, sc->sc_bh, TPM_INTERRUPT_ENABLE,
	    bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_INTERRUPT_ENABLE) &
	    ~TPM_GLOBAL_INT_ENABLE);
	bus_space_write_4(sc->sc_bt, sc->sc_bh, TPM_INT_STATUS,
	    bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_INT_STATUS));

	/* Program interrupt vector. */
	bus_space_write_1(sc->sc_bt, sc->sc_bh, TPM_INT_VECTOR, irq);
	sc->sc_vector = irq;

	/* Program interrupt type. */
	if (sc->sc_capabilities & TPM_INTF_INT_EDGE_RISING)
		r = TPM_INT_EDGE_RISING;
	else if (sc->sc_capabilities & TPM_INTF_INT_LEVEL_HIGH)
		r = TPM_INT_LEVEL_HIGH;
	else
		r = TPM_INT_LEVEL_LOW;
	bus_space_write_4(sc->sc_bt, sc->sc_bh, TPM_INTERRUPT_ENABLE, r);

	return 0;
}

/* Setup TPM using TIS 1.2 interface. */
int
tpm_tis12_init(struct tpm_softc *sc, int irq, const char *name)
{
	u_int32_t r;
	int i;

	r = bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_INTF_CAPABILITIES);
#ifdef TPM_DEBUG
	printf(" caps=%b ", r, TPM_CAPBITS);
#endif
	if ((r & TPM_CAPSREQ) != TPM_CAPSREQ ||
	    !(r & (TPM_INTF_INT_EDGE_RISING | TPM_INTF_INT_LEVEL_LOW))) {
		printf(": capabilities too low (caps=%b)\n", r, TPM_CAPBITS);
		return 1;
	}
	sc->sc_capabilities = r;

	sc->sc_devid = bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_ID);
	sc->sc_rev = bus_space_read_1(sc->sc_bt, sc->sc_bh, TPM_REV);

	for (i = 0; tpm_devs[i].devid; i++)
		if (tpm_devs[i].devid == sc->sc_devid)
			break;

	if (tpm_devs[i].devid)
		printf(": %s rev 0x%x\n", tpm_devs[i].name, sc->sc_rev);
	else
		printf(": device 0x%08x rev 0x%x\n", sc->sc_devid, sc->sc_rev);

	if (tpm_tis12_irqinit(sc, irq, i))
		return 1;

	if (tpm_request_locality(sc, 0))
		return 1;

	/* Abort whatever it thought it was doing. */
	bus_space_write_1(sc->sc_bt, sc->sc_bh, TPM_STS, TPM_STS_CMD_READY);

	return 0;
}

int
tpm_request_locality(struct tpm_softc *sc, int l)
{
	u_int32_t r;
	int to, rv;

	if (l != 0)
		return EINVAL;

	if ((bus_space_read_1(sc->sc_bt, sc->sc_bh, TPM_ACCESS) &
	    (TPM_ACCESS_VALID | TPM_ACCESS_ACTIVE_LOCALITY)) ==
	    (TPM_ACCESS_VALID | TPM_ACCESS_ACTIVE_LOCALITY))
		return 0;

	bus_space_write_1(sc->sc_bt, sc->sc_bh, TPM_ACCESS,
	    TPM_ACCESS_REQUEST_USE);

	to = tpm_tmotohz(TPM_ACCESS_TMO);

	while ((r = bus_space_read_1(sc->sc_bt, sc->sc_bh, TPM_ACCESS) &
	    (TPM_ACCESS_VALID | TPM_ACCESS_ACTIVE_LOCALITY)) !=
	    (TPM_ACCESS_VALID | TPM_ACCESS_ACTIVE_LOCALITY) && to--) {
		rv = tsleep(sc->sc_init, PRIBIO | PCATCH, "tpm_locality", 1);
		if (rv &&  rv != EWOULDBLOCK) {
#ifdef TPM_DEBUG
			printf("tpm_request_locality: interrupted %d\n", rv);
#endif
			return rv;
		}
	}

	if ((r & (TPM_ACCESS_VALID | TPM_ACCESS_ACTIVE_LOCALITY)) !=
	    (TPM_ACCESS_VALID | TPM_ACCESS_ACTIVE_LOCALITY)) {
#ifdef TPM_DEBUG
		printf("tpm_request_locality: access %b\n", r, TPM_ACCESS_BITS);
#endif
		return EBUSY;
	}

	return 0;
}

int
tpm_getburst(struct tpm_softc *sc)
{
	int burst, to, rv;

	to = tpm_tmotohz(TPM_BURST_TMO);

	burst = 0;
	while (burst == 0 && to--) {
		/*
		 * Burst count has to be read from bits 8 to 23 without
		 * touching any other bits, eg. the actual status bits 0
		 * to 7.
		 */
		burst = bus_space_read_1(sc->sc_bt, sc->sc_bh, TPM_STS + 1);
		burst |= bus_space_read_1(sc->sc_bt, sc->sc_bh, TPM_STS + 2)
		    << 8;
#ifdef TPM_DEBUG
		printf("tpm_getburst: read %d\n", burst);
#endif
		if (burst)
			return burst;

		rv = tsleep(sc, PRIBIO | PCATCH, "tpm_getburst", 1);
		if (rv && rv != EWOULDBLOCK) {
			return 0;
		}
	}

	return 0;
}

u_int8_t
tpm_status(struct tpm_softc *sc)
{
	u_int8_t status;

	status = bus_space_read_1(sc->sc_bt, sc->sc_bh, TPM_STS) &
	    TPM_STS_MASK;

	return status;
}

int
tpm_tmotohz(int tmo)
{
	struct timeval tv;

	tv.tv_sec = tmo / 1000;
	tv.tv_usec = 1000 * (tmo % 1000);

	return tvtohz(&tv);
}

/* Save TPM state on suspend. */
int
#ifdef __FreeBSD__
tpm_suspend(device_t dev)
#else
tpm_suspend(struct tpm_softc *sc, int why)
#endif
{
#ifdef __FreeBSD__
	struct tpm_softc *sc = device_get_softc(dev);
	int why = 1;
#endif
	u_int8_t command[] = {
	    0, 193,		/* TPM_TAG_RQU_COMMAND */
	    0, 0, 0, 10,	/* Length in bytes */
	    0, 0, 0, 156	/* TPM_ORD_SaveStates */
	};

	/*
	 * Power down:  We have to issue the SaveStates command.
	 */
	sc->sc_write(sc, &command, sizeof(command));
	sc->sc_read(sc, &command, sizeof(command), NULL, TPM_HDRSIZE);
#ifdef TPM_DEBUG
	printf("tpm_suspend: power down: %d -> %d\n", sc->sc_suspend, why);
#endif
	sc->sc_suspend = why;

	return 0;
}

/*
 * Handle resume event.  Actually nothing to do as the BIOS is supposed
 * to restore the previously saved state.
 */
int
#ifdef __FreeBSD__
tpm_resume(device_t dev)
#else
tpm_resume(struct tpm_softc *sc, int why)
#endif
{
#ifdef __FreeBSD__
	struct tpm_softc *sc = device_get_softc(dev);
	int why = 0;
#endif
#ifdef TPM_DEBUG
	printf("tpm_resume: resume: %d -> %d\n", sc->sc_suspend, why);
#endif
	sc->sc_suspend = why;

	return 0;
}

/* Dispatch suspend and resume events. */
#ifndef __FreeBSD__
void
tpm_powerhook(int why, void *self)
{
	struct tpm_softc *sc = (struct tpm_softc *)self;

	if (why != PWR_RESUME)
		tpm_suspend(sc, why);
	else
		tpm_resume(sc, why);
}
#endif	/* !__FreeBSD__ */

/* Wait for given status bits using polling. */
int
tpm_waitfor_poll(struct tpm_softc *sc, u_int8_t mask, int tmo, void *c)
{
	int rv;

	/*
	 * Poll until either the requested condition or a time out is
	 * met.
	 */
	while (((sc->sc_stat = tpm_status(sc)) & mask) != mask && tmo--) {
		rv = tsleep(c, PRIBIO | PCATCH, "tpm_poll", 1);
		if (rv && rv != EWOULDBLOCK) {
#ifdef TPM_DEBUG
			printf("tpm_waitfor_poll: interrupted %d\n", rv);
#endif
			return rv;
		}
	}

	return 0;
}

/* Wait for given status bits using interrupts. */
int
tpm_waitfor_int(struct tpm_softc *sc, u_int8_t mask, int tmo, void *c,
    int inttype)
{
	int rv, to;

	/* Poll and return when condition is already met. */
	sc->sc_stat = tpm_status(sc);
	if ((sc->sc_stat & mask) == mask)
		return 0;

	/*
	 * Enable interrupt on tpm chip.  Note that interrupts on our
	 * level (SPL_TTY) are disabled (see tpm{read,write} et al) and
	 * will not be delivered to the cpu until we call tsleep(9) below.
	 */
	bus_space_write_4(sc->sc_bt, sc->sc_bh, TPM_INTERRUPT_ENABLE,
	    bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_INTERRUPT_ENABLE) |
	    inttype);
	bus_space_write_4(sc->sc_bt, sc->sc_bh, TPM_INTERRUPT_ENABLE,
	    bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_INTERRUPT_ENABLE) |
	    TPM_GLOBAL_INT_ENABLE);

	/*
	 * Poll once more to remedy the race between previous polling
	 * and enabling interrupts on the tpm chip.
	 */
	sc->sc_stat = tpm_status(sc);
	if ((sc->sc_stat & mask) == mask) {
		rv = 0;
		goto out;
	}

	to = tpm_tmotohz(tmo);
#ifdef TPM_DEBUG
	printf("tpm_waitfor_int: sleeping for %d ticks on %p\n", to, c);
#endif
	/*
	 * tsleep(9) enables interrupts on the cpu and returns after
	 * wake up with interrupts disabled again.  Note that interrupts
	 * generated by the tpm chip while being at SPL_TTY are not lost
	 * but held and delivered as soon as the cpu goes below SPL_TTY.
	 */
	rv = tsleep(c, PRIBIO | PCATCH, "tpm_intr", to);

	sc->sc_stat = tpm_status(sc);
#ifdef TPM_DEBUG
	printf("tpm_waitfor_int: woke up with rv %d stat %b\n", rv,
	    sc->sc_stat, TPM_STS_BITS);
#endif
	if ((sc->sc_stat & mask) == mask)
		rv = 0;

	/* Disable interrupts on tpm chip again. */
out:	bus_space_write_4(sc->sc_bt, sc->sc_bh, TPM_INTERRUPT_ENABLE,
	    bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_INTERRUPT_ENABLE) &
	    ~TPM_GLOBAL_INT_ENABLE);
	bus_space_write_4(sc->sc_bt, sc->sc_bh, TPM_INTERRUPT_ENABLE,
	    bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_INTERRUPT_ENABLE) &
	    ~inttype);

	return rv;
}

/*
 * Wait on given status bits, uses interrupts where possible, otherwise polls.
 */
int
tpm_waitfor(struct tpm_softc *sc, u_int8_t b0, int tmo, void *c)
{
	u_int8_t b;
	int re, to, rv;

#ifdef TPM_DEBUG
	printf("tpm_waitfor: b0 %b\n", b0, TPM_STS_BITS);
#endif

	/*
	 * If possible, use interrupts, otherwise poll.
	 *
	 * We use interrupts for TPM_STS_VALID and TPM_STS_DATA_AVAIL (if
	 * the tpm chips supports them) as waiting for those can take
	 * really long.  The other TPM_STS* are not needed very often
	 * so we do not support them.
	 */
	if (sc->sc_vector != IRQUNK) {
		b = b0;

		/*
		 * Wait for data ready.  This interrupt only occurs
		 * when both TPM_STS_VALID and TPM_STS_DATA_AVAIL are asserted.
		 * Thus we don't have to bother with TPM_STS_VALID
		 * separately and can just return.
		 *
		 * This only holds for interrupts!  When using polling
		 * both flags have to be waited for, see below.
		 */
		if ((b & TPM_STS_DATA_AVAIL) && (sc->sc_capabilities &
		    TPM_INTF_DATA_AVAIL_INT))
			return tpm_waitfor_int(sc, b, tmo, c,
			    TPM_DATA_AVAIL_INT);

		/* Wait for status valid bit. */
		if ((b & TPM_STS_VALID) && (sc->sc_capabilities &
		    TPM_INTF_STS_VALID_INT)) {
			rv = tpm_waitfor_int(sc, b, tmo, c, TPM_STS_VALID_INT);
			if (rv != 0)
				return rv;
			else
				b = b0 & ~TPM_STS_VALID;
		}

		/*
		 * When all flags are taken care of, return.  Otherwise
		 * use polling for eg. TPM_STS_CMD_READY.
		 */
		if (b == 0)
			return 0;
	}

	re = 3;
restart:
	/*
	 * If requested wait for TPM_STS_VALID before dealing with
	 * any other flag.  Eg. when both TPM_STS_DATA_AVAIL and TPM_STS_VALID
	 * are requested, wait for the latter first.
	 */
	b = b0;
	if (b0 & TPM_STS_VALID)
		b = TPM_STS_VALID;

	to = tpm_tmotohz(tmo);
again:
	if ((rv = tpm_waitfor_poll(sc, b, to, c)) != 0)
		return rv;

	if ((b & sc->sc_stat) == TPM_STS_VALID) {
		/* Now wait for other flags. */
		b = b0 & ~TPM_STS_VALID;
		to++;
		goto again;
	}

	if ((sc->sc_stat & b) != b) {
#ifdef TPM_DEBUG
		printf("tpm_waitfor: timeout: stat=%b b=%b\n",
		    sc->sc_stat, TPM_STS_BITS, b, TPM_STS_BITS);
#endif
		if (re-- && (b0 & TPM_STS_VALID)) {
			bus_space_write_1(sc->sc_bt, sc->sc_bh, TPM_STS,
			    TPM_STS_RESP_RETRY);
			goto restart;
		}
		return EIO;
	}

	return 0;
}

/* Start transaction. */
int
tpm_tis12_start(struct tpm_softc *sc, int flag)
{
	int rv;

	if (flag == UIO_READ) {
		rv = tpm_waitfor(sc, TPM_STS_DATA_AVAIL | TPM_STS_VALID,
		    TPM_READ_TMO, sc->sc_read);
		return rv;
	}

	/* Own our (0th) locality. */
	if ((rv = tpm_request_locality(sc, 0)) != 0)
		return rv;

	sc->sc_stat = tpm_status(sc);
	if (sc->sc_stat & TPM_STS_CMD_READY) {
#ifdef TPM_DEBUG
		printf("tpm_tis12_start: UIO_WRITE status %b\n", sc->sc_stat,
		   TPM_STS_BITS);
#endif
		return 0;
	}

#ifdef TPM_DEBUG
	printf("tpm_tis12_start: UIO_WRITE readying chip\n");
#endif

	/* Abort previous and restart. */
	bus_space_write_1(sc->sc_bt, sc->sc_bh, TPM_STS, TPM_STS_CMD_READY);
	if ((rv = tpm_waitfor(sc, TPM_STS_CMD_READY, TPM_READY_TMO,
	    sc->sc_write))) {
#ifdef TPM_DEBUG
		printf("tpm_tis12_start: UIO_WRITE readying failed %d\n", rv);
#endif
		return rv;
	}

#ifdef TPM_DEBUG
	printf("tpm_tis12_start: UIO_WRITE readying done\n");
#endif

	return 0;
}

int
tpm_tis12_read(struct tpm_softc *sc, void *buf, int len, size_t *count,
    int flags)
{
	u_int8_t *p = buf;
	size_t cnt;
	int rv, n, bcnt;

#ifdef TPM_DEBUG
	printf("tpm_tis12_read: len %d\n", len);
#endif
	cnt = 0;
	while (len > 0) {
		if ((rv = tpm_waitfor(sc, TPM_STS_DATA_AVAIL | TPM_STS_VALID,
		    TPM_READ_TMO, sc->sc_read)))
			return rv;

		bcnt = tpm_getburst(sc);
		n = MIN(len, bcnt);
#ifdef TPM_DEBUG
		printf("tpm_tis12_read: fetching %d, burst is %d\n", n, bcnt);
#endif
		for (; n--; len--) {
			*p++ = bus_space_read_1(sc->sc_bt, sc->sc_bh, TPM_DATA);
			cnt++;
		}

		if ((flags & TPM_PARAM_SIZE) == 0 && cnt >= 6)
			break;
	}
#ifdef TPM_DEBUG
	printf("tpm_tis12_read: read %zd bytes, len %d\n", cnt, len);
#endif

	if (count)
		*count = cnt;

	return 0;
}

int
tpm_tis12_write(struct tpm_softc *sc, void *buf, int len)
{
	u_int8_t *p = buf;
	size_t cnt;
	int rv, r;

#ifdef TPM_DEBUG
	printf("tpm_tis12_write: sc %p buf %p len %d\n", sc, buf, len);
#endif

	if ((rv = tpm_request_locality(sc, 0)) != 0)
		return rv;

	cnt = 0;
	while (cnt < len - 1) {
		for (r = tpm_getburst(sc); r > 0 && cnt < len - 1; r--) {
			bus_space_write_1(sc->sc_bt, sc->sc_bh, TPM_DATA, *p++);
			cnt++;
		}
		if ((rv = tpm_waitfor(sc, TPM_STS_VALID, TPM_READ_TMO, sc))) {
#ifdef TPM_DEBUG
			printf("tpm_tis12_write: failed burst rv %d\n", rv);
#endif
			return rv;
		}
		sc->sc_stat = tpm_status(sc);
		if (!(sc->sc_stat & TPM_STS_DATA_EXPECT)) {
#ifdef TPM_DEBUG
			printf("tpm_tis12_write: failed rv %d stat=%b\n", rv,
			    sc->sc_stat, TPM_STS_BITS);
#endif
			return EIO;
		}
	}

	bus_space_write_1(sc->sc_bt, sc->sc_bh, TPM_DATA, *p++);
	cnt++;

	if ((rv = tpm_waitfor(sc, TPM_STS_VALID, TPM_READ_TMO, sc))) {
#ifdef TPM_DEBUG
		printf("tpm_tis12_write: failed last byte rv %d\n", rv);
#endif
		return rv;
	}
	if ((sc->sc_stat & TPM_STS_DATA_EXPECT) != 0) {
#ifdef TPM_DEBUG
		printf("tpm_tis12_write: failed rv %d stat=%b\n", rv,
		    sc->sc_stat, TPM_STS_BITS);
#endif
		return EIO;
	}

#ifdef TPM_DEBUG
	printf("tpm_tis12_write: wrote %d byte\n", cnt);
#endif

	return 0;
}

/* Finish transaction. */
int
tpm_tis12_end(struct tpm_softc *sc, int flag, int err)
{
	int rv = 0;

	if (flag == UIO_READ) {
		if ((rv = tpm_waitfor(sc, TPM_STS_VALID, TPM_READ_TMO,
		    sc->sc_read)))
			return rv;

		/* Still more data? */
		sc->sc_stat = tpm_status(sc);
		if (!err && ((sc->sc_stat & TPM_STS_DATA_AVAIL) == TPM_STS_DATA_AVAIL)) {
#ifdef TPM_DEBUG
			printf("tpm_tis12_end: read failed stat=%b\n",
			    sc->sc_stat, TPM_STS_BITS);
#endif
			rv = EIO;
		}

		bus_space_write_1(sc->sc_bt, sc->sc_bh, TPM_STS,
		    TPM_STS_CMD_READY);

		/* Release our (0th) locality. */
		bus_space_write_1(sc->sc_bt, sc->sc_bh,TPM_ACCESS,
		    TPM_ACCESS_ACTIVE_LOCALITY);
	} else {
		/* Hungry for more? */
		sc->sc_stat = tpm_status(sc);
		if (!err && (sc->sc_stat & TPM_STS_DATA_EXPECT)) {
#ifdef TPM_DEBUG
			printf("tpm_tis12_end: write failed stat=%b\n",
			    sc->sc_stat, TPM_STS_BITS);
#endif
			rv = EIO;
		}

		bus_space_write_1(sc->sc_bt, sc->sc_bh, TPM_STS,
		    err ? TPM_STS_CMD_READY : TPM_STS_GO);
	}

	return rv;
}

#ifdef __FreeBSD__
void
#else
int
#endif
tpm_intr(void *v)
{
	struct tpm_softc *sc = v;
	u_int32_t r;
#ifdef TPM_DEBUG
	static int cnt = 0;
#endif

	r = bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_INT_STATUS);
#ifdef TPM_DEBUG
	if (r != 0)
		printf("tpm_intr: int=%b (%d)\n", r, TPM_INTERRUPT_ENABLE_BITS,
		    cnt);
	else
		cnt++;
#endif
	if (!(r & (TPM_CMD_READY_INT | TPM_LOCALITY_CHANGE_INT |
	    TPM_STS_VALID_INT | TPM_DATA_AVAIL_INT)))
#ifdef __FreeBSD__
		return;
#else
		return 0;
#endif
	if (r & TPM_STS_VALID_INT)
		wakeup(sc);

	if (r & TPM_CMD_READY_INT)
		wakeup(sc->sc_write);

	if (r & TPM_DATA_AVAIL_INT)
		wakeup(sc->sc_read);

	if (r & TPM_LOCALITY_CHANGE_INT)
		wakeup(sc->sc_init);

	bus_space_write_4(sc->sc_bt, sc->sc_bh, TPM_INT_STATUS, r);

#ifdef __FreeBSD__
	return;
#else
	return 1;
#endif
}

/* Read single byte using legacy interface. */
static inline u_int8_t
tpm_legacy_in(bus_space_tag_t iot, bus_space_handle_t ioh, int reg)
{
	bus_space_write_1(iot, ioh, 0, reg);
	return bus_space_read_1(iot, ioh, 1);
}

#if 0
/* Write single byte using legacy interface. */
static inline void
tpm_legacy_out(bus_space_tag_t iot, bus_space_handle_t ioh, int reg, u_int8_t v)
{
	bus_space_write_1(iot, ioh, 0, reg);
	bus_space_write_1(iot, ioh, 1, v);
}
#endif

/* Probe for TPM using legacy interface. */
int
tpm_legacy_probe(bus_space_tag_t iot, bus_addr_t iobase)
{
	bus_space_handle_t ioh;
	u_int8_t r, v;
	int i, rv = 0;
	char id[8];

	if (!tpm_enabled || iobase == -1)
		return 0;

	if (bus_space_map(iot, iobase, 2, 0, &ioh))
		return 0;

	v = bus_space_read_1(iot, ioh, 0);
	if (v == 0xff) {
		bus_space_unmap(iot, ioh, 2);
		return 0;
	}
	r = bus_space_read_1(iot, ioh, 1);

	for (i = sizeof(id); i--; )
		id[i] = tpm_legacy_in(iot, ioh, TPM_ID + i);

#ifdef TPM_DEBUG
	printf("tpm_legacy_probe %.4s %d.%d.%d.%d\n",
	    &id[4], id[0], id[1], id[2], id[3]);
#endif
	/*
	 * The only chips using the legacy interface we are aware of are
	 * by Atmel.  For other chips more signature would have to be added.
	 */
	if (!bcmp(&id[4], "ATML", 4))
		rv = 1;

	if (!rv) {
		bus_space_write_1(iot, ioh, r, 1);
		bus_space_write_1(iot, ioh, v, 0);
	}
	bus_space_unmap(iot, ioh, 2);

	return rv;
}

/* Setup TPM using legacy interface. */
int
tpm_legacy_init(struct tpm_softc *sc, int irq, const char *name)
{
	char id[8];
	u_int8_t ioh, iol;
	int i;

	if ((i = bus_space_map(sc->sc_batm, tpm_enabled, 2, 0, &sc->sc_bahm))) {
		printf(": cannot map tpm registers (%d)\n", i);
		tpm_enabled = 0;
		return 1;
	}

	for (i = sizeof(id); i--; )
		id[i] = tpm_legacy_in(sc->sc_bt, sc->sc_bh, TPM_ID + i);

	printf(": %.4s %d.%d @0x%x\n", &id[4], id[0], id[1], tpm_enabled);
	iol = tpm_enabled & 0xff;
	ioh = tpm_enabled >> 16;
	tpm_enabled = 0;

	return 0;
}

/* Start transaction. */
int
tpm_legacy_start(struct tpm_softc *sc, int flag)
{
	struct timeval tv;
	u_int8_t bits, r;
	int to, rv;

	bits = flag == UIO_READ ? TPM_LEGACY_DA : 0;
	tv.tv_sec = TPM_LEGACY_TMO;
	tv.tv_usec = 0;
	to = tvtohz(&tv) / TPM_LEGACY_SLEEP;
	while (((r = bus_space_read_1(sc->sc_batm, sc->sc_bahm, 1)) &
	    (TPM_LEGACY_BUSY|bits)) != bits && to--) {
		rv = tsleep(sc, PRIBIO | PCATCH, "legacy_tpm_start",
		    TPM_LEGACY_SLEEP);
		if (rv && rv != EWOULDBLOCK)
			return rv;
	}

#if defined(TPM_DEBUG) && !defined(__FreeBSD__)
	printf("%s: bits %b\n", sc->sc_dev.dv_xname, r, TPM_LEGACY_BITS);
#endif
	if ((r & (TPM_LEGACY_BUSY|bits)) != bits)
		return EIO;

	return 0;
}

int
tpm_legacy_read(struct tpm_softc *sc, void *buf, int len, size_t *count,
    int flags)
{
	u_int8_t *p;
	size_t cnt;
	int to, rv;

	cnt = rv = 0;
	for (p = buf; !rv && len > 0; len--) {
		for (to = 1000;
		    !(bus_space_read_1(sc->sc_batm, sc->sc_bahm, 1) &
		    TPM_LEGACY_DA); DELAY(1))
			if (!to--)
				return EIO;

		DELAY(TPM_LEGACY_DELAY);
		*p++ = bus_space_read_1(sc->sc_batm, sc->sc_bahm, 0);
		cnt++;
	}

	*count = cnt;
	return 0;
}

int
tpm_legacy_write(struct tpm_softc *sc, void *buf, int len)
{
	u_int8_t *p;
	int n;

	for (p = buf, n = len; n--; DELAY(TPM_LEGACY_DELAY)) {
		if (!n && len != TPM_BUFSIZ) {
			bus_space_write_1(sc->sc_batm, sc->sc_bahm, 1,
			    TPM_LEGACY_LAST);
			DELAY(TPM_LEGACY_DELAY);
		}
		bus_space_write_1(sc->sc_batm, sc->sc_bahm, 0, *p++);
	}

	return 0;
}

/* Finish transaction. */
int
tpm_legacy_end(struct tpm_softc *sc, int flag, int rv)
{
	struct timeval tv;
	u_int8_t r;
	int to;

	if (rv || flag == UIO_READ)
		bus_space_write_1(sc->sc_batm, sc->sc_bahm, 1, TPM_LEGACY_ABRT);
	else {
		tv.tv_sec = TPM_LEGACY_TMO;
		tv.tv_usec = 0;
		to = tvtohz(&tv) / TPM_LEGACY_SLEEP;
		while(((r = bus_space_read_1(sc->sc_batm, sc->sc_bahm, 1)) &
		    TPM_LEGACY_BUSY) && to--) {
			rv = tsleep(sc, PRIBIO | PCATCH, "legacy_tpm_end",
			    TPM_LEGACY_SLEEP);
			if (rv && rv != EWOULDBLOCK)
				return rv;
		}

#if defined(TPM_DEBUG) && !defined(__FreeBSD__)
		printf("%s: bits %b\n", sc->sc_dev.dv_xname, r, TPM_LEGACY_BITS);
#endif
		if (r & TPM_LEGACY_BUSY)
			return EIO;

		if (r & TPM_LEGACY_RE)
			return EIO;	/* XXX Retry the loop? */
	}

	return rv;
}

int
#ifdef __FreeBSD__
tpmopen(struct cdev *dev, int flag, int mode, struct thread *td)
#else
tpmopen(dev_t dev, int flag, int mode, struct proc *p)
#endif
{
	struct tpm_softc *sc = TPMSOFTC(dev);

	if (!sc)
		return ENXIO;

	if (sc->sc_flags & TPM_OPEN)
		return EBUSY;

	sc->sc_flags |= TPM_OPEN;

	return 0;
}

int
#ifdef __FreeBSD__
tpmclose(struct cdev *dev, int flag, int mode, struct thread *td)
#else
tpmclose(dev_t dev, int flag, int mode, struct proc *p)
#endif
{
	struct tpm_softc *sc = TPMSOFTC(dev);

	if (!sc)
		return ENXIO;

	if (!(sc->sc_flags & TPM_OPEN))
		return EINVAL;

	sc->sc_flags &= ~TPM_OPEN;

	return 0;
}

int
#ifdef __FreeBSD__
tpmread(struct cdev *dev, struct uio *uio, int flags)
#else
tpmread(dev_t dev, struct uio *uio, int flags)
#endif
{
	struct tpm_softc *sc = TPMSOFTC(dev);
	u_int8_t buf[TPM_BUFSIZ], *p;
	size_t cnt;
	int n, len, rv, s;

	if (!sc)
		return ENXIO;

	s = spltty();
	if ((rv = (sc->sc_start)(sc, UIO_READ))) {
		splx(s);
		return rv;
	}

#ifdef TPM_DEBUG
	printf("tpmread: getting header\n");
#endif
	if ((rv = (sc->sc_read)(sc, buf, TPM_HDRSIZE, &cnt, 0))) {
		(sc->sc_end)(sc, UIO_READ, rv);
		splx(s);
		return rv;
	}

	len = (buf[2] << 24) | (buf[3] << 16) | (buf[4] << 8) | buf[5];
#ifdef TPM_DEBUG
	printf("tpmread: len %d, io count %d\n", len, uio->uio_resid);
#endif
	if (len > uio->uio_resid) {
		rv = EIO;
		(sc->sc_end)(sc, UIO_READ, rv);
#ifdef TPM_DEBUG
		printf("tpmread: bad residual io count 0x%x\n", uio->uio_resid);
#endif
		splx(s);
		return rv;
	}

	/* Copy out header. */
	if ((rv = uiomove((caddr_t)buf, cnt, uio))) {
		(sc->sc_end)(sc, UIO_READ, rv);
		splx(s);
		return rv;
	}

	/* Get remaining part of the answer (if anything is left). */
	for (len -= cnt, p = buf, n = sizeof(buf); len > 0; p = buf, len -= n,
	    n = sizeof(buf)) {
		n = MIN(n, len);
#ifdef TPM_DEBUG
		printf("tpmread: n %d len %d\n", n, len);
#endif
		if ((rv = (sc->sc_read)(sc, p, n, NULL, TPM_PARAM_SIZE))) {
			(sc->sc_end)(sc, UIO_READ, rv);
			splx(s);
			return rv;
		}
		p += n;
		if ((rv = uiomove((caddr_t)buf, p - buf, uio))) {
			(sc->sc_end)(sc, UIO_READ, rv);
			splx(s);
			return rv;
		}
	}

	rv = (sc->sc_end)(sc, UIO_READ, rv);
	splx(s);
	return rv;
}

int
#ifdef __FreeBSD__
tpmwrite(struct cdev *dev, struct uio *uio, int flags)
#else
tpmwrite(dev_t dev, struct uio *uio, int flags)
#endif
{
	struct tpm_softc *sc = TPMSOFTC(dev);
	u_int8_t buf[TPM_BUFSIZ];
	int n, rv, s;

	if (!sc)
		return ENXIO;

	s = spltty();

#ifdef TPM_DEBUG
	printf("tpmwrite: io count %d\n", uio->uio_resid);
#endif

	n = MIN(sizeof(buf), uio->uio_resid);
	if ((rv = uiomove((caddr_t)buf, n, uio))) {
		splx(s);
		return rv;
	}

	if ((rv = (sc->sc_start)(sc, UIO_WRITE))) {
		splx(s);
		return rv;
	}

	if ((rv = (sc->sc_write(sc, buf, n)))) {
		splx(s);
		return rv;
	}

	rv = (sc->sc_end)(sc, UIO_WRITE, rv);
	splx(s);
	return rv;
}

int
#ifdef __FreeBSD__
tpmioctl(struct cdev *dev, u_long cmd, caddr_t data, int flags,
    struct thread *td)
#else
tpmioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
#endif
{
	return ENOTTY;
}
