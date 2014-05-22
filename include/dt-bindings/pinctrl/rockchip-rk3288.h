/*
 * Header providing constants for Rockchip pinctrl bindings.
 *
 * Copyright (c) 2013 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DT_BINDINGS_ROCKCHIP_PINCTRL_RK3288_H__
#define __DT_BINDINGS_ROCKCHIP_PINCTRL_RK3288_H__

/* GPIO0_A */
#define GPIO0_A0 0x0a00
#define GLOBAL_PWROFF 0x0a01

#define GPIO0_A1 0x0a10
#define DDRIO_PWROFF 0x0a11

#define GPIO0_A2 0x0a20
#define DDR0_RETENTION 0x0a21

#define GPIO0_A3 0x0a30
#define DDR1_RETENTION 0x0a31

#define GPIO0_A4 0x0a40

#define GPIO0_A5 0x0a50

#define GPIO0_A6 0x0a60

#define GPIO0_A7 0x0a70

/* GPIO0_B */
#define GPIO0_B0 0x0b00

#define GPIO0_B1 0x0b10

#define GPIO0_B2 0x0b20
#define TSADC_INT 0x0b21

#define GPIO0_B3 0x0b30

#define GPIO0_B4 0x0b40

#define GPIO0_B5 0x0b50
#define CLK_27M 0x0b51

#define GPIO0_B6 0x0b60

#define GPIO0_B7 0x0b70
#define I2C0PMU_SDA 0x0b71


/* GPIO0_C */
#define GPIO0_C0 0x0c00
#define I2C0PMU_SCL 0x0c01

#define GPIO0_C1 0x0c10
#define TEST_CLKOUT 0x0c11
#define CLKT1_27M 0x0c12

#define GPIO0_C2 0x0c20


/* GPIO1_A */
/* GPIO1_B */
/* GPIO1_C */
/* GPIO1_D */
#define GPIO1_D0 0x1d00
#define LCDC0_HSYNC_GPIO1D 0x1d01

#define GPIO1_D1 0x1d10
#define LCDC0_VSYNC_GPIO1D 0x1d11

#define GPIO1_D2 0x1d20
#define LCDC0_DEN_GPIO1D 0x1d21

#define GPIO1_D3 0x1d30
#define LCDC0_DCLK_GPIO1D 0x1d31


/* GPIO2_A */
#define GPIO2_A0 0x2a00
#define CIF_DATA2 0x2a01
#define HOST_DIN0 0x2a02
#define HSADC_DATA0 0x2a03

#define GPIO2_A1 0x2a10
#define CIF_DATA3 0x2a11
#define HOST_DIN1 0x2a12
#define HSADC_DATA1 0x2a13

#define GPIO2_A2 0x2a20
#define CIF_DATA4 0x2a21
#define HOST_DIN2 0x2a22
#define HSADC_DATA2 0x2a23

#define GPIO2_A3 0x2a30
#define CIF_DATA5 0x2a31
#define HOST_DIN3 0x2a32
#define HSADC_DATA3 0x2a33

#define GPIO2_A4 0x2a40
#define CIF_DATA6 0x2a41
#define HOST_CKINP 0x2a42
#define HSADC_DATA4 0x2a43

#define GPIO2_A5 0x2a50
#define CIF_DATA7 0x2a51
#define HOST_CKINN 0x2a52
#define HSADC_DATA5 0x2a53

#define GPIO2_A6 0x2a60
#define CIF_DATA8 0x2a61
#define HOST_DIN4 0x2a62
#define HSADC_DATA6 0x2a63

#define GPIO2_A7 0x2a70
#define CIF_DATA9 0x2a71
#define HOST_DIN5 0x2a72
#define HSADC_DATA7 0x2a73


/* GPIO2_B */
#define GPIO2_B0 0x2b00
#define CIF_VSYNC 0x2b01
#define HOST_DIN6 0x2b02
#define HSADCTS_SYNC 0x2b03

#define GPIO2_B1 0x2b10
#define CIF_HREF 0x2b11
#define HOST_DIN7 0x2b12
#define HSADCTS_VALID 0x2b13

#define GPIO2_B2 0x2b20
#define CIF_CLKIN 0x2b21
#define HOST_WKACK 0x2b22
#define GPS_CLK 0x2b23

#define GPIO2_B3 0x2b30
#define CIF_CLKOUT 0x2b31
#define HOST_WKREQ 0x2b32
#define HSADCTS_FAIL 0x2b33

#define GPIO2_B4 0x2b40
#define CIF_DATA0 0x2b41

#define GPIO2_B5 0x2b50
#define CIF_DATA1 0x2b51

#define GPIO2_B6 0x2b60
#define CIF_DATA10 0x2b61

#define GPIO2_B7 0x2b70
#define CIF_DATA11 0x2b71


/* GPIO2_C */
#define GPIO2_C0 0x2c00
#define I2C3CAM_SCL 0x2c01

#define GPIO2_C1 0x2c10
#define I2C3CAM_SDA 0x2c11


/* GPIO2_D */
/* GPIO3_A */
#define GPIO3_A0 0x3a00
#define FLASH0_DATA0 0x3a01
#define EMMC_DATA0 0x3a02

#define GPIO3_A1 0x3a10
#define FLASH0_DATA1 0x3a11
#define EMMC_DATA1 0x3a12

#define GPIO3_A2 0x3a20
#define FLASH0_DATA2 0x3a21
#define EMMC_DATA2 0x3a22

#define GPIO3_A3 0x3a30
#define FLASH0_DATA3 0x3a31
#define EMMC_DATA3 0x3a32

#define GPIO3_A4 0x3a40
#define FLASH0_DATA4 0x3a41
#define EMMC_DATA4 0x3a42

#define GPIO3_A5 0x3a50
#define FLASH0_DATA5 0x3a51
#define EMMC_DATA5 0x3a52

#define GPIO3_A6 0x3a60
#define FLASH0_DATA6 0x3a61
#define EMMC_DATA6 0x3a62

#define GPIO3_A7 0x3a70
#define FLASH0_DATA7 0x3a71
#define EMMC_DATA7 0x3a72


/* GPIO3_B */
#define GPIO3_B0 0x3b00
#define FLASH0_RDY 0x3b01

#define GPIO3_B1 0x3b10
#define FLASH0_WP 0x3b11
#define EMMC_PWREN 0x3b12

#define GPIO3_B2 0x3b20
#define FLASH0_RDN 0x3b21

#define GPIO3_B3 0x3b30
#define FLASH0_ALE 0x3b31

#define GPIO3_B4 0x3b40
#define FLASH0_CLE 0x3b41

#define GPIO3_B5 0x3b50
#define FLASH0_WRN 0x3b51

#define GPIO3_B6 0x3b60
#define FLASH0_CSN0 0x3b61

#define GPIO3_B7 0x3b70
#define FLASH0_CSN1 0x3b71


/* GPIO3_C */
#define GPIO3_C0 0x3c00
#define FLASH0_CSN2 0x3c01
#define EMMC_CMD 0x3c02

#define GPIO3_C1 0x3c10
#define FLASH0_CSN3 0x3c11
#define EMMC_RSTNOUT 0x3c12

#define GPIO3_C2 0x3c20
#define FLASH0_DQS 0x3c21
#define EMMC_CLKOUT 0x3c22


/* GPIO3_D */
#define GPIO3_D0 0x3d00
#define FLASH1_DATA0 0x3d01
#define HOST_DOUT0 0x3d02
#define MAC_TXD2 0x3d03
#define SDIO1_DATA0 0x3d04

#define GPIO3_D1 0x3d10
#define FLASH1_DATA1 0x3d11
#define HOST_DOUT1 0x3d12
#define MAC_TXD3 0x3d13
#define SDIO1_DATA1 0x3d14

#define GPIO3_D2 0x3d20
#define FLASH1_DATA2 0x3d21
#define HOST_DOUT2 0x3d22
#define MAC_RXD2 0x3d23
#define SDIO1_DATA2 0x3d24

#define GPIO3_D3 0x3d30
#define FLASH1_DATA3 0x3d31
#define HOST_DOUT3 0x3d32
#define MAC_RXD3 0x3d33
#define SDIO1_DATA3 0x3d34

#define GPIO3_D4 0x3d40
#define FLASH1_DATA4 0x3d41
#define HOST_DOUT4 0x3d42
#define MAC_TXD0 0x3d43
#define SDIO1_DETECTN 0x3d44

#define GPIO3_D5 0x3d50
#define FLASH1_DATA5 0x3d51
#define HOST_DOUT5 0x3d52
#define MAC_TXD1 0x3d53
#define SDIO1_WRPRT 0x3d54

#define GPIO3_D6 0x3d60
#define FLASH1_DATA6 0x3d61
#define HOST_DOUT6 0x3d62
#define MAC_RXD0 0x3d63
#define SDIO1_BKPWR 0x3d64

#define GPIO3_D7 0x3d70
#define FLASH1_DATA7 0x3d71
#define HOST_DOUT7 0x3d72
#define MAC_RXD1 0x3d73
#define SDIO1_INTN 0x3d74


/* GPIO4_A */
#define GPIO4_A0 0x4a00
#define FLASH1_RDY 0x4a01
#define HOST_CKOUTP 0x4a02
#define MAC_MDC 0x4a03

#define GPIO4_A1 0x4a10
#define FLASH1_WP 0x4a11
#define HOST_CKOUTN 0x4a12
#define MAC_RXDV 0x4a13
#define FLASH0_CSN4 0x4a14

#define GPIO4_A2 0x4a20
#define FLASH1_RDN 0x4a21
#define HOST_DOUT8 0x4a22
#define MAC_RXER 0x4a23
#define FLASH0_CSN5 0x4a24

#define GPIO4_A3 0x4a30
#define FLASH1_ALE 0x4a31
#define HOST_DOUT9 0x4a32
#define MAC_CLK 0x4a33
#define FLASH0_CSN6 0x4a34

#define GPIO4_A4 0x4a40
#define FLASH1_CLE 0x4a41
#define HOST_DOUT10 0x4a42
#define MAC_TXEN 0x4a43
#define FLASH0_CSN7 0x4a44

#define GPIO4_A5 0x4a50
#define FLASH1_WRN 0x4a51
#define HOST_DOUT11 0x4a52
#define MAC_MDIO 0x4a53

#define GPIO4_A6 0x4a60
#define FLASH1_CSN0 0x4a61
#define HOST_DOUT12 0x4a62
#define MAC_RXCLK 0x4a63
#define SDIO1_CMD 0x4a64

#define GPIO4_A7 0x4a70
#define FLASH1_CSN1 0x4a71
#define HOST_DOUT13 0x4a72
#define MAC_CRS 0x4a73
#define SDIO1_CLKOUT 0x4a74


/* GPIO4_B */
#define GPIO4_B0 0x4b00
#define FLASH1_DQS 0x4b01
#define HOST_DOUT14 0x4b02
#define MAC_COL 0x4b03
#define FLASH1_CSN3 0x4b04

#define GPIO4_B1 0x4b10
#define FLASH1_CSN2 0x4b11
#define HOST_DOUT15 0x4b12
#define MAC_TXCLK 0x4b13
#define SDIO1_PWREN 0x4b14


/* GPIO4_C */
#define GPIO4_C0 0x4c00
#define UART0BT_SIN 0x4c01

#define GPIO4_C1 0x4c10
#define UART0BT_SOUT 0x4c11

#define GPIO4_C2 0x4c20
#define UART0BT_CTSN 0x4c21

#define GPIO4_C3 0x4c30
#define UART0BT_RTSN 0x4c31

#define GPIO4_C4 0x4c40
#define SDIO0_DATA0 0x4c41

#define GPIO4_C5 0x4c50
#define SDIO0_DATA1 0x4c51

#define GPIO4_C6 0x4c60
#define SDIO0_DATA2 0x4c61

#define GPIO4_C7 0x4c70
#define SDIO0_DATA3 0x4c71


/* GPIO4_D */
#define GPIO4_D0 0x4d00
#define SDIO0_CMD 0x4d01

#define GPIO4_D1 0x4d10
#define SDIO0_CLKOUT 0x4d11

#define GPIO4_D2 0x4d20
#define SDIO0_DETECTN 0x4d21

#define GPIO4_D3 0x4d30
#define SDIO0_WRPRT 0x4d31

#define GPIO4_D4 0x4d40
#define SDIO0_PWREN 0x4d41

#define GPIO4_D5 0x4d50
#define SDIO0_BKPWR 0x4d51

#define GPIO4_D6 0x4d60
#define SDIO0_INTN 0x4d61


/* GPIO5_A */
/* GPIO5_B */
#define GPIO5_B0 0x5b00
#define UART1BB_SIN 0x5b01
#define TS0_DATA0 0x5b02

#define GPIO5_B1 0x5b10
#define UART1BB_SOUT 0x5b11
#define TS0_DATA1 0x5b12

#define GPIO5_B2 0x5b20
#define UART1BB_CTSN 0x5b21
#define TS0_DATA2 0x5b22

#define GPIO5_B3 0x5b30
#define UART1BB_RTSN 0x5b31
#define TS0_DATA3 0x5b32

#define GPIO5_B4 0x5b40
#define SPI0_CLK 0x5b41
#define TS0_DATA4 0x5b42
#define UART4EXP_CTSN 0x5b43

#define GPIO5_B5 0x5b50
#define SPI0_CS0 0x5b51
#define TS0_DATA5 0x5b52
#define UART4EXP_RTSN 0x5b53

#define GPIO5_B6 0x5b60
#define SPI0_TXD 0x5b61
#define TS0_DATA6 0x5b62
#define UART4EXP_SOUT 0x5b63

#define GPIO5_B7 0x5b70
#define SPI0_RXD 0x5b71
#define TS0_DATA7 0x5b72
#define UART4EXP_SIN 0x5b73


/* GPIO5_C */
#define GPIO5_C0 0x5c00
#define SPI0_CS1 0x5c01
#define TS0_SYNC 0x5c02

#define GPIO5_C1 0x5c10
#define TS0_VALID 0x5c11

#define GPIO5_C2 0x5c20
#define TS0_CLK 0x5c21

#define GPIO5_C3 0x5c30
#define TS0_ERR 0x5c31


/* GPIO5_D */
/* GPIO6_A */
#define GPIO6_A0 0x6a00
#define I2S_SCLK 0x6a01

#define GPIO6_A1 0x6a10
#define I2S_LRCKRX 0x6a11

#define GPIO6_A2 0x6a20
#define I2S_LRCKTX 0x6a21

#define GPIO6_A3 0x6a30
#define I2S_SDI 0x6a31

#define GPIO6_A4 0x6a40
#define I2S_SDO0 0x6a41

#define GPIO6_A5 0x6a50
#define I2S_SDO1 0x6a51

#define GPIO6_A6 0x6a60
#define I2S_SDO2 0x6a61

#define GPIO6_A7 0x6a70
#define I2S_SDO3 0x6a71


/* GPIO6_B */
#define GPIO6_B0 0x6b00
#define I2S_CLK 0x6b01

#define GPIO6_B1 0x6b10
#define I2C2AUDIO_SDA 0x6b11

#define GPIO6_B2 0x6b20
#define I2C2AUDIO_SCL 0x6b21

#define GPIO6_B3 0x6b30
#define SPDIF_TX 0x6b31


/* GPIO6_C */
#define GPIO6_C0 0x6c00
#define SDMMC0_DATA0 0x6c01
#define JTAG_TMS 0x6c02

#define GPIO6_C1 0x6c10
#define SDMMC0_DATA1 0x6c11
#define JTAG_TRSTN 0x6c12

#define GPIO6_C2 0x6c20
#define SDMMC0_DATA2 0x6c21
#define JTAG_TDI 0x6c22

#define GPIO6_C3 0x6c30
#define SDMMC0_DATA3 0x6c31
#define JTAG_TCK 0x6c32

#define GPIO6_C4 0x6c40
#define SDMMC0_CLKOUT 0x6c41
#define JTAG_TDO 0x6c42

#define GPIO6_C5 0x6c50
#define SDMMC0_CMD 0x6c51

#define GPIO6_C6 0x6c60
#define SDMMC0_DECTN 0x6c61


/* GPIO6_D */
/* GPIO7_A */
#define GPIO7_A0 0x7a00
#define PWM0 0x7a01
#define VOP0_PWM 0x7a02
#define VOP1_PWM 0x7a03

#define GPIO7_A1 0x7a10
#define PWM1 0x7a11

#define GPIO7_A7 0x7a70
#define UART3GPS_SIN 0x7a71
#define GPS_MAG 0x7a72
#define HSADCT1_DATA0 0x7a73


/* GPIO7_B */
#define GPIO7_B0 0x7b00
#define UART3GPS_SOUT 0x7b01
#define GPS_SIG 0x7b02
#define HSADCT1_DATA1 0x7b03

#define GPIO7_B1 0x7b10
#define UART3GPS_CTSN 0x7b11
#define GPS_RFCLK 0x7b12
#define GPST1_CLK 0x7b13

#define GPIO7_B2 0x7b20
#define UART3GPS_RTSN 0x7b21
#define USB_DRVVBUS0 0x7b22

#define GPIO7_B3 0x7b30
#define USB_DRVVBUS1 0x7b31
#define EDP_HOTPLUG 0x7b32

#define GPIO7_B4 0x7b40
#define ISP_SHUTTEREN 0x7b41
#define SPI1_CLK 0x7b42

#define GPIO7_B5 0x7b50
#define ISP_FLASHTRIGOUTSPI1_CS0 0x7b51
#define SPI1_CS0 0x7b52

#define GPIO7_B6 0x7b60
#define ISP_PRELIGHTTRIGSPI1_RXD 0x7b61
#define SPI1_RXD 0x7b62

#define GPIO7_B7 0x7b70
#define ISP_SHUTTERTRIG 0x7b71
#define SPI1_TXD 0x7b72


/* GPIO7_C */
#define GPIO7_C0 0x7c00
#define ISP_FLASHTRIGIN 0x7c01
#define EDPHDMI_CECINOUTRESERVED 0x7c02

#define GPIO7_C1 0x7c10
#define I2C4TP_SDA 0x7c11

#define GPIO7_C2 0x7c20
#define I2C4TP_SCL 0x7c21

#define GPIO7_C3 0x7c30
#define I2C5HDMI_SDA 0x7c31
#define EDPHDMII2C_SDA 0x7c32

#define GPIO7_C4 0x7c40
#define I2C5HDMI_SCL 0x7c41
#define EDPHDMII2C_SCL 0x7c42

#define GPIO7_C6 0x7c60
#define UART2DBG_SIN 0x7c61
#define UART2DBG_SIRIN 0x7c62
#define PWM2 0x7c63

#define GPIO7_C7 0x7c70
#define UART2DBG_SOUT 0x7c71
#define UART2DBG_SIROUT 0x7c72
#define PWM3 0x7c73
#define EDPHDMI_CECINOUT 0x7c74


/* GPIO7_D */
/* GPIO8_A */
#define GPIO8_A0 0x8a00
#define PS2_CLK 0x8a01
#define SC_VCC18V 0x8a02

#define GPIO8_A1 0x8a10
#define PS2_DATA 0x8a11
#define SC_VCC33V 0x8a12

#define GPIO8_A2 0x8a20
#define SC_DETECTT1 0x8a21

#define GPIO8_A3 0x8a30
#define SPI2_CS1 0x8a31
#define SC_IOT1 0x8a32

#define GPIO8_A4 0x8a40
#define I2C1SENSOR_SDA 0x8a41
#define SC_RST_GPIO8A 0x8a42

#define GPIO8_A5 0x8a50
#define I2C1SENSOR_SCL 0x8a51
#define SC_CLK_GPIO8A 0x8a52

#define GPIO8_A6 0x8a60
#define SPI2_CLK 0x8a61
#define SC_IO 0x8a62

#define GPIO8_A7 0x8a70
#define SPI2_CS0 0x8a71
#define SC_DETECT 0x8a72


/* GPIO8_B */
#define GPIO8_B0 0x8b00
#define SPI2_RXD 0x8b01
#define SC_RST_GPIO8B 0x8b02

#define GPIO8_B1 0x8b10
#define SPI2_TXD 0x8b11
#define SC_CLK_GPIO8B 0x8b12


/* GPIO8_C */
/* GPIO8_D */


#endif
