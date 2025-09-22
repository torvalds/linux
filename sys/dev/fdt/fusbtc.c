/* $OpenBSD: fusbtc.c,v 1.2 2021/10/24 17:52:26 mpi Exp $ */
/*
 * Copyright (c) 2019 Patrick Wildt <patrick@blueri.se>
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
#include <sys/timeout.h>
#include <sys/task.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/i2c/i2cvar.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_regulator.h>

/* #define FUSB_DEBUG */
#define FUSB_DEBUG

#ifdef FUSB_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

#define FUSB_DEVICE_ID				0x01
#define FUSB_SWITCHES0				0x02
#define  FUSB_SWITCHES0_PD_EN1				(1 << 0)
#define  FUSB_SWITCHES0_PD_EN2				(1 << 1)
#define  FUSB_SWITCHES0_MEAS_CC1			(1 << 2)
#define  FUSB_SWITCHES0_MEAS_CC2			(1 << 3)
#define  FUSB_SWITCHES0_VCONN_CC1			(1 << 4)
#define  FUSB_SWITCHES0_VCONN_CC2			(1 << 5)
#define  FUSB_SWITCHES0_PU_EN1				(1 << 6)
#define  FUSB_SWITCHES0_PU_EN2				(1 << 7)
#define FUSB_SWITCHES1				0x03
#define  FUSB_SWITCHES1_TXCC1				(1 << 0)
#define  FUSB_SWITCHES1_TXCC2				(1 << 1)
#define  FUSB_SWITCHES1_AUTO_CRC			(1 << 2)
#define  FUSB_SWITCHES1_DATAROLE			(1 << 4)
#define  FUSB_SWITCHES1_SPECREV0			(1 << 5)
#define  FUSB_SWITCHES1_SPECREV1			(1 << 6)
#define  FUSB_SWITCHES1_POWERROLE			(1 << 7)
#define FUSB_MEASURE				0x04
#define FUSB_SLICE				0x05
#define FUSB_CONTROL0				0x06
#define  FUSB_CONTROL0_HOST_CUR0			(1 << 2)
#define  FUSB_CONTROL0_HOST_CUR1			(1 << 3)
#define  FUSB_CONTROL0_INT_MASK				(1 << 5)
#define FUSB_CONTROL1				0x07
#define FUSB_CONTROL2				0x08
#define  FUSB_CONTROL2_TOGGLE				(1 << 0)
#define  FUSB_CONTROL2_MODE_NONE			(0 << 1)
#define  FUSB_CONTROL2_MODE_DRP				(1 << 1)
#define  FUSB_CONTROL2_MODE_SNK				(2 << 1)
#define  FUSB_CONTROL2_MODE_SRC				(3 << 1)
#define  FUSB_CONTROL2_MODE_MASK			(0x3 << 1)
#define FUSB_CONTROL3				0x09
#define  FUSB_CONTROL3_AUTO_RETRY			(1 << 0)
#define  FUSB_CONTROL3_N_RETRIES(x)			(((x) & 0x3) << 1)
#define FUSB_MASK				0x0a
#define  FUSB_MASK_BC_LVL				(1 << 0)
#define  FUSB_MASK_COLLISION				(1 << 1)
#define  FUSB_MASK_WAKE				(1 << 2)
#define  FUSB_MASK_ALERT				(1 << 3)
#define  FUSB_MASK_CRC_CHK				(1 << 4)
#define  FUSB_MASK_COMP_CHNG				(1 << 5)
#define  FUSB_MASK_ACTIVITY				(1 << 6)
#define  FUSB_MASK_VBUSOK				(1 << 7)
#define FUSB_POWER				0x0b
#define  FUSB_POWER_ALL					(0x7 << 0)
#define FUSB_RESET				0x0c
#define  FUSB_RESET_SW					(1 << 0)
#define  FUSB_RESET_PD					(1 << 1)
#define FUSB_OCPREG				0x0d
#define FUSB_MASKA				0x0e
#define  FUSB_MASKA_HARDRST				(1 << 0)
#define  FUSB_MASKA_SOFTRST				(1 << 1)
#define  FUSB_MASKA_TXSENT				(1 << 2)
#define  FUSB_MASKA_HARDSENT				(1 << 3)
#define  FUSB_MASKA_RETRYFAIL				(1 << 4)
#define  FUSB_MASKA_SOFTFAIL				(1 << 5)
#define  FUSB_MASKA_TOGDONE				(1 << 6)
#define  FUSB_MASKA_OCP_TEMP				(1 << 7)
#define FUSB_MASKB				0x0f
#define  FUSB_MASKB_GCRCSENT				(1 << 0)
#define FUSB_CONTROL4				0x10
#define FUSB_STATUS0A				0x3c
#define FUSB_STATUS1A				0x3d
#define  FUSB_STATUS1A_TOGSS_RUNNING			(0x0 << 3)
#define  FUSB_STATUS1A_TOGSS_CC1(x)			(((x) & (0x1 << 3)) != 0)
#define  FUSB_STATUS1A_TOGSS_CC2(x)			(((x) & (0x1 << 3)) == 0)
#define  FUSB_STATUS1A_TOGSS_SRC1			(0x1 << 3)
#define  FUSB_STATUS1A_TOGSS_SRC2			(0x2 << 3)
#define  FUSB_STATUS1A_TOGSS_SNK1			(0x5 << 3)
#define  FUSB_STATUS1A_TOGSS_SNK2			(0x6 << 3)
#define  FUSB_STATUS1A_TOGSS_AA				(0x7 << 3)
#define  FUSB_STATUS1A_TOGSS_MASK			(0x7 << 3)
#define FUSB_INTERRUPTA				0x3e
#define  FUSB_INTERRUPTA_HARDRST			(1 << 0)
#define  FUSB_INTERRUPTA_SOFTRST			(1 << 1)
#define  FUSB_INTERRUPTA_TXSENT				(1 << 2)
#define  FUSB_INTERRUPTA_HARDSENT			(1 << 3)
#define  FUSB_INTERRUPTA_RETRYFAIL			(1 << 4)
#define  FUSB_INTERRUPTA_SOFTFAIL			(1 << 5)
#define  FUSB_INTERRUPTA_TOGDONE			(1 << 6)
#define  FUSB_INTERRUPTA_OCP_TEMP			(1 << 7)
#define FUSB_INTERRUPTB				0x3f
#define  FUSB_INTERRUPTB_GCRCSENT			(1 << 0)
#define FUSB_STATUS0				0x40
#define  FUSB_STATUS0_BC_LVL_0_200			(0x0 << 0)
#define  FUSB_STATUS0_BC_LVL_200_600			(0x1 << 0)
#define  FUSB_STATUS0_BC_LVL_600_1230			(0x2 << 0)
#define  FUSB_STATUS0_BC_LVL_1230_MAX			(0x3 << 0)
#define  FUSB_STATUS0_BC_LVL_MASK			(0x3 << 0)
#define  FUSB_STATUS0_COMP				(1 << 5)
#define  FUSB_STATUS0_ACTIVITY				(1 << 6)
#define  FUSB_STATUS0_VBUSOK				(1 << 7)
#define FUSB_STATUS1				0x41
#define FUSB_INTERRUPT				0x42
#define  FUSB_INTERRUPT_BC_LVL				(1 << 0)
#define  FUSB_INTERRUPT_COLLISION			(1 << 1)
#define  FUSB_INTERRUPT_WAKE				(1 << 2)
#define  FUSB_INTERRUPT_ALERT				(1 << 3)
#define  FUSB_INTERRUPT_CRC_CHK				(1 << 4)
#define  FUSB_INTERRUPT_COMP_CHNG			(1 << 5)
#define  FUSB_INTERRUPT_ACTIVITY			(1 << 6)
#define  FUSB_INTERRUPT_VBUSOK				(1 << 7)
#define FUSB_FIFOS				0x43

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

enum fusbtc_src_current_mode {
	SRC_CURRENT_DEFAULT,
	SRC_CURRENT_MEDIUM,
	SRC_CURRENT_HIGH,
};

uint8_t fusbtc_ra_mda[] = {
	[SRC_CURRENT_DEFAULT] = 4,	/* 210 mV */
	[SRC_CURRENT_MEDIUM] = 9,	/* 420 mV */
	[SRC_CURRENT_HIGH] = 18,	/* 798 mV */
};

uint8_t fusbtc_rd_mda[] = {
	[SRC_CURRENT_DEFAULT] = 38,	/* 1638 mV */
	[SRC_CURRENT_MEDIUM] = 38,	/* 1638 mV */
	[SRC_CURRENT_HIGH] = 61,	/* 2604 mV */
};

struct fusbtc_softc {
	struct device		 sc_dev;
	i2c_tag_t		 sc_tag;
	i2c_addr_t		 sc_addr;
	int			 sc_node;
	void			*sc_ih;
	struct task		 sc_task;

	int			 sc_attached;
	uint8_t			 sc_drp_mode;
	int			 sc_data_role;
	int			 sc_power_role;

	uint32_t		*sc_ss_sel;

	int			 sc_vbus;
	uint8_t			 sc_vbus_det;

	struct timeout		 sc_bclvl_tmo;
};

int	 fusbtc_match(struct device *, void *, void *);
void	 fusbtc_attach(struct device *, struct device *, void *);
int	 fusbtc_detach(struct device *, int);

int	 fusbtc_intr(void *);
void	 fusbtc_task(void *);
void	 fusbtc_toggle(struct fusbtc_softc *, int);
void	 fusbtc_toggle_change(struct fusbtc_softc *);
void	 fusbtc_power_change(struct fusbtc_softc *);
void	 fusbtc_bclvl_change(void *args);
void	 fusbtc_comp_change(struct fusbtc_softc *);
void	 fusbtc_set_polarity(struct fusbtc_softc *, int);
void	 fusbtc_set_vbus(struct fusbtc_softc *, int, int);
void	 fusbtc_set_roles(struct fusbtc_softc *, enum typec_data_role,
	    enum typec_power_role);
void	 fusbtc_set_cc_pull(struct fusbtc_softc *, int, int, int);

void	 fusbtc_set_reg(struct fusbtc_softc *, uint8_t, uint8_t);
void	 fusbtc_clr_reg(struct fusbtc_softc *, uint8_t, uint8_t);

void	 fusbtc_write_reg(struct fusbtc_softc *, uint8_t, uint8_t);
uint8_t	 fusbtc_read_reg(struct fusbtc_softc *, uint8_t);

const struct cfattach fusbtc_ca = {
	sizeof(struct fusbtc_softc),
	fusbtc_match,
	fusbtc_attach,
	fusbtc_detach,
};

struct cfdriver fusbtc_cd = {
	NULL, "fusbtc", DV_DULL
};

int
fusbtc_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "fcs,fusb302") == 0)
		return 1;

	return 0;
}

void
fusbtc_attach(struct device *parent, struct device *self, void *aux)
{
	struct fusbtc_softc *sc = (struct fusbtc_softc *)self;
	struct i2c_attach_args *ia = aux;
	uint8_t reg;
	char *role;
	int child;
	int len;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	sc->sc_node = *(int *)ia->ia_cookie;

	sc->sc_vbus = OF_getpropint(sc->sc_node, "vbus-supply", 0);

	sc->sc_drp_mode = FUSB_CONTROL2_MODE_NONE;
	for (child = OF_child(sc->sc_node); child != 0; child = OF_peer(child)){
		if (!OF_is_compatible(child, "usb-c-connector"))
			continue;
		len = OF_getproplen(child, "power-role");
		role = malloc(len, M_TEMP, M_WAITOK);
		OF_getprop(child, "power-role", role, len);
		if (!strcmp(role, "dual"))
			sc->sc_drp_mode = FUSB_CONTROL2_MODE_DRP;
		if (!strcmp(role, "sink"))
			sc->sc_drp_mode = FUSB_CONTROL2_MODE_SNK;
		if (!strcmp(role, "source"))
			sc->sc_drp_mode = FUSB_CONTROL2_MODE_SRC;
		free(role, M_TEMP, len);
	}
	/* For broken device trees without children. */
	if (sc->sc_drp_mode == FUSB_CONTROL2_MODE_NONE &&
	    sc->sc_vbus)
		sc->sc_drp_mode = FUSB_CONTROL2_MODE_SRC;
	if (sc->sc_drp_mode == FUSB_CONTROL2_MODE_NONE) {
		printf(": no USB-C connector defined\n");
		return;
	}

	timeout_set_proc(&sc->sc_bclvl_tmo, fusbtc_bclvl_change, sc);

	pinctrl_byname(sc->sc_node, "default");

	task_set(&sc->sc_task, fusbtc_task, sc);
	sc->sc_ih = fdt_intr_establish(sc->sc_node, IPL_BIO,
	    fusbtc_intr, sc, sc->sc_dev.dv_xname);
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

	fusbtc_write_reg(sc, FUSB_RESET, FUSB_RESET_SW);
	reg = fusbtc_read_reg(sc, FUSB_CONTROL3);
	reg |= FUSB_CONTROL3_AUTO_RETRY;
	reg |= FUSB_CONTROL3_N_RETRIES(3);
	fusbtc_write_reg(sc, FUSB_CONTROL3, reg);
	fusbtc_write_reg(sc, FUSB_MASK, ~FUSB_MASK_VBUSOK);
	fusbtc_write_reg(sc, FUSB_MASKA, ~FUSB_MASKA_TOGDONE);
	fusbtc_write_reg(sc, FUSB_MASKB, 0xff);
	reg = fusbtc_read_reg(sc, FUSB_CONTROL0);
	reg &= ~FUSB_CONTROL0_INT_MASK;
	fusbtc_write_reg(sc, FUSB_CONTROL0, reg);
	fusbtc_write_reg(sc, FUSB_POWER, FUSB_POWER_ALL);

	sc->sc_vbus_det =
	    fusbtc_read_reg(sc, FUSB_STATUS0) & FUSB_STATUS0_VBUSOK;
	fusbtc_toggle(sc, 1);

	printf("\n");
}

int
fusbtc_detach(struct device *self, int flags)
{
	return 0;
}

int
fusbtc_intr(void *args)
{
	struct fusbtc_softc *sc = args;
	fdt_intr_disable(sc->sc_ih);
	task_add(systq, &sc->sc_task);
	return 1;
}

void
fusbtc_task(void *args)
{
	struct fusbtc_softc *sc = args;
	uint8_t intr, intra, intrb;

	intr = fusbtc_read_reg(sc, FUSB_INTERRUPT);
	intra = fusbtc_read_reg(sc, FUSB_INTERRUPTA);
	intrb = fusbtc_read_reg(sc, FUSB_INTERRUPTB);

	if (intr & FUSB_INTERRUPT_VBUSOK)
		fusbtc_power_change(sc);

	if (intra & FUSB_INTERRUPTA_TOGDONE)
		fusbtc_toggle_change(sc);

	if (intr & FUSB_INTERRUPT_BC_LVL)
		timeout_add_msec(&sc->sc_bclvl_tmo, 30);

	if (intr & FUSB_INTERRUPT_COMP_CHNG)
		fusbtc_comp_change(sc);

	fdt_intr_enable(sc->sc_ih);
}

void
fusbtc_toggle(struct fusbtc_softc *sc, int on)
{
	uint8_t reg;

	fusbtc_clr_reg(sc, FUSB_CONTROL2,
	    FUSB_CONTROL2_MODE_MASK | FUSB_CONTROL2_TOGGLE);
	fusbtc_set_reg(sc, FUSB_MASK,
	    FUSB_MASK_BC_LVL | FUSB_MASK_COMP_CHNG);
	fusbtc_set_reg(sc, FUSB_MASKA, FUSB_MASKA_TOGDONE);

	if (on) {
		reg = fusbtc_read_reg(sc, FUSB_CONTROL0);
		reg |= FUSB_CONTROL0_HOST_CUR0;
		reg &= ~FUSB_CONTROL0_HOST_CUR1;
		fusbtc_write_reg(sc, FUSB_CONTROL0, reg);
		reg = fusbtc_read_reg(sc, FUSB_SWITCHES0);
		reg &= ~(FUSB_SWITCHES0_VCONN_CC1 | FUSB_SWITCHES0_VCONN_CC2);
		fusbtc_write_reg(sc, FUSB_SWITCHES0, reg);

		fusbtc_clr_reg(sc, FUSB_MASKA, FUSB_MASKA_TOGDONE);
		fusbtc_set_reg(sc, FUSB_CONTROL2,
		    sc->sc_drp_mode | FUSB_CONTROL2_TOGGLE);
	}
}

int
fusbtc_bclvl_to_typec(uint8_t bclvl)
{
	if (bclvl == FUSB_STATUS0_BC_LVL_200_600)
		return TYPEC_CC_RP_DEF;
	if (bclvl == FUSB_STATUS0_BC_LVL_600_1230)
		return TYPEC_CC_RP_1_5;
	if (bclvl == FUSB_STATUS0_BC_LVL_1230_MAX)
		return TYPEC_CC_RP_3_0;
	return TYPEC_CC_OPEN;
}

void
fusbtc_toggle_change(struct fusbtc_softc *sc)
{
	uint8_t cc, reg;
	uint8_t status;
	int pol;

	status = fusbtc_read_reg(sc, FUSB_STATUS1A);
	status &= FUSB_STATUS1A_TOGSS_MASK;

	if (FUSB_STATUS1A_TOGSS_CC1(status))
		pol = TYPEC_POLARITY_CC1;
	else
		pol = TYPEC_POLARITY_CC2;

	if (status == FUSB_STATUS1A_TOGSS_SRC1 ||
	    status == FUSB_STATUS1A_TOGSS_SRC2) {
		/* Host */
		DPRINTF(("%s: attached (source)\n", sc->sc_dev.dv_xname));
		fusbtc_set_cc_pull(sc, pol, 1, 0);
		fusbtc_set_polarity(sc, pol);
		fusbtc_write_reg(sc, FUSB_MEASURE,
		    fusbtc_rd_mda[SRC_CURRENT_DEFAULT]);
		delay(100);
		reg = fusbtc_read_reg(sc, FUSB_STATUS0);
		cc = TYPEC_CC_OPEN;
		if ((reg & FUSB_STATUS0_COMP) == 0) {
			fusbtc_write_reg(sc, FUSB_MEASURE,
			    fusbtc_ra_mda[SRC_CURRENT_DEFAULT]);
			delay(100);
			reg = fusbtc_read_reg(sc, FUSB_STATUS0);
			cc = TYPEC_CC_RD;
			if ((reg & FUSB_STATUS0_COMP) == 0)
				cc = TYPEC_CC_RA;
		}
		if (cc == TYPEC_CC_OPEN) {
			fusbtc_toggle(sc, 1);
			return;
		}
		fusbtc_toggle(sc, 0);
		fusbtc_write_reg(sc, FUSB_MEASURE,
		    fusbtc_rd_mda[SRC_CURRENT_DEFAULT]);
		fusbtc_clr_reg(sc, FUSB_MASK, FUSB_MASK_COMP_CHNG);
		fusbtc_set_roles(sc, TYPEC_HOST, TYPEC_SOURCE);
		fusbtc_set_vbus(sc, 1, 0);
		sc->sc_attached = 1;
	} else if (status == FUSB_STATUS1A_TOGSS_SNK1 ||
	    status == FUSB_STATUS1A_TOGSS_SNK2) {
		/* Device */
		DPRINTF(("%s: attached (sink)\n", sc->sc_dev.dv_xname));
		fusbtc_set_cc_pull(sc, pol, 0, 1);
		fusbtc_set_polarity(sc, pol);
		reg = fusbtc_read_reg(sc, FUSB_STATUS0);
		reg &= FUSB_STATUS0_BC_LVL_MASK;
		if (fusbtc_bclvl_to_typec(reg) == TYPEC_CC_OPEN) {
			fusbtc_toggle(sc, 1);
			return;
		}
		fusbtc_toggle(sc, 0);
		fusbtc_clr_reg(sc, FUSB_MASK, FUSB_MASK_BC_LVL);
		fusbtc_set_roles(sc, TYPEC_DEVICE, TYPEC_SINK);
		fusbtc_set_vbus(sc, 0, 0);
		sc->sc_attached = 1;
	} else {
		panic("%s: unknown combination %x", sc->sc_dev.dv_xname,
		   status);
	}
}

void
fusbtc_power_change(struct fusbtc_softc *sc)
{
	uint8_t power;

	power = fusbtc_read_reg(sc, FUSB_STATUS0);
	power &= FUSB_STATUS0_VBUSOK;
	if (sc->sc_vbus_det == power)
		return;

	sc->sc_vbus_det = power;

	if (!sc->sc_vbus_det) {
		DPRINTF(("%s: detached (vbus)\n", sc->sc_dev.dv_xname));
		sc->sc_attached = 0;
		fusbtc_toggle(sc, 1);
	}
}

void
fusbtc_bclvl_change(void *args)
{
	struct fusbtc_softc *sc = args;
	uint8_t bc;

	if (!sc->sc_attached || sc->sc_power_role == TYPEC_SOURCE)
		return;

	bc = fusbtc_read_reg(sc, FUSB_STATUS0);
	if (bc & FUSB_STATUS0_ACTIVITY) {
		timeout_add_msec(&sc->sc_bclvl_tmo, 30);
		return;
	}

	bc &= FUSB_STATUS0_BC_LVL_MASK;
	bc = fusbtc_bclvl_to_typec(bc);

	switch (bc) {
	case TYPEC_CC_OPEN:
		printf("%s: can draw 0 mA\n", sc->sc_dev.dv_xname);
		break;
	case TYPEC_CC_RP_DEF:
		printf("%s: can draw 500 mA\n", sc->sc_dev.dv_xname);
		break;
	case TYPEC_CC_RP_1_5:
		printf("%s: can draw 1500 mA\n", sc->sc_dev.dv_xname);
		break;
	case TYPEC_CC_RP_3_0:
		printf("%s: can draw 3000 mA\n", sc->sc_dev.dv_xname);
		break;
	}
}

void
fusbtc_comp_change(struct fusbtc_softc *sc)
{
	uint8_t reg;

	if (!sc->sc_attached || sc->sc_power_role == TYPEC_SINK)
		return;

	reg = fusbtc_read_reg(sc, FUSB_STATUS0);
	if ((reg & FUSB_STATUS0_COMP) == 0)
		return;

	DPRINTF(("%s: detached (comp)\n", sc->sc_dev.dv_xname));
	fusbtc_set_vbus(sc, 0, 0);
	sc->sc_attached = 0;
	fusbtc_toggle(sc, 1);
}

void
fusbtc_set_roles(struct fusbtc_softc *sc, enum typec_data_role data,
    enum typec_power_role power)
{
	uint8_t reg;

	reg = fusbtc_read_reg(sc, FUSB_SWITCHES1);
	reg &= ~(FUSB_SWITCHES1_POWERROLE | FUSB_SWITCHES1_DATAROLE);
	if (power == TYPEC_SOURCE)
		reg |= FUSB_SWITCHES1_POWERROLE;
	if (data == TYPEC_HOST)
		reg |= FUSB_SWITCHES1_DATAROLE;
	fusbtc_write_reg(sc, FUSB_SWITCHES1, reg);

	if (data == TYPEC_HOST)
		printf("%s: connected in host mode\n",
		    sc->sc_dev.dv_xname);
	else
		printf("%s: connected in device mode\n",
		    sc->sc_dev.dv_xname);

	sc->sc_data_role = data;
	sc->sc_power_role = power;
}

void
fusbtc_set_cc_pull(struct fusbtc_softc *sc, int pol, int up, int down)
{
	uint8_t reg;

	reg = fusbtc_read_reg(sc, FUSB_SWITCHES0);
	reg &= ~(FUSB_SWITCHES0_PU_EN1 | FUSB_SWITCHES0_PU_EN2);
	reg &= ~(FUSB_SWITCHES0_PD_EN1 | FUSB_SWITCHES0_PD_EN2);
	if (up) { /* host mode */
		if (pol == TYPEC_POLARITY_CC1)
			reg |= FUSB_SWITCHES0_PU_EN1;
		else
			reg |= FUSB_SWITCHES0_PU_EN2;
	}
	if (down) { /* device mode */
		if (pol == TYPEC_POLARITY_CC1)
			reg |= FUSB_SWITCHES0_PD_EN1;
		else
			reg |= FUSB_SWITCHES0_PD_EN2;
	}
	fusbtc_write_reg(sc, FUSB_SWITCHES0, reg);
}

void
fusbtc_set_polarity(struct fusbtc_softc *sc, int pol)
{
	uint8_t reg;

	reg = fusbtc_read_reg(sc, FUSB_SWITCHES0);
	reg &= ~(FUSB_SWITCHES0_MEAS_CC1 | FUSB_SWITCHES0_MEAS_CC2);
	reg &= ~(FUSB_SWITCHES0_VCONN_CC1 | FUSB_SWITCHES0_VCONN_CC2);
	if (pol == TYPEC_POLARITY_CC1)
		reg |= FUSB_SWITCHES0_MEAS_CC1;
	else
		reg |= FUSB_SWITCHES0_MEAS_CC2;
	fusbtc_write_reg(sc, FUSB_SWITCHES0, reg);

	reg = fusbtc_read_reg(sc, FUSB_SWITCHES1);
	reg &= ~(FUSB_SWITCHES1_TXCC1 | FUSB_SWITCHES1_TXCC2);
	if (pol == TYPEC_POLARITY_CC1)
		reg |= FUSB_SWITCHES1_TXCC1;
	else
		reg |= FUSB_SWITCHES1_TXCC2;
	fusbtc_write_reg(sc, FUSB_SWITCHES1, reg);

	if (sc->sc_ss_sel) {
		if (pol == TYPEC_POLARITY_CC1)
			gpio_controller_set_pin(sc->sc_ss_sel, 1);
		else
			gpio_controller_set_pin(sc->sc_ss_sel, 0);
	}
}

void
fusbtc_set_vbus(struct fusbtc_softc *sc, int source, int sink)
{
	if (source)
		regulator_enable(sc->sc_vbus);
	else
		regulator_disable(sc->sc_vbus);
}

void
fusbtc_set_reg(struct fusbtc_softc *sc, uint8_t off, uint8_t val)
{
	uint8_t reg;
	reg = fusbtc_read_reg(sc, off);
	reg |= val;
	fusbtc_write_reg(sc, off, reg);
}

void
fusbtc_clr_reg(struct fusbtc_softc *sc, uint8_t off, uint8_t val)
{
	uint8_t reg;
	reg = fusbtc_read_reg(sc, off);
	reg &= ~val;
	fusbtc_write_reg(sc, off, reg);
}

uint8_t
fusbtc_read_reg(struct fusbtc_softc *sc, uint8_t reg)
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
fusbtc_write_reg(struct fusbtc_softc *sc, uint8_t reg, uint8_t val)
{
	iic_acquire_bus(sc->sc_tag, 0);
	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP,
	    sc->sc_addr, &reg, sizeof(reg), &val, sizeof(val), 0)) {
		printf("%s: cannot write register 0x%x\n",
		    sc->sc_dev.dv_xname, reg);
	}
	iic_release_bus(sc->sc_tag, 0);
}
