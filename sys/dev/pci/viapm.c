/*	$OpenBSD: viapm.c,v 1.23 2024/05/24 06:02:58 jsg Exp $	*/

/*
 * Copyright (c) 2005 Mark Kettenis <kettenis@openbsd.org>
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

/*	$NetBSD: viaenv.c,v 1.9 2002/10/02 16:51:59 thorpej Exp $	*/

/*
 * Copyright (c) 2000 Johan Danielsson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of author nor the names of any contributors may
 *    be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS
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

/*
 * Driver for the SMBus controller and power management timer
 * in the VIA VT82C596[B], VT82C686A, VT8231, VT8233[A], VT8235, VT8237[A,S],
 * VT8251, CX700, VX800, VX855 and VX900 South Bridges.
 * Also for the hardware monitoring part of the VIA VT82C686A and VT8231.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/rwlock.h>
#include <sys/sensors.h>
#include <sys/timeout.h>
#include <sys/timetc.h>

#include <machine/bus.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/i2c/i2cvar.h>

/*
 * Register definitions.
 */

/* PCI configuration registers */
#define VIAPM_PM_CFG1		0x40	/* general configuration */
#define VIAPM_PM_CFG2		0x80
#define VIAPM_PM_CFG_TMR32	(1 << 11)	/* 32-bit PM timer */
#define VIAPM_PM_CFG_PMEN	(1 << 15)	/* enable PM I/O space */
#define VIAPM_PM_BASE1		0x48	/* power management I/O base address */
#define VIAPM_PM_BASE2		0x88
#define VIAPM_PM_BASE_MASK	0xff80

#define VIAPM_HWMON_BASE	0x70	/* HWMon I/O base address */
#define VIAPM_HWMON_BASE_MASK	0xff80
#define VIAPM_HWMON_CFG		0x74	/* HWMon control register */
#define VIAPM_HWMON_CFG_HWEN	(1 << 0)	/* enable HWMon I/O space */

#define VIAPM_SMB_BASE1		0x90	/* SMBus I/O base address */
#define VIAPM_SMB_BASE2		0x80
#define VIAPM_SMB_BASE3		0xd0
#define VIAPM_SMB_BASE_MASK	0xfff0
#define VIAPM_SMB_CFG1		0xd2	/* host configuration */
#define VIAPM_SMB_CFG2		0x84
#define VIAPM_SMB_CFG_HSTEN	(1 << 0)	/* enable SMBus I/O space */
#define VIAPM_SMB_CFG_INTEN	(1 << 1)	/* enable SCI/SMI */
#define VIAPM_SMB_CFG_SCIEN	(1 << 3)	/* interrupt type (SCI/SMI) */

#define VIAPM_PM_SIZE		256	/* Power management I/O space size */
#define VIAPM_HWMON_SIZE	128	/* HWMon I/O space size */
#define VIAPM_SMB_SIZE		16	/* SMBus I/O space size */

/* HWMon I/O registers */
#define VIAPM_HWMON_TSENS3	0x1f
#define VIAPM_HWMON_TSENS1	0x20
#define VIAPM_HWMON_TSENS2	0x21
#define VIAPM_HWMON_VSENS1	0x22
#define VIAPM_HWMON_VSENS2	0x23
#define VIAPM_HWMON_VCORE	0x24
#define VIAPM_HWMON_VSENS3	0x25
#define VIAPM_HWMON_VSENS4	0x26
#define VIAPM_HWMON_FAN1	0x29
#define VIAPM_HWMON_FAN2	0x2a
#define VIAPM_HWMON_FANCONF	0x47	/* fan configuration */
#define VIAPM_HWMON_TLOW	0x49	/* temperature low order value */
#define VIAPM_HWMON_TIRQ	0x4b	/* temperature interrupt configuration */

/* ACPI I/O registers */
#define VIAPM_PM_TMR		0x08	/* PM timer */

/* SMBus I/O registers */
#define VIAPM_SMB_HS		0x00	/* host status */
#define VIAPM_SMB_HS_BUSY	(1 << 0)	/* running a command */
#define VIAPM_SMB_HS_INTR	(1 << 1)	/* command completed */
#define VIAPM_SMB_HS_DEVERR	(1 << 2)	/* command error */
#define VIAPM_SMB_HS_BUSERR	(1 << 3)	/* transaction collision */
#define VIAPM_SMB_HS_FAILED	(1 << 4)	/* failed bus transaction */
#define VIAPM_SMB_HS_INUSE	(1 << 6)	/* bus semaphore */
#define VIAPM_SMB_HS_BITS	\
  "\020\001BUSY\002INTR\003DEVERR\004BUSERR\005FAILED\007INUSE"
#define VIAPM_SMB_HC		0x02	/* host control */
#define VIAPM_SMB_HC_INTREN	(1 << 0)	/* enable interrupts */
#define VIAPM_SMB_HC_KILL	(1 << 1)	/* kill current transaction */
#define VIAPM_SMB_HC_CMD_QUICK	(0 << 2)	/* QUICK command */
#define VIAPM_SMB_HC_CMD_BYTE	(1 << 2)	/* BYTE command */
#define VIAPM_SMB_HC_CMD_BDATA	(2 << 2)	/* BYTE DATA command */
#define VIAPM_SMB_HC_CMD_WDATA	(3 << 2)	/* WORD DATA command */
#define VIAPM_SMB_HC_CMD_PCALL	(4 << 2)	/* PROCESS CALL command */
#define VIAPM_SMB_HC_CMD_BLOCK	(5 << 2)	/* BLOCK command */
#define VIAPM_SMB_HC_START	(1 << 6)	/* start transaction */
#define VIAPM_SMB_HCMD		0x03	/* host command */
#define VIAPM_SMB_TXSLVA	0x04	/* transmit slave address */
#define VIAPM_SMB_TXSLVA_READ	(1 << 0)	/* read direction */
#define VIAPM_SMB_TXSLVA_ADDR(x) (((x) & 0x7f) << 1) /* 7-bit address */
#define VIAPM_SMB_HD0		0x05	/* host data 0 */
#define VIAPM_SMB_HD1		0x06	/* host data 1 */
#define VIAPM_SMB_HBDB		0x07	/* host block data byte */

#ifdef VIAPM_DEBUG
#define DPRINTF(x...) printf(x)
#else
#define DPRINTF(x...)
#endif

#define DEVNAME(sc) ((sc)->sc_dev.dv_xname)

#define VIAPM_SMBUS_DELAY	100
#define VIAPM_SMBUS_TIMEOUT	1

#define VIAPM_NUM_SENSORS	10	/* three temp, two fan, five voltage */

u_int	viapm_get_timecount(struct timecounter *tc);

#ifndef VIAPM_FREQUENCY
#define VIAPM_FREQUENCY 3579545
#endif

static struct timecounter viapm_timecounter = {
	.tc_get_timecount = viapm_get_timecount,
	.tc_counter_mask = 0xffffff,
	.tc_frequency = VIAPM_FREQUENCY,
	.tc_name = "VIAPM",
	.tc_quality = 1000,
	.tc_priv = NULL,
	.tc_user = 0,
};

struct timeout viapm_timeout;

struct viapm_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_pm_ioh;
	bus_space_handle_t	sc_smbus_ioh;
	bus_space_handle_t	sc_hwmon_ioh;
	void *			sc_ih;
	int			sc_poll;

	int			sc_fan_div[2];	/* fan RPM divisor */

	struct ksensor		sc_data[VIAPM_NUM_SENSORS];
	struct ksensordev	sc_sensordev;

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

int	viapm_match(struct device *, void *, void *);
void	viapm_attach(struct device *, struct device *, void *);

int	viapm_i2c_acquire_bus(void *, int);
void	viapm_i2c_release_bus(void *, int);
int	viapm_i2c_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
	    void *, size_t, int);

int	viapm_intr(void *);

int	val_to_uK(unsigned int);
int	val_to_rpm(unsigned int, int);
long	val_to_uV(unsigned int, int);
void	viapm_refresh_sensor_data(struct viapm_softc *);
void	viapm_refresh(void *);

const struct cfattach viapm_ca = {
	sizeof(struct viapm_softc), viapm_match, viapm_attach
};

struct cfdriver viapm_cd = {
	NULL, "viapm", DV_DULL
};

const struct pci_matchid viapm_ids[] = {
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT82C596 },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT82C596B_PM },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT82C686A_SMB },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT8231_PWR },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT8233_ISA },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT8233A_ISA },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT8235_ISA },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT8237_ISA },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT8237A_ISA },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT8237S_ISA },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VT8251_ISA },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_CX700_ISA },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VX800_ISA },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VX855_ISA },
	{ PCI_VENDOR_VIATECH, PCI_PRODUCT_VIATECH_VX900_ISA }
};

/*
 * XXX there doesn't seem to exist much hard documentation on how to
 * convert the raw values to usable units, this code is more or less
 * stolen from the Linux driver, but changed to suit our conditions
 */

/*
 * lookup-table to translate raw values to uK, this is the same table
 * used by the Linux driver (modulo units); there is a fifth degree
 * polynomial that supposedly been used to generate this table, but I
 * haven't been able to figure out how -- it doesn't give the same values
 */

static const long val_to_temp[] = {
	20225, 20435, 20645, 20855, 21045, 21245, 21425, 21615, 21785, 21955,
	22125, 22285, 22445, 22605, 22755, 22895, 23035, 23175, 23315, 23445,
	23565, 23695, 23815, 23925, 24045, 24155, 24265, 24365, 24465, 24565,
	24665, 24765, 24855, 24945, 25025, 25115, 25195, 25275, 25355, 25435,
	25515, 25585, 25655, 25725, 25795, 25865, 25925, 25995, 26055, 26115,
	26175, 26235, 26295, 26355, 26405, 26465, 26515, 26575, 26625, 26675,
	26725, 26775, 26825, 26875, 26925, 26975, 27025, 27065, 27115, 27165,
	27205, 27255, 27295, 27345, 27385, 27435, 27475, 27515, 27565, 27605,
	27645, 27685, 27735, 27775, 27815, 27855, 27905, 27945, 27985, 28025,
	28065, 28105, 28155, 28195, 28235, 28275, 28315, 28355, 28405, 28445,
	28485, 28525, 28565, 28615, 28655, 28695, 28735, 28775, 28825, 28865,
	28905, 28945, 28995, 29035, 29075, 29125, 29165, 29205, 29245, 29295,
	29335, 29375, 29425, 29465, 29505, 29555, 29595, 29635, 29685, 29725,
	29765, 29815, 29855, 29905, 29945, 29985, 30035, 30075, 30125, 30165,
	30215, 30255, 30305, 30345, 30385, 30435, 30475, 30525, 30565, 30615,
	30655, 30705, 30755, 30795, 30845, 30885, 30935, 30975, 31025, 31075,
	31115, 31165, 31215, 31265, 31305, 31355, 31405, 31455, 31505, 31545,
	31595, 31645, 31695, 31745, 31805, 31855, 31905, 31955, 32005, 32065,
	32115, 32175, 32225, 32285, 32335, 32395, 32455, 32515, 32575, 32635,
	32695, 32755, 32825, 32885, 32955, 33025, 33095, 33155, 33235, 33305,
	33375, 33455, 33525, 33605, 33685, 33765, 33855, 33935, 34025, 34115,
	34205, 34295, 34395, 34495, 34595, 34695, 34805, 34905, 35015, 35135,
	35245, 35365, 35495, 35615, 35745, 35875, 36015, 36145, 36295, 36435,
	36585, 36745, 36895, 37065, 37225, 37395, 37575, 37755, 37935, 38125,
	38325, 38525, 38725, 38935, 39155, 39375, 39605, 39835, 40075, 40325,
	40575, 40835, 41095, 41375, 41655, 41935,
};

int
viapm_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, viapm_ids, nitems(viapm_ids)));
}

void
viapm_attach(struct device *parent, struct device *self, void *aux)
{
	struct viapm_softc *sc = (struct viapm_softc *)self;
	struct pci_attach_args *pa = aux;
	struct i2cbus_attach_args iba;
	pcireg_t conf, iobase;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	int basereg, cfgreg;
	int i, v;

	sc->sc_iot = pa->pa_iot;

	/* SMBus */
	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_VIATECH_VT82C596:
	case PCI_PRODUCT_VIATECH_VT82C596B_PM:
	case PCI_PRODUCT_VIATECH_VT82C686A_SMB:
	case PCI_PRODUCT_VIATECH_VT8231_PWR:
		basereg = VIAPM_SMB_BASE1;
		break;
	default:
		basereg = VIAPM_SMB_BASE3;
	}

	cfgreg = (VIAPM_SMB_CFG1 & (~0x03));	/* XXX 4-byte aligned */

	/* Check 2nd address for VT82C596 */
	iobase = pci_conf_read(pa->pa_pc, pa->pa_tag, basereg);
	if ((PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_VIATECH_VT82C596) &&
	    ((iobase & 0x0001) == 0)) {
		iobase = pci_conf_read(pa->pa_pc, pa->pa_tag, VIAPM_SMB_BASE2);
		cfgreg = VIAPM_SMB_CFG2;
	}

	/* Check if SMBus I/O space is enabled */
	conf = pci_conf_read(pa->pa_pc, pa->pa_tag, cfgreg);
	if (cfgreg != VIAPM_SMB_CFG2)
		conf >>= 16;
	DPRINTF(": conf 0x%02x", conf & 0xff);

	if ((conf & VIAPM_SMB_CFG_HSTEN) == 0) {
		printf(": SMBus disabled\n");
		goto nosmb;
	}

	/* Map SMBus I/O space */
	iobase &= VIAPM_SMB_BASE_MASK;
	if (iobase == 0 || bus_space_map(sc->sc_iot, iobase,
	    VIAPM_SMB_SIZE, 0, &sc->sc_smbus_ioh)) {
		printf(": can't map SMBus i/o space\n");
		goto nosmb;
	}

	sc->sc_poll = 1;
	if ((conf & VIAPM_SMB_CFG_SCIEN) == 0) {
		/* No PCI IRQ */
		printf(": SMI");
	} else {
		/* Install interrupt handler */
		if (pci_intr_map(pa, &ih) == 0) {
			intrstr = pci_intr_string(pa->pa_pc, ih);
			sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_BIO,
			    viapm_intr, sc, DEVNAME(sc));
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
	sc->sc_i2c_tag.ic_acquire_bus = viapm_i2c_acquire_bus;
	sc->sc_i2c_tag.ic_release_bus = viapm_i2c_release_bus;
	sc->sc_i2c_tag.ic_exec = viapm_i2c_exec;

	bzero(&iba, sizeof iba);
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_i2c_tag;
	config_found(self, &iba, iicbus_print);

nosmb:

	/* Power management */
	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_VIATECH_VT82C596:
	case PCI_PRODUCT_VIATECH_VT82C596B_PM:
	case PCI_PRODUCT_VIATECH_VT82C686A_SMB:
	case PCI_PRODUCT_VIATECH_VT8231_PWR:
		basereg = VIAPM_PM_BASE1;
		cfgreg = VIAPM_PM_CFG1;
		break;
	default:
		basereg = VIAPM_PM_BASE2;
		cfgreg = VIAPM_PM_CFG2;
	}

	/* Check if power management I/O space is enabled */
	conf = pci_conf_read(pa->pa_pc, pa->pa_tag, cfgreg);
	if ((conf & VIAPM_PM_CFG_PMEN) == 0) {
		printf("%s: PM disabled\n", DEVNAME(sc));
		goto nopm;
	}

	/* Map power management I/O space */
	iobase = pci_conf_read(pa->pa_pc, pa->pa_tag, basereg);
	iobase &= VIAPM_PM_BASE_MASK;
	if (iobase == 0 || bus_space_map(sc->sc_iot, iobase,
	    VIAPM_PM_SIZE, 0, &sc->sc_pm_ioh)) {
		/* XXX can't map PM i/o space if ACPI mode */
		DPRINTF("%s: can't map PM i/o space\n", DEVNAME(sc));
		goto nopm;
	}

	/* Check for 32-bit PM timer */
	if (conf & VIAPM_PM_CFG_TMR32)
		viapm_timecounter.tc_counter_mask = 0xffffffff;

	/* Register new timecounter */
	viapm_timecounter.tc_priv = sc;
	tc_init(&viapm_timecounter);

	printf("%s: %s-bit timer at %lluHz\n", DEVNAME(sc),
	    (viapm_timecounter.tc_counter_mask == 0xffffffff ? "32" : "24"),
	    (unsigned long long)viapm_timecounter.tc_frequency);

nopm:

	/* HWMon */
	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_VIATECH_VT82C686A_SMB:
	case PCI_PRODUCT_VIATECH_VT8231_PWR:
		break;
	default:
		return;
	}

	/* Check if HWMon I/O space is enabled */
	conf = pci_conf_read(pa->pa_pc, pa->pa_tag, VIAPM_HWMON_CFG);
	if ((conf & VIAPM_HWMON_CFG_HWEN) == 0) {
		printf("%s: HWM disabled\n", DEVNAME(sc));
		return;
	}

	/* Map HWMon I/O space */
	iobase = pci_conf_read(pa->pa_pc, pa->pa_tag, VIAPM_HWMON_BASE);
	iobase &= VIAPM_HWMON_BASE_MASK;
	if (iobase == 0 || bus_space_map(sc->sc_iot, iobase,
	    VIAPM_HWMON_SIZE, 0, &sc->sc_hwmon_ioh)) {
		printf("%s: can't map HWM i/o space\n", DEVNAME(sc));
		return;
	}

	v = bus_space_read_1(sc->sc_iot, sc->sc_hwmon_ioh, VIAPM_HWMON_FANCONF);

	sc->sc_fan_div[0] = 1 << ((v >> 4) & 0x3);
	sc->sc_fan_div[1] = 1 << ((v >> 6) & 0x3);

	for (i = 0; i <= 2; i++)
		sc->sc_data[i].type = SENSOR_TEMP;
	for (i = 3; i <= 4; i++)
		sc->sc_data[i].type = SENSOR_FANRPM;
	for (i = 5; i <= 9; ++i)
		sc->sc_data[i].type = SENSOR_VOLTS_DC;

	strlcpy(sc->sc_data[5].desc, "VSENS1",
	    sizeof(sc->sc_data[5].desc));	/* CPU core (2V) */
	strlcpy(sc->sc_data[6].desc, "VSENS2",
	    sizeof(sc->sc_data[6].desc));	/* NB core? (2.5V) */
	strlcpy(sc->sc_data[7].desc, "Vcore",
	    sizeof(sc->sc_data[7].desc));	/* Vcore (3.3V) */
	strlcpy(sc->sc_data[8].desc, "VSENS3",
	    sizeof(sc->sc_data[8].desc));	/* VSENS3 (5V) */
	strlcpy(sc->sc_data[9].desc, "VSENS4",
	    sizeof(sc->sc_data[9].desc));	/* VSENS4 (12V) */

	/* Get initial set of sensor values. */
	viapm_refresh_sensor_data(sc);

	/* Register sensors with sysctl */
	strlcpy(sc->sc_sensordev.xname, DEVNAME(sc),
	    sizeof(sc->sc_sensordev.xname));
	for (i = 0; i < VIAPM_NUM_SENSORS; ++i)
		sensor_attach(&sc->sc_sensordev, &sc->sc_data[i]);
	sensordev_install(&sc->sc_sensordev);

	/* Refresh sensors data every 1.5 seconds */
	timeout_set(&viapm_timeout, viapm_refresh, sc);
	timeout_add_msec(&viapm_timeout, 1500);
}

int
viapm_i2c_acquire_bus(void *cookie, int flags)
{
	struct viapm_softc *sc = cookie;

	if (cold || sc->sc_poll || (flags & I2C_F_POLL))
		return (0);

	return (rw_enter(&sc->sc_i2c_lock, RW_WRITE | RW_INTR));
}

void
viapm_i2c_release_bus(void *cookie, int flags)
{
	struct viapm_softc *sc = cookie;

	if (cold || sc->sc_poll || (flags & I2C_F_POLL))
		return;

	rw_exit(&sc->sc_i2c_lock);
}

int
viapm_i2c_exec(void *cookie, i2c_op_t op, i2c_addr_t addr,
    const void *cmdbuf, size_t cmdlen, void *buf, size_t len, int flags)
{
	struct viapm_softc *sc = cookie;
	u_int8_t *b;
	u_int8_t ctl, st;
	int retries;

	/* Check if there's a transfer already running */
	st = bus_space_read_1(sc->sc_iot, sc->sc_smbus_ioh, VIAPM_SMB_HS);
	DPRINTF("%s: exec op %d, addr 0x%x, cmdlen %d, len %d, "
	    "flags 0x%x, status 0x%b\n", DEVNAME(sc), op, addr,
	    cmdlen, len, flags, st, VIAPM_SMB_HS_BITS);
	if (st & VIAPM_SMB_HS_BUSY)
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
	bus_space_write_1(sc->sc_iot, sc->sc_smbus_ioh, VIAPM_SMB_TXSLVA,
	    VIAPM_SMB_TXSLVA_ADDR(addr) |
	    (I2C_OP_READ_P(op) ? VIAPM_SMB_TXSLVA_READ : 0));

	b = (void *)cmdbuf;
	if (cmdlen > 0)
		/* Set command byte */
		bus_space_write_1(sc->sc_iot, sc->sc_smbus_ioh,
		    VIAPM_SMB_HCMD, b[0]);

	if (I2C_OP_WRITE_P(op)) {
		/* Write data */
		b = buf;
		if (len > 0)
			bus_space_write_1(sc->sc_iot, sc->sc_smbus_ioh,
			    VIAPM_SMB_HD0, b[0]);
		if (len > 1)
			bus_space_write_1(sc->sc_iot, sc->sc_smbus_ioh,
			    VIAPM_SMB_HD1, b[1]);
	}

	/* Set SMBus command */
	if (len == 0)
		ctl = VIAPM_SMB_HC_CMD_BYTE;
	else if (len == 1)
		ctl = VIAPM_SMB_HC_CMD_BDATA;
	else if (len == 2)
		ctl = VIAPM_SMB_HC_CMD_WDATA;
	else
		panic("%s: unexpected len %zd", __func__, len);

	if ((flags & I2C_F_POLL) == 0)
		ctl |= VIAPM_SMB_HC_INTREN;

	/* Start transaction */
	ctl |= VIAPM_SMB_HC_START;
	bus_space_write_1(sc->sc_iot, sc->sc_smbus_ioh, VIAPM_SMB_HC, ctl);

	if (flags & I2C_F_POLL) {
		/* Poll for completion */
		DELAY(VIAPM_SMBUS_DELAY);
		for (retries = 1000; retries > 0; retries--) {
			st = bus_space_read_1(sc->sc_iot, sc->sc_smbus_ioh,
			    VIAPM_SMB_HS);
			if ((st & VIAPM_SMB_HS_BUSY) == 0)
				break;
			DELAY(VIAPM_SMBUS_DELAY);
		}
		if (st & VIAPM_SMB_HS_BUSY)
			goto timeout;
		viapm_intr(sc);
	} else {
		/* Wait for interrupt */
		if (tsleep_nsec(sc, PRIBIO, "iicexec",
		    SEC_TO_NSEC(VIAPM_SMBUS_TIMEOUT)))
			goto timeout;
	}

	if (sc->sc_i2c_xfer.error)
		return (1);

	return (0);

timeout:
	/*
	 * Transfer timeout. Kill the transaction and clear status bits.
	 */
	printf("%s: timeout, status 0x%b\n", DEVNAME(sc), st,
	    VIAPM_SMB_HS_BITS);
	bus_space_write_1(sc->sc_iot, sc->sc_smbus_ioh, VIAPM_SMB_HC,
	    VIAPM_SMB_HC_KILL);
	DELAY(VIAPM_SMBUS_DELAY);
	st = bus_space_read_1(sc->sc_iot, sc->sc_smbus_ioh, VIAPM_SMB_HS);
	if ((st & VIAPM_SMB_HS_FAILED) == 0)
		printf("%s: transaction abort failed, status 0x%b\n",
		    DEVNAME(sc), st, VIAPM_SMB_HS_BITS);
	bus_space_write_1(sc->sc_iot, sc->sc_smbus_ioh, VIAPM_SMB_HS, st);
	return (1);
}

int
viapm_intr(void *arg)
{
	struct viapm_softc *sc = arg;
	u_int8_t st;
	u_int8_t *b;
	size_t len;

	/* Read status */
	st = bus_space_read_1(sc->sc_iot, sc->sc_smbus_ioh, VIAPM_SMB_HS);
	if ((st & VIAPM_SMB_HS_BUSY) != 0 || (st & (VIAPM_SMB_HS_INTR |
	    VIAPM_SMB_HS_DEVERR | VIAPM_SMB_HS_BUSERR |
	    VIAPM_SMB_HS_FAILED)) == 0)
		/* Interrupt was not for us */
		return (0);

	DPRINTF("%s: intr st 0x%b\n", DEVNAME(sc), st, VIAPM_SMB_HS_BITS);

	/* Clear status bits */
	bus_space_write_1(sc->sc_iot, sc->sc_smbus_ioh, VIAPM_SMB_HS, st);

	/* Check for errors */
	if (st & (VIAPM_SMB_HS_DEVERR | VIAPM_SMB_HS_BUSERR |
	    VIAPM_SMB_HS_FAILED)) {
		sc->sc_i2c_xfer.error = 1;
		goto done;
	}

	if (st & VIAPM_SMB_HS_INTR) {
		if (I2C_OP_WRITE_P(sc->sc_i2c_xfer.op))
			goto done;

		/* Read data */
		b = sc->sc_i2c_xfer.buf;
		len = sc->sc_i2c_xfer.len;
		if (len > 0)
			b[0] = bus_space_read_1(sc->sc_iot, sc->sc_smbus_ioh,
			    VIAPM_SMB_HD0);
		if (len > 1)
			b[1] = bus_space_read_1(sc->sc_iot, sc->sc_smbus_ioh,
			    VIAPM_SMB_HD1);
	}

done:
	if ((sc->sc_i2c_xfer.flags & I2C_F_POLL) == 0)
		wakeup(sc);
	return (1);
}

int
val_to_uK(unsigned int val)
{
	int i = val / 4;
	int j = val % 4;

	KASSERT(i >= 0 && i <= 255);

	if (j == 0 || i == 255)
		return val_to_temp[i] * 10000;

	/* is linear interpolation ok? */
	return (val_to_temp[i] * (4 - j) +
	    val_to_temp[i + 1] * j) * 2500 /* really: / 4 * 10000 */ ;
}

int
val_to_rpm(unsigned int val, int div)
{
	if (val == 0)
		return 0;

	return 1350000 / val / div;
}

long
val_to_uV(unsigned int val, int index)
{
	static const long mult[] =
	    {1250000, 1250000, 1670000, 2600000, 6300000};

	KASSERT(index >= 0 && index <= 4);

	return (25LL * val + 133) * mult[index] / 2628;
}

void
viapm_refresh_sensor_data(struct viapm_softc *sc)
{
	int i;
	u_int8_t v, v2;

	/* temperature */
	v = bus_space_read_1(sc->sc_iot, sc->sc_hwmon_ioh, VIAPM_HWMON_TIRQ);
	v2 = bus_space_read_1(sc->sc_iot, sc->sc_hwmon_ioh, VIAPM_HWMON_TSENS1);
	DPRINTF("%s: TSENS1 = %d\n", DEVNAME(sc), (v2 << 2) | (v >> 6));
	sc->sc_data[0].value = val_to_uK((v2 << 2) | (v >> 6));

	v = bus_space_read_1(sc->sc_iot, sc->sc_hwmon_ioh, VIAPM_HWMON_TLOW);
	v2 = bus_space_read_1(sc->sc_iot, sc->sc_hwmon_ioh, VIAPM_HWMON_TSENS2);
	DPRINTF("%s: TSENS2 = %d\n", DEVNAME(sc), (v2 << 2) | ((v >> 4) & 0x3));
	sc->sc_data[1].value = val_to_uK((v2 << 2) | ((v >> 4) & 0x3));

	v2 = bus_space_read_1(sc->sc_iot, sc->sc_hwmon_ioh, VIAPM_HWMON_TSENS3);
	DPRINTF("%s: TSENS3 = %d\n", DEVNAME(sc), (v2 << 2) | (v >> 6));
	sc->sc_data[2].value = val_to_uK((v2 << 2) | (v >> 6));

	/* fan */
	for (i = 3; i <= 4; i++) {
		v = bus_space_read_1(sc->sc_iot, sc->sc_hwmon_ioh,
		    VIAPM_HWMON_FAN1 + i - 3);
		DPRINTF("%s: FAN%d = %d / %d\n", DEVNAME(sc), i - 3, v,
		    sc->sc_fan_div[i - 3]);
		sc->sc_data[i].value = val_to_rpm(v, sc->sc_fan_div[i - 3]);
	}

	/* voltage */
	for (i = 5; i <= 9; i++) {
		v = bus_space_read_1(sc->sc_iot, sc->sc_hwmon_ioh,
		    VIAPM_HWMON_VSENS1 + i - 5);
		DPRINTF("%s: V%d = %d\n", DEVNAME(sc), i - 5, v);
		sc->sc_data[i].value = val_to_uV(v, i - 5);
	}
}

void
viapm_refresh(void *arg)
{
	struct viapm_softc *sc = (struct viapm_softc *)arg;

	viapm_refresh_sensor_data(sc);
	timeout_add_msec(&viapm_timeout, 1500);
}

u_int
viapm_get_timecount(struct timecounter *tc)
{
	struct viapm_softc *sc = tc->tc_priv;
	u_int u1, u2, u3;

	u2 = bus_space_read_4(sc->sc_iot, sc->sc_pm_ioh, VIAPM_PM_TMR);
	u3 = bus_space_read_4(sc->sc_iot, sc->sc_pm_ioh, VIAPM_PM_TMR);
	do {
		u1 = u2;
		u2 = u3;
		u3 = bus_space_read_4(sc->sc_iot, sc->sc_pm_ioh, VIAPM_PM_TMR);
	} while (u1 > u2 || u2 > u3);

	return (u2);
}
