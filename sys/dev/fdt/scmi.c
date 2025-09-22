/*	$OpenBSD: scmi.c,v 1.4 2025/07/31 10:23:20 tobhe Exp $	*/

/*
 * Copyright (c) 2023 Mark Kettenis <kettenis@openbsd.org>
 * Copyright (c) 2024 Tobias Heider <tobhe@openbsd.org>
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
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sensors.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#include <dev/fdt/pscivar.h>

struct scmi_shmem {
	uint32_t reserved1;
	uint32_t channel_status;
#define SCMI_CHANNEL_ERROR		(1 << 1)
#define SCMI_CHANNEL_FREE		(1 << 0)
	uint32_t reserved2;
	uint32_t reserved3;
	uint32_t channel_flags;
	uint32_t length;
	uint32_t message_header;
	uint32_t message_payload[];
};

#define SCMI_SUCCESS		0
#define SCMI_NOT_SUPPORTED	-1
#define SCMI_BUSY		-6
#define SCMI_COMMS_ERROR	-7

/* Protocols */
#define SCMI_BASE		0x10
#define SCMI_PERF		0x13
#define SCMI_CLOCK		0x14

/* Common messages */
#define SCMI_PROTOCOL_VERSION			0x0
#define SCMI_PROTOCOL_ATTRIBUTES		0x1
#define SCMI_PROTOCOL_MESSAGE_ATTRIBUTES	0x2

/* Clock management messages */
#define SCMI_CLOCK_ATTRIBUTES			0x3
#define SCMI_CLOCK_DESCRIBE_RATES		0x4
#define SCMI_CLOCK_RATE_SET			0x5
#define SCMI_CLOCK_RATE_GET			0x6
#define SCMI_CLOCK_CONFIG_SET			0x7
#define  SCMI_CLOCK_CONFIG_SET_ENABLE		(1 << 0)

/* Performance management messages */
#define SCMI_PERF_DOMAIN_ATTRIBUTES		0x3
#define SCMI_PERF_DESCRIBE_LEVELS		0x4
#define SCMI_PERF_LEVEL_SET			0x7
#define SCMI_PERF_LEVEL_GET			0x8

struct scmi_resp_perf_describe_levels_40 {
	uint16_t pl_nret;
	uint16_t pl_nrem;
	struct {
		uint32_t	pe_perf;
		uint32_t	pe_cost;
		uint16_t	pe_latency;
		uint16_t	pe_reserved;
		uint32_t	pe_ifreq;
		uint32_t	pe_lindex;
	} pl_entry[];
};

static inline void
scmi_message_header(volatile struct scmi_shmem *shmem,
    uint32_t protocol_id, uint32_t message_id)
{
	shmem->message_header = (protocol_id << 10) | (message_id << 0);
}

struct scmi_perf_level {
	uint32_t	pl_perf;
	uint32_t	pl_cost;
	uint32_t	pl_ifreq;
};

struct scmi_perf_domain {
	size_t				pd_nlevels;
	uint32_t			pd_perf_max;
	uint32_t			pd_perf_min;
	struct scmi_perf_level		*pd_levels;
	int				pd_curlevel;
};

struct scmi_softc {
	struct device			sc_dev;
	bus_space_tag_t			sc_iot;
	int				sc_node;

	bus_space_handle_t		sc_ioh_tx;
	bus_space_handle_t		sc_ioh_rx;
	volatile struct scmi_shmem	*sc_shmem_tx;
	volatile struct scmi_shmem	*sc_shmem_rx;

	uint32_t			sc_smc_id;
	struct mbox_channel		*sc_mc_tx;
	struct mbox_channel		*sc_mc_rx;

	uint16_t			sc_ver_major;
	uint16_t			sc_ver_minor;

	/* SCMI_CLOCK */
	struct clock_device		sc_cd;

	/* SCMI_PERF */
	int				sc_perf_power_unit;
#define SCMI_POWER_UNIT_UW	0x2
#define SCMI_POWER_UNIT_MW	0x1
#define SCMI_POWER_UNIT_NONE	0x0
	size_t				sc_perf_ndomains;
	struct scmi_perf_domain		*sc_perf_domains;

	struct ksensordev		sc_perf_sensordev;
	struct ksensordev		sc_perf_psensordev;
	struct ksensor			*sc_perf_fsensors;
	struct ksensor			*sc_perf_psensors;

	int32_t				(*sc_command)(struct scmi_softc *);
};

int	scmi_match(struct device *, void *, void *);
void	scmi_attach(struct device *, struct device *, void *);
int	scmi_attach_smc(struct scmi_softc *, struct fdt_attach_args *);
void	scmi_attach_mbox_deferred(struct device *);

const struct cfattach scmi_ca = {
	sizeof(struct scmi_softc), scmi_match, scmi_attach
};

struct cfdriver scmi_cd = {
	NULL, "scmi", DV_DULL
};

void	scmi_attach_proto(struct scmi_softc *, int);
void	scmi_attach_clock(struct scmi_softc *, int);
void	scmi_attach_perf(struct scmi_softc *, int);
void	scmi_attach_sensors(struct scmi_softc *, int);

int32_t	scmi_smc_command(struct scmi_softc *);
int32_t	scmi_mbox_command(struct scmi_softc *);

int
scmi_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "arm,scmi-smc") ||
	    OF_is_compatible(faa->fa_node, "arm,scmi");
}

void
scmi_attach(struct device *parent, struct device *self, void *aux)
{
	struct scmi_softc *sc = (struct scmi_softc *)self;
	struct fdt_attach_args *faa = aux;

	sc->sc_iot = faa->fa_iot;
	sc->sc_node = faa->fa_node;

	if (OF_is_compatible(faa->fa_node, "arm,scmi-smc")) {
		scmi_attach_smc(sc, faa);
	} else if (OF_is_compatible(faa->fa_node, "arm,scmi")) {
		printf("\n");
		/* Defer because we need the mailbox driver attached first */
		config_defer(self, scmi_attach_mbox_deferred);
	}
}

int
scmi_attach_smc(struct scmi_softc *sc, struct fdt_attach_args *faa)
{
	volatile struct scmi_shmem *shmem;
	struct fdt_reg reg;
	int32_t status;
	uint32_t version;
	uint32_t phandle;
	void *node;
	int proto;

	sc->sc_smc_id = OF_getpropint(faa->fa_node, "arm,smc-id", 0);
	if (sc->sc_smc_id == 0) {
		printf(": no SMC id\n");
		return -1;
	}

	phandle = OF_getpropint(faa->fa_node, "shmem", 0);
	node = fdt_find_phandle(phandle);
	if (node == NULL || !fdt_is_compatible(node, "arm,scmi-shmem") ||
	    fdt_get_reg(node, 0, &reg)) {
		printf(": no shared memory\n");
		return -1;
	}

	if (bus_space_map(sc->sc_iot, reg.addr,
	    reg.size, 0, &sc->sc_ioh_tx)) {
		printf(": can't map shared memory\n");
		return -1;
	}
	sc->sc_shmem_tx = bus_space_vaddr(sc->sc_iot, sc->sc_ioh_tx);
	shmem = sc->sc_shmem_tx;

	sc->sc_command = scmi_smc_command;

	if ((shmem->channel_status & SCMI_CHANNEL_FREE) == 0) {
		printf(": channel busy\n");
		return -1;
	}

	scmi_message_header(shmem, SCMI_BASE, SCMI_PROTOCOL_VERSION);
	shmem->length = sizeof(uint32_t);
	status = sc->sc_command(sc);
	if (status != SCMI_SUCCESS) {
		printf(": protocol version command failed\n");
		return -1;
	}

	version = shmem->message_payload[1];
	sc->sc_ver_major = version >> 16;
	sc->sc_ver_minor = version & 0xfffff;
	printf(": SCMI %d.%d\n", sc->sc_ver_major, sc->sc_ver_minor);

	for (proto = OF_child(faa->fa_node); proto; proto = OF_peer(proto))
		scmi_attach_proto(sc, proto);

	return 0;
}

void
scmi_attach_mbox_deferred(struct device *self)
{
	struct scmi_softc *sc = (struct scmi_softc *)self;
	uint32_t *shmems;
	int32_t status;
	uint32_t version;
	struct fdt_reg reg;
	int len;
	void *node;
	int proto;

	/* we only support the 2 mbox / 2 shmem case */
	len = OF_getproplen(sc->sc_node, "mboxes");
	if (len != 4 * sizeof(uint32_t)) {
		printf("%s: invalid number of mboxes\n", sc->sc_dev.dv_xname);
		return;
	}

	len = OF_getproplen(sc->sc_node, "shmem");
	if (len != 2 * sizeof(uint32_t)) {
		printf("%s: invalid number of shmems\n", sc->sc_dev.dv_xname);
		return;
	}

	shmems = malloc(len, M_DEVBUF, M_WAITOK);
	OF_getpropintarray(sc->sc_node, "shmem", shmems, len);

	sc->sc_mc_tx = mbox_channel(sc->sc_node, "tx", NULL);
	if (sc->sc_mc_tx == NULL) {
		printf("%s: no tx mbox\n", sc->sc_dev.dv_xname);
		return;
	}
	sc->sc_mc_rx = mbox_channel(sc->sc_node, "rx", NULL);
	if (sc->sc_mc_rx == NULL) {
		printf("%s: no rx mbox\n", sc->sc_dev.dv_xname);
		return;
	}

	node = fdt_find_phandle(shmems[0]);
	if (node == NULL || !fdt_is_compatible(node, "arm,scmi-shmem") ||
	    fdt_get_reg(node, 0, &reg)) {
		printf("%s: no shared memory\n", sc->sc_dev.dv_xname);
		return;
	}
	if (bus_space_map(sc->sc_iot, reg.addr, reg.size, 0, &sc->sc_ioh_tx)) {
		printf("%s: can't map shared memory\n", sc->sc_dev.dv_xname);
		return;
	}
	sc->sc_shmem_tx = bus_space_vaddr(sc->sc_iot, sc->sc_ioh_tx);

	node = fdt_find_phandle(shmems[1]);
	if (node == NULL || !fdt_is_compatible(node, "arm,scmi-shmem") ||
	    fdt_get_reg(node, 0, &reg)) {
		printf("%s: no shared memory\n", sc->sc_dev.dv_xname);
		return;
	}
	if (bus_space_map(sc->sc_iot, reg.addr, reg.size, 0, &sc->sc_ioh_rx)) {
		printf("%s: can't map shared memory\n", sc->sc_dev.dv_xname);
		return;
	}
	sc->sc_shmem_rx = bus_space_vaddr(sc->sc_iot, sc->sc_ioh_rx);

	sc->sc_command = scmi_mbox_command;

	scmi_message_header(sc->sc_shmem_tx, SCMI_BASE, SCMI_PROTOCOL_VERSION);
	sc->sc_shmem_tx->length = sizeof(uint32_t);
	status = sc->sc_command(sc);
	if (status != SCMI_SUCCESS) {
		printf("%s: protocol version command failed\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	version = sc->sc_shmem_tx->message_payload[1];
	sc->sc_ver_major = version >> 16;
	sc->sc_ver_minor = version & 0xfffff;
	printf("%s: SCMI %d.%d\n", sc->sc_dev.dv_xname, sc->sc_ver_major,
	    sc->sc_ver_minor);

	for (proto = OF_child(sc->sc_node); proto; proto = OF_peer(proto))
		scmi_attach_proto(sc, proto);
}

int32_t
scmi_smc_command(struct scmi_softc *sc)
{
	volatile struct scmi_shmem *shmem = sc->sc_shmem_tx;
	int32_t status;

	shmem->channel_status = 0;
	status = smccc(sc->sc_smc_id, 0, 0, 0);
	if (status != PSCI_SUCCESS)
		return SCMI_NOT_SUPPORTED;
	if ((shmem->channel_status & SCMI_CHANNEL_ERROR))
		return SCMI_COMMS_ERROR;
	if ((shmem->channel_status & SCMI_CHANNEL_FREE) == 0)
		return SCMI_BUSY;
	return shmem->message_payload[0];
}

int32_t
scmi_mbox_command(struct scmi_softc *sc)
{
	volatile struct scmi_shmem *shmem = sc->sc_shmem_tx;
	int ret;
	int i;

	shmem->channel_status = 0;
	ret = mbox_send(sc->sc_mc_tx, NULL, 0);
	if (ret != 0)
		return SCMI_NOT_SUPPORTED; 

	/* XXX: poll for now */
	for (i = 0; i < 20; i++) {
		if (shmem->channel_status & SCMI_CHANNEL_FREE)
			break;
		delay(10);
	}
	if ((shmem->channel_status & SCMI_CHANNEL_ERROR))
		return SCMI_COMMS_ERROR;
	if ((shmem->channel_status & SCMI_CHANNEL_FREE) == 0)
		return SCMI_BUSY;

	return shmem->message_payload[0];
}

void
scmi_attach_proto(struct scmi_softc *sc, int node)
{
	switch (OF_getpropint(node, "reg", -1)) {
	case SCMI_CLOCK:
		scmi_attach_clock(sc, node);
		break;
	case SCMI_PERF:
		scmi_attach_perf(sc, node);
		break;
	default:
		break;
	}
}

/* Clock management. */

void	scmi_clock_enable(void *, uint32_t *, int);
uint32_t scmi_clock_get_frequency(void *, uint32_t *);
int	scmi_clock_set_frequency(void *, uint32_t *, uint32_t);

void
scmi_attach_clock(struct scmi_softc *sc, int node)
{
	volatile struct scmi_shmem *shmem = sc->sc_shmem_tx;
	int32_t status;
	int nclocks;

	scmi_message_header(shmem, SCMI_CLOCK, SCMI_PROTOCOL_ATTRIBUTES);
	shmem->length = sizeof(uint32_t);
	status = sc->sc_command(sc);
	if (status != SCMI_SUCCESS)
		return;

	nclocks = shmem->message_payload[1] & 0xffff;
	if (nclocks == 0)
		return;

	sc->sc_cd.cd_node = node;
	sc->sc_cd.cd_cookie = sc;
	sc->sc_cd.cd_enable = scmi_clock_enable;
	sc->sc_cd.cd_get_frequency = scmi_clock_get_frequency;
	sc->sc_cd.cd_set_frequency = scmi_clock_set_frequency;
	clock_register(&sc->sc_cd);
}

void
scmi_clock_enable(void *cookie, uint32_t *cells, int on)
{
	struct scmi_softc *sc = cookie;
	volatile struct scmi_shmem *shmem = sc->sc_shmem_tx;
	uint32_t idx = cells[0];

	scmi_message_header(shmem, SCMI_CLOCK, SCMI_CLOCK_CONFIG_SET);
	shmem->length = 3 * sizeof(uint32_t);
	shmem->message_payload[0] = idx;
	shmem->message_payload[1] = on ? SCMI_CLOCK_CONFIG_SET_ENABLE : 0;
	sc->sc_command(sc);
}

uint32_t
scmi_clock_get_frequency(void *cookie, uint32_t *cells)
{
	struct scmi_softc *sc = cookie;
	volatile struct scmi_shmem *shmem = sc->sc_shmem_tx;
	uint32_t idx = cells[0];
	int32_t status;

	scmi_message_header(shmem, SCMI_CLOCK, SCMI_CLOCK_RATE_GET);
	shmem->length = 2 * sizeof(uint32_t);
	shmem->message_payload[0] = idx;
	status = sc->sc_command(sc);
	if (status != SCMI_SUCCESS)
		return 0;
	if (shmem->message_payload[2] != 0)
		return 0;

	return shmem->message_payload[1];
}

int
scmi_clock_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	struct scmi_softc *sc = cookie;
	volatile struct scmi_shmem *shmem = sc->sc_shmem_tx;
	uint32_t idx = cells[0];
	int32_t status;

	scmi_message_header(shmem, SCMI_CLOCK, SCMI_CLOCK_RATE_SET);
	shmem->length = 5 * sizeof(uint32_t);
	shmem->message_payload[0] = 0;
	shmem->message_payload[1] = idx;
	shmem->message_payload[2] = freq;
	shmem->message_payload[3] = 0;
	status = sc->sc_command(sc);
	if (status != SCMI_SUCCESS)
		return -1;

	return 0;
}

/* Performance management */
void	scmi_perf_descr_levels(struct scmi_softc *, int);
int	scmi_perf_level_get(struct scmi_softc *, int);
int	scmi_perf_level_set(struct scmi_softc *, int, int);
void	scmi_perf_refresh_sensor(void *);

int	scmi_perf_cpuspeed(int *);
void	scmi_perf_cpusetperf(int);

void
scmi_attach_perf(struct scmi_softc *sc, int node)
{
	volatile struct scmi_shmem *shmem = sc->sc_shmem_tx;
	struct scmi_perf_domain *pd;
	int32_t status;
	uint32_t version;
	int i;

	scmi_message_header(sc->sc_shmem_tx, SCMI_PERF, SCMI_PROTOCOL_VERSION);
	sc->sc_shmem_tx->length = sizeof(uint32_t);
	status = sc->sc_command(sc);
	if (status != SCMI_SUCCESS) {
		printf("%s: SCMI_PROTOCOL_VERSION failed\n",
		    sc->sc_dev.dv_xname);	
		return;
	}

	version = shmem->message_payload[1];
	if (version != 0x40000) {
		printf("%s: invalid perf protocol version (0x%x != 0x4000)",
		    sc->sc_dev.dv_xname, version);	
		return;
	}

	scmi_message_header(shmem, SCMI_PERF, SCMI_PROTOCOL_ATTRIBUTES);
	shmem->length = sizeof(uint32_t);
	status = sc->sc_command(sc);
	if (status != SCMI_SUCCESS) {
		printf("%s: SCMI_PROTOCOL_ATTRIBUTES failed\n",
		    sc->sc_dev.dv_xname);	
		return;
	}

	sc->sc_perf_ndomains = shmem->message_payload[1] & 0xffff;
	sc->sc_perf_domains = malloc(sc->sc_perf_ndomains *
	    sizeof(struct scmi_perf_domain), M_DEVBUF, M_ZERO | M_WAITOK);
	sc->sc_perf_power_unit = (shmem->message_payload[1] >> 16) & 0x3;

	strlcpy(sc->sc_perf_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_perf_sensordev.xname));
	
	sc->sc_perf_fsensors =
	    malloc(sc->sc_perf_ndomains * sizeof(struct ksensor),
	    M_DEVBUF, M_ZERO | M_WAITOK);
	sc->sc_perf_psensors =
	    malloc(sc->sc_perf_ndomains * sizeof(struct ksensor),
	    M_DEVBUF, M_ZERO | M_WAITOK);

	/* Add one frequency sensor per perf domain */
	for (i = 0; i < sc->sc_perf_ndomains; i++) {
		scmi_message_header(shmem, SCMI_PERF,
		    SCMI_PERF_DOMAIN_ATTRIBUTES);
		shmem->length = 2 * sizeof(uint32_t);
		shmem->message_payload[0] = i;
		status = sc->sc_command(sc);
		if (status != SCMI_SUCCESS) {
			printf("%s: SCMI_PERF_DOMAIN_ATTRIBUTES failed\n",
			    sc->sc_dev.dv_xname);	
			goto err;
		}

		scmi_perf_descr_levels(sc, i);

		sc->sc_perf_fsensors[i].type = SENSOR_FREQ;
		sensor_attach(&sc->sc_perf_sensordev, &sc->sc_perf_fsensors[i]);
		sc->sc_perf_psensors[i].type = SENSOR_WATTS;
		sensor_attach(&sc->sc_perf_sensordev, &sc->sc_perf_psensors[i]);

		pd = &sc->sc_perf_domains[i];
		scmi_perf_level_set(sc, i, pd->pd_nlevels - 1);
	}
	sensordev_install(&sc->sc_perf_sensordev);
	sensor_task_register(sc, scmi_perf_refresh_sensor, 1);

	cpu_setperf = scmi_perf_cpusetperf;
	cpu_cpuspeed = scmi_perf_cpuspeed;

	return;
err:
	free(sc->sc_perf_fsensors, M_DEVBUF,
	    sc->sc_perf_ndomains * sizeof(struct ksensor));
	free(sc->sc_perf_psensors, M_DEVBUF,
	    sc->sc_perf_ndomains * sizeof(struct ksensor));
}

void
scmi_perf_descr_levels(struct scmi_softc *sc, int domain)
{
	volatile struct scmi_shmem *shmem = sc->sc_shmem_tx;
	volatile struct scmi_resp_perf_describe_levels_40 *pl;
	struct scmi_perf_domain *pd = &sc->sc_perf_domains[domain];
	int status, i, idx;

	idx = 0;
	do {
		scmi_message_header(shmem, SCMI_PERF,
		    SCMI_PERF_DESCRIBE_LEVELS);
		shmem->length = sizeof(uint32_t) * 3;
		shmem->message_payload[0] = domain;
		shmem->message_payload[1] = idx;
		status = sc->sc_command(sc);
		if (status != SCMI_SUCCESS) {
			printf("%s: SCMI_PERF_DESCRIBE_LEVELS failed\n",
			    sc->sc_dev.dv_xname);
			return;
		}

		pl = (struct scmi_resp_perf_describe_levels_40 *)
		    &shmem->message_payload[1];

		if (pd->pd_levels == NULL) {
			pd->pd_nlevels = pl->pl_nret + pl->pl_nrem;
			pd->pd_levels = malloc(pd->pd_nlevels *
			    sizeof(struct scmi_perf_level),
			    M_DEVBUF, M_ZERO | M_WAITOK);

			pd->pd_perf_min = UINT32_MAX;
			pd->pd_perf_max = 0;
		}

		for (i = 0; i < pl->pl_nret; i++) {
			if (pl->pl_entry[i].pe_perf < pd->pd_perf_min)
				pd->pd_perf_min = pl->pl_entry[i].pe_perf;
			if (pl->pl_entry[i].pe_perf > pd->pd_perf_max)
				pd->pd_perf_max = pl->pl_entry[i].pe_perf;

			pd->pd_levels[idx + i].pl_cost =
			    pl->pl_entry[i].pe_cost;
			pd->pd_levels[idx + i].pl_perf =
			    pl->pl_entry[i].pe_perf;
			pd->pd_levels[idx + i].pl_ifreq =
			    pl->pl_entry[i].pe_ifreq;
		}
		idx += pl->pl_nret;
	} while (pl->pl_nrem);
}

int
scmi_perf_level_get(struct scmi_softc *sc, int domain)
{
	volatile struct scmi_shmem *shmem = sc->sc_shmem_tx;
	int32_t status;

	scmi_message_header(shmem, SCMI_PERF,
	    SCMI_PERF_LEVEL_GET);
	shmem->length = sizeof(uint32_t) * 2;
	shmem->message_payload[0] = domain;
	status = sc->sc_command(sc);
	if (status != SCMI_SUCCESS) {
		printf("%s: SCMI_PERF_LEVEL_GET failed\n",
		    sc->sc_dev.dv_xname);
		return -1;
	}
	return shmem->message_payload[1];
}

int
scmi_perf_level_set(struct scmi_softc *sc, int domain, int level)
{
	volatile struct scmi_shmem *shmem = sc->sc_shmem_tx;
	int32_t status;

	scmi_message_header(shmem, SCMI_PERF,
	    SCMI_PERF_LEVEL_SET);
	shmem->length = sizeof(uint32_t) * 3;
	shmem->message_payload[0] = domain;
	shmem->message_payload[1] = level;
	status = sc->sc_command(sc);
	if (status != SCMI_SUCCESS) {
		printf("%s: SCMI_PERF_LEVEL_SET failed\n",
		    sc->sc_dev.dv_xname);
		return -1;
	}
	return 0;
}

int
scmi_perf_cpuspeed(int *freq)
{
	struct scmi_softc *sc;
	int i, level = -1;
	uint64_t opp_hz = 0;

        for (i = 0; i < scmi_cd.cd_ndevs; i++) {
                sc = scmi_cd.cd_devs[i];
                if (sc == NULL)
                        continue;

		if (sc->sc_perf_domains == NULL)
			continue;

		for (i = 0; i < sc->sc_perf_ndomains; i++) {
			if (sc->sc_perf_domains[i].pd_levels == NULL)
				return EINVAL;

			level = scmi_perf_level_get(sc, i);
			opp_hz = MAX(opp_hz, (uint64_t)sc->sc_perf_domains[i].
			    pd_levels[level].pl_ifreq * 1000);
		}
	}

	if (opp_hz == 0)
		return EINVAL;

	*freq = opp_hz / 1000000;
	return 0;
}

void
scmi_perf_cpusetperf(int level)
{
	struct scmi_softc *sc;
	struct scmi_perf_domain *d;
	uint64_t min, max, perf;
	int i, j;

        for (i = 0; i < scmi_cd.cd_ndevs; i++) {
                sc = scmi_cd.cd_devs[i];
                if (sc == NULL)
			return;
		if (sc->sc_perf_domains == NULL)
			continue;

		for (i = 0; i < sc->sc_perf_ndomains; i++) {
			d = &sc->sc_perf_domains[i];
			if (d->pd_nlevels == 0)
				continue;

			/*
			 * Map the performance level onto SCMI perf scale
			 */
			min = d->pd_perf_min;
			max = d->pd_perf_max;
			perf = min + (level * (max - min)) / 100;

			/*
			 * Find best matching level
			 */
			for (j = 0; j < d->pd_nlevels - 1; j++)
				if (d->pd_levels[j + 1].pl_perf > perf)
					break;

			scmi_perf_level_set(sc, i, j);
		}
	}
}

void
scmi_perf_refresh_sensor(void *arg)
{
	struct scmi_softc *sc = arg;
	uint64_t power_cost;
	int level, i;

	if (sc->sc_perf_domains == NULL)
		return;

	for (i = 0; i < sc->sc_perf_ndomains; i++) {
		if (sc->sc_perf_domains[i].pd_levels == NULL)
			return;

		level = scmi_perf_level_get(sc, i);
		if (level == -1 ||
		    sc->sc_perf_fsensors == NULL ||
		    sc->sc_perf_psensors == NULL)
			return;

		sc->sc_perf_domains[i].pd_curlevel = level;
		sc->sc_perf_fsensors[i].value =
		    (uint64_t)sc->sc_perf_domains[i].
		    pd_levels[level].pl_ifreq * 1000000000;

		switch (sc->sc_perf_power_unit) {
		case SCMI_POWER_UNIT_UW:
			power_cost = (uint64_t)sc->sc_perf_domains[i].
			    pd_levels[level].pl_cost;
			break;
		case SCMI_POWER_UNIT_MW:
			power_cost = (uint64_t)sc->sc_perf_domains[i].
			    pd_levels[level].pl_cost * 1000;
			break;
		default:
			continue;
		}
		sc->sc_perf_psensors[i].value = power_cost;
	}
}
