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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rman.h>

#include <machine/bus.h>

#include <dev/extres/clk/clk.h>

#include <gnu/dts/include/dt-bindings/clock/tegra124-car.h>
#include "tegra124_car.h"

/* The TEGRA124_CLK_XUSB_GATE is missing in current
 * DT bindings, define it localy
 */
#ifdef TEGRA124_CLK_XUSB_GATE
#error "TEGRA124_CLK_XUSB_GATE is now defined, revisit XUSB code!"
#else
#define TEGRA124_CLK_XUSB_GATE 143
#endif

/* Bits in base register. */
#define	PERLCK_AMUX_MASK	0x0F
#define	PERLCK_AMUX_SHIFT	16
#define	PERLCK_AMUX_DIS		(1 << 20)
#define	PERLCK_UDIV_DIS		(1 << 24)
#define	PERLCK_ENA_MASK		(1 << 28)
#define	PERLCK_MUX_SHIFT	29
#define	PERLCK_MUX_MASK		0x07


struct periph_def {
	struct clknode_init_def	clkdef;
	uint32_t		base_reg;
	uint32_t		div_width;
	uint32_t		div_mask;
	uint32_t		div_f_width;
	uint32_t		div_f_mask;
	uint32_t		flags;
};

struct pgate_def {
	struct clknode_init_def	clkdef;
	uint32_t		idx;
	uint32_t		flags;
};
#define	PLIST(x) static const char *x[]

#define	GATE(_id, cname, plist, _idx)					\
{									\
	.clkdef.id = TEGRA124_CLK_##_id,				\
	.clkdef.name = cname,						\
	.clkdef.parent_names = (const char *[]){plist},			\
	.clkdef.parent_cnt = 1,						\
	.clkdef.flags = CLK_NODE_STATIC_STRINGS,			\
	.idx = _idx,							\
	.flags = 0,							\
}

/* Sources for multiplexors. */
PLIST(mux_a_N_audio_N_p_N_clkm) =
    {"pllA_out0", NULL, "audio",  NULL,
     "pllP_out0", NULL, "clk_m"};
PLIST(mux_a_N_audio0_N_p_N_clkm) =
    {"pllA_out0", NULL, "audio0", NULL,
     "pllP_out0", NULL, "clk_m"};
PLIST(mux_a_N_audio1_N_p_N_clkm) =
    {"pllA_out0", NULL, "audio1", NULL,
     "pllP_out0", NULL, "clk_m"};
PLIST(mux_a_N_audio2_N_p_N_clkm) =
    {"pllA_out0", NULL, "audio2", NULL,
     "pllP_out0", NULL, "clk_m"};
PLIST(mux_a_N_audio3_N_p_N_clkm) =
    {"pllA_out0", NULL, "audio3", NULL,
     "pllP_out0", NULL, "clk_m"};
PLIST(mux_a_N_audio4_N_p_N_clkm) =
    {"pllA_out0", NULL, "audio4", NULL,
     "pllP_out0", NULL, "clk_m"};
PLIST(mux_a_clks_p_clkm_e) =
    {"pllA_out0", "clk_s", "pllP_out0",
     "clk_m", "pllE_out0"};
PLIST(mux_a_c2_c_c3_p_N_clkm) =
    {"pllA_out0", "pllC2_out0", "pllC_out0", "pllC3_out0",
     "pllP_out0", NULL, "clk_m"};

PLIST(mux_m_c_p_a_c2_c3) =
    {"pllM_out0", "pllC_out0", "pllP_out0", "pllA_out0",
     "pllC2_out0", "pllC3_out0"};
PLIST(mux_m_c_p_a_c2_c3_clkm) =
    {"pllM_out0", "pllC_out0", "pllP_out0", "pllA_out0",
     "pllC2_out0", "pllC3_out0", "clk_m"};
PLIST(mux_m_c_p_a_c2_c3_clkm_c4) =
    {"pllM_out0", "pllC_out0", "pllP_out0", "pllA_out0",
     "pllC2_out0", "pllC3_out0", "clk_m", "pllC4_out0"};
PLIST(mux_m_c_p_clkm_mud_c2_c3) =
    {"pllM_out0", "pllC_out0", "pllP_out0", "clk_m",
     "pllM_UD", "pllC2_out0", "pllC3_out0"};
PLIST(mux_m_c_p_clkm_mud_c2_c3_cud) =
    {"pllM_out0", "pllC_out0", "pllP_out0", "clk_m",
     "pllM_UD", "pllC2_out0", "pllC3_out0", "pllC_UD"};

PLIST(mux_m_c2_c_c3_p_N_a) =
    {"pllM_out0", "pllC2_out0", "pllC_out0", "pllC3_out0",
     "pllP_out0", NULL, "pllA_out0"};
PLIST(mux_m_c2_c_c3_p_N_a_c4) =
    {"pllM_out0", "pllC2_out0", "pllC_out0", "pllC3_out0",
     NULL, "pllA_out0", "pllC4_out0"};

PLIST(mux_p_N_c_N_N_N_clkm) =
    {"pllP_out0", NULL, "pllC_out0", NULL,
     NULL, NULL, "clk_m"};
PLIST(mux_p_N_c_N_m_N_clkm) =
    {"pllP_out0", NULL, "pllC_out0", NULL,
     "pllM_out0", NULL, "clk_m"};
PLIST(mux_p_c_c2_clkm) =
    {"pllP_out0", "pllC_out0", "pllC2_out0", "clk_m"};
PLIST(mux_p_c2_c_c3_m) =
    {"pllP_out0", "pllC2_out0", "pllC_out0", "pllC3_out0",
     "pllM_out0"};
PLIST(mux_p_c2_c_c3_m_N_clkm) =
    {"pllP_out0", "pllC2_out0", "pllC_out0", "pllC3_out0",
     "pllM_out0", NULL, "clk_m"};
PLIST(mux_p_c2_c_c3_m_e_clkm) =
    {"pllP_out0", "pllC2_out0", "pllC_out0", "pllC3_out0",
     "pllM_out0", "pllE_out0", "clk_m"};
PLIST(mux_p_c2_c_c3_m_a_clkm) =
    {"pllP_out0", "pllC2_out0", "pllC_out0", "pllC3_out0",
     "pllM_out0", "pllA_out0", "clk_m"};
PLIST(mux_p_c2_c_c3_m_clks_clkm) =
    {"pllP_out0", "pllC2_out0", "pllC_out0", "pllC3_out0",
     "pllM_out0", "clk_s", "clk_m"};
PLIST(mux_p_c2_c_c3_clks_N_clkm) =
    {"pllP_out0", "pllC2_out0", "pllC_out0", "pllC3_out0",
     "clk_s", NULL, "clk_m"};
PLIST(mux_p_c2_c_c3_clkm_N_clks) =
    {"pllP_out0", "pllC2_out0", "pllC_out0", "pllC3_out0",
     "clk_m", NULL, "clk_s"};
PLIST(mux_p_clkm_clks_E) =
    {"pllP_out0", "clk_m", "clk_s", "pllE_out0"};
PLIST(mux_p_m_d_a_c_d2_clkm) =
    {"pllP_out0", "pllM_out0", "pllD_out0", "pllA_out0",
     "pllC_out0", "pllD2_out0", "clk_m"};

PLIST(mux_clkm_N_u48_N_p_N_u480) =
    {"clk_m", NULL, "pllU_48", NULL,
     "pllP_out0", NULL, "pllU_480"};
PLIST(mux_clkm_p_c2_c_c3_refre) =
    {"clk_m", "pllP_out0", "pllC2_out0", "pllC_out0",
     "pllC3_out0", "pllREFE_out"};
PLIST(mux_clkm_refe_clks_u480_c_c2_c3_oscdiv) =
    {"clk_m", "pllREFE_out", "clk_s", "pllU_480",
     "pllC_out0", "pllC2_out0", "pllC3_out0", "osc_div_clk"};

PLIST(mux_sep_audio) =
   {"pllA_out0", "pllC2_out0", "pllC_out0", "pllC3_out0",
    "pllP_out0", NULL, "clk_m", NULL,
    "spdif_in", "i2s0", "i2s1", "i2s2",
    "i2s4", "pllA_out0", "ext_vimclk"};

static uint32_t clk_enable_reg[] = {
	CLK_OUT_ENB_L,
	CLK_OUT_ENB_H,
	CLK_OUT_ENB_U,
	CLK_OUT_ENB_V,
	CLK_OUT_ENB_W,
	CLK_OUT_ENB_X,
};

static uint32_t clk_reset_reg[] = {
	RST_DEVICES_L,
	RST_DEVICES_H,
	RST_DEVICES_U,
	RST_DEVICES_V,
	RST_DEVICES_W,
	RST_DEVICES_X,
};

#define	L(n)  ((0 * 32) + (n))
#define	H(n)  ((1 * 32) + (n))
#define	U(n)  ((2 * 32) + (n))
#define	V(n)  ((3 * 32) + (n))
#define	W(n)  ((4 * 32) + (n))
#define	X(n)  ((5 * 32) + (n))

static struct pgate_def pgate_def[] = {
	/* bank L ->  0-31 */
	/* GATE(CPU, "cpu", "clk_m", L(0)), */
	GATE(ISPB, "ispb", "clk_m", L(3)),
	GATE(RTC, "rtc", "clk_s", L(4)),
	GATE(TIMER, "timer", "clk_m", L(5)),
	GATE(UARTA, "uarta", "pc_uarta" , L(6)),
	GATE(UARTB, "uartb", "pc_uartb", L(7)),
	GATE(VFIR, "vfir", "pc_vfir", L(7)),
	/* GATE(GPIO, "gpio", "clk_m", L(8)), */
	GATE(SDMMC2, "sdmmc2", "pc_sdmmc2", L(9)),
	GATE(SPDIF_OUT, "spdif_out", "pc_spdif_out", L(10)),
	GATE(SPDIF_IN, "spdif_in", "pc_spdif_in", L(10)),
	GATE(I2S1, "i2s1", "pc_i2s1", L(11)),
	GATE(I2C1, "i2c1", "pc_i2c1", L(12)),
	GATE(SDMMC1, "sdmmc1", "pc_sdmmc1", L(14)),
	GATE(SDMMC4, "sdmmc4", "pc_sdmmc4", L(15)),
	GATE(PWM, "pwm", "pc_pwm", L(17)),
	GATE(I2S2, "i2s2", "pc_i2s2", L(18)),
	GATE(VI, "vi", "pc_vi", L(20)),
	GATE(USBD, "usbd", "clk_m", L(22)),
	GATE(ISP, "isp", "pc_isp", L(23)),
	GATE(DISP2, "disp2", "pc_disp2", L(26)),
	GATE(DISP1, "disp1", "pc_disp1", L(27)),
	GATE(HOST1X, "host1x", "pc_host1x", L(28)),
	GATE(VCP, "vcp", "clk_m", L(29)),
	GATE(I2S0, "i2s0", "pc_i2s0", L(30)),
	/* GATE(CACHE2, "ccache2", "clk_m", L(31)), */

	/* bank H -> 32-63 */
	GATE(MC, "mem", "clk_m", H(0)),
	/* GATE(AHBDMA, "ahbdma", "clk_m", H(1)), */
	GATE(APBDMA, "apbdma", "clk_m", H(2)),
	GATE(KBC, "kbc", "clk_s", H(4)),
	/* GATE(STAT_MON, "stat_mon", "clk_s", H(5)), */
	/* GATE(PMC, "pmc", "clk_s", H(6)), */
	GATE(FUSE, "fuse", "clk_m", H(7)),
	GATE(KFUSE, "kfuse", "clk_m", H(8)),
	GATE(SBC1, "spi1", "pc_spi1", H(9)),
	GATE(NOR, "snor", "pc_snor", H(10)),
	/* GATE(JTAG2TBC, "jtag2tbc", "clk_m", H(11)), */
	GATE(SBC2, "spi2", "pc_spi2", H(12)),
	GATE(SBC3, "spi3", "pc_spi3", H(14)),
	GATE(I2C5, "i2c5", "pc_i2c5", H(15)),
	GATE(DSIA, "dsia", "dsia_mux", H(16)),
	GATE(MIPI, "hsi", "pc_hsi", H(18)),
	GATE(HDMI, "hdmi", "pc_hdmi", H(19)),
	GATE(CSI, "csi", "pllP_out3", H(20)),
	GATE(I2C2, "i2c2", "pc_i2c2", H(22)),
	GATE(UARTC, "uartc", "pc_uartc", H(23)),
	GATE(MIPI_CAL, "mipi_cal", "clk_m", H(24)),
	GATE(EMC, "emc", "pc_emc_2x", H(25)),
	GATE(USB2, "usb2", "clk_m", H(26)),
	GATE(USB3, "usb3", "clk_m", H(27)),
	GATE(VDE, "vde", "pc_vde", H(29)),
	GATE(BSEA, "bsea", "clk_m", H(30)),
	GATE(BSEV, "bsev", "clk_m", H(31)),

	/* bank U  -> 64-95 */
	GATE(UARTD, "uartd", "pc_uartd", U(1)),
	GATE(I2C3, "i2c3", "pc_i2c3", U(3)),
	GATE(SBC4, "spi4", "pc_spi4", U(4)),
	GATE(SDMMC3, "sdmmc3", "pc_sdmmc3", U(5)),
	GATE(PCIE, "pcie", "clk_m", U(6)),
	GATE(OWR, "owr", "pc_owr", U(7)),
	GATE(AFI, "afi", "clk_m", U(8)),
	GATE(CSITE, "csite", "pc_csite", U(9)),
	/* GATE(AVPUCQ, "avpucq", clk_m, U(11)), */
	GATE(TRACE, "traceclkin", "pc_traceclkin", U(13)),
	GATE(SOC_THERM, "soc_therm", "pc_soc_therm", U(14)),
	GATE(DTV, "dtv", "clk_m", U(15)),
	GATE(I2CSLOW, "i2c_slow", "pc_i2c_slow", U(17)),
	GATE(DSIB, "dsib", "dsib_mux", U(18)),
	GATE(TSEC, "tsec", "pc_tsec", U(19)),
	/* GATE(IRAMA, "irama", "clk_m", U(20)), */
	/* GATE(IRAMB, "iramb", "clk_m", U(21)), */
	/* GATE(IRAMC, "iramc", "clk_m", U(22)), */
	/* GATE(IRAMD, "iramd", "clk_m", U(23)), */
	/* GATE(CRAM2, "cram2", "clk_m", U(24)), */
	GATE(XUSB_HOST, "xusb_core_host", "pc_xusb_core_host", U(25)),
	/* GATE(M_DOUBLER, "m_doubler", "clk_m", U(26)), */
	GATE(MSENC, "msenc", "pc_msenc", U(27)),
	GATE(CSUS, "sus_out", "clk_m", U(28)),
	/* GATE(DEVD2_OUT, "devd2_out", "clk_m", U(29)), */
	/* GATE(DEVD1_OUT, "devd1_out", "clk_m", U(30)), */
	GATE(XUSB_DEV, "xusb_core_dev", "pc_xusb_core_dev", U(31)),

	/* bank V  -> 96-127 */
	/* GATE(CPUG, "cpug", "clk_m", V(0)), */
	/* GATE(CPULP, "cpuLP", "clk_m", V(1)), */
	GATE(MSELECT, "mselect", "pc_mselect", V(3)),
	GATE(TSENSOR, "tsensor", "pc_tsensor", V(4)),
	GATE(I2S3, "i2s3", "pc_i2s3", V(5)),
	GATE(I2S4, "i2s4", "pc_i2s4", V(6)),
	GATE(I2C4, "i2c4", "pc_i2c4", V(7)),
	GATE(SBC5, "spi5", "pc_spi5", V(8)),
	GATE(SBC6, "spi6", "pc_spi6", V(9)),
	GATE(D_AUDIO, "audio", "pc_audio", V(10)),
	GATE(APBIF, "apbif", "clk_m", V(11)),
	GATE(DAM0, "dam0", "pc_dam0", V(12)),
	GATE(DAM1, "dam1", "pc_dam1", V(13)),
	GATE(DAM2, "dam2",  "pc_dam2", V(14)),
	GATE(HDA2CODEC_2X, "hda2codec_2x", "pc_hda2codec_2x", V(15)),
	/* GATE(ATOMICS, "atomics", "clk_m", V(16)), */
	/* GATE(SPDIF_DOUBLER, "spdif_doubler", "clk_m", V(22)), */
	GATE(ACTMON, "actmon", "pc_actmon", V(23)),
	GATE(EXTERN1, "extperiph1", "pc_extperiph1", V(24)),
	GATE(EXTERN2, "extperiph2", "pc_extperiph2", V(25)),
	GATE(EXTERN3, "extperiph3", "pc_extperiph3", V(26)),
	GATE(SATA_OOB, "sata_oob", "pc_sata_oob", V(27)),
	GATE(SATA, "sata", "pc_sata", V(28)),
	GATE(HDA, "hda", "pc_hda", V(29)),

	/* bank W   -> 128-159*/
	GATE(HDA2HDMI, "hda2hdmi", "clk_m", W(0)),
	GATE(SATA_COLD, "sata_cold", "clk_m", W(1)), /* Reset only */
	/* GATE(PCIERX0, "pcierx0", "clk_m", W(2)), */
	/* GATE(PCIERX1, "pcierx1", "clk_m", W(3)), */
	/* GATE(PCIERX2, "pcierx2", "clk_m", W(4)), */
	/* GATE(PCIERX3, "pcierx3", "clk_m", W(5)), */
	/* GATE(PCIERX4, "pcierx4", "clk_m", W(6)), */
	/* GATE(PCIERX5, "pcierx5", "clk_m", W(7)), */
	/* GATE(CEC, "cec", "clk_m", W(8)), */
	/* GATE(PCIE2_IOBIST, "pcie2_iobist", "clk_m", W(9)), */
	/* GATE(EMC_IOBIST, "emc_iobist", "clk_m", W(10)), */
	/* GATE(HDMI_IOBIST, "hdmi_iobist", "clk_m", W(11)), */
	/* GATE(SATA_IOBIST, "sata_iobist", "clk_m", W(12)), */
	/* GATE(MIPI_IOBIST, "mipi_iobist", "clk_m", W(13)), */
	GATE(XUSB_GATE, "xusb_gate", "clk_m", W(15)),
	GATE(CILAB, "cilab", "pc_cilab", W(16)),
	GATE(CILCD, "cilcd", "pc_cilcd", W(17)),
	GATE(CILE, "cile", "pc_cile", W(18)),
	GATE(DSIALP, "dsia_lp", "pc_dsia_lp", W(19)),
	GATE(DSIBLP, "dsib_lp", "pc_dsib_lp", W(20)),
	GATE(ENTROPY, "entropy", "pc_entropy", W(21)),
	GATE(AMX, "amx", "pc_amx", W(25)),
	GATE(ADX, "adx", "pc_adx", W(26)),
	GATE(DFLL_REF, "dvfs_ref", "pc_dvfs_ref", W(27)),
	GATE(DFLL_SOC, "dvfs_soc", "pc_dvfs_soc",  W(27)),
	GATE(XUSB_SS, "xusb_ss", "xusb_ss_mux", W(28)),
	/* GATE(EMC_LATENCY, "emc_latency", "pc_emc_latency", W(29)), */

	/* bank X -> 160-191*/
	/* GATE(SPARE, "spare", "clk_m", X(0)), */
	/* GATE(CAM_MCLK, "CAM_MCLK", "clk_m", X(4)), */
	/* GATE(CAM_MCLK2, "CAM_MCLK2", "clk_m", X(5)), */
	GATE(I2C6, "i2c6", "pc_i2c6", X(6)),
	GATE(VIM2_CLK, "vim2_clk", "clk_m", X(11)),
	/* GATE(EMC_DLL, "emc_dll", "pc_emc_dll", X(14)), */
	GATE(HDMI_AUDIO, "hdmi_audio", "pc_hdmi_audio", X(16)),
	GATE(CLK72MHZ, "clk72mhz", "pc_clk72mhz", X(17)),
	GATE(VIC03, "vic", "pc_vic", X(18)),
	GATE(ADX1, "adx1", "pc_adx1", X(20)),
	GATE(DPAUX, "dpaux", "clk_m", X(21)),
	GATE(SOR0_LVDS, "sor0", "pc_sor0", X(22)),
	GATE(GPU, "gpu", "osc_div_clk", X(24)),
	GATE(AMX1, "amx1", "pc_amx1", X(26)),
};

/* Peripheral clock clock */
#define	DCF_HAVE_MUX		0x0100 /* Block with multipexor */
#define	DCF_HAVE_ENA		0x0200 /* Block with enable bit */
#define	DCF_HAVE_DIV		0x0400 /* Block with divider */

/* Mark block with additional bits / functionality. */
#define	DCF_IS_MASK		0x00FF
#define	DCF_IS_UART		0x0001
#define	DCF_IS_VI		0x0002
#define	DCF_IS_HOST1X		0x0003
#define	DCF_IS_XUSB_SS		0x0004
#define	DCF_IS_EMC_DLL		0x0005
#define	DCF_IS_SATA		0x0006
#define	DCF_IS_VIC		0x0007
#define	DCF_IS_AUDIO		0x0008
#define	DCF_IS_SOR0		0x0009
#define	DCF_IS_EMC		0x000A

/* Basic pheripheral clock */
#define	PER_CLK(_id, cn, pl, r, diw, fiw, f)				\
{									\
	.clkdef.id = _id,						\
	.clkdef.name = cn,						\
	.clkdef.parent_names = pl,					\
	.clkdef.parent_cnt = nitems(pl),				\
	.clkdef.flags = CLK_NODE_STATIC_STRINGS,			\
	.base_reg = r,							\
	.div_width = diw,						\
	.div_f_width = fiw,						\
	.flags = f,							\
}

/* Mux with fractional 8.1 divider. */
#define	CLK_8_1(id, cn, pl, r,  f)					\
	PER_CLK(id, cn, pl, r,  8, 1, (f) | DCF_HAVE_MUX | DCF_HAVE_DIV)

/* Mux with fractional 16.1 divider. */
#define	CLK16_1(id, cn, pl, r,  f)					\
	PER_CLK(id, cn, pl, r,  16, 1, (f) | DCF_HAVE_MUX | DCF_HAVE_DIV)
/* Mux with integer 16bits divider. */
#define	CLK16_0(id, cn, pl, r,  f)					\
	PER_CLK(id, cn, pl, r,  16, 0, (f) | DCF_HAVE_MUX | DCF_HAVE_DIV)
/* Mux wihout divider. */
#define	CLK_0_0(id, cn, pl, r,  f)					\
	PER_CLK(id, cn, pl, r,  0, 0, (f) | DCF_HAVE_MUX)

static struct periph_def periph_def[] = {
	CLK_8_1(0, "pc_i2s1", mux_a_N_audio1_N_p_N_clkm, CLK_SOURCE_I2S1, DCF_HAVE_ENA),
	CLK_8_1(0, "pc_i2s2", mux_a_N_audio2_N_p_N_clkm, CLK_SOURCE_I2S2, DCF_HAVE_ENA),
	CLK_8_1(0, "pc_spdif_out", mux_a_N_audio_N_p_N_clkm, CLK_SOURCE_SPDIF_OUT, 0),
	CLK_8_1(0, "pc_spdif_in", mux_p_c2_c_c3_m, CLK_SOURCE_SPDIF_IN, 0),
	CLK_8_1(0, "pc_pwm", mux_p_c2_c_c3_clks_N_clkm, CLK_SOURCE_PWM, 0),
	CLK_8_1(0, "pc_spi2", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_SPI2, 0),
	CLK_8_1(0, "pc_spi3", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_SPI3, 0),
	CLK16_0(0, "pc_i2c5", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_I2C5, 0),
	CLK16_0(0, "pc_i2c1", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_I2C1, 0),
	CLK_8_1(0, "pc_spi1", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_SPI1, 0),
	CLK_0_0(0, "pc_disp1", mux_p_m_d_a_c_d2_clkm, CLK_SOURCE_DISP1, 0),
	CLK_0_0(0, "pc_disp2", mux_p_m_d_a_c_d2_clkm, CLK_SOURCE_DISP2, 0),
	CLK_8_1(0, "pc_isp", mux_m_c_p_a_c2_c3_clkm_c4, CLK_SOURCE_ISP, 0),
	CLK_8_1(0, "pc_vi", mux_m_c2_c_c3_p_N_a_c4, CLK_SOURCE_VI, DCF_IS_VI),
	CLK_8_1(0, "pc_sdmmc1", mux_p_c2_c_c3_m_e_clkm, CLK_SOURCE_SDMMC1, 0),
	CLK_8_1(0, "pc_sdmmc2", mux_p_c2_c_c3_m_e_clkm, CLK_SOURCE_SDMMC2, 0),
	CLK_8_1(0, "pc_sdmmc4", mux_p_c2_c_c3_m_e_clkm, CLK_SOURCE_SDMMC4, 0),
	CLK_8_1(0, "pc_vfir", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_VFIR, 0),
	CLK_8_1(0, "pc_hsi", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_HSI, 0),
	CLK16_1(0, "pc_uarta", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_UARTA, DCF_IS_UART),
	CLK16_1(0, "pc_uartb", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_UARTB, DCF_IS_UART),
	CLK_8_1(0, "pc_host1x", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_HOST1X, DCF_IS_HOST1X),
	CLK_8_1(0, "pc_hdmi", mux_p_m_d_a_c_d2_clkm, CLK_SOURCE_HDMI, 0),
	CLK16_0(0, "pc_i2c2", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_I2C2, 0),
	CLK_8_1(0, "pc_emc_2x", mux_m_c_p_clkm_mud_c2_c3_cud, CLK_SOURCE_EMC, DCF_IS_EMC),
	CLK16_1(0, "pc_uartc", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_UARTC, DCF_IS_UART),
	CLK_8_1(0, "pc_vi_sensor", mux_m_c2_c_c3_p_N_a, CLK_SOURCE_VI_SENSOR, 0),
	CLK_8_1(0, "pc_spi4", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_SPI4, 0),
	CLK16_0(0, "pc_i2c3", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_I2C3, 0),
	CLK_8_1(0, "pc_sdmmc3", mux_p_c2_c_c3_m_e_clkm, CLK_SOURCE_SDMMC3, 0),
	CLK16_1(0, "pc_uartd", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_UARTD, DCF_IS_UART),
	CLK_8_1(0, "pc_vde", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_VDE, 0),
	CLK_8_1(0, "pc_owr", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_OWR, 0),
	CLK_8_1(0, "pc_snor", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_NOR, 0),
	CLK_8_1(0, "pc_csite", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_CSITE, 0),
	CLK_8_1(0, "pc_i2s0", mux_a_N_audio0_N_p_N_clkm, CLK_SOURCE_I2S0, 0),
/* DTV xxx */
	CLK_8_1(0, "pc_msenc", mux_m_c2_c_c3_p_N_a, CLK_SOURCE_MSENC, 0),
	CLK_8_1(0, "pc_tsec", mux_p_c2_c_c3_m_a_clkm, CLK_SOURCE_TSEC, 0),
/* SPARE2 */


	CLK_8_1(0, "pc_mselect", mux_p_c2_c_c3_m_clks_clkm, CLK_SOURCE_MSELECT, 0),
	CLK_8_1(0, "pc_tsensor", mux_p_c2_c_c3_clkm_N_clks, CLK_SOURCE_TSENSOR, 0),
	CLK_8_1(0, "pc_i2s3", mux_a_N_audio3_N_p_N_clkm, CLK_SOURCE_I2S3, DCF_HAVE_ENA),
	CLK_8_1(0, "pc_i2s4", mux_a_N_audio4_N_p_N_clkm, CLK_SOURCE_I2S4, DCF_HAVE_ENA),
	CLK16_0(0, "pc_i2c4", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_I2C4, 0),
	CLK_8_1(0, "pc_spi5", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_SPI5, 0),
	CLK_8_1(0, "pc_spi6", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_SPI6, 0),
	CLK_8_1(0, "pc_audio", mux_sep_audio, CLK_SOURCE_AUDIO, DCF_IS_AUDIO),
	CLK_8_1(0, "pc_dam0", mux_sep_audio, CLK_SOURCE_DAM0, DCF_IS_AUDIO),
	CLK_8_1(0, "pc_dam1", mux_sep_audio, CLK_SOURCE_DAM1, DCF_IS_AUDIO),
	CLK_8_1(0, "pc_dam2",  mux_sep_audio, CLK_SOURCE_DAM2, DCF_IS_AUDIO),
	CLK_8_1(0, "pc_hda2codec_2x", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_HDA2CODEC_2X, 0),
	CLK_8_1(0, "pc_actmon", mux_p_c2_c_c3_clks_N_clkm, CLK_SOURCE_ACTMON, 0),
	CLK_8_1(0, "pc_extperiph1", mux_a_clks_p_clkm_e, CLK_SOURCE_EXTPERIPH1, 0),
	CLK_8_1(0, "pc_extperiph2", mux_a_clks_p_clkm_e, CLK_SOURCE_EXTPERIPH2,  0),
	CLK_8_1(0, "pc_extperiph3", mux_a_clks_p_clkm_e, CLK_SOURCE_EXTPERIPH3, 0),
	CLK_8_1(0, "pc_i2c_slow", mux_p_c2_c_c3_clks_N_clkm, CLK_SOURCE_I2C_SLOW, 0),
/* SYS */
	CLK_8_1(0, "pc_sor0", mux_p_m_d_a_c_d2_clkm,  CLK_SOURCE_SOR0, DCF_IS_SOR0),
	CLK_8_1(0, "pc_sata_oob", mux_p_N_c_N_m_N_clkm, CLK_SOURCE_SATA_OOB, 0),
	CLK_8_1(0, "pc_sata", mux_p_N_c_N_m_N_clkm, CLK_SOURCE_SATA, DCF_IS_SATA),
	CLK_8_1(0, "pc_hda", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_HDA, 0),
	CLK_8_1(TEGRA124_CLK_XUSB_HOST_SRC,
		   "pc_xusb_core_host", mux_clkm_p_c2_c_c3_refre, CLK_SOURCE_XUSB_CORE_HOST, 0),
	CLK_8_1(TEGRA124_CLK_XUSB_FALCON_SRC,
		   "pc_xusb_falcon", mux_clkm_p_c2_c_c3_refre, CLK_SOURCE_XUSB_FALCON, 0),
	CLK_8_1(TEGRA124_CLK_XUSB_FS_SRC,
		   "pc_xusb_fs", mux_clkm_N_u48_N_p_N_u480, CLK_SOURCE_XUSB_FS, 0),
	CLK_8_1(TEGRA124_CLK_XUSB_DEV_SRC,
		   "pc_xusb_core_dev", mux_clkm_p_c2_c_c3_refre, CLK_SOURCE_XUSB_CORE_DEV, 0),
	CLK_8_1(TEGRA124_CLK_XUSB_SS_SRC,
		   "pc_xusb_ss", mux_clkm_refe_clks_u480_c_c2_c3_oscdiv, CLK_SOURCE_XUSB_SS, DCF_IS_XUSB_SS),
	CLK_8_1(0, "pc_cilab", mux_p_N_c_N_N_N_clkm, CLK_SOURCE_CILAB, 0),
	CLK_8_1(0, "pc_cilcd", mux_p_N_c_N_N_N_clkm, CLK_SOURCE_CILCD, 0),
	CLK_8_1(0, "pc_cile", mux_p_N_c_N_N_N_clkm, CLK_SOURCE_CILE, 0),
	CLK_8_1(0, "pc_dsia_lp", mux_p_N_c_N_N_N_clkm, CLK_SOURCE_DSIA_LP, 0),
	CLK_8_1(0, "pc_dsib_lp", mux_p_N_c_N_N_N_clkm, CLK_SOURCE_DSIB_LP, 0),
	CLK_8_1(0, "pc_entropy", mux_p_clkm_clks_E, CLK_SOURCE_ENTROPY, 0),
	CLK_8_1(0, "pc_dvfs_ref", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_DVFS_REF, DCF_HAVE_ENA),
	CLK_8_1(0, "pc_dvfs_soc", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_DVFS_SOC, DCF_HAVE_ENA),
	CLK_8_1(0, "pc_traceclkin", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_TRACECLKIN, 0),
	CLK_8_1(0, "pc_adx", mux_a_c2_c_c3_p_N_clkm, CLK_SOURCE_ADX, DCF_HAVE_ENA),
	CLK_8_1(0, "pc_amx", mux_a_c2_c_c3_p_N_clkm, CLK_SOURCE_AMX, DCF_HAVE_ENA),
	CLK_8_1(0, "pc_emc_latency", mux_m_c_p_clkm_mud_c2_c3, CLK_SOURCE_EMC_LATENCY, 0),
	CLK_8_1(0, "pc_soc_therm", mux_m_c_p_a_c2_c3, CLK_SOURCE_SOC_THERM, 0),
	CLK_8_1(0, "pc_vi_sensor2", mux_m_c2_c_c3_p_N_a, CLK_SOURCE_VI_SENSOR2, 0),
	CLK16_0(0, "pc_i2c6", mux_p_c2_c_c3_m_N_clkm, CLK_SOURCE_I2C6, 0),
	CLK_8_1(0, "pc_emc_dll", mux_m_c_p_clkm_mud_c2_c3, CLK_SOURCE_EMC_DLL, DCF_IS_EMC_DLL),
	CLK_8_1(0, "pc_hdmi_audio", mux_p_c_c2_clkm, CLK_SOURCE_HDMI_AUDIO, 0),
	CLK_8_1(0, "pc_clk72mhz", mux_p_c_c2_clkm, CLK_SOURCE_CLK72MHZ, 0),
	CLK_8_1(0, "pc_adx1", mux_a_c2_c_c3_p_N_clkm, CLK_SOURCE_ADX1, DCF_HAVE_ENA),
	CLK_8_1(0, "pc_amx1", mux_a_c2_c_c3_p_N_clkm, CLK_SOURCE_AMX1, DCF_HAVE_ENA),
	CLK_8_1(0, "pc_vic", mux_m_c_p_a_c2_c3_clkm, CLK_SOURCE_VIC, DCF_IS_VIC),
};

static int periph_init(struct clknode *clk, device_t dev);
static int periph_recalc(struct clknode *clk, uint64_t *freq);
static int periph_set_freq(struct clknode *clk, uint64_t fin,
    uint64_t *fout, int flags, int *stop);
static int periph_set_mux(struct clknode *clk, int idx);

struct periph_sc {
	device_t		clkdev;
	uint32_t		base_reg;
	uint32_t		div_shift;
	uint32_t		div_width;
	uint32_t		div_mask;
	uint32_t		div_f_width;
	uint32_t		div_f_mask;
	uint32_t		flags;

	uint32_t		divider;
	int 			mux;
};

static clknode_method_t periph_methods[] = {
	/* Device interface */
	CLKNODEMETHOD(clknode_init,		periph_init),
	CLKNODEMETHOD(clknode_recalc_freq,	periph_recalc),
	CLKNODEMETHOD(clknode_set_freq,		periph_set_freq),
	CLKNODEMETHOD(clknode_set_mux, 		periph_set_mux),
	CLKNODEMETHOD_END
};
DEFINE_CLASS_1(tegra124_periph, tegra124_periph_class, periph_methods,
   sizeof(struct periph_sc), clknode_class);

static int
periph_init(struct clknode *clk, device_t dev)
{
	struct periph_sc *sc;
	uint32_t reg;
	sc = clknode_get_softc(clk);

	DEVICE_LOCK(sc);
	if (sc->flags & DCF_HAVE_ENA)
		MD4(sc, sc->base_reg, PERLCK_ENA_MASK, PERLCK_ENA_MASK);

	RD4(sc, sc->base_reg, &reg);
	DEVICE_UNLOCK(sc);

	/* Stnadard mux. */
	if (sc->flags & DCF_HAVE_MUX)
		sc->mux = (reg >> PERLCK_MUX_SHIFT) & PERLCK_MUX_MASK;
	else
		sc->mux = 0;
	if (sc->flags & DCF_HAVE_DIV)
		sc->divider = (reg & sc->div_mask) + 2;
	else
		sc->divider = 1;
	if ((sc->flags & DCF_IS_MASK) == DCF_IS_UART) {
		if (!(reg & PERLCK_UDIV_DIS))
			sc->divider = 2;
	}

	/* AUDIO MUX */
	if ((sc->flags & DCF_IS_MASK) == DCF_IS_AUDIO) {
		if (!(reg & PERLCK_AMUX_DIS) && (sc->mux == 7)) {
			sc->mux = 8 +
			    ((reg >> PERLCK_AMUX_SHIFT) & PERLCK_MUX_MASK);
		}
	}
	clknode_init_parent_idx(clk, sc->mux);
	return(0);
}

static int
periph_set_mux(struct clknode *clk, int idx)
{
	struct periph_sc *sc;
	uint32_t reg;


	sc = clknode_get_softc(clk);
	if (!(sc->flags & DCF_HAVE_MUX))
		return (ENXIO);

	sc->mux = idx;
	DEVICE_LOCK(sc);
	RD4(sc, sc->base_reg, &reg);
	reg &= ~(PERLCK_MUX_MASK << PERLCK_MUX_SHIFT);
	if ((sc->flags & DCF_IS_MASK) == DCF_IS_AUDIO) {
		reg &= ~PERLCK_AMUX_DIS;
		reg &= ~(PERLCK_MUX_MASK << PERLCK_AMUX_SHIFT);

		if (idx <= 7) {
			reg |= idx << PERLCK_MUX_SHIFT;
		} else {
			reg |= 7 << PERLCK_MUX_SHIFT;
			reg |= (idx - 8) << PERLCK_AMUX_SHIFT;
		}
	} else {
		reg |= idx << PERLCK_MUX_SHIFT;
	}
	WR4(sc, sc->base_reg, reg);
	DEVICE_UNLOCK(sc);

	return(0);
}

static int
periph_recalc(struct clknode *clk, uint64_t *freq)
{
	struct periph_sc *sc;
	uint32_t reg;

	sc = clknode_get_softc(clk);

	if (sc->flags & DCF_HAVE_DIV) {
		DEVICE_LOCK(sc);
		RD4(sc, sc->base_reg, &reg);
		DEVICE_UNLOCK(sc);
		*freq = (*freq << sc->div_f_width) / sc->divider;
	}
	return (0);
}

static int
periph_set_freq(struct clknode *clk, uint64_t fin, uint64_t *fout,
   int flags, int *stop)
{
	struct periph_sc *sc;
	uint64_t tmp, divider;

	sc = clknode_get_softc(clk);
	if (!(sc->flags & DCF_HAVE_DIV)) {
		*stop = 0;
		return (0);
	}

	tmp = fin << sc->div_f_width;
	divider = tmp / *fout;
	if ((tmp % *fout) != 0)
		divider++;

	if (divider < (1 << sc->div_f_width))
		 divider = 1 << (sc->div_f_width - 1);

	if (flags & CLK_SET_DRYRUN) {
		if (((flags & (CLK_SET_ROUND_UP | CLK_SET_ROUND_DOWN)) == 0) &&
		    (*fout != (tmp / divider)))
			return (ERANGE);
	} else {
		DEVICE_LOCK(sc);
		MD4(sc, sc->base_reg, sc->div_mask,
		    (divider - (1 << sc->div_f_width)));
		DEVICE_UNLOCK(sc);
		sc->divider = divider;
	}
	*fout = tmp / divider;
	*stop = 1;
	return (0);
}

static int
periph_register(struct clkdom *clkdom, struct periph_def *clkdef)
{
	struct clknode *clk;
	struct periph_sc *sc;

	clk = clknode_create(clkdom, &tegra124_periph_class, &clkdef->clkdef);
	if (clk == NULL)
		return (1);

	sc = clknode_get_softc(clk);
	sc->clkdev = clknode_get_device(clk);
	sc->base_reg = clkdef->base_reg;
	sc->div_width = clkdef->div_width;
	sc->div_mask = (1 <<clkdef->div_width) - 1;
	sc->div_f_width = clkdef->div_f_width;
	sc->div_f_mask = (1 <<clkdef->div_f_width) - 1;
	sc->flags = clkdef->flags;

	clknode_register(clkdom, clk);
	return (0);
}

/* -------------------------------------------------------------------------- */
static int pgate_init(struct clknode *clk, device_t dev);
static int pgate_set_gate(struct clknode *clk, bool enable);

struct pgate_sc {
	device_t		clkdev;
	uint32_t		idx;
	uint32_t		flags;
	uint32_t		enabled;

};

static clknode_method_t pgate_methods[] = {
	/* Device interface */
	CLKNODEMETHOD(clknode_init,		pgate_init),
	CLKNODEMETHOD(clknode_set_gate,		pgate_set_gate),
	CLKNODEMETHOD_END
};
DEFINE_CLASS_1(tegra124_pgate, tegra124_pgate_class, pgate_methods,
   sizeof(struct pgate_sc), clknode_class);

static uint32_t
get_enable_reg(int idx)
{
	KASSERT(idx / 32 < nitems(clk_enable_reg),
	    ("Invalid clock index for enable: %d", idx));
	return (clk_enable_reg[idx / 32]);
}

static uint32_t
get_reset_reg(int idx)
{
	KASSERT(idx / 32 < nitems(clk_reset_reg),
	    ("Invalid clock index for reset: %d", idx));
	return (clk_reset_reg[idx / 32]);
}

static int
pgate_init(struct clknode *clk, device_t dev)
{
	struct pgate_sc *sc;
	uint32_t ena_reg, rst_reg, mask;

	sc = clknode_get_softc(clk);
	mask = 1 << (sc->idx % 32);

	DEVICE_LOCK(sc);
	RD4(sc, get_enable_reg(sc->idx), &ena_reg);
	RD4(sc, get_reset_reg(sc->idx), &rst_reg);
	DEVICE_UNLOCK(sc);

	sc->enabled = ena_reg & mask ? 1 : 0;
	clknode_init_parent_idx(clk, 0);

	return(0);
}

static int
pgate_set_gate(struct clknode *clk, bool enable)
{
	struct pgate_sc *sc;
	uint32_t reg, mask, base_reg;

	sc = clknode_get_softc(clk);
	mask = 1 << (sc->idx % 32);
	sc->enabled = enable;
	base_reg = get_enable_reg(sc->idx);

	DEVICE_LOCK(sc);
	MD4(sc, base_reg, mask, enable ? mask : 0);
	RD4(sc, base_reg, &reg);
	DEVICE_UNLOCK(sc);

	DELAY(2);
	return(0);
}

int
tegra124_hwreset_by_idx(struct tegra124_car_softc *sc, intptr_t idx, bool reset)
{
	uint32_t reg, mask, reset_reg;

	mask = 1 << (idx % 32);
	reset_reg = get_reset_reg(idx);

	CLKDEV_DEVICE_LOCK(sc->dev);
	CLKDEV_MODIFY_4(sc->dev, reset_reg, mask, reset ? mask : 0);
	CLKDEV_READ_4(sc->dev, reset_reg, &reg);
	CLKDEV_DEVICE_UNLOCK(sc->dev);

	return(0);
}

static int
pgate_register(struct clkdom *clkdom, struct pgate_def *clkdef)
{
	struct clknode *clk;
	struct pgate_sc *sc;

	clk = clknode_create(clkdom, &tegra124_pgate_class, &clkdef->clkdef);
	if (clk == NULL)
		return (1);

	sc = clknode_get_softc(clk);
	sc->clkdev = clknode_get_device(clk);
	sc->idx = clkdef->idx;
	sc->flags = clkdef->flags;

	clknode_register(clkdom, clk);
	return (0);
}

void
tegra124_periph_clock(struct tegra124_car_softc *sc)
{
	int i, rv;

	for (i = 0; i <  nitems(periph_def); i++) {
		rv = periph_register(sc->clkdom, &periph_def[i]);
		if (rv != 0)
			panic("tegra124_periph_register failed");
	}
	for (i = 0; i <  nitems(pgate_def); i++) {
		rv = pgate_register(sc->clkdom, &pgate_def[i]);
		if (rv != 0)
			panic("tegra124_pgate_register failed");
	}

}
