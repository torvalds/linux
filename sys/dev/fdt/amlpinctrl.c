/*	$OpenBSD: amlpinctrl.c,v 1.12 2022/06/28 23:43:12 naddy Exp $	*/
/*
 * Copyright (c) 2019 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#define BIAS_DISABLE	0x00
#define BIAS_PULL_UP	0x01
#define BIAS_PULL_DOWN	0x02

#define GPIOZ_0		0
#define GPIOZ_1		1
#define GPIOZ_7		7
#define GPIOZ_8		8
#define GPIOZ_14	14
#define GPIOZ_15	15
#define GPIOH_0		16
#define GPIOH_1		17
#define GPIOH_2		18
#define GPIOH_3		19
#define GPIOH_5		21
#define GPIOH_6		22
#define GPIOH_7		23
#define BOOT_0		25
#define BOOT_1		26
#define BOOT_2		27
#define BOOT_3		28
#define BOOT_4		29
#define BOOT_5		30
#define BOOT_6		31
#define BOOT_7		32
#define BOOT_8		33
#define BOOT_10		35
#define BOOT_13		38
#define GPIOC_0		41
#define GPIOC_1		42
#define GPIOC_2		43
#define GPIOC_3		44
#define GPIOC_4		45
#define GPIOC_5		46
#define GPIOC_6		47
#define GPIOA_0		49
#define GPIOA_14	63
#define GPIOA_15	64
#define GPIOX_0		65
#define GPIOX_3		68
#define GPIOX_5		70
#define GPIOX_6		71
#define GPIOX_7		72
#define GPIOX_8		73
#define GPIOX_10	75
#define GPIOX_11	76
#define GPIOX_16	81
#define GPIOX_17	82
#define GPIOX_18	83
#define GPIOX_19	84

#define GPIOAO_0	0
#define GPIOAO_1	1
#define GPIOAO_3	3
#define GPIOAO_4	4
#define GPIOAO_5	5
#define GPIOAO_6	6
#define GPIOAO_10	10
#define GPIOAO_11	11
#define GPIOE_0		12
#define GPIOE_1		13
#define GPIOE_2		14

#define PERIPHS_PIN_MUX_0		0xb0
#define PERIPHS_PIN_MUX_3		0xb3
#define PERIPHS_PIN_MUX_6		0xb6
#define PERIPHS_PIN_MUX_9		0xb9
#define PERIPHS_PIN_MUX_B		0xbb
#define PERIPHS_PIN_MUX_D		0xbd
#define PREG_PAD_GPIO0_EN_N		0x10
#define PREG_PAD_GPIO0_O		0x11
#define PREG_PAD_GPIO0_I		0x12
#define PREG_PAD_GPIO1_EN_N		0x13
#define PREG_PAD_GPIO1_O		0x14
#define PREG_PAD_GPIO1_I		0x15
#define PREG_PAD_GPIO2_EN_N		0x16
#define PREG_PAD_GPIO2_O		0x16
#define PREG_PAD_GPIO2_I		0x18
#define PREG_PAD_GPIO3_EN_N		0x19
#define PREG_PAD_GPIO3_O		0x1a
#define PREG_PAD_GPIO3_I		0x1b
#define PREG_PAD_GPIO4_EN_N		0x1c
#define PREG_PAD_GPIO4_O		0x1d
#define PREG_PAD_GPIO4_I		0x1e
#define PREG_PAD_GPIO5_EN_N		0x20
#define PREG_PAD_GPIO5_O		0x21
#define PREG_PAD_GPIO5_I		0x22
#define PAD_PULL_UP_EN_0		0x48
#define PAD_PULL_UP_EN_1		0x49
#define PAD_PULL_UP_EN_2		0x4a
#define PAD_PULL_UP_EN_3		0x4b
#define PAD_PULL_UP_EN_4		0x4c
#define PAD_PULL_UP_EN_5		0x4d
#define PAD_PULL_UP_0			0x3a
#define PAD_PULL_UP_1			0x3b
#define PAD_PULL_UP_2			0x3c
#define PAD_PULL_UP_3			0x3d
#define PAD_PULL_UP_4			0x3e
#define PAD_PULL_UP_5			0x3f
#define PAD_DS_0A			0xd0
#define PAD_DS_1A			0xd1
#define PAD_DS_2A			0xd2
#define PAD_DS_3A			0xd4
#define PAD_DS_4A			0xd5
#define PAD_DS_5A			0xd6

#define AO_RTI_PINMUX_0			0x05
#define AO_RTI_PINMUX_1			0x06
#define AO_PAD_DS_A			0x07
#define AO_PAD_DS_B			0x08
#define AO_GPIO_O_EN_N			0x09
#define AO_GPIO_I			0x0a
#define AO_GPIO_O			0x0d
#define AO_RTI_PULL_UP			0x0b
#define AO_RTI_PULL_UP_EN		0x0c

struct aml_gpio_bank {
	uint8_t first_pin, num_pins;
	uint8_t mux_reg, mux_bit;
	uint8_t dir_reg, dir_bit;
	uint8_t in_reg, in_bit;
	uint8_t out_reg, out_bit;
	uint8_t pull_reg, pull_bit;
	uint8_t pull_en_reg, pull_en_bit;
	uint8_t ds_reg, ds_bit;
};

struct aml_pin_group {
	const char *name;
	uint8_t	pin;
	uint8_t func;
	const char *function;
};

const struct aml_gpio_bank aml_g12a_gpio_banks[] = {
	/* BOOT */
	{ BOOT_0, 16,
	  PERIPHS_PIN_MUX_0 - PERIPHS_PIN_MUX_0, 0,
	  PREG_PAD_GPIO0_EN_N - PREG_PAD_GPIO0_EN_N, 0,
	  PREG_PAD_GPIO0_I - PREG_PAD_GPIO0_EN_N, 0,
	  PREG_PAD_GPIO0_O - PREG_PAD_GPIO0_EN_N, 0,
	  PAD_PULL_UP_0 - PAD_PULL_UP_0, 0,
	  PAD_PULL_UP_EN_0 - PAD_PULL_UP_EN_0, 0,
	  PAD_DS_0A - PAD_DS_0A, 0 },

	/* GPIOC */
	{ GPIOC_0, 8,
	  PERIPHS_PIN_MUX_9 - PERIPHS_PIN_MUX_0, 0,
	  PREG_PAD_GPIO1_EN_N - PREG_PAD_GPIO0_EN_N, 0,
	  PREG_PAD_GPIO1_I - PREG_PAD_GPIO0_EN_N, 0,
	  PREG_PAD_GPIO1_O - PREG_PAD_GPIO0_EN_N, 0,
	  PAD_PULL_UP_1 - PAD_PULL_UP_0, 0,
	  PAD_PULL_UP_EN_1 - PAD_PULL_UP_EN_0, 0,
	  PAD_DS_1A - PAD_DS_0A, 0 },

	/* GPIOX */
	{ GPIOX_0, 20,
	  PERIPHS_PIN_MUX_3 - PERIPHS_PIN_MUX_0, 0,
	  PREG_PAD_GPIO2_EN_N - PREG_PAD_GPIO0_EN_N, 0,
	  PREG_PAD_GPIO2_I - PREG_PAD_GPIO0_EN_N, 0,
	  PREG_PAD_GPIO2_O - PREG_PAD_GPIO0_EN_N, 0,
	  PAD_PULL_UP_2 - PAD_PULL_UP_0, 0,
	  PAD_PULL_UP_EN_2 - PAD_PULL_UP_EN_0, 0,
	  PAD_DS_2A - PAD_DS_0A, 0 },

	/* GPIOH */
	{ GPIOH_0, 9,
	  PERIPHS_PIN_MUX_B - PERIPHS_PIN_MUX_0, 0,
	  PREG_PAD_GPIO3_EN_N - PREG_PAD_GPIO0_EN_N, 0,
	  PREG_PAD_GPIO3_I - PREG_PAD_GPIO0_EN_N, 0,
	  PREG_PAD_GPIO3_O - PREG_PAD_GPIO0_EN_N, 0,
	  PAD_PULL_UP_3 - PAD_PULL_UP_0, 0,
	  PAD_PULL_UP_EN_3 - PAD_PULL_UP_EN_0, 0,
	  PAD_DS_3A - PAD_DS_0A, 0 },

	/* GPIOZ */
	{ GPIOZ_0, 16,
	  PERIPHS_PIN_MUX_6 - PERIPHS_PIN_MUX_0, 0,
	  PREG_PAD_GPIO4_EN_N - PREG_PAD_GPIO0_EN_N, 0,
	  PREG_PAD_GPIO4_I - PREG_PAD_GPIO0_EN_N, 0,
	  PREG_PAD_GPIO4_O - PREG_PAD_GPIO0_EN_N, 0,
	  PAD_PULL_UP_4 - PAD_PULL_UP_0, 0,
	  PAD_PULL_UP_EN_4 - PAD_PULL_UP_EN_0, 0,
	  PAD_DS_4A - PAD_DS_0A, 0 },

	/* GPIOA */
	{ GPIOA_0, 16,
	  PERIPHS_PIN_MUX_D - PERIPHS_PIN_MUX_0, 0,
	  PREG_PAD_GPIO5_EN_N - PREG_PAD_GPIO0_EN_N, 0,
	  PREG_PAD_GPIO5_I - PREG_PAD_GPIO0_EN_N, 0,
	  PREG_PAD_GPIO5_O - PREG_PAD_GPIO0_EN_N, 0,
	  PAD_PULL_UP_5 - PAD_PULL_UP_0, 0,
	  PAD_PULL_UP_EN_5 - PAD_PULL_UP_EN_0, 0,
	  PAD_DS_5A - PAD_DS_0A, 0 },

	{ }
};

const struct aml_pin_group aml_g12a_pin_groups[] = {
	/* GPIOZ */
	{ "i2c0_sda_z0", GPIOZ_0, 4, "i2c0" },
	{ "i2c0_sck_z1", GPIOZ_1, 4, "i2c0" },
	{ "i2c0_sda_z7", GPIOZ_7, 7, "i2c0" },
	{ "i2c0_sck_z8", GPIOZ_8, 7, "i2c0" },
	{ "i2c2_sda_z", GPIOZ_14, 3, "i2c2" },
	{ "i2c2_sck_z", GPIOZ_15, 3, "i2c2" },

	/* GPIOA */
	{ "i2c3_sda_a", GPIOA_14, 2, "i2c3" },
	{ "i2c3_sck_a", GPIOA_15, 2, "i2c3" },

	/* BOOT */
	{ "emmc_nand_d0", BOOT_0, 1, "emmc" },
	{ "emmc_nand_d1", BOOT_1, 1, "emmc" },
	{ "emmc_nand_d2", BOOT_2, 1, "emmc" },
	{ "emmc_nand_d3", BOOT_3, 1, "emmc" },
	{ "emmc_nand_d4", BOOT_4, 1, "emmc" },
	{ "emmc_nand_d5", BOOT_5, 1, "emmc" },
	{ "emmc_nand_d6", BOOT_6, 1, "emmc" },
	{ "emmc_nand_d7", BOOT_7, 1, "emmc" },
	{ "BOOT_8", BOOT_8, 0, "gpio_periphs" },
	{ "emmc_clk", BOOT_8, 1, "emmc" },
	{ "emmc_cmd", BOOT_10, 1, "emmc" },
	{ "emmc_nand_ds", BOOT_13, 1, "emmc" },

	/* GPIOC */
	{ "sdcard_d0_c", GPIOC_0, 1, "sdcard" },
	{ "sdcard_d1_c", GPIOC_1, 1, "sdcard" },
	{ "sdcard_d2_c", GPIOC_2, 1, "sdcard" },
	{ "sdcard_d3_c", GPIOC_3, 1, "sdcard" },
	{ "GPIOC_4", GPIOC_4, 0, "gpio_periphs" },
	{ "pwm_c_c", GPIOC_4, 5, "pwm_c" },
	{ "sdcard_clk_c", GPIOC_4, 1, "sdcard" },
	{ "sdcard_cmd_c", GPIOC_5, 1, "sdcard" },
	{ "i2c0_sda_c", GPIOC_5, 3, "i2c0" },
	{ "i2c0_sck_c", GPIOC_6, 3, "i2c0" },

	/* GPIOX */
	{ "pwm_d_x3", GPIOX_3, 4, "pwm_d" },
	{ "pwm_c_x5", GPIOX_5, 4, "pwm_c" },
	{ "pwm_a", GPIOX_6, 1, "pwm_a" },
	{ "pwm_d_x6", GPIOX_6, 4, "pwm_d" },
	{ "pwm_b_x7", GPIOX_7, 4, "pwm_b" },
	{ "pwm_f_x", GPIOX_7, 1, "pwm_f" },
	{ "pwm_c_x8", GPIOX_8, 5, "pwm_c" },
	{ "i2c1_sda_x", GPIOX_10, 5, "i2c1" },
	{ "i2c1_sck_x", GPIOX_11, 5, "i2c1" },
	{ "pwm_e", GPIOX_16, 1, "pwm_e" },
	{ "i2c2_sda_x", GPIOX_17, 1, "i2c2" },
	{ "i2c2_sck_x", GPIOX_18, 1, "i2c2" },
	{ "pwm_b_x19", GPIOX_19, 1, "pwm_b" },

	/* GPIOH */
	{ "i2c3_sda_h", GPIOH_0, 2, "i2c3" },
	{ "i2c3_sck_h", GPIOH_1, 2, "i2c3" },
	{ "i2c1_sda_h2", GPIOH_2, 2, "i2c1" },
	{ "i2c1_sck_h3", GPIOH_3, 2, "i2c1" },
	{ "pwm_f_h", GPIOH_5, 4, "pwm_f" },
	{ "i2c1_sda_h6", GPIOH_6, 4, "i2c1" },
	{ "i2c1_sck_h7", GPIOH_7, 4, "i2c1" },

	{ }
};

const struct aml_gpio_bank aml_g12a_ao_gpio_banks[] = {
	/* GPIOAO */
	{ GPIOAO_0, 12,
	  AO_RTI_PINMUX_0 - AO_RTI_PINMUX_0, 0,
	  AO_GPIO_O_EN_N - AO_GPIO_O_EN_N, 0,
	  AO_GPIO_I - AO_GPIO_O_EN_N, 0,
	  AO_GPIO_O - AO_GPIO_O_EN_N, 0,
	  AO_RTI_PULL_UP - AO_RTI_PULL_UP, 0,
	  AO_RTI_PULL_UP_EN - AO_RTI_PULL_UP_EN, 0,
	  AO_PAD_DS_A - AO_PAD_DS_A, 0 },

	/* GPIOE */
	{ GPIOE_0, 3,
	  AO_RTI_PINMUX_1 - AO_RTI_PINMUX_0, 16,
	  AO_GPIO_O_EN_N - AO_GPIO_O_EN_N, 16,
	  AO_GPIO_I - AO_GPIO_O_EN_N, 16,
	  AO_GPIO_O - AO_GPIO_O_EN_N, 16,
	  AO_RTI_PULL_UP - AO_RTI_PULL_UP, 16,
	  AO_RTI_PULL_UP_EN - AO_RTI_PULL_UP_EN, 16,
	  AO_PAD_DS_B - AO_PAD_DS_A, 0 },

	{ }
};

const struct aml_pin_group aml_g12a_ao_pin_groups[] = {
	/* GPIOAO */
	{ "uart_ao_a_tx", GPIOAO_0, 1, "uart_ao_a" },
	{ "uart_ao_a_rx", GPIOAO_1, 1, "uart_ao_a" },
	{ "pwm_ao_c_4", GPIOAO_4, 3, "pwm_ao_c" },
	{ "pwm_ao_c_hiz", GPIOAO_4, 4, "pwm_ao_c" },
	{ "pwm_ao_d_5", GPIOAO_5, 3, "pwm_ao_d" },
	{ "remote_ao_input", GPIOAO_5, 1, "remote_ao_input" },
	{ "pwm_ao_c_6", GPIOAO_6, 3, "pwm_ao_c" },
	{ "pwm_ao_d_10", GPIOAO_10, 3, "pwm_ao_d" },
	{ "pwm_ao_a", GPIOAO_11, 3, "pwm_ao_a" },
	{ "pwm_ao_a_hiz", GPIOAO_11, 2, "pwm_ao_a" },

	/* GPIOE */
	{ "pwm_ao_b", GPIOE_0, 3, "pwm_ao_b" },
	{ "pwm_ao_d_e", GPIOE_1, 3, "pwm_ao_d" },
	{ "pwm_a_e", GPIOE_2, 3, "pwm_a_e" },

	{ }
};

struct amlpinctrl_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_gpio_ioh;
	bus_space_handle_t	sc_pull_ioh;
	bus_space_handle_t	sc_pull_en_ioh;
	bus_space_handle_t	sc_mux_ioh;
	bus_space_handle_t	sc_ds_ioh;
	int			sc_nobias;

	const struct aml_gpio_bank *sc_gpio_banks;
	const struct aml_pin_group *sc_pin_groups;

	struct gpio_controller	sc_gc;
};

int	amlpinctrl_match(struct device *, void *, void *);
void	amlpinctrl_attach(struct device *, struct device *, void *);

const struct cfattach amlpinctrl_ca = {
	sizeof(struct amlpinctrl_softc), amlpinctrl_match, amlpinctrl_attach
};

struct cfdriver amlpinctrl_cd = {
	NULL, "amlpinctrl", DV_DULL
};

int	amlpinctrl_pinctrl(uint32_t, void *);
void	amlpinctrl_config_pin(void *, uint32_t *, int);
int	amlpinctrl_get_pin(void *, uint32_t *);
void	amlpinctrl_set_pin(void *, uint32_t *, int);

int
amlpinctrl_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;
	int node = faa->fa_node;

	return (OF_is_compatible(node, "amlogic,meson-g12a-periphs-pinctrl") ||
	    OF_is_compatible(node, "amlogic,meson-g12a-aobus-pinctrl"));
}

void
amlpinctrl_attach(struct device *parent, struct device *self, void *aux)
{
	struct amlpinctrl_softc *sc = (struct amlpinctrl_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint64_t addr[5], size[5];
	uint32_t *cell;
	uint32_t acells, scells;
	uint32_t reg[20];
	int node = faa->fa_node;
	int child;
	int i, len, line;

	for (child = OF_child(node); child; child = OF_peer(child)) {
		if (OF_getproplen(child, "gpio-controller") == 0)
			break;
	}
	if (child == 0) {
		printf(": no register banks\n");
		return;
	}

	acells = OF_getpropint(node, "#address-cells", faa->fa_acells);
	scells = OF_getpropint(node, "#size-cells", faa->fa_scells);
	len = OF_getproplen(child, "reg");
	line = (acells + scells) * sizeof(uint32_t);
	if (acells < 1 || acells > 2 || scells < 1 || scells > 2 ||
	    len > sizeof(reg) || (len / line) > nitems(addr)) {
		printf(": unexpected register layout\n");
		return;
	}

	memset(&size, 0, sizeof(size));
	OF_getpropintarray(child, "reg", reg, len);
	for (i = 0, cell = reg; i < len / line; i++) {
		addr[i] = cell[0];
		if (acells > 1)
			addr[i] = (addr[i] << 32) | cell[1];
		cell += acells;
		size[i] = cell[0];
		if (scells > 1)
			size[i] = (size[i] << 32) | cell[1];
		cell += scells;
	}

	sc->sc_iot = faa->fa_iot;

	i = OF_getindex(child, "gpio", "reg-names");
	if (i < 0 || i >= nitems(size) || size[i] == 0 ||
	    bus_space_map(sc->sc_iot, addr[i], size[i], 0, &sc->sc_gpio_ioh)) {
		printf(": can't map gpio registers\n");
		return;
	}
	i = OF_getindex(child, "mux", "reg-names");
	if (i < 0 || i >= nitems(size) || size[i] == 0 ||
	    bus_space_map(sc->sc_iot, addr[i], size[i], 0, &sc->sc_mux_ioh)) {
		printf(": can't map mux registers\n");
		return;
	}
	i = OF_getindex(child, "ds", "reg-names");
	if (i < 0 || i >= nitems(size) || size[i] == 0 ||
	    bus_space_map(sc->sc_iot, addr[i], size[i], 0, &sc->sc_ds_ioh)) {
		printf(": can't map ds registers\n");
		return;
	}
	i = OF_getindex(child, "pull", "reg-names");
	if (i < 0)
		sc->sc_nobias = 1;
	else if (i >= nitems(size) || size[i] == 0 ||
	    bus_space_map(sc->sc_iot, addr[i], size[i], 0, &sc->sc_pull_ioh)) {
		printf(": can't map pull registers\n");
		return;
	}
	i = OF_getindex(child, "pull-enable", "reg-names");
	if (i < 0)
		sc->sc_nobias = 1;
	else if (i >= nitems(size) || size[i] == 0 ||
	    bus_space_map(sc->sc_iot, addr[i], size[i], 0, &sc->sc_pull_en_ioh)) {
		printf(": can't map pull-enable registers\n");
		return;
	}

	printf("\n");

	if (OF_is_compatible(node, "amlogic,meson-g12a-periphs-pinctrl")) {
		sc->sc_gpio_banks = aml_g12a_gpio_banks;
		sc->sc_pin_groups = aml_g12a_pin_groups;
	} else {
		sc->sc_gpio_banks = aml_g12a_ao_gpio_banks;
		sc->sc_pin_groups = aml_g12a_ao_pin_groups;
	}

	pinctrl_register(faa->fa_node, amlpinctrl_pinctrl, sc);

	sc->sc_gc.gc_node = child;
	sc->sc_gc.gc_cookie = sc;
	sc->sc_gc.gc_config_pin = amlpinctrl_config_pin;
	sc->sc_gc.gc_get_pin = amlpinctrl_get_pin;
	sc->sc_gc.gc_set_pin = amlpinctrl_set_pin;
	gpio_controller_register(&sc->sc_gc);
}

const struct aml_gpio_bank *
amlpinctrl_lookup_bank(struct amlpinctrl_softc *sc, uint32_t pin)
{
	const struct aml_gpio_bank *bank;

	for (bank = sc->sc_gpio_banks; bank->num_pins > 0; bank++) {
		if (pin >= bank->first_pin &&
		    pin < bank->first_pin + bank->num_pins)
			return bank;
	}

	return NULL;
}

const struct aml_pin_group *
amlpinctrl_lookup_group(struct amlpinctrl_softc *sc, const char *name)
{
	const struct aml_pin_group *group;

	for (group = sc->sc_pin_groups; group->name; group++) {
		if (strcmp(name, group->name) == 0)
			return group;
	}

	return NULL;
}

void
amlpinctrl_config_func(struct amlpinctrl_softc *sc, const char *name,
    const char *function, int bias, int ds)
{
	const struct aml_pin_group *group;
	const struct aml_gpio_bank *bank;
	bus_addr_t off;
	uint32_t pin;
	uint32_t reg;

	group = amlpinctrl_lookup_group(sc, name);
	if (group == NULL) {
		printf("%s: %s\n", __func__, name);
		return;
	}
	if (strcmp(function, group->function) != 0) {
		printf("%s: mismatched function %s\n", __func__, function);
		return;
	}

	bank = amlpinctrl_lookup_bank(sc, group->pin);
	KASSERT(bank);

	pin = group->pin - bank->first_pin;

	/* mux */
	off = (bank->mux_reg + pin / 8) << 2;
	reg = bus_space_read_4(sc->sc_iot, sc->sc_mux_ioh, off);
	reg &= ~(0xf << (((pin % 8) * 4) + bank->mux_bit));
	reg |= (group->func << (((pin % 8) * 4) + bank->mux_bit));
	bus_space_write_4(sc->sc_iot, sc->sc_mux_ioh, off, reg);

	if (!sc->sc_nobias) {
		/* pull */
		off = bank->pull_reg << 2;
		reg = bus_space_read_4(sc->sc_iot, sc->sc_pull_ioh, off);
		if (bias == BIAS_PULL_UP)
			reg |= (1 << (pin + bank->pull_bit));
		else
			reg &= ~(1 << (pin + bank->pull_bit));
		bus_space_write_4(sc->sc_iot, sc->sc_pull_ioh, off, reg);

		/* pull-enable */
		off = bank->pull_en_reg << 2;
		reg = bus_space_read_4(sc->sc_iot, sc->sc_pull_en_ioh, off);
		if (bias != BIAS_DISABLE)
			reg |= (1 << (pin + bank->pull_en_bit));
		else
			reg &= ~(1 << (pin + bank->pull_en_bit));
		bus_space_write_4(sc->sc_iot, sc->sc_pull_en_ioh, off, reg);
	}

	if (ds < 0)
		return;
	else if (ds <= 500)
		ds = 0;
	else if (ds <= 2500)
		ds = 1;
	else if (ds <= 3000)
		ds = 2;
	else if (ds <= 4000)
		ds = 3;
	else {
		printf("%s: invalid drive-strength %d\n", __func__, ds);
		ds = 3;
	}

	/* ds */
	off = (bank->ds_reg + pin / 16) << 2;
	reg = bus_space_read_4(sc->sc_iot, sc->sc_ds_ioh, off);
	reg &= ~(0x3 << (((pin % 16) * 2) + bank->ds_bit));
	reg |= (ds << (((pin % 16) * 2) + bank->ds_bit));
	bus_space_write_4(sc->sc_iot, sc->sc_ds_ioh, off, reg);
}

int
amlpinctrl_pinctrl(uint32_t phandle, void *cookie)
{
	struct amlpinctrl_softc *sc = cookie;
	int node, child;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return -1;

	for (child = OF_child(node); child; child = OF_peer(child)) {
		char function[16];
		char *groups;
		char *group;
		int bias, ds;
		int len;

		memset(function, 0, sizeof(function));
		OF_getprop(child, "function", function, sizeof(function));
		function[sizeof(function) - 1] = 0;

		/* Bias */
		if (OF_getproplen(child, "bias-pull-up") == 0)
			bias = BIAS_PULL_UP;
		else if (OF_getproplen(child, "bias-pull-down") == 0)
			bias = BIAS_PULL_DOWN;
		else
			bias = BIAS_DISABLE;

		/* Drive-strength */
		ds = OF_getpropint(child, "drive-strength-microamp", -1);

		len = OF_getproplen(child, "groups");
		if (len <= 0) {
			printf("%s: 0x%08x\n", __func__, phandle);
			continue;
		}

		groups = malloc(len, M_TEMP, M_WAITOK);
		OF_getprop(child, "groups", groups, len);

		group = groups;
		while (group < groups + len) {
			amlpinctrl_config_func(sc, group, function, bias, ds);
			group += strlen(group) + 1;
		}

		free(groups, M_TEMP, len);
	}

	return 0;
}

void
amlpinctrl_config_pin(void *cookie, uint32_t *cells, int config)
{
	struct amlpinctrl_softc *sc = cookie;
	const struct aml_gpio_bank *bank;
	bus_addr_t off;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	uint32_t reg;

	bank = amlpinctrl_lookup_bank(sc, pin);
	if (bank == NULL) {
		printf("%s: 0x%08x 0x%08x\n", __func__, cells[0], cells[1]);
		return;
	}

	pin = pin - bank->first_pin;

	/* mux */
	off = (bank->mux_reg + pin / 8) << 2;
	reg = bus_space_read_4(sc->sc_iot, sc->sc_mux_ioh, off);
	reg &= ~(0xf << (((pin % 8) * 4) + bank->mux_bit));
	bus_space_write_4(sc->sc_iot, sc->sc_mux_ioh, off, reg);

	/* Emulate open drain. */
	if (flags & GPIO_OPEN_DRAIN)
		config &= ~GPIO_CONFIG_OUTPUT;

	/* gpio */
	off = bank->dir_reg << 2;
	reg = bus_space_read_4(sc->sc_iot, sc->sc_gpio_ioh, off);
	if (config & GPIO_CONFIG_OUTPUT)
		reg &= ~(1 << (pin + bank->dir_bit));
	else
		reg |= (1 << (pin + bank->dir_bit));
	bus_space_write_4(sc->sc_iot, sc->sc_gpio_ioh, off, reg);
}

int
amlpinctrl_get_pin(void *cookie, uint32_t *cells)
{
	struct amlpinctrl_softc *sc = cookie;
	const struct aml_gpio_bank *bank;
	bus_addr_t off;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	uint32_t reg;
	int val;

	bank = amlpinctrl_lookup_bank(sc, pin);
	if (bank == NULL) {
		printf("%s: 0x%08x 0x%08x\n", __func__, cells[0], cells[1]);
		return 0;
	}

	pin = pin - bank->first_pin;

	/* gpio */
	off = bank->in_reg << 2;
	reg = bus_space_read_4(sc->sc_iot, sc->sc_gpio_ioh, off);
	val = (reg >> (pin + bank->in_bit)) & 1;
	if (flags & GPIO_ACTIVE_LOW)
		val = !val;

	return val;
}

void
amlpinctrl_set_pin(void *cookie, uint32_t *cells, int val)
{
	struct amlpinctrl_softc *sc = cookie;
	const struct aml_gpio_bank *bank;
	bus_addr_t off;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	int reg;

	bank = amlpinctrl_lookup_bank(sc, pin);
	if (bank == NULL) {
		printf("%s: 0x%08x 0x%08x\n", __func__, cells[0], cells[1]);
		return;
	}

	pin = pin - bank->first_pin;

	if (flags & GPIO_ACTIVE_LOW)
		val = !val;

	/* Emulate open drain. */
	if (flags & GPIO_OPEN_DRAIN) {
		/* gpio */
		off = bank->dir_reg << 2;
		reg = bus_space_read_4(sc->sc_iot, sc->sc_gpio_ioh, off);
		if (val)
			reg |= (1 << (pin + bank->dir_bit));
		else
			reg &= ~(1 << (pin + bank->dir_bit));
		bus_space_write_4(sc->sc_iot, sc->sc_gpio_ioh, off, reg);
		if (val)
			return;
	}

	/* gpio */
	off = bank->out_reg << 2;
	reg = bus_space_read_4(sc->sc_iot, sc->sc_gpio_ioh, off);
	if (val)
		reg |= (1 << (pin + bank->out_bit));
	else
		reg &= ~(1 << (pin + bank->out_bit));
	bus_space_write_4(sc->sc_iot, sc->sc_gpio_ioh, off, reg);
}
