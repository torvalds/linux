/*	$OpenBSD: qcdpc.c,v 1.1 2025/07/17 15:52:10 kettenis Exp $	*/
/*
 * Copyright (c) 2025 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/simplebusvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#include <drm/display/drm_dp_helper.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>

#define DP_HW_REVISION			0x00
#define DP_INTR_STATUS			0x20
#define  DP_INTR_AUX_XFER_DONE		(1U << 3)
#define  DP_INTR_WRONG_ADDR		(1U << 6)
#define  DP_INTR_TIMEOUT		(1U << 9)
#define  DP_INTR_NACK_DEFER		(1U << 12)
#define  DP_INTR_WRONG_DATA_CNT		(1U << 15)
#define  DP_INTR_AUX_ERROR		(1U << 27)
#define  DP_INTR_STATUS_ACK_SHIFT	1
#define  DP_INTR_STATUS_MASK_SHIFT	2
#define  DP_INTR_ERROR \
    (DP_INTR_WRONG_ADDR | DP_INTR_TIMEOUT | DP_INTR_NACK_DEFER | \
     DP_INTR_WRONG_DATA_CNT | DP_INTR_AUX_ERROR)

#define DP_AUX_CTRL			0x30
#define DP_AUX_DATA			0x34
#define  DP_AUX_DATA_SHIFT		8
#define  DP_AUX_DATA_MASK		(0xffU << 8)
#define  DP_AUX_DATA_READ		(1U << 0)
#define  DP_AUX_DATA_WRITE		(0U << 0)
#define  DP_AUX_DATA_INDEX_SHIFT	16
#define  DP_AUX_DATA_INDEX_MASK		(0xffU << 16)
#define  DP_AUX_DATA_INDEX_WRITE	(1U << 31)
#define DP_AUX_TRANS_CTRL		0x38
#define  DP_AUX_TRANS_CTRL_GO		(1U << 9)
#define DP_PHY_AUX_INTERRUPT_CLEAR	0x4c
#define DP_PHY_AUX_INTERRUPT_STATUS	0xbc

struct qcdpc_softc {
	struct simplebus_softc	sc_sbus;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ahb_ioh;
	bus_space_handle_t	sc_aux_ioh;
	void			*sc_ih;

	struct drm_dp_aux	sc_aux;
	uint32_t		sc_intr_status;

	struct drm_edp_backlight_info sc_bl;
	uint32_t		sc_bl_level;
};

struct qcdpc_softc *qcdpc_bl;

int	qcdpc_match(struct device *, void *, void *);
void	qcdpc_attach(struct device *, struct device *, void *);

const struct cfattach qcdpc_ca = {
	sizeof(struct qcdpc_softc), qcdpc_match, qcdpc_attach
};

struct cfdriver qcdpc_cd = {
	NULL, "qcdpc", DV_DULL
};

int	qcdpc_print(void *, const char *);
void	qcdpc_attach_backlight(struct qcdpc_softc *);
int	qcdpc_intr(void *);
ssize_t	qcdpc_dp_aux_transfer(struct drm_dp_aux *, struct drm_dp_aux_msg *);

int
qcdpc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "qcom,sc8280xp-dp") ||
	    OF_is_compatible(faa->fa_node, "qcom,sc8280xp-edp") ||
	    OF_is_compatible(faa->fa_node, "qcom,x1e80100-dp"));
}

void
qcdpc_attach(struct device *parent, struct device *self, void *aux)
{
	struct qcdpc_softc *sc = (struct qcdpc_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct fdt_attach_args fa;
	int node;

	/*
	 * Check that we have an AUX channel as all the functionality
	 * provided by this driver requires one.
	 *
	 * XXX Effectively this means we only attach to eDP
	 * controllers.  Since the eDP controller is almost certainly
	 * enabled on all machines we care about, this means we can
	 * get away with not having a clock/reset controller.
	 * Accessing registers on a disabled hardware block tends to
	 * result in an instant reset on Qualcomm SoCs.
	 */
	node = OF_getnodebyname(faa->fa_node, "aux-bus");
	if (node == 0) {
		printf("\n");
		return;
	}

	if (faa->fa_nreg < 2) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ahb_ioh)) {
		printf(": can't map AHB registers\n");
		return;
	}

	if (bus_space_map(sc->sc_iot, faa->fa_reg[1].addr,
	    faa->fa_reg[1].size, 0, &sc->sc_aux_ioh)) {
		printf(": can't map AUX registers\n");
		bus_space_unmap(sc->sc_iot, sc->sc_ahb_ioh,
		    faa->fa_reg[0].size);
		return;
	}

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_BIO, qcdpc_intr,
	    sc, sc->sc_sbus.sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		bus_space_unmap(sc->sc_iot, sc->sc_ahb_ioh,
		    faa->fa_reg[0].size);
		bus_space_unmap(sc->sc_iot, sc->sc_aux_ioh,
		    faa->fa_reg[1].size);
		return;
	}

	printf("\n");

	sc->sc_aux.name = sc->sc_sbus.sc_dev.dv_xname;
	sc->sc_aux.transfer = qcdpc_dp_aux_transfer;
	drm_dp_aux_init(&sc->sc_aux);

	node = OF_getnodebyname(node, "panel");
	if (node) {
		/* We have a panel.  Try to enable its backlight control. */
		qcdpc_attach_backlight(sc);

		memset(&fa, 0, sizeof(fa));
		fa.fa_name = "";
		fa.fa_node = node;
		config_found(self, &fa, qcdpc_print);
	}
}

int
qcdpc_print(void *aux, const char *pnp)
{
	struct fdt_attach_args *fa = aux;
	char name[32];

	if (!pnp)
		return (QUIET);

	if (OF_getprop(fa->fa_node, "name", name, sizeof(name)) > 0) {
		name[sizeof(name) - 1] = 0;
		printf("\"%s\"", name);
	} else
		printf("node %u", fa->fa_node);

	printf(" at %s", pnp);

	return (UNCONF);
}

int
qcdpc_get_param(struct wsdisplay_param *dp)
{
	struct qcdpc_softc *sc = qcdpc_bl;

	switch (dp->param) {
	case WSDISPLAYIO_PARAM_BRIGHTNESS:
		dp->min = 0;
		dp->max = sc->sc_bl.max;
		dp->curval = sc->sc_bl_level;
		return 0;
	default:
		return -1;
	}
}

int
qcdpc_set_param(struct wsdisplay_param *dp)
{
	struct qcdpc_softc *sc = qcdpc_bl;
	int ret;

	switch (dp->param) {
	case WSDISPLAYIO_PARAM_BRIGHTNESS:
		ret = drm_edp_backlight_set_level(&sc->sc_aux,
		    &sc->sc_bl, dp->curval);
		if (ret < 0)
			return -1;
		sc->sc_bl_level = dp->curval;
		return 0;
	default:
		return -1;
	}
}

void
qcdpc_attach_backlight(struct qcdpc_softc *sc)
{
	uint8_t edp_dpcd[EDP_DISPLAY_CTL_CAP_SIZE];
	int ret;

	ret = drm_dp_dpcd_read(&sc->sc_aux, DP_EDP_DPCD_REV, edp_dpcd,
	    EDP_DISPLAY_CTL_CAP_SIZE);
	if (ret < 0)
		return;

	if (drm_edp_backlight_supported(edp_dpcd)) {
		uint16_t current_level;
		uint8_t current_mode;

		ret = drm_edp_backlight_init(&sc->sc_aux, &sc->sc_bl, 0,
		    edp_dpcd, &current_level, &current_mode);
		if (ret < 0 || !sc->sc_bl.aux_set)
			return;
		sc->sc_bl_level = current_level;

		qcdpc_bl = sc;
		ws_get_param = qcdpc_get_param;
		ws_set_param = qcdpc_set_param;
	}
}

int
qcdpc_intr(void *arg)
{
	struct qcdpc_softc *sc = arg;
	int handled = 0;
	uint32_t status;

	status = bus_space_read_4(sc->sc_iot, sc->sc_ahb_ioh, DP_INTR_STATUS);
	if (status & (DP_INTR_AUX_XFER_DONE | DP_INTR_ERROR)) {
		sc->sc_intr_status = status;
		handled = 1;
	}
	bus_space_write_4(sc->sc_iot, sc->sc_ahb_ioh, DP_INTR_STATUS,
	    DP_INTR_AUX_XFER_DONE << DP_INTR_STATUS_ACK_SHIFT |
	    DP_INTR_ERROR << DP_INTR_STATUS_ACK_SHIFT);

	return handled;
}

/*
 * The function below implement a DP AUX channel backend for DRM and
 * therefore follow the Linux convention of returning negative error
 * values.
 */

int
qcdpc_dp_aux_wait(struct qcdpc_softc *sc)
{
	uint32_t status;
	int s, timo;

	if (cold) {
		for (timo = 250; timo > 0; timo--) {
			status = bus_space_read_4(sc->sc_iot, sc->sc_ahb_ioh,
			    DP_INTR_STATUS);
			if (status & (DP_INTR_AUX_XFER_DONE | DP_INTR_ERROR)) {
				sc->sc_intr_status = status;
				break;
			}
			delay(1000);
		}
	} else {
		s = splbio();
		if (sc->sc_intr_status == 0) {
			tsleep_nsec(&sc->sc_intr_status, PWAIT, "qcdpc",
			    MSEC_TO_NSEC(250));
		}
		splx(s);
	}

	return sc->sc_intr_status ? 0 : -ETIMEDOUT;
}

ssize_t
qcdpc_dp_aux_write(struct qcdpc_softc *sc, struct drm_dp_aux_msg *msg)
{
	uint8_t buf[DP_AUX_MAX_PAYLOAD_BYTES + 4];
	uint8_t *msgbuf;
	uint32_t val, stat;
	size_t len;
	int i, ret;

	KASSERT(msg->size <= DP_AUX_MAX_PAYLOAD_BYTES);

	if (msg->request == DP_AUX_NATIVE_READ)
		len = 0;
	else
		len = msg->size;

	buf[0] = msg->address >> 16;
	if (msg->request == DP_AUX_NATIVE_READ)
		buf[0] |= (1 << 4);
	buf[1] = msg->address >> 8;
	buf[2] = msg->address >> 0;
	buf[3] = msg->size - 1;
	memcpy(&buf[4], msg->buffer, len);

	for (i = 0; i < len + 4; i++) {
		val = buf[i] << DP_AUX_DATA_SHIFT;
		val |= DP_AUX_DATA_WRITE;
		if (i == 0)
			val |= DP_AUX_DATA_INDEX_WRITE;
		bus_space_write_4(sc->sc_iot, sc->sc_aux_ioh,
		    DP_AUX_DATA, val);
	}

	bus_space_write_4(sc->sc_iot, sc->sc_aux_ioh, DP_AUX_TRANS_CTRL, 0);

	/* Clear HW interrupts. */
	bus_space_read_4(sc->sc_iot, sc->sc_aux_ioh,
	     DP_PHY_AUX_INTERRUPT_STATUS);
	bus_space_write_4(sc->sc_iot, sc->sc_aux_ioh,
	     DP_PHY_AUX_INTERRUPT_CLEAR, 0x1f);
	bus_space_write_4(sc->sc_iot, sc->sc_aux_ioh,
	     DP_PHY_AUX_INTERRUPT_CLEAR, 0x9f);
	bus_space_write_4(sc->sc_iot, sc->sc_aux_ioh,
	     DP_PHY_AUX_INTERRUPT_CLEAR, 0);

	sc->sc_intr_status = 0;
	bus_space_write_4(sc->sc_iot, sc->sc_ahb_ioh, DP_INTR_STATUS,
	    DP_INTR_AUX_XFER_DONE << DP_INTR_STATUS_ACK_SHIFT |
	    DP_INTR_ERROR << DP_INTR_STATUS_ACK_SHIFT |
	    DP_INTR_AUX_XFER_DONE << DP_INTR_STATUS_MASK_SHIFT |
	    DP_INTR_ERROR << DP_INTR_STATUS_MASK_SHIFT);

	bus_space_write_4(sc->sc_iot, sc->sc_aux_ioh, DP_AUX_TRANS_CTRL,
	    DP_AUX_TRANS_CTRL_GO);

	ret = qcdpc_dp_aux_wait(sc);

	bus_space_write_4(sc->sc_iot, sc->sc_ahb_ioh, DP_INTR_STATUS,
	    DP_INTR_AUX_XFER_DONE << DP_INTR_STATUS_ACK_SHIFT |
	    DP_INTR_ERROR << DP_INTR_STATUS_ACK_SHIFT);

	if (ret < 0)
		return ret;

	return len;
}

ssize_t
qcdpc_dp_aux_read(struct qcdpc_softc *sc, struct drm_dp_aux_msg *msg)
{
	uint8_t *p = msg->buffer;
	uint32_t val;
	int i, j;

	bus_space_write_4(sc->sc_iot, sc->sc_aux_ioh, DP_AUX_TRANS_CTRL, 0);

	val = DP_AUX_DATA_INDEX_WRITE | DP_AUX_DATA_READ;
	bus_space_write_4(sc->sc_iot, sc->sc_aux_ioh, DP_AUX_DATA, val);

	/* Discard the first byte. */
	bus_space_read_4(sc->sc_iot, sc->sc_aux_ioh, DP_AUX_DATA);

	for (i = 0; i < msg->size; i++) {
		val = bus_space_read_4(sc->sc_iot, sc->sc_aux_ioh,
		    DP_AUX_DATA);
		*p++ = val >> DP_AUX_DATA_SHIFT;
		j = (val & DP_AUX_DATA_INDEX_MASK) >> DP_AUX_DATA_INDEX_SHIFT;
		if (j != i)
			break;
	}

	return i;
}

ssize_t
qcdpc_dp_aux_transfer(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg)
{
	struct qcdpc_softc *sc = container_of(aux, struct qcdpc_softc, sc_aux);
	ssize_t ret;
	int native;

	if (msg->request == DP_AUX_NATIVE_WRITE ||
	    msg->request == DP_AUX_NATIVE_READ)
		native = 1;
	else
		native = 0;

	if (msg->size == 0 || msg->buffer == NULL) {
		if (native)
			msg->reply = DP_AUX_NATIVE_REPLY_ACK;
		else
			msg->reply = DP_AUX_I2C_REPLY_ACK;
		return msg->size;
	}

	if ((native && msg->size > DP_AUX_MAX_PAYLOAD_BYTES) ||
	    msg->size > 128)
		return -EINVAL;

	/* XXX */
	if (msg->request != DP_AUX_NATIVE_READ &&
	    msg->request != DP_AUX_NATIVE_WRITE)
		return -EINVAL;

	ret = qcdpc_dp_aux_write(sc, msg);
	if (ret < 0)
		return ret;

	if (msg->request == DP_AUX_NATIVE_READ)
		ret = qcdpc_dp_aux_read(sc, msg);

	if (sc->sc_intr_status & DP_INTR_TIMEOUT)
		ret = -ETIMEDOUT;
	else if (sc->sc_intr_status & DP_INTR_ERROR)
		msg->reply = DP_AUX_NATIVE_REPLY_NACK;
	else
		msg->reply = DP_AUX_NATIVE_REPLY_ACK;
	
	return ret;
}
