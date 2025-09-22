/* $OpenBSD: tcpci.c,v 1.3 2022/04/06 18:59:28 naddy Exp $ */
/*
 * Copyright (c) 2018 Patrick Wildt <patrick@blueri.se>
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
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/stdint.h>
#include <sys/task.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/i2c/i2cvar.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>

/* #define TCPCI_DEBUG */

#ifdef TCPCI_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

#define TCPC_VENDOR_ID				0x00
#define TCPC_PRODUCT_ID				0x02
#define TCPC_BCD_DEV				0x04
#define TCPC_TC_REV				0x06
#define TCPC_PD_REV				0x08
#define TCPC_PD_INT_REV				0x0a
#define TCPC_ALERT				0x10
#define  TCPC_ALERT_CC_STATUS				(1 << 0)
#define  TCPC_ALERT_POWER_STATUS			(1 << 1)
#define  TCPC_ALERT_RX_STATUS				(1 << 2)
#define  TCPC_ALERT_RX_HARD_RST				(1 << 3)
#define  TCPC_ALERT_TX_FAILED				(1 << 4)
#define  TCPC_ALERT_TX_DISCARDED			(1 << 5)
#define  TCPC_ALERT_TX_SUCCESS				(1 << 6)
#define  TCPC_ALERT_V_ALARM_HI				(1 << 7)
#define  TCPC_ALERT_V_ALARM_LO				(1 << 8)
#define  TCPC_ALERT_FAULT				(1 << 9)
#define  TCPC_ALERT_RX_BUF_OVF				(1 << 10)
#define  TCPC_ALERT_VBUS_DISCNCT			(1 << 11)
#define TCPC_ALERT_MASK				0x12
#define TCPC_POWER_STATUS_MASK			0x14
#define  TCPC_POWER_STATUS_VBUS_PRES			(1 << 2)
#define TCPC_FAULT_STATUS_MASK			0x15
#define TCPC_CONFIG_STD_OUTPUT			0x18
#define TCPC_TCPC_CTRL				0x19
#define  TCPC_TCPC_CTRL_ORIENTATION			(1 << 0)
#define  TCPC_TCPC_CTRL_BIST_MODE			(1 << 1)
#define TCPC_ROLE_CTRL				0x1a
#define  TCPC_ROLE_CTRL_CC1_SHIFT			0
#define  TCPC_ROLE_CTRL_CC2_SHIFT			2
#define  TCPC_ROLE_CTRL_CC_RA				0x0
#define  TCPC_ROLE_CTRL_CC_RP				0x1
#define  TCPC_ROLE_CTRL_CC_RD				0x2
#define  TCPC_ROLE_CTRL_CC_OPEN				0x3
#define  TCPC_ROLE_CTRL_CC_MASK				0x3
#define  TCPC_ROLE_CTRL_RP_VAL_MASK			(0x3 << 4)
#define  TCPC_ROLE_CTRL_RP_VAL_DEF			(0x0 << 4)
#define  TCPC_ROLE_CTRL_RP_VAL_1_5			(0x1 << 4)
#define  TCPC_ROLE_CTRL_RP_VAL_3_0			(0x2 << 4)
#define  TCPC_ROLE_CTRL_DRP				(1 << 6)
#define TCPC_FAULT_CTRL				0x1b
#define TCPC_POWER_CTRL				0x1c
#define  TCPC_POWER_CTRL_VCONN_ENABLE			(1 << 0)
#define  TCPC_POWER_CTRL_FORCEDISCH			(1 << 2)
#define  TCPC_POWER_CTRL_DIS_VOL_ALARM			(1 << 5)
#define TCPC_CC_STATUS				0x1d
#define  TCPC_CC_STATUS_CC1_SHIFT			0
#define  TCPC_CC_STATUS_CC2_SHIFT			2
#define  TCPC_CC_STATUS_CC_MASK				0x3
#define  TCPC_CC_STATUS_TERM				(1 << 4)
#define  TCPC_CC_STATUS_TOGGLING			(1 << 5)
#define TCPC_POWER_STATUS			0x1e
#define TCPC_FAULT_STATUS			0x1f
#define  TCPC_FAULT_STATUS_CLEAR			(1 << 7)
#define TCPC_COMMAND				0x23
#define  TCPC_COMMAND_WAKE_I2C				0x11
#define  TCPC_COMMAND_DISABLE_VBUS_DETECT		0x22
#define  TCPC_COMMAND_ENABLE_VBUS_DETECT		0x33
#define  TCPC_COMMAND_DISABLE_SINK_VBUS			0x44
#define  TCPC_COMMAND_SINK_VBUS				0x55
#define  TCPC_COMMAND_DISABLE_SRC_VBUS			0x66
#define  TCPC_COMMAND_SRC_VBUS_DEFAULT			0x77
#define  TCPC_COMMAND_SRC_VBUS_HIGH			0x88
#define  TCPC_COMMAND_LOOK4CONNECTION			0x99
#define  TCPC_COMMAND_RXONEMORE				0xAA
#define  TCPC_COMMAND_I2C_IDLE				0xFF
#define TCPC_DEV_CAP_1				0x24
#define TCPC_DEV_CAP_2				0x26
#define TCPC_STD_INPUT_CAP			0x28
#define TCPC_STD_OUTPUT_CAP			0x29
#define TCPC_MSG_HDR_INFO			0x2e
#define  TCPC_MSG_HDR_INFO_PWR_ROLE			(1 << 0)
#define  TCPC_MSG_HDR_INFO_PD_REV10			(0 << 1)
#define  TCPC_MSG_HDR_INFO_PD_REV20			(1 << 1)
#define  TCPC_MSG_HDR_INFO_DATA_ROLE			(1 << 3)
#define TCPC_RX_DETECT				0x2f
#define  TCPC_RX_DETECT_SOP				(1 << 0)
#define  TCPC_RX_DETECT_HARD_RESET			(1 << 5)
#define TCPC_RX_BYTE_CNT			0x30
#define TCPC_RX_BUF_FRAME_TYPE			0x31
#define TCPC_RX_HDR				0x32
#define TCPC_RX_DATA				0x34 /* through 0x4f */
#define TCPC_TRANSMIT				0x50
#define TCPC_TX_BYTE_CNT			0x51
#define TCPC_TX_HDR				0x52
#define TCPC_TX_DATA				0x54 /* through 0x6f */
#define TCPC_VBUS_VOLTAGE			0x70
#define TCPC_VBUS_SINK_DISCONNECT_THRESH	0x72
#define TCPC_VBUS_STOP_DISCHARGE_THRESH		0x74
#define TCPC_VBUS_VOLTAGE_ALARM_HI_CFG		0x76
#define TCPC_VBUS_VOLTAGE_ALARM_LO_CFG		0x78

enum typec_cc_status {
	TYPEC_CC_OPEN,
	TYPEC_CC_RA,
	TYPEC_CC_RD,
	TYPEC_CC_RP_DEF,
	TYPEC_CC_RP_1_5,
	TYPEC_CC_RP_3_0,
};

enum typec_data_role {
	TYPEC_DEVICE,
	TYPEC_HOST,
};

enum typec_power_role {
	TYPEC_SINK,
	TYPEC_SOURCE,
};

enum typec_polarity {
	TYPEC_POLARITY_CC1,
	TYPEC_POLARITY_CC2,
};

struct tcpci_softc {
	struct device		 sc_dev;
	i2c_tag_t		 sc_tag;
	i2c_addr_t		 sc_addr;
	int			 sc_node;
	void			*sc_ih;

	struct task		 sc_task;

	int			 sc_attached;
	enum typec_data_role	 sc_try_data;
	enum typec_power_role	 sc_try_power;

	uint32_t		*sc_ss_sel;
	uint8_t			 sc_cc;
	uint8_t			 sc_vbus_det;
};

int	 tcpci_match(struct device *, void *, void *);
void	 tcpci_attach(struct device *, struct device *, void *);
int	 tcpci_detach(struct device *, int);

int	 tcpci_intr(void *);
void	 tcpci_task(void *);
void	 tcpci_cc_change(struct tcpci_softc *);
void	 tcpci_power_change(struct tcpci_softc *);
void	 tcpci_set_polarity(struct tcpci_softc *, int);
void	 tcpci_set_vbus(struct tcpci_softc *, int, int);
void	 tcpci_set_roles(struct tcpci_softc *, enum typec_data_role,
	    enum typec_power_role);

void	 tcpci_write_reg16(struct tcpci_softc *, uint8_t, uint16_t);
uint16_t tcpci_read_reg16(struct tcpci_softc *, uint8_t);
void	 tcpci_write_reg8(struct tcpci_softc *, uint8_t, uint8_t);
uint8_t	 tcpci_read_reg8(struct tcpci_softc *, uint8_t);

const struct cfattach tcpci_ca = {
	sizeof(struct tcpci_softc),
	tcpci_match,
	tcpci_attach,
	tcpci_detach,
};

struct cfdriver tcpci_cd = {
	NULL, "tcpci", DV_DULL
};

int
tcpci_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "nxp,ptn5110") == 0)
		return 1;

	return 0;
}

void
tcpci_attach(struct device *parent, struct device *self, void *aux)
{
	struct tcpci_softc *sc = (struct tcpci_softc *)self;
	struct i2c_attach_args *ia = aux;
	int len;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	sc->sc_node = *(int *)ia->ia_cookie;

	/* Automatic DRP toggling should try first as ... */
	sc->sc_try_data = TYPEC_HOST;
	sc->sc_try_power = TYPEC_SOURCE;

	pinctrl_byname(sc->sc_node, "default");

	task_set(&sc->sc_task, tcpci_task, sc);
	sc->sc_ih = fdt_intr_establish(sc->sc_node, IPL_BIO,
	    tcpci_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": unable to establish interrupt\n");
		return;
	}

	len = OF_getproplen(sc->sc_node, "ss-sel-gpios");
	if (len > 0) {
		sc->sc_ss_sel = malloc(len, M_TEMP, M_WAITOK);
		OF_getpropintarray(sc->sc_node, "ss-sel-gpios",
		    sc->sc_ss_sel, len);
		gpio_controller_config_pin(sc->sc_ss_sel,
		    GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(sc->sc_ss_sel, 1);
	}

	tcpci_write_reg16(sc, TCPC_ALERT, 0xffff);
	tcpci_write_reg8(sc, TCPC_FAULT_STATUS, 0x80);
	tcpci_write_reg8(sc, TCPC_POWER_STATUS_MASK,
	    TCPC_POWER_STATUS_VBUS_PRES);
	tcpci_write_reg8(sc, TCPC_POWER_CTRL, tcpci_read_reg8(sc,
	    TCPC_POWER_CTRL) & ~TCPC_POWER_CTRL_DIS_VOL_ALARM);
	tcpci_write_reg16(sc, TCPC_ALERT_MASK,
	    TCPC_ALERT_TX_SUCCESS | TCPC_ALERT_TX_FAILED |
	    TCPC_ALERT_TX_DISCARDED | TCPC_ALERT_RX_STATUS |
	    TCPC_ALERT_RX_HARD_RST | TCPC_ALERT_CC_STATUS |
	    TCPC_ALERT_RX_BUF_OVF | TCPC_ALERT_FAULT |
	    TCPC_ALERT_V_ALARM_LO | TCPC_ALERT_POWER_STATUS);

	if (sc->sc_try_data == TYPEC_HOST)
		tcpci_write_reg8(sc, TCPC_ROLE_CTRL, TCPC_ROLE_CTRL_DRP | 0xa);
	else
		tcpci_write_reg8(sc, TCPC_ROLE_CTRL, TCPC_ROLE_CTRL_DRP | 0x5);
	tcpci_write_reg8(sc, TCPC_COMMAND, TCPC_COMMAND_LOOK4CONNECTION);

	printf("\n");
}

int
tcpci_detach(struct device *self, int flags)
{
	return 0;
}

int
tcpci_intr(void *args)
{
	struct tcpci_softc *sc = args;
	fdt_intr_disable(sc->sc_ih);
	task_add(systq, &sc->sc_task);
	return 1;
}

void
tcpci_task(void *args)
{
	struct tcpci_softc *sc = args;
	uint16_t status;

	status = tcpci_read_reg16(sc, TCPC_ALERT);
	tcpci_write_reg16(sc, TCPC_ALERT, status);

	DPRINTF(("%s: alert %x\n", __func__, status));

	if (status & TCPC_ALERT_CC_STATUS)
		tcpci_cc_change(sc);

	if (status & TCPC_ALERT_POWER_STATUS)
		tcpci_power_change(sc);

	if (status & TCPC_ALERT_V_ALARM_LO) {
		tcpci_write_reg8(sc, TCPC_VBUS_VOLTAGE_ALARM_LO_CFG, 0);
		tcpci_write_reg8(sc, TCPC_POWER_CTRL, tcpci_read_reg8(sc,
		    TCPC_POWER_CTRL) & ~TCPC_POWER_CTRL_FORCEDISCH);
	}

	if (status & TCPC_ALERT_FAULT)
		tcpci_write_reg8(sc, TCPC_FAULT_STATUS, tcpci_read_reg8(sc,
		    TCPC_FAULT_STATUS) | TCPC_FAULT_STATUS_CLEAR);

	fdt_intr_enable(sc->sc_ih);
}

uint8_t
tcpci_typec_to_rp(int typec)
{
	switch (typec) {
	case TYPEC_CC_RP_DEF:
		return TCPC_ROLE_CTRL_RP_VAL_DEF;
	case TYPEC_CC_RP_1_5:
		return TCPC_ROLE_CTRL_RP_VAL_1_5;
	case TYPEC_CC_RP_3_0:
		return TCPC_ROLE_CTRL_RP_VAL_3_0;
	default:
		panic("%s:%d", __func__, __LINE__);
	}
}

int
tcpci_cc_to_typec(int cc, int sink)
{
	if (sink) {
		if (cc == 0x1)
			return TYPEC_CC_RP_DEF;
		if (cc == 0x2)
			return TYPEC_CC_RP_1_5;
		if (cc == 0x3)
			return TYPEC_CC_RP_3_0;
	} else {
		if (cc == 0x1)
			return TYPEC_CC_RA;
		if (cc == 0x2)
			return TYPEC_CC_RD;
	}

	return TYPEC_CC_OPEN;
}

int
tcpci_cc_is_sink(int cc1, int cc2)
{
	if ((cc1 == TYPEC_CC_RP_DEF || cc1 == TYPEC_CC_RP_1_5 ||
	    cc1 == TYPEC_CC_RP_3_0) && cc2 == TYPEC_CC_OPEN)
		return 1;
	if ((cc2 == TYPEC_CC_RP_DEF || cc2 == TYPEC_CC_RP_1_5 ||
	    cc2 == TYPEC_CC_RP_3_0) && cc1 == TYPEC_CC_OPEN)
		return 1;
	return 0;
}

int
tcpci_cc_is_source(int cc1, int cc2)
{
	if (cc1 == TYPEC_CC_RD && cc2 != TYPEC_CC_RD)
		return 1;
	if (cc2 == TYPEC_CC_RD && cc1 != TYPEC_CC_RD)
		return 1;
	return 0;
}

int
tcpci_cc_is_audio(int cc1, int cc2)
{
	if (cc1 == TYPEC_CC_RA && cc2 == TYPEC_CC_RA)
		return 1;
	return 0;
}

int
tcpci_cc_is_audio_detached(int cc1, int cc2)
{
	if (cc1 == TYPEC_CC_RA && cc2 == TYPEC_CC_OPEN)
		return 1;
	if (cc2 == TYPEC_CC_RA && cc1 == TYPEC_CC_OPEN)
		return 1;
	return 0;
}

void
tcpci_cc_change(struct tcpci_softc *sc)
{
	uint8_t cc, cc1, cc2;

	cc = tcpci_read_reg8(sc, TCPC_CC_STATUS);
	if (sc->sc_cc == cc)
		return;

	cc1 = (cc >> TCPC_ROLE_CTRL_CC1_SHIFT) & TCPC_ROLE_CTRL_CC_MASK;
	cc1 = tcpci_cc_to_typec(cc1, cc & TCPC_CC_STATUS_TERM);
	cc2 = (cc >> TCPC_ROLE_CTRL_CC2_SHIFT) & TCPC_ROLE_CTRL_CC_MASK;
	cc2 = tcpci_cc_to_typec(cc2, cc & TCPC_CC_STATUS_TERM);

	if (cc1 == TYPEC_CC_OPEN && cc2 == TYPEC_CC_OPEN) {
		/* No CC, wait for new connection. */
		DPRINTF(("%s: disconnected\n", __func__));
		tcpci_write_reg8(sc, TCPC_RX_DETECT, 0);
		tcpci_set_vbus(sc, 0, 0);
		tcpci_write_reg8(sc, TCPC_POWER_CTRL, tcpci_read_reg8(sc,
		    TCPC_POWER_CTRL) & ~TCPC_POWER_CTRL_VCONN_ENABLE);
		tcpci_set_polarity(sc, TYPEC_POLARITY_CC1);
		if (sc->sc_try_data == TYPEC_HOST) {
			tcpci_write_reg8(sc, TCPC_ROLE_CTRL,
			    TCPC_ROLE_CTRL_DRP | 0xa);
		} else {
			tcpci_write_reg8(sc, TCPC_ROLE_CTRL,
			    TCPC_ROLE_CTRL_DRP | 0x5);
		}
		tcpci_write_reg8(sc, TCPC_COMMAND,
		    TCPC_COMMAND_LOOK4CONNECTION);
		sc->sc_attached = 0;
	} else if (tcpci_cc_is_source(cc1, cc2)) {
		/* Host */
		DPRINTF(("%s: attached as source\n", __func__));
		if (cc1 == TYPEC_CC_RD)
			tcpci_set_polarity(sc, TYPEC_POLARITY_CC1);
		else
			tcpci_set_polarity(sc, TYPEC_POLARITY_CC2);
		tcpci_set_roles(sc, TYPEC_HOST, TYPEC_SOURCE);
		tcpci_write_reg8(sc, TCPC_RX_DETECT,
		    TCPC_RX_DETECT_SOP | TCPC_RX_DETECT_HARD_RESET);
		if ((cc1 == TYPEC_CC_RD && cc2 == TYPEC_CC_RA) ||
		    (cc2 == TYPEC_CC_RD && cc1 == TYPEC_CC_RA))
			tcpci_write_reg8(sc, TCPC_POWER_CTRL, tcpci_read_reg8(sc,
			    TCPC_POWER_CTRL) | TCPC_POWER_CTRL_VCONN_ENABLE);
		tcpci_set_vbus(sc, 1, 0);
		sc->sc_attached = 1;
	} else if (tcpci_cc_is_sink(cc1, cc2)) {
		/* Device */
		DPRINTF(("%s: attached as sink\n", __func__));
		if (cc1 != TYPEC_CC_OPEN) {
			tcpci_set_polarity(sc, TYPEC_POLARITY_CC1);
			tcpci_write_reg8(sc, TCPC_ROLE_CTRL,
			    TCPC_ROLE_CTRL_CC_RD << TCPC_ROLE_CTRL_CC1_SHIFT |
			    TCPC_ROLE_CTRL_CC_OPEN << TCPC_ROLE_CTRL_CC2_SHIFT);
		} else {
			tcpci_set_polarity(sc, TYPEC_POLARITY_CC2);
			tcpci_write_reg8(sc, TCPC_ROLE_CTRL,
			    TCPC_ROLE_CTRL_CC_OPEN << TCPC_ROLE_CTRL_CC1_SHIFT |
			    TCPC_ROLE_CTRL_CC_RD << TCPC_ROLE_CTRL_CC2_SHIFT);
		}
		tcpci_set_roles(sc, TYPEC_DEVICE, TYPEC_SINK);
		tcpci_set_vbus(sc, 0, 0);
		sc->sc_attached = 1;
	} else if (tcpci_cc_is_audio_detached(cc1, cc2)) {
		/* Audio Detached */
		DPRINTF(("%s: audio detached\n", __func__));
	} else {
		panic("%s: unknown combination cc %x", __func__, cc);
	}

	sc->sc_cc = cc;
}

void
tcpci_power_change(struct tcpci_softc *sc)
{
	uint8_t power;

	if (tcpci_read_reg8(sc, TCPC_POWER_STATUS_MASK) == 0xff)
		DPRINTF(("%s: power reset\n", __func__));

	power = tcpci_read_reg8(sc, TCPC_POWER_STATUS);
	power &= TCPC_POWER_STATUS_VBUS_PRES;
	if (sc->sc_vbus_det == power)
		return;

	DPRINTF(("%s: power %d\n", __func__, power));
	sc->sc_vbus_det = power;
}

void
tcpci_set_roles(struct tcpci_softc *sc, enum typec_data_role data,
    enum typec_power_role power)
{
	uint8_t reg;

	reg = TCPC_MSG_HDR_INFO_PD_REV20;
	if (power == TYPEC_SOURCE)
		reg |= TCPC_MSG_HDR_INFO_PWR_ROLE;
	if (data == TYPEC_HOST)
		reg |= TCPC_MSG_HDR_INFO_DATA_ROLE;

	tcpci_write_reg8(sc, TCPC_MSG_HDR_INFO, reg);

	if (data == TYPEC_HOST)
		printf("%s: connected in host mode\n",
		    sc->sc_dev.dv_xname);
	else
		printf("%s: connected in device mode\n",
		    sc->sc_dev.dv_xname);
}

void
tcpci_set_polarity(struct tcpci_softc *sc, int cc)
{
	if (cc == TYPEC_POLARITY_CC1) {
		tcpci_write_reg8(sc, TCPC_TCPC_CTRL, 0);
		if (sc->sc_ss_sel)
			gpio_controller_set_pin(sc->sc_ss_sel, 1);
	}
	if (cc == TYPEC_POLARITY_CC2) {
		tcpci_write_reg8(sc, TCPC_TCPC_CTRL,
		    TCPC_TCPC_CTRL_ORIENTATION);
		if (sc->sc_ss_sel)
			gpio_controller_set_pin(sc->sc_ss_sel, 0);
	}
}

void
tcpci_set_vbus(struct tcpci_softc *sc, int source, int sink)
{
	if (!source)
		tcpci_write_reg8(sc, TCPC_COMMAND,
		    TCPC_COMMAND_DISABLE_SRC_VBUS);

	if (!sink)
		tcpci_write_reg8(sc, TCPC_COMMAND,
		    TCPC_COMMAND_DISABLE_SINK_VBUS);

	if (!source && !sink) {
		tcpci_write_reg8(sc, TCPC_VBUS_VOLTAGE_ALARM_LO_CFG, 0x1c);
		tcpci_write_reg8(sc, TCPC_POWER_CTRL, tcpci_read_reg8(sc,
		    TCPC_POWER_CTRL) | TCPC_POWER_CTRL_FORCEDISCH);
	}

	if (source)
		tcpci_write_reg8(sc, TCPC_COMMAND,
		    TCPC_COMMAND_SRC_VBUS_DEFAULT);

	if (sink)
		tcpci_write_reg8(sc, TCPC_COMMAND,
		    TCPC_COMMAND_SINK_VBUS);
}

uint8_t
tcpci_read_reg8(struct tcpci_softc *sc, uint8_t reg)
{
	uint8_t val = 0;

	iic_acquire_bus(sc->sc_tag, 0);
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &reg, sizeof(reg), &val, sizeof(val), 0)) {
		printf("%s: cannot read register 0x%x\n",
		    sc->sc_dev.dv_xname, reg);
	}
	iic_release_bus(sc->sc_tag, 0);

	return val;
}

void
tcpci_write_reg8(struct tcpci_softc *sc, uint8_t reg, uint8_t val)
{
	iic_acquire_bus(sc->sc_tag, 0);
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &reg, sizeof(reg), &val, sizeof(val), 0)) {
		printf("%s: cannot write register 0x%x\n",
		    sc->sc_dev.dv_xname, reg);
	}
	iic_release_bus(sc->sc_tag, 0);
}

uint16_t
tcpci_read_reg16(struct tcpci_softc *sc, uint8_t reg)
{
	uint16_t val = 0;

	iic_acquire_bus(sc->sc_tag, 0);
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP,
	    sc->sc_addr, &reg, sizeof(reg), &val, sizeof(val), 0)) {
		printf("%s: cannot read register 0x%x\n",
		    sc->sc_dev.dv_xname, reg);
	}
	iic_release_bus(sc->sc_tag, 0);

	return val;
}

void
tcpci_write_reg16(struct tcpci_softc *sc, uint8_t reg, uint16_t val)
{
	iic_acquire_bus(sc->sc_tag, 0);
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &reg, sizeof(reg), &val, sizeof(val), 0)) {
		printf("%s: cannot write register 0x%x\n",
		    sc->sc_dev.dv_xname, reg);
	}
	iic_release_bus(sc->sc_tag, 0);
}
