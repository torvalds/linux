/* SPDX-License-Identifier: GPL-2.0 */
/** @file */

#ifndef _MACH_T186_CLK_T186_H
#define _MACH_T186_CLK_T186_H

/**
 * @defgroup clock_ids Clock Identifiers
 * @{
 *   @defgroup extern_input external input clocks
 *   @{
 *     @def TEGRA186_CLK_OSC
 *     @def TEGRA186_CLK_CLK_32K
 *     @def TEGRA186_CLK_DTV_INPUT
 *     @def TEGRA186_CLK_SOR0_PAD_CLKOUT
 *     @def TEGRA186_CLK_SOR1_PAD_CLKOUT
 *     @def TEGRA186_CLK_I2S1_SYNC_INPUT
 *     @def TEGRA186_CLK_I2S2_SYNC_INPUT
 *     @def TEGRA186_CLK_I2S3_SYNC_INPUT
 *     @def TEGRA186_CLK_I2S4_SYNC_INPUT
 *     @def TEGRA186_CLK_I2S5_SYNC_INPUT
 *     @def TEGRA186_CLK_I2S6_SYNC_INPUT
 *     @def TEGRA186_CLK_SPDIFIN_SYNC_INPUT
 *   @}
 *
 *   @defgroup extern_output external output clocks
 *   @{
 *     @def TEGRA186_CLK_EXTPERIPH1
 *     @def TEGRA186_CLK_EXTPERIPH2
 *     @def TEGRA186_CLK_EXTPERIPH3
 *     @def TEGRA186_CLK_EXTPERIPH4
 *   @}
 *
 *   @defgroup display_clks display related clocks
 *   @{
 *     @def TEGRA186_CLK_CEC
 *     @def TEGRA186_CLK_DSIC
 *     @def TEGRA186_CLK_DSIC_LP
 *     @def TEGRA186_CLK_DSID
 *     @def TEGRA186_CLK_DSID_LP
 *     @def TEGRA186_CLK_DPAUX1
 *     @def TEGRA186_CLK_DPAUX
 *     @def TEGRA186_CLK_HDA2HDMICODEC
 *     @def TEGRA186_CLK_NVDISPLAY_DISP
 *     @def TEGRA186_CLK_NVDISPLAY_DSC
 *     @def TEGRA186_CLK_NVDISPLAY_P0
 *     @def TEGRA186_CLK_NVDISPLAY_P1
 *     @def TEGRA186_CLK_NVDISPLAY_P2
 *     @def TEGRA186_CLK_NVDISPLAYHUB
 *     @def TEGRA186_CLK_SOR_SAFE
 *     @def TEGRA186_CLK_SOR0
 *     @def TEGRA186_CLK_SOR0_OUT
 *     @def TEGRA186_CLK_SOR1
 *     @def TEGRA186_CLK_SOR1_OUT
 *     @def TEGRA186_CLK_DSI
 *     @def TEGRA186_CLK_MIPI_CAL
 *     @def TEGRA186_CLK_DSIA_LP
 *     @def TEGRA186_CLK_DSIB
 *     @def TEGRA186_CLK_DSIB_LP
 *   @}
 *
 *   @defgroup camera_clks camera related clocks
 *   @{
 *     @def TEGRA186_CLK_NVCSI
 *     @def TEGRA186_CLK_NVCSILP
 *     @def TEGRA186_CLK_VI
 *   @}
 *
 *   @defgroup audio_clks audio related clocks
 *   @{
 *     @def TEGRA186_CLK_ACLK
 *     @def TEGRA186_CLK_ADSP
 *     @def TEGRA186_CLK_ADSPNEON
 *     @def TEGRA186_CLK_AHUB
 *     @def TEGRA186_CLK_APE
 *     @def TEGRA186_CLK_APB2APE
 *     @def TEGRA186_CLK_AUD_MCLK
 *     @def TEGRA186_CLK_DMIC1
 *     @def TEGRA186_CLK_DMIC2
 *     @def TEGRA186_CLK_DMIC3
 *     @def TEGRA186_CLK_DMIC4
 *     @def TEGRA186_CLK_DSPK1
 *     @def TEGRA186_CLK_DSPK2
 *     @def TEGRA186_CLK_HDA
 *     @def TEGRA186_CLK_HDA2CODEC_2X
 *     @def TEGRA186_CLK_I2S1
 *     @def TEGRA186_CLK_I2S2
 *     @def TEGRA186_CLK_I2S3
 *     @def TEGRA186_CLK_I2S4
 *     @def TEGRA186_CLK_I2S5
 *     @def TEGRA186_CLK_I2S6
 *     @def TEGRA186_CLK_MAUD
 *     @def TEGRA186_CLK_PLL_A_OUT0
 *     @def TEGRA186_CLK_SPDIF_DOUBLER
 *     @def TEGRA186_CLK_SPDIF_IN
 *     @def TEGRA186_CLK_SPDIF_OUT
 *     @def TEGRA186_CLK_SYNC_DMIC1
 *     @def TEGRA186_CLK_SYNC_DMIC2
 *     @def TEGRA186_CLK_SYNC_DMIC3
 *     @def TEGRA186_CLK_SYNC_DMIC4
 *     @def TEGRA186_CLK_SYNC_DMIC5
 *     @def TEGRA186_CLK_SYNC_DSPK1
 *     @def TEGRA186_CLK_SYNC_DSPK2
 *     @def TEGRA186_CLK_SYNC_I2S1
 *     @def TEGRA186_CLK_SYNC_I2S2
 *     @def TEGRA186_CLK_SYNC_I2S3
 *     @def TEGRA186_CLK_SYNC_I2S4
 *     @def TEGRA186_CLK_SYNC_I2S5
 *     @def TEGRA186_CLK_SYNC_I2S6
 *     @def TEGRA186_CLK_SYNC_SPDIF
 *   @}
 *
 *   @defgroup uart_clks UART clocks
 *   @{
 *     @def TEGRA186_CLK_AON_UART_FST_MIPI_CAL
 *     @def TEGRA186_CLK_UARTA
 *     @def TEGRA186_CLK_UARTB
 *     @def TEGRA186_CLK_UARTC
 *     @def TEGRA186_CLK_UARTD
 *     @def TEGRA186_CLK_UARTE
 *     @def TEGRA186_CLK_UARTF
 *     @def TEGRA186_CLK_UARTG
 *     @def TEGRA186_CLK_UART_FST_MIPI_CAL
 *   @}
 *
 *   @defgroup i2c_clks I2C clocks
 *   @{
 *     @def TEGRA186_CLK_AON_I2C_SLOW
 *     @def TEGRA186_CLK_I2C1
 *     @def TEGRA186_CLK_I2C2
 *     @def TEGRA186_CLK_I2C3
 *     @def TEGRA186_CLK_I2C4
 *     @def TEGRA186_CLK_I2C5
 *     @def TEGRA186_CLK_I2C6
 *     @def TEGRA186_CLK_I2C8
 *     @def TEGRA186_CLK_I2C9
 *     @def TEGRA186_CLK_I2C1
 *     @def TEGRA186_CLK_I2C12
 *     @def TEGRA186_CLK_I2C13
 *     @def TEGRA186_CLK_I2C14
 *     @def TEGRA186_CLK_I2C_SLOW
 *     @def TEGRA186_CLK_VI_I2C
 *   @}
 *
 *   @defgroup spi_clks SPI clocks
 *   @{
 *     @def TEGRA186_CLK_SPI1
 *     @def TEGRA186_CLK_SPI2
 *     @def TEGRA186_CLK_SPI3
 *     @def TEGRA186_CLK_SPI4
 *   @}
 *
 *   @defgroup storage storage related clocks
 *   @{
 *     @def TEGRA186_CLK_SATA
 *     @def TEGRA186_CLK_SATA_OOB
 *     @def TEGRA186_CLK_SATA_IOBIST
 *     @def TEGRA186_CLK_SDMMC_LEGACY_TM
 *     @def TEGRA186_CLK_SDMMC1
 *     @def TEGRA186_CLK_SDMMC2
 *     @def TEGRA186_CLK_SDMMC3
 *     @def TEGRA186_CLK_SDMMC4
 *     @def TEGRA186_CLK_QSPI
 *     @def TEGRA186_CLK_QSPI_OUT
 *     @def TEGRA186_CLK_UFSDEV_REF
 *     @def TEGRA186_CLK_UFSHC
 *   @}
 *
 *   @defgroup pwm_clks PWM clocks
 *   @{
 *     @def TEGRA186_CLK_PWM1
 *     @def TEGRA186_CLK_PWM2
 *     @def TEGRA186_CLK_PWM3
 *     @def TEGRA186_CLK_PWM4
 *     @def TEGRA186_CLK_PWM5
 *     @def TEGRA186_CLK_PWM6
 *     @def TEGRA186_CLK_PWM7
 *     @def TEGRA186_CLK_PWM8
 *   @}
 *
 *   @defgroup plls PLLs and related clocks
 *   @{
 *     @def TEGRA186_CLK_PLLREFE_OUT_GATED
 *     @def TEGRA186_CLK_PLLREFE_OUT1
 *     @def TEGRA186_CLK_PLLD_OUT1
 *     @def TEGRA186_CLK_PLLP_OUT0
 *     @def TEGRA186_CLK_PLLP_OUT5
 *     @def TEGRA186_CLK_PLLA
 *     @def TEGRA186_CLK_PLLE_PWRSEQ
 *     @def TEGRA186_CLK_PLLA_OUT1
 *     @def TEGRA186_CLK_PLLREFE_REF
 *     @def TEGRA186_CLK_UPHY_PLL0_PWRSEQ
 *     @def TEGRA186_CLK_UPHY_PLL1_PWRSEQ
 *     @def TEGRA186_CLK_PLLREFE_PLLE_PASSTHROUGH
 *     @def TEGRA186_CLK_PLLREFE_PEX
 *     @def TEGRA186_CLK_PLLREFE_IDDQ
 *     @def TEGRA186_CLK_PLLC_OUT_AON
 *     @def TEGRA186_CLK_PLLC_OUT_ISP
 *     @def TEGRA186_CLK_PLLC_OUT_VE
 *     @def TEGRA186_CLK_PLLC4_OUT
 *     @def TEGRA186_CLK_PLLREFE_OUT
 *     @def TEGRA186_CLK_PLLREFE_PLL_REF
 *     @def TEGRA186_CLK_PLLE
 *     @def TEGRA186_CLK_PLLC
 *     @def TEGRA186_CLK_PLLP
 *     @def TEGRA186_CLK_PLLD
 *     @def TEGRA186_CLK_PLLD2
 *     @def TEGRA186_CLK_PLLREFE_VCO
 *     @def TEGRA186_CLK_PLLC2
 *     @def TEGRA186_CLK_PLLC3
 *     @def TEGRA186_CLK_PLLDP
 *     @def TEGRA186_CLK_PLLC4_VCO
 *     @def TEGRA186_CLK_PLLA1
 *     @def TEGRA186_CLK_PLLNVCSI
 *     @def TEGRA186_CLK_PLLDISPHUB
 *     @def TEGRA186_CLK_PLLD3
 *     @def TEGRA186_CLK_PLLBPMPCAM
 *     @def TEGRA186_CLK_PLLAON
 *     @def TEGRA186_CLK_PLLU
 *     @def TEGRA186_CLK_PLLC4_VCO_DIV2
 *     @def TEGRA186_CLK_PLL_REF
 *     @def TEGRA186_CLK_PLLREFE_OUT1_DIV5
 *     @def TEGRA186_CLK_UTMIP_PLL_PWRSEQ
 *     @def TEGRA186_CLK_PLL_U_48M
 *     @def TEGRA186_CLK_PLL_U_480M
 *     @def TEGRA186_CLK_PLLC4_OUT0
 *     @def TEGRA186_CLK_PLLC4_OUT1
 *     @def TEGRA186_CLK_PLLC4_OUT2
 *     @def TEGRA186_CLK_PLLC4_OUT_MUX
 *     @def TEGRA186_CLK_DFLLDISP_DIV
 *     @def TEGRA186_CLK_PLLDISPHUB_DIV
 *     @def TEGRA186_CLK_PLLP_DIV8
 *   @}
 *
 *   @defgroup nafll_clks NAFLL clock sources
 *   @{
 *     @def TEGRA186_CLK_NAFLL_AXI_CBB
 *     @def TEGRA186_CLK_NAFLL_BCPU
 *     @def TEGRA186_CLK_NAFLL_BPMP
 *     @def TEGRA186_CLK_NAFLL_DISP
 *     @def TEGRA186_CLK_NAFLL_GPU
 *     @def TEGRA186_CLK_NAFLL_ISP
 *     @def TEGRA186_CLK_NAFLL_MCPU
 *     @def TEGRA186_CLK_NAFLL_NVDEC
 *     @def TEGRA186_CLK_NAFLL_NVENC
 *     @def TEGRA186_CLK_NAFLL_NVJPG
 *     @def TEGRA186_CLK_NAFLL_SCE
 *     @def TEGRA186_CLK_NAFLL_SE
 *     @def TEGRA186_CLK_NAFLL_TSEC
 *     @def TEGRA186_CLK_NAFLL_TSECB
 *     @def TEGRA186_CLK_NAFLL_VI
 *     @def TEGRA186_CLK_NAFLL_VIC
 *   @}
 *
 *   @defgroup mphy MPHY related clocks
 *   @{
 *     @def TEGRA186_CLK_MPHY_L0_RX_SYMB
 *     @def TEGRA186_CLK_MPHY_L0_RX_LS_BIT
 *     @def TEGRA186_CLK_MPHY_L0_TX_SYMB
 *     @def TEGRA186_CLK_MPHY_L0_TX_LS_3XBIT
 *     @def TEGRA186_CLK_MPHY_L0_RX_ANA
 *     @def TEGRA186_CLK_MPHY_L1_RX_ANA
 *     @def TEGRA186_CLK_MPHY_IOBIST
 *     @def TEGRA186_CLK_MPHY_TX_1MHZ_REF
 *     @def TEGRA186_CLK_MPHY_CORE_PLL_FIXED
 *   @}
 *
 *   @defgroup eavb EAVB related clocks
 *   @{
 *     @def TEGRA186_CLK_EQOS_AXI
 *     @def TEGRA186_CLK_EQOS_PTP_REF
 *     @def TEGRA186_CLK_EQOS_RX
 *     @def TEGRA186_CLK_EQOS_RX_INPUT
 *     @def TEGRA186_CLK_EQOS_TX
 *   @}
 *
 *   @defgroup usb USB related clocks
 *   @{
 *     @def TEGRA186_CLK_PEX_USB_PAD0_MGMT
 *     @def TEGRA186_CLK_PEX_USB_PAD1_MGMT
 *     @def TEGRA186_CLK_HSIC_TRK
 *     @def TEGRA186_CLK_USB2_TRK
 *     @def TEGRA186_CLK_USB2_HSIC_TRK
 *     @def TEGRA186_CLK_XUSB_CORE_SS
 *     @def TEGRA186_CLK_XUSB_CORE_DEV
 *     @def TEGRA186_CLK_XUSB_FALCON
 *     @def TEGRA186_CLK_XUSB_FS
 *     @def TEGRA186_CLK_XUSB
 *     @def TEGRA186_CLK_XUSB_DEV
 *     @def TEGRA186_CLK_XUSB_HOST
 *     @def TEGRA186_CLK_XUSB_SS
 *   @}
 *
 *   @defgroup bigblock compute block related clocks
 *   @{
 *     @def TEGRA186_CLK_GPCCLK
 *     @def TEGRA186_CLK_GPC2CLK
 *     @def TEGRA186_CLK_GPU
 *     @def TEGRA186_CLK_HOST1X
 *     @def TEGRA186_CLK_ISP
 *     @def TEGRA186_CLK_NVDEC
 *     @def TEGRA186_CLK_NVENC
 *     @def TEGRA186_CLK_NVJPG
 *     @def TEGRA186_CLK_SE
 *     @def TEGRA186_CLK_TSEC
 *     @def TEGRA186_CLK_TSECB
 *     @def TEGRA186_CLK_VIC
 *   @}
 *
 *   @defgroup can CAN bus related clocks
 *   @{
 *     @def TEGRA186_CLK_CAN1
 *     @def TEGRA186_CLK_CAN1_HOST
 *     @def TEGRA186_CLK_CAN2
 *     @def TEGRA186_CLK_CAN2_HOST
 *   @}
 *
 *   @defgroup system basic system clocks
 *   @{
 *     @def TEGRA186_CLK_ACTMON
 *     @def TEGRA186_CLK_AON_APB
 *     @def TEGRA186_CLK_AON_CPU_NIC
 *     @def TEGRA186_CLK_AON_NIC
 *     @def TEGRA186_CLK_AXI_CBB
 *     @def TEGRA186_CLK_BPMP_APB
 *     @def TEGRA186_CLK_BPMP_CPU_NIC
 *     @def TEGRA186_CLK_BPMP_NIC_RATE
 *     @def TEGRA186_CLK_CLK_M
 *     @def TEGRA186_CLK_EMC
 *     @def TEGRA186_CLK_MSS_ENCRYPT
 *     @def TEGRA186_CLK_SCE_APB
 *     @def TEGRA186_CLK_SCE_CPU_NIC
 *     @def TEGRA186_CLK_SCE_NIC
 *     @def TEGRA186_CLK_TSC
 *   @}
 *
 *   @defgroup pcie_clks PCIe related clocks
 *   @{
 *     @def TEGRA186_CLK_AFI
 *     @def TEGRA186_CLK_PCIE
 *     @def TEGRA186_CLK_PCIE2_IOBIST
 *     @def TEGRA186_CLK_PCIERX0
 *     @def TEGRA186_CLK_PCIERX1
 *     @def TEGRA186_CLK_PCIERX2
 *     @def TEGRA186_CLK_PCIERX3
 *     @def TEGRA186_CLK_PCIERX4
 *   @}
 */

/** @brief output of gate CLK_ENB_FUSE */
#define TEGRA186_CLK_FUSE 0
/**
 * @brief It's not what you think
 * @details output of gate CLK_ENB_GPU. This output connects to the GPU
 * pwrclk. @warning: This is almost certainly not the clock you think
 * it is. If you're looking for the clock of the graphics engine, see
 * TEGRA186_GPCCLK
 */
#define TEGRA186_CLK_GPU 1
/** @brief output of gate CLK_ENB_PCIE */
#define TEGRA186_CLK_PCIE 3
/** @brief output of the divider IPFS_CLK_DIVISOR */
#define TEGRA186_CLK_AFI 4
/** @brief output of gate CLK_ENB_PCIE2_IOBIST */
#define TEGRA186_CLK_PCIE2_IOBIST 5
/** @brief output of gate CLK_ENB_PCIERX0*/
#define TEGRA186_CLK_PCIERX0 6
/** @brief output of gate CLK_ENB_PCIERX1*/
#define TEGRA186_CLK_PCIERX1 7
/** @brief output of gate CLK_ENB_PCIERX2*/
#define TEGRA186_CLK_PCIERX2 8
/** @brief output of gate CLK_ENB_PCIERX3*/
#define TEGRA186_CLK_PCIERX3 9
/** @brief output of gate CLK_ENB_PCIERX4*/
#define TEGRA186_CLK_PCIERX4 10
/** @brief output branch of PLL_C for ISP, controlled by gate CLK_ENB_PLLC_OUT_ISP */
#define TEGRA186_CLK_PLLC_OUT_ISP 11
/** @brief output branch of PLL_C for VI, controlled by gate CLK_ENB_PLLC_OUT_VE */
#define TEGRA186_CLK_PLLC_OUT_VE 12
/** @brief output branch of PLL_C for AON domain, controlled by gate CLK_ENB_PLLC_OUT_AON */
#define TEGRA186_CLK_PLLC_OUT_AON 13
/** @brief output of gate CLK_ENB_SOR_SAFE */
#define TEGRA186_CLK_SOR_SAFE 39
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2S2 */
#define TEGRA186_CLK_I2S2 42
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2S3 */
#define TEGRA186_CLK_I2S3 43
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SPDF_IN */
#define TEGRA186_CLK_SPDIF_IN 44
/** @brief output of gate CLK_ENB_SPDIF_DOUBLER */
#define TEGRA186_CLK_SPDIF_DOUBLER 45
/**  @clkdesc{spi_clks, out, mux, CLK_RST_CONTROLLER_CLK_SOURCE_SPI3} */
#define TEGRA186_CLK_SPI3 46
/** @clkdesc{i2c_clks, out, mux, CLK_RST_CONTROLLER_CLK_SOURCE_I2C1} */
#define TEGRA186_CLK_I2C1 47
/** @clkdesc{i2c_clks, out, mux, CLK_RST_CONTROLLER_CLK_SOURCE_I2C5} */
#define TEGRA186_CLK_I2C5 48
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SPI1 */
#define TEGRA186_CLK_SPI1 49
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_ISP */
#define TEGRA186_CLK_ISP 50
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_VI */
#define TEGRA186_CLK_VI 51
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SDMMC1 */
#define TEGRA186_CLK_SDMMC1 52
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SDMMC2 */
#define TEGRA186_CLK_SDMMC2 53
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SDMMC4 */
#define TEGRA186_CLK_SDMMC4 54
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UARTA */
#define TEGRA186_CLK_UARTA 55
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UARTB */
#define TEGRA186_CLK_UARTB 56
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_HOST1X */
#define TEGRA186_CLK_HOST1X 57
/**
 * @brief controls the EMC clock frequency.
 * @details Doing a clk_set_rate on this clock will select the
 * appropriate clock source, program the source rate and execute a
 * specific sequence to switch to the new clock source for both memory
 * controllers. This can be used to control the balance between memory
 * throughput and memory controller power.
 */
#define TEGRA186_CLK_EMC 58
/* @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_EXTPERIPH4 */
#define TEGRA186_CLK_EXTPERIPH4 73
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SPI4 */
#define TEGRA186_CLK_SPI4 74
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C3 */
#define TEGRA186_CLK_I2C3 75
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SDMMC3 */
#define TEGRA186_CLK_SDMMC3 76
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UARTD */
#define TEGRA186_CLK_UARTD 77
/** output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2S1 */
#define TEGRA186_CLK_I2S1 79
/** output of gate CLK_ENB_DTV */
#define TEGRA186_CLK_DTV 80
/** output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_TSEC */
#define TEGRA186_CLK_TSEC 81
/** @brief output of gate CLK_ENB_DP2 */
#define TEGRA186_CLK_DP2 82
/** output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2S4 */
#define TEGRA186_CLK_I2S4 84
/** output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2S5 */
#define TEGRA186_CLK_I2S5 85
/** output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C4 */
#define TEGRA186_CLK_I2C4 86
/** output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_AHUB */
#define TEGRA186_CLK_AHUB 87
/** output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_HDA2CODEC_2X */
#define TEGRA186_CLK_HDA2CODEC_2X 88
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_EXTPERIPH1 */
#define TEGRA186_CLK_EXTPERIPH1 89
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_EXTPERIPH2 */
#define TEGRA186_CLK_EXTPERIPH2 90
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_EXTPERIPH3 */
#define TEGRA186_CLK_EXTPERIPH3 91
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C_SLOW */
#define TEGRA186_CLK_I2C_SLOW 92
/** @brief output of the SOR1_CLK_SRC mux in CLK_RST_CONTROLLER_CLK_SOURCE_SOR1 */
#define TEGRA186_CLK_SOR1 93
/** @brief output of gate CLK_ENB_CEC */
#define TEGRA186_CLK_CEC 94
/** @brief output of gate CLK_ENB_DPAUX1 */
#define TEGRA186_CLK_DPAUX1 95
/** @brief output of gate CLK_ENB_DPAUX */
#define TEGRA186_CLK_DPAUX 96
/** @brief output of the SOR0_CLK_SRC mux in CLK_RST_CONTROLLER_CLK_SOURCE_SOR0 */
#define TEGRA186_CLK_SOR0 97
/** @brief output of gate CLK_ENB_HDA2HDMICODEC */
#define TEGRA186_CLK_HDA2HDMICODEC 98
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SATA */
#define TEGRA186_CLK_SATA 99
/** @brief output of gate CLK_ENB_SATA_OOB */
#define TEGRA186_CLK_SATA_OOB 100
/** @brief output of gate CLK_ENB_SATA_IOBIST */
#define TEGRA186_CLK_SATA_IOBIST 101
/** output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_HDA */
#define TEGRA186_CLK_HDA 102
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SE */
#define TEGRA186_CLK_SE 103
/** @brief output of gate CLK_ENB_APB2APE */
#define TEGRA186_CLK_APB2APE 104
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_APE */
#define TEGRA186_CLK_APE 105
/** @brief output of gate CLK_ENB_IQC1 */
#define TEGRA186_CLK_IQC1 106
/** @brief output of gate CLK_ENB_IQC2 */
#define TEGRA186_CLK_IQC2 107
/** divide by 2 version of TEGRA186_CLK_PLLREFE_VCO */
#define TEGRA186_CLK_PLLREFE_OUT 108
/** @brief output of gate CLK_ENB_PLLREFE_PLL_REF */
#define TEGRA186_CLK_PLLREFE_PLL_REF 109
/** @brief output of gate CLK_ENB_PLLC4_OUT */
#define TEGRA186_CLK_PLLC4_OUT 110
/** @brief output of mux xusb_core_clk_switch on page 67 of T186_Clocks_IAS.doc */
#define TEGRA186_CLK_XUSB 111
/** controls xusb_dev_ce signal on page 66 and 67 of T186_Clocks_IAS.doc */
#define TEGRA186_CLK_XUSB_DEV 112
/** controls xusb_host_ce signal on page 67 of T186_Clocks_IAS.doc */
#define TEGRA186_CLK_XUSB_HOST 113
/** controls xusb_ss_ce signal on page 67 of T186_Clocks_IAS.doc */
#define TEGRA186_CLK_XUSB_SS 114
/** @brief output of gate CLK_ENB_DSI */
#define TEGRA186_CLK_DSI 115
/** @brief output of gate CLK_ENB_MIPI_CAL */
#define TEGRA186_CLK_MIPI_CAL 116
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DSIA_LP */
#define TEGRA186_CLK_DSIA_LP 117
/** @brief output of gate CLK_ENB_DSIB */
#define TEGRA186_CLK_DSIB 118
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DSIB_LP */
#define TEGRA186_CLK_DSIB_LP 119
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DMIC1 */
#define TEGRA186_CLK_DMIC1 122
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DMIC2 */
#define TEGRA186_CLK_DMIC2 123
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_AUD_MCLK */
#define TEGRA186_CLK_AUD_MCLK 124
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C6 */
#define TEGRA186_CLK_I2C6 125
/**output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UART_FST_MIPI_CAL */
#define TEGRA186_CLK_UART_FST_MIPI_CAL 126
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_VIC */
#define TEGRA186_CLK_VIC 127
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SDMMC_LEGACY_TM */
#define TEGRA186_CLK_SDMMC_LEGACY_TM 128
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_NVDEC */
#define TEGRA186_CLK_NVDEC 129
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_NVJPG */
#define TEGRA186_CLK_NVJPG 130
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_NVENC */
#define TEGRA186_CLK_NVENC 131
/** @brief output of the QSPI_CLK_SRC mux in CLK_RST_CONTROLLER_CLK_SOURCE_QSPI */
#define TEGRA186_CLK_QSPI 132
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_VI_I2C */
#define TEGRA186_CLK_VI_I2C 133
/** @brief output of gate CLK_ENB_HSIC_TRK */
#define TEGRA186_CLK_HSIC_TRK 134
/** @brief output of gate CLK_ENB_USB2_TRK */
#define TEGRA186_CLK_USB2_TRK 135
/** output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_MAUD */
#define TEGRA186_CLK_MAUD 136
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_TSECB */
#define TEGRA186_CLK_TSECB 137
/** @brief output of gate CLK_ENB_ADSP */
#define TEGRA186_CLK_ADSP 138
/** @brief output of gate CLK_ENB_ADSPNEON */
#define TEGRA186_CLK_ADSPNEON 139
/** @brief output of the divider CLK_RST_CONTROLLER_CLK_SOURCE_MPHY_L0_RX_LS_SYMB */
#define TEGRA186_CLK_MPHY_L0_RX_SYMB 140
/** @brief output of gate CLK_ENB_MPHY_L0_RX_LS_BIT */
#define TEGRA186_CLK_MPHY_L0_RX_LS_BIT 141
/** @brief output of the divider CLK_RST_CONTROLLER_CLK_SOURCE_MPHY_L0_TX_LS_SYMB */
#define TEGRA186_CLK_MPHY_L0_TX_SYMB 142
/** @brief output of gate CLK_ENB_MPHY_L0_TX_LS_3XBIT */
#define TEGRA186_CLK_MPHY_L0_TX_LS_3XBIT 143
/** @brief output of gate CLK_ENB_MPHY_L0_RX_ANA */
#define TEGRA186_CLK_MPHY_L0_RX_ANA 144
/** @brief output of gate CLK_ENB_MPHY_L1_RX_ANA */
#define TEGRA186_CLK_MPHY_L1_RX_ANA 145
/** @brief output of the divider CLK_RST_CONTROLLER_CLK_SOURCE_MPHY_IOBIST */
#define TEGRA186_CLK_MPHY_IOBIST 146
/** @brief output of the divider CLK_RST_CONTROLLER_CLK_SOURCE_MPHY_TX_1MHZ_REF */
#define TEGRA186_CLK_MPHY_TX_1MHZ_REF 147
/** @brief output of the divider CLK_RST_CONTROLLER_CLK_SOURCE_MPHY_CORE_PLL_FIXED */
#define TEGRA186_CLK_MPHY_CORE_PLL_FIXED 148
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_AXI_CBB */
#define TEGRA186_CLK_AXI_CBB 149
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DMIC3 */
#define TEGRA186_CLK_DMIC3 150
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DMIC4 */
#define TEGRA186_CLK_DMIC4 151
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DSPK1 */
#define TEGRA186_CLK_DSPK1 152
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DSPK2 */
#define TEGRA186_CLK_DSPK2 153
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C6 */
#define TEGRA186_CLK_I2S6 154
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_NVDISPLAY_P0 */
#define TEGRA186_CLK_NVDISPLAY_P0 155
/** @brief output of the NVDISPLAY_DISP_CLK_SRC mux in CLK_RST_CONTROLLER_CLK_SOURCE_NVDISPLAY_DISP */
#define TEGRA186_CLK_NVDISPLAY_DISP 156
/** @brief output of gate CLK_ENB_NVDISPLAY_DSC */
#define TEGRA186_CLK_NVDISPLAY_DSC 157
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_NVDISPLAYHUB */
#define TEGRA186_CLK_NVDISPLAYHUB 158
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_NVDISPLAY_P1 */
#define TEGRA186_CLK_NVDISPLAY_P1 159
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_NVDISPLAY_P2 */
#define TEGRA186_CLK_NVDISPLAY_P2 160
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_TACH */
#define TEGRA186_CLK_TACH 166
/** @brief output of gate CLK_ENB_EQOS */
#define TEGRA186_CLK_EQOS_AXI 167
/** @brief output of gate CLK_ENB_EQOS_RX */
#define TEGRA186_CLK_EQOS_RX 168
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UFSHC_CG_SYS */
#define TEGRA186_CLK_UFSHC 178
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UFSDEV_REF */
#define TEGRA186_CLK_UFSDEV_REF 179
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_NVCSI */
#define TEGRA186_CLK_NVCSI 180
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_NVCSILP */
#define TEGRA186_CLK_NVCSILP 181
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C7 */
#define TEGRA186_CLK_I2C7 182
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C9 */
#define TEGRA186_CLK_I2C9 183
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C12 */
#define TEGRA186_CLK_I2C12 184
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C13 */
#define TEGRA186_CLK_I2C13 185
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C14 */
#define TEGRA186_CLK_I2C14 186
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_PWM1 */
#define TEGRA186_CLK_PWM1 187
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_PWM2 */
#define TEGRA186_CLK_PWM2 188
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_PWM3 */
#define TEGRA186_CLK_PWM3 189
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_PWM5 */
#define TEGRA186_CLK_PWM5 190
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_PWM6 */
#define TEGRA186_CLK_PWM6 191
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_PWM7 */
#define TEGRA186_CLK_PWM7 192
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_PWM8 */
#define TEGRA186_CLK_PWM8 193
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UARTE */
#define TEGRA186_CLK_UARTE 194
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UARTF */
#define TEGRA186_CLK_UARTF 195
/** @deprecated */
#define TEGRA186_CLK_DBGAPB 196
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_BPMP_CPU_NIC */
#define TEGRA186_CLK_BPMP_CPU_NIC 197
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_BPMP_APB */
#define TEGRA186_CLK_BPMP_APB 199
/** @brief output of mux controlled by TEGRA186_CLK_SOC_ACTMON */
#define TEGRA186_CLK_ACTMON 201
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_AON_CPU_NIC */
#define TEGRA186_CLK_AON_CPU_NIC 208
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_CAN1 */
#define TEGRA186_CLK_CAN1 210
/** @brief output of gate CLK_ENB_CAN1_HOST */
#define TEGRA186_CLK_CAN1_HOST 211
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_CAN2 */
#define TEGRA186_CLK_CAN2 212
/** @brief output of gate CLK_ENB_CAN2_HOST */
#define TEGRA186_CLK_CAN2_HOST 213
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_AON_APB */
#define TEGRA186_CLK_AON_APB 214
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UARTC */
#define TEGRA186_CLK_UARTC 215
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_UARTG */
#define TEGRA186_CLK_UARTG 216
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_AON_UART_FST_MIPI_CAL */
#define TEGRA186_CLK_AON_UART_FST_MIPI_CAL 217
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C2 */
#define TEGRA186_CLK_I2C2 218
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C8 */
#define TEGRA186_CLK_I2C8 219
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_I2C10 */
#define TEGRA186_CLK_I2C10 220
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_AON_I2C_SLOW */
#define TEGRA186_CLK_AON_I2C_SLOW 221
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SPI2 */
#define TEGRA186_CLK_SPI2 222
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DMIC5 */
#define TEGRA186_CLK_DMIC5 223
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_AON_TOUCH */
#define TEGRA186_CLK_AON_TOUCH 224
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_PWM4 */
#define TEGRA186_CLK_PWM4 225
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_TSC. This clock object is read only and is used for all timers in the system. */
#define TEGRA186_CLK_TSC 226
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_MSS_ENCRYPT */
#define TEGRA186_CLK_MSS_ENCRYPT 227
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SCE_CPU_NIC */
#define TEGRA186_CLK_SCE_CPU_NIC 228
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SCE_APB */
#define TEGRA186_CLK_SCE_APB 230
/** @brief output of gate CLK_ENB_DSIC */
#define TEGRA186_CLK_DSIC 231
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DSIC_LP */
#define TEGRA186_CLK_DSIC_LP 232
/** @brief output of gate CLK_ENB_DSID */
#define TEGRA186_CLK_DSID 233
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_DSID_LP */
#define TEGRA186_CLK_DSID_LP 234
/** @brief output of the divider CLK_RST_CONTROLLER_CLK_SOURCE_PEX_SATA_USB_RX_BYP */
#define TEGRA186_CLK_PEX_SATA_USB_RX_BYP 236
/** @brief output of mux controlled by CLK_RST_CONTROLLER_CLK_SOURCE_SPDIF_OUT */
#define TEGRA186_CLK_SPDIF_OUT 238
/** @brief output of the divider CLK_RST_CONTROLLER_CLK_SOURCE_EQOS_PTP_REF_CLK_0 */
#define TEGRA186_CLK_EQOS_PTP_REF 239
/** @brief output of the divider CLK_RST_CONTROLLER_CLK_SOURCE_EQOS_TX_CLK */
#define TEGRA186_CLK_EQOS_TX 240
/** @brief output of the divider CLK_RST_CONTROLLER_CLK_SOURCE_USB2_HSIC_TRK */
#define TEGRA186_CLK_USB2_HSIC_TRK 241
/** @brief output of mux xusb_ss_clk_switch on page 66 of T186_Clocks_IAS.doc */
#define TEGRA186_CLK_XUSB_CORE_SS 242
/** @brief output of mux xusb_core_dev_clk_switch on page 67 of T186_Clocks_IAS.doc */
#define TEGRA186_CLK_XUSB_CORE_DEV 243
/** @brief output of mux xusb_core_falcon_clk_switch on page 67 of T186_Clocks_IAS.doc */
#define TEGRA186_CLK_XUSB_FALCON 244
/** @brief output of mux xusb_fs_clk_switch on page 66 of T186_Clocks_IAS.doc */
#define TEGRA186_CLK_XUSB_FS 245
/** @brief output of the divider CLK_RST_CONTROLLER_PLLA_OUT */
#define TEGRA186_CLK_PLL_A_OUT0 246
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_I2S1 */
#define TEGRA186_CLK_SYNC_I2S1 247
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_I2S2 */
#define TEGRA186_CLK_SYNC_I2S2 248
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_I2S3 */
#define TEGRA186_CLK_SYNC_I2S3 249
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_I2S4 */
#define TEGRA186_CLK_SYNC_I2S4 250
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_I2S5 */
#define TEGRA186_CLK_SYNC_I2S5 251
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_I2S6 */
#define TEGRA186_CLK_SYNC_I2S6 252
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_DSPK1 */
#define TEGRA186_CLK_SYNC_DSPK1 253
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_DSPK2 */
#define TEGRA186_CLK_SYNC_DSPK2 254
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_DMIC1 */
#define TEGRA186_CLK_SYNC_DMIC1 255
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_DMIC2 */
#define TEGRA186_CLK_SYNC_DMIC2 256
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_DMIC3 */
#define TEGRA186_CLK_SYNC_DMIC3 257
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_DMIC4 */
#define TEGRA186_CLK_SYNC_DMIC4 259
/** @brief output of mux controlled by CLK_RST_CONTROLLER_AUDIO_SYNC_CLK_SPDIF */
#define TEGRA186_CLK_SYNC_SPDIF 260
/** @brief output of gate CLK_ENB_PLLREFE_OUT */
#define TEGRA186_CLK_PLLREFE_OUT_GATED 261
/** @brief output of the divider PLLREFE_DIVP in CLK_RST_CONTROLLER_PLLREFE_BASE. PLLREFE has 2 outputs:
  *      * VCO/pdiv defined by this clock object
  *      * VCO/2 defined by TEGRA186_CLK_PLLREFE_OUT
  */
#define TEGRA186_CLK_PLLREFE_OUT1 262
#define TEGRA186_CLK_PLLD_OUT1 267
/** @brief output of the divider PLLP_DIVP in CLK_RST_CONTROLLER_PLLP_BASE */
#define TEGRA186_CLK_PLLP_OUT0 269
/** @brief output of the divider CLK_RST_CONTROLLER_PLLP_OUTC */
#define TEGRA186_CLK_PLLP_OUT5 270
/** PLL controlled by CLK_RST_CONTROLLER_PLLA_BASE for use by audio clocks */
#define TEGRA186_CLK_PLLA 271
/** @brief output of mux controlled by CLK_RST_CONTROLLER_ACLK_BURST_POLICY divided by the divider controlled by ACLK_CLK_DIVISOR in CLK_RST_CONTROLLER_SUPER_ACLK_DIVIDER */
#define TEGRA186_CLK_ACLK 273
/** fixed 48MHz clock divided down from TEGRA186_CLK_PLL_U */
#define TEGRA186_CLK_PLL_U_48M 274
/** fixed 480MHz clock divided down from TEGRA186_CLK_PLL_U */
#define TEGRA186_CLK_PLL_U_480M 275
/** @brief output of the divider PLLC4_DIVP in CLK_RST_CONTROLLER_PLLC4_BASE. Output frequency is TEGRA186_CLK_PLLC4_VCO/PLLC4_DIVP */
#define TEGRA186_CLK_PLLC4_OUT0 276
/** fixed /3 divider. Output frequency of this clock is TEGRA186_CLK_PLLC4_VCO/3 */
#define TEGRA186_CLK_PLLC4_OUT1 277
/** fixed /5 divider. Output frequency of this clock is TEGRA186_CLK_PLLC4_VCO/5 */
#define TEGRA186_CLK_PLLC4_OUT2 278
/** @brief output of mux controlled by PLLC4_CLK_SEL in CLK_RST_CONTROLLER_PLLC4_MISC1 */
#define TEGRA186_CLK_PLLC4_OUT_MUX 279
/** @brief output of divider NVDISPLAY_DISP_CLK_DIVISOR in CLK_RST_CONTROLLER_CLK_SOURCE_NVDISPLAY_DISP when DFLLDISP_DIV is selected in NVDISPLAY_DISP_CLK_SRC */
#define TEGRA186_CLK_DFLLDISP_DIV 284
/** @brief output of divider NVDISPLAY_DISP_CLK_DIVISOR in CLK_RST_CONTROLLER_CLK_SOURCE_NVDISPLAY_DISP when PLLDISPHUB_DIV is selected in NVDISPLAY_DISP_CLK_SRC */
#define TEGRA186_CLK_PLLDISPHUB_DIV 285
/** fixed /8 divider which is used as the input for TEGRA186_CLK_SOR_SAFE */
#define TEGRA186_CLK_PLLP_DIV8 286
/** @brief output of divider CLK_RST_CONTROLLER_BPMP_NIC_RATE */
#define TEGRA186_CLK_BPMP_NIC 287
/** @brief output of the divider CLK_RST_CONTROLLER_PLLA1_OUT1 */
#define TEGRA186_CLK_PLL_A_OUT1 288
/** @deprecated */
#define TEGRA186_CLK_GPC2CLK 289
/** A fake clock which must be enabled during KFUSE read operations to ensure adequate VDD_CORE voltage. */
#define TEGRA186_CLK_KFUSE 293
/**
 * @brief controls the PLLE hardware sequencer.
 * @details This clock only has enable and disable methods. When the
 * PLLE hw sequencer is enabled, PLLE, will be enabled or disabled by
 * hw based on the control signals from the PCIe, SATA and XUSB
 * clocks. When the PLLE hw sequencer is disabled, the state of PLLE
 * is controlled by sw using clk_enable/clk_disable on
 * TEGRA186_CLK_PLLE.
 */
#define TEGRA186_CLK_PLLE_PWRSEQ 294
/** fixed 60MHz clock divided down from, TEGRA186_CLK_PLL_U */
#define TEGRA186_CLK_PLLREFE_REF 295
/** @brief output of mux controlled by SOR0_CLK_SEL0 and SOR0_CLK_SEL1 in CLK_RST_CONTROLLER_CLK_SOURCE_SOR0 */
#define TEGRA186_CLK_SOR0_OUT 296
/** @brief output of mux controlled by SOR1_CLK_SEL0 and SOR1_CLK_SEL1 in CLK_RST_CONTROLLER_CLK_SOURCE_SOR1 */
#define TEGRA186_CLK_SOR1_OUT 297
/** @brief fixed /5 divider.  Output frequency of this clock is TEGRA186_CLK_PLLREFE_OUT1/5. Used as input for TEGRA186_CLK_EQOS_AXI */
#define TEGRA186_CLK_PLLREFE_OUT1_DIV5 298
/** @brief controls the UTMIP_PLL (aka PLLU) hardware sqeuencer */
#define TEGRA186_CLK_UTMIP_PLL_PWRSEQ 301
/** @brief output of the divider CLK_RST_CONTROLLER_CLK_SOURCE_PEX_USB_PAD_PLL0_MGMT */
#define TEGRA186_CLK_PEX_USB_PAD0_MGMT 302
/** @brief output of the divider CLK_RST_CONTROLLER_CLK_SOURCE_PEX_USB_PAD_PLL1_MGMT */
#define TEGRA186_CLK_PEX_USB_PAD1_MGMT 303
/** @brief controls the UPHY_PLL0 hardware sqeuencer */
#define TEGRA186_CLK_UPHY_PLL0_PWRSEQ 304
/** @brief controls the UPHY_PLL1 hardware sqeuencer */
#define TEGRA186_CLK_UPHY_PLL1_PWRSEQ 305
/** @brief control for PLLREFE_IDDQ in CLK_RST_CONTROLLER_PLLREFE_MISC so the bypass output even be used when the PLL is disabled */
#define TEGRA186_CLK_PLLREFE_PLLE_PASSTHROUGH 306
/** @brief output of the mux controlled by PLLREFE_SEL_CLKIN_PEX in CLK_RST_CONTROLLER_PLLREFE_MISC */
#define TEGRA186_CLK_PLLREFE_PEX 307
/** @brief control for PLLREFE_IDDQ in CLK_RST_CONTROLLER_PLLREFE_MISC to turn on the PLL when enabled */
#define TEGRA186_CLK_PLLREFE_IDDQ 308
/** @brief output of the divider QSPI_CLK_DIV2_SEL in CLK_RST_CONTROLLER_CLK_SOURCE_QSPI */
#define TEGRA186_CLK_QSPI_OUT 309
/**
 * @brief GPC2CLK-div-2
 * @details fixed /2 divider. Output frequency is
 * TEGRA186_CLK_GPC2CLK/2. The frequency of this clock is the
 * frequency at which the GPU graphics engine runs. */
#define TEGRA186_CLK_GPCCLK 310
/** @brief output of divider CLK_RST_CONTROLLER_AON_NIC_RATE */
#define TEGRA186_CLK_AON_NIC 450
/** @brief output of divider CLK_RST_CONTROLLER_SCE_NIC_RATE */
#define TEGRA186_CLK_SCE_NIC 451
/** Fixed 100MHz PLL for PCIe, SATA and superspeed USB */
#define TEGRA186_CLK_PLLE 512
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLC_BASE */
#define TEGRA186_CLK_PLLC 513
/** Fixed 408MHz PLL for use by peripheral clocks */
#define TEGRA186_CLK_PLLP 516
/** @deprecated */
#define TEGRA186_CLK_PLL_P TEGRA186_CLK_PLLP
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLD_BASE for use by DSI */
#define TEGRA186_CLK_PLLD 518
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLD2_BASE for use by HDMI or DP */
#define TEGRA186_CLK_PLLD2 519
/**
 * @brief PLL controlled by CLK_RST_CONTROLLER_PLLREFE_BASE.
 * @details Note that this clock only controls the VCO output, before
 * the post-divider. See TEGRA186_CLK_PLLREFE_OUT1 for more
 * information.
 */
#define TEGRA186_CLK_PLLREFE_VCO 520
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLC2_BASE */
#define TEGRA186_CLK_PLLC2 521
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLC3_BASE */
#define TEGRA186_CLK_PLLC3 522
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLDP_BASE for use as the DP link clock */
#define TEGRA186_CLK_PLLDP 523
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLC4_BASE */
#define TEGRA186_CLK_PLLC4_VCO 524
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLA1_BASE for use by audio clocks */
#define TEGRA186_CLK_PLLA1 525
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLNVCSI_BASE */
#define TEGRA186_CLK_PLLNVCSI 526
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLDISPHUB_BASE */
#define TEGRA186_CLK_PLLDISPHUB 527
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLD3_BASE for use by HDMI or DP */
#define TEGRA186_CLK_PLLD3 528
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLBPMPCAM_BASE */
#define TEGRA186_CLK_PLLBPMPCAM 531
/** @brief PLL controlled by CLK_RST_CONTROLLER_PLLAON_BASE for use by IP blocks in the AON domain */
#define TEGRA186_CLK_PLLAON 532
/** Fixed frequency 960MHz PLL for USB and EAVB */
#define TEGRA186_CLK_PLLU 533
/** fixed /2 divider. Output frequency is TEGRA186_CLK_PLLC4_VCO/2 */
#define TEGRA186_CLK_PLLC4_VCO_DIV2 535
/** @brief NAFLL clock source for AXI_CBB */
#define TEGRA186_CLK_NAFLL_AXI_CBB 564
/** @brief NAFLL clock source for BPMP */
#define TEGRA186_CLK_NAFLL_BPMP 565
/** @brief NAFLL clock source for ISP */
#define TEGRA186_CLK_NAFLL_ISP 566
/** @brief NAFLL clock source for NVDEC */
#define TEGRA186_CLK_NAFLL_NVDEC 567
/** @brief NAFLL clock source for NVENC */
#define TEGRA186_CLK_NAFLL_NVENC 568
/** @brief NAFLL clock source for NVJPG */
#define TEGRA186_CLK_NAFLL_NVJPG 569
/** @brief NAFLL clock source for SCE */
#define TEGRA186_CLK_NAFLL_SCE 570
/** @brief NAFLL clock source for SE */
#define TEGRA186_CLK_NAFLL_SE 571
/** @brief NAFLL clock source for TSEC */
#define TEGRA186_CLK_NAFLL_TSEC 572
/** @brief NAFLL clock source for TSECB */
#define TEGRA186_CLK_NAFLL_TSECB 573
/** @brief NAFLL clock source for VI */
#define TEGRA186_CLK_NAFLL_VI 574
/** @brief NAFLL clock source for VIC */
#define TEGRA186_CLK_NAFLL_VIC 575
/** @brief NAFLL clock source for DISP */
#define TEGRA186_CLK_NAFLL_DISP 576
/** @brief NAFLL clock source for GPU */
#define TEGRA186_CLK_NAFLL_GPU 577
/** @brief NAFLL clock source for M-CPU cluster */
#define TEGRA186_CLK_NAFLL_MCPU 578
/** @brief NAFLL clock source for B-CPU cluster */
#define TEGRA186_CLK_NAFLL_BCPU 579
/** @brief input from Tegra's CLK_32K_IN pad */
#define TEGRA186_CLK_CLK_32K 608
/** @brief output of divider CLK_RST_CONTROLLER_CLK_M_DIVIDE */
#define TEGRA186_CLK_CLK_M 609
/** @brief output of divider PLL_REF_DIV in CLK_RST_CONTROLLER_OSC_CTRL */
#define TEGRA186_CLK_PLL_REF 610
/** @brief input from Tegra's XTAL_IN */
#define TEGRA186_CLK_OSC 612
/** @brief clock recovered from EAVB input */
#define TEGRA186_CLK_EQOS_RX_INPUT 613
/** @brief clock recovered from DTV input */
#define TEGRA186_CLK_DTV_INPUT 614
/** @brief SOR0 brick output which feeds into SOR0_CLK_SEL mux in CLK_RST_CONTROLLER_CLK_SOURCE_SOR0*/
#define TEGRA186_CLK_SOR0_PAD_CLKOUT 615
/** @brief SOR1 brick output which feeds into SOR1_CLK_SEL mux in CLK_RST_CONTROLLER_CLK_SOURCE_SOR1*/
#define TEGRA186_CLK_SOR1_PAD_CLKOUT 616
/** @brief clock recovered from I2S1 input */
#define TEGRA186_CLK_I2S1_SYNC_INPUT 617
/** @brief clock recovered from I2S2 input */
#define TEGRA186_CLK_I2S2_SYNC_INPUT 618
/** @brief clock recovered from I2S3 input */
#define TEGRA186_CLK_I2S3_SYNC_INPUT 619
/** @brief clock recovered from I2S4 input */
#define TEGRA186_CLK_I2S4_SYNC_INPUT 620
/** @brief clock recovered from I2S5 input */
#define TEGRA186_CLK_I2S5_SYNC_INPUT 621
/** @brief clock recovered from I2S6 input */
#define TEGRA186_CLK_I2S6_SYNC_INPUT 622
/** @brief clock recovered from SPDIFIN input */
#define TEGRA186_CLK_SPDIFIN_SYNC_INPUT 623

/**
 * @brief subject to change
 * @details maximum clock identifier value plus one.
 */
#define TEGRA186_CLK_CLK_MAX 624

/** @} */

#endif
