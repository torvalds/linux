/*
 * Meson8b clock tree IDs
 */

#ifndef __MESON8B_CLKC_H
#define __MESON8B_CLKC_H

#define CLKID_UNUSED		0
#define CLKID_XTAL		1
#define CLKID_PLL_FIXED		2
#define CLKID_PLL_VID		3
#define CLKID_PLL_SYS		4
#define CLKID_FCLK_DIV2		5
#define CLKID_FCLK_DIV3		6
#define CLKID_FCLK_DIV4		7
#define CLKID_FCLK_DIV5		8
#define CLKID_FCLK_DIV7		9
#define CLKID_CLK81		10
#define CLKID_MALI		11
#define CLKID_CPUCLK		12
#define CLKID_ZERO		13
#define CLKID_MPEG_SEL		14
#define CLKID_MPEG_DIV		15

#define CLK_NR_CLKS		(CLKID_MPEG_DIV + 1)

#endif /* __MESON8B_CLKC_H */
