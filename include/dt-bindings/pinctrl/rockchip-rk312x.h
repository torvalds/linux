#ifndef __DT_BINDINGS_ROCKCHIP_PINCTRL_RK312X_H__
#define __DT_BINDINGS_ROCKCHIP_PINCTRL_RK312X_H__

/* GPIO0_A */
#define GPIO0_A0 0x0a00
#define I2C0_SCL 0x0a01

#define GPIO0_A1 0x0a10
#define I2C0_SDA 0x0a11

#define GPIO0_A2 0x0a20
#define I2C1_SCL 0x0a21

#define GPIO0_A3 0x0a30
#define I2C1_SDA 0x0a31
#define MMC1_CMD 0x0a32

#define GPIO0_A6 0x0a60
#define I2C3_SCL 0x0a61
#define HDMI_DSCL 0x0a62

#define GPIO0_A7 0x0a70
#define I2C3_SDA 0x0a71
#define HDMI_DSDA 0x0a72


/* GPIO0_B */
#define GPIO0_B0 0x0b00
#define I2S0_MCLK_MUX0 0x0b01

#define GPIO0_B1 0x0b10
#define I2S0_SCLK_MUX0 0x0b11
#define SPI0_CLK_MUX2 0x0b12

#define GPIO0_B3 0x0b30
#define I2S0_LRCKRX_MUX0 0x0b31
#define SPI0_TXD_MUX2 0x0b32

#define GPIO0_B4 0x0b40
#define I2S0_LRCKTX_MUX0 0x0b41

#define GPIO0_B5 0x0b50
#define I2S0_SDO_MUX0 0x0b51
#define SPI0_RXD_MUX2 0x0b52

#define GPIO0_B6 0x0b60
#define I2S0_SDI_MUX0 0x0b61
#define SPI0_CS0_MUX2 0x0b62

#define GPIO0_B7 0x0b70
#define HDMI_HPD 0x0b71


/* GPIO0_C */
#define GPIO0_C1 0x0c10
#define SC_IO 0x0c11
#define UART0_RTSN 0x0c12

#define GPIO0_C4 0x0c40
#define HDMI_CEC 0x0c41

#define GPIO0_C7 0x0c70
#define NAND_CS1 0x0c71


/* GPIO0_D */
#define GPIO0_D0 0x0d00
#define UART2_RTSN 0x0d01
#define PMIC_SLEEP_MUX0 0x0d02

#define GPIO0_D1 0x0d10
#define UART2_CTSN 0x0d11

#define GPIO0_D2 0x0d20
#define PWM0 0x0d21

#define GPIO0_D3 0x0d30
#define PWM1 0x0d31

#define GPIO0_D4 0x0d40
#define PWM2 0x0d41

#define GPIO0_D6 0x0d60
#define MMC1_PWREN 0x0d61


/* GPIO1_A */
#define GPIO1_A0 0x1a00
#define I2S0_MCLK_MUX1 0x1a01
#define SDMMC_CLKOUT 0x1a02
#define XIN32K 0x1a03

#define GPIO1_A1 0x1a10
#define I2S0_SCLK_MUX1 0x1a11
#define SDMMC_DATA0 0x1a12
#define PMIC_SLEEP_MUX1 0x1a13

#define GPIO1_A2 0x1a20
#define I2S0_LRCKRX_MUX1 0x1a21
#define SDMMC_DATA1 0x1a22

#define GPIO1_A3 0x1a30
#define I2S0_LRCKTX_MUX1 0x1a31

#define GPIO1_A4 0x1a40
#define I2S0_SDO_MUX1 0x1a41
#define SDMMC_DATA2 0x1a42

#define GPIO1_A5 0x1a50
#define I2S0_SDI_MUX1 0x1a51
#define SDMMC_DATA3 0x1a52

#define GPIO1_A7 0x1a70
#define MMC0_WRPRT 0x1a71


/* GPIO1_B */
#define GPIO1_B0 0x1b00
#define SPI0_CLK_MUX0 0x1b01
#define UART1_CTSN 0x1b02

#define GPIO1_B1 0x1b10
#define SPI0_TXD_MUX0 0x1b11
#define UART1_SOUT 0x1b12

#define GPIO1_B2 0x1b20
#define SPI0_RXD_MUX0 0x1b21
#define UART1_SIN 0x1b22

#define GPIO1_B3 0x1b30
#define SPI0_CS0_MUX0 0x1b31
#define UART1_RTSN 0x1b32

#define GPIO1_B4 0x1b40
#define SPI0_CS1_MUX0 0x1b41

#define GPIO1_B6 0x1b60
#define MMC0_PWREN 0x1b61

#define GPIO1_B7 0x1b70
#define MMC0_CMD 0x1b71


/* GPIO1_C */
#define GPIO1_C0 0x1c00
#define MMC0_CLKOUT 0x1c01

#define GPIO1_C1 0x1c10
#define MMC0_DETN 0x1c11

#define GPIO1_C2 0x1c20
#define MMC0_D0 0x1c21
#define UART2_SOUT 0x1c22

#define GPIO1_C3 0x1c30
#define MMC0_D1 0x1c31
#define UART2_SIN 0x1c32

#define GPIO1_C4 0x1c40
#define MMC0_D2 0x1c41
#define JTAG_TCK 0x1c42

#define GPIO1_C5 0x1c50
#define MMC0_D3 0x1c51
#define JTAG_TMS 0x1c52

#define GPIO1_C6 0x1c60
#define NAND_CS2 0x1c61
#define EMMC_CMD_MUX0 0x1c62

#define GPIO1_C7 0x1c70
#define NAND_CS3 0x1c71
#define EMMC_RSTNOUT 0x1c72


/* GPIO1_D */
#define GPIO1_D0 0x1d00
#define NAND_D0 0x1d01
#define EMMC_D0 0x1d02
#define SFC_D0 0x1d03

#define GPIO1_D1 0x1d10
#define NAND_D1 0x1d11
#define EMMC_D1 0x1d12
#define SFC_D1 0x1d13

#define GPIO1_D2 0x1d20
#define NAND_D2 0x1d21
#define EMMC_D2 0x1d22
#define SFC_D2 0x1d23

#define GPIO1_D3 0x1d30
#define NAND_D3 0x1d31
#define EMMC_D3 0x1d32
#define SFC_D3 0x1d33

#define GPIO1_D4 0x1d40
#define NAND_D4 0x1d41
#define EMMC_D4 0x1d42
#define SPI0_RXD_MUX1 0x1d43

#define GPIO1_D5 0x1d50
#define NAND_D5 0x1d51
#define EMMC_D5 0x1d52
#define SPI0_TXD_MUX1 0x1d53

#define GPIO1_D6 0x1d60
#define NAND_D6 0x1d61
#define EMMC_D6 0x1d62
#define SPI0_CS0_MUX1 0x1d63

#define GPIO1_D7 0x1d70
#define NAND_D7 0x1d71
#define EMMC_D7 0x1d72
#define SPI0_CS1_MUX1 0x1d73


/* GPIO2_A */
#define GPIO2_A0 0x2a00
#define NAND_ALE 0x2a01
#define SPI0_CLK_MUX1 0x2a02

#define GPIO2_A1 0x2a10
#define NAND_CLE 0x2a11

#define GPIO2_A2 0x2a20
#define NAND_WRN 0x2a21
#define SFC_CSN0 0x2a22

#define GPIO2_A3 0x2a30
#define NAND_RDN 0x2a31
#define SFC_CSN1 0x2a32

#define GPIO2_A4 0x2a40
#define NAND_RDY 0x2a41
#define EMMC_CMD_MUX1 0x2a42
#define SFC_CLK 0x2a43

#define GPIO2_A5 0x2a50
#define NAND_WP 0x2a51
#define EMMC_PWREN 0x2a52

#define GPIO2_A6 0x2a60
#define NAND_CS0 0x2a61

#define GPIO2_A7 0x2a70
#define NAND_DQS 0x2a71
#define EMMC_CLKOUT 0x2a72


/* GPIO2_B */
#define GPIO2_B0 0x2b00
#define LCDC0_DCLK 0x2b01
#define EBC_SDCLK 0x2b02
#define GMAC_RXDV 0x2b03

#define GPIO2_B1 0x2b10
#define LCDC0_HSYNC 0x2b11
#define EBC_SDLE 0x2b12
#define GMAC_TXCLK 0x2b13

#define GPIO2_B2 0x2b20
#define LCDC0_VSYNC 0x2b21
#define EBC_SDOE 0x2b22
#define GMAC_CRS 0x2b23

#define GPIO2_B3 0x2b30
#define LCDC0_DEN 0x2b31
#define EBC_GDCLK 0x2b32
#define GMAC_RXCLK 0x2b33

#define GPIO2_B4 0x2b40
#define LCDC0_D10 0x2b41
#define EBC_SDCE2 0x2b42
#define GMAC_MDIO 0x2b43

#define GPIO2_B5 0x2b50
#define LCDC0_D11 0x2b51
#define EBC_SDCE3 0x2b52
#define GMAC_TXEN 0x2b53

#define GPIO2_B6 0x2b60
#define LCDC0_D12 0x2b61
#define EBC_SDCE4 0x2b62
#define GMAC_CLK 0x2b63

#define GPIO2_B7 0x2b70
#define LCDC0_D13 0x2b71
#define EBC_SDCE5 0x2b72
#define GMAC_RXER 0x2b73


/* GPIO2_C */
#define GPIO2_C0 0x2c00
#define LCDC0_D14 0x2c01
#define EBC_VCOM 0x2c02
#define GMAC_RXD1 0x2c03

#define GPIO2_C1 0x2c10
#define LCDC0_D15 0x2c11
#define EBC_GDOE 0x2c12
#define GMAC_RXD0 0x2c13

#define GPIO2_C2 0x2c20
#define LCDC0_D16 0x2c21
#define EBC_GDSP 0x2c22
#define GMAC_TXD1 0x2c23

#define GPIO2_C3 0x2c30
#define LCDC0_D17 0x2c31
#define EBC_GDPWR0 0x2c32
#define GMAC_TXD0 0x2c33

#define GPIO2_C4 0x2c40
#define LCDC0_D18 0x2c41
#define EBC_GDRL 0x2c42
#define I2C2_SDA 0x2c43
#define GMAC_RXD3 0x2c44

#define GPIO2_C5 0x2c50
#define LCDC0_D19 0x2c51
#define EBC_SDSHR 0x2c52
#define I2C2_SCL 0x2c53
#define GMAC_RXD2 0x2c54

#define GPIO2_C6 0x2c60
#define LCDC0_D20 0x2c61
#define EBC_BORDER0 0x2c62
#define GPS_SIGN 0x2c63
#define GMAC_TXD2 0x2c64

#define GPIO2_C7 0x2c70
#define LCDC0_D21 0x2c71
#define EBC_BORDER1 0x2c72
#define GPS_MAG 0x2c73
#define GMAC_TXD3 0x2c74


/* GPIO2_D */
#define GPIO2_D0 0x2d00
#define LCDC0_D22 0x2d01
#define EBC_GDPWR1 0x2d02
#define GPS_CLK 0x2d03
#define GMAC_COL 0x2d04

#define GPIO2_D1 0x2d10
#define LCDC0_D23 0x2d11
#define EBC_GDPWR2 0x2d12
#define GMAC_MDC 0x2d13

#define GPIO2_D2 0x2d20
#define SC_RST 0x2d21
#define UART0_SOUT 0x2d22

#define GPIO2_D3 0x2d30
#define SC_CLK 0x2d31
#define UART0_SIN 0x2d32

#define GPIO2_D5 0x2d50
#define SC_DET 0x2d51
#define UART0_CTSN 0x2d52


/* GPIO3_A */
/* GPIO3_B */
#define GPIO3_B3 0x3b30
#define TESTCLK_OUT 0x3b31


/* GPIO3_C */
#define GPIO3_C1 0x3c10
#define OTG_DRVVBUS 0x3c11


/* GPIO3_D */
#define GPIO3_D2 0x3d20
#define PWM_IRIN 0x3d21

#define GPIO3_D3 0x3d30
#define SPDIF_TX 0x3d31


#endif
