/* $OpenBSD: tpm.c,v 1.20 2024/05/29 12:21:33 kettenis Exp $ */

/*
 * Minimal interface to Trusted Platform Module chips implementing the
 * TPM Interface Spec 1.2, just enough to tell the TPM to save state before
 * a system suspend.
 *
 * Copyright (c) 2008, 2009 Michael Shalayeff
 * Copyright (c) 2009, 2010 Hans-Joerg Hoexer
 * Copyright (c) 2016 joshua stein <jcs@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/apmvar.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

/* #define TPM_DEBUG */

#ifdef TPM_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

#define TPM_BUFSIZ			1024
#define TPM_HDRSIZE			10
#define TPM_PARAM_SIZE			0x0001

#define TPM_ACCESS			0x0000	/* access register */
#define TPM_ACCESS_ESTABLISHMENT	0x01	/* establishment */
#define TPM_ACCESS_REQUEST_USE		0x02	/* request using locality */
#define TPM_ACCESS_REQUEST_PENDING	0x04	/* pending request */
#define TPM_ACCESS_SEIZE		0x08	/* request locality seize */
#define TPM_ACCESS_SEIZED		0x10	/* locality has been seized */
#define TPM_ACCESS_ACTIVE_LOCALITY	0x20	/* locality is active */
#define TPM_ACCESS_VALID		0x80	/* bits are valid */
#define TPM_ACCESS_BITS	\
    "\020\01EST\02REQ\03PEND\04SEIZE\05SEIZED\06ACT\010VALID"

#define TPM_INTERRUPT_ENABLE		0x0008
#define TPM_GLOBAL_INT_ENABLE		0x80000000 /* enable ints */
#define TPM_CMD_READY_INT		0x00000080 /* cmd ready enable */
#define TPM_INT_EDGE_FALLING		0x00000018
#define TPM_INT_EDGE_RISING		0x00000010
#define TPM_INT_LEVEL_LOW		0x00000008
#define TPM_INT_LEVEL_HIGH		0x00000000
#define TPM_LOCALITY_CHANGE_INT		0x00000004 /* locality change enable */
#define TPM_STS_VALID_INT		0x00000002 /* int on TPM_STS_VALID is set */
#define TPM_DATA_AVAIL_INT		0x00000001 /* int on TPM_STS_DATA_AVAIL is set */
#define TPM_INTERRUPT_ENABLE_BITS \
    "\020\040ENA\010RDY\03LOCH\02STSV\01DRDY"

#define TPM_INT_VECTOR			0x000c	/* 8 bit reg for 4 bit irq vector */
#define TPM_INT_STATUS			0x0010	/* bits are & 0x87 from TPM_INTERRUPT_ENABLE */

#define TPM_INTF_CAPABILITIES		0x0014	/* capability register */
#define TPM_INTF_BURST_COUNT_STATIC	0x0100	/* TPM_STS_BMASK static */
#define TPM_INTF_CMD_READY_INT		0x0080	/* int on ready supported */
#define TPM_INTF_INT_EDGE_FALLING	0x0040	/* falling edge ints supported */
#define TPM_INTF_INT_EDGE_RISING	0x0020	/* rising edge ints supported */
#define TPM_INTF_INT_LEVEL_LOW		0x0010	/* level-low ints supported */
#define TPM_INTF_INT_LEVEL_HIGH		0x0008	/* level-high ints supported */
#define TPM_INTF_LOCALITY_CHANGE_INT	0x0004	/* locality-change int (mb 1) */
#define TPM_INTF_STS_VALID_INT		0x0002	/* TPM_STS_VALID int supported */
#define TPM_INTF_DATA_AVAIL_INT		0x0001	/* TPM_STS_DATA_AVAIL int supported (mb 1) */
#define TPM_CAPSREQ \
  (TPM_INTF_DATA_AVAIL_INT|TPM_INTF_LOCALITY_CHANGE_INT|TPM_INTF_INT_LEVEL_LOW)
#define TPM_CAPBITS \
  "\020\01IDRDY\02ISTSV\03ILOCH\04IHIGH\05ILOW\06IEDGE\07IFALL\010IRDY\011BCST"

#define TPM_STS				0x0018	   /* status register */
#define TPM_STS_MASK			0x000000ff /* status bits */
#define TPM_STS_BMASK			0x00ffff00 /* ro io burst size */
#define TPM_STS_VALID			0x00000080 /* ro other bits are valid */
#define TPM_STS_CMD_READY		0x00000040 /* rw chip/signal ready */
#define TPM_STS_GO			0x00000020 /* wo start the command */
#define TPM_STS_DATA_AVAIL		0x00000010 /* ro data available */
#define TPM_STS_DATA_EXPECT		0x00000008 /* ro more data to be written */
#define TPM_STS_RESP_RETRY		0x00000002 /* wo resend the response */
#define TPM_STS_BITS	"\020\010VALID\07RDY\06GO\05DRDY\04EXPECT\02RETRY"

#define TPM_DATA			0x0024
#define TPM_ID				0x0f00
#define TPM_REV				0x0f04
#define TPM_SIZE			0x5000	/* five pages of the above */

#define TPM_ACCESS_TMO			2000	/* 2sec */
#define TPM_READY_TMO			2000	/* 2sec */
#define TPM_READ_TMO			120000	/* 2 minutes */
#define TPM_BURST_TMO			2000	/* 2sec */

#define TPM2_START_METHOD_TIS		6
#define TPM2_START_METHOD_CRB		7

#define TPM_CRB_LOC_STATE		0x0
#define TPM_CRB_LOC_CTRL		0x8
#define TPM_LOC_STS			0xC
#define TPM_CRB_INTF_ID			0x30
#define TPM_CRB_CTRL_EXT		0x38
#define TPM_CRB_CTRL_REQ		0x40
#define TPM_CRB_CTRL_STS		0x44
#define TPM_CRB_CTRL_CANCEL		0x48
#define TPM_CRB_CTRL_START		0x4C
#define TPM_CRB_CTRL_CMD_SIZE		0x58
#define TPM_CRB_CTRL_CMD_LADDR		0x5C
#define TPM_CRB_CTRL_CMD_HADDR		0x60
#define TPM_CRB_CTRL_RSP_SIZE		0x64
#define TPM_CRB_CTRL_RSP_LADDR		0x68
#define TPM_CRB_CTRL_RSP_HADDR		0x6c
#define TPM_CRB_DATA_BUFFER		0x80

#define TPM_CRB_LOC_STATE_ESTB		(1 << 0)
#define TPM_CRB_LOC_STATE_ASSIGNED	(1 << 1)
#define TPM_CRB_LOC_ACTIVE_MASK	0x009c
#define TPM_CRB_LOC_VALID		(1 << 7)

#define TPM_CRB_LOC_REQUEST		(1 << 0)
#define TPM_CRB_LOC_RELEASE		(1 << 1)

#define TPM_CRB_CTRL_REQ_GO_READY	(1 << 0)
#define TPM_CRB_CTRL_REQ_GO_IDLE	(1 << 1)

#define TPM_CRB_CTRL_STS_ERR_BIT	(1 << 0)
#define TPM_CRB_CTRL_STS_IDLE_BIT	(1 << 1)

#define TPM_CRB_CTRL_CANCEL_CMD		0x1
#define TPM_CRB_CTRL_CANCEL_CLEAR	0x0

#define TPM_CRB_CTRL_START_CMD		(1 << 0)
#define TPM_CRB_INT_ENABLED_BIT		(1U << 31)

#define TPM2_RC_SUCCESS			0x0000
#define TPM2_RC_INITIALIZE		0x0100
#define TPM2_RC_FAILURE			0x0101
#define TPM2_RC_DISABLED		0x0120
#define TPM2_RC_RETRY			0x0922

struct tpm_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_bt;
	bus_space_handle_t	sc_bh;
	bus_size_t		sc_bbase;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;

	uint32_t		sc_devid;
	uint32_t		sc_rev;

	int			sc_tpm20;
	int			sc_tpm_mode;
#define TPM_TIS		0
#define TPM_CRB		1
	bus_size_t		sc_cmd_off;
	bus_size_t		sc_rsp_off;
	size_t			sc_cmd_sz;
	size_t			sc_rsp_sz;

	int			sc_enabled;
};

const struct {
	uint32_t devid;
	char name[32];
} tpm_devs[] = {
	{ 0x000615d1, "Infineon SLD9630 1.1" },
	{ 0x000b15d1, "Infineon SLB9635 1.2" },
	{ 0x100214e4, "Broadcom BCM0102" },
	{ 0x00fe1050, "WEC WPCT200" },
	{ 0x687119fa, "SNS SSX35" },
	{ 0x2e4d5453, "STM ST19WP18" },
	{ 0x32021114, "Atmel 97SC3203" },
	{ 0x10408086, "Intel INTC0102" },
	{ 0, "" },
};

int	tpm_match(struct device *, void *, void *);
void	tpm_attach(struct device *, struct device *, void *);
int	tpm_activate(struct device *, int);

int	tpm_probe(bus_space_tag_t, bus_space_handle_t);
int	tpm_init_tis(struct tpm_softc *);
int	tpm_init_crb(struct tpm_softc *);
int	tpm_read_tis(struct tpm_softc *, void *, int, size_t *, int);
int	tpm_read_crb(struct tpm_softc *, void *, int);
int	tpm_write_tis(struct tpm_softc *, void *, int);
int	tpm_write_crb(struct tpm_softc *, void *, int);
int	tpm_suspend(struct tpm_softc *);
int	tpm_resume(struct tpm_softc *);

int	tpm_waitfor(struct tpm_softc *, bus_space_handle_t, uint32_t, uint32_t, int);
int	tpm_waitfor_status(struct tpm_softc *, uint8_t, int);
int	tpm_request_locality_tis(struct tpm_softc *, int);
int	tpm_request_locality_crb(struct tpm_softc *, int);
void	tpm_release_locality_tis(struct tpm_softc *);
void	tpm_release_locality_crb(struct tpm_softc *);
uint8_t	tpm_status(struct tpm_softc *);

uint32_t tpm2_start_method(struct acpi_softc *);

const struct cfattach tpm_ca = {
	sizeof(struct tpm_softc),
	tpm_match,
	tpm_attach,
	NULL,
	tpm_activate
};

struct cfdriver tpm_cd = {
	NULL, "tpm", DV_DULL, CD_SKIPHIBERNATE	/* XXX */
};

const char *tpm_hids[] = {
	"PNP0C31",
	"ATM1200",
	"IFX0102",
	"BCM0101",
	"BCM0102",
	"NSC1200",
	"ICO0102",
	"MSFT0101",
	NULL
};

int
tpm_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args	*aa = aux;
	struct cfdata		*cf = match;

	if (aa->aaa_naddr < 1)
		return 0;
	return (acpi_matchhids(aa, tpm_hids, cf->cf_driver->cd_name));
}

void
tpm_attach(struct device *parent, struct device *self, void *aux)
{
	struct tpm_softc	*sc = (struct tpm_softc *)self;
	struct acpi_attach_args *aaa = aux;
	int64_t			sta;
	uint32_t		start_method;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aaa->aaa_node;
	sc->sc_enabled = 0;
	sc->sc_tpm_mode = TPM_TIS;

	printf(" %s", sc->sc_devnode->name);

	if (strcmp(aaa->aaa_dev, "MSFT0101") == 0 ||
	    strcmp(aaa->aaa_cdev, "MSFT0101") == 0) {
		sc->sc_tpm20 = 1;
		/* Identify if using 1.2 TIS or 2.0's CRB methods */
		start_method = tpm2_start_method(sc->sc_acpi);
		switch (start_method) {
		case TPM2_START_METHOD_TIS:
			/* Already default */
			break;
		case TPM2_START_METHOD_CRB:
			sc->sc_tpm_mode = TPM_CRB;
			break;
		default:
			printf(": unsupported TPM2 start method %d\n", start_method);
			return;
		}
	}

	printf(" %s (%s)", sc->sc_tpm20 ? "2.0" : "1.2",
	    sc->sc_tpm_mode == TPM_TIS ? "TIS" : "CRB");

	sta = acpi_getsta(sc->sc_acpi, sc->sc_devnode);
	if ((sta & (STA_PRESENT | STA_ENABLED | STA_DEV_OK)) !=
	    (STA_PRESENT | STA_ENABLED | STA_DEV_OK)) {
		printf(": not enabled\n");
		return;
	}

	printf(" addr 0x%llx/0x%llx", aaa->aaa_addr[0], aaa->aaa_size[0]);
	sc->sc_bbase = aaa->aaa_addr[0];

	sc->sc_bt = aaa->aaa_bst[0];
	if (bus_space_map(sc->sc_bt, aaa->aaa_addr[0], aaa->aaa_size[0],
	    0, &sc->sc_bh)) {
		printf(": can't map registers\n");
		return;
	}

	if (sc->sc_tpm_mode == TPM_TIS) {
		if (!tpm_probe(sc->sc_bt, sc->sc_bh)) {
			printf(": probe failed\n");
			return;
		}

		if (tpm_init_tis(sc) != 0) {
			printf(": init failed\n");
			return;
		}
	} else {
		if (tpm_init_crb(sc) != 0) {
			printf(": init failed\n");
			return;
		}
	}

	printf("\n");
	sc->sc_enabled = 1;
}

int
tpm_activate(struct device *self, int act)
{
	struct tpm_softc	*sc = (struct tpm_softc *)self;

	switch (act) {
	case DVACT_SUSPEND:
		if (!sc->sc_enabled) {
			DPRINTF(("%s: suspend, but not enabled\n",
			    sc->sc_dev.dv_xname));
			return 0;
		}
		tpm_suspend(sc);
		break;

	case DVACT_WAKEUP:
		if (!sc->sc_enabled) {
			DPRINTF(("%s: wakeup, but not enabled\n",
			    sc->sc_dev.dv_xname));
			return 0;
		}
		tpm_resume(sc);
		break;
	}

	return 0;
}

int
tpm_suspend(struct tpm_softc *sc)
{
	uint8_t command1[] = {
	    0, 0xc1,		/* TPM_TAG_RQU_COMMAND */
	    0, 0, 0, 10,	/* Length in bytes */
	    0, 0, 0, 0x98	/* TPM_ORD_SaveStates */
	};
	uint8_t command2[] = {
	    0x80, 0x01,		/* TPM_ST_COMMAND_TAG */
	    0, 0, 0, 12,	/* Length in bytes */
	    0, 0, 0x01, 0x45,	/* TPM_CC_Shutdown */
	    0x00, 0x01
	};
	uint8_t *command;
	size_t commandlen;

	if (sc->sc_acpi->sc_state == ACPI_STATE_S0)
		return 0;

	DPRINTF(("%s: saving state preparing for suspend\n",
	    sc->sc_dev.dv_xname));

	if (sc->sc_tpm20) {
		command = command2;
		commandlen = sizeof(command2);
	} else {
		command = command1;
		commandlen = sizeof(command1);
	}

	/*
	 * Tell the chip to save its state so the BIOS can then restore it upon
	 * resume.
	 */
	if (sc->sc_tpm_mode == TPM_TIS) {
		tpm_write_tis(sc, command, commandlen);
		memset(command, 0, commandlen);
		tpm_read_tis(sc, command, commandlen, NULL, TPM_HDRSIZE);
	} else {
		tpm_write_crb(sc, command, commandlen);
		memset(command, 0, commandlen);
		tpm_read_crb(sc, command, commandlen);
	}
	return 0;
}

int
tpm_resume(struct tpm_softc *sc)
{
	/*
	 * TODO: The BIOS should have restored the chip's state for us already,
	 * but we should tell the chip to do a self-test here (according to the
	 * Linux driver).
	 */

	DPRINTF(("%s: resume\n", sc->sc_dev.dv_xname));
	return 0;
}

uint32_t
tpm2_start_method(struct acpi_softc *sc)
{
	struct acpi_q *entry;
	struct acpi_tpm2 *p_tpm2 = NULL;

	SIMPLEQ_FOREACH(entry, &sc->sc_tables, q_next) {
		if (memcmp(entry->q_table, TPM2_SIG,
		    sizeof(TPM2_SIG) - 1) == 0) {
			p_tpm2 = entry->q_table;
			break;
		}
	}

	if (!p_tpm2) {
		DPRINTF((", no TPM2 table"));
		return 0;
	}

	return p_tpm2->start_method;
}

int
tpm_probe(bus_space_tag_t bt, bus_space_handle_t bh)
{
	uint32_t r;
	int tries = 10000;

	/* wait for chip to settle */
	while (tries--) {
		if (bus_space_read_1(bt, bh, TPM_ACCESS) & TPM_ACCESS_VALID)
			break;
		else if (!tries) {
			printf(": timed out waiting for validity\n");
			return 1;
		}

		DELAY(10);
	}

	r = bus_space_read_4(bt, bh, TPM_INTF_CAPABILITIES);
	if (r == 0xffffffff)
		return 0;

	return 1;
}

int
tpm_init_tis(struct tpm_softc *sc)
{
	uint32_t r, intmask;
	int i;

	r = bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_INTF_CAPABILITIES);
	if ((r & TPM_CAPSREQ) != TPM_CAPSREQ ||
	    !(r & (TPM_INTF_INT_EDGE_RISING | TPM_INTF_INT_LEVEL_LOW))) {
		DPRINTF((": caps too low (caps=%b)\n", r, TPM_CAPBITS));
		return 0;
	}

	/* ack and disable all interrupts, we'll be using polling only */
	intmask = bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_INTERRUPT_ENABLE);
	intmask |= TPM_INTF_CMD_READY_INT | TPM_INTF_LOCALITY_CHANGE_INT |
	    TPM_INTF_DATA_AVAIL_INT | TPM_INTF_STS_VALID_INT;
	intmask &= ~TPM_GLOBAL_INT_ENABLE;
	bus_space_write_4(sc->sc_bt, sc->sc_bh, TPM_INTERRUPT_ENABLE, intmask);

	if (tpm_request_locality_tis(sc, 0)) {
		printf(", requesting locality failed\n");
		return 1;
	}

	sc->sc_devid = bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_ID);
	sc->sc_rev = bus_space_read_1(sc->sc_bt, sc->sc_bh, TPM_REV);

	for (i = 0; tpm_devs[i].devid; i++)
		if (tpm_devs[i].devid == sc->sc_devid)
			break;

	if (tpm_devs[i].devid)
		printf(", %s rev 0x%x", tpm_devs[i].name, sc->sc_rev);
	else
		printf(", device 0x%08x rev 0x%x", sc->sc_devid, sc->sc_rev);

	return 0;
}

int
tpm_init_crb(struct tpm_softc *sc)
{
	uint32_t intmask;
	int i;

	if (tpm_request_locality_crb(sc, 0)) {
		printf(", request locality failed\n");
		return 1;
	}

	/* ack and disable all interrupts, we'll be using polling only */
	intmask = bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_INTERRUPT_ENABLE);
	intmask &= ~TPM_CRB_INT_ENABLED_BIT;
	bus_space_write_4(sc->sc_bt, sc->sc_bh, TPM_INTERRUPT_ENABLE, intmask);

	/* Identify command and response registers and sizes */
	sc->sc_cmd_off = bus_space_read_4(sc->sc_bt, sc->sc_bh,
	    TPM_CRB_CTRL_CMD_LADDR);
	sc->sc_cmd_off |= ((uint64_t) bus_space_read_4(sc->sc_bt, sc->sc_bh,
	    TPM_CRB_CTRL_CMD_HADDR) << 32);
	sc->sc_cmd_sz = bus_space_read_4(sc->sc_bt, sc->sc_bh,
	    TPM_CRB_CTRL_CMD_SIZE);

	sc->sc_rsp_off = bus_space_read_4(sc->sc_bt, sc->sc_bh,
	    TPM_CRB_CTRL_RSP_LADDR);
	sc->sc_rsp_off |= ((uint64_t) bus_space_read_4(sc->sc_bt, sc->sc_bh,
	    TPM_CRB_CTRL_RSP_HADDR) << 32);
	sc->sc_rsp_sz = bus_space_read_4(sc->sc_bt, sc->sc_bh,
	    TPM_CRB_CTRL_RSP_SIZE);

	DPRINTF((", cmd @ 0x%lx, %ld, rsp @ 0x%lx, %ld", sc->sc_cmd_off,
	    sc->sc_cmd_sz, sc->sc_rsp_off, sc->sc_rsp_sz));

	sc->sc_cmd_off = sc->sc_cmd_off - sc->sc_bbase;
	sc->sc_rsp_off = sc->sc_rsp_off - sc->sc_bbase;

	tpm_release_locality_crb(sc);

	/* If it's a unified buffer, the sizes must be the same. */
	if (sc->sc_cmd_off == sc->sc_rsp_off) {
		if (sc->sc_cmd_sz != sc->sc_rsp_sz) {
			printf(", invalid buffer sizes\n");
			return 1;
		}
	}

	for (i = 0; tpm_devs[i].devid; i++)
		if (tpm_devs[i].devid == sc->sc_devid)
			break;

	if (tpm_devs[i].devid)
		printf(", %s rev 0x%x", tpm_devs[i].name, sc->sc_rev);
	else
		printf(", device 0x%08x rev 0x%x", sc->sc_devid, sc->sc_rev);

	return 0;
}

int
tpm_request_locality_tis(struct tpm_softc *sc, int l)
{
	uint32_t r;
	int to;

	if (l != 0)
		return EINVAL;

	if ((bus_space_read_1(sc->sc_bt, sc->sc_bh, TPM_ACCESS) &
	    (TPM_ACCESS_VALID | TPM_ACCESS_ACTIVE_LOCALITY)) ==
	    (TPM_ACCESS_VALID | TPM_ACCESS_ACTIVE_LOCALITY))
		return 0;

	bus_space_write_1(sc->sc_bt, sc->sc_bh, TPM_ACCESS,
	    TPM_ACCESS_REQUEST_USE);

	to = TPM_ACCESS_TMO * 100;	/* steps of 10 microseconds */

	while ((r = bus_space_read_1(sc->sc_bt, sc->sc_bh, TPM_ACCESS) &
	    (TPM_ACCESS_VALID | TPM_ACCESS_ACTIVE_LOCALITY)) !=
	    (TPM_ACCESS_VALID | TPM_ACCESS_ACTIVE_LOCALITY) && to--) {
		DELAY(10);
	}

	if ((r & (TPM_ACCESS_VALID | TPM_ACCESS_ACTIVE_LOCALITY)) !=
	    (TPM_ACCESS_VALID | TPM_ACCESS_ACTIVE_LOCALITY)) {
		DPRINTF(("%s: %s: access %b\n", sc->sc_dev.dv_xname, __func__,
		    r, TPM_ACCESS_BITS));
		return EBUSY;
	}

	return 0;
}

int
tpm_request_locality_crb(struct tpm_softc *sc, int l)
{
	uint32_t r, mask;
	int to;

	if (l != 0)
		return EINVAL;

	r = bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_CRB_LOC_CTRL);
	bus_space_write_4(sc->sc_bt, sc->sc_bh, TPM_CRB_LOC_CTRL,
	    r | TPM_CRB_LOC_REQUEST);

	to = TPM_ACCESS_TMO * 200;
	mask = TPM_CRB_LOC_STATE_ASSIGNED | TPM_CRB_LOC_VALID;

	r = bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_CRB_LOC_STATE);
	while ((r & mask) != mask && to--) {
		DELAY(10);
		r = bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_CRB_LOC_STATE);
	}

	if ((r & mask) != mask) {
		printf(", CRB loc FAILED");
		return EBUSY;
	}

	return 0;
}

void
tpm_release_locality_tis(struct tpm_softc *sc)
{
	if ((bus_space_read_1(sc->sc_bt, sc->sc_bh, TPM_ACCESS) &
	    (TPM_ACCESS_REQUEST_PENDING|TPM_ACCESS_VALID)) ==
	    (TPM_ACCESS_REQUEST_PENDING|TPM_ACCESS_VALID)) {
		DPRINTF(("%s: releasing locality\n", sc->sc_dev.dv_xname));
		bus_space_write_1(sc->sc_bt, sc->sc_bh, TPM_ACCESS,
		    TPM_ACCESS_ACTIVE_LOCALITY);
	}
}

void
tpm_release_locality_crb(struct tpm_softc *sc)
{
	uint32_t r;

	r = bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_CRB_LOC_CTRL);
	bus_space_write_4(sc->sc_bt, sc->sc_bh, TPM_CRB_LOC_CTRL,
	    r | TPM_CRB_LOC_RELEASE);
}

int
tpm_getburst(struct tpm_softc *sc)
{
	int burst, burst2, to;

	to = TPM_BURST_TMO * 100;	/* steps of 10 microseconds */

	burst = 0;
	while (burst == 0 && to--) {
		/*
		 * Burst count has to be read from bits 8 to 23 without
		 * touching any other bits, eg. the actual status bits 0 to 7.
		 */
		burst = bus_space_read_1(sc->sc_bt, sc->sc_bh, TPM_STS + 1);
		DPRINTF(("%s: %s: read1(0x%x): 0x%x\n", sc->sc_dev.dv_xname,
		    __func__, TPM_STS + 1, burst));
		burst2 = bus_space_read_1(sc->sc_bt, sc->sc_bh, TPM_STS + 2);
		DPRINTF(("%s: %s: read1(0x%x): 0x%x\n", sc->sc_dev.dv_xname,
		    __func__, TPM_STS + 2, burst2));
		burst |= burst2 << 8;
		if (burst)
			return burst;

		DELAY(10);
	}

	DPRINTF(("%s: getburst timed out\n", sc->sc_dev.dv_xname));

	return 0;
}

uint8_t
tpm_status(struct tpm_softc *sc)
{
	return bus_space_read_1(sc->sc_bt, sc->sc_bh, TPM_STS) & TPM_STS_MASK;
}

int
tpm_waitfor(struct tpm_softc *sc, bus_size_t offset, uint32_t mask,
    uint32_t val, int msecs)
{
	int usecs;
	uint32_t r;

	usecs = msecs * 1000;

	r = bus_space_read_4(sc->sc_bt, sc->sc_bh, offset);
	if ((r & mask) == val)
		return 0;

	while (usecs > 0) {
		r = bus_space_read_4(sc->sc_bt, sc->sc_bh, offset);
		if ((r & mask) == val)
			return 0;
		DELAY(1);
		usecs--;
	}

	DPRINTF(("%s: %s: timed out, status 0x%x != 0x%x\n",
		    sc->sc_dev.dv_xname, __func__, r, mask));
	return ETIMEDOUT;
}

int
tpm_waitfor_status(struct tpm_softc *sc, uint8_t mask, int msecs)
{
	int usecs;
	uint8_t status;

	usecs = msecs * 1000;

	while (((status = tpm_status(sc)) & mask) != mask) {
		if (usecs == 0) {
			DPRINTF(("%s: %s: timed out, status 0x%x != 0x%x\n",
			    sc->sc_dev.dv_xname, __func__, status, mask));
			return status;
		}

		usecs--;
		DELAY(1);
	}

	return 0;
}

int
tpm_read_tis(struct tpm_softc *sc, void *buf, int len, size_t *count,
    int flags)
{
	uint8_t *p = buf;
	uint8_t c;
	size_t cnt;
	int rv, n, bcnt;

	DPRINTF(("%s: %s %d:", sc->sc_dev.dv_xname, __func__, len));

	cnt = 0;
	while (len > 0) {
		if ((rv = tpm_waitfor_status(sc,
		    TPM_STS_DATA_AVAIL | TPM_STS_VALID, TPM_READ_TMO)))
			return rv;

		bcnt = tpm_getburst(sc);
		n = MIN(len, bcnt);

		for (; n--; len--) {
			c = bus_space_read_1(sc->sc_bt, sc->sc_bh, TPM_DATA);
			DPRINTF((" %02x", c));
			*p++ = c;
			cnt++;
		}

		if ((flags & TPM_PARAM_SIZE) == 0 && cnt >= 6)
			break;
	}

	DPRINTF(("\n"));

	if (count)
		*count = cnt;

	return 0;
}

int
tpm_read_crb(struct tpm_softc *sc, void *buf, int len)
{
	uint8_t *p = buf;
	uint32_t sz = 0, mask, rc;
	size_t count = 0;
	int r;

	DPRINTF(("%s: %s %d:", sc->sc_dev.dv_xname, __func__, len));

	if (len < TPM_HDRSIZE) {
		printf("%s: %s buf len too small\n", sc->sc_dev.dv_xname,
		    __func__);
		return EINVAL;
	}

	while (count < TPM_HDRSIZE) {
		*p = bus_space_read_1(sc->sc_bt, sc->sc_bh,
		    sc->sc_rsp_off + count);
		DPRINTF((" %02x", *p));
		count++;
		p++;
	}
	DPRINTF(("\n"));

	/* Response length is bytes 2-5 in the response header. */
	p = buf;
	sz = be32toh(*(uint32_t *) (p + 2));
	if (sz < TPM_HDRSIZE || sz > sc->sc_rsp_sz) {
		printf("%s: invalid response size %d\n",
		    sc->sc_dev.dv_xname, sz);
		return EIO;
	}
	if (sz > len)
		printf("%s: response size too large, truncated to %d\n",
		    sc->sc_dev.dv_xname, len);

	/* Response code is bytes 6-9. */
	rc = be32toh(*(uint32_t *) (p + 6));
	if (rc != TPM2_RC_SUCCESS) {
		printf("%s: command failed (0x%04x)\n", sc->sc_dev.dv_xname,
		    rc);
		/* Nothing we can do on failure. Still try to idle the tpm. */
	}

	/* Tell the device to go idle. */
	r = bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_CRB_CTRL_REQ);
	bus_space_write_4(sc->sc_bt, sc->sc_bh, TPM_CRB_CTRL_REQ,
	    r | TPM_CRB_CTRL_REQ_GO_IDLE);

	mask = TPM_CRB_CTRL_STS_IDLE_BIT;
	if (tpm_waitfor(sc, TPM_CRB_CTRL_STS, mask, mask, 200)) {
		printf("%s: failed to transition to idle state after read\n",
		    sc->sc_dev.dv_xname);
	}

	tpm_release_locality_crb(sc);

	DPRINTF(("%s: %s completed\n", sc->sc_dev.dv_xname, __func__));
	return 0;
}

int
tpm_write_tis(struct tpm_softc *sc, void *buf, int len)
{
	uint8_t *p = buf;
	uint8_t status;
	size_t count = 0;
	int rv, r;

	if ((rv = tpm_request_locality_tis(sc, 0)) != 0)
		return rv;

	DPRINTF(("%s: %s %d:", sc->sc_dev.dv_xname, __func__, len));
	for (r = 0; r < len; r++)
		DPRINTF((" %02x", (uint8_t)(*(p + r))));
	DPRINTF(("\n"));

	/* read status */
	status = tpm_status(sc);
	if ((status & TPM_STS_CMD_READY) == 0) {
		/* abort! */
		bus_space_write_1(sc->sc_bt, sc->sc_bh, TPM_STS,
		    TPM_STS_CMD_READY);
		if ((rv = tpm_waitfor_status(sc, TPM_STS_CMD_READY,
		    TPM_READ_TMO))) {
			DPRINTF(("%s: failed waiting for ready after abort "
			    "(0x%x)\n", sc->sc_dev.dv_xname, rv));
			return rv;
		}
	}

	while (count < len - 1) {
		for (r = tpm_getburst(sc); r > 0 && count < len - 1; r--) {
			DPRINTF(("%s: %s: write1(0x%x, 0x%x)\n",
			    sc->sc_dev.dv_xname, __func__, TPM_DATA, *p));
			bus_space_write_1(sc->sc_bt, sc->sc_bh, TPM_DATA, *p++);
			count++;
		}
		if ((rv = tpm_waitfor_status(sc, TPM_STS_VALID | TPM_STS_DATA_EXPECT,
		    TPM_READ_TMO))) {
			DPRINTF(("%s: %s: failed waiting for next byte (%d)\n",
			    sc->sc_dev.dv_xname, __func__, rv));
			return rv;
		}
	}

	DPRINTF(("%s: %s: write1(0x%x, 0x%x)\n", sc->sc_dev.dv_xname, __func__,
	    TPM_DATA, *p));
	bus_space_write_1(sc->sc_bt, sc->sc_bh, TPM_DATA, *p);
	count++;

	if ((rv = tpm_waitfor_status(sc, TPM_STS_VALID, TPM_READ_TMO))) {
		DPRINTF(("%s: %s: failed after last byte (%d)\n",
		    sc->sc_dev.dv_xname, __func__, rv));
		return rv;
	}

	if ((status = tpm_status(sc)) & TPM_STS_DATA_EXPECT) {
		DPRINTF(("%s: %s: final status still expecting data: %b\n",
		    sc->sc_dev.dv_xname, __func__, status, TPM_STS_BITS));
		return status;
	}

	DPRINTF(("%s: final status after write: %b\n", sc->sc_dev.dv_xname,
	    status, TPM_STS_BITS));

	/* XXX: are we ever sending non-command data? */
	bus_space_write_1(sc->sc_bt, sc->sc_bh, TPM_STS, TPM_STS_GO);

	return 0;
}

int
tpm_write_crb(struct tpm_softc *sc, void *buf, int len)
{
	uint8_t *p = buf;
	size_t count = 0;
	uint32_t r, mask;

	if (len > sc->sc_cmd_sz) {
		printf("%s: requested write length larger than cmd buffer\n",
		    sc->sc_dev.dv_xname);
		return EINVAL;
	}

	if (bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_CRB_CTRL_STS)
	    & TPM_CRB_CTRL_STS_ERR_BIT) {
		printf("%s: device error bit set\n", sc->sc_dev.dv_xname);
		return EIO;
	}

	if (tpm_request_locality_crb(sc, 0)) {
		printf("%s: failed to acquire locality\n", sc->sc_dev.dv_xname);
		return EIO;
	}

	/* Clear cancellation bit */
	bus_space_write_4(sc->sc_bt, sc->sc_bh, TPM_CRB_CTRL_CANCEL,
	    TPM_CRB_CTRL_CANCEL_CLEAR);

	/* Toggle to idle state (if needed) and then to ready */
	r = bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_CRB_CTRL_STS);
	if(!(r & TPM_CRB_CTRL_STS_IDLE_BIT)) {
		printf("%s: asking device to idle\n", sc->sc_dev.dv_xname);
		r = bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_CRB_CTRL_REQ);
		bus_space_write_4(sc->sc_bt, sc->sc_bh, TPM_CRB_CTRL_REQ,
		    r | TPM_CRB_CTRL_REQ_GO_IDLE);

		mask = TPM_CRB_CTRL_STS_IDLE_BIT;
		if (tpm_waitfor(sc, TPM_CRB_CTRL_STS, mask, mask, 200)) {
			printf("%s: failed to transition to idle state before "
			    "write\n", sc->sc_dev.dv_xname);
			return EIO;
		}
	}
	r = bus_space_read_4(sc->sc_bt, sc->sc_bh, TPM_CRB_CTRL_REQ);
	bus_space_write_4(sc->sc_bt, sc->sc_bh, TPM_CRB_CTRL_REQ,
 	    r | TPM_CRB_CTRL_REQ_GO_READY);
	mask = TPM_CRB_CTRL_REQ_GO_READY;
	if (tpm_waitfor(sc, TPM_CRB_CTRL_STS, mask, !mask, 200)) {
		printf("%s: failed to transition to ready state\n",
		    sc->sc_dev.dv_xname);
		return EIO;
	}

	/* Write the command */
	DPRINTF(("%s: %s %d:", sc->sc_dev.dv_xname, __func__, len));
	while (count < len) {
		DPRINTF((" %02x", (uint8_t)(*p)));
		bus_space_write_1(sc->sc_bt, sc->sc_bh, sc->sc_cmd_off + count,
		    *p++);
		count++;
	}
	DPRINTF(("\n"));
	bus_space_barrier(sc->sc_bt, sc->sc_bh, sc->sc_cmd_off, len,
	    BUS_SPACE_BARRIER_WRITE);
	DPRINTF(("%s: %s wrote %lu bytes\n", sc->sc_dev.dv_xname, __func__,
	    count));

	/* Send the Start Command request */
	bus_space_write_4(sc->sc_bt, sc->sc_bh, TPM_CRB_CTRL_START,
	    TPM_CRB_CTRL_START_CMD);
	bus_space_barrier(sc->sc_bt, sc->sc_bh, TPM_CRB_CTRL_START, 4,
	    BUS_SPACE_BARRIER_WRITE);

	/* Check if command was processed */
	mask = ~0;
	if (tpm_waitfor(sc, TPM_CRB_CTRL_START, mask, ~mask, 200)) {
		printf("%s: timeout waiting for device to process command\n",
		    sc->sc_dev.dv_xname);
		return EIO;
	}

	return 0;
}
