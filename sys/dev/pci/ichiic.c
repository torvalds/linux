/*	$OpenBSD: ichiic.c,v 1.58 2025/08/21 03:06:20 jsg Exp $	*/

/*
 * Copyright (c) 2005, 2006 Alexander Yurchenko <grange@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Intel ICH SMBus controller driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/rwlock.h>

#include <machine/bus.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/pci/ichreg.h>

#include <dev/i2c/i2cvar.h>

#ifdef ICHIIC_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

#define ICHIIC_DELAY	100
#define ICHIIC_TIMEOUT	1

struct ichiic_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	void *			sc_ih;
	int			sc_poll;

	struct i2c_controller	sc_i2c_tag;
	struct rwlock		sc_i2c_lock;
	struct {
		i2c_op_t     op;
		void *       buf;
		size_t       len;
		int          flags;
		volatile int error;
	}			sc_i2c_xfer;
};

int	ichiic_match(struct device *, void *, void *);
void	ichiic_attach(struct device *, struct device *, void *);

int	ichiic_i2c_acquire_bus(void *, int);
void	ichiic_i2c_release_bus(void *, int);
int	ichiic_i2c_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
	    void *, size_t, int);

int	ichiic_intr(void *);

const struct cfattach ichiic_ca = {
	sizeof(struct ichiic_softc),
	ichiic_match,
	ichiic_attach
};

struct cfdriver ichiic_cd = {
	NULL, "ichiic", DV_DULL
};

const struct pci_matchid ichiic_ids[] = {
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_3400_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_6SERIES_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_6300ESB_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_6321ESB_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_7SERIES_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_8SERIES_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_8SERIES_LP_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_9SERIES_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_9SERIES_LP_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801AA_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801AB_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801BA_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801CA_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801DB_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801E_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801EB_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801FB_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801GB_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801H_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801I_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801JD_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82801JI_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_APOLLOLAKE_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ATOMC2000_PCU_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_C3000_SMB_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_BAYTRAIL_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_BRASWELL_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_C600_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_C600_SMB_IDF_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_C600_SMB_IDF_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_C600_SMB_IDF_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_C610_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_C610_MS_SMB_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_C610_MS_SMB_2 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_C610_MS_SMB_3 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_C620_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_C740_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_DH8900_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EP80579_SMBUS },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_GLK_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_100SERIES_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_100SERIES_LP_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_200SERIES_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_300SERIES_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_300SERIES_U_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_LP_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_400SERIES_V_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_495SERIES_LP_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_500SERIES_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_500SERIES_LP_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_600SERIES_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_600SERIES_LP_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_700SERIES_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_JSL_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EHL_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ADL_N_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_MTL_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_LNL_SMB },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_ARL_U_SMB },
};

int
ichiic_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, ichiic_ids,
	    sizeof(ichiic_ids) / sizeof(ichiic_ids[0])));
}

void
ichiic_attach(struct device *parent, struct device *self, void *aux)
{
	struct ichiic_softc *sc = (struct ichiic_softc *)self;
	struct pci_attach_args *pa = aux;
	struct i2cbus_attach_args iba;
	pcireg_t conf;
	bus_size_t iosize;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;

	/* Read configuration */
	conf = pci_conf_read(pa->pa_pc, pa->pa_tag, ICH_SMB_HOSTC);
	DPRINTF((": conf 0x%08x", conf));

	if ((conf & ICH_SMB_HOSTC_HSTEN) == 0) {
		printf(": SMBus disabled\n");
		return;
	}

	/* Map I/O space */
	if (pci_mapreg_map(pa, ICH_SMB_BASE, PCI_MAPREG_TYPE_IO, 0,
	    &sc->sc_iot, &sc->sc_ioh, NULL, &iosize, 0)) {
		printf(": can't map i/o space\n");
		return;
	}

	sc->sc_poll = 1;
	if (conf & ICH_SMB_HOSTC_SMIEN) {
		/* No PCI IRQ */
		printf(": SMI");
	} else {
		/* Install interrupt handler */
		if (pci_intr_map(pa, &ih) == 0) {
			intrstr = pci_intr_string(pa->pa_pc, ih);
			sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_BIO,
			    ichiic_intr, sc, sc->sc_dev.dv_xname);
			if (sc->sc_ih != NULL) {
				printf(": %s", intrstr);
				sc->sc_poll = 0;
			}
		}
		if (sc->sc_poll)
			printf(": polling");
	}

	printf("\n");

	/* Attach I2C bus */
	rw_init(&sc->sc_i2c_lock, "iiclk");
	sc->sc_i2c_tag.ic_cookie = sc;
	sc->sc_i2c_tag.ic_acquire_bus = ichiic_i2c_acquire_bus;
	sc->sc_i2c_tag.ic_release_bus = ichiic_i2c_release_bus;
	sc->sc_i2c_tag.ic_exec = ichiic_i2c_exec;

	bzero(&iba, sizeof(iba));
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_i2c_tag;
	config_found(self, &iba, iicbus_print);

	return;
}

int
ichiic_i2c_acquire_bus(void *cookie, int flags)
{
	struct ichiic_softc *sc = cookie;

	if (cold || sc->sc_poll || (flags & I2C_F_POLL))
		return (0);

	return (rw_enter(&sc->sc_i2c_lock, RW_WRITE | RW_INTR));
}

void
ichiic_i2c_release_bus(void *cookie, int flags)
{
	struct ichiic_softc *sc = cookie;

	if (cold || sc->sc_poll || (flags & I2C_F_POLL))
		return;

	rw_exit(&sc->sc_i2c_lock);
}

int
ichiic_i2c_exec(void *cookie, i2c_op_t op, i2c_addr_t addr,
    const void *cmdbuf, size_t cmdlen, void *buf, size_t len, int flags)
{
	struct ichiic_softc *sc = cookie;
	u_int8_t *b;
	u_int8_t ctl, st;
	int retries;

	DPRINTF(("%s: exec: op %d, addr 0x%02x, cmdlen %d, len %d, "
	    "flags 0x%02x\n", sc->sc_dev.dv_xname, op, addr, cmdlen,
	    len, flags));

	/* Wait for bus to be idle */
	for (retries = 100; retries > 0; retries--) {
		st = bus_space_read_1(sc->sc_iot, sc->sc_ioh, ICH_SMB_HS);
		if (!(st & ICH_SMB_HS_BUSY))
			break;
		DELAY(ICHIIC_DELAY);
	}
	DPRINTF(("%s: exec: st 0x%b\n", sc->sc_dev.dv_xname, st,
	    ICH_SMB_HS_BITS));
	if (st & ICH_SMB_HS_BUSY)
		return (1);

	if (cold || sc->sc_poll)
		flags |= I2C_F_POLL;

	if (!I2C_OP_STOP_P(op) || cmdlen > 1 || len > 2)
		return (1);

	/* Setup transfer */
	sc->sc_i2c_xfer.op = op;
	sc->sc_i2c_xfer.buf = buf;
	sc->sc_i2c_xfer.len = len;
	sc->sc_i2c_xfer.flags = flags;
	sc->sc_i2c_xfer.error = 0;

	/* Set slave address and transfer direction */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, ICH_SMB_TXSLVA,
	    ICH_SMB_TXSLVA_ADDR(addr) |
	    (I2C_OP_READ_P(op) ? ICH_SMB_TXSLVA_READ : 0));

	b = (void *)cmdbuf;
	if (cmdlen > 0)
		/* Set command byte */
		bus_space_write_1(sc->sc_iot, sc->sc_ioh, ICH_SMB_HCMD, b[0]);

	if (I2C_OP_WRITE_P(op)) {
		/* Write data */
		b = buf;
		if (len > 0)
			bus_space_write_1(sc->sc_iot, sc->sc_ioh,
			    ICH_SMB_HD0, b[0]);
		if (len > 1)
			bus_space_write_1(sc->sc_iot, sc->sc_ioh,
			    ICH_SMB_HD1, b[1]);
	}

	/* Set SMBus command */
	if (len == 0)
		ctl = ICH_SMB_HC_CMD_BYTE;
	else if (len == 1)
		ctl = ICH_SMB_HC_CMD_BDATA;
	else if (len == 2)
		ctl = ICH_SMB_HC_CMD_WDATA;
	else
		panic("%s: unexpected len %zd", __func__, len);

	if ((flags & I2C_F_POLL) == 0)
		ctl |= ICH_SMB_HC_INTREN;

	/* Start transaction */
	ctl |= ICH_SMB_HC_START;
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, ICH_SMB_HC, ctl);

	if (flags & I2C_F_POLL) {
		/* Poll for completion */
		DELAY(ICHIIC_DELAY);
		for (retries = 1000; retries > 0; retries--) {
			st = bus_space_read_1(sc->sc_iot, sc->sc_ioh,
			    ICH_SMB_HS);
			if ((st & ICH_SMB_HS_BUSY) == 0)
				break;
			DELAY(ICHIIC_DELAY);
		}
		if (st & ICH_SMB_HS_BUSY)
			goto timeout;
		ichiic_intr(sc);
	} else {
		/* Wait for interrupt */
		if (tsleep_nsec(sc, PRIBIO, "ichiic",
		    SEC_TO_NSEC(ICHIIC_TIMEOUT)))
			goto timeout;
	}

	if (sc->sc_i2c_xfer.error)
		return (1);

	return (0);

timeout:
	/*
	 * Transfer timeout. Kill the transaction and clear status bits.
	 */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, ICH_SMB_HC,
	    ICH_SMB_HC_KILL);
	DELAY(ICHIIC_DELAY);
	st = bus_space_read_1(sc->sc_iot, sc->sc_ioh, ICH_SMB_HS);
	if ((st & ICH_SMB_HS_FAILED) == 0)
		printf("%s: abort failed, status 0x%b\n",
		    sc->sc_dev.dv_xname, st, ICH_SMB_HS_BITS);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, ICH_SMB_HS, st);
	return (1);
}

int
ichiic_intr(void *arg)
{
	struct ichiic_softc *sc = arg;
	u_int8_t st;
	u_int8_t *b;
	size_t len;

	/* Read status */
	st = bus_space_read_1(sc->sc_iot, sc->sc_ioh, ICH_SMB_HS);

	/* Clear status bits */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, ICH_SMB_HS, st);

	/* XXX Ignore SMBALERT# for now */
	if ((st & ICH_SMB_HS_BUSY) != 0 || (st & (ICH_SMB_HS_INTR |
	    ICH_SMB_HS_DEVERR | ICH_SMB_HS_BUSERR | ICH_SMB_HS_FAILED |
	    ICH_SMB_HS_BDONE)) == 0)
		/* Interrupt was not for us */
		return (0);

	DPRINTF(("%s: intr st 0x%b\n", sc->sc_dev.dv_xname, st,
	    ICH_SMB_HS_BITS));

	/* Check for errors */
	if (st & (ICH_SMB_HS_DEVERR | ICH_SMB_HS_BUSERR | ICH_SMB_HS_FAILED)) {
		sc->sc_i2c_xfer.error = 1;
		goto done;
	}

	if (st & ICH_SMB_HS_INTR) {
		if (I2C_OP_WRITE_P(sc->sc_i2c_xfer.op))
			goto done;

		/* Read data */
		b = sc->sc_i2c_xfer.buf;
		len = sc->sc_i2c_xfer.len;
		if (len > 0)
			b[0] = bus_space_read_1(sc->sc_iot, sc->sc_ioh,
			    ICH_SMB_HD0);
		if (len > 1)
			b[1] = bus_space_read_1(sc->sc_iot, sc->sc_ioh,
			    ICH_SMB_HD1);
	}

done:
	if ((sc->sc_i2c_xfer.flags & I2C_F_POLL) == 0)
		wakeup(sc);
	return (1);
}
