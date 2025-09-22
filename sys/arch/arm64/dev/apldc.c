/*	$OpenBSD: apldc.c,v 1.12 2024/01/20 08:00:59 kettenis Exp $	*/
/*
 * Copyright (c) 2022 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/evcount.h>
#include <sys/malloc.h>
#include <sys/task.h>
#include <sys/timeout.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/fdt.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wskbdvar.h>
#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/wsmousevar.h>

#include <dev/hid/hid.h>
#include <dev/hid/hidkbdsc.h>
#include <dev/hid/hidmsvar.h>

#include <arm64/dev/rtkit.h>
#include <machine/simplebusvar.h>

#include "apldc.h"

#define DC_IRQ_MASK		0x0000
#define DC_IRQ_STAT		0x0004

#define DC_CONFIG_TX_THRESH	0x0000
#define DC_CONFIG_RX_THRESH	0x0004

#define DC_DATA_TX8		0x0004
#define DC_DATA_TX32		0x0010
#define DC_DATA_TX_FREE		0x0014
#define DC_DATA_RX8		0x001c
#define  DC_DATA_RX8_COUNT(d)	((d) & 0x7f)
#define  DC_DATA_RX8_DATA(d)	(((d) >> 8) & 0xff)
#define DC_DATA_RX32		0x0028
#define DC_DATA_RX_COUNT	0x002c

#define APLDC_MAX_INTR		32

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct apldchidev_attach_args {
	const char *aa_name;
	void	*aa_desc;
	size_t	aa_desclen;
};

struct intrhand {
	int (*ih_func)(void *);
	void *ih_arg;
	int ih_ipl;
	int ih_irq;
	int ih_level;
	struct evcount ih_count;
	char *ih_name;
	void *ih_sc;
};

struct apldc_softc {
	struct simplebus_softc	sc_sbus;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	void			*sc_ih;
	struct intrhand		*sc_handlers[APLDC_MAX_INTR];
	struct interrupt_controller sc_ic;
};

int	apldc_match(struct device *, void *, void *);
void	apldc_attach(struct device *, struct device *, void *);

const struct cfattach apldc_ca = {
	sizeof (struct apldc_softc), apldc_match, apldc_attach
};

struct cfdriver apldc_cd = {
	NULL, "apldc", DV_DULL
};

int	apldc_intr(void *);
void	*apldc_intr_establish(void *, int *, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	apldc_intr_enable(void *);
void	apldc_intr_disable(void *);
void	apldc_intr_barrier(void *);

int
apldc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "apple,dockchannel");
}

void
apldc_attach(struct device *parent, struct device *self, void *aux)
{
	struct apldc_softc *sc = (struct apldc_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	/* Disable and clear all interrupts. */
	HWRITE4(sc, DC_IRQ_MASK, 0);
	HWRITE4(sc, DC_IRQ_STAT, 0xffffffff);

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_TTY,
	    apldc_intr, sc, sc->sc_sbus.sc_dev.dv_xname);

	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_cookie = sc;
	sc->sc_ic.ic_establish = apldc_intr_establish;
	sc->sc_ic.ic_enable = apldc_intr_enable;
	sc->sc_ic.ic_disable = apldc_intr_disable;
	sc->sc_ic.ic_barrier = apldc_intr_barrier;
	fdt_intr_register(&sc->sc_ic);

	simplebus_attach(parent, &sc->sc_sbus.sc_dev, faa);
}

int
apldc_intr(void *arg)
{
	struct apldc_softc *sc = arg;
	struct intrhand *ih;
	uint32_t stat, pending;
	int irq, s;

	stat = HREAD4(sc, DC_IRQ_STAT);

	pending = stat;
	while (pending) {
		irq = ffs(pending) - 1;
		ih = sc->sc_handlers[irq];
		if (ih) {
			s = splraise(ih->ih_ipl);
			if (ih->ih_func(ih->ih_arg))
				ih->ih_count.ec_count++;
			splx(s);
		}

		pending &= ~(1 << irq);
	}

	HWRITE4(sc, DC_IRQ_STAT, stat);

	return 1;
}

void *
apldc_intr_establish(void *cookie, int *cells, int ipl,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct apldc_softc *sc = cookie;
	struct intrhand *ih;
	int irq = cells[0];
	int level = cells[1];

	if (irq < 0 || irq >= APLDC_MAX_INTR)
		return NULL;

	if (ipl != IPL_TTY)
		return NULL;

	if (ci != NULL && !CPU_IS_PRIMARY(ci))
		return NULL;

	if (sc->sc_handlers[irq])
		return NULL;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_ipl = ipl & IPL_IRQMASK;
	ih->ih_irq = irq;
	ih->ih_name = name;
	ih->ih_level = level;
	ih->ih_sc = sc;

	sc->sc_handlers[irq] = ih;

	if (name != NULL)
		evcount_attach(&ih->ih_count, name, &ih->ih_irq);

	return ih;
}

void
apldc_intr_enable(void *cookie)
{
	struct intrhand	*ih = cookie;
	struct apldc_softc *sc = ih->ih_sc;

	HSET4(sc, DC_IRQ_MASK, 1 << ih->ih_irq);
}

void
apldc_intr_disable(void *cookie)
{
	struct intrhand	*ih = cookie;
	struct apldc_softc *sc = ih->ih_sc;

	HCLR4(sc, DC_IRQ_MASK, 1 << ih->ih_irq);
}

void
apldc_intr_barrier(void *cookie)
{
	struct intrhand	*ih = cookie;
	struct apldc_softc *sc = ih->ih_sc;

	intr_barrier(sc->sc_ih);
}

#define APLDCHIDEV_DESC_MAX	512
#define APLDCHIDEV_PKT_MAX	1024
#define APLDCHIDEV_GPIO_MAX	4

#define APLDCHIDEV_NUM_GPIOS	16

struct apldchidev_gpio {
	struct apldchidev_softc	*ag_sc;
	uint8_t			ag_id;
	uint8_t			ag_iface;
	uint32_t		ag_gpio[APLDCHIDEV_GPIO_MAX];
	struct task		ag_task;
};

struct apldchidev_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_cfg_ioh;
	bus_space_handle_t	sc_data_ioh;

	bus_dma_tag_t		sc_dmat;
	int			sc_node;

	void			*sc_rx_ih;

	uint8_t			sc_seq_comm;

	uint8_t			sc_iface_stm;
	uint8_t			sc_seq_stm;
	uint8_t			sc_stmdesc[APLDCHIDEV_DESC_MAX];
	size_t			sc_stmdesclen;
	int			sc_stm_ready;

	uint8_t			sc_iface_kbd;
	uint8_t			sc_seq_kbd;
	struct device		*sc_kbd;
	uint8_t			sc_kbddesc[APLDCHIDEV_DESC_MAX];
	size_t			sc_kbddesclen;
	int			sc_kbd_ready;

	uint8_t			sc_iface_mt;
	uint8_t			sc_seq_mt;
	struct device		*sc_mt;
	uint8_t			sc_mtdesc[APLDCHIDEV_DESC_MAX];
	size_t			sc_mtdesclen;
	int			sc_mt_ready;
	int			sc_x_min;
	int			sc_x_max;
	int			sc_y_min;
	int			sc_y_max;
	int			sc_h_res;
	int			sc_v_res;

	struct apldchidev_gpio	sc_gpio[APLDCHIDEV_NUM_GPIOS];
	u_int			sc_ngpios;
	uint8_t			sc_gpio_cmd[APLDCHIDEV_PKT_MAX];
	size_t			sc_gpio_cmd_len;

	uint8_t			sc_cmd_iface;
	uint8_t			sc_cmd_seq;
	uint8_t			sc_data[APLDCHIDEV_DESC_MAX];
	size_t			sc_data_len;
	uint32_t		sc_retcode;
	int			sc_busy;
};

int	apldchidev_match(struct device *, void *, void *);
void	apldchidev_attach(struct device *, struct device *, void *);

const struct cfattach apldchidev_ca = {
	sizeof(struct apldchidev_softc), apldchidev_match, apldchidev_attach
};

struct cfdriver apldchidev_cd = {
	NULL, "apldchidev", DV_DULL
};

void	apldchidev_attachhook(struct device *);
void	apldchidev_cmd(struct apldchidev_softc *, uint8_t, uint8_t,
	    void *, size_t);
void	apldchidev_wait(struct apldchidev_softc *);
int	apldchidev_send_firmware(struct apldchidev_softc *, int,
	    void *, size_t);
void	apldchidev_enable(struct apldchidev_softc *, uint8_t);
void	apldchidev_reset(struct apldchidev_softc *, uint8_t, uint8_t);
int	apldchidev_rx_intr(void *);
void	apldchidev_gpio_task(void *);

int
apldchidev_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "apple,dockchannel-hid");
}

void
apldchidev_attach(struct device *parent, struct device *self, void *aux)
{
	struct apldchidev_softc *sc = (struct apldchidev_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct apldchidev_attach_args aa;
	uint32_t phandle;
	int error, idx, retry;

	if (faa->fa_nreg < 2) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_cfg_ioh)) {
		printf(": can't map registers\n");
		return;
	}
	if (bus_space_map(sc->sc_iot, faa->fa_reg[1].addr,
	    faa->fa_reg[1].size, 0, &sc->sc_data_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_dmat = faa->fa_dmat;
	sc->sc_node = faa->fa_node;

	idx = OF_getindex(faa->fa_node, "rx", "interrupt-names");
	if (idx < 0) {
		printf(": no rx interrupt\n");
		return;
	}
	sc->sc_rx_ih = fdt_intr_establish_idx(faa->fa_node, idx, IPL_TTY,
	    apldchidev_rx_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_rx_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}

	phandle = OF_getpropint(faa->fa_node, "apple,helper-cpu", 0);
	if (phandle) {
		error = aplrtk_start(phandle);
		if (error) {
			printf(": can't start helper CPU\n");
			return;
		}
	}

	printf("\n");

	/* Poll until we have received the STM HID descriptor. */
	for (retry = 10; retry > 0; retry--) {
		if (sc->sc_stmdesclen > 0)
			break;
		apldchidev_rx_intr(sc);
		delay(1000);
	}

	if (sc->sc_stmdesclen > 0) {
		/* Enable interface. */
		apldchidev_enable(sc, sc->sc_iface_stm);
	}

	/* Poll until we have received the keyboard HID descriptor. */
	for (retry = 10; retry > 0; retry--) {
		if (sc->sc_kbddesclen > 0)
			break;
		apldchidev_rx_intr(sc);
		delay(1000);
	}

	if (sc->sc_kbddesclen > 0) {
		/* Enable interface. */
		apldchidev_enable(sc, sc->sc_iface_kbd);

		aa.aa_name = "keyboard";
		aa.aa_desc = sc->sc_kbddesc;
		aa.aa_desclen = sc->sc_kbddesclen;
		sc->sc_kbd = config_found(self, &aa, NULL);
	}

	bus_space_write_4(sc->sc_iot, sc->sc_cfg_ioh, DC_CONFIG_RX_THRESH, 8);
	fdt_intr_enable(sc->sc_rx_ih);

#if NAPLDCMS > 0
	config_mountroot(self, apldchidev_attachhook);
#endif
}

int
apldchidev_read(struct apldchidev_softc *sc, void *buf, size_t len,
    uint32_t *checksum)
{
	uint8_t *dst = buf;
	uint32_t data;
	int shift = 0;

	while (len > 0) {
		data = bus_space_read_4(sc->sc_iot, sc->sc_data_ioh,
		    DC_DATA_RX8);
		if (DC_DATA_RX8_COUNT(data) > 0) {
			*dst++ = DC_DATA_RX8_DATA(data);
			*checksum += (DC_DATA_RX8_DATA(data) << shift);
			shift += 8;
			if (shift > 24)
				shift = 0;
			len--;
		} else {
			delay(10);
		}
	}

	return 0;
}

int
apldchidev_write(struct apldchidev_softc *sc, const void *buf, size_t len,
    uint32_t *checksum)
{
	const uint8_t *src = buf;
	uint32_t free;
	int shift = 0;

	while (len > 0) {
		free = bus_space_read_4(sc->sc_iot, sc->sc_data_ioh,
		    DC_DATA_TX_FREE);
		if (free > 0) {
			if (checksum)
				*checksum -= *src << shift;
			bus_space_write_4(sc->sc_iot, sc->sc_data_ioh,
			    DC_DATA_TX8, *src++);
			shift += 8;
			if (shift > 24)
				shift = 0;
			len--;
		} else {
			delay(10);
		}
	}

	return 0;
}

struct mtp_hdr {
	uint8_t hdr_len;
	uint8_t chan;
#define MTP_CHAN_CMD		0x11
#define MTP_CHAN_REPORT		0x12
	uint16_t pkt_len;
	uint8_t seq;
	uint8_t iface;
#define MTP_IFACE_COMM		0
	uint16_t pad;
} __packed;

struct mtp_subhdr {
	uint8_t flags;
#define MTP_GROUP_SHIFT	6
#define MTP_GROUP(x)		((x >> 6) & 0x3)
#define MTP_GROUP_INPUT	0
#define MTP_GROUP_OUTPUT	1
#define MTP_GROUP_CMD		2
#define MTP_REQ_SHIFT		0
#define MTP_REQ(x)		((x >> 0) & 0x3f)
#define MTP_REQ_SET_REPORT	0
#define MTP_REQ_GET_REPORT	1
	uint8_t unk;
	uint16_t len;
	uint32_t retcode;
} __packed;

struct mtp_init_hdr {
	uint8_t type;
#define MTP_EVENT_GPIO_CMD	0xa0
#define MTP_EVENT_INIT	0xf0
#define MTP_EVENT_READY	0xf1
	uint8_t unk1;
	uint8_t unk2;
	uint8_t iface;
	char name[16];
} __packed;

struct mtp_init_block_hdr {
	uint16_t type;
#define MTP_BLOCK_DESCRIPTOR	0
#define MTP_BLOCK_GPIO_REQ	1
#define MTP_BLOCK_END		2
	uint16_t subtype;
	uint16_t len;
} __packed;

struct mtp_gpio_req {
	uint16_t unk;
	uint16_t id;
	char name[32];
} __packed;

struct mtp_gpio_cmd {
	uint8_t type;
	uint8_t iface;
	uint8_t id;
	uint8_t unk;
	uint8_t cmd;
#define MTP_GPIO_CMD_TOGGLE	0x03
} __packed;

struct mtp_gpio_ack {
	uint8_t type;
	uint32_t retcode;
	uint8_t cmd[512];
} __packed;

struct mtp_dim {
	uint32_t width;
	uint32_t height;
	int16_t x_min;
	int16_t y_min;
	int16_t x_max;
	int16_t y_max;
};

#define MTP_CMD_RESET_INTERFACE	0x40
#define MTP_CMD_SEND_FIRMWARE		0x95
#define MTP_CMD_ENABLE_INTERFACE	0xb4
#define MTP_CMD_ACK_GPIO_CMD		0xa1
#define MTP_CMD_GET_DIMENSIONS		0xd9

void
apldchidev_handle_gpio_req(struct apldchidev_softc *sc, uint8_t iface,
    void *buf, size_t len)
{
	struct mtp_gpio_req *req = buf;
	uint32_t gpio[APLDCHIDEV_GPIO_MAX];
	char name[64];
	int node = -1;

	if (len < sizeof(*req))
		return;

	if (sc->sc_ngpios >= APLDCHIDEV_NUM_GPIOS)
		return;

	node = sc->sc_node;
	snprintf(name, sizeof(name), "apple,%s-gpios", req->name);
	len = OF_getproplen(node, name);
	if (len <= 0 || len > sizeof(gpio)) {
		/* XXX: older device trees store gpios in sub-nodes */
		if (iface == sc->sc_iface_mt)
			node = OF_getnodebyname(sc->sc_node, "multi-touch");
		else if (iface == sc->sc_iface_stm)
			node = OF_getnodebyname(sc->sc_node, "stm");
		if (node == -1)
			return;
		len = OF_getproplen(node, name);
		if (len <= 0 || len > sizeof(gpio))
			return;
	}

	OF_getpropintarray(node, name, gpio, len);
	gpio_controller_config_pin(gpio, GPIO_CONFIG_OUTPUT);
	gpio_controller_set_pin(gpio, 0);

	sc->sc_gpio[sc->sc_ngpios].ag_sc = sc;
	sc->sc_gpio[sc->sc_ngpios].ag_id = req->id;
	sc->sc_gpio[sc->sc_ngpios].ag_iface = iface;
	memcpy(sc->sc_gpio[sc->sc_ngpios].ag_gpio, gpio, len);
	task_set(&sc->sc_gpio[sc->sc_ngpios].ag_task,
	    apldchidev_gpio_task, &sc->sc_gpio[sc->sc_ngpios]);
	sc->sc_ngpios++;
}

void
apldchidev_handle_init(struct apldchidev_softc *sc, uint8_t iface,
    void *buf, size_t len)
{
	struct mtp_init_block_hdr *bhdr = buf;

	for (;;) {
		if (len < sizeof(*bhdr))
			return;
		len -= sizeof(*bhdr);

		if (len < bhdr->len)
			return;
		len -= bhdr->len;

		switch (bhdr->type) {
		case MTP_BLOCK_DESCRIPTOR:
			if (iface == sc->sc_iface_kbd &&
			    bhdr->len <= sizeof(sc->sc_kbddesc)) {
				memcpy(sc->sc_kbddesc, bhdr + 1, bhdr->len);
				sc->sc_kbddesclen = bhdr->len;
			} else if (iface == sc->sc_iface_mt &&
			    bhdr->len <= sizeof(sc->sc_mtdesc)) {
				memcpy(sc->sc_mtdesc, bhdr + 1, bhdr->len);
				sc->sc_mtdesclen = bhdr->len;
			} else if (iface == sc->sc_iface_stm &&
			    bhdr->len <= sizeof(sc->sc_stmdesc)) {
				memcpy(sc->sc_stmdesc, bhdr + 1, bhdr->len);
				sc->sc_stmdesclen = bhdr->len;
			}
			break;
		case MTP_BLOCK_GPIO_REQ:
			apldchidev_handle_gpio_req(sc, iface,
			    bhdr + 1, bhdr->len);
			break;
		case MTP_BLOCK_END:
			return;
		default:
			printf("%s: unhandled block type 0x%04x\n",
			    sc->sc_dev.dv_xname, bhdr->type);
			break;
		}

		bhdr = (struct mtp_init_block_hdr *)
		    ((uint8_t *)(bhdr + 1) + bhdr->len);
	}
}

void
apldchidev_handle_comm(struct apldchidev_softc *sc, void *buf, size_t len)
{
	struct mtp_init_hdr *ihdr = buf;
	struct mtp_gpio_cmd *cmd = buf;
	uint8_t iface;
	int i;

	switch (ihdr->type) {
	case MTP_EVENT_INIT:
		if (strcmp(ihdr->name, "keyboard") == 0) {
			sc->sc_iface_kbd = ihdr->iface;
			apldchidev_handle_init(sc, ihdr->iface,
			    ihdr + 1, len - sizeof(*ihdr));
		}
		if (strcmp(ihdr->name, "multi-touch") == 0) {
			sc->sc_iface_mt = ihdr->iface;
			apldchidev_handle_init(sc, ihdr->iface,
			    ihdr + 1, len - sizeof(*ihdr));
		}
		if (strcmp(ihdr->name, "stm") == 0) {
			sc->sc_iface_stm = ihdr->iface;
			apldchidev_handle_init(sc, ihdr->iface,
			    ihdr + 1, len - sizeof(*ihdr));
		}
		break;
	case MTP_EVENT_READY:
		iface = ihdr->unk1;
		if (iface == sc->sc_iface_stm)
			sc->sc_stm_ready = 1;
		if (iface == sc->sc_iface_kbd)
			sc->sc_kbd_ready = 1;
		if (iface == sc->sc_iface_mt)
			sc->sc_mt_ready = 1;
		break;
	case MTP_EVENT_GPIO_CMD:
		for (i =0; i < sc->sc_ngpios; i++) {
			if (cmd->id == sc->sc_gpio[i].ag_id &&
			    cmd->iface == sc->sc_gpio[i].ag_iface &&
			    cmd->cmd == MTP_GPIO_CMD_TOGGLE) {
				/* Stash the command for the reply. */
				KASSERT(len < sizeof(sc->sc_gpio_cmd));
				memcpy(sc->sc_gpio_cmd, buf, len);
				sc->sc_gpio_cmd_len = len;
				task_add(systq, &sc->sc_gpio[i].ag_task);
				return;
			}
		}
		printf("%s: unhandled gpio id %d iface %d cmd 0x%02x\n",
		       sc->sc_dev.dv_xname, cmd->id, cmd->iface, cmd->cmd);
		break;
	default:
		printf("%s: unhandled comm event 0x%02x\n",
		    sc->sc_dev.dv_xname, ihdr->type);
		break;
	}
}

void
apldchidev_gpio_task(void *arg)
{
	struct apldchidev_gpio *ag = arg;
	struct apldchidev_softc *sc = ag->ag_sc;
	struct mtp_gpio_ack *ack;
	uint8_t flags;
	size_t len;

	gpio_controller_set_pin(ag->ag_gpio, 1);
	delay(10000);
	gpio_controller_set_pin(ag->ag_gpio, 0);

	len = sizeof(*ack) + sc->sc_gpio_cmd_len;
	ack = malloc(len, M_TEMP, M_WAITOK);
	ack->type = MTP_CMD_ACK_GPIO_CMD;
	ack->retcode = 0;
	memcpy(ack->cmd, sc->sc_gpio_cmd, sc->sc_gpio_cmd_len);

	flags = MTP_GROUP_CMD << MTP_GROUP_SHIFT;
	flags |= MTP_REQ_SET_REPORT << MTP_REQ_SHIFT;
	apldchidev_cmd(sc, MTP_IFACE_COMM, flags, ack, len);

	free(ack, M_TEMP, len);
}

void apldckbd_intr(struct device *, uint8_t *, size_t);
void apldcms_intr(struct device *, uint8_t *, size_t);

int
apldchidev_rx_intr(void *arg)
{
	struct apldchidev_softc *sc = arg;
	struct mtp_hdr hdr;
	struct mtp_subhdr *shdr;
	uint32_t checksum = 0;
	char buf[APLDCHIDEV_PKT_MAX];

	apldchidev_read(sc, &hdr, sizeof(hdr), &checksum);
	apldchidev_read(sc, buf, hdr.pkt_len + 4, &checksum);
	if (checksum != 0xffffffff) {
		printf("%s: packet checksum error\n", sc->sc_dev.dv_xname);
		return 1;
	}
	if (hdr.pkt_len < sizeof(*shdr)) {
		printf("%s: packet too small\n", sc->sc_dev.dv_xname);
		return 1;
	}

	shdr = (struct mtp_subhdr *)buf;
	if (MTP_GROUP(shdr->flags) == MTP_GROUP_OUTPUT ||
	    MTP_GROUP(shdr->flags) == MTP_GROUP_CMD) {
		if (hdr.iface != sc->sc_cmd_iface) {
			printf("%s: got ack for unexpected iface\n",
			    sc->sc_dev.dv_xname);
		}
		if (hdr.seq != sc->sc_cmd_seq) {
			printf("%s: got ack with unexpected seq\n",
			    sc->sc_dev.dv_xname);
		}
		if (MTP_REQ(shdr->flags) == MTP_REQ_GET_REPORT &&
		    shdr->len <= sizeof(sc->sc_data)) {
			memcpy(sc->sc_data, (shdr + 1), shdr->len);
			sc->sc_data_len = shdr->len;
		} else {
			sc->sc_data_len = 0;
		}
		sc->sc_retcode = shdr->retcode;
		sc->sc_busy = 0;
		wakeup(sc);
		return 1;
	}
	if (MTP_GROUP(shdr->flags) != MTP_GROUP_INPUT) {
		printf("%s: unhandled group 0x%02x\n",
		    sc->sc_dev.dv_xname, shdr->flags);
		return 1;
	}

	if (hdr.iface == MTP_IFACE_COMM)
		apldchidev_handle_comm(sc, shdr + 1, shdr->len);
	else if (hdr.iface == sc->sc_iface_kbd && sc->sc_kbd)
		apldckbd_intr(sc->sc_kbd, (uint8_t *)(shdr + 1), shdr->len);
	else if (hdr.iface == sc->sc_iface_mt && sc->sc_mt)
		apldcms_intr(sc->sc_mt, (uint8_t *)(shdr + 1), shdr->len);
	else {
		printf("%s: unhandled iface %d\n",
		    sc->sc_dev.dv_xname, hdr.iface);
	}

	wakeup(sc);
	return 1;
}

void
apldchidev_cmd(struct apldchidev_softc *sc, uint8_t iface, uint8_t flags,
    void *data, size_t len)
{
	struct mtp_hdr hdr;
	struct mtp_subhdr shdr;
	uint32_t checksum = 0xffffffff;
	uint8_t pad[4];

	KASSERT(sc->sc_busy == 0);
	sc->sc_busy = 1;

	memset(&hdr, 0, sizeof(hdr));
	hdr.hdr_len = sizeof(hdr);
	hdr.chan = MTP_CHAN_CMD;
	hdr.pkt_len = roundup(len, 4) + sizeof(shdr);
	if (iface == MTP_IFACE_COMM)
		hdr.seq = sc->sc_seq_comm++;
	else if (iface == sc->sc_iface_kbd)
		hdr.seq = sc->sc_seq_kbd++;
	else if (iface == sc->sc_iface_mt)
		hdr.seq = sc->sc_seq_mt++;
	else if (iface == sc->sc_iface_stm)
		hdr.seq = sc->sc_seq_stm++;
	hdr.iface = iface;
	sc->sc_cmd_iface = hdr.iface;
	sc->sc_cmd_seq = hdr.seq;
	memset(&shdr, 0, sizeof(shdr));
	shdr.flags = flags;
	shdr.len = len;
	apldchidev_write(sc, &hdr, sizeof(hdr), &checksum);
	apldchidev_write(sc, &shdr, sizeof(shdr), &checksum);
	apldchidev_write(sc, data, len & ~3, &checksum);
	if (len & 3) {
		memset(pad, 0, sizeof(pad));
		memcpy(pad, &data[len & ~3], len & 3);
		apldchidev_write(sc, pad, sizeof(pad), &checksum);
	}
	apldchidev_write(sc, &checksum, sizeof(checksum), NULL);
}

void
apldchidev_wait(struct apldchidev_softc *sc)
{
	int retry, error;

	if (cold) {
		for (retry = 10; retry > 0; retry--) {
			if (sc->sc_busy == 0)
				break;
			apldchidev_rx_intr(sc);
			delay(1000);
		}
		return;
	}
	
	while (sc->sc_busy) {
		error = tsleep_nsec(sc, PZERO, "apldcwt", SEC_TO_NSEC(1));
		if (error == EWOULDBLOCK)
			return;
	}

	if (sc->sc_retcode) {
		printf("%s: command failed with error 0x%04x\n",
		    sc->sc_dev.dv_xname, sc->sc_retcode);
	}
}

void
apldchidev_enable(struct apldchidev_softc *sc, uint8_t iface)
{
	uint8_t cmd[2] = { MTP_CMD_ENABLE_INTERFACE, iface };
	uint8_t flags;

	flags = MTP_GROUP_CMD << MTP_GROUP_SHIFT;
	flags |= MTP_REQ_SET_REPORT << MTP_REQ_SHIFT;
	apldchidev_cmd(sc, MTP_IFACE_COMM, flags, cmd, sizeof(cmd));
	apldchidev_wait(sc);
}

void
apldchidev_reset(struct apldchidev_softc *sc, uint8_t iface, uint8_t state)
{
	uint8_t cmd[4] = { MTP_CMD_RESET_INTERFACE, 1, iface, state };
	uint8_t flags;

	flags = MTP_GROUP_CMD << MTP_GROUP_SHIFT;
	flags |= MTP_REQ_SET_REPORT << MTP_REQ_SHIFT;
	apldchidev_cmd(sc, MTP_IFACE_COMM, flags, cmd, sizeof(cmd));
	apldchidev_wait(sc);
}

#if NAPLDCMS > 0

int
apldchidev_send_firmware(struct apldchidev_softc *sc, int iface,
    void *ucode, size_t ucode_size)
{
	bus_dmamap_t map;
	bus_dma_segment_t seg;
	uint8_t cmd[16] = {};
	uint64_t addr;
	uint32_t size;
	uint8_t flags;
	caddr_t buf;
	int nsegs;
	int error;

	error = bus_dmamap_create(sc->sc_dmat, ucode_size, 1, ucode_size, 0,
	    BUS_DMA_WAITOK, &map);
	if (error)
		return error;

	error = bus_dmamem_alloc(sc->sc_dmat, ucode_size, 4 * PAGE_SIZE, 0,
	    &seg, 1, &nsegs, BUS_DMA_WAITOK);
	if (error) {
		bus_dmamap_destroy(sc->sc_dmat, map);
		return error;
	}

	error = bus_dmamem_map(sc->sc_dmat, &seg, 1, ucode_size, &buf,
	    BUS_DMA_WAITOK);
	if (error) {
		bus_dmamem_free(sc->sc_dmat, &seg, 1);
		bus_dmamap_destroy(sc->sc_dmat, map);
		return error;
	}

	error = bus_dmamap_load_raw(sc->sc_dmat, map, &seg, 1,
	    ucode_size, BUS_DMA_WAITOK);
	if (error) {
		bus_dmamem_unmap(sc->sc_dmat, buf, ucode_size);
		bus_dmamem_free(sc->sc_dmat, &seg, 1);
		bus_dmamap_destroy(sc->sc_dmat, map);
		return error;
	}

	memcpy(buf, ucode, ucode_size);
	bus_dmamap_sync(sc->sc_dmat, map, 0, ucode_size, BUS_DMASYNC_PREWRITE);

	cmd[0] = MTP_CMD_SEND_FIRMWARE;
	cmd[1] = 2;
	cmd[2] = 0;
	cmd[3] = iface;
	addr = map->dm_segs[0].ds_addr;
	memcpy(&cmd[4], &addr, sizeof(addr));
	size = map->dm_segs[0].ds_len;
	memcpy(&cmd[12], &size, sizeof(size));

	flags = MTP_GROUP_CMD << MTP_GROUP_SHIFT;
	flags |= MTP_REQ_SET_REPORT << MTP_REQ_SHIFT;
	apldchidev_cmd(sc, MTP_IFACE_COMM, flags, cmd, sizeof(cmd));
	apldchidev_wait(sc);

	bus_dmamap_unload(sc->sc_dmat, map);
	bus_dmamem_unmap(sc->sc_dmat, buf, ucode_size);
	bus_dmamem_free(sc->sc_dmat, &seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, map);

	return 0;
}

struct mtp_fwhdr {
	uint32_t magic;
#define MTP_FW_MAGIC	0x46444948
	uint32_t version;
#define MTP_FW_VERSION	1
	uint32_t hdr_len;
	uint32_t data_len;
	uint32_t iface_off;
};

int
apldchidev_load_firmware(struct apldchidev_softc *sc, const char *name)
{
	struct mtp_fwhdr *hdr;
	uint8_t *ucode;
	size_t ucode_size;
	uint8_t *data;
	size_t size;
	int error;

	error = loadfirmware(name, &ucode, &ucode_size);
	if (error) {
		printf("%s: error %d, could not read firmware %s\n",
		    sc->sc_dev.dv_xname, error, name);
		return error;
	}

	hdr = (struct mtp_fwhdr *)ucode;
	if (sizeof(hdr) > ucode_size ||
	    hdr->hdr_len + hdr->data_len > ucode_size) {
		printf("%s: loaded firmware is too small\n",
		    sc->sc_dev.dv_xname);
		return EINVAL;
	}
	if (hdr->magic != MTP_FW_MAGIC) {
		printf("%s: wrong firmware magic number 0x%08x\n",
		    sc->sc_dev.dv_xname, hdr->magic);
		return EINVAL;
	}
	if (hdr->version != MTP_FW_VERSION) {
		printf("%s: wrong firmware version %d\n",
		    sc->sc_dev.dv_xname, hdr->version);
		return EINVAL;
	}
	data = ucode + hdr->hdr_len;
	if (hdr->iface_off)
		data[hdr->iface_off] = sc->sc_iface_mt;
	size = hdr->data_len;

	apldchidev_send_firmware(sc, sc->sc_iface_mt, data, size);
	apldchidev_reset(sc, sc->sc_iface_mt, 0);
	apldchidev_reset(sc, sc->sc_iface_mt, 2);

	/* Wait until ready. */
	while (sc->sc_mt_ready == 0) {
		error = tsleep_nsec(sc, PZERO, "apldcmt", SEC_TO_NSEC(2));
		if (error == EWOULDBLOCK)
			return error;
	}

	return 0;
}

void
apldchidev_get_dimensions(struct apldchidev_softc *sc)
{
	uint8_t cmd[1] = { MTP_CMD_GET_DIMENSIONS };
	struct mtp_dim dim;
	uint8_t flags;

	flags = MTP_GROUP_CMD << MTP_GROUP_SHIFT;
	flags |= MTP_REQ_GET_REPORT << MTP_REQ_SHIFT;
	apldchidev_cmd(sc, sc->sc_iface_mt, flags, cmd, sizeof(cmd));
	apldchidev_wait(sc);

	if (sc->sc_retcode == 0 && sc->sc_data_len == sizeof(dim) + 1 &&
	    sc->sc_data[0] == MTP_CMD_GET_DIMENSIONS) {
		memcpy(&dim, &sc->sc_data[1], sizeof(dim));
		sc->sc_x_min = dim.x_min;
		sc->sc_x_max = dim.x_max;
		sc->sc_y_min = dim.y_min;
		sc->sc_y_max = dim.y_max;
		sc->sc_h_res = (100 * (dim.x_max - dim.x_min)) / dim.width;
		sc->sc_v_res = (100 * (dim.y_max - dim.y_min)) / dim.height;
	}		
}

void
apldchidev_attachhook(struct device *self)
{
	struct apldchidev_softc *sc = (struct apldchidev_softc *)self;
	struct apldchidev_attach_args aa;
	char *firmware_name;
	int node, len;
	int retry;
	int error;

	/* Enable interface. */
	apldchidev_enable(sc, sc->sc_iface_mt);

	node = OF_getnodebyname(sc->sc_node, "multi-touch");
	if (node == -1)
		return;
	len = OF_getproplen(node, "firmware-name");
	if (len <= 0)
		return;

	/* Wait until we have received the multi-touch HID descriptor. */
	while (sc->sc_mtdesclen == 0) {
		error = tsleep_nsec(sc, PZERO, "apldcmt", SEC_TO_NSEC(1));
		if (error == EWOULDBLOCK)
			return;
	}

	firmware_name = malloc(len, M_TEMP, M_WAITOK);
	OF_getprop(node, "firmware-name", firmware_name, len);

	for (retry = 5; retry > 0; retry--) {
		error = apldchidev_load_firmware(sc, firmware_name);
		if (error != EWOULDBLOCK)
			break;
	}
	if (error)
		goto out;

	apldchidev_get_dimensions(sc);

	aa.aa_name = "multi-touch";
	aa.aa_desc = sc->sc_mtdesc;
	aa.aa_desclen = sc->sc_mtdesclen;
	sc->sc_mt = config_found(self, &aa, NULL);

out:
	free(firmware_name, M_TEMP, len);
}

#endif

void
apldchidev_set_leds(struct apldchidev_softc *sc, uint8_t leds)
{
	uint8_t report[2] = { 1, leds };
	uint8_t flags;

	flags = MTP_GROUP_OUTPUT << MTP_GROUP_SHIFT;
	flags |= MTP_REQ_SET_REPORT << MTP_REQ_SHIFT;
	apldchidev_cmd(sc, sc->sc_iface_kbd, flags, report, sizeof(report));
}

/* Keyboard */

struct apldckbd_softc {
	struct device		sc_dev;
	struct apldchidev_softc	*sc_hidev;
	struct hidkbd		sc_kbd;
	int			sc_spl;
};

void	apldckbd_cngetc(void *, u_int *, int *);
void	apldckbd_cnpollc(void *, int);
void	apldckbd_cnbell(void *, u_int, u_int, u_int);

const struct wskbd_consops apldckbd_consops = {
	apldckbd_cngetc,
	apldckbd_cnpollc,
	apldckbd_cnbell,
};

int	apldckbd_enable(void *, int);
void	apldckbd_set_leds(void *, int);
int	apldckbd_ioctl(void *, u_long, caddr_t, int, struct proc *);

const struct wskbd_accessops apldckbd_accessops = {
	.enable = apldckbd_enable,
	.ioctl = apldckbd_ioctl,
	.set_leds = apldckbd_set_leds,
};

int	 apldckbd_match(struct device *, void *, void *);
void	 apldckbd_attach(struct device *, struct device *, void *);

const struct cfattach apldckbd_ca = {
	sizeof(struct apldckbd_softc), apldckbd_match, apldckbd_attach
};

struct cfdriver apldckbd_cd = {
	NULL, "apldckbd", DV_DULL
};

int
apldckbd_match(struct device *parent, void *match, void *aux)
{
	struct apldchidev_attach_args *aa = aux;

	return strcmp(aa->aa_name, "keyboard") == 0;
}

void
apldckbd_attach(struct device *parent, struct device *self, void *aux)
{
	struct apldckbd_softc *sc = (struct apldckbd_softc *)self;
	struct apldchidev_attach_args *aa = aux;
	struct hidkbd *kbd = &sc->sc_kbd;

#define APLHIDEV_KBD_DEVICE	1
	sc->sc_hidev = (struct apldchidev_softc *)parent;
	if (hidkbd_attach(self, kbd, 1, 0, APLHIDEV_KBD_DEVICE,
	    aa->aa_desc, aa->aa_desclen))
		return;

	printf("\n");

	if (hid_locate(aa->aa_desc, aa->aa_desclen, HID_USAGE2(HUP_APPLE, HUG_FN_KEY),
	    1, hid_input, &kbd->sc_fn, NULL))
		kbd->sc_munge = hidkbd_apple_munge;

	if (kbd->sc_console_keyboard) {
		extern struct wskbd_mapdata ukbd_keymapdata;

		ukbd_keymapdata.layout = KB_US | KB_DEFAULT;
		wskbd_cnattach(&apldckbd_consops, sc, &ukbd_keymapdata);
		apldckbd_enable(sc, 1);
	}

	hidkbd_attach_wskbd(kbd, KB_US | KB_DEFAULT, &apldckbd_accessops);
}

void
apldckbd_intr(struct device *self, uint8_t *packet, size_t packetlen)
{
	struct apldckbd_softc *sc = (struct apldckbd_softc *)self;
	struct hidkbd *kbd = &sc->sc_kbd;

	if (kbd->sc_enabled)
		hidkbd_input(kbd, &packet[1], packetlen - 1);
}

int
apldckbd_enable(void *v, int on)
{
	struct apldckbd_softc *sc = v;
	struct hidkbd *kbd = &sc->sc_kbd;

	return hidkbd_enable(kbd, on);
}

int
apldckbd_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct apldckbd_softc *sc = v;
	struct hidkbd *kbd = &sc->sc_kbd;

	switch (cmd) {
	case WSKBDIO_GTYPE:
		/* XXX: should we set something else? */
		*(u_int *)data = WSKBD_TYPE_USB;
		return 0;
	case WSKBDIO_SETLEDS:
		apldckbd_set_leds(v, *(int *)data);
		return 0;
	default:
		return hidkbd_ioctl(kbd, cmd, data, flag, p);
	}
}

void
apldckbd_set_leds(void *v, int leds)
{
	struct apldckbd_softc *sc = v;
	struct hidkbd *kbd = &sc->sc_kbd;
	uint8_t res;

	if (hidkbd_set_leds(kbd, leds, &res))
		apldchidev_set_leds(sc->sc_hidev, res);
}

/* Console interface. */
void
apldckbd_cngetc(void *v, u_int *type, int *data)
{
	struct apldckbd_softc *sc = v;
	struct hidkbd *kbd = &sc->sc_kbd;

	kbd->sc_polling = 1;
	while (kbd->sc_npollchar <= 0) {
		apldchidev_rx_intr(sc->sc_dev.dv_parent);
		delay(1000);
	}
	kbd->sc_polling = 0;
	hidkbd_cngetc(kbd, type, data);
}

void
apldckbd_cnpollc(void *v, int on)
{
	struct apldckbd_softc *sc = v;

	if (on)
		sc->sc_spl = spltty();
	else
		splx(sc->sc_spl);
}

void
apldckbd_cnbell(void *v, u_int pitch, u_int period, u_int volume)
{
	hidkbd_bell(pitch, period, volume, 1);
}

#if NAPLDCMS > 0

/* Touchpad */

/*
 * The contents of the touchpad event packets is identical to those
 * used by the ubcmtp(4) driver.  The relevant definitions and the
 * code to decode the packets is replicated here.
 */

struct ubcmtp_finger {
	uint16_t	origin;
	uint16_t	abs_x;
	uint16_t	abs_y;
	uint16_t	rel_x;
	uint16_t	rel_y;
	uint16_t	tool_major;
	uint16_t	tool_minor;
	uint16_t	orientation;
	uint16_t	touch_major;
	uint16_t	touch_minor;
	uint16_t	unused[2];
	uint16_t	pressure;
	uint16_t	multi;
} __packed __attribute((aligned(2)));

#define UBCMTP_MAX_FINGERS	16

#define UBCMTP_TYPE4_TPOFF	(20 * sizeof(uint16_t))
#define UBCMTP_TYPE4_BTOFF	23
#define UBCMTP_TYPE4_FINGERPAD	(1 * sizeof(uint16_t))

/* Use a constant, synaptics-compatible pressure value for now. */
#define DEFAULT_PRESSURE	40

struct apldcms_softc {
	struct device		sc_dev;
	struct apldchidev_softc	*sc_hidev;
	struct device		*sc_wsmousedev;

	int			sc_enabled;

	int			tp_offset;
	int			tp_fingerpad;

	struct mtpoint		frame[UBCMTP_MAX_FINGERS];
	int			contacts;
	int			btn;
};

int	apldcms_enable(void *);
void	apldcms_disable(void *);
int	apldcms_ioctl(void *, u_long, caddr_t, int, struct proc *);

static struct wsmouse_param apldcms_wsmousecfg[] = {
	{ WSMOUSECFG_MTBTN_MAXDIST, 0 }, /* 0: Compute a default value. */
};

const struct wsmouse_accessops apldcms_accessops = {
	.enable = apldcms_enable,
	.disable = apldcms_disable,
	.ioctl = apldcms_ioctl,
};

int	 apldcms_match(struct device *, void *, void *);
void	 apldcms_attach(struct device *, struct device *, void *);

const struct cfattach apldcms_ca = {
	sizeof(struct apldcms_softc), apldcms_match, apldcms_attach
};

struct cfdriver apldcms_cd = {
	NULL, "apldcms", DV_DULL
};

int	apldcms_configure(struct apldcms_softc *);

int
apldcms_match(struct device *parent, void *match, void *aux)
{
	struct apldchidev_attach_args *aa = aux;

	return strcmp(aa->aa_name, "multi-touch") == 0;
}

void
apldcms_attach(struct device *parent, struct device *self, void *aux)
{
	struct apldcms_softc *sc = (struct apldcms_softc *)self;
	struct wsmousedev_attach_args aa;

	sc->sc_hidev = (struct apldchidev_softc *)parent;

	printf("\n");

	sc->tp_offset = UBCMTP_TYPE4_TPOFF;
	sc->tp_fingerpad = UBCMTP_TYPE4_FINGERPAD;

	aa.accessops = &apldcms_accessops;
	aa.accesscookie = sc;

	sc->sc_wsmousedev = config_found(self, &aa, wsmousedevprint);
	if (sc->sc_wsmousedev != NULL && apldcms_configure(sc))
		apldcms_disable(sc);
}

int
apldcms_configure(struct apldcms_softc *sc)
{
	struct wsmousehw *hw = wsmouse_get_hw(sc->sc_wsmousedev);

	hw->type = WSMOUSE_TYPE_TOUCHPAD;
	hw->hw_type = WSMOUSEHW_CLICKPAD;
	hw->x_min = sc->sc_hidev->sc_x_min;
	hw->x_max = sc->sc_hidev->sc_x_max;
	hw->y_min = sc->sc_hidev->sc_y_min;
	hw->y_max = sc->sc_hidev->sc_y_max;
	hw->h_res = sc->sc_hidev->sc_h_res;
	hw->v_res = sc->sc_hidev->sc_v_res;
	hw->mt_slots = UBCMTP_MAX_FINGERS;
	hw->flags = WSMOUSEHW_MT_TRACKING;

	return wsmouse_configure(sc->sc_wsmousedev, apldcms_wsmousecfg,
	    nitems(apldcms_wsmousecfg));
}

void
apldcms_intr(struct device *self, uint8_t *packet, size_t packetlen)
{
	struct apldcms_softc *sc = (struct apldcms_softc *)self;
	struct ubcmtp_finger *finger;
	int off, s, btn, contacts;

	if (!sc->sc_enabled)
		return;

	contacts = 0;
	for (off = sc->tp_offset; off < packetlen;
	    off += (sizeof(struct ubcmtp_finger) + sc->tp_fingerpad)) {
		finger = (struct ubcmtp_finger *)(packet + off);

		if ((int16_t)letoh16(finger->touch_major) == 0)
			continue; /* finger lifted */

		sc->frame[contacts].x = (int16_t)letoh16(finger->abs_x);
		sc->frame[contacts].y = (int16_t)letoh16(finger->abs_y);
		sc->frame[contacts].pressure = DEFAULT_PRESSURE;
		contacts++;
	}

	btn = sc->btn;
	sc->btn = !!((int16_t)letoh16(packet[UBCMTP_TYPE4_BTOFF]));

	if (contacts || sc->contacts || sc->btn != btn) {
		sc->contacts = contacts;
		s = spltty();
		wsmouse_buttons(sc->sc_wsmousedev, sc->btn);
		wsmouse_mtframe(sc->sc_wsmousedev, sc->frame, contacts);
		wsmouse_input_sync(sc->sc_wsmousedev);
		splx(s);
	}
}

int
apldcms_enable(void *v)
{
	struct apldcms_softc *sc = v;

	if (sc->sc_enabled)
		return EBUSY;

	sc->sc_enabled = 1;
	return 0;
}

void
apldcms_disable(void *v)
{
	struct apldcms_softc *sc = v;

	sc->sc_enabled = 0;
}

int
apldcms_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct apldcms_softc *sc = v;
	struct wsmousehw *hw = wsmouse_get_hw(sc->sc_wsmousedev);
	struct wsmouse_calibcoords *wsmc = (struct wsmouse_calibcoords *)data;
	int wsmode;

	switch (cmd) {
	case WSMOUSEIO_GTYPE:
		*(u_int *)data = hw->type;
		break;

	case WSMOUSEIO_GCALIBCOORDS:
		wsmc->minx = hw->x_min;
		wsmc->maxx = hw->x_max;
		wsmc->miny = hw->y_min;
		wsmc->maxy = hw->y_max;
		wsmc->swapxy = 0;
		wsmc->resx = 0;
		wsmc->resy = 0;
		break;

	case WSMOUSEIO_SETMODE:
		wsmode = *(u_int *)data;
		if (wsmode != WSMOUSE_COMPAT && wsmode != WSMOUSE_NATIVE) {
			printf("%s: invalid mode %d\n", sc->sc_dev.dv_xname,
			    wsmode);
			return (EINVAL);
		}
		wsmouse_set_mode(sc->sc_wsmousedev, wsmode);
		break;

	default:
		return -1;
	}

	return 0;
}

#else

void
apldcms_intr(struct device *self, uint8_t *packet, size_t packetlen)
{
}

#endif
