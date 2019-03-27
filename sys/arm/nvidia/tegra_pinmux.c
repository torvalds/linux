/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Pin multiplexer driver for Tegra SoCs.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/fdt/fdt_common.h>
#include <dev/fdt/fdt_pinctrl.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

/* Pin multipexor register. */
#define	TEGRA_MUX_FUNCTION_MASK  0x03
#define	TEGRA_MUX_FUNCTION_SHIFT 0
#define	TEGRA_MUX_PUPD_MASK  0x03
#define	TEGRA_MUX_PUPD_SHIFT 2
#define	TEGRA_MUX_TRISTATE_SHIFT 4
#define	TEGRA_MUX_ENABLE_INPUT_SHIFT 5
#define	TEGRA_MUX_OPEN_DRAIN_SHIFT 6
#define	TEGRA_MUX_LOCK_SHIFT 7
#define	TEGRA_MUX_IORESET_SHIFT 8
#define	TEGRA_MUX_RCV_SEL_SHIFT 9


/* Pin goup register. */
#define	TEGRA_GRP_HSM_SHIFT 2
#define	TEGRA_GRP_SCHMT_SHIFT 3
#define	TEGRA_GRP_DRV_TYPE_SHIFT 6
#define	TEGRA_GRP_DRV_TYPE_MASK 0x03
#define	TEGRA_GRP_DRV_DRVDN_SLWR_SHIFT 28
#define	TEGRA_GRP_DRV_DRVDN_SLWR_MASK 0x03
#define	TEGRA_GRP_DRV_DRVUP_SLWF_SHIFT 30
#define	TEGRA_GRP_DRV_DRVUP_SLWF_MASK 0x03

struct pinmux_softc {
	device_t	dev;
	struct resource	*pad_mem_res;
	struct resource	*mux_mem_res;
	struct resource	*mipi_mem_res;
};

static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-pinmux",	1},
	{NULL,				0},
};

enum prop_id {
	PROP_ID_PULL,
	PROP_ID_TRISTATE,
	PROP_ID_ENABLE_INPUT,
	PROP_ID_OPEN_DRAIN,
	PROP_ID_LOCK,
	PROP_ID_IORESET,
	PROP_ID_RCV_SEL,
	PROP_ID_HIGH_SPEED_MODE,
	PROP_ID_SCHMITT,
	PROP_ID_LOW_POWER_MODE,
	PROP_ID_DRIVE_DOWN_STRENGTH,
	PROP_ID_DRIVE_UP_STRENGTH,
	PROP_ID_SLEW_RATE_FALLING,
	PROP_ID_SLEW_RATE_RISING,
	PROP_ID_DRIVE_TYPE,

	PROP_ID_MAX_ID
};

/* Numeric based parameters. */
static const struct prop_name {
	const char *name;
	enum prop_id id;
} prop_names[] = {
	{"nvidia,pull",			PROP_ID_PULL},
	{"nvidia,tristate",		PROP_ID_TRISTATE},
	{"nvidia,enable-input",		PROP_ID_ENABLE_INPUT},
	{"nvidia,open-drain",		PROP_ID_OPEN_DRAIN},
	{"nvidia,lock",			PROP_ID_LOCK},
	{"nvidia,io-reset",		PROP_ID_IORESET},
	{"nvidia,rcv-sel",		PROP_ID_RCV_SEL},
	{"nvidia,high-speed-mode",	PROP_ID_HIGH_SPEED_MODE},
	{"nvidia,schmitt",		PROP_ID_SCHMITT},
	{"nvidia,low-power-mode",	PROP_ID_LOW_POWER_MODE},
	{"nvidia,pull-down-strength",	PROP_ID_DRIVE_DOWN_STRENGTH},
	{"nvidia,pull-up-strength",	PROP_ID_DRIVE_UP_STRENGTH},
	{"nvidia,slew-rate-falling",	PROP_ID_SLEW_RATE_FALLING},
	{"nvidia,slew-rate-rising",	PROP_ID_SLEW_RATE_RISING},
	{"nvidia,drive-type",		PROP_ID_DRIVE_TYPE},
};

/*
 * configuration for one pin group.
 */
struct pincfg {
	char	*function;
	int	params[PROP_ID_MAX_ID];
};
#define	GPIO_BANK_A	 0
#define	GPIO_BANK_B	 1
#define	GPIO_BANK_C	 2
#define	GPIO_BANK_D	 3
#define	GPIO_BANK_E	 4
#define	GPIO_BANK_F	 5
#define	GPIO_BANK_G	 6
#define	GPIO_BANK_H	 7
#define	GPIO_BANK_I	 8
#define	GPIO_BANK_J	 9
#define	GPIO_BANK_K	10
#define	GPIO_BANK_L	11
#define	GPIO_BANK_M	12
#define	GPIO_BANK_N	13
#define	GPIO_BANK_O	14
#define	GPIO_BANK_P	15
#define	GPIO_BANK_Q	16
#define	GPIO_BANK_R	17
#define	GPIO_BANK_S	18
#define	GPIO_BANK_T	19
#define	GPIO_BANK_U	20
#define	GPIO_BANK_V	21
#define	GPIO_BANK_W	22
#define	GPIO_BANK_X	23
#define	GPIO_BANK_Y	24
#define	GPIO_BANK_Z	25
#define	GPIO_BANK_AA	26
#define	GPIO_BANK_BB	27
#define	GPIO_BANK_CC	28
#define	GPIO_BANK_DD	29
#define	GPIO_BANK_EE	30
#define	GPIO_BANK_FF	31
#define	GPIO_NUM(b, p) (8 * (b) + (p))

struct tegra_mux {
	char *name;
	bus_size_t reg;
	char *functions[4];
	int gpio_num;
};

#define	GMUX(r, gb, gi, nm, f1, f2, f3, f4)				\
{									\
	.name = #nm,							\
	.reg = r,							\
	.gpio_num = GPIO_NUM(GPIO_BANK_##gb, gi),			\
	.functions = {#f1, #f2, #f3, #f4},				\
}

#define	FMUX(r, nm, f1, f2, f3, f4)					\
{									\
	.name = #nm,							\
	.reg = r,							\
	.gpio_num = -1,							\
	.functions = {#f1, #f2, #f3, #f4},				\
}

static const struct tegra_mux pin_mux_tbl[] = {
	GMUX(0x000,  O, 1, ulpi_data0_po1,         spi3,       hsi,        uarta,        ulpi),
	GMUX(0x004,  O, 2, ulpi_data1_po2,         spi3,       hsi,        uarta,        ulpi),
	GMUX(0x008,  O, 3, ulpi_data2_po3,         spi3,       hsi,        uarta,        ulpi),
	GMUX(0x00C,  O, 4, ulpi_data3_po4,         spi3,       hsi,        uarta,        ulpi),
	GMUX(0x010,  O, 5, ulpi_data4_po5,         spi2,       hsi,        uarta,        ulpi),
	GMUX(0x014,  O, 6, ulpi_data5_po6,         spi2,       hsi,        uarta,        ulpi),
	GMUX(0x018,  O, 7, ulpi_data6_po7,         spi2,       hsi,        uarta,        ulpi),
	GMUX(0x01C,  O, 0, ulpi_data7_po0,         spi2,       hsi,        uarta,        ulpi),
	GMUX(0x020,  P, 9, ulpi_clk_py0,           spi1,       spi5,       uartd,        ulpi),
	GMUX(0x024,  P, 1, ulpi_dir_py1,           spi1,       spi5,       uartd,        ulpi),
	GMUX(0x028,  P, 2, ulpi_nxt_py2,           spi1,       spi5,       uartd,        ulpi),
	GMUX(0x02C,  P, 3, ulpi_stp_py3,           spi1,       spi5,       uartd,        ulpi),
	GMUX(0x030,  P, 0, dap3_fs_pp0,            i2s2,       spi5,       displaya,     displayb),
	GMUX(0x034,  P, 1, dap3_din_pp1,           i2s2,       spi5,       displaya,     displayb),
	GMUX(0x038,  P, 2, dap3_dout_pp2,          i2s2,       spi5,       displaya,     rsvd4),
	GMUX(0x03C,  P, 3, dap3_sclk_pp3,          i2s2,       spi5,       rsvd3,        displayb),
	GMUX(0x040,  V, 0, pv0,                    rsvd1,      rsvd2,      rsvd3,        rsvd4),
	GMUX(0x044,  V, 1, pv1,                    rsvd1,      rsvd2,      rsvd3,        rsvd4),
	GMUX(0x048,  Z, 0, sdmmc1_clk_pz0,         sdmmc1,     clk12,      rsvd3,        rsvd4),
	GMUX(0x04C,  Z, 1, sdmmc1_cmd_pz1,         sdmmc1,     spdif,      spi4,         uarta),
	GMUX(0x050,  Y, 4, sdmmc1_dat3_py4,        sdmmc1,     spdif,      spi4,         uarta),
	GMUX(0x054,  Y, 5, sdmmc1_dat2_py5,        sdmmc1,     pwm0,       spi4,         uarta),
	GMUX(0x058,  Y, 6, sdmmc1_dat1_py6,        sdmmc1,     pwm1,       spi4,         uarta),
	GMUX(0x05C,  Y, 7, sdmmc1_dat0_py7,        sdmmc1,     rsvd2,      spi4,         uarta),
	GMUX(0x068,  W, 5, clk2_out_pw5,           extperiph2, rsvd2,      rsvd3,        rsvd4),
	GMUX(0x06C, CC, 5, clk2_req_pcc5,          dap,        rsvd2,      rsvd3,        rsvd4),
	GMUX(0x110,  N, 7, hdmi_int_pn7,           rsvd1,      rsvd2,      rsvd3,        rsvd4),
	GMUX(0x114,  V, 4, ddc_scl_pv4,            i2c4,       rsvd2,      rsvd3,        rsvd4),
	GMUX(0x118,  V, 5, ddc_sda_pv5,            i2c4,       rsvd2,      rsvd3,        rsvd4),
	GMUX(0x164,  V, 3, uart2_rxd_pc3,          irda,       spdif,      uarta,        spi4),
	GMUX(0x168,  C, 2, uart2_txd_pc2,          irda,       spdif,      uarta,        spi4),
	GMUX(0x16C,  J, 6, uart2_rts_n_pj6,        uarta,      uartb,      gmi,          spi4),
	GMUX(0x170,  J, 5, uart2_cts_n_pj5,        uarta,      uartb,      gmi,          spi4),
	GMUX(0x174,  W, 6, uart3_txd_pw6,          uartc,      rsvd2,      gmi,          spi4),
	GMUX(0x178,  W, 7, uart3_rxd_pw7,          uartc,      rsvd2,      gmi,          spi4),
	GMUX(0x17C,  S, 1, uart3_cts_n_pa1,        uartc,      sdmmc1,     dtv,          gmi),
	GMUX(0x180,  C, 0, uart3_rts_n_pc0,        uartc,      pwm0,       dtv,          gmi),
	GMUX(0x184,  U, 0, pu0,                    owr,        uarta,      gmi,          rsvd4),
	GMUX(0x188,  U, 1, pu1,                    rsvd1,      uarta,      gmi,          rsvd4),
	GMUX(0x18C,  U, 2, pu2,                    rsvd1,      uarta,      gmi,          rsvd4),
	GMUX(0x190,  U, 3, pu3,                    pwm0,       uarta,      gmi,          displayb),
	GMUX(0x194,  U, 4, pu4,                    pwm1,       uarta,      gmi,          displayb),
	GMUX(0x198,  U, 5, pu5,                    pwm2,       uarta,      gmi,          displayb),
	GMUX(0x19C,  U, 6, pu6,                    pwm3,       uarta,      rsvd3,        gmi),
	GMUX(0x1A0,  C, 5, gen1_i2c_sda_pc5,       i2c1,       rsvd2,      rsvd3,        rsvd4),
	GMUX(0x1A4,  C, 4, gen1_i2c_scl_pc4,       i2c1,       rsvd2,      rsvd3,        rsvd4),
	GMUX(0x1A8,  P, 3, dap4_fs_pp4,            i2s3,       gmi,        dtv,          rsvd4),
	GMUX(0x1AC,  P, 4, dap4_din_pp5,           i2s3,       gmi,        rsvd3,        rsvd4),
	GMUX(0x1B0,  P, 5, dap4_dout_pp6,          i2s3,       gmi,        dtv,          rsvd4),
	GMUX(0x1B4,  P, 7, dap4_sclk_pp7,          i2s3,       gmi,        rsvd3,        rsvd4),
	GMUX(0x1B8,  P, 0, clk3_out_pee0,          extperiph3, rsvd2,      rsvd3,        rsvd4),
	GMUX(0x1BC, EE, 1, clk3_req_pee1,          dev3,       rsvd2,      rsvd3,        rsvd4),
	GMUX(0x1C0,  C, 7, pc7,                    rsvd1,      rsvd2,      gmi,          gmi_alt),
	GMUX(0x1C4,  I, 5, pi5,                    sdmmc2,     rsvd2,      gmi,          rsvd4),
	GMUX(0x1C8,  I, 7, pi7,                    rsvd1,      trace,      gmi,          dtv),
	GMUX(0x1CC,  K, 0, pk0,                    rsvd1,      sdmmc3,     gmi,          soc),
	GMUX(0x1D0,  K, 1, pk1,                    sdmmc2,     trace,      gmi,          rsvd4),
	GMUX(0x1D4,  J, 0, pj0,                    rsvd1,      rsvd2,      gmi,          usb),
	GMUX(0x1D8,  J, 2, pj2,                    rsvd1,      rsvd2,      gmi,          soc),
	GMUX(0x1DC,  K, 3, pk3,                    sdmmc2,     trace,      gmi,          ccla),
	GMUX(0x1E0,  K, 4, pk4,                    sdmmc2,     rsvd2,      gmi,          gmi_alt),
	GMUX(0x1E4,  K, 2, pk2,                    rsvd1,      rsvd2,      gmi,          rsvd4),
	GMUX(0x1E8,  I, 3, pi3,                    rsvd1,      rsvd2,      gmi,          spi4),
	GMUX(0x1EC,  I, 6, pi6,                    rsvd1,      rsvd2,      gmi,          sdmmc2),
	GMUX(0x1F0,  G, 0, pg0,                    rsvd1,      rsvd2,      gmi,          rsvd4),
	GMUX(0x1F4,  G, 1, pg1,                    rsvd1,      rsvd2,      gmi,          rsvd4),
	GMUX(0x1F8,  G, 2, pg2,                    rsvd1,      trace,      gmi,          rsvd4),
	GMUX(0x1FC,  G, 3, pg3,                    rsvd1,      trace,      gmi,          rsvd4),
	GMUX(0x200,  G, 4, pg4,                    rsvd1,      tmds,       gmi,          spi4),
	GMUX(0x204,  G, 5, pg5,                    rsvd1,      rsvd2,      gmi,          spi4),
	GMUX(0x208,  G, 6, pg6,                    rsvd1,      rsvd2,      gmi,          spi4),
	GMUX(0x20C,  G, 7, pg7,                    rsvd1,      rsvd2,      gmi,          spi4),
	GMUX(0x210,  H, 0, ph0,                    pwm0,       trace,      gmi,          dtv),
	GMUX(0x214,  H, 1, ph1,                    pwm1,       tmds,       gmi,          displaya),
	GMUX(0x218,  H, 2, ph2,                    pwm2,       tmds,       gmi,          cldvfs),
	GMUX(0x21C,  H, 3, ph3,                    pwm3,       spi4,       gmi,          cldvfs),
	GMUX(0x220,  H, 4, ph4,                    sdmmc2,     rsvd2,      gmi,          rsvd4),
	GMUX(0x224,  H, 5, ph5,                    sdmmc2,     rsvd2,      gmi,          rsvd4),
	GMUX(0x228,  H, 6, ph6,                    sdmmc2,     trace,      gmi,          dtv),
	GMUX(0x22C,  H, 7, ph7,                    sdmmc2,     trace,      gmi,          dtv),
	GMUX(0x230,  J, 7, pj7,                    uartd,      rsvd2,      gmi,          gmi_alt),
	GMUX(0x234,  B, 0, pb0,                    uartd,      rsvd2,      gmi,          rsvd4),
	GMUX(0x238,  B, 1, pb1,                    uartd,      rsvd2,      gmi,          rsvd4),
	GMUX(0x23C,  K, 7, pk7,                    uartd,      rsvd2,      gmi,          rsvd4),
	GMUX(0x240,  I, 0, pi0,                    rsvd1,      rsvd2,      gmi,          rsvd4),
	GMUX(0x244,  I, 1, pi1,                    rsvd1,      rsvd2,      gmi,          rsvd4),
	GMUX(0x248,  I, 2, pi2,                    sdmmc2,     trace,      gmi,          rsvd4),
	GMUX(0x24C,  I, 4, pi4,                    spi4,       trace,      gmi,          displaya),
	GMUX(0x250,  T, 5, gen2_i2c_scl_pt5,       i2c2,       rsvd2,      gmi,          rsvd4),
	GMUX(0x254,  T, 6, gen2_i2c_sda_pt6,       i2c2,       rsvd2,      gmi,          rsvd4),
	GMUX(0x258, CC, 4, sdmmc4_clk_pcc4,        sdmmc4,     rsvd2,      gmi,          rsvd4),
	GMUX(0x25C,  T, 7, sdmmc4_cmd_pt7,         sdmmc4,     rsvd2,      gmi,          rsvd4),
	GMUX(0x260, AA, 0, sdmmc4_dat0_paa0,       sdmmc4,     spi3,       gmi,          rsvd4),
	GMUX(0x264, AA, 1, sdmmc4_dat1_paa1,       sdmmc4,     spi3,       gmi,          rsvd4),
	GMUX(0x268, AA, 2, sdmmc4_dat2_paa2,       sdmmc4,     spi3,       gmi,          rsvd4),
	GMUX(0x26C, AA, 3, sdmmc4_dat3_paa3,       sdmmc4,     spi3,       gmi,          rsvd4),
	GMUX(0x270, AA, 4, sdmmc4_dat4_paa4,       sdmmc4,     spi3,       gmi,          rsvd4),
	GMUX(0x274, AA, 5, sdmmc4_dat5_paa5,       sdmmc4,     spi3,       rsvd3,        rsvd4),
	GMUX(0x278, AA, 6, sdmmc4_dat6_paa6,       sdmmc4,     spi3,       gmi,          rsvd4),
	GMUX(0x27C, AA, 7, sdmmc4_dat7_paa7,       sdmmc4,     rsvd2,      gmi,          rsvd4),
	GMUX(0x284, CC, 0, cam_mclk_pcc0,          vi,         vi_alt1,    vi_alt3,      sdmmc2),
	GMUX(0x288, CC, 1, pcc1,                   i2s4,       rsvd2,      rsvd3,        sdmmc2),
	GMUX(0x28C, BB, 0, pbb0,                   vgp6,       vimclk2,    sdmmc2,       vimclk2_alt),
	GMUX(0x290, BB, 1, cam_i2c_scl_pbb1,       vgp1,       i2c3,       rsvd3,        sdmmc2),
	GMUX(0x294, BB, 2, cam_i2c_sda_pbb2,       vgp2,       i2c3,       rsvd3,        sdmmc2),
	GMUX(0x298, BB, 3, pbb3,                   vgp3,       displaya,   displayb,     sdmmc2),
	GMUX(0x29C, BB, 4, pbb4,                   vgp4,       displaya,   displayb,     sdmmc2),
	GMUX(0x2A0, BB, 5, pbb5,                   vgp5,       displaya,   rsvd3,        sdmmc2),
	GMUX(0x2A4, BB, 6, pbb6,                   i2s4,       rsvd2,      displayb,     sdmmc2),
	GMUX(0x2A8, BB, 7, pbb7,                   i2s4,       rsvd2,      rsvd3,        sdmmc2),
	GMUX(0x2AC, CC, 2, pcc2,                   i2s4,       rsvd2,      sdmmc3,       sdmmc2),
	FMUX(0x2B0,        jtag_rtck,              rtck,       rsvd2,      rsvd3,        rsvd4),
	GMUX(0x2B4,  Z, 6, pwr_i2c_scl_pz6,        i2cpwr,     rsvd2,      rsvd3,        rsvd4),
	GMUX(0x2B8,  Z, 7, pwr_i2c_sda_pz7,        i2cpwr,     rsvd2,      rsvd3,        rsvd4),
	GMUX(0x2BC,  R, 0, kb_row0_pr0,            kbc,        rsvd2,      rsvd3,        rsvd4),
	GMUX(0x2C0,  R, 1, kb_row1_pr1,            kbc,        rsvd2,      rsvd3,        rsvd4),
	GMUX(0x2C4,  R, 2, kb_row2_pr2,            kbc,        rsvd2,      rsvd3,        rsvd4),
	GMUX(0x2C8,  R, 3, kb_row3_pr3,            kbc,        displaya,   sys,          displayb),
	GMUX(0x2CC,  R, 4, kb_row4_pr4,            kbc,        displaya,   rsvd3,        displayb),
	GMUX(0x2D0,  R, 5, kb_row5_pr5,            kbc,        displaya,   rsvd3,        displayb),
	GMUX(0x2D4,  R, 6, kb_row6_pr6,            kbc,        displaya,   displaya_alt, displayb),
	GMUX(0x2D8,  R, 7, kb_row7_pr7,            kbc,        rsvd2,      cldvfs,       uarta),
	GMUX(0x2DC,  S, 0, kb_row8_ps0,            kbc,        rsvd2,      cldvfs,       uarta),
	GMUX(0x2E0,  S, 1, kb_row9_ps1,            kbc,        rsvd2,      rsvd3,        uarta),
	GMUX(0x2E4,  S, 2, kb_row10_ps2,           kbc,        rsvd2,      rsvd3,        uarta),
	GMUX(0x2E8,  S, 3, kb_row11_ps3,           kbc,        rsvd2,      rsvd3,        irda),
	GMUX(0x2EC,  S, 4, kb_row12_ps4,           kbc,        rsvd2,      rsvd3,        irda),
	GMUX(0x2F0,  S, 5, kb_row13_ps5,           kbc,        rsvd2,      spi2,         rsvd4),
	GMUX(0x2F4,  S, 6, kb_row14_ps6,           kbc,        rsvd2,      spi2,         rsvd4),
	GMUX(0x2F8,  S, 7, kb_row15_ps7,           kbc,        soc,        rsvd3,        rsvd4),
	GMUX(0x2FC,  Q, 0, kb_col0_pq0,            kbc,        rsvd2,      spi2,         rsvd4),
	GMUX(0x300,  Q, 1, kb_col1_pq1,            kbc,        rsvd2,      spi2,         rsvd4),
	GMUX(0x304,  Q, 2, kb_col2_pq2,            kbc,        rsvd2,      spi2,         rsvd4),
	GMUX(0x308,  Q, 3, kb_col3_pq3,            kbc,        displaya,   pwm2,         uarta),
	GMUX(0x30C,  Q, 4, kb_col4_pq4,            kbc,        owr,        sdmmc3,       uarta),
	GMUX(0x310,  Q, 5, kb_col5_pq5,            kbc,        rsvd2,      sdmmc3,       rsvd4),
	GMUX(0x314,  Q, 6, kb_col6_pq6,            kbc,        rsvd2,      spi2,         uartd),
	GMUX(0x318,  Q, 7, kb_col7_pq7,            kbc,        rsvd2,      spi2,         uartd),
	GMUX(0x31C,  A, 0, clk_32k_out_pa0,        blink,      soc,        rsvd3,        rsvd4),
	FMUX(0x324,        core_pwr_req,           pwron,      rsvd2,      rsvd3,        rsvd4),
	FMUX(0x328,        cpu_pwr_req,            cpu,        rsvd2,      rsvd3,        rsvd4),
	FMUX(0x32C,        pwr_int_n,              pmi,        rsvd2,      rsvd3,        rsvd4),
	FMUX(0x330,        clk_32k_in,             clk,        rsvd2,      rsvd3,        rsvd4),
	FMUX(0x334,        owr,                    owr,        rsvd2,      rsvd3,        rsvd4),
	GMUX(0x338,  N, 0, dap1_fs_pn0,            i2s0,       hda,        gmi,          rsvd4),
	GMUX(0x33C,  N, 1, dap1_din_pn1,           i2s0,       hda,        gmi,          rsvd4),
	GMUX(0x340,  N, 2, dap1_dout_pn2,          i2s0,       hda,        gmi,          sata),
	GMUX(0x344,  N, 3, dap1_sclk_pn3,          i2s0,       hda,        gmi,          rsvd4),
	GMUX(0x348, EE, 2, dap_mclk1_req_pee2,     dap,        dap1,       sata,         rsvd4),
	GMUX(0x34C,  W, 4, dap_mclk1_pw4,          extperiph1, dap2,       rsvd3,        rsvd4),
	GMUX(0x350,  K, 6, spdif_in_pk6,           spdif,      rsvd2,      rsvd3,        i2c3),
	GMUX(0x354,  K, 5, spdif_out_pk5,          spdif,      rsvd2,      rsvd3,        i2c3),
	GMUX(0x358,  A, 2, dap2_fs_pa2,            i2s1,       hda,        gmi,          rsvd4),
	GMUX(0x35C,  A, 4, dap2_din_pa4,           i2s1,       hda,        gmi,          rsvd4),
	GMUX(0x360,  A, 5, dap2_dout_pa5,          i2s1,       hda,        gmi,          rsvd4),
	GMUX(0x364,  A, 3, dap2_sclk_pa3,          i2s1,       hda,        gmi,          rsvd4),
	GMUX(0x368,  X, 0, dvfs_pwm_px0,           spi6,       cldvfs,     gmi,          rsvd4),
	GMUX(0x36C,  X, 1, gpio_x1_aud_px1,        spi6,       rsvd2,      gmi,          rsvd4),
	GMUX(0x370,  X, 3, gpio_x3_aud_px3,        spi6,       spi1,       gmi,          rsvd4),
	GMUX(0x374,  X, 2, dvfs_clk_px2,           spi6,       cldvfs,     gmi,          rsvd4),
	GMUX(0x378,  X, 4, gpio_x4_aud_px4,        gmi,        spi1,       spi2,         dap2),
	GMUX(0x37C,  X, 5, gpio_x5_aud_px5,        gmi,        spi1,       spi2,         rsvd4),
	GMUX(0x380,  X, 6, gpio_x6_aud_px6,        spi6,       spi1,       spi2,         gmi),
	GMUX(0x384,  X, 7, gpio_x7_aud_px7,        rsvd1,      spi1,       spi2,         rsvd4),
	GMUX(0x390,  A, 6, sdmmc3_clk_pa6,         sdmmc3,     rsvd2,      rsvd3,        spi3),
	GMUX(0x394,  A, 7, sdmmc3_cmd_pa7,         sdmmc3,     pwm3,       uarta,        spi3),
	GMUX(0x398,  B, 7, sdmmc3_dat0_pb7,        sdmmc3,     rsvd2,      rsvd3,        spi3),
	GMUX(0x39C,  B, 6, sdmmc3_dat1_pb6,        sdmmc3,     pwm2,       uarta,        spi3),
	GMUX(0x3A0,  B, 5, sdmmc3_dat2_pb5,        sdmmc3,     pwm1,       displaya,     spi3),
	GMUX(0x3A4,  B, 4, sdmmc3_dat3_pb4,        sdmmc3,     pwm0,       displayb,     spi3),
	GMUX(0x3BC, DD, 1, pex_l0_rst_n_pdd1,      pe0,        rsvd2,      rsvd3,        rsvd4),
	GMUX(0x3C0, DD, 2, pex_l0_clkreq_n_pdd2,   pe0,        rsvd2,      rsvd3,        rsvd4),
	GMUX(0x3C4, DD, 3, pex_wake_n_pdd3,        pe,         rsvd2,      rsvd3,        rsvd4),
	GMUX(0x3CC, DD, 5, pex_l1_rst_n_pdd5,      pe1,        rsvd2,      rsvd3,        rsvd4),
	GMUX(0x3D0, DD, 6, pex_l1_clkreq_n_pdd6,   pe1,        rsvd2,      rsvd3,        rsvd4),
	GMUX(0x3E0, EE, 3, hdmi_cec_pee3,          cec,        rsvd2,      rsvd3,        rsvd4),
	GMUX(0x3E4,  V, 3, sdmmc1_wp_n_pv3,        sdmmc1,     clk12,      spi4,         uarta),
	GMUX(0x3E8,  V, 2, sdmmc3_cd_n_pv2,        sdmmc3,     owr,        rsvd3,        rsvd4),
	GMUX(0x3EC,  W, 2, gpio_w2_aud_pw2,        spi6,       rsvd2,      spi2,         i2c1),
	GMUX(0x3F0,  W, 3, gpio_w3_aud_pw3,        spi6,       spi1,       spi2,         i2c1),
	GMUX(0x3F4,  N, 4, usb_vbus_en0_pn4,       usb,        rsvd2,      rsvd3,        rsvd4),
	GMUX(0x3F8,  N, 5, usb_vbus_en1_pn5,       usb,        rsvd2,      rsvd3,        rsvd4),
	GMUX(0x3FC, EE, 5, sdmmc3_clk_lb_in_pee5,  sdmmc3,     rsvd2,      rsvd3,        rsvd4),
	GMUX(0x400, EE, 4, sdmmc3_clk_lb_out_pee4, sdmmc3,     rsvd2,      rsvd3,        rsvd4),
	FMUX(0x404,        gmi_clk_lb,             sdmmc2,     rsvd2,      gmi,          rsvd4),
	FMUX(0x408,        reset_out_n,            rsvd1,      rsvd2,      rsvd3,        reset_out_n),
	GMUX(0x40C,  T, 0, kb_row16_pt0,           kbc,        rsvd2,      rsvd3,        uartc),
	GMUX(0x410,  T, 1, kb_row17_pt1,           kbc,        rsvd2,      rsvd3,        uartc),
	GMUX(0x414, FF, 1, usb_vbus_en2_pff1,      usb,        rsvd2,      rsvd3,        rsvd4),
	GMUX(0x418, FF, 2, pff2,                   sata,       rsvd2,      rsvd3,        rsvd4),
	GMUX(0x430, FF, 0, dp_hpd_pff0,            dp,         rsvd2,      rsvd3,        rsvd4),
};

struct tegra_grp {
	char *name;
	bus_size_t reg;
	int drvdn_shift;
	int drvdn_mask;
	int drvup_shift;
	int drvup_mask;
};

#define	GRP(r, nm, dn_s, dn_w, up_s, up_w)				\
{									\
	.name = #nm,							\
	.reg = r - 0x868,						\
	.drvdn_shift = dn_s,						\
	.drvdn_mask = (1 << dn_w) - 1,					\
	.drvup_shift = up_s,						\
	.drvup_mask = (1 << dn_w) - 1,					\
}

/* Use register offsets from TRM */
static const struct tegra_grp pin_grp_tbl[] = {
	GRP(0x868, ao1,          12,  5,  20,  5),
	GRP(0x86C, ao2,          12,  5,  20,  5),
	GRP(0x870, at1,          12,  7,  20,  7),
	GRP(0x874, at2,          12,  7,  20,  7),
	GRP(0x878, at3,          12,  7,  20,  7),
	GRP(0x87C, at4,          12,  7,  20,  7),
	GRP(0x880, at5,          14,  5,  19,  5),
	GRP(0x884, cdev1,        12,  5,  20,  5),
	GRP(0x888, cdev2,        12,  5,  20,  5),
	GRP(0x890, dap1,         12,  5,  20,  5),
	GRP(0x894, dap2,         12,  5,  20,  5),
	GRP(0x898, dap3,         12,  5,  20,  5),
	GRP(0x89C, dap4,         12,  5,  20,  5),
	GRP(0x8A0, dbg,          12,  5,  20,  5),
	GRP(0x8B0, sdio3,        12,  7,  20,  7),
	GRP(0x8B4, spi,          12,  5,  20,  5),
	GRP(0x8B8, uaa,          12,  5,  20,  5),
	GRP(0x8BC, uab,          12,  5,  20,  5),
	GRP(0x8C0, uart2,        12,  5,  20,  5),
	GRP(0x8C4, uart3,        12,  5,  20,  5),
	GRP(0x8EC, sdio1,        12,  7,  20,  7),
	GRP(0x8FC, ddc,          12,  5,  20,  5),
	GRP(0x900, gma,          14,  5,  20,  5),
	GRP(0x910, gme,          14,  5,  19,  5),
	GRP(0x914, gmf,          14,  5,  19,  5),
	GRP(0x918, gmg,          14,  5,  19,  5),
	GRP(0x91C, gmh,          14,  5,  19,  5),
	GRP(0x920, owr,          12,  5,  20,  5),
	GRP(0x924, uda,          12,  5,  20,  5),
	GRP(0x928, gpv,          12,  5,  20,  5),
	GRP(0x92C, dev3,         12,  5,  20,  5),
	GRP(0x938, cec,          12,  5,  20,  5),
	GRP(0x994, at6,          12,  7,  20,  7),
	GRP(0x998, dap5,         12,  5,  20,  5),
	GRP(0x99C, usb_vbus_en,  12,  5,  20,  5),
	GRP(0x9A8, ao3,          12,  5,  -1,  0),
	GRP(0x9B0, ao0,          12,  5,  20,  5),
	GRP(0x9B4, hv0,          12,  5,  -1,  0),
	GRP(0x9C4, sdio4,        12,  5,  20,  5),
	GRP(0x9C8, ao4,          12,  7,  20,  7),
};

static const struct tegra_grp *
pinmux_search_grp(char *grp_name)
{
	int i;

	for (i = 0; i < nitems(pin_grp_tbl); i++) {
		if (strcmp(grp_name, pin_grp_tbl[i].name) == 0)
			return 	(&pin_grp_tbl[i]);
	}
	return (NULL);
}

static const struct tegra_mux *
pinmux_search_mux(char *pin_name)
{
	int i;

	for (i = 0; i < nitems(pin_mux_tbl); i++) {
		if (strcmp(pin_name, pin_mux_tbl[i].name) == 0)
			return 	(&pin_mux_tbl[i]);
	}
	return (NULL);
}

static int
pinmux_mux_function(const struct tegra_mux *mux, char *fnc_name)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (strcmp(fnc_name, mux->functions[i]) == 0)
			return 	(i);
	}
	return (-1);
}

static int
pinmux_config_mux(struct pinmux_softc *sc, char *pin_name,
    const struct tegra_mux *mux, struct pincfg *cfg)
{
	int tmp;
	uint32_t reg;

	reg = bus_read_4(sc->mux_mem_res, mux->reg);

	if (cfg->function != NULL) {
		tmp = pinmux_mux_function(mux, cfg->function);
		if (tmp == -1) {
			device_printf(sc->dev,
			    "Unknown function %s for pin %s\n", cfg->function,
			    pin_name);
			return (ENXIO);
		}
		reg &= ~(TEGRA_MUX_FUNCTION_MASK << TEGRA_MUX_FUNCTION_SHIFT);
		reg |=  (tmp & TEGRA_MUX_FUNCTION_MASK) <<
		    TEGRA_MUX_FUNCTION_SHIFT;
	}
	if (cfg->params[PROP_ID_PULL] != -1) {
		reg &= ~(TEGRA_MUX_PUPD_MASK << TEGRA_MUX_PUPD_SHIFT);
		reg |=  (cfg->params[PROP_ID_PULL] & TEGRA_MUX_PUPD_MASK) <<
		    TEGRA_MUX_PUPD_SHIFT;
	}
	if (cfg->params[PROP_ID_TRISTATE] != -1) {
		reg &= ~(1 << TEGRA_MUX_TRISTATE_SHIFT);
		reg |=  (cfg->params[PROP_ID_TRISTATE] & 1) <<
		    TEGRA_MUX_TRISTATE_SHIFT;
	}
	if (cfg->params[TEGRA_MUX_ENABLE_INPUT_SHIFT] != -1) {
		reg &= ~(1 << TEGRA_MUX_ENABLE_INPUT_SHIFT);
		reg |=  (cfg->params[TEGRA_MUX_ENABLE_INPUT_SHIFT] & 1) <<
		    TEGRA_MUX_ENABLE_INPUT_SHIFT;
	}
	if (cfg->params[PROP_ID_ENABLE_INPUT] != -1) {
		reg &= ~(1 << TEGRA_MUX_ENABLE_INPUT_SHIFT);
		reg |=  (cfg->params[PROP_ID_ENABLE_INPUT] & 1) <<
		    TEGRA_MUX_ENABLE_INPUT_SHIFT;
	}
	if (cfg->params[PROP_ID_ENABLE_INPUT] != -1) {
		reg &= ~(1 << TEGRA_MUX_ENABLE_INPUT_SHIFT);
		reg |=  (cfg->params[PROP_ID_OPEN_DRAIN] & 1) <<
		    TEGRA_MUX_ENABLE_INPUT_SHIFT;
	}
	if (cfg->params[PROP_ID_LOCK] != -1) {
		reg &= ~(1 << TEGRA_MUX_LOCK_SHIFT);
		reg |=  (cfg->params[PROP_ID_LOCK] & 1) <<
		    TEGRA_MUX_LOCK_SHIFT;
	}
	if (cfg->params[PROP_ID_IORESET] != -1) {
		reg &= ~(1 << TEGRA_MUX_IORESET_SHIFT);
		reg |=  (cfg->params[PROP_ID_IORESET] & 1) <<
		    TEGRA_MUX_IORESET_SHIFT;
	}
	if (cfg->params[PROP_ID_RCV_SEL] != -1) {
		reg &= ~(1 << TEGRA_MUX_RCV_SEL_SHIFT);
		reg |=  (cfg->params[PROP_ID_RCV_SEL] & 1) <<
		    TEGRA_MUX_RCV_SEL_SHIFT;
	}
	bus_write_4(sc->mux_mem_res, mux->reg, reg);
	return (0);
}

static int
pinmux_config_grp(struct pinmux_softc *sc, char *grp_name,
    const struct tegra_grp *grp, struct pincfg *cfg)
{
	uint32_t reg;

	reg = bus_read_4(sc->pad_mem_res, grp->reg);

	if (cfg->params[PROP_ID_HIGH_SPEED_MODE] != -1) {
		reg &= ~(1 << TEGRA_GRP_HSM_SHIFT);
		reg |=  (cfg->params[PROP_ID_HIGH_SPEED_MODE] & 1) <<
		    TEGRA_GRP_HSM_SHIFT;
	}
	if (cfg->params[PROP_ID_SCHMITT] != -1) {
		reg &= ~(1 << TEGRA_GRP_SCHMT_SHIFT);
		reg |=  (cfg->params[PROP_ID_SCHMITT] & 1) <<
		    TEGRA_GRP_SCHMT_SHIFT;
	}
	if (cfg->params[PROP_ID_DRIVE_TYPE] != -1) {
		reg &= ~(TEGRA_GRP_DRV_TYPE_MASK << TEGRA_GRP_DRV_TYPE_SHIFT);
		reg |=  (cfg->params[PROP_ID_DRIVE_TYPE] &
		    TEGRA_GRP_DRV_TYPE_MASK) << TEGRA_GRP_DRV_TYPE_SHIFT;
	}
	if (cfg->params[PROP_ID_SLEW_RATE_RISING] != -1) {
		reg &= ~(TEGRA_GRP_DRV_DRVDN_SLWR_MASK <<
		    TEGRA_GRP_DRV_DRVDN_SLWR_SHIFT);
		reg |=  (cfg->params[PROP_ID_SLEW_RATE_RISING] &
		    TEGRA_GRP_DRV_DRVDN_SLWR_MASK) <<
		    TEGRA_GRP_DRV_DRVDN_SLWR_SHIFT;
	}
	if (cfg->params[PROP_ID_SLEW_RATE_FALLING] != -1) {
		reg &= ~(TEGRA_GRP_DRV_DRVUP_SLWF_MASK <<
		    TEGRA_GRP_DRV_DRVUP_SLWF_SHIFT);
		reg |=  (cfg->params[PROP_ID_SLEW_RATE_FALLING] &
		    TEGRA_GRP_DRV_DRVUP_SLWF_MASK) <<
		    TEGRA_GRP_DRV_DRVUP_SLWF_SHIFT;
	}
	if ((cfg->params[PROP_ID_DRIVE_DOWN_STRENGTH] != -1) &&
		 (grp->drvdn_mask != -1)) {
		reg &= ~(grp->drvdn_shift << grp->drvdn_mask);
		reg |=  (cfg->params[PROP_ID_DRIVE_DOWN_STRENGTH] &
		    grp->drvdn_mask) << grp->drvdn_shift;
	}
	if ((cfg->params[PROP_ID_DRIVE_UP_STRENGTH] != -1) &&
		 (grp->drvup_mask != -1)) {
		reg &= ~(grp->drvup_shift << grp->drvup_mask);
		reg |=  (cfg->params[PROP_ID_DRIVE_UP_STRENGTH] &
		    grp->drvup_mask) << grp->drvup_shift;
	}
	bus_write_4(sc->pad_mem_res, grp->reg, reg);
	return (0);
}

static int
pinmux_config_node(struct pinmux_softc *sc, char *pin_name, struct pincfg *cfg)
{
	const struct tegra_mux *mux;
	const struct tegra_grp *grp;
	uint32_t reg;
	int rv;

	/* Handle MIPI special case first */
	if (strcmp(pin_name, "dsi_b") == 0) {
		if (cfg->function == NULL) {
			/* nothing to set */
			return (0);
		}
		reg = bus_read_4(sc->mipi_mem_res, 0); /* register 0x820 */
		if (strcmp(cfg->function, "csi") == 0)
			reg &= ~(1 << 1);
		else if (strcmp(cfg->function, "dsi_b") == 0)
			reg |= (1 << 1);
		bus_write_4(sc->mipi_mem_res, 0, reg); /* register 0x820 */
	}

	/* Handle pin muxes */
	mux = pinmux_search_mux(pin_name);
	if (mux != NULL) {
		if (mux->gpio_num != -1) {
			/* XXXX TODO: Reserve gpio here */
		}
		rv = pinmux_config_mux(sc, pin_name, mux, cfg);
		return (rv);
	}

	/* Handle pin groups */
	grp = pinmux_search_grp(pin_name);
	if (grp != NULL) {
		rv = pinmux_config_grp(sc, pin_name, grp, cfg);
		return (rv);
	}

	device_printf(sc->dev, "Unknown pin: %s\n", pin_name);
	return (ENXIO);
}

static int
pinmux_read_node(struct pinmux_softc *sc, phandle_t node, struct pincfg *cfg,
    char **pins, int *lpins)
{
	int rv, i;

	*lpins = OF_getprop_alloc(node, "nvidia,pins", (void **)pins);
	if (*lpins <= 0)
		return (ENOENT);

	/* Read function (mux) settings. */
	rv = OF_getprop_alloc(node, "nvidia,function",
	    (void **)&cfg->function);
	if (rv <= 0)
		cfg->function = NULL;

	/* Read numeric properties. */
	for (i = 0; i < PROP_ID_MAX_ID; i++) {
		rv = OF_getencprop(node, prop_names[i].name, &cfg->params[i],
		    sizeof(cfg->params[i]));
		if (rv <= 0)
			cfg->params[i] = -1;
	}
	return (0);
}

static int
pinmux_process_node(struct pinmux_softc *sc, phandle_t node)
{
	struct pincfg cfg;
	char *pins, *pname;
	int i, len, lpins, rv;

	rv = pinmux_read_node(sc, node, &cfg, &pins, &lpins);
	if (rv != 0)
		return (rv);

	len = 0;
	pname = pins;
	do {
		i = strlen(pname) + 1;
		rv = pinmux_config_node(sc, pname, &cfg);
		if (rv != 0)
			device_printf(sc->dev,
			    "Cannot configure pin: %s: %d\n", pname, rv);

		len += i;
		pname += i;
	} while (len < lpins);

	if (pins != NULL)
		OF_prop_free(pins);
	if (cfg.function != NULL)
		OF_prop_free(cfg.function);
	return (rv);
}

static int pinmux_configure(device_t dev, phandle_t cfgxref)
{
	struct pinmux_softc *sc;
	phandle_t node, cfgnode;
	int rv;

	sc = device_get_softc(dev);
	cfgnode = OF_node_from_xref(cfgxref);


	for (node = OF_child(cfgnode); node != 0; node = OF_peer(node)) {
		if (!ofw_bus_node_status_okay(node))
			continue;
		rv = pinmux_process_node(sc, node);
	}
	return (0);
}

static int
pinmux_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "Tegra pin configuration");
	return (BUS_PROBE_DEFAULT);
}

static int
pinmux_detach(device_t dev)
{

	/* This device is always present. */
	return (EBUSY);
}

static int
pinmux_attach(device_t dev)
{
	struct pinmux_softc * sc;
	int rid;

	sc = device_get_softc(dev);
	sc->dev = dev;

	rid = 0;
	sc->pad_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->pad_mem_res == NULL) {
		device_printf(dev, "Cannot allocate memory resources\n");
		return (ENXIO);
	}

	rid = 1;
	sc->mux_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mux_mem_res == NULL) {
		device_printf(dev, "Cannot allocate memory resources\n");
		return (ENXIO);
	}

	rid = 2;
	sc->mipi_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mipi_mem_res == NULL) {
		device_printf(dev, "Cannot allocate memory resources\n");
		return (ENXIO);
	}

	/* Register as a pinctrl device and process default configuration */
	fdt_pinctrl_register(dev, NULL);
	fdt_pinctrl_configure_by_name(dev, "boot");

	return (0);
}


static device_method_t tegra_pinmux_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,         pinmux_probe),
	DEVMETHOD(device_attach,        pinmux_attach),
	DEVMETHOD(device_detach,        pinmux_detach),

	/* fdt_pinctrl interface */
	DEVMETHOD(fdt_pinctrl_configure,pinmux_configure),

	DEVMETHOD_END
};

static devclass_t tegra_pinmux_devclass;
static DEFINE_CLASS_0(pinmux, tegra_pinmux_driver, tegra_pinmux_methods,
    sizeof(struct pinmux_softc));
EARLY_DRIVER_MODULE(tegra_pinmux, simplebus, tegra_pinmux_driver,
    tegra_pinmux_devclass, NULL, NULL, 71);
