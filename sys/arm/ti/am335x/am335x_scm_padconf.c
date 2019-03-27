/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Damjan Marion <dmarion@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/intr.h>
#include <sys/gpio.h>

#include <arm/ti/tivar.h>
#include <arm/ti/ti_pinmux.h>

#include <arm/ti/am335x/am335x_scm_padconf.h>

#define _PIN(r, b, gp, gm, m0, m1, m2, m3, m4, m5, m6, m7) \
	{	.reg_off = r, \
		.gpio_pin = gp, \
		.gpio_mode = gm, \
		.ballname = b, \
		.muxmodes[0] = m0, \
		.muxmodes[1] = m1, \
		.muxmodes[2] = m2, \
		.muxmodes[3] = m3, \
		.muxmodes[4] = m4, \
		.muxmodes[5] = m5, \
		.muxmodes[6] = m6, \
		.muxmodes[7] = m7, \
	}

const static struct ti_pinmux_padstate ti_padstate_devmap[] = {
	{"output",		PADCONF_OUTPUT },
	{"output_pullup",	PADCONF_OUTPUT_PULLUP },
	{"input",		PADCONF_INPUT },
	{"input_pulldown",	PADCONF_INPUT_PULLDOWN },
	{"input_pullup",	PADCONF_INPUT_PULLUP },
	{"i2c",			PADCONF_INPUT_PULLUP_SLOW },
	{ .state = NULL }
};

const static struct ti_pinmux_padconf ti_padconf_devmap[] = {
	_PIN(0x000, "GPMC_AD0",		32, 7,"gpmc_ad0", "mmc1_dat0", NULL, NULL, NULL, NULL, NULL, "gpio1_0"),
	_PIN(0x004, "GPMC_AD1",		33, 7,"gpmc_ad1", "mmc1_dat1", NULL, NULL, NULL, NULL, NULL, "gpio1_1"),
	_PIN(0x008, "GPMC_AD2",		34, 7,"gpmc_ad2", "mmc1_dat2", NULL, NULL, NULL, NULL, NULL, "gpio1_2"),
	_PIN(0x00C, "GPMC_AD3",		35, 7,"gpmc_ad3", "mmc1_dat3", NULL, NULL, NULL, NULL, NULL, "gpio1_3"),
	_PIN(0x010, "GPMC_AD4",		36, 7,"gpmc_ad4", "mmc1_dat4", NULL, NULL, NULL, NULL, NULL, "gpio1_4"),
	_PIN(0x014, "GPMC_AD5",		37, 7,"gpmc_ad5", "mmc1_dat5", NULL, NULL, NULL, NULL, NULL, "gpio1_5"),
	_PIN(0x018, "GPMC_AD6",		38, 7,"gpmc_ad6", "mmc1_dat6", NULL, NULL, NULL, NULL, NULL, "gpio1_6"),
	_PIN(0x01C, "GPMC_AD7",		39, 7,"gpmc_ad7", "mmc1_dat7", NULL, NULL, NULL, NULL, NULL, "gpio1_7"),
	_PIN(0x020, "GPMC_AD8",		22, 7, "gpmc_ad8", "lcd_data23", "mmc1_dat0", "mmc2_dat4", "ehrpwm2A", NULL, NULL, "gpio0_22"),
	_PIN(0x024, "GPMC_AD9",		23, 7, "gpmc_ad9", "lcd_data22", "mmc1_dat1", "mmc2_dat5", "ehrpwm2B", NULL, NULL, "gpio0_23"),
	_PIN(0x028, "GPMC_AD10",	26, 7, "gpmc_ad10", "lcd_data21", "mmc1_dat2", "mmc2_dat6", "ehrpwm2_tripzone_in", NULL, NULL, "gpio0_26"),
	_PIN(0x02C, "GPMC_AD11",	27, 7, "gpmc_ad11", "lcd_data20", "mmc1_dat3", "mmc2_dat7", "ehrpwm0_synco", NULL, NULL, "gpio0_27"),
	_PIN(0x030, "GPMC_AD12",	44, 7, "gpmc_ad12", "lcd_data19", "mmc1_dat4", "mmc2_dat0", "eQEP2A_in", "pr1_mii0_txd2", "pr1_pru0_pru_r30_14", "gpio1_12"),
	_PIN(0x034, "GPMC_AD13",	45, 7, "gpmc_ad13", "lcd_data18", "mmc1_dat5", "mmc2_dat1", "eQEP2B_in", "pr1_mii0_txd1", "pr1_pru0_pru_r30_15", "gpio1_13"),
	_PIN(0x038, "GPMC_AD14",	46, 7, "gpmc_ad14", "lcd_data17", "mmc1_dat6", "mmc2_dat2", "eQEP2_index", "pr1_mii0_txd0", "pr1_pru0_pru_r31_14", "gpio1_14"),
	_PIN(0x03C, "GPMC_AD15",	47, 7, "gpmc_ad15", "lcd_data16", "mmc1_dat7", "mmc2_dat3", "eQEP2_strobe", "pr1_ecap0_ecap_capin_apwm_o", "pr1_pru0_pru_r31_15", "gpio1_15"),
	_PIN(0x040, "GPMC_A0",		48, 7, "gpmc_a0", "gmii2_txen", "rgmii2_tctl", "rmii2_txen", "gpmc_a16", "pr1_mii_mt1_clk", "ehrpwm1_tripzone_input", "gpio1_16"),
	_PIN(0x044, "GPMC_A1",		49, 7, "gpmc_a1", "gmii2_rxdv", "rgmii2_rctl", "mmc2_dat0", "gpmc_a17", "pr1_mii1_txd3", "ehrpwm0_synco", "gpio1_17"),
	_PIN(0x048, "GPMC_A2",		50, 7, "gpmc_a2", "gmii2_txd3", "rgmii2_td3", "mmc2_dat1", "gpmc_a18", "pr1_mii1_txd2", "ehrpwm1A", "gpio1_18"),
	_PIN(0x04C, "GPMC_A3",		51, 7, "gpmc_a3", "gmii2_txd2", "rgmii2_td2", "mmc2_dat2", "gpmc_a19", "pr1_mii1_txd1", "ehrpwm1B", "gpio1_19"),
	_PIN(0x050, "GPMC_A4",		52, 7, "gpmc_a4", "gmii2_txd1", "rgmii2_td1", "rmii2_tdx1", "gpmc_a20", "pr1_mii1_txd0", "eQEP1A_in", "gpio1_20"),
	_PIN(0x054, "GPMC_A5",		53, 7, "gpmc_a5", "gmii2_txd0", "rgmii2_td0", "rmii2_txd0", "gpmc_a21", "pr1_mii1_rxd3", "eQEP1B_in", "gpio1_21"),
	_PIN(0x058, "GPMC_A6",		54, 7, "gpmc_a6", "gmii2_txclk", "rgmii2_tclk", "mmc2_dat4", "gpmc_a22", "pr1_mii1_rxd2", "eQEP1_index", "gpio1_22"),
	_PIN(0x05C, "GPMC_A7",		55, 7, "gpmc_a7", "gmii2_rxclk", "rgmii2_rclk", "mmc2_dat5", "gpmc_a23", "pr1_mii1_rxd1", "eQEP1_strobe", "gpio1_23"),
	_PIN(0x060, "GPMC_A8",		56, 7, "gpmc_a8", "gmii2_rxd3", "rgmii2_rd3", "mmc2_dat6", "gpmc_a24", "pr1_mii1_rxd0", "mcasp0_aclkx", "gpio1_24"),
	_PIN(0x064, "GPMC_A9",		57, 7, "gmpc_a9", "gmii2_rxd2", "rgmii2_rd2", "mmc2_dat7 / rmii2_crs_dv", "gpmc_a25", "pr1_mii_mr1_clk", "mcasp0_fsx", "gpio1_25"),
	_PIN(0x068, "GPMC_A10",		58, 7, "gmpc_a10", "gmii2_rxd1", "rgmii2_rd1", "rmii2_rxd1", "gpmc_a26", "pr1_mii1_rxdv", "mcasp0_arx0", "gpio1_26"),
	_PIN(0x06C, "GPMC_A11",		59, 7, "gmpc_a11", "gmii2_rxd0", "rgmii2_rd0", "rmii2_rxd0", "gpmc_a27", "pr1_mii1_rxer", "mcasp0_axr1", "gpio1_27"),
	_PIN(0x070, "GPMC_WAIT0",	30, 7, "gpmc_wait0", "gmii2_crs", "gpmc_csn4", "rmii2_crs_dv", "mmc1_sdcd", "pr1_mii1_col", "uart4_rxd", "gpio0_30"),
	_PIN(0x074, "GPMC_WPn",		31, 7, "gpmc_wpn", "gmii2_rxerr", "gpmc_csn5", "rmii2_rxerr", "mmc2_sdcd", "pr1_mii1_txen", "uart4_txd", "gpio0_31"),
	_PIN(0x078, "GPMC_BEn1",	60, 7, "gpmc_be1n", "gmii2_col", "gmpc_csn6","mmc2_dat3", "gpmc_dir", "pr1_mii1_rxlink", "mcasp0_aclkr", "gpio1_28"),
	_PIN(0x07c, "GPMC_CSn0",	61, 7, "gpmc_csn0", NULL, NULL, NULL, NULL, NULL, NULL, "gpio1_29"),
	_PIN(0x080, "GPMC_CSn1",	62, 7, "gpmc_csn1", "gpmc_clk", "mmc1_clk", "pr1_edio_data_in6", "pr1_edio_data_out6", "pr1_pru1_pru_r30_12", "pr1_pru1_pru_r31_12", "gpio1_30"),
	_PIN(0x084, "GPMC_CSn2",	63, 7, "gpmc_csn2", "gpmc_be1n", "mmc1_cmd", "pr1_edio_data_in7", "pr1_edio_data_out7", "pr1_pru1_pru_r30_13", "pr1_pru1_pru_r31_13", "gpio1_31"),
	_PIN(0x088, "GPMC_CSn3",	64, 7, "gpmc_csn3", "gpmc_a3", "rmii2_crs_dv", "mmc2_cmd", "pr1_mii0_crs", "pr1_mdio_data", "EMU4", "gpio2_0"),
	_PIN(0x08c, "GPMC_CLK",		65, 7, "gpmc_clk", "lcd_memory_clk", "gpmc_wait1", "mmc2_clk", "pr1_mii1_crs", "pr1_mdio_mdclk", "mcasp0_fsr", "gpio2_1"),
	_PIN(0x090, "GPMC_ADVn_ALE",	66, 7, "gpmc_advn_ale", NULL, "timer4", NULL, NULL, NULL, NULL, "gpio2_2"),
	_PIN(0x094, "GPMC_OEn_REn",	67, 7, "gpmc_oen_ren", NULL, "timer7", NULL, NULL, NULL, NULL, "gpio2_3"),
	_PIN(0x098, "GPMC_WEn",		68, 7, "gpmc_wen", NULL, "timer6", NULL, NULL, NULL, NULL, "gpio2_4"),
	_PIN(0x09c, "GPMC_BEn0_CLE",	67, 7, "gpmc_ben0_cle", NULL, "timer5", NULL, NULL, NULL, NULL, "gpio2_5"),
	_PIN(0x0a0, "LCD_DATA0",	68, 7, "lcd_data0", "gpmc_a0", "pr1_mii_mt0_clk", "ehrpwm2A", NULL, "pr1_pru1_pru_r30_0", "pr1_pru1_pru_r31_0", "gpio2_6"),
	_PIN(0x0a4, "LCD_DATA1",	69, 7, "lcd_data1", "gpmc_a1", "pr1_mii0_txen", "ehrpwm2B", NULL, "pr1_pru1_pru_r30_1", "pr1_pru1_pru_r31_1", "gpio2_7"),
	_PIN(0x0a8, "LCD_DATA2",	70, 7, "lcd_data2", "gpmc_a2", "pr1_mii0_txd3", "ehrpwm2_tripzone_input", NULL, "pr1_pru1_pru_r30_2", "pr1_pru1_pru_r31_2", "gpio2_8"),
	_PIN(0x0ac, "LCD_DATA3",	71, 7, "lcd_data3", "gpmc_a3", "pr1_mii0_txd2", "ehrpwm0_synco", NULL, "pr1_pru1_pru_r30_3", "pr1_pru1_pru_r31_3", "gpio2_9"),
	_PIN(0x0b0, "LCD_DATA4",	72, 7, "lcd_data4", "gpmc_a4", "pr1_mii0_txd1", "eQEP2A_in", NULL, "pr1_pru1_pru_r30_4", "pr1_pru1_pru_r31_4", "gpio2_10"),
	_PIN(0x0b4, "LCD_DATA5",	73, 7, "lcd_data5", "gpmc_a5", "pr1_mii0_txd0", "eQEP2B_in", NULL, "pr1_pru1_pru_r30_5", "pr1_pru1_pru_r31_5", "gpio2_11"),
	_PIN(0x0b8, "LCD_DATA6",	74, 7, "lcd_data6", "gpmc_a6", "pr1_edio_data_in6", "eQEP2_index", "pr1_edio_data_out6", "pr1_pru1_pru_r30_6", "pr1_pru1_pru_r31_6", "gpio2_12"),
	_PIN(0x0bc, "LCD_DATA7",	75, 7, "lcd_data7", "gpmc_a7", "pr1_edio_data_in7", "eQEP2_strobe", "pr1_edio_data_out7", "pr1_pru1_pru_r30_7", "pr1_pru1_pru_r31_7", "gpio2_13"),
	_PIN(0x0c0, "LCD_DATA8",	76, 7, "lcd_data8", "gpmc_a12", "ehrpwm1_tripzone_input", "mcasp0_aclkx", "uart5_txd", "pr1_mii0_rxd3", "uart2_ctsn", "gpio2_14"),
	_PIN(0x0c4, "LCD_DATA9",	76, 7, "lcd_data9", "gpmc_a13", "ehrpwm0_synco", "mcasp0_fsx", "uart5_rxd", "pr1_mii0_rxd2", "uart2_rtsn", "gpio2_15"),
	_PIN(0x0c8, "LCD_DATA10",	77, 7, "lcd_data10", "gpmc_a14", "ehrpwm1A", "mcasp0_axr0", NULL, "pr1_mii0_rxd1", "uart3_ctsn", "gpio2_16"),
	_PIN(0x0cc, "LCD_DATA11",	78, 7, "lcd_data11", "gpmc_a15", "ehrpwm1B", "mcasp0_ahclkr", "mcasp0_axr2", "pr1_mii0_rxd0", "uart3_rtsn", "gpio2_17"),
	_PIN(0x0d0, "LCD_DATA12",	8, 7, "lcd_data12", "gpmc_a16", "eQEP1A_in", "mcasp0_aclkr", "mcasp0_axr2", "pr1_mii0_rxlink", "uart4_ctsn", "gpio0_8"),
	_PIN(0x0d4, "LCD_DATA13",	9, 7, "lcd_data13", "gpmc_a17", "eQEP1B_in", "mcasp0_fsr", "mcasp0_axr3", "pr1_mii0_rxer", "uart4_rtsn", "gpio0_9"),
	_PIN(0x0d8, "LCD_DATA14",	10, 7, "lcd_data14", "gpmc_a18", "eQEP1_index", "mcasp0_axr1", "uart5_rxd", "pr1_mii_mr0_clk", "uart5_ctsn", "gpio0_10"),
	_PIN(0x0dc, "LCD_DATA15",	11, 7, "lcd_data15", "gpmc_a19", "eQEP1_strobe", "mcasp0_ahclkx", "mcasp0_axr3", "pr1_mii0_rxdv", "uart5_rtsn", "gpio0_11"),
	_PIN(0x0e0, "LCD_VSYNC",	86, 7, "lcd_vsync", "gpmc_a8", "gpmc_a1", "pr1_edio_data_in2", "pr1_edio_data_out2", "pr1_pru1_pru_r30_8", "pr1_pru1_pru_r31_8", "gpio2_22"),
	_PIN(0x0e4, "LCD_HSYNC",	87, 7, "lcd_hsync", "gmpc_a9", "gpmc_a2", "pr1_edio_data_in3", "pr1_edio_data_out3", "pr1_pru1_pru_r30_9", "pr1_pru1_pru_r31_9", "gpio2_23"),
	_PIN(0x0e8, "LCD_PCLK",		88, 7, "lcd_pclk", "gpmc_a10", "pr1_mii0_crs", "pr1_edio_data_in4", "pr1_edio_data_out4", "pr1_pru1_pru_r30_10", "pr1_pru1_pru_r31_10", "gpio2_24"),
	_PIN(0x0ec, "LCD_AC_BIAS_EN",	89, 7, "lcd_ac_bias_en", "gpmc_a11", "pr1_mii1_crs", "pr1_edio_data_in5", "pr1_edio_data_out5", "pr1_pru1_pru_r30_11", "pr1_pru1_pru_r31_11", "gpio2_25"),
	_PIN(0x0f0, "MMC0_DAT3",	90, 7, "mmc0_dat3", "gpmc_a20", "uart4_ctsn", "timer5", "uart1_dcdn", "pr1_pru0_pru_r30_8", "pr1_pru0_pru_r31_8", "gpio2_26"),
	_PIN(0x0f4, "MMC0_DAT2",	91, 7, "mmc0_dat2", "gpmc_a21", "uart4_rtsn", "timer6", "uart1_dsrn", "pr1_pru0_pru_r30_9", "pr1_pru0_pru_r31_9", "gpio2_27"),
	_PIN(0x0f8, "MMC0_DAT1",	92, 7, "mmc0_dat1", "gpmc_a22", "uart5_ctsn", "uart3_rxd", "uart1_dtrn", "pr1_pru0_pru_r30_10", "pr1_pru0_pru_r31_10", "gpio2_28"),
	_PIN(0x0fc, "MMC0_DAT0",	93, 7, "mmc0_dat0", "gpmc_a23", "uart5_rtsn", "uart3_txd", "uart1_rin", "pr1_pru0_pru_r30_11", "pr1_pru0_pru_r31_11", "gpio2_29"),
	_PIN(0x100, "MMC0_CLK",		94, 7, "mmc0_clk", "gpmc_a24", "uart3_ctsn", "uart2_rxd", "dcan1_tx", "pr1_pru0_pru_r30_12", "pr1_pru0_pru_r31_12", "gpio2_30"),
	_PIN(0x104, "MMC0_CMD",		95, 7, "mmc0_cmd", "gpmc_a25", "uart3_rtsn", "uart2_txd", "dcan1_rx", "pr1_pru0_pru_r30_13", "pr1_pru0_pru_r31_13", "gpio2_31"),
	_PIN(0x108, "MII1_COL",		96, 7, "gmii1_col", "rmii2_refclk", "spi1_sclk", "uart5_rxd", "mcasp1_axr2", "mmc2_dat3", "mcasp0_axr2", "gpio3_0"),
	_PIN(0x10c, "MII1_CRS",		97, 7, "gmii1_crs", "rmii1_crs_dv", "spi1_d0", "I2C1_SDA", "mcasp1_aclkx", "uart5_ctsn", "uart2_rxd", "gpio3_1"),
	_PIN(0x110, "MII1_RX_ER",	98, 7, "gmii1_rxerr", "rmii1_rxerr", "spi1_d1", "I2C1_SCL", "mcasp1_fsx", "uart5_rtsn", "uart2_txd", "gpio3_2"),
	_PIN(0x114, "MII1_TX_EN",	99, 7, "gmii1_txen", "rmii1_txen", "rgmii1_tctl", "timer4", "mcasp1_axr0", "eQEP0_index", "mmc2_cmd", "gpio3_3"),
	_PIN(0x118, "MII1_RX_DV",	100, 7, "gmii1_rxdv", "cd_memory_clk", "rgmii1_rctl", "uart5_txd", "mcasp1_aclkx", "mmc2_dat0", "mcasp0_aclkr", "gpio3_4"),
	_PIN(0x11c, "MII1_TXD3",	16, 7, "gmii1_txd3", "dcan0_tx", "rgmii1_td3", "uart4_rxd", "mcasp1_fsx", "mmc2_dat1", "mcasp0_fsr", "gpio0_16"),
	_PIN(0x120, "MII1_TXD2",	17, 7, "gmii1_txd2", "dcan0_rx", "rgmii1_td2", "uart4_txd", "mcasp1_axr0", "mmc2_dat2", "mcasp0_ahclkx", "gpio0_17"),
	_PIN(0x124, "MII1_TXD1",	21, 7, "gmii1_txd1", "rmii1_txd1", "rgmii1_td1", "mcasp1_fsr", "mcasp1_axr1", "eQEP0A_in", "mmc1_cmd", "gpio0_21"),
	_PIN(0x128, "MII1_TXD0",	28, 7, "gmii1_txd0", "rmii1_txd0", "rgmii1_td0", "mcasp1_axr2", "mcasp1_aclkr", "eQEP0B_in", "mmc1_clk", "gpio0_28"),
	_PIN(0x12c, "MII1_TX_CLK",	105, 7, "gmii1_txclk", "uart2_rxd", "rgmii1_tclk", "mmc0_dat7", "mmc1_dat0", "uart1_dcdn", "mcasp0_aclkx", "gpio3_9"),
	_PIN(0x130, "MII1_RX_CLK",	106, 7, "gmii1_rxclk", "uart2_txd", "rgmii1_rclk", "mmc0_dat6", "mmc1_dat1", "uart1_dsrn", "mcasp0_fsx", "gpio3_10"),
	_PIN(0x134, "MII1_RXD3",	82, 7, "gmii1_rxd3", "uart3_rxd", "rgmii1_rd3", "mmc0_dat5", "mmc1_dat2", "uart1_dtrn", "mcasp0_axr0", "gpio2_18"),
	_PIN(0x138, "MII1_RXD2",	83, 7, "gmii1_rxd2", "uart3_txd", "rgmii1_rd2", "mmc0_dat4", "mmc1_dat3", "uart1_rin", "mcasp0_axr1", "gpio2_19"),
	_PIN(0x13c, "MII1_RXD1",	84, 7, "gmii1_rxd1", "rmii1_rxd1", "rgmii1_rd1", "mcasp1_axr3", "mcasp1_fsr", "eQEP0_strobe", "mmc2_clk", "gpio2_20"),
	_PIN(0x140, "MII1_RXD0",	85, 7, "gmii1_rxd0", "rmii1_rxd0", "rgmii1_rd0", "mcasp1_ahclkx", "mcasp1_ahclkr", "mcasp1_aclkr", "mcasp0_axr3", "gpio2_21"),
	_PIN(0x144, "RMII1_REF_CLK",	29, 7, "rmii1_refclk", "xdma_event_intr2", "spi1_cs0", "uart5_txd", "mcasp1_axr3", "mmc0_pow", "mcasp1_ahclkx", "gpio0_29"),
	_PIN(0x148, "MDIO",		0, 7, "mdio_data", "timer6", "uart5_rxd", "uart3_ctsn", "mmc0_sdcd","mmc1_cmd", "mmc2_cmd","gpio0_0"),
	_PIN(0x14c, "MDC",		1, 7, "mdio_clk", "timer5", "uart5_txd", "uart3_rtsn", "mmc0_sdwp", "mmc1_clk", "mmc2_clk", "gpio0_1"),
	_PIN(0x150, "SPI0_SCLK",	2, 7, "spi0_sclk", "uart2_rxd", "I2C2_SDA", "ehrpwm0A", "pr1_uart0_cts_n", "pr1_edio_sof", "EMU2", "gpio0_2"),
	_PIN(0x154, "SPI0_D0",		3, 7, "spi0_d0", "uart2_txd", "I2C2_SCL", "ehrpwm0B", "pr1_uart0_rts_n", "pr1_edio_latch_in", "EMU3", "gpio0_3"),
	_PIN(0x158, "SPI0_D1",		4, 7, "spi0_d1", "mmc1_sdwp", "I2C1_SDA", "ehrpwm0_tripzone_input", "pr1_uart0_rxd", "pr1_edio_data_in0", "pr1_edio_data_out0", "gpio0_4"),
	_PIN(0x15c, "SPI0_CS0",		5, 7, "spi0_cs0", "mmc2_sdwp", "I2C1_SCL", "ehrpwm0_synci", "pr1_uart0_txd", "pr1_edio_data_in1", "pr1_edio_data_out1", "gpio0_5"),
	_PIN(0x160, "SPI0_CS1",		6, 7, "spi0_cs1", "uart3_rxd", "eCAP1_in_PWM1_out", "mcc0_pow", "xdm_event_intr2", "mmc0_sdcd", "EMU4", "gpio0_6"),
	_PIN(0x164, "ECAP0_IN_PWM0_OUT",7, 7, "eCAP0_in_PWM0_out", "uart3_txd", "spi1_cs1", "pr1_ecap0_ecap_capin_apwm_o", "spi1_sclk", "mmc0_sdwp", "xdma_event_intr2", "gpio0_7"),
	_PIN(0x168, "UART0_CTSn",	40, 7, "uart0_ctsn", "uart4_rxd", "dcan1_tx", "I2C1_SDA", "spi1_d0", "timer7", "pr1_edc_sync0_out", "gpio1_8"),
	_PIN(0x16c, "UART0_RTSn",	41, 7, "uart0_rtsn", "uart4_txd", "dcan1_rx", "I2C1_SCL", "spi1_d1", "spi1_cs0", "pr1_edc_sync1_out", "gpio1_9"),
	_PIN(0x170, "UART0_rxd",	42, 7, "uart0_rxd", "spi1_cs0", "dcan0_tx", "I2C2_SDA", "eCAP2_in_PWM2_out", "pr1_pru1_pru_r30_14", "pr1_pru1_pru_r31_14", "gpio1_10"),
	_PIN(0x174, "UART0_txd",	43, 7, "uart0_txd", "spi1_cs1", "dcan0_rx", "I2C2_SCL", "eCAP1_in_PWM1_out", "pr1_pru1_pru_r30_15", "pr1_pru1_pru_r31_15", "gpio1_11"),
	_PIN(0x178, "UART1_CTSn",	12, 7, "uart1_ctsn", "timer6_mux1", "dcan0_tx", "I2C2_SDA", "spi1_cs0", "pr1_uart0_cts_n", "pr1_edc_latch0_in", "gpio0_12"),
	_PIN(0x17c, "UART1_RTSn",	13, 7, "uart1_rtsn", "timer5_mux1", "dcan0_rx", "I2C2_SCL", "spi1_cs1", "pr1_uart0_rts_n", "pr1_edc_latch1_in", "gpio0_13"),
	_PIN(0x180, "UART1_RXD",	14, 7, "uart1_rxd", "mmc1_sdwp", "dcan1_tx", "I2C1_SDA", NULL, "pr1_uart0_rxd", "pr1_pru1_pru_r31_16", "gpio0_14"),
	_PIN(0x184, "UART1_TXD",	15, 7, "uart1_txd", "mmc2_sdwp", "dcan1_rx", "I2C1_SCL", NULL, "pr1_uart0_txd", "pr1_pru0_pru_r31_16", "gpio0_15"),
	_PIN(0x188, "I2C0_SDA",		101, 7, "I2C0_SDA", "timer4", "uart2_ctsn", "eCAP2_in_PWM2_out", NULL, NULL, NULL, "gpio3_5"),
	_PIN(0x18c, "I2C0_SCL",		102, 7, "I2C0_SCL", "timer7", "uart2_rtsn", "eCAP1_in_PWM1_out", NULL, NULL, NULL, "gpio3_6"),
	_PIN(0x190, "MCASP0_ACLKX",	110, 7, "mcasp0_aclkx", "ehrpwm0A", NULL, "spi1_sclk", "mmc0_sdcd", "pr1_pru0_pru_r30_0", "pr1_pru0_pru_r31_0", "gpio3_14"),
	_PIN(0x194, "MCASP0_FSX",	111, 7, "mcasp0_fsx", "ehrpwm0B", NULL, "spi1_d0", "mmc1_sdcd", "pr1_pru0_pru_r30_1", "pr1_pru0_pru_r31_1", "gpio3_15"),
	_PIN(0x198, "MCASP0_AXR0",	112, 7, "mcasp0_axr0", "ehrpwm0_tripzone_input", NULL, "spi1_d1", "mmc2_sdcd", "pr1_pru0_pru_r30_2", "pr1_pru0_pru_r31_2", "gpio3_16"),
	_PIN(0x19c, "MCASP0_AHCLKR",	113, 7, "mcasp0_ahclkr", "ehrpwm0_synci", "mcasp0_axr2", "spi1_cs0", "eCAP2_in_PWM2_out", "pr1_pru0_pru_r30_3", "pr1_pru0_pru_r31_3", "gpio3_17"),
	_PIN(0x1a0, "MCASP0_ACLKR",	114, 7, "mcasp0_aclkr", "eQEP0A_in", "mcasp0_axr2", "mcasp1_aclkx", "mmc0_sdwp", "pr1_pru0_pru_r30_4", "pr1_pru0_pru_r31_4", "gpio3_18"),
	_PIN(0x1a4, "MCASP0_FSR",	115, 7, "mcasp0_fsr", "eQEP0B_in", "mcasp0_axr3", "mcasp1_fsx", "EMU2", "pr1_pru0_pru_r30_5", "pr1_pru0_pru_r31_5", "gpio3_19"),
	_PIN(0x1a8, "MCASP0_AXR1",	116, 7, "mcasp0_axr1", "eQEP0_index", NULL, "mcasp1_axr0", "EMU3", "pr1_pru0_pru_r30_6", "pr1_pru0_pru_r31_6", "gpio3_20"),
	_PIN(0x1ac, "MCASP0_AHCLKX",	117, 7, "mcasp0_ahclkx", "eQEP0_strobe", "mcasp0_axr3", "mcasp1_axr1", "EMU4", "pr1_pru0_pru_r30_7", "pr1_pru0_pru_r31_7", "gpio3_21"),
	_PIN(0x1b0, "XDMA_EVENT_INTR0",	19, 7, "xdma_event_intr0", NULL, "timer4", "clkout1", "spi1_cs1", "pr1_pru1_pru_r31_16", "EMU2", "gpio0_19"),
	_PIN(0x1b4, "XDMA_EVENT_INTR1",	20, 7, "xdma_event_intr1", NULL, "tclkin", "clkout2", "timer7", "pr1_pru0_pru_r31_16", "EMU3", "gpio0_20"),
#if 0
	_PIN(0x1b8, "nresetin_out",	0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x1bc, "porz",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x1c0, "nnmi",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x1c4, "osc0_in",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x1c8, "osc0_out",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x1cc, "osc0_vss",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x1d0, "tms",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x1d4, "tdi",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x1d8, "tdo",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x1dc, "tck",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x1e0, "ntrst",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
#endif
	_PIN(0x1e4, "EMU0",		103, 7, "EMU0", NULL, NULL, NULL, NULL, NULL, NULL, "gpio3_7"),
	_PIN(0x1e8, "EMU1",		104, 0, "EMU1", NULL, NULL, NULL, NULL, NULL, NULL, "gpio3_8"),
#if 0
	_PIN(0x1ec, "osc1_in",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x1f0, "osc1_out",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x1f4, "osc1_vss",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x1f8, "rtc_porz",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x1fc, "pmic_power_en",	0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x200, "ext_wakeup",	0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x204, "enz_kaldo_1p8v",	0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
#endif
	_PIN(0x208, "USB0_DM",		0, 0, "USB0_DM", NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x20c, "USB0_DP",		0, 0, "USB0_DP", NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x210, "USB0_CE",		0, 0, "USB0_CE", NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x214, "USB0_ID",		0, 0, "USB0_ID", NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x218, "USB0_VBUS",	0, 0, "USB0_VBUS", NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x21c, "USB0_DRVVBUS",	18, 7, "USB0_DRVVBUS", NULL, NULL, NULL, NULL, NULL, NULL, "gpio0_18"),
	_PIN(0x220, "USB1_DM",		0, 0, "USB1_DM", NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x224, "USB1_DP",		0, 0, "USB1_DP", NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x228, "USB1_CE",		0, 0, "USB1_CE", NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x22c, "USB1_ID",		0, 0, "USB1_ID", NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x230, "USB1_VBUS",	0, 0, "USB1_VBUS", NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x234, "USB1_DRVVBUS",	109, 7, "USB1_DRVVBUS", NULL, NULL, NULL, NULL, NULL, NULL, "gpio3_13"),
#if 0
	_PIN(0x238, "ddr_resetn",	0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x23c, "ddr_csn0",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x240, "ddr_cke",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x244, "ddr_ck",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x248, "ddr_nck",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x24c, "ddr_casn",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x250, "ddr_rasn",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x254, "ddr_wen",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x258, "ddr_ba0",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x25c, "ddr_ba1",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x260, "ddr_ba2",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x264, "ddr_a0",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x268, "ddr_a1",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x26c, "ddr_a2",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x270, "ddr_a3",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x274, "ddr_a4",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x278, "ddr_a5",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x27c, "ddr_a6",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x280, "ddr_a7",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x284, "ddr_a8",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x288, "ddr_a9",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x28c, "ddr_a10",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x290, "ddr_a11",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x294, "ddr_a12",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x298, "ddr_a13",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x29c, "ddr_a14",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2a0, "ddr_a15",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2a4, "ddr_odt",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2a8, "ddr_d0",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2ac, "ddr_d1",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2b0, "ddr_d2",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2b4, "ddr_d3",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2b8, "ddr_d4",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2bc, "ddr_d5",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2c0, "ddr_d6",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2c4, "ddr_d7",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2c8, "ddr_d8",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2cc, "ddr_d9",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2d0, "ddr_d10",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2d4, "ddr_d11",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2d8, "ddr_d12",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2dc, "ddr_d13",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2e0, "ddr_d14",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2e4, "ddr_d15",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2e8, "ddr_dqm0",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2ec, "ddr_dqm1",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2f0, "ddr_dqs0",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2f4, "ddr_dqsn0",	0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2f8, "ddr_dqs1",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x2fc, "ddr_dqsn1",	0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x300, "ddr_vref",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x304, "ddr_vtp",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x308, "ddr_strben0",	0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x30c, "ddr_strben1",	0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x32c, "ain0",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x328, "ain1",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x324, "ain2",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x320, "ain3",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x31c, "ain4",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x318, "ain5",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x314, "ain6",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x310, "ain7",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x330, "vrefp",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x334, "vrefn",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x338, "avdd",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x33c, "avss",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x340, "iforce",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x344, "vsense",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
	_PIN(0x348, "testout",		0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL),
#endif
	{  .ballname = NULL  },
};

const struct ti_pinmux_device ti_am335x_pinmux_dev = {
	.padconf_muxmode_mask	= 0x7,
	.padconf_sate_mask	= 0x78,
	.padstate		= ti_padstate_devmap,
	.padconf		= ti_padconf_devmap,
};
