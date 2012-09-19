#ifndef __IMX_AUDMUX_H
#define __IMX_AUDMUX_H

#define MX27_AUDMUX_HPCR1_SSI0		0
#define MX27_AUDMUX_HPCR2_SSI1		1
#define MX27_AUDMUX_HPCR3_SSI_PINS_4	2
#define MX27_AUDMUX_PPCR1_SSI_PINS_1	3
#define MX27_AUDMUX_PPCR2_SSI_PINS_2	4
#define MX27_AUDMUX_PPCR3_SSI_PINS_3	5

#define MX31_AUDMUX_PORT1_SSI0		0
#define MX31_AUDMUX_PORT2_SSI1		1
#define MX31_AUDMUX_PORT3_SSI_PINS_3	2
#define MX31_AUDMUX_PORT4_SSI_PINS_4	3
#define MX31_AUDMUX_PORT5_SSI_PINS_5	4
#define MX31_AUDMUX_PORT6_SSI_PINS_6	5
#define MX31_AUDMUX_PORT7_SSI_PINS_7	6

#define MX51_AUDMUX_PORT1_SSI0		0
#define MX51_AUDMUX_PORT2_SSI1		1
#define MX51_AUDMUX_PORT3		2
#define MX51_AUDMUX_PORT4		3
#define MX51_AUDMUX_PORT5		4
#define MX51_AUDMUX_PORT6		5
#define MX51_AUDMUX_PORT7		6

/* Register definitions for the i.MX21/27 Digital Audio Multiplexer */
#define IMX_AUDMUX_V1_PCR_INMMASK(x)	((x) & 0xff)
#define IMX_AUDMUX_V1_PCR_INMEN		(1 << 8)
#define IMX_AUDMUX_V1_PCR_TXRXEN	(1 << 10)
#define IMX_AUDMUX_V1_PCR_SYN		(1 << 12)
#define IMX_AUDMUX_V1_PCR_RXDSEL(x)	(((x) & 0x7) << 13)
#define IMX_AUDMUX_V1_PCR_RFCSEL(x)	(((x) & 0xf) << 20)
#define IMX_AUDMUX_V1_PCR_RCLKDIR	(1 << 24)
#define IMX_AUDMUX_V1_PCR_RFSDIR	(1 << 25)
#define IMX_AUDMUX_V1_PCR_TFCSEL(x)	(((x) & 0xf) << 26)
#define IMX_AUDMUX_V1_PCR_TCLKDIR	(1 << 30)
#define IMX_AUDMUX_V1_PCR_TFSDIR	(1 << 31)

/* Register definitions for the i.MX25/31/35/51 Digital Audio Multiplexer */
#define IMX_AUDMUX_V2_PTCR_TFSDIR	(1 << 31)
#define IMX_AUDMUX_V2_PTCR_TFSEL(x)	(((x) & 0xf) << 27)
#define IMX_AUDMUX_V2_PTCR_TCLKDIR	(1 << 26)
#define IMX_AUDMUX_V2_PTCR_TCSEL(x)	(((x) & 0xf) << 22)
#define IMX_AUDMUX_V2_PTCR_RFSDIR	(1 << 21)
#define IMX_AUDMUX_V2_PTCR_RFSEL(x)	(((x) & 0xf) << 17)
#define IMX_AUDMUX_V2_PTCR_RCLKDIR	(1 << 16)
#define IMX_AUDMUX_V2_PTCR_RCSEL(x)	(((x) & 0xf) << 12)
#define IMX_AUDMUX_V2_PTCR_SYN		(1 << 11)

#define IMX_AUDMUX_V2_PDCR_RXDSEL(x)	(((x) & 0x7) << 13)
#define IMX_AUDMUX_V2_PDCR_TXRXEN	(1 << 12)
#define IMX_AUDMUX_V2_PDCR_MODE(x)	(((x) & 0x3) << 8)
#define IMX_AUDMUX_V2_PDCR_INMMASK(x)	((x) & 0xff)

int imx_audmux_v1_configure_port(unsigned int port, unsigned int pcr);

int imx_audmux_v2_configure_port(unsigned int port, unsigned int ptcr,
		unsigned int pdcr);

#endif /* __IMX_AUDMUX_H */
