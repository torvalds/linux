/*	$OpenBSD: amdpm.c,v 1.41 2025/07/15 13:40:02 jsg Exp $	*/

/*
 * Copyright (c) 2006 Alexander Yurchenko <grange@openbsd.org>
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

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Enami Tsugutomo.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/rwlock.h>
#include <sys/timeout.h>
#include <sys/timetc.h>

#include <machine/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <dev/i2c/i2cvar.h>

#ifdef AMDPM_DEBUG
#define DPRINTF(x...) printf(x)
#else
#define DPRINTF(x...)
#endif

#define AMDPM_SMBUS_DELAY	100
#define AMDPM_SMBUS_TIMEOUT	1

u_int amdpm_get_timecount(struct timecounter *tc);

#ifndef AMDPM_FREQUENCY
#define AMDPM_FREQUENCY 3579545
#endif

static struct timecounter amdpm_timecounter = {
	.tc_get_timecount = amdpm_get_timecount,
	.tc_counter_mask = 0xffffff,
	.tc_frequency = AMDPM_FREQUENCY,
	.tc_name = "AMDPM",
	.tc_quality = 1000,
	.tc_priv = NULL,
	.tc_user = 0,
};

#define	AMDPM_CONFREG	0x40

/* 0x40: General Configuration 1 Register */
#define	AMDPM_RNGEN	0x00000080	/* random number generator enable */
#define	AMDPM_STOPTMR	0x00000040	/* stop free-running timer */

/* 0x41: General Configuration 2 Register */
#define	AMDPM_PMIOEN	0x00008000	/* system management IO space enable */
#define	AMDPM_TMRRST	0x00004000	/* reset free-running timer */
#define	AMDPM_TMR32	0x00000800	/* extended (32 bit) timer enable */

/* 0x42: SCI Interrupt Configuration Register */
/* 0x43: Previous Power State Register */

#define	AMDPM_PMPTR	0x58		/* PMxx System Management IO space
					   Pointer */
#define NFPM_PMPTR	0x14		/* nForce System Management IO space
					   Pointer */
#define	AMDPM_PMBASE(x)	((x) & 0xff00)	/* PMxx base address */
#define	AMDPM_PMSIZE	256		/* PMxx space size */

/* Registers in PMxx space */
#define	AMDPM_TMR	0x08		/* 24/32 bit timer register */

#define	AMDPM_RNGDATA	0xf0		/* 32 bit random data register */
#define	AMDPM_RNGSTAT	0xf4		/* RNG status register */
#define	AMDPM_RNGDONE	0x00000001	/* Random number generation complete */

#define AMDPM_SMB_REGS  0xe0		/* offset of SMB register space */
#define AMDPM_SMB_SIZE  0xf		/* size of SMB register space */ 
#define AMDPM_SMBSTAT	0x0		/* SMBus status */
#define AMDPM_SMBSTAT_ABRT	(1 << 0)	/* transfer abort */
#define AMDPM_SMBSTAT_COL	(1 << 1)	/* collision */
#define AMDPM_SMBSTAT_PRERR	(1 << 2)	/* protocol error */
#define AMDPM_SMBSTAT_HBSY	(1 << 3)	/* host controller busy */
#define AMDPM_SMBSTAT_CYC	(1 << 4)	/* cycle complete */
#define AMDPM_SMBSTAT_TO	(1 << 5)	/* timeout */
#define AMDPM_SMBSTAT_SNP	(1 << 8)	/* snoop address match */
#define AMDPM_SMBSTAT_SLV	(1 << 9)	/* slave address match */
#define AMDPM_SMBSTAT_SMBA	(1 << 10)	/* SMBALERT# asserted */
#define AMDPM_SMBSTAT_BSY	(1 << 11)	/* bus busy */
#define AMDPM_SMBSTAT_BITS	"\020\001ABRT\002COL\003PRERR\004HBSY\005CYC\006TO\011SNP\012SLV\013SMBA\014BSY"
#define AMDPM_SMBCTL	0x2		/* SMBus control */
#define AMDPM_SMBCTL_CMD_QUICK	0		/* QUICK command */
#define AMDPM_SMBCTL_CMD_BYTE	1		/* BYTE command */
#define AMDPM_SMBCTL_CMD_BDATA	2		/* BYTE DATA command */
#define AMDPM_SMBCTL_CMD_WDATA	3		/* WORD DATA command */
#define AMDPM_SMBCTL_CMD_PCALL	4		/* PROCESS CALL command */
#define AMDPM_SMBCTL_CMD_BLOCK	5		/* BLOCK command */
#define AMDPM_SMBCTL_START	(1 << 3)	/* start transfer */
#define AMDPM_SMBCTL_CYCEN	(1 << 4)	/* intr on cycle complete */
#define AMDPM_SMBCTL_ABORT	(1 << 5)	/* abort transfer */
#define AMDPM_SMBCTL_SNPEN	(1 << 8)	/* intr on snoop addr match */
#define AMDPM_SMBCTL_SLVEN	(1 << 9)	/* intr on slave addr match */
#define AMDPM_SMBCTL_SMBAEN	(1 << 10)	/* intr on SMBALERT# */
#define AMDPM_SMBADDR	0x4		/* SMBus address */
#define AMDPM_SMBADDR_READ	(1 << 0)	/* read direction */
#define AMDPM_SMBADDR_ADDR(x)	(((x) & 0x7f) << 1) /* 7-bit address */
#define AMDPM_SMBDATA	0x6		/* SMBus data */
#define AMDPM_SMBCMD	0x8		/* SMBus command */


struct amdpm_softc {
	struct device sc_dev;

	pci_chipset_tag_t sc_pc;
	pcitag_t sc_tag;

	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;		/* PMxx space */
	bus_space_handle_t sc_i2c_ioh;		/* I2C space */
	int sc_poll;

	struct timeout sc_rnd_ch;

	struct i2c_controller sc_i2c_tag;
	struct rwlock sc_i2c_lock;
	struct {
		i2c_op_t op;
		void *buf;
		size_t len;
		int flags;
		volatile int error;
	} sc_i2c_xfer;
};

int	amdpm_match(struct device *, void *, void *);
void	amdpm_attach(struct device *, struct device *, void *);
int	amdpm_activate(struct device *, int);
void	amdpm_rnd_callout(void *);

int	amdpm_i2c_acquire_bus(void *, int);
void	amdpm_i2c_release_bus(void *, int);
int	amdpm_i2c_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
	    void *, size_t, int);

int	amdpm_intr(void *);

const struct cfattach amdpm_ca = {
	sizeof(struct amdpm_softc), amdpm_match, amdpm_attach,
	NULL, amdpm_activate
};

struct cfdriver amdpm_cd = {
	NULL, "amdpm", DV_DULL
};

const struct pci_matchid amdpm_ids[] = {
	{ PCI_VENDOR_AMD, PCI_PRODUCT_AMD_PBC756_PMC },
	{ PCI_VENDOR_AMD, PCI_PRODUCT_AMD_766_PMC },
	{ PCI_VENDOR_AMD, PCI_PRODUCT_AMD_PBC768_PMC },
	{ PCI_VENDOR_AMD, PCI_PRODUCT_AMD_8111_PMC },
	{ PCI_VENDOR_NVIDIA, PCI_PRODUCT_NVIDIA_NFORCE_SMB }
};

int
amdpm_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, amdpm_ids,
	    sizeof(amdpm_ids) / sizeof(amdpm_ids[0])));
}

void
amdpm_attach(struct device *parent, struct device *self, void *aux)
{
	struct amdpm_softc *sc = (struct amdpm_softc *) self;
	struct pci_attach_args *pa = aux;
	struct i2cbus_attach_args iba;
	pcireg_t cfg_reg, reg;
	int i;

	sc->sc_pc = pa->pa_pc;
	sc->sc_tag = pa->pa_tag;
	sc->sc_iot = pa->pa_iot;
	sc->sc_poll = 1; /* XXX */

	
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_AMD)  {
		cfg_reg = pci_conf_read(pa->pa_pc, pa->pa_tag, AMDPM_CONFREG);
		if ((cfg_reg & AMDPM_PMIOEN) == 0) {
			printf(": PMxx space isn't enabled\n");
			return;
		}

		reg = pci_conf_read(pa->pa_pc, pa->pa_tag, AMDPM_PMPTR);
		if (AMDPM_PMBASE(reg) == 0 ||
		    bus_space_map(sc->sc_iot, AMDPM_PMBASE(reg), AMDPM_PMSIZE,
		    0, &sc->sc_ioh)) {
			printf("\n");
			return;
		}
		if (bus_space_subregion(sc->sc_iot, sc->sc_ioh, AMDPM_SMB_REGS,
		    AMDPM_SMB_SIZE, &sc->sc_i2c_ioh)) {
			printf(": failed to map I2C subregion\n");
			return;	
		}

		if ((cfg_reg & AMDPM_TMRRST) == 0 &&
		    (cfg_reg & AMDPM_STOPTMR) == 0 &&
		    (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_AMD_PBC768_PMC ||
		    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_AMD_8111_PMC)) {
			printf(": %d-bit timer at %lluHz",
			    (cfg_reg & AMDPM_TMR32) ? 32 : 24,
			    amdpm_timecounter.tc_frequency);

			amdpm_timecounter.tc_priv = sc;
			if (cfg_reg & AMDPM_TMR32)
				amdpm_timecounter.tc_counter_mask = 0xffffffffu;
			tc_init(&amdpm_timecounter);
		}	
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_AMD_PBC768_PMC ||
		    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_AMD_8111_PMC) {
			if ((cfg_reg & AMDPM_RNGEN) ==0) {
				pci_conf_write(pa->pa_pc, pa->pa_tag, 
				    AMDPM_CONFREG, cfg_reg | AMDPM_RNGEN);
				cfg_reg = pci_conf_read(pa->pa_pc, pa->pa_tag,
				    AMDPM_CONFREG);
			}
			if (cfg_reg & AMDPM_RNGEN) {
			/* Check to see if we can read data from the RNG. */
				(void) bus_space_read_4(sc->sc_iot, sc->sc_ioh,
				    AMDPM_RNGDATA);
				for (i = 1000; i--; ) {
					if (bus_space_read_1(sc->sc_iot, 
					    sc->sc_ioh, AMDPM_RNGSTAT) & 
					    AMDPM_RNGDONE)
						break;
					DELAY(10);
				}
				if (bus_space_read_1(sc->sc_iot, sc->sc_ioh,
				    AMDPM_RNGSTAT) & AMDPM_RNGDONE) {
					printf(": rng active");
					timeout_set(&sc->sc_rnd_ch, 
					    amdpm_rnd_callout, sc);
					amdpm_rnd_callout(sc);
				}
			}
		}
	} else if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_NVIDIA) {
		reg = pci_conf_read(pa->pa_pc, pa->pa_tag, NFPM_PMPTR);
		if (AMDPM_PMBASE(reg) == 0 ||
		    bus_space_map(sc->sc_iot, AMDPM_PMBASE(reg), AMDPM_SMB_SIZE, 0,
		    &sc->sc_i2c_ioh)) {
			printf(": failed to map I2C subregion\n");
			return;
		}
	}
	printf("\n");

	/* Attach I2C bus */
	rw_init(&sc->sc_i2c_lock, "iiclk");
	sc->sc_i2c_tag.ic_cookie = sc;
	sc->sc_i2c_tag.ic_acquire_bus = amdpm_i2c_acquire_bus;
	sc->sc_i2c_tag.ic_release_bus = amdpm_i2c_release_bus;
	sc->sc_i2c_tag.ic_exec = amdpm_i2c_exec;

	bzero(&iba, sizeof(iba));
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_i2c_tag;
	config_found(self, &iba, iicbus_print);
}

int
amdpm_activate(struct device *self, int act)
{
	struct amdpm_softc *sc = (struct amdpm_softc *)self;
	int rv = 0;

	switch (act) {
	case DVACT_RESUME:
		if (timeout_initialized(&sc->sc_rnd_ch)) {
			pcireg_t cfg_reg;

			/* Restart the AMD PBC768_PMC/8111_PMC RNG */
			cfg_reg = pci_conf_read(sc->sc_pc, sc->sc_tag,
			    AMDPM_CONFREG);
			pci_conf_write(sc->sc_pc, sc->sc_tag, 
			    AMDPM_CONFREG, cfg_reg | AMDPM_RNGEN);
		
		}
		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}

void
amdpm_rnd_callout(void *v)
{
	struct amdpm_softc *sc = v;
	u_int32_t reg;

	if ((bus_space_read_4(sc->sc_iot, sc->sc_ioh, AMDPM_RNGSTAT) &
	    AMDPM_RNGDONE) != 0) {
		reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh, AMDPM_RNGDATA);
		enqueue_randomness(reg);
	}
	timeout_add(&sc->sc_rnd_ch, 1);
}

u_int
amdpm_get_timecount(struct timecounter *tc)
{
	struct amdpm_softc *sc = tc->tc_priv;
	u_int u2;
#if 0
	u_int u1, u3;
#endif

	u2 = bus_space_read_4(sc->sc_iot, sc->sc_ioh, AMDPM_TMR);
#if 0
	u3 = bus_space_read_4(sc->sc_iot, sc->sc_ioh, AMDPM_TMR);
	do {
		u1 = u2;
		u2 = u3;
		u3 = bus_space_read_4(sc->sc_iot, sc->sc_ioh, AMDPM_TMR);
	} while (u1 > u2 || u2 > u3);
#endif
	return (u2);
}

int
amdpm_i2c_acquire_bus(void *cookie, int flags)
{
	struct amdpm_softc *sc = cookie;

	if (cold || sc->sc_poll || (flags & I2C_F_POLL))
		return (0);

	return (rw_enter(&sc->sc_i2c_lock, RW_WRITE | RW_INTR));
}

void
amdpm_i2c_release_bus(void *cookie, int flags)
{
	struct amdpm_softc *sc = cookie;

	if (cold || sc->sc_poll || (flags & I2C_F_POLL))
		return;

	rw_exit(&sc->sc_i2c_lock);
}

int
amdpm_i2c_exec(void *cookie, i2c_op_t op, i2c_addr_t addr,
    const void *cmdbuf, size_t cmdlen, void *buf, size_t len, int flags)
{
	struct amdpm_softc *sc = cookie;
	u_int8_t *b;
	u_int16_t st, ctl, data;
	int retries;

	DPRINTF("%s: exec: op %d, addr 0x%02x, cmdlen %d, len %d, "
	    "flags 0x%02x\n", sc->sc_dev.dv_xname, op, addr, cmdlen,
	    len, flags);

	/* Wait for bus to be idle */
	for (retries = 100; retries > 0; retries--) {
		st = bus_space_read_2(sc->sc_iot, sc->sc_i2c_ioh, AMDPM_SMBSTAT);
		if (!(st & AMDPM_SMBSTAT_BSY))
			break;
		DELAY(AMDPM_SMBUS_DELAY);
	}
	DPRINTF("%s: exec: st 0x%b\n", sc->sc_dev.dv_xname, st,
	    AMDPM_SMBSTAT_BITS);
	if (st & AMDPM_SMBSTAT_BSY)
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
	bus_space_write_2(sc->sc_iot, sc->sc_i2c_ioh, AMDPM_SMBADDR,
	    AMDPM_SMBADDR_ADDR(addr) |
	    (I2C_OP_READ_P(op) ? AMDPM_SMBADDR_READ : 0));

	b = (void *)cmdbuf;
	if (cmdlen > 0)
		/* Set command byte */
		bus_space_write_1(sc->sc_iot, sc->sc_i2c_ioh, AMDPM_SMBCMD, b[0]);

	if (I2C_OP_WRITE_P(op)) {
		/* Write data */
		data = 0;
		b = buf;
		if (len > 0)
			data = b[0];
		if (len > 1)
			data |= ((u_int16_t)b[1] << 8);
		if (len > 0)
			bus_space_write_2(sc->sc_iot, sc->sc_i2c_ioh,
			    AMDPM_SMBDATA, data);
	}

	/* Set SMBus command */
	if (len == 0)
		ctl = AMDPM_SMBCTL_CMD_BYTE;
	else if (len == 1)
		ctl = AMDPM_SMBCTL_CMD_BDATA;
	else if (len == 2)
		ctl = AMDPM_SMBCTL_CMD_WDATA;
	else
		panic("%s: unexpected len %zd", __func__, len);

	if ((flags & I2C_F_POLL) == 0)
		ctl |= AMDPM_SMBCTL_CYCEN;

	/* Start transaction */
	ctl |= AMDPM_SMBCTL_START;
	bus_space_write_2(sc->sc_iot, sc->sc_i2c_ioh, AMDPM_SMBCTL, ctl);

	if (flags & I2C_F_POLL) {
		/* Poll for completion */
		DELAY(AMDPM_SMBUS_DELAY);
		for (retries = 1000; retries > 0; retries--) {
			st = bus_space_read_2(sc->sc_iot, sc->sc_i2c_ioh,
			    AMDPM_SMBSTAT);
			if ((st & AMDPM_SMBSTAT_HBSY) == 0)
				break;
			DELAY(AMDPM_SMBUS_DELAY);
		}
		if (st & AMDPM_SMBSTAT_HBSY)
			goto timeout;
		amdpm_intr(sc);
	} else {
		/* Wait for interrupt */
		if (tsleep_nsec(sc, PRIBIO, "amdpm",
		    SEC_TO_NSEC(AMDPM_SMBUS_TIMEOUT)))
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
	    st, AMDPM_SMBSTAT_BITS);
	bus_space_write_2(sc->sc_iot, sc->sc_i2c_ioh, AMDPM_SMBCTL,
	    AMDPM_SMBCTL_ABORT);
	DELAY(AMDPM_SMBUS_DELAY);
	st = bus_space_read_2(sc->sc_iot, sc->sc_i2c_ioh, AMDPM_SMBSTAT);
	if ((st & AMDPM_SMBSTAT_ABRT) == 0)
		printf("%s: abort failed, status 0x%b\n",
		    sc->sc_dev.dv_xname, st, AMDPM_SMBSTAT_BITS);
	bus_space_write_2(sc->sc_iot, sc->sc_i2c_ioh, AMDPM_SMBSTAT, st);
	return (1);
}

int
amdpm_intr(void *arg)
{
	struct amdpm_softc *sc = arg;
	u_int16_t st, data;
	u_int8_t *b;
	size_t len;

	/* Read status */
	st = bus_space_read_2(sc->sc_iot, sc->sc_i2c_ioh, AMDPM_SMBSTAT);
	if ((st & AMDPM_SMBSTAT_HBSY) != 0 || (st & (AMDPM_SMBSTAT_ABRT |
	    AMDPM_SMBSTAT_COL | AMDPM_SMBSTAT_PRERR | AMDPM_SMBSTAT_CYC |
	    AMDPM_SMBSTAT_TO | AMDPM_SMBSTAT_SNP | AMDPM_SMBSTAT_SLV |
	    AMDPM_SMBSTAT_SMBA)) == 0)
		/* Interrupt was not for us */
		return (0);

	DPRINTF("%s: intr: st 0x%b\n", sc->sc_dev.dv_xname, st,
	    AMDPM_SMBSTAT_BITS);

	/* Clear status bits */
	bus_space_write_2(sc->sc_iot, sc->sc_i2c_ioh, AMDPM_SMBSTAT, st);

	/* Check for errors */
	if (st & (AMDPM_SMBSTAT_COL | AMDPM_SMBSTAT_PRERR |
	    AMDPM_SMBSTAT_TO)) {
		sc->sc_i2c_xfer.error = 1;
		goto done;
	}

	if (st & AMDPM_SMBSTAT_CYC) {
		if (I2C_OP_WRITE_P(sc->sc_i2c_xfer.op))
			goto done;

		/* Read data */
		b = sc->sc_i2c_xfer.buf;
		len = sc->sc_i2c_xfer.len;
		if (len > 0) {
			data = bus_space_read_2(sc->sc_iot, sc->sc_i2c_ioh,
			    AMDPM_SMBDATA);
			b[0] = data & 0xff;
		}
		if (len > 1)
			b[1] = (data >> 8) & 0xff;
	}

done:
	if ((sc->sc_i2c_xfer.flags & I2C_F_POLL) == 0)
		wakeup(sc);
	return (1);
}
