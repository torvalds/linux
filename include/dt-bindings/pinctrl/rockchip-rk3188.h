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

#ifndef __DT_BINDINGS_ROCKCHIP_PINCTRL_RK3188_H__
#define __DT_BINDINGS_ROCKCHIP_PINCTRL_RK3188_H__


/* GPIO0_A */
#define GPIO0_A0 0x0A00
#define GPIO0_A1 0x0A10

/* GPIO0_B */
/* GPIO0_C */
#define GPIO0_C0 0x0c00
#define NAND_D8 0x0c01

#define GPIO0_C1 0x0c10
#define NAND_D9 0x0c11

#define GPIO0_C2 0x0c20
#define NAND_D10 0x0c21

#define GPIO0_C3 0x0c30
#define NAND_D11 0x0c31

#define GPIO0_C4 0x0c40
#define NAND_D12 0x0c41

#define GPIO0_C5 0x0c50
#define NAND_D13 0x0c51

#define GPIO0_C6 0x0c60
#define NAND_D14 0x0c61

#define GPIO0_C7 0x0c70
#define NAND_D15 0x0c71


/* GPIO0_D */
#define GPIO0_D0 0x0d00
#define NAND_DQS 0x0d01
#define EMMC_CLKOUT 0x0d02

#define GPIO0_D1 0x0d10
#define NAND_CS1 0x0d11

#define GPIO0_D2 0x0d20
#define NAND_CS2 0x0d21
#define EMMC_CMD 0x0d22

#define GPIO0_D3 0x0d30
#define NAND_CS3 0x0d31
#define EMMC_RSTNOUT 0x0d32

#define GPIO0_D4 0x0d40
#define SPI1_RXD 0x0d41

#define GPIO0_D5 0x0d50
#define SPI1_TXD 0x0d51

#define GPIO0_D6 0x0d60
#define SPI1_CLK 0x0d61

#define GPIO0_D7 0x0d70
#define SPI1_CS0 0x0d71


/* GPIO1_A */
#define GPIO1_A0 0x1a00
#define UART0_SIN 0x1a01

#define GPIO1_A1 0x1a10
#define UART0_SOUT 0x1a11

#define GPIO1_A2 0x1a20
#define UART0_CTSN 0x1a21

#define GPIO1_A3 0x1a30
#define UART0_RTSN 0x1a31

#define GPIO1_A4 0x1a40
#define UART1_SIN 0x1a41
#define SPI0_RXD 0x1a42

#define GPIO1_A5 0x1a50
#define UART1_SOUT 0x1a51
#define SPI0_TXD 0x1a52

#define GPIO1_A6 0x1a60
#define UART1_CTSN 0x1a61
#define SPI0_CLK 0x1a62

#define GPIO1_A7 0x1a70
#define UART1_RTSN 0x1a71
#define SPI0_CS0 0x1a72

/* GPIO1_B */
#define GPIO1_B0 0x1b00
#define UART2_SIN 0x1b01
#define JTAG_TDI 0x1b02

#define GPIO1_B1 0x1b10
#define UART2_SOUT 0x1b11
#define JTAG_TDO 0x1b12

#define GPIO1_B2 0x1b20
#define UART3_SIN 0x1b21
#define GPS_MAG 0x1b22

#define GPIO1_B3 0x1b30
#define UART3_SOUT 0x1b31
#define GPS_SIG 0x1b32

#define GPIO1_B4 0x1b40
#define UART3_CTSN 0x1b41
#define GPS_RFCLK 0x1b42

#define GPIO1_B5 0x1b50
#define UART3_RTSN 0x1b51

#define GPIO1_B6 0x1b60
#define SPDIF_TX 0x1b61
#define SPI1_CS1 0x1b62

#define GPIO1_B7 0x1b70
#define SPI0_CS1 0x1b71


/* GPIO1_C */
#define GPIO1_C0 0x1c00
#define I2S0_MCLK 0x1c01

#define GPIO1_C1 0x1c10
#define I2S0_SCLK 0x1c11

#define GPIO1_C2 0x1c20
#define I2S0_LRCKRX 0x1c21

#define GPIO1_C3 0x1c30
#define I2S0_LRCKTX 0x1c31

#define GPIO1_C4 0x1c40
#define I2S0_SDI 0x1c41

#define GPIO1_C5 0x1c50
#define I2S0_SDO 0x1c51


/* GPIO1_D */
#define GPIO1_D0 0x1d00
#define I2C0_SDA 0x1d01

#define GPIO1_D1 0x1d10
#define I2C0_SCL 0x1d11

#define GPIO1_D2 0x1d20
#define I2C1_SDA 0x1d21

#define GPIO1_D3 0x1d30
#define I2C1_SCL 0x1d31

#define GPIO1_D4 0x1d40
#define I2C2_SDA 0x1d41

#define GPIO1_D5 0x1d50
#define I2C2_SCL 0x1d51

#define GPIO1_D6 0x1d60
#define I2C4_SDA 0x1d61

#define GPIO1_D7 0x1d70
#define I2C4_SCL 0x1d71


/* GPIO2_A */
#define GPIO2_A0 0x2a00
#define LCDC1_D0 0x2a01
#define SMC_D0 0x2a02
#define TRACE_D0 0x2a03

#define GPIO2_A1 0x2a10
#define LCDC1_D1 0x2a11
#define SMC_D1 0x2a12
#define TRACE_D1 0x2a13

#define GPIO2_A2 0x2a20
#define LCDC1_D2 0x2a21
#define SMC_D2 0x2a22
#define TRACE_D2 0x2a23

#define GPIO2_A3 0x2a30
#define LCDC1_D3 0x2a31
#define SMC_D3 0x2a32
#define TRACE_D3 0x2a33

#define GPIO2_A4 0x2a40
#define LCDC1_D4 0x2a41
#define SMC_D4 0x2a42
#define TRACE_D4 0x2a43

#define GPIO2_A5 0x2a50
#define LCDC1_D5 0x2a51
#define SMC_D5 0x2a52
#define TRACE_D5 0x2a53

#define GPIO2_A6 0x2a60
#define LCDC1_D6 0x2a61
#define SMC_D6 0x2a62
#define TRACE_D6 0x2a63

#define GPIO2_A7 0x2a70
#define LCDC1_D7 0x2a71
#define SMC_D7 0x2a72
#define TRACE_D7 0x2a73


/* GPIO2_B */
#define GPIO2_B0 0x2b00
#define LCDC1_D8 0x2b01
#define SMC_D8 0x2b02
#define TRACE_D8 0x2b03

#define GPIO2_B1 0x2b10
#define LCDC1_D9 0x2b11
#define SMC_D9 0x2b11
#define TRACE_D9 0x2b12

#define GPIO2_B2 0x2b20
#define LCDC1_D10 0x2b21
#define SMC_D10 0x2b22
#define TRACE_D10 0x2b23

#define GPIO2_B3 0x2b30
#define LCDC1_D11 0x2b31
#define SMC_D11 0x2b32
#define TRACE_D11 0x2b33

#define GPIO2_B4 0x2b40
#define LCDC1_D12 0x2b41
#define SMC_D12 0x2b42
#define TRACE_D12 0x2b43

#define GPIO2_B5 0x2b50
#define LCDC1_D13 0x2b51
#define SMC_D13 0x2b52
#define TRACE_D13 0x2b53

#define GPIO2_B6 0x2b60
#define LCDC1_D14 0x2b61
#define SMC_D14 0x2b62
#define TRACE_D14 0x2b63


#define GPIO2_B7 0x2b70
#define LCDC1_D15 0x2b71
#define SMC_D15 0x2b72
#define TRACE_D15 0x2b73


/* GPIO2_C */
#define GPIO2_C0 0x2c00
#define LCDC1_D16 0x2c01
#define SMC_R0 0x2c02
#define TRACE_CLK 0x2c03

#define GPIO2_C1 0x2c10
#define LCDC1_D17 0x2c11
#define SMC_R1 0x2c12
#define TRACE_CTL 0x2c13

#define GPIO2_C2 0x2c20
#define LCDC1_D18 0x2c21
#define SMC_R2 0x2c22

#define GPIO2_C3 0x2c30
#define LCDC1_D19 0x2c31
#define SMC_R3 0x2c32

#define GPIO2_C4 0x2c40
#define LCDC1_D20 0x2c41
#define SMC_R4 0x2c42

#define GPIO2_C5 0x2c50
#define LCDC1_D21 0x2c51
#define SMC_R5 0x2c52

#define GPIO2_C6 0x2c60
#define LCDC1_D22 0x2c61
#define SMC_R6 0x2c62

#define GPIO2_C7 0x2c70
#define LCDC1_D23 0x2c71
#define SMC_R7 0x2c72
 

/* GPIO2_D */
#define GPIO2_D0 0x2d00
#define LCDC1_DCLK 0x2d01
#define SMC_CS0 0x2d02

#define GPIO2_D1 0x2d10
#define LCDC1_DEN 0x2d11
#define SMC_WEN 0x2d12

#define GPIO2_D2 0x2d20
#define LCDC1_HSYNC 0x2d21
#define SMC_OEN 0x2d22

#define GPIO2_D3 0x2d30
#define LCDC1_VSYNC 0x2d31
#define SMC_ADVN 0x2d32

#define GPIO2_D4 0x2d40
#define SMC_BLSN0 0x2d41

#define GPIO2_D5 0x2d50
#define SMC_BLSN1 0x2d51

#define GPIO2_D6 0x2d60
#define SMC_CS1 0x2d61

#define GPIO2_D7 0x2d70
#define TEST_CLK_OUT 0x2d71


/* GPIO3_A */
#define GPIO3_A0 0x3a00
#define MMC0_RSTNOUT 0x3a01

#define GPIO3_A1 0x3a10
#define MMC0_PWREN 0x3a11

#define GPIO3_A2 0x3a20
#define MMC0_CLKOUT 0x3a21

#define GPIO3_A3 0x3a30
#define MMC0_CMD 0x3a31

#define GPIO3_A4 0x3a40
#define MMC0_D0 0x3a41

#define GPIO3_A5 0x3a50
#define MMC0_D1 0x3a51

#define GPIO3_A6 0x3a60
#define MMC0_D2 0x3a61

#define GPIO3_A7 0x3a70
#define MMC0_D3 0x3a71


/* GPIO3_B */
#define GPIO3_B0 0x3b00
#define MMC0_DETN 0x3b01

#define GPIO3_B1 0x3b10
#define MMC0_WRPRT 0x3b11

#define GPIO3_B3 0x3b30
#define CIF0_CLKOUT 0x3b31

#define GPIO3_B4 0x3b40
#define CIF0_D0 0x3b41
#define HSADC_D8 0x3b42

#define GPIO3_B5 0x3b50
#define CIF0_D1 0x3b51
#define HSADC_D9 0x3b52

#define GPIO3_B6 0x3b60
#define CIF0_D10 0x3b61
#define I2C3_SDA 0x3b62

#define GPIO3_B7 0x3b70
#define CIF0_D11 0x3b71
#define I2C3_SCL 0x3b72


/* GPIO3_C */
#define GPIO3_C0 0x3c00
#define MMC1_CMD 0x3c01
#define RMII_TXEN 0x3c02

#define GPIO3_C1 0x3c10
#define MMC1_D0 0x3c11
#define RMII_TXD1 0x3c12

#define GPIO3_C2 0x3c20
#define MMC1_D1 0x3c21
#define RMII_TXD0 0x3c22

#define GPIO3_C3 0x3c30
#define MMC1_D2 0x3c31
#define RMII_RXD0 0x3c32

#define GPIO3_C4 0x3c40
#define MMC1_D3 0x3c41
#define RMII_RXD1 0x3c42

#define GPIO3_C5 0x3c50
#define MMC1_CLKOUT 0x3c51
#define RMII_CLKOUT 0x3c52
#define RMII_CLKIN 0x3c52

#define GPIO3_C6 0x3c60
#define MMC1_DETN 0x3c61
#define RMII_RXERR 0x3c62

#define GPIO3_C7 0x3c70
#define MMC1_WRPRT 0x3c71
#define RMII_CRS 0x3c72


/* GPIO3_D */
#define GPIO3_D0 0x3d00
#define MMC1_PWREN 0x3d01
#define RMII_MD 0x3d02

#define GPIO3_D1 0x3d10
#define MMC1_BKEPWR 0x3d11
#define RMII_MDCLK 0x3d12

#define GPIO3_D2 0x3d20
#define MMC1_INTN 0x3d21

#define GPIO3_D3 0x3d30
#define PWM0 0x3d31

#define GPIO3_D4 0x3d40
#define PWM1 0x3d41
#define JTAG_TRSTN 0x3d42

#define GPIO3_D5 0x3d50
#define PWM2 0x3d51
#define JTAG_TCK 0x3d52
#define OTG_DRV_VBUS 0x3d53

#define GPIO3_D6 0x3d60
#define PWM3 0x3d61
#define JTAG_TMS 0x3d62
#define HOST_DRV_VBUS 0x3d63

#endif
