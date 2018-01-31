/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DT_BINDINGS_ROCKCHIP_PINCTRL_RK3036_H__
#define __DT_BINDINGS_ROCKCHIP_PINCTRL_RK3036_H__

        /* GPIO0_A */
        #define GPIO0_A0 0x0a00
        #define I2C0_SCL 0x0a01
        #define PWM1 0x0a02

        #define GPIO0_A1 0x0a10
        #define I2C0_SDA 0x0a11
        #define PWM2 0x0a12

        #define GPIO0_A2 0x0a20
        #define I2C1_SCL 0x0a21

        #define GPIO0_A3 0x0a30
        #define I2C1_SDA 0x0a31


        /* GPIO0_B */
        #define GPIO0_B0 0x0b00
        #define MMC1_CMD 0x0b01
        #define I2S1_SDO 0x0b02

        #define GPIO0_B1 0x0b10
        #define MMC1_CLKOUT 0x0b11
        #define I2S1_MCLK 0x0b12

        #define GPIO0_B3 0x0b30
        #define MMC1_D0 0x0b31
        #define I2S1_LRCKRX 0x0b32

        #define GPIO0_B4 0x0b40
        #define MMC1_D1 0x0b41
        #define I2S1_LRCKTX 0x0b42

        #define GPIO0_B5 0x0b50
        #define MMC1_D2 0x0b51
        #define I2S1_SDI 0x0b52

        #define GPIO0_B6 0x0b60
        #define MMC1_D3 0x0b61
        #define I2S1_SCLK 0x0b62


        /* GPIO0_C */
        #define GPIO0_C0 0x0c00
        #define UART0_SOUT 0x0c01

        #define GPIO0_C1 0x0c10
        #define UART0_SIN 0x0c11

        #define GPIO0_C2 0x0c20
        #define UART0_RTSN 0x0c21

        #define GPIO0_C3 0x0c30
        #define UART0_CTSN 0x0c31

        #define GPIO0_C4 0x0c40
        #define DRIVE_VBUS 0x0c41


        /* GPIO0_D */
        #define GPIO0_D2 0x0d20
        #define PWM0 0x0d21

        #define GPIO0_D3 0x0d30
        #define PWM3(IR) 0x0d31

        #define GPIO0_D4 0x0d40
        #define SPDIF_TX 0x0d41


        /* GPIO1_A */
        #define GPIO1_A0 0x1a00
        #define I2S0_MCLK 0x1a01

        #define GPIO1_A1 0x1a10
        #define I2S0_SCLK 0x1a11

        #define GPIO1_A2 0x1a20
        #define I2S0_LRCKRX 0x1a21
        #define PWM1_0 0x1a22

        #define GPIO1_A3 0x1a30
        #define I2S0_LRCKTX 0x1a31

        #define GPIO1_A4 0x1a40
        #define I2S0_SDO 0x1a41

        #define GPIO1_A5 0x1a50
        #define I2S0_SDI 0x1a51


        /* GPIO1_B */
        #define GPIO1_B0 0x1b00
        #define HDMI_CEC 0x1b01

        #define GPIO1_B1 0x1b10
        #define HDMI_SDA 0x1b11

        #define GPIO1_B2 0x1b20
        #define HDMI_SCL 0x1b21

        #define GPIO1_B3 0x1b30
        #define HDMI_HPD 0x1b31

        #define GPIO1_B7 0x1b70
        #define MMC0_CMD 0x1b71


        /* GPIO1_C */
        #define GPIO1_C0 0x1c00
        #define MMC0_CLKOUT 0x1c01

        #define GPIO1_C1 0x1c10
        #define MMC0_DETN 0x1c11

        #define GPIO1_C2 0x1c20
        #define MMC0_D0 0x1c21
        #define UART2_SIN 0x1c22

        #define GPIO1_C3 0x1c30
        #define MMC0_D1 0x1c31
        #define UART2_SOUT 0x1c32

        #define GPIO1_C4 0x1c40
        #define MMC0_D2 0x1c41
        #define JTAG_TCK 0x1c42

        #define GPIO1_C5 0x1c50
        #define MMC0_D3 0x1c51
        #define JTAG_TMS 0x1c52


        /* GPIO1_D */
        #define GPIO1_D0 0x1d00
        #define NAND_D0 0x1d01
        #define EMMC_D0 0x1d02
        #define SFC_SIO0 0x1d03

        #define GPIO1_D1 0x1d10
        #define NAND_D1 0x1d11
        #define EMMC_D1 0x1d12
        #define SFC_SIO1 0x1d13

        #define GPIO1_D2 0x1d20
        #define NAND_D2 0x1d21
        #define EMMC_D2 0x1d22
        #define SFC_SIO2 0x1d23

        #define GPIO1_D3 0x1d30
        #define NAND_D3 0x1d31
        #define EMMC_D3 0x1d32
        #define SFC_SIO3 0x1d33

        #define GPIO1_D4 0x1d40
        #define NAND_D4 0x1d41
        #define EMMC_D4 0x1d42
        #define SPI0_RXD 0x1d43

        #define GPIO1_D5 0x1d50
        #define NAND_D5 0x1d51
        #define EMMC_D5 0x1d52
        #define SPI0_TXD 0x1d53

        #define GPIO1_D6 0x1d60
        #define NAND_D6 0x1d61
        #define EMMC_D6 0x1d62
        #define SPI0_CS0 0x1d63

        #define GPIO1_D7 0x1d70
        #define NAND_D7 0x1d71
        #define EMMC_D7 0x1d72
        #define SPI0_CS1 0x1d73


        /* GPIO2_A */
        #define GPIO2_A0 0x2a00
        #define NAND_ALE 0x2a01
        #define SPI0_CLK 0x2a02

        #define GPIO2_A1 0x2a10
        #define NAND_CLE 0x2a11
        #define EMMC_CLKOUT 0x2a12

        #define GPIO2_A2 0x2a20
        #define NAND_WRN 0x2a21
        #define SFC_CSN0 0x2a22

        #define GPIO2_A3 0x2a30
        #define NAND_RDN 0x2a31
        #define SFC_CSN1 0x2a32

        #define GPIO2_A4 0x2a40
        #define NAND_RDY 0x2a41
        #define EMMC_CMD 0x2a42
        #define SFC_CLK 0x2a43

        #define GPIO2_A6 0x2a60
        #define NAND_CS0 0x2a61

        #define GPIO2_A7 0x2a70
        #define TESTCLK_OUT 0x2a71


        /* GPIO2_B */
        #define GPIO2_B2 0x2b20
        #define MAC_CRS 0x2b21

        #define GPIO2_B4 0x2b40
        #define MAC_MDIO 0x2b41

        #define GPIO2_B5 0x2b50
        #define MAC_TXEN 0x2b51

        #define GPIO2_B6 0x2b60
        #define MAC_CLKOUT 0x2b61
        #define MAC_CLKIN 0x2b62

        #define GPIO2_B7 0x2b70
        #define MAC_RXER 0x2b71


        /* GPIO2_C */
        #define GPIO2_C0 0x2c00
        #define MAC_RXD1 0x2c01

        #define GPIO2_C1 0x2c10
        #define MAC_RXD0 0x2c11

        #define GPIO2_C2 0x2c20
        #define MAC_TXD1 0x2c21

        #define GPIO2_C3 0x2c30
        #define MAC_TXD0 0x2c31

        #define GPIO2_C4 0x2c40
        #define I2C2_SDA 0x2c41

        #define GPIO2_C5 0x2c50
        #define I2C2_SCL 0x2c51

        #define GPIO2_C6 0x2c60
        #define UART1_SIN 0x2c61

        #define GPIO2_C7 0x2c70
        #define UART1_SOUT 0x2c71
        #define TESTCLK_OUT1 0x2c72


        /* GPIO2_D */
        #define GPIO2_D1 0x2d10
        #define MAC_MDC 0x2d11

        #define GPIO2_D4 0x2d40
        #define I2S0_SDO3 0x2d41

        #define GPIO2_D5 0x2d50
        #define I2S0_SDO2 0x2d51

        #define GPIO2_D6 0x2d60
        #define I2S0_SDO1 0x2d61


#endif
