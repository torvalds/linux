/************************************************************
*
* Copyright (C) 2004, Analog Devices. All Rights Reserved
*
* FILE bfin5xx_spi.h
* PROGRAMMER(S): Luke Yang (Analog Devices Inc.)
*
*
* DATE OF CREATION: March. 10th 2006
*
* SYNOPSIS:
*
* DESCRIPTION: header file for SPI controller driver for Blackfin5xx.
**************************************************************

* MODIFICATION HISTORY:
* March 10, 2006  bfin5xx_spi.h Created. (Luke Yang)

************************************************************/

#ifndef _SPI_CHANNEL_H_
#define _SPI_CHANNEL_H_

#define SPI_READ              0
#define SPI_WRITE             1

#define SPI_CTRL_OFF            0x0
#define SPI_FLAG_OFF            0x4
#define SPI_STAT_OFF            0x8
#define SPI_TXBUFF_OFF          0xc
#define SPI_RXBUFF_OFF          0x10
#define SPI_BAUD_OFF            0x14
#define SPI_SHAW_OFF            0x18

#define CMD_SPI_OUT_ENABLE    1
#define CMD_SPI_SET_BAUDRATE  2
#define CMD_SPI_SET_POLAR     3
#define CMD_SPI_SET_PHASE     4
#define CMD_SPI_SET_MASTER    5
#define CMD_SPI_SET_SENDOPT   6
#define CMD_SPI_SET_RECVOPT   7
#define CMD_SPI_SET_ORDER     8
#define CMD_SPI_SET_LENGTH16  9
#define CMD_SPI_GET_STAT      11
#define CMD_SPI_GET_CFG       12
#define CMD_SPI_SET_CSAVAIL   13
#define CMD_SPI_SET_CSHIGH    14	/* CS unavail */
#define CMD_SPI_SET_CSLOW     15	/* CS avail */
#define CMD_SPI_MISO_ENABLE   16
#define CMD_SPI_SET_CSENABLE  17
#define CMD_SPI_SET_CSDISABLE 18

#define CMD_SPI_SET_TRIGGER_MODE  19
#define CMD_SPI_SET_TRIGGER_SENSE 20
#define CMD_SPI_SET_TRIGGER_EDGE  21
#define CMD_SPI_SET_TRIGGER_LEVEL 22

#define CMD_SPI_SET_TIME_SPS 	  23
#define CMD_SPI_SET_TIME_SAMPLES  24
#define CMD_SPI_GET_SYSTEMCLOCK   25

#define CMD_SPI_SET_WRITECONTINUOUS     26
#define CMD_SPI_SET_SKFS    		27

#define CMD_SPI_GET_ALLCONFIG 32	/* For debug */

#define SPI_DEFAULT_BARD    0x0100

#define SPI0_IRQ_NUM        IRQ_SPI
#define SPI_ERR_TRIG	   -1

#define BIT_CTL_ENABLE      0x4000
#define BIT_CTL_OPENDRAIN   0x2000
#define BIT_CTL_MASTER      0x1000
#define BIT_CTL_POLAR       0x0800
#define BIT_CTL_PHASE       0x0400
#define BIT_CTL_BITORDER    0x0200
#define BIT_CTL_WORDSIZE    0x0100
#define BIT_CTL_MISOENABLE  0x0020
#define BIT_CTL_RXMOD       0x0000
#define BIT_CTL_TXMOD       0x0001
#define BIT_CTL_TIMOD_DMA_TX 0x0003
#define BIT_CTL_TIMOD_DMA_RX 0x0002
#define BIT_CTL_SENDOPT     0x0004
#define BIT_CTL_TIMOD       0x0003

#define BIT_STAT_SPIF       0x0001
#define BIT_STAT_MODF       0x0002
#define BIT_STAT_TXE        0x0004
#define BIT_STAT_TXS        0x0008
#define BIT_STAT_RBSY       0x0010
#define BIT_STAT_RXS        0x0020
#define BIT_STAT_TXCOL      0x0040
#define BIT_STAT_CLR        0xFFFF

#define BIT_STU_SENDOVER    0x0001
#define BIT_STU_RECVFULL    0x0020

#define CFG_SPI_ENABLE      1
#define CFG_SPI_DISABLE     0

#define CFG_SPI_OUTENABLE   1
#define CFG_SPI_OUTDISABLE  0

#define CFG_SPI_ACTLOW      1
#define CFG_SPI_ACTHIGH     0

#define CFG_SPI_PHASESTART  1
#define CFG_SPI_PHASEMID    0

#define CFG_SPI_MASTER      1
#define CFG_SPI_SLAVE       0

#define CFG_SPI_SENELAST    0
#define CFG_SPI_SENDZERO    1

#define CFG_SPI_RCVFLUSH    1
#define CFG_SPI_RCVDISCARD  0

#define CFG_SPI_LSBFIRST    1
#define CFG_SPI_MSBFIRST    0

#define CFG_SPI_WORDSIZE16  1
#define CFG_SPI_WORDSIZE8   0

#define CFG_SPI_MISOENABLE   1
#define CFG_SPI_MISODISABLE  0

#define CFG_SPI_READ      0x00
#define CFG_SPI_WRITE     0x01
#define CFG_SPI_DMAREAD   0x02
#define CFG_SPI_DMAWRITE  0x03

#define CFG_SPI_CSCLEARALL  0
#define CFG_SPI_CHIPSEL1    1
#define CFG_SPI_CHIPSEL2    2
#define CFG_SPI_CHIPSEL3    3
#define CFG_SPI_CHIPSEL4    4
#define CFG_SPI_CHIPSEL5    5
#define CFG_SPI_CHIPSEL6    6
#define CFG_SPI_CHIPSEL7    7

#define CFG_SPI_CS1VALUE    1
#define CFG_SPI_CS2VALUE    2
#define CFG_SPI_CS3VALUE    3
#define CFG_SPI_CS4VALUE    4
#define CFG_SPI_CS5VALUE    5
#define CFG_SPI_CS6VALUE    6
#define CFG_SPI_CS7VALUE    7

/* device.platform_data for SSP controller devices */
struct bfin5xx_spi_master {
	u16 num_chipselect;
	u8 enable_dma;
};

/* spi_board_info.controller_data for SPI slave devices,
 * copied to spi_device.platform_data ... mostly for dma tuning
 */
struct bfin5xx_spi_chip {
	u16 ctl_reg;
	u8 enable_dma;
	u8 bits_per_word;
	u8 cs_change_per_word;
	u16 cs_chg_udelay; /* Some devices require 16-bit delays */
};

#endif /* _SPI_CHANNEL_H_ */
