/*
 * This header provides constants for binding nvidia,tegra20-car.
 *
 * The first 96 clocks are numbered to match the bits in the CAR's CLK_OUT_ENB
 * registers. These IDs often match those in the CAR's RST_DEVICES registers,
 * but not in all cases. Some bits in CLK_OUT_ENB affect multiple clocks. In
 * this case, those clocks are assigned IDs above 95 in order to highlight
 * this issue. Implementations that interpret these clock IDs as bit values
 * within the CLK_OUT_ENB or RST_DEVICES registers should be careful to
 * explicitly handle these special cases.
 *
 * The balance of the clocks controlled by the CAR are assigned IDs of 96 and
 * above.
 */

#ifndef _DT_BINDINGS_CLOCK_TEGRA20_CAR_H
#define _DT_BINDINGS_CLOCK_TEGRA20_CAR_H

#define TEGRA20_CLK_CPU 0
/* 1 */
/* 2 */
#define TEGRA20_CLK_AC97 3
#define TEGRA20_CLK_RTC 4
#define TEGRA20_CLK_TIMER 5
#define TEGRA20_CLK_UARTA 6
/* 7 (register bit affects uart2 and vfir) */
#define TEGRA20_CLK_GPIO 8
#define TEGRA20_CLK_SDMMC2 9
/* 10 (register bit affects spdif_in and spdif_out) */
#define TEGRA20_CLK_I2S1 11
#define TEGRA20_CLK_I2C1 12
#define TEGRA20_CLK_NDFLASH 13
#define TEGRA20_CLK_SDMMC1 14
#define TEGRA20_CLK_SDMMC4 15
#define TEGRA20_CLK_TWC 16
#define TEGRA20_CLK_PWM 17
#define TEGRA20_CLK_I2S2 18
#define TEGRA20_CLK_EPP 19
/* 20 (register bit affects vi and vi_sensor) */
#define TEGRA20_CLK_GR2D 21
#define TEGRA20_CLK_USBD 22
#define TEGRA20_CLK_ISP 23
#define TEGRA20_CLK_GR3D 24
#define TEGRA20_CLK_IDE 25
#define TEGRA20_CLK_DISP2 26
#define TEGRA20_CLK_DISP1 27
#define TEGRA20_CLK_HOST1X 28
#define TEGRA20_CLK_VCP 29
/* 30 */
#define TEGRA20_CLK_CACHE2 31

#define TEGRA20_CLK_MEM 32
#define TEGRA20_CLK_AHBDMA 33
#define TEGRA20_CLK_APBDMA 34
/* 35 */
#define TEGRA20_CLK_KBC 36
#define TEGRA20_CLK_STAT_MON 37
#define TEGRA20_CLK_PMC 38
#define TEGRA20_CLK_FUSE 39
#define TEGRA20_CLK_KFUSE 40
#define TEGRA20_CLK_SBC1 41
#define TEGRA20_CLK_NOR 42
#define TEGRA20_CLK_SPI 43
#define TEGRA20_CLK_SBC2 44
#define TEGRA20_CLK_XIO 45
#define TEGRA20_CLK_SBC3 46
#define TEGRA20_CLK_DVC 47
#define TEGRA20_CLK_DSI 48
/* 49 (register bit affects tvo and cve) */
#define TEGRA20_CLK_MIPI 50
#define TEGRA20_CLK_HDMI 51
#define TEGRA20_CLK_CSI 52
#define TEGRA20_CLK_TVDAC 53
#define TEGRA20_CLK_I2C2 54
#define TEGRA20_CLK_UARTC 55
/* 56 */
#define TEGRA20_CLK_EMC 57
#define TEGRA20_CLK_USB2 58
#define TEGRA20_CLK_USB3 59
#define TEGRA20_CLK_MPE 60
#define TEGRA20_CLK_VDE 61
#define TEGRA20_CLK_BSEA 62
#define TEGRA20_CLK_BSEV 63

#define TEGRA20_CLK_SPEEDO 64
#define TEGRA20_CLK_UARTD 65
#define TEGRA20_CLK_UARTE 66
#define TEGRA20_CLK_I2C3 67
#define TEGRA20_CLK_SBC4 68
#define TEGRA20_CLK_SDMMC3 69
#define TEGRA20_CLK_PEX 70
#define TEGRA20_CLK_OWR 71
#define TEGRA20_CLK_AFI 72
#define TEGRA20_CLK_CSITE 73
/* 74 */
#define TEGRA20_CLK_AVPUCQ 75
#define TEGRA20_CLK_LA 76
/* 77 */
/* 78 */
/* 79 */
/* 80 */
/* 81 */
/* 82 */
/* 83 */
#define TEGRA20_CLK_IRAMA 84
#define TEGRA20_CLK_IRAMB 85
#define TEGRA20_CLK_IRAMC 86
#define TEGRA20_CLK_IRAMD 87
#define TEGRA20_CLK_CRAM2 88
#define TEGRA20_CLK_AUDIO_2X 89 /* a/k/a audio_2x_sync_clk */
#define TEGRA20_CLK_CLK_D 90
/* 91 */
#define TEGRA20_CLK_CSUS 92
#define TEGRA20_CLK_CDEV2 93
#define TEGRA20_CLK_CDEV1 94
/* 95 */

#define TEGRA20_CLK_UARTB 96
#define TEGRA20_CLK_VFIR 97
#define TEGRA20_CLK_SPDIF_IN 98
#define TEGRA20_CLK_SPDIF_OUT 99
#define TEGRA20_CLK_VI 100
#define TEGRA20_CLK_VI_SENSOR 101
#define TEGRA20_CLK_TVO 102
#define TEGRA20_CLK_CVE 103
#define TEGRA20_CLK_OSC 104
#define TEGRA20_CLK_CLK_32K 105 /* a/k/a clk_s */
#define TEGRA20_CLK_CLK_M 106
#define TEGRA20_CLK_SCLK 107
#define TEGRA20_CLK_CCLK 108
#define TEGRA20_CLK_HCLK 109
#define TEGRA20_CLK_PCLK 110
#define TEGRA20_CLK_BLINK 111
#define TEGRA20_CLK_PLL_A 112
#define TEGRA20_CLK_PLL_A_OUT0 113
#define TEGRA20_CLK_PLL_C 114
#define TEGRA20_CLK_PLL_C_OUT1 115
#define TEGRA20_CLK_PLL_D 116
#define TEGRA20_CLK_PLL_D_OUT0 117
#define TEGRA20_CLK_PLL_E 118
#define TEGRA20_CLK_PLL_M 119
#define TEGRA20_CLK_PLL_M_OUT1 120
#define TEGRA20_CLK_PLL_P 121
#define TEGRA20_CLK_PLL_P_OUT1 122
#define TEGRA20_CLK_PLL_P_OUT2 123
#define TEGRA20_CLK_PLL_P_OUT3 124
#define TEGRA20_CLK_PLL_P_OUT4 125
#define TEGRA20_CLK_PLL_S 126
#define TEGRA20_CLK_PLL_U 127

#define TEGRA20_CLK_PLL_X 128
#define TEGRA20_CLK_COP 129 /* a/k/a avp */
#define TEGRA20_CLK_AUDIO 130 /* a/k/a audio_sync_clk */
#define TEGRA20_CLK_PLL_REF 131
#define TEGRA20_CLK_TWD 132
#define TEGRA20_CLK_CLK_MAX 133

#endif	/* _DT_BINDINGS_CLOCK_TEGRA20_CAR_H */
