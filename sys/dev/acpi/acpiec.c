/* $OpenBSD: acpiec.c,v 1.66 2024/06/25 11:57:10 kettenis Exp $ */
/*
 * Copyright (c) 2006 Can Erkin Acar <canacar@openbsd.org>
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

#include <sys/param.h>
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <sys/sensors.h>

int		acpiec_match(struct device *, void *, void *);
void		acpiec_attach(struct device *, struct device *, void *);

uint8_t		acpiec_status(struct acpiec_softc *);
uint8_t		acpiec_read_data(struct acpiec_softc *);
void		acpiec_write_cmd(struct acpiec_softc *, uint8_t);
void		acpiec_write_data(struct acpiec_softc *, uint8_t);
void		acpiec_burst_enable(struct acpiec_softc *sc);
void		acpiec_burst_disable(struct acpiec_softc *sc);

uint8_t		acpiec_read_1(struct acpiec_softc *, uint8_t);
void		acpiec_write_1(struct acpiec_softc *, uint8_t, uint8_t);

void		acpiec_read(struct acpiec_softc *, uint8_t, int, uint8_t *);
void		acpiec_write(struct acpiec_softc *, uint8_t, int, uint8_t *);

int		acpiec_getcrs(struct acpiec_softc *,
		    struct acpi_attach_args *);
int		acpiec_parse_resources(int, union acpi_resource *, void *);

void		acpiec_wait(struct acpiec_softc *, uint8_t, uint8_t);
void		acpiec_sci_event(struct acpiec_softc *);

void		acpiec_get_events(struct acpiec_softc *);

int		acpiec_gpehandler(struct acpi_softc *, int, void *);

/* EC Status bits */
#define		EC_STAT_SMI_EVT	0x40	/* SMI event pending */
#define		EC_STAT_SCI_EVT	0x20	/* SCI event pending */
#define		EC_STAT_BURST	0x10	/* Controller in burst mode */
#define		EC_STAT_CMD	0x08	/* data is command */
#define		EC_STAT_IBF	0x02	/* input buffer full */
#define		EC_STAT_OBF	0x01	/* output buffer full */

/* EC Commands */
#define		EC_CMD_RD	0x80	/* Read */
#define		EC_CMD_WR	0x81	/* Write */
#define		EC_CMD_BE	0x82	/* Burst Enable */
#define		EC_CMD_BD	0x83	/* Burst Disable */
#define		EC_CMD_QR	0x84	/* Query */

int	acpiec_reg(struct acpiec_softc *);

const struct cfattach acpiec_ca = {
	sizeof(struct acpiec_softc), acpiec_match, acpiec_attach
};

struct cfdriver acpiec_cd = {
	NULL, "acpiec", DV_DULL
};

const char *acpiec_hids[] = {
	ACPI_DEV_ECD,
	NULL
};

void
acpiec_wait(struct acpiec_softc *sc, uint8_t mask, uint8_t val)
{
	static int acpiecnowait;
	uint8_t		stat;

	dnprintf(40, "%s: EC wait_ns for: %b == %02x\n",
	    DEVNAME(sc), (int)mask,
	    "\20\x8IGN\x7SMI\x6SCI\05BURST\04CMD\03IGN\02IBF\01OBF", (int)val);

	while (((stat = acpiec_status(sc)) & mask) != val) {
		if (stat & EC_STAT_SCI_EVT)
			sc->sc_gotsci = 1;
		if (cold || (stat & EC_STAT_BURST))
			delay(1);
		else
			tsleep(&acpiecnowait, PWAIT, "acpiec", 1);
	}

	dnprintf(40, "%s: EC wait_ns, stat: %b\n", DEVNAME(sc), (int)stat,
	    "\20\x8IGN\x7SMI\x6SCI\05BURST\04CMD\03IGN\02IBF\01OBF");
}

uint8_t
acpiec_status(struct acpiec_softc *sc)
{
	return (bus_space_read_1(sc->sc_cmd_bt, sc->sc_cmd_bh, 0));
}

void
acpiec_write_data(struct acpiec_softc *sc, uint8_t val)
{
	acpiec_wait(sc, EC_STAT_IBF, 0);
	dnprintf(40, "acpiec: write_data -- %d\n", (int)val);
	bus_space_write_1(sc->sc_data_bt, sc->sc_data_bh, 0, val);
}

void
acpiec_write_cmd(struct acpiec_softc *sc, uint8_t val)
{
	acpiec_wait(sc, EC_STAT_IBF, 0);
	dnprintf(40, "acpiec: write_cmd -- %d\n", (int)val);
	bus_space_write_1(sc->sc_cmd_bt, sc->sc_cmd_bh, 0, val);
}

uint8_t
acpiec_read_data(struct acpiec_softc *sc)
{
	uint8_t		val;

	acpiec_wait(sc, EC_STAT_OBF, EC_STAT_OBF);
	val = bus_space_read_1(sc->sc_data_bt, sc->sc_data_bh, 0);

	dnprintf(40, "acpiec: read_data %d\n", (int)val);

	return (val);
}

void
acpiec_sci_event(struct acpiec_softc *sc)
{
	uint8_t		evt;

	sc->sc_gotsci = 0;

	acpiec_wait(sc, EC_STAT_IBF, 0);
	bus_space_write_1(sc->sc_cmd_bt, sc->sc_cmd_bh, 0, EC_CMD_QR);

	acpiec_wait(sc, EC_STAT_OBF, EC_STAT_OBF);
	evt = bus_space_read_1(sc->sc_data_bt, sc->sc_data_bh, 0);

	if (evt) {
		dnprintf(10, "%s: sci_event: 0x%02x\n", DEVNAME(sc), (int)evt);
		aml_evalnode(sc->sc_acpi, sc->sc_events[evt].event, 0, NULL,
		    NULL);
	}
}

uint8_t
acpiec_read_1(struct acpiec_softc *sc, uint8_t addr)
{
	uint8_t		val;

	if ((acpiec_status(sc) & EC_STAT_SCI_EVT) == EC_STAT_SCI_EVT)
		sc->sc_gotsci = 1;

	acpiec_write_cmd(sc, EC_CMD_RD);
	acpiec_write_data(sc, addr);

	val = acpiec_read_data(sc);

	return (val);
}

void
acpiec_write_1(struct acpiec_softc *sc, uint8_t addr, uint8_t data)
{
	if ((acpiec_status(sc) & EC_STAT_SCI_EVT) == EC_STAT_SCI_EVT)
		sc->sc_gotsci = 1;

	acpiec_write_cmd(sc, EC_CMD_WR);
	acpiec_write_data(sc, addr);
	acpiec_write_data(sc, data);
}

void
acpiec_burst_enable(struct acpiec_softc *sc)
{
	if (sc->sc_cantburst)
		return;

	acpiec_write_cmd(sc, EC_CMD_BE);
	acpiec_read_data(sc);
}

void
acpiec_burst_disable(struct acpiec_softc *sc)
{
	if (sc->sc_cantburst)
		return;

	if ((acpiec_status(sc) & EC_STAT_BURST) == EC_STAT_BURST)
		acpiec_write_cmd(sc, EC_CMD_BD);
}

void
acpiec_read(struct acpiec_softc *sc, uint8_t addr, int len, uint8_t *buffer)
{
	int			reg;

	/*
	 * this works because everything runs in the acpi thread context.
	 * at some point add a lock to deal with concurrency so that a
	 * transaction does not get interrupted.
	 */
	dnprintf(20, "%s: read %d, %d\n", DEVNAME(sc), (int)addr, len);
	sc->sc_ecbusy = 1;
	acpiec_burst_enable(sc);
	for (reg = 0; reg < len; reg++)
		buffer[reg] = acpiec_read_1(sc, addr + reg);
	acpiec_burst_disable(sc);
	sc->sc_ecbusy = 0;
}

void
acpiec_write(struct acpiec_softc *sc, uint8_t addr, int len, uint8_t *buffer)
{
	int			reg;

	/*
	 * this works because everything runs in the acpi thread context.
	 * at some point add a lock to deal with concurrency so that a
	 * transaction does not get interrupted.
	 */
	dnprintf(20, "%s: write %d, %d\n", DEVNAME(sc), (int)addr, len);
	sc->sc_ecbusy = 1;
	acpiec_burst_enable(sc);
	for (reg = 0; reg < len; reg++)
		acpiec_write_1(sc, addr + reg, buffer[reg]);
	acpiec_burst_disable(sc);
	sc->sc_ecbusy = 0;
}

int
acpiec_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args	*aa = aux;
	struct cfdata		*cf = match;
	struct acpi_ecdt	*ecdt = aa->aaa_table;
	struct acpi_softc	*acpisc = (struct acpi_softc *)parent;

	/* Check for early ECDT table attach */
	if (ecdt && 
	    !memcmp(ecdt->hdr.signature, ECDT_SIG, sizeof(ECDT_SIG) - 1))
		return (1);
	if (acpisc->sc_ec)
		return (0);

	/* sanity */
	return (acpi_matchhids(aa, acpiec_hids, cf->cf_driver->cd_name));
}

void
acpiec_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpiec_softc	*sc = (struct acpiec_softc *)self;
	struct acpi_attach_args *aa = aux;
#ifndef SMALL_KERNEL
	struct acpi_wakeq *wq;
#endif
	struct aml_value res;
	int64_t st;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aa->aaa_node;
	sc->sc_cantburst = 0;

	if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "_STA", 0, NULL, &st))
		st = STA_PRESENT | STA_ENABLED | STA_DEV_OK;
	if ((st & STA_PRESENT) == 0) {
		printf(": not present\n");
		return;
	}

	printf("\n");
	if (acpiec_getcrs(sc, aa)) {
		printf("%s: Failed to read resource settings\n", DEVNAME(sc));
		return;
	}

	sc->sc_acpi->sc_ec = sc;

	if (acpiec_reg(sc)) {
		printf("%s: Failed to register address space\n", DEVNAME(sc));
		return;
	}

	/*
	 * Some Chromebooks using the Google EC do not support burst mode and
	 * cause us to spin forever waiting for the acknowledgment.  Don't use
	 * burst mode at all on these machines.
	 */
	if (hw_vendor != NULL && hw_prod != NULL &&
	    strcmp(hw_vendor, "GOOGLE") == 0 &&
	    strcmp(hw_prod, "Samus") == 0)
		sc->sc_cantburst = 1;

	acpiec_get_events(sc);

	dnprintf(10, "%s: GPE: %d\n", DEVNAME(sc), sc->sc_gpe);

#ifndef SMALL_KERNEL
	acpi_set_gpehandler(sc->sc_acpi, sc->sc_gpe, acpiec_gpehandler,
	    sc, GPE_EDGE);

	/*
	 * On many machines the EC is not listed as a wakeup device
	 * but is necessary to wake up from S0i.
	 */
	wq = malloc(sizeof(struct acpi_wakeq), M_DEVBUF, M_WAITOK | M_ZERO);
	wq->q_node = sc->sc_devnode;
	wq->q_gpe = sc->sc_gpe;
	wq->q_state = ACPI_STATE_S0;
	wq->q_enabled = 1;
	SIMPLEQ_INSERT_TAIL(&sc->sc_acpi->sc_wakedevs, wq, q_next);
#endif

	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, "_GLK", 0, NULL, &res))
		sc->sc_glk = 0;
	else if (res.type != AML_OBJTYPE_INTEGER)
		sc->sc_glk = 0;
	else
		sc->sc_glk = res.v_integer ? 1 : 0;
}

void
acpiec_get_events(struct acpiec_softc *sc)
{
	int			idx;
	char			name[16];

	memset(sc->sc_events, 0, sizeof(sc->sc_events));
	for (idx = 0; idx < ACPIEC_MAX_EVENTS; idx++) {
		snprintf(name, sizeof(name), "_Q%02X", idx);
		sc->sc_events[idx].event = aml_searchname(sc->sc_devnode, name);
		if (sc->sc_events[idx].event != NULL)
			dnprintf(10, "%s: Found event %s\n", DEVNAME(sc), name);
	}
}

int
acpiec_gpehandler(struct acpi_softc *acpi_sc, int gpe, void *arg)
{
	struct acpiec_softc	*sc = arg;
	uint8_t			mask, stat, en;
	int			s;

	KASSERT(sc->sc_ecbusy == 0);
	dnprintf(10, "ACPIEC: got gpe\n");

	do {
		if (sc->sc_gotsci)
			acpiec_sci_event(sc);

		stat = acpiec_status(sc);
		dnprintf(40, "%s: EC interrupt, stat: %b\n",
		    DEVNAME(sc), (int)stat,
		    "\20\x8IGN\x7SMI\x6SCI\05BURST\04CMD\03IGN\02IBF\01OBF");

		if (stat & EC_STAT_SCI_EVT)
			sc->sc_gotsci = 1;
		else
			sc->sc_gotsci = 0;
	} while (sc->sc_gotsci);

	/* Unmask the GPE which was blocked at interrupt time */
	s = splbio();
	mask = (1L << (gpe & 7));
	en = acpi_read_pmreg(acpi_sc, ACPIREG_GPE_EN, gpe>>3);
	acpi_write_pmreg(acpi_sc, ACPIREG_GPE_EN, gpe>>3, en | mask);
	splx(s);

	return (0);
}

int
acpiec_parse_resources(int crsidx, union acpi_resource *crs, void *arg)
{
	struct acpiec_softc *sc = arg;
	int type = AML_CRSTYPE(crs);

	switch (crsidx) {
	case 0:
		if (type != SR_IOPORT) {
			printf("%s: Unexpected resource #%d type %d\n",
			    DEVNAME(sc), crsidx, type);
			break;
		}
		sc->sc_data_bt = sc->sc_acpi->sc_iot;
		sc->sc_ec_data = crs->sr_ioport._max;
		break;
	case 1:
		if (type != SR_IOPORT) {
			printf("%s: Unexpected resource #%d type %d\n",
			    DEVNAME(sc), crsidx, type);
			break;
		}
		sc->sc_cmd_bt = sc->sc_acpi->sc_iot;
		sc->sc_ec_sc = crs->sr_ioport._max;
		break;
	case 2:
		if (!sc->sc_acpi->sc_hw_reduced) {
			printf("%s: Not running on HW-Reduced ACPI type %d\n",
			    DEVNAME(sc), type);
			break;
		}
		/* XXX: handle SCI GPIO  */
		break;
	default:
		printf("%s: invalid resource #%d type %d\n",
		    DEVNAME(sc), crsidx, type);
	}

	return 0;
}

int
acpiec_getcrs(struct acpiec_softc *sc, struct acpi_attach_args *aa)
{
	struct aml_value	res;
	int64_t			gpe;
	struct acpi_ecdt	*ecdt = aa->aaa_table;
	int			rc;

	/* Check if this is ECDT initialization */
	if (ecdt) {
		/* Get GPE, Data and Control segments */
		sc->sc_gpe = ecdt->gpe_bit;

		if (ecdt->ec_control.address_space_id == GAS_SYSTEM_IOSPACE)
			sc->sc_cmd_bt = sc->sc_acpi->sc_iot;
		else
			sc->sc_cmd_bt = sc->sc_acpi->sc_memt;
		sc->sc_ec_sc = ecdt->ec_control.address;

		if (ecdt->ec_data.address_space_id == GAS_SYSTEM_IOSPACE)
			sc->sc_data_bt = sc->sc_acpi->sc_iot;
		else
			sc->sc_data_bt = sc->sc_acpi->sc_memt;
		sc->sc_ec_data = ecdt->ec_data.address;

		/* Get devnode from header */
		sc->sc_devnode = aml_searchname(sc->sc_acpi->sc_root,
		    ecdt->ec_id);

		goto ecdtdone;
	}

	rc = aml_evalinteger(sc->sc_acpi, sc->sc_devnode,
	    "_GPE", 0, NULL, &gpe);
	if (rc) {
		dnprintf(10, "%s: no _GPE\n", DEVNAME(sc));
		return (1);
	}

	sc->sc_gpe = gpe;

	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, "_CRS", 0, NULL, &res)) {
		dnprintf(10, "%s: no _CRS\n", DEVNAME(sc));
		return (1);
	}

	/* Parse CRS to get control and data registers */

	if (res.type != AML_OBJTYPE_BUFFER) {
		dnprintf(10, "%s: unknown _CRS type %d\n",
		    DEVNAME(sc), res.type);
		aml_freevalue(&res);
		return (1);
	}

	aml_parse_resource(&res, acpiec_parse_resources, sc);
	aml_freevalue(&res);
	if (sc->sc_ec_data == 0 || sc->sc_ec_sc == 0) {
		printf("%s: failed to read from _CRS\n", DEVNAME(sc));
		return (1);
	}

ecdtdone:

	dnprintf(10, "%s: Data: 0x%lx, S/C: 0x%lx\n",
	    DEVNAME(sc), sc->sc_ec_data, sc->sc_ec_sc);

	if (bus_space_map(sc->sc_cmd_bt, sc->sc_ec_sc, 1, 0, &sc->sc_cmd_bh)) {
		dnprintf(10, "%s: failed to map S/C reg.\n", DEVNAME(sc));
		return (1);
	}

	rc = bus_space_map(sc->sc_data_bt, sc->sc_ec_data, 1, 0,
	    &sc->sc_data_bh);
	if (rc) {
		dnprintf(10, "%s: failed to map DATA reg.\n", DEVNAME(sc));
		bus_space_unmap(sc->sc_cmd_bt, sc->sc_cmd_bh, 1);
		return (1);
	}

	return (0);
}

int
acpiec_reg(struct acpiec_softc *sc)
{
	struct aml_value arg[2];
	struct aml_node *node;

	memset(&arg, 0, sizeof(arg));
	arg[0].type = AML_OBJTYPE_INTEGER;
	arg[0].v_integer = ACPI_OPREG_EC;
	arg[1].type = AML_OBJTYPE_INTEGER;
	arg[1].v_integer = 1;

	node = aml_searchname(sc->sc_devnode, "_REG");
	if (node && aml_evalnode(sc->sc_acpi, node, 2, arg, NULL)) {
		dnprintf(10, "%s: eval method _REG failed\n", DEVNAME(sc));
		printf("acpiec _REG failed, broken BIOS\n");
	}

	return (0);
}
