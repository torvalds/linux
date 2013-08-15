/*
 * This header provides constants for Samsung audio subsystem
 * clock controller.
 *
 * The constants defined in this header are being used in dts
 * and exynos audss driver.
 */

#ifndef _DT_BINDINGS_CLK_EXYNOS_AUDSS_H
#define _DT_BINDINGS_CLK_EXYNOS_AUDSS_H

#define EXYNOS_MOUT_AUDSS	0
#define EXYNOS_MOUT_I2S	1
#define EXYNOS_DOUT_SRP	2
#define EXYNOS_DOUT_AUD_BUS	3
#define EXYNOS_DOUT_I2S	4
#define EXYNOS_SRP_CLK		5
#define EXYNOS_I2S_BUS		6
#define EXYNOS_SCLK_I2S	7
#define EXYNOS_PCM_BUS		8
#define EXYNOS_SCLK_PCM	9

#define EXYNOS_AUDSS_MAX_CLKS	10

#endif
