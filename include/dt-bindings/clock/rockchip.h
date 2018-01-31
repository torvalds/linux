/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _DT_BINDINGS_CLOCK_ROCKCHIP_H
#define _DT_BINDINGS_CLOCK_ROCKCHIP_H

#ifndef BIT
#define BIT(nr)			(1 << (nr))
#endif

#define CLK_DIVIDER_PLUS_ONE		(0)
#define CLK_DIVIDER_ONE_BASED		BIT(0)
#define CLK_DIVIDER_POWER_OF_TWO	BIT(1)
#define CLK_DIVIDER_ALLOW_ZERO		BIT(2)
#define CLK_DIVIDER_HIWORD_MASK		BIT(3)

/* Rockchip special defined */
//#define CLK_DIVIDER_FIXED		BIT(6)
#define CLK_DIVIDER_USER_DEFINE		BIT(7)

/*
 * flags used across common struct clk.  these flags should only affect the
 * top-level framework.  custom flags for dealing with hardware specifics
 * belong in struct clk_foo
 */
#define CLK_SET_RATE_GATE	BIT(0) /* must be gated across rate change */
#define CLK_SET_PARENT_GATE	BIT(1) /* must be gated across re-parent */
#define CLK_SET_RATE_PARENT	BIT(2) /* propagate rate change up one level */
#define CLK_IGNORE_UNUSED	BIT(3) /* do not gate even if unused */
#define CLK_IS_ROOT		BIT(4) /* root clk, has no parent */
#define CLK_IS_BASIC		BIT(5) /* Basic clk, can't do a to_clk_foo() */
#define CLK_GET_RATE_NOCACHE	BIT(6) /* do not use the cached clk rate */
#define CLK_SET_RATE_NO_REPARENT BIT(7) /* don't re-parent on rate change */
#define CLK_SET_RATE_PARENT_IN_ORDER BIT(8) /* consider the order of re-parent
						and set_div on rate change */



/* Rockchip pll flags */
#define CLK_PLL_3188		BIT(0)
#define CLK_PLL_3188_APLL	BIT(1)
#define CLK_PLL_3188PLUS	BIT(2)
#define CLK_PLL_3188PLUS_APLL	BIT(3)
#define CLK_PLL_3288_APLL	BIT(4)
#define CLK_PLL_3188PLUS_AUTO	BIT(5)
#define CLK_PLL_3036_APLL	BIT(6)
#define CLK_PLL_3036PLUS_AUTO	BIT(7)
#define CLK_PLL_312XPLUS	BIT(8)
#define CLK_PLL_3368_APLLB	BIT(9)
#define CLK_PLL_3368_APLLL	BIT(10)
#define CLK_PLL_3368_LOW_JITTER	BIT(11)


/* rate_ops index */
#define CLKOPS_RATE_MUX_DIV		1
#define CLKOPS_RATE_EVENDIV		2
#define CLKOPS_RATE_MUX_EVENDIV		3
#define CLKOPS_RATE_I2S_FRAC		4
#define CLKOPS_RATE_FRAC		5
#define CLKOPS_RATE_I2S			6
#define CLKOPS_RATE_CIFOUT		7
#define CLKOPS_RATE_UART		8
#define CLKOPS_RATE_HSADC		9
#define CLKOPS_RATE_MAC_REF		10
#define CLKOPS_RATE_CORE		11
#define CLKOPS_RATE_CORE_CHILD		12
#define CLKOPS_RATE_DDR			13
#define CLKOPS_RATE_RK3288_I2S		14
#define CLKOPS_RATE_RK3288_USB480M	15
#define CLKOPS_RATE_RK3288_DCLK_LCDC0	16
#define CLKOPS_RATE_RK3288_DCLK_LCDC1	17
#define CLKOPS_RATE_DDR_DIV2		18
#define CLKOPS_RATE_DDR_DIV4		19
#define CLKOPS_RATE_RK3368_MUX_DIV_NPLL 20
#define CLKOPS_RATE_RK3368_DCLK_LCDC	21
#define CLKOPS_RATE_RK3368_DDR		22

#define CLKOPS_TABLE_END		(~0)

/* pd id */
#define CLK_PD_BCPU		0
#define CLK_PD_BDSP		1
#define CLK_PD_BUS		2
#define CLK_PD_CPU_0 		3
#define CLK_PD_CPU_1 		4
#define CLK_PD_CPU_2 		5
#define CLK_PD_CPU_3 		6
#define CLK_PD_CS 		7
#define CLK_PD_GPU 		8
#define CLK_PD_HEVC 		9
#define CLK_PD_PERI 		10
#define CLK_PD_SCU 		11
#define CLK_PD_VIDEO 		12
#define CLK_PD_VIO		13
#define CLK_PD_GPU_0		14
#define CLK_PD_GPU_1		15

#define CLK_PD_VIRT		255

/* reset flag */
#define ROCKCHIP_RESET_HIWORD_MASK	BIT(0)

#endif /* _DT_BINDINGS_CLOCK_ROCKCHIP_H */
