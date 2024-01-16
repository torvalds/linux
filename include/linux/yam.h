/* SPDX-License-Identifier: GPL-2.0-or-later */
/*****************************************************************************/

/*
 *	yam.h  -- YAM radio modem driver.
 *
 *	Copyright (C) 1998 Frederic Rible F1OAT (frible@teaser.fr)
 *	Adapted from baycom.c driver written by Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *  Please note that the GPL allows you to use the driver, NOT the radio.
 *  In order to use the radio, you need a license from the communications
 *  authority of your country.
 */

/*****************************************************************************/

#define SIOCYAMRESERVED	(0)
#define SIOCYAMSCFG 	(1)	/* Set configuration */
#define SIOCYAMGCFG 	(2)	/* Get configuration */
#define SIOCYAMSMCS 	(3)	/* Set mcs data */

#define YAM_IOBASE   (1 << 0)
#define YAM_IRQ      (1 << 1)
#define YAM_BITRATE  (1 << 2) /* Bit rate of radio port ->57600 */
#define YAM_MODE     (1 << 3) /* 0=simplex 1=duplex 2=duplex+tempo */
#define YAM_HOLDDLY  (1 << 4) /* duplex tempo (sec) */
#define YAM_TXDELAY  (1 << 5) /* Tx Delay (ms) */
#define YAM_TXTAIL   (1 << 6) /* Tx Tail  (ms) */
#define YAM_PERSIST  (1 << 7) /* Persist  (ms) */
#define YAM_SLOTTIME (1 << 8) /* Slottime (ms) */
#define YAM_BAUDRATE (1 << 9) /* Baud rate of rs232 port ->115200 */

#define YAM_MAXBITRATE  57600
#define YAM_MAXBAUDRATE 115200
#define YAM_MAXMODE     2
#define YAM_MAXHOLDDLY  99
#define YAM_MAXTXDELAY  999
#define YAM_MAXTXTAIL   999
#define YAM_MAXPERSIST  255
#define YAM_MAXSLOTTIME 999

#define YAM_FPGA_SIZE	5302

struct yamcfg {
	unsigned int mask;		/* Mask of commands */
	unsigned int iobase;	/* IO Base of COM port */
	unsigned int irq;		/* IRQ of COM port */
	unsigned int bitrate;	/* Bit rate of radio port */
	unsigned int baudrate;	/* Baud rate of the RS232 port */
	unsigned int txdelay;	/* TxDelay */
	unsigned int txtail;	/* TxTail */
	unsigned int persist;	/* Persistence */
	unsigned int slottime;	/* Slottime */
	unsigned int mode;		/* mode 0 (simp), 1(Dupl), 2(Dupl+delay) */
	unsigned int holddly;	/* PTT delay in FullDuplex 2 mode */
};

struct yamdrv_ioctl_cfg {
	int cmd;
	struct yamcfg cfg;
};

struct yamdrv_ioctl_mcs {
	int cmd;
	unsigned int bitrate;
	unsigned char bits[YAM_FPGA_SIZE];
};
