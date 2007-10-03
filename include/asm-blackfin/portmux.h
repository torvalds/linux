/*
 * Common header file for blackfin family of processors.
 *
 */

#ifndef _PORTMUX_H_
#define _PORTMUX_H_

#define P_IDENT(x)	((x) & 0x1FF)
#define P_FUNCT(x)	(((x) & 0x3) << 9)
#define P_FUNCT2MUX(x)	(((x) >> 9) & 0x3)
#define P_DEFINED	0x8000
#define P_UNDEF		0x4000
#define P_MAYSHARE	0x2000
#define P_DONTCARE	0x1000


int peripheral_request(unsigned short per, const char *label);
void peripheral_free(unsigned short per);
int peripheral_request_list(unsigned short per[], const char *label);
void peripheral_free_list(unsigned short per[]);

#include <asm/gpio.h>
#include <asm/mach/portmux.h>

#ifndef P_SPORT2_TFS
#define P_SPORT2_TFS P_UNDEF
#endif

#ifndef P_SPORT2_DTSEC
#define P_SPORT2_DTSEC P_UNDEF
#endif

#ifndef P_SPORT2_DTPRI
#define P_SPORT2_DTPRI P_UNDEF
#endif

#ifndef P_SPORT2_TSCLK
#define P_SPORT2_TSCLK P_UNDEF
#endif

#ifndef P_SPORT2_RFS
#define P_SPORT2_RFS P_UNDEF
#endif

#ifndef P_SPORT2_DRSEC
#define P_SPORT2_DRSEC P_UNDEF
#endif

#ifndef P_SPORT2_DRPRI
#define P_SPORT2_DRPRI P_UNDEF
#endif

#ifndef P_SPORT2_RSCLK
#define P_SPORT2_RSCLK P_UNDEF
#endif

#ifndef P_SPORT3_TFS
#define P_SPORT3_TFS P_UNDEF
#endif

#ifndef P_SPORT3_DTSEC
#define P_SPORT3_DTSEC P_UNDEF
#endif

#ifndef P_SPORT3_DTPRI
#define P_SPORT3_DTPRI P_UNDEF
#endif

#ifndef P_SPORT3_TSCLK
#define P_SPORT3_TSCLK P_UNDEF
#endif

#ifndef P_SPORT3_RFS
#define P_SPORT3_RFS P_UNDEF
#endif

#ifndef P_SPORT3_DRSEC
#define P_SPORT3_DRSEC P_UNDEF
#endif

#ifndef P_SPORT3_DRPRI
#define P_SPORT3_DRPRI P_UNDEF
#endif

#ifndef P_SPORT3_RSCLK
#define P_SPORT3_RSCLK P_UNDEF
#endif

#ifndef P_TMR4
#define P_TMR4 P_UNDEF
#endif

#ifndef P_TMR5
#define P_TMR5 P_UNDEF
#endif

#ifndef P_TMR6
#define P_TMR6 P_UNDEF
#endif

#ifndef P_TMR7
#define P_TMR7 P_UNDEF
#endif

#ifndef P_TWI1_SCL
#define P_TWI1_SCL P_UNDEF
#endif

#ifndef P_TWI1_SDA
#define P_TWI1_SDA P_UNDEF
#endif

#ifndef P_UART3_RTS
#define P_UART3_RTS P_UNDEF
#endif

#ifndef P_UART3_CTS
#define P_UART3_CTS P_UNDEF
#endif

#ifndef P_UART2_TX
#define P_UART2_TX P_UNDEF
#endif

#ifndef P_UART2_RX
#define P_UART2_RX P_UNDEF
#endif

#ifndef P_UART3_TX
#define P_UART3_TX P_UNDEF
#endif

#ifndef P_UART3_RX
#define P_UART3_RX P_UNDEF
#endif

#ifndef P_SPI2_SS
#define P_SPI2_SS P_UNDEF
#endif

#ifndef P_SPI2_SSEL1
#define P_SPI2_SSEL1 P_UNDEF
#endif

#ifndef P_SPI2_SSEL2
#define P_SPI2_SSEL2 P_UNDEF
#endif

#ifndef P_SPI2_SSEL3
#define P_SPI2_SSEL3 P_UNDEF
#endif

#ifndef P_SPI2_SSEL4
#define P_SPI2_SSEL4 P_UNDEF
#endif

#ifndef P_SPI2_SSEL5
#define P_SPI2_SSEL5 P_UNDEF
#endif

#ifndef P_SPI2_SSEL6
#define P_SPI2_SSEL6 P_UNDEF
#endif

#ifndef P_SPI2_SSEL7
#define P_SPI2_SSEL7 P_UNDEF
#endif

#ifndef P_SPI2_SCK
#define P_SPI2_SCK P_UNDEF
#endif

#ifndef P_SPI2_MOSI
#define P_SPI2_MOSI P_UNDEF
#endif

#ifndef P_SPI2_MISO
#define P_SPI2_MISO P_UNDEF
#endif

#ifndef P_TMR0
#define P_TMR0 P_UNDEF
#endif

#ifndef P_TMR1
#define P_TMR1 P_UNDEF
#endif

#ifndef P_TMR2
#define P_TMR2 P_UNDEF
#endif

#ifndef P_TMR3
#define P_TMR3 P_UNDEF
#endif

#ifndef P_SPORT0_TFS
#define P_SPORT0_TFS P_UNDEF
#endif

#ifndef P_SPORT0_DTSEC
#define P_SPORT0_DTSEC P_UNDEF
#endif

#ifndef P_SPORT0_DTPRI
#define P_SPORT0_DTPRI P_UNDEF
#endif

#ifndef P_SPORT0_TSCLK
#define P_SPORT0_TSCLK P_UNDEF
#endif

#ifndef P_SPORT0_RFS
#define P_SPORT0_RFS P_UNDEF
#endif

#ifndef P_SPORT0_DRSEC
#define P_SPORT0_DRSEC P_UNDEF
#endif

#ifndef P_SPORT0_DRPRI
#define P_SPORT0_DRPRI P_UNDEF
#endif

#ifndef P_SPORT0_RSCLK
#define P_SPORT0_RSCLK P_UNDEF
#endif

#ifndef P_SD_D0
#define P_SD_D0 P_UNDEF
#endif

#ifndef P_SD_D1
#define P_SD_D1 P_UNDEF
#endif

#ifndef P_SD_D2
#define P_SD_D2 P_UNDEF
#endif

#ifndef P_SD_D3
#define P_SD_D3 P_UNDEF
#endif

#ifndef P_SD_CLK
#define P_SD_CLK P_UNDEF
#endif

#ifndef P_SD_CMD
#define P_SD_CMD P_UNDEF
#endif

#ifndef P_MMCLK
#define P_MMCLK P_UNDEF
#endif

#ifndef P_MBCLK
#define P_MBCLK P_UNDEF
#endif

#ifndef P_PPI1_D0
#define P_PPI1_D0 P_UNDEF
#endif

#ifndef P_PPI1_D1
#define P_PPI1_D1 P_UNDEF
#endif

#ifndef P_PPI1_D2
#define P_PPI1_D2 P_UNDEF
#endif

#ifndef P_PPI1_D3
#define P_PPI1_D3 P_UNDEF
#endif

#ifndef P_PPI1_D4
#define P_PPI1_D4 P_UNDEF
#endif

#ifndef P_PPI1_D5
#define P_PPI1_D5 P_UNDEF
#endif

#ifndef P_PPI1_D6
#define P_PPI1_D6 P_UNDEF
#endif

#ifndef P_PPI1_D7
#define P_PPI1_D7 P_UNDEF
#endif

#ifndef P_PPI1_D8
#define P_PPI1_D8 P_UNDEF
#endif

#ifndef P_PPI1_D9
#define P_PPI1_D9 P_UNDEF
#endif

#ifndef P_PPI1_D10
#define P_PPI1_D10 P_UNDEF
#endif

#ifndef P_PPI1_D11
#define P_PPI1_D11 P_UNDEF
#endif

#ifndef P_PPI1_D12
#define P_PPI1_D12 P_UNDEF
#endif

#ifndef P_PPI1_D13
#define P_PPI1_D13 P_UNDEF
#endif

#ifndef P_PPI1_D14
#define P_PPI1_D14 P_UNDEF
#endif

#ifndef P_PPI1_D15
#define P_PPI1_D15 P_UNDEF
#endif

#ifndef P_HOST_D8
#define P_HOST_D8 P_UNDEF
#endif

#ifndef P_HOST_D9
#define P_HOST_D9 P_UNDEF
#endif

#ifndef P_HOST_D10
#define P_HOST_D10 P_UNDEF
#endif

#ifndef P_HOST_D11
#define P_HOST_D11 P_UNDEF
#endif

#ifndef P_HOST_D12
#define P_HOST_D12 P_UNDEF
#endif

#ifndef P_HOST_D13
#define P_HOST_D13 P_UNDEF
#endif

#ifndef P_HOST_D14
#define P_HOST_D14 P_UNDEF
#endif

#ifndef P_HOST_D15
#define P_HOST_D15 P_UNDEF
#endif

#ifndef P_HOST_D0
#define P_HOST_D0 P_UNDEF
#endif

#ifndef P_HOST_D1
#define P_HOST_D1 P_UNDEF
#endif

#ifndef P_HOST_D2
#define P_HOST_D2 P_UNDEF
#endif

#ifndef P_HOST_D3
#define P_HOST_D3 P_UNDEF
#endif

#ifndef P_HOST_D4
#define P_HOST_D4 P_UNDEF
#endif

#ifndef P_HOST_D5
#define P_HOST_D5 P_UNDEF
#endif

#ifndef P_HOST_D6
#define P_HOST_D6 P_UNDEF
#endif

#ifndef P_HOST_D7
#define P_HOST_D7 P_UNDEF
#endif

#ifndef P_SPORT1_TFS
#define P_SPORT1_TFS P_UNDEF
#endif

#ifndef P_SPORT1_DTSEC
#define P_SPORT1_DTSEC P_UNDEF
#endif

#ifndef P_SPORT1_DTPRI
#define P_SPORT1_DTPRI P_UNDEF
#endif

#ifndef P_SPORT1_TSCLK
#define P_SPORT1_TSCLK P_UNDEF
#endif

#ifndef P_SPORT1_RFS
#define P_SPORT1_RFS P_UNDEF
#endif

#ifndef P_SPORT1_DRSEC
#define P_SPORT1_DRSEC P_UNDEF
#endif

#ifndef P_SPORT1_DRPRI
#define P_SPORT1_DRPRI P_UNDEF
#endif

#ifndef P_SPORT1_RSCLK
#define P_SPORT1_RSCLK P_UNDEF
#endif

#ifndef P_PPI2_D0
#define P_PPI2_D0 P_UNDEF
#endif

#ifndef P_PPI2_D1
#define P_PPI2_D1 P_UNDEF
#endif

#ifndef P_PPI2_D2
#define P_PPI2_D2 P_UNDEF
#endif

#ifndef P_PPI2_D3
#define P_PPI2_D3 P_UNDEF
#endif

#ifndef P_PPI2_D4
#define P_PPI2_D4 P_UNDEF
#endif

#ifndef P_PPI2_D5
#define P_PPI2_D5 P_UNDEF
#endif

#ifndef P_PPI2_D6
#define P_PPI2_D6 P_UNDEF
#endif

#ifndef P_PPI2_D7
#define P_PPI2_D7 P_UNDEF
#endif

#ifndef P_PPI0_D18
#define P_PPI0_D18 P_UNDEF
#endif

#ifndef P_PPI0_D19
#define P_PPI0_D19 P_UNDEF
#endif

#ifndef P_PPI0_D20
#define P_PPI0_D20 P_UNDEF
#endif

#ifndef P_PPI0_D21
#define P_PPI0_D21 P_UNDEF
#endif

#ifndef P_PPI0_D22
#define P_PPI0_D22 P_UNDEF
#endif

#ifndef P_PPI0_D23
#define P_PPI0_D23 P_UNDEF
#endif

#ifndef P_KEY_ROW0
#define P_KEY_ROW0 P_UNDEF
#endif

#ifndef P_KEY_ROW1
#define P_KEY_ROW1 P_UNDEF
#endif

#ifndef P_KEY_ROW2
#define P_KEY_ROW2 P_UNDEF
#endif

#ifndef P_KEY_ROW3
#define P_KEY_ROW3 P_UNDEF
#endif

#ifndef P_KEY_COL0
#define P_KEY_COL0 P_UNDEF
#endif

#ifndef P_KEY_COL1
#define P_KEY_COL1 P_UNDEF
#endif

#ifndef P_KEY_COL2
#define P_KEY_COL2 P_UNDEF
#endif

#ifndef P_KEY_COL3
#define P_KEY_COL3 P_UNDEF
#endif

#ifndef P_SPI0_SCK
#define P_SPI0_SCK P_UNDEF
#endif

#ifndef P_SPI0_MISO
#define P_SPI0_MISO P_UNDEF
#endif

#ifndef P_SPI0_MOSI
#define P_SPI0_MOSI P_UNDEF
#endif

#ifndef P_SPI0_SS
#define P_SPI0_SS P_UNDEF
#endif

#ifndef P_SPI0_SSEL1
#define P_SPI0_SSEL1 P_UNDEF
#endif

#ifndef P_SPI0_SSEL2
#define P_SPI0_SSEL2 P_UNDEF
#endif

#ifndef P_SPI0_SSEL3
#define P_SPI0_SSEL3 P_UNDEF
#endif

#ifndef P_SPI0_SSEL4
#define P_SPI0_SSEL4 P_UNDEF
#endif

#ifndef P_SPI0_SSEL5
#define P_SPI0_SSEL5 P_UNDEF
#endif

#ifndef P_SPI0_SSEL6
#define P_SPI0_SSEL6 P_UNDEF
#endif

#ifndef P_SPI0_SSEL7
#define P_SPI0_SSEL7 P_UNDEF
#endif

#ifndef P_UART0_TX
#define P_UART0_TX P_UNDEF
#endif

#ifndef P_UART0_RX
#define P_UART0_RX P_UNDEF
#endif

#ifndef P_UART1_RTS
#define P_UART1_RTS P_UNDEF
#endif

#ifndef P_UART1_CTS
#define P_UART1_CTS P_UNDEF
#endif

#ifndef P_PPI1_CLK
#define P_PPI1_CLK P_UNDEF
#endif

#ifndef P_PPI1_FS1
#define P_PPI1_FS1 P_UNDEF
#endif

#ifndef P_PPI1_FS2
#define P_PPI1_FS2 P_UNDEF
#endif

#ifndef P_TWI0_SCL
#define P_TWI0_SCL P_UNDEF
#endif

#ifndef P_TWI0_SDA
#define P_TWI0_SDA P_UNDEF
#endif

#ifndef P_KEY_COL7
#define P_KEY_COL7 P_UNDEF
#endif

#ifndef P_KEY_ROW6
#define P_KEY_ROW6 P_UNDEF
#endif

#ifndef P_KEY_COL6
#define P_KEY_COL6 P_UNDEF
#endif

#ifndef P_KEY_ROW5
#define P_KEY_ROW5 P_UNDEF
#endif

#ifndef P_KEY_COL5
#define P_KEY_COL5 P_UNDEF
#endif

#ifndef P_KEY_ROW4
#define P_KEY_ROW4 P_UNDEF
#endif

#ifndef P_KEY_COL4
#define P_KEY_COL4 P_UNDEF
#endif

#ifndef P_KEY_ROW7
#define P_KEY_ROW7 P_UNDEF
#endif

#ifndef P_PPI0_D0
#define P_PPI0_D0 P_UNDEF
#endif

#ifndef P_PPI0_D1
#define P_PPI0_D1 P_UNDEF
#endif

#ifndef P_PPI0_D2
#define P_PPI0_D2 P_UNDEF
#endif

#ifndef P_PPI0_D3
#define P_PPI0_D3 P_UNDEF
#endif

#ifndef P_PPI0_D4
#define P_PPI0_D4 P_UNDEF
#endif

#ifndef P_PPI0_D5
#define P_PPI0_D5 P_UNDEF
#endif

#ifndef P_PPI0_D6
#define P_PPI0_D6 P_UNDEF
#endif

#ifndef P_PPI0_D7
#define P_PPI0_D7 P_UNDEF
#endif

#ifndef P_PPI0_D8
#define P_PPI0_D8 P_UNDEF
#endif

#ifndef P_PPI0_D9
#define P_PPI0_D9 P_UNDEF
#endif

#ifndef P_PPI0_D10
#define P_PPI0_D10 P_UNDEF
#endif

#ifndef P_PPI0_D11
#define P_PPI0_D11 P_UNDEF
#endif

#ifndef P_PPI0_D12
#define P_PPI0_D12 P_UNDEF
#endif

#ifndef P_PPI0_D13
#define P_PPI0_D13 P_UNDEF
#endif

#ifndef P_PPI0_D14
#define P_PPI0_D14 P_UNDEF
#endif

#ifndef P_PPI0_D15
#define P_PPI0_D15 P_UNDEF
#endif

#ifndef P_ATAPI_D0A
#define P_ATAPI_D0A P_UNDEF
#endif

#ifndef P_ATAPI_D1A
#define P_ATAPI_D1A P_UNDEF
#endif

#ifndef P_ATAPI_D2A
#define P_ATAPI_D2A P_UNDEF
#endif

#ifndef P_ATAPI_D3A
#define P_ATAPI_D3A P_UNDEF
#endif

#ifndef P_ATAPI_D4A
#define P_ATAPI_D4A P_UNDEF
#endif

#ifndef P_ATAPI_D5A
#define P_ATAPI_D5A P_UNDEF
#endif

#ifndef P_ATAPI_D6A
#define P_ATAPI_D6A P_UNDEF
#endif

#ifndef P_ATAPI_D7A
#define P_ATAPI_D7A P_UNDEF
#endif

#ifndef P_ATAPI_D8A
#define P_ATAPI_D8A P_UNDEF
#endif

#ifndef P_ATAPI_D9A
#define P_ATAPI_D9A P_UNDEF
#endif

#ifndef P_ATAPI_D10A
#define P_ATAPI_D10A P_UNDEF
#endif

#ifndef P_ATAPI_D11A
#define P_ATAPI_D11A P_UNDEF
#endif

#ifndef P_ATAPI_D12A
#define P_ATAPI_D12A P_UNDEF
#endif

#ifndef P_ATAPI_D13A
#define P_ATAPI_D13A P_UNDEF
#endif

#ifndef P_ATAPI_D14A
#define P_ATAPI_D14A P_UNDEF
#endif

#ifndef P_ATAPI_D15A
#define P_ATAPI_D15A P_UNDEF
#endif

#ifndef P_PPI0_CLK
#define P_PPI0_CLK P_UNDEF
#endif

#ifndef P_PPI0_FS1
#define P_PPI0_FS1 P_UNDEF
#endif

#ifndef P_PPI0_FS2
#define P_PPI0_FS2 P_UNDEF
#endif

#ifndef P_PPI0_D16
#define P_PPI0_D16 P_UNDEF
#endif

#ifndef P_PPI0_D17
#define P_PPI0_D17 P_UNDEF
#endif

#ifndef P_SPI1_SSEL1
#define P_SPI1_SSEL1 P_UNDEF
#endif

#ifndef P_SPI1_SSEL2
#define P_SPI1_SSEL2 P_UNDEF
#endif

#ifndef P_SPI1_SSEL3
#define P_SPI1_SSEL3 P_UNDEF
#endif


#ifndef P_SPI1_SSEL4
#define P_SPI1_SSEL4 P_UNDEF
#endif

#ifndef P_SPI1_SSEL5
#define P_SPI1_SSEL5 P_UNDEF
#endif

#ifndef P_SPI1_SSEL6
#define P_SPI1_SSEL6 P_UNDEF
#endif

#ifndef P_SPI1_SSEL7
#define P_SPI1_SSEL7 P_UNDEF
#endif

#ifndef P_SPI1_SCK
#define P_SPI1_SCK P_UNDEF
#endif

#ifndef P_SPI1_MISO
#define P_SPI1_MISO P_UNDEF
#endif

#ifndef P_SPI1_MOSI
#define P_SPI1_MOSI P_UNDEF
#endif

#ifndef P_SPI1_SS
#define P_SPI1_SS P_UNDEF
#endif

#ifndef P_CAN0_TX
#define P_CAN0_TX P_UNDEF
#endif

#ifndef P_CAN0_RX
#define P_CAN0_RX P_UNDEF
#endif

#ifndef P_CAN1_TX
#define P_CAN1_TX P_UNDEF
#endif

#ifndef P_CAN1_RX
#define P_CAN1_RX P_UNDEF
#endif

#ifndef P_ATAPI_A0A
#define P_ATAPI_A0A P_UNDEF
#endif

#ifndef P_ATAPI_A1A
#define P_ATAPI_A1A P_UNDEF
#endif

#ifndef P_ATAPI_A2A
#define P_ATAPI_A2A P_UNDEF
#endif

#ifndef P_HOST_CE
#define P_HOST_CE P_UNDEF
#endif

#ifndef P_HOST_RD
#define P_HOST_RD P_UNDEF
#endif

#ifndef P_HOST_WR
#define P_HOST_WR P_UNDEF
#endif

#ifndef P_MTXONB
#define P_MTXONB P_UNDEF
#endif

#ifndef P_PPI2_FS2
#define P_PPI2_FS2 P_UNDEF
#endif

#ifndef P_PPI2_FS1
#define P_PPI2_FS1 P_UNDEF
#endif

#ifndef P_PPI2_CLK
#define P_PPI2_CLK P_UNDEF
#endif

#ifndef P_CNT_CZM
#define P_CNT_CZM P_UNDEF
#endif

#ifndef P_UART1_TX
#define P_UART1_TX P_UNDEF
#endif

#ifndef P_UART1_RX
#define P_UART1_RX P_UNDEF
#endif

#ifndef P_ATAPI_RESET
#define P_ATAPI_RESET P_UNDEF
#endif

#ifndef P_HOST_ADDR
#define P_HOST_ADDR P_UNDEF
#endif

#ifndef P_HOST_ACK
#define P_HOST_ACK P_UNDEF
#endif

#ifndef P_MTX
#define P_MTX P_UNDEF
#endif

#ifndef P_MRX
#define P_MRX P_UNDEF
#endif

#ifndef P_MRXONB
#define P_MRXONB P_UNDEF
#endif

#ifndef P_A4
#define P_A4 P_UNDEF
#endif

#ifndef P_A5
#define P_A5 P_UNDEF
#endif

#ifndef P_A6
#define P_A6 P_UNDEF
#endif

#ifndef P_A7
#define P_A7 P_UNDEF
#endif

#ifndef P_A8
#define P_A8 P_UNDEF
#endif

#ifndef P_A9
#define P_A9 P_UNDEF
#endif

#ifndef P_PPI1_FS3
#define P_PPI1_FS3 P_UNDEF
#endif

#ifndef P_PPI2_FS3
#define P_PPI2_FS3 P_UNDEF
#endif

#ifndef P_TMR8
#define P_TMR8 P_UNDEF
#endif

#ifndef P_TMR9
#define P_TMR9 P_UNDEF
#endif

#ifndef P_TMR10
#define P_TMR10 P_UNDEF
#endif
#ifndef P_TMR11
#define P_TMR11 P_UNDEF
#endif

#ifndef P_DMAR0
#define P_DMAR0 P_UNDEF
#endif

#ifndef P_DMAR1
#define P_DMAR1 P_UNDEF
#endif

#ifndef P_PPI0_FS3
#define P_PPI0_FS3 P_UNDEF
#endif

#ifndef P_CNT_CDG
#define P_CNT_CDG P_UNDEF
#endif

#ifndef P_CNT_CUD
#define P_CNT_CUD P_UNDEF
#endif

#ifndef P_A10
#define P_A10 P_UNDEF
#endif

#ifndef P_A11
#define P_A11 P_UNDEF
#endif

#ifndef P_A12
#define P_A12 P_UNDEF
#endif

#ifndef P_A13
#define P_A13 P_UNDEF
#endif

#ifndef P_A14
#define P_A14 P_UNDEF
#endif

#ifndef P_A15
#define P_A15 P_UNDEF
#endif

#ifndef P_A16
#define P_A16 P_UNDEF
#endif

#ifndef P_A17
#define P_A17 P_UNDEF
#endif

#ifndef P_A18
#define P_A18 P_UNDEF
#endif

#ifndef P_A19
#define P_A19 P_UNDEF
#endif

#ifndef P_A20
#define P_A20 P_UNDEF
#endif

#ifndef P_A21
#define P_A21 P_UNDEF
#endif

#ifndef P_A22
#define P_A22 P_UNDEF
#endif

#ifndef P_A23
#define P_A23 P_UNDEF
#endif

#ifndef P_A24
#define P_A24 P_UNDEF
#endif

#ifndef P_A25
#define P_A25 P_UNDEF
#endif

#ifndef P_NOR_CLK
#define P_NOR_CLK P_UNDEF
#endif

#ifndef  P_TMRCLK
#define  P_TMRCLK P_UNDEF
#endif

#ifndef P_AMC_ARDY_NOR_WAIT
#define P_AMC_ARDY_NOR_WAIT P_UNDEF
#endif

#ifndef P_NAND_CE
#define P_NAND_CE P_UNDEF
#endif

#ifndef P_NAND_RB
#define P_NAND_RB P_UNDEF
#endif

#ifndef P_ATAPI_DIOR
#define P_ATAPI_DIOR P_UNDEF
#endif

#ifndef P_ATAPI_DIOW
#define P_ATAPI_DIOW P_UNDEF
#endif

#ifndef P_ATAPI_CS0
#define P_ATAPI_CS0 P_UNDEF
#endif

#ifndef P_ATAPI_CS1
#define P_ATAPI_CS1 P_UNDEF
#endif

#ifndef P_ATAPI_DMACK
#define P_ATAPI_DMACK P_UNDEF
#endif

#ifndef P_ATAPI_DMARQ
#define P_ATAPI_DMARQ P_UNDEF
#endif

#ifndef P_ATAPI_INTRQ
#define P_ATAPI_INTRQ P_UNDEF
#endif

#ifndef P_ATAPI_IORDY
#define P_ATAPI_IORDY P_UNDEF
#endif

#ifndef P_AMC_BR
#define P_AMC_BR P_UNDEF
#endif

#ifndef P_AMC_BG
#define P_AMC_BG P_UNDEF
#endif

#ifndef P_AMC_BGH
#define P_AMC_BGH P_UNDEF
#endif

/* EMAC */

#ifndef P_MII0_ETxD0
#define P_MII0_ETxD0 P_UNDEF
#endif

#ifndef P_MII0_ETxD1
#define P_MII0_ETxD1 P_UNDEF
#endif

#ifndef P_MII0_ETxD2
#define P_MII0_ETxD2 P_UNDEF
#endif

#ifndef P_MII0_ETxD3
#define P_MII0_ETxD3 P_UNDEF
#endif

#ifndef P_MII0_ETxEN
#define P_MII0_ETxEN P_UNDEF
#endif

#ifndef P_MII0_TxCLK
#define P_MII0_TxCLK P_UNDEF
#endif

#ifndef P_MII0_PHYINT
#define P_MII0_PHYINT P_UNDEF
#endif

#ifndef P_MII0_COL
#define P_MII0_COL P_UNDEF
#endif

#ifndef P_MII0_ERxD0
#define P_MII0_ERxD0 P_UNDEF
#endif

#ifndef P_MII0_ERxD1
#define P_MII0_ERxD1 P_UNDEF
#endif

#ifndef P_MII0_ERxD2
#define P_MII0_ERxD2 P_UNDEF
#endif

#ifndef P_MII0_ERxD3
#define P_MII0_ERxD3 P_UNDEF
#endif

#ifndef P_MII0_ERxDV
#define P_MII0_ERxDV P_UNDEF
#endif

#ifndef P_MII0_ERxCLK
#define P_MII0_ERxCLK P_UNDEF
#endif

#ifndef P_MII0_ERxER
#define P_MII0_ERxER P_UNDEF
#endif

#ifndef P_MII0_CRS
#define P_MII0_CRS P_UNDEF
#endif

#ifndef P_RMII0_REF_CLK
#define P_RMII0_REF_CLK P_UNDEF
#endif

#ifndef P_RMII0_MDINT
#define P_RMII0_MDINT P_UNDEF
#endif

#ifndef P_RMII0_CRS_DV
#define P_RMII0_CRS_DV P_UNDEF
#endif

#ifndef P_MDC
#define P_MDC P_UNDEF
#endif

#ifndef P_MDIO
#define P_MDIO P_UNDEF
#endif

#endif				/* _PORTMUX_H_ */
