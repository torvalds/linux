/*-
 * Copyright (c) 2016 Stanislav Galabov.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 *
 * $FreeBSD$
 */

#ifndef _MTK_SOC_H_
#define _MTK_SOC_H_

enum mtk_soc_id {
	MTK_SOC_UNKNOWN,
	MTK_SOC_RT2880,
	MTK_SOC_RT3050,
	MTK_SOC_RT3052,
	MTK_SOC_RT3350,
	MTK_SOC_RT3352,
	MTK_SOC_RT3662,
	MTK_SOC_RT3883,
	MTK_SOC_RT5350,
	MTK_SOC_MT7620A,
	MTK_SOC_MT7620N,
	MTK_SOC_MT7621,
	MTK_SOC_MT7628,
	MTK_SOC_MT7688,
	MTK_SOC_MAX
};

#define RT2880_CPU_CLKSEL_OFF	20
#define RT2880_CPU_CLKSEL_MSK	0x3
#define RT305X_CPU_CLKSEL_OFF	18
#define RT305X_CPU_CLKSEL_MSK	0x1
#define RT3352_CPU_CLKSEL_OFF	8
#define RT3352_CPU_CLKSEL_MSK	0x1
#define RT3883_CPU_CLKSEL_OFF	8
#define RT3883_CPU_CLKSEL_MSK	0x3
#define RT5350_CPU_CLKSEL_OFF1	8
#define RT5350_CPU_CLKSEL_OFF2	10
#define RT5350_CPU_CLKSEL_MSK	0x1
#define MT7628_CPU_CLKSEL_OFF	6
#define MT7628_CPU_CLKSEL_MSK	0x1

#define MT7620_CPU_CLK_AUX0	(1u<<24)
#define MT7620_CPLL_SW_CFG	(1u<<31)
#define MT7620_PLL_MULT_RATIO_OFF	16
#define MT7620_PLL_MULT_RATIO_MSK	0x7
#define MT7620_PLL_MULT_RATIO_BASE	24
#define MT7620_PLL_DIV_RATIO_OFF	10
#define MT7620_PLL_DIV_RATIO_MSK	0x3
#define MT7620_PLL_DIV_RATIO_BASE	2
#define MT7620_PLL_DIV_RATIO_MAX	8
#define MT7620_XTAL_40			40

#define MT7621_USES_MEMDIV	(1u<<30)
#define MT7621_MEMDIV_OFF	4
#define MT7621_MEMDIV_MSK	0x7f
#define MT7621_MEMDIV_BASE	1
#define MT7621_CLKSEL_OFF	6
#define MT7621_CLKSEL_MSK	0x7
#define MT7621_CLKSEL_25MHZ_VAL	6
#define MT7621_CLKSEL_20MHZ_VAL	3
#define MT7621_CLKSEL_20MHZ	20
#define MT7621_CLKSEL_25MHZ	25
#define MT7621_CLK_STS_DIV_OFF	8
#define MT7621_CLK_STS_MSK	0x1f
#define MT7621_CLK_STS_BASE	500

#define MTK_MT7621_CLKDIV_REG	0x5648
#define MTK_MT7621_CLKDIV_OFF	4
#define MTK_MT7621_CLKDIV_MSK	0x7f

#define MTK_MHZ(x)		((x) * 1000 * 1000)

#define MTK_CPU_CLK_UNKNOWN	0
#define MTK_CPU_CLK_233MHZ	233333333
#define MTK_CPU_CLK_250MHZ	250000000
#define MTK_CPU_CLK_266MHZ	266666666
#define MTK_CPU_CLK_280MHZ	280000000
#define MTK_CPU_CLK_300MHZ	300000000
#define MTK_CPU_CLK_320MHZ	320000000
#define MTK_CPU_CLK_360MHZ	360000000
#define MTK_CPU_CLK_384MHZ	384000000
#define MTK_CPU_CLK_400MHZ	400000000
#define MTK_CPU_CLK_480MHZ	480000000
#define MTK_CPU_CLK_500MHZ	500000000
#define MTK_CPU_CLK_575MHZ	575000000
#define MTK_CPU_CLK_580MHZ	580000000
#define MTK_CPU_CLK_600MHZ	600000000
#define MTK_CPU_CLK_880MHZ	880000000

#define MTK_UART_CLK_40MHZ	40000000
#define MTK_UART_CLK_50MHZ	50000000

#define MTK_UARTDIV_2		2
#define MTK_UARTDIV_3		3

#define MTK_DEFAULT_BASE	0x10000000
#define MTK_RT2880_BASE		0x00300000
#define MTK_MT7621_BASE		0x1e000000
#define MTK_DEFAULT_SIZE	0x6000

extern void     mtk_soc_try_early_detect(void);
extern void	mtk_soc_set_cpu_model(void);
extern uint32_t mtk_soc_get_uartclk(void);
extern uint32_t mtk_soc_get_cpuclk(void);
extern uint32_t mtk_soc_get_timerclk(void);
extern uint32_t mtk_soc_get_socid(void);

extern int	mtk_soc_reset_device(device_t);
extern int	mtk_soc_stop_clock(device_t);
extern int	mtk_soc_start_clock(device_t);
extern int	mtk_soc_assert_reset(device_t);
extern int	mtk_soc_deassert_reset(device_t);
extern void     mtk_soc_reset(void);

#endif /* _MTK_SOC_H_ */
