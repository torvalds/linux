/*
 * wm9713.h  --  WM9713 Soc Audio driver
 */

#ifndef _WM9713_H
#define _WM9713_H

/* clock inputs */
#define WM9713_CLKA_PIN			0
#define WM9713_CLKB_PIN			1

/* clock divider ID's */
#define WM9713_PCMCLK_DIV		0
#define WM9713_CLKA_MULT		1
#define WM9713_CLKB_MULT		2
#define WM9713_HIFI_DIV			3
#define WM9713_PCMBCLK_DIV		4
#define WM9713_PCMCLK_PLL_DIV           5
#define WM9713_HIFI_PLL_DIV             6

/* Calculate the appropriate bit mask for the external PCM clock divider */
#define WM9713_PCMDIV(x)	((x - 1) << 8)

/* Calculate the appropriate bit mask for the external HiFi clock divider */
#define WM9713_HIFIDIV(x)	((x - 1) << 12)

/* MCLK clock mulitipliers */
#define WM9713_CLKA_X1		(0 << 1)
#define WM9713_CLKA_X2		(1 << 1)
#define WM9713_CLKB_X1		(0 << 2)
#define WM9713_CLKB_X2		(1 << 2)

/* MCLK clock MUX */
#define WM9713_CLK_MUX_A		(0 << 0)
#define WM9713_CLK_MUX_B		(1 << 0)

/* Voice DAI BCLK divider */
#define WM9713_PCMBCLK_DIV_1	(0 << 9)
#define WM9713_PCMBCLK_DIV_2	(1 << 9)
#define WM9713_PCMBCLK_DIV_4	(2 << 9)
#define WM9713_PCMBCLK_DIV_8	(3 << 9)
#define WM9713_PCMBCLK_DIV_16	(4 << 9)

#define WM9713_DAI_AC97_HIFI	0
#define WM9713_DAI_AC97_AUX		1
#define WM9713_DAI_PCM_VOICE	2

int wm9713_reset(struct snd_soc_codec *codec,  int try_warm);

#endif
