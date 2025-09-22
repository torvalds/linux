/*	$OpenBSD: piixpm.c,v 1.44 2024/05/24 06:02:58 jsg Exp $	*/

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
 * Intel PIIX and compatible Power Management controller driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/rwlock.h>

#include <machine/bus.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/pci/piixreg.h>

#include <dev/i2c/i2cvar.h>

#ifdef PIIXPM_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

#define PIIXPM_DELAY	200
#define PIIXPM_TIMEOUT	1

struct piixpm_smbus {
	int			 sb_bus;
	struct piixpm_softc	*sb_sc;
};

struct piixpm_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_space_handle_t	sc_sb800_ioh;
	void *			sc_ih;
	int			sc_poll;
	int			sc_is_sb800;
	int			sc_is_fch;

	struct piixpm_smbus	sc_busses[4];
	struct i2c_controller	sc_i2c_tag[4];
	struct rwlock		sc_i2c_lock;
	struct {
		i2c_op_t     op;
		void *       buf;
		size_t       len;
		int          flags;
		volatile int error;
	}			sc_i2c_xfer;
};

int	piixpm_match(struct device *, void *, void *);
void	piixpm_attach(struct device *, struct device *, void *);

int	piixpm_i2c_acquire_bus(void *, int);
void	piixpm_i2c_release_bus(void *, int);
int	piixpm_i2c_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
	    void *, size_t, int);

int	piixpm_intr(void *);

const struct cfattach piixpm_ca = {
	sizeof(struct piixpm_softc),
	piixpm_match,
	piixpm_attach
};

struct cfdriver piixpm_cd = {
	NULL, "piixpm", DV_DULL
};

const struct pci_matchid piixpm_ids[] = {
	{ PCI_VENDOR_AMD, PCI_PRODUCT_AMD_HUDSON2_SMB },
	{ PCI_VENDOR_AMD, PCI_PRODUCT_AMD_KERNCZ_SMB },

	{ PCI_VENDOR_ATI, PCI_PRODUCT_ATI_SB200_SMB },
	{ PCI_VENDOR_ATI, PCI_PRODUCT_ATI_SB300_SMB },
	{ PCI_VENDOR_ATI, PCI_PRODUCT_ATI_SB400_SMB },
	{ PCI_VENDOR_ATI, PCI_PRODUCT_ATI_SBX00_SMB },

	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82371AB_PM },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_82440MX_PM },

	{ PCI_VENDOR_RCC, PCI_PRODUCT_RCC_CSB5 },
	{ PCI_VENDOR_RCC, PCI_PRODUCT_RCC_CSB6 },
	{ PCI_VENDOR_RCC, PCI_PRODUCT_RCC_HT_1000 },
	{ PCI_VENDOR_RCC, PCI_PRODUCT_RCC_HT_1100 },
	{ PCI_VENDOR_RCC, PCI_PRODUCT_RCC_OSB4 },

	{ PCI_VENDOR_SMSC, PCI_PRODUCT_SMSC_VICTORY66_PM }
};

int
piixpm_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, piixpm_ids,
	    sizeof(piixpm_ids) / sizeof(piixpm_ids[0])));
}

void
piixpm_attach(struct device *parent, struct device *self, void *aux)
{
	struct piixpm_softc *sc = (struct piixpm_softc *)self;
	struct pci_attach_args *pa = aux;
	bus_space_handle_t ioh;
	u_int16_t val, smb0en;
	bus_addr_t base;
	pcireg_t conf;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	struct i2cbus_attach_args iba;
	int numbusses, i;

	sc->sc_iot = pa->pa_iot;
	numbusses = 1;

	if ((PCI_VENDOR(pa->pa_id) == PCI_VENDOR_AMD &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_AMD_HUDSON2_SMB) ||
	    (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_AMD &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_AMD_KERNCZ_SMB) ||
	    (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_ATI &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ATI_SBX00_SMB &&
	    PCI_REVISION(pa->pa_class) >= 0x40)) {
		/* 
		 * On the AMD SB800+, the SMBus I/O registers are well
		 * hidden.  We need to look at the "SMBus0En" Power
		 * Management register to find out where they live.
		 * We use indirect IO access through the index/data
		 * pair at 0xcd6/0xcd7 to access "SMBus0En".
		 */
		if (bus_space_map(sc->sc_iot, SB800_PMREG_BASE,
		    SB800_PMREG_SIZE, 0, &ioh) != 0) {
			printf(": can't map i/o space\n");
			return;
		}

		/*
		 * AMD Bolton matches PCI_PRODUCT_AMD_HUDSON2_SMB but
		 * uses old register layout. Therefor check PCI_REVISION.
		 */
		if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_AMD &&
		    ((PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_AMD_HUDSON2_SMB &&
		    PCI_REVISION(pa->pa_class) >= 0x1f) ||
		    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_AMD_KERNCZ_SMB)) {
			bus_space_write_1(sc->sc_iot, ioh, 0,
			    AMDFCH41_PM_DECODE_EN);
			val = bus_space_read_1(sc->sc_iot, ioh, 1);
			smb0en = val & AMDFCH41_SMBUS_EN;

			bus_space_write_1(sc->sc_iot, ioh, 0,
			    AMDFCH41_PM_DECODE_EN + 1);
			val = bus_space_read_1(sc->sc_iot, ioh, 1) << 8;
			base = val;

			sc->sc_is_fch = 1;
			numbusses = 2;
		} else {
			/* Read "SmBus0En" */
			bus_space_write_1(sc->sc_iot, ioh, 0,
			    SB800_PMREG_SMB0EN);
			val = bus_space_read_1(sc->sc_iot, ioh, 1);

			bus_space_write_1(sc->sc_iot, ioh, 0,
			    SB800_PMREG_SMB0EN + 1);
			val |= (bus_space_read_1(sc->sc_iot, ioh, 1) << 8);
			smb0en = val & SB800_SMB0EN_EN;
			base = val & SB800_SMB0EN_BASE_MASK;

			bus_space_write_1(sc->sc_iot, ioh, 0,
			    SB800_PMREG_SMB0SELEN);
			val = bus_space_read_1(sc->sc_iot, ioh, 1);
			if (val & SB800_SMB0SELEN_EN) {
				sc->sc_is_sb800 = 1;
				numbusses = 4;
			}
		}

		if (smb0en == 0) {
			printf(": SMBus disabled\n");
			bus_space_unmap(sc->sc_iot, ioh, SB800_PMREG_SIZE);
			return;
		}

		sc->sc_sb800_ioh = ioh;

		/* Map I/O space */
		if (base == 0 || bus_space_map(sc->sc_iot, base,
		    SB800_SMB_SIZE, 0, &sc->sc_ioh)) {
			printf(": can't map i/o space");
			bus_space_unmap(sc->sc_iot, ioh, SB800_PMREG_SIZE);
			return;
		}

		/* Read configuration */
		conf = bus_space_read_1(sc->sc_iot, sc->sc_ioh,
		    SB800_SMB_HOSTC);
		if (conf & SB800_SMB_HOSTC_INTMASK)
			conf = PIIX_SMB_HOSTC_IRQ;
		else
			conf = PIIX_SMB_HOSTC_SMI;
	} else {
		/* Read configuration */
		conf = pci_conf_read(pa->pa_pc, pa->pa_tag, PIIX_SMB_HOSTC);
		DPRINTF((": conf 0x%08x", conf));

		if ((conf & PIIX_SMB_HOSTC_HSTEN) == 0) {
			printf(": SMBus disabled\n");
			return;
		}

		/* Map I/O space */
		base = pci_conf_read(pa->pa_pc, pa->pa_tag, PIIX_SMB_BASE) &
		    PIIX_SMB_BASE_MASK;
		if (base == 0 || bus_space_map(sc->sc_iot, base,
		    PIIX_SMB_SIZE, 0, &sc->sc_ioh)) {
			printf(": can't map i/o space\n");
			return;
		}
	}

	sc->sc_poll = 1;
	if ((conf & PIIX_SMB_HOSTC_INTMASK) == PIIX_SMB_HOSTC_SMI) {
		/* No PCI IRQ */
		printf(": SMI");
	} else {
		if ((conf & PIIX_SMB_HOSTC_INTMASK) == PIIX_SMB_HOSTC_IRQ) {
			/* Install interrupt handler */
			if (pci_intr_map(pa, &ih) == 0) {
				intrstr = pci_intr_string(pa->pa_pc, ih);
				sc->sc_ih = pci_intr_establish(pa->pa_pc,
				    ih, IPL_BIO, piixpm_intr, sc,
				    sc->sc_dev.dv_xname);
				if (sc->sc_ih != NULL) {
					printf(": %s", intrstr);
					sc->sc_poll = 0;
				}
			}
		}
		if (sc->sc_poll)
			printf(": polling");
	}

	printf("\n");

	/* Attach I2C bus */
	rw_init(&sc->sc_i2c_lock, "iiclk");
	for (i = 0; i < numbusses; i++) {
		sc->sc_busses[i].sb_bus = i;
		sc->sc_busses[i].sb_sc = sc;
		sc->sc_i2c_tag[i].ic_cookie = &sc->sc_busses[i];
		sc->sc_i2c_tag[i].ic_acquire_bus = piixpm_i2c_acquire_bus;
		sc->sc_i2c_tag[i].ic_release_bus = piixpm_i2c_release_bus;
		sc->sc_i2c_tag[i].ic_exec = piixpm_i2c_exec;

		bzero(&iba, sizeof(iba));
		iba.iba_name = "iic";
		iba.iba_tag = &sc->sc_i2c_tag[i];
		config_found(self, &iba, iicbus_print);
	}

	return;
}

int
piixpm_i2c_acquire_bus(void *cookie, int flags)
{
	struct piixpm_smbus *smbus = cookie;
	struct piixpm_softc *sc = smbus->sb_sc;
	int rc;

	if (!cold && !sc->sc_poll && !(flags & I2C_F_POLL))
		if ((rc = rw_enter(&sc->sc_i2c_lock, RW_WRITE | RW_INTR)) != 0)
			return (rc);

	if (sc->sc_is_fch) {
		bus_space_write_1(sc->sc_iot, sc->sc_sb800_ioh, 0,
		    AMDFCH41_PM_PORT_INDEX);
		bus_space_write_1(sc->sc_iot, sc->sc_sb800_ioh, 1,
		    smbus->sb_bus << 3);
	} else if (sc->sc_is_sb800) {
		bus_space_write_1(sc->sc_iot, sc->sc_sb800_ioh, 0,
		    SB800_PMREG_SMB0SEL);
		bus_space_write_1(sc->sc_iot, sc->sc_sb800_ioh, 1,
		    smbus->sb_bus << 1);
	}

	return (0);
}

void
piixpm_i2c_release_bus(void *cookie, int flags)
{
	struct piixpm_smbus *smbus = cookie;
	struct piixpm_softc *sc = smbus->sb_sc;

	if (sc->sc_is_fch) {
		bus_space_write_1(sc->sc_iot, sc->sc_sb800_ioh, 0,
		    AMDFCH41_PM_PORT_INDEX);
		bus_space_write_1(sc->sc_iot, sc->sc_sb800_ioh, 1,
		    0);
	} else if (sc->sc_is_sb800) {
		bus_space_write_1(sc->sc_iot, sc->sc_sb800_ioh, 0,
		    SB800_PMREG_SMB0SEL);
		bus_space_write_1(sc->sc_iot, sc->sc_sb800_ioh, 1,
		    0);
	}

	if (cold || sc->sc_poll || (flags & I2C_F_POLL))
		return;
	rw_exit(&sc->sc_i2c_lock);
}

int
piixpm_i2c_exec(void *cookie, i2c_op_t op, i2c_addr_t addr,
    const void *cmdbuf, size_t cmdlen, void *buf, size_t len, int flags)
{
	struct piixpm_smbus *smbus = cookie;
	struct piixpm_softc *sc = smbus->sb_sc;
	u_int8_t *b;
	u_int8_t ctl, st;
	int retries;

	DPRINTF(("%s: exec: op %d, addr 0x%02x, cmdlen %zu, len %zu, "
	    "flags 0x%02x\n", sc->sc_dev.dv_xname, op, addr, cmdlen,
	    len, flags));

	/* Wait for bus to be idle */
	for (retries = 100; retries > 0; retries--) {
		st = bus_space_read_1(sc->sc_iot, sc->sc_ioh, PIIX_SMB_HS);
		if (!(st & PIIX_SMB_HS_BUSY))
			break;
		DELAY(PIIXPM_DELAY);
	}
	DPRINTF(("%s: exec: st 0x%b\n", sc->sc_dev.dv_xname, st,
	    PIIX_SMB_HS_BITS));
	if (st & PIIX_SMB_HS_BUSY)
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
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, PIIX_SMB_TXSLVA,
	    PIIX_SMB_TXSLVA_ADDR(addr) |
	    (I2C_OP_READ_P(op) ? PIIX_SMB_TXSLVA_READ : 0));

	b = (void *)cmdbuf;
	if (cmdlen > 0)
		/* Set command byte */
		bus_space_write_1(sc->sc_iot, sc->sc_ioh, PIIX_SMB_HCMD, b[0]);

	if (I2C_OP_WRITE_P(op)) {
		/* Write data */
		b = buf;
		if (len > 0)
			bus_space_write_1(sc->sc_iot, sc->sc_ioh,
			    PIIX_SMB_HD0, b[0]);
		if (len > 1)
			bus_space_write_1(sc->sc_iot, sc->sc_ioh,
			    PIIX_SMB_HD1, b[1]);
	}

	/* Set SMBus command */
	if (len == 0)
		ctl = PIIX_SMB_HC_CMD_BYTE;
	else if (len == 1)
		ctl = PIIX_SMB_HC_CMD_BDATA;
	else if (len == 2)
		ctl = PIIX_SMB_HC_CMD_WDATA;
	else
		panic("%s: unexpected len %zd", __func__, len);

	if ((flags & I2C_F_POLL) == 0)
		ctl |= PIIX_SMB_HC_INTREN;

	/* Start transaction */
	ctl |= PIIX_SMB_HC_START;
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, PIIX_SMB_HC, ctl);

	if (flags & I2C_F_POLL) {
		/* Poll for completion */
		DELAY(PIIXPM_DELAY);
		for (retries = 1000; retries > 0; retries--) {
			st = bus_space_read_1(sc->sc_iot, sc->sc_ioh,
			    PIIX_SMB_HS);
			if ((st & PIIX_SMB_HS_BUSY) == 0)
				break;
			DELAY(PIIXPM_DELAY);
		}
		if (st & PIIX_SMB_HS_BUSY)
			goto timeout;
		piixpm_intr(sc);
	} else {
		/* Wait for interrupt */
		if (tsleep_nsec(sc, PRIBIO, "piixpm",
		    SEC_TO_NSEC(PIIXPM_TIMEOUT)))
			goto timeout;
	}

	if (sc->sc_i2c_xfer.error)
		return (1);

	return (0);

timeout:
	/*
	 * Transfer timeout. Kill the transaction and clear status bits.
	 */
	printf("%s: exec: op %d, addr 0x%02x, cmdlen %zu, len %zu, "
	    "flags 0x%02x: timeout, status 0x%b\n",
	    sc->sc_dev.dv_xname, op, addr, cmdlen, len, flags,
	    st, PIIX_SMB_HS_BITS);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, PIIX_SMB_HC,
	    PIIX_SMB_HC_KILL);
	DELAY(PIIXPM_DELAY);
	st = bus_space_read_1(sc->sc_iot, sc->sc_ioh, PIIX_SMB_HS);
	if ((st & PIIX_SMB_HS_FAILED) == 0)
		printf("%s: abort failed, status 0x%b\n",
		    sc->sc_dev.dv_xname, st, PIIX_SMB_HS_BITS);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, PIIX_SMB_HS, st);
	return (1);
}

int
piixpm_intr(void *arg)
{
	struct piixpm_softc *sc = arg;
	u_int8_t st;
	u_int8_t *b;
	size_t len;

	/* Read status */
	st = bus_space_read_1(sc->sc_iot, sc->sc_ioh, PIIX_SMB_HS);
	if ((st & PIIX_SMB_HS_BUSY) != 0 || (st & (PIIX_SMB_HS_INTR |
	    PIIX_SMB_HS_DEVERR | PIIX_SMB_HS_BUSERR |
	    PIIX_SMB_HS_FAILED)) == 0)
		/* Interrupt was not for us */
		return (0);

	DPRINTF(("%s: intr st 0x%b\n", sc->sc_dev.dv_xname, st,
	    PIIX_SMB_HS_BITS));

	/* Clear status bits */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, PIIX_SMB_HS, st);

	/* Check for errors */
	if (st & (PIIX_SMB_HS_DEVERR | PIIX_SMB_HS_BUSERR |
	    PIIX_SMB_HS_FAILED)) {
		sc->sc_i2c_xfer.error = 1;
		goto done;
	}

	if (st & PIIX_SMB_HS_INTR) {
		if (I2C_OP_WRITE_P(sc->sc_i2c_xfer.op))
			goto done;

		/* Read data */
		b = sc->sc_i2c_xfer.buf;
		len = sc->sc_i2c_xfer.len;
		if (len > 0)
			b[0] = bus_space_read_1(sc->sc_iot, sc->sc_ioh,
			    PIIX_SMB_HD0);
		if (len > 1)
			b[1] = bus_space_read_1(sc->sc_iot, sc->sc_ioh,
			    PIIX_SMB_HD1);
	}

done:
	if ((sc->sc_i2c_xfer.flags & I2C_F_POLL) == 0)
		wakeup(sc);
	return (1);
}
