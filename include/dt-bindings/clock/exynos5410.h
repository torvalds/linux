#ifndef _DT_BINDINGS_CLOCK_EXYNOS_5410_H
#define _DT_BINDINGS_CLOCK_EXYNOS_5410_H

/* core clocks */
#define CLK_FOUT_APLL 1
#define CLK_FOUT_CPLL 2
#define CLK_FOUT_MPLL 3
#define CLK_FOUT_BPLL 4
#define CLK_FOUT_KPLL 5
#define CLK_FOUT_VPLL 6
#define CLK_FOUT_DPLL 7

/* gate for special clocks (sclk) */
#define CLK_SCLK_UART0 128
#define CLK_SCLK_UART1 129
#define CLK_SCLK_UART2 130
#define CLK_SCLK_UART3 131
#define CLK_SCLK_MMC0 132
#define CLK_SCLK_MMC1 133
#define CLK_SCLK_MMC2 134
#define CLK_SCLK_HDMIPHY 135
#define CLK_SCLK_PIXEL 136
#define CLK_SCLK_HDMI 137

/* gate clocks */
#define CLK_UART0 257
#define CLK_UART1 258
#define CLK_UART2 259
#define CLK_UART3 260
#define CLK_I2C0 261
#define CLK_I2C1 262
#define CLK_I2C2 263
#define CLK_I2C3 264
#define CLK_I2C4 265
#define CLK_I2C5 266
#define CLK_I2C6 267
#define CLK_I2C7 268
#define CLK_I2C_HDMI 269
#define CLK_MCT 315
#define CLK_MMC0 351
#define CLK_MMC1 352
#define CLK_MMC2 353
#define CLK_MIXER 354
#define CLK_HDMI 355
#define CLK_FIMD1 356
#define CLK_MIE1 357
#define CLK_DSIM0 358
#define CLK_DP 359

/* mux clocks */
#define CLK_MOUT_HDMI 500
#define CLK_NR_CLKS 512

#endif /* _DT_BINDINGS_CLOCK_EXYNOS_5410_H */
