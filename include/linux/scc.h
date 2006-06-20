/* $Id: scc.h,v 1.29 1997/04/02 14:56:45 jreuter Exp jreuter $ */

#ifndef	_SCC_H
#define	_SCC_H


/* selection of hardware types */

#define PA0HZP		0x00	/* hardware type for PA0HZP SCC card and compatible */
#define EAGLE		0x01    /* hardware type for EAGLE card */
#define PC100		0x02	/* hardware type for PC100 card */
#define PRIMUS		0x04	/* hardware type for PRIMUS-PC (DG9BL) card */
#define DRSI		0x08	/* hardware type for DRSI PC*Packet card */
#define BAYCOM		0x10	/* hardware type for BayCom (U)SCC */

/* DEV ioctl() commands */

enum SCC_ioctl_cmds {
	SIOCSCCRESERVED = SIOCDEVPRIVATE,
	SIOCSCCCFG,
	SIOCSCCINI,
	SIOCSCCCHANINI,
	SIOCSCCSMEM,
	SIOCSCCGKISS,
	SIOCSCCSKISS,
	SIOCSCCGSTAT,
	SIOCSCCCAL
};

/* Device parameter control (from WAMPES) */

enum L1_params {
	PARAM_DATA,
	PARAM_TXDELAY,
	PARAM_PERSIST,
	PARAM_SLOTTIME,
	PARAM_TXTAIL,
	PARAM_FULLDUP,
	PARAM_SOFTDCD,		/* was: PARAM_HW */
	PARAM_MUTE,		/* ??? */
	PARAM_DTR,
	PARAM_RTS,
	PARAM_SPEED,
	PARAM_ENDDELAY,		/* ??? */
	PARAM_GROUP,
	PARAM_IDLE,
	PARAM_MIN,
	PARAM_MAXKEY,
	PARAM_WAIT,
	PARAM_MAXDEFER,
	PARAM_TX,
	PARAM_HWEVENT = 31,
	PARAM_RETURN = 255	/* reset kiss mode */
};

/* fulldup parameter */

enum FULLDUP_modes {
	KISS_DUPLEX_HALF,	/* normal CSMA operation */
	KISS_DUPLEX_FULL,	/* fullduplex, key down trx after transmission */
	KISS_DUPLEX_LINK,	/* fullduplex, key down trx after 'idletime' sec */
	KISS_DUPLEX_OPTIMA	/* fullduplex, let the protocol layer control the hw */
};

/* misc. parameters */

#define TIMER_OFF	65535U	/* to switch off timers */
#define NO_SUCH_PARAM	65534U	/* param not implemented */

/* HWEVENT parameter */

enum HWEVENT_opts {
	HWEV_DCD_ON,
	HWEV_DCD_OFF,
	HWEV_ALL_SENT
};

/* channel grouping */

#define RXGROUP		0100	/* if set, only tx when all channels clear */
#define TXGROUP		0200	/* if set, don't transmit simultaneously */

/* Tx/Rx clock sources */

enum CLOCK_sources {
	CLK_DPLL,	/* normal halfduplex operation */
	CLK_EXTERNAL,	/* external clocking (G3RUH/DF9IC modems) */
	CLK_DIVIDER,	/* Rx = DPLL, Tx = divider (fullduplex with */
			/* modems without clock regeneration */
	CLK_BRG		/* experimental fullduplex mode with DPLL/BRG for */
			/* MODEMs without clock recovery */
};

/* Tx state */

enum TX_state {
	TXS_IDLE,	/* Transmitter off, no data pending */
	TXS_BUSY,	/* waiting for permission to send / tailtime */
	TXS_ACTIVE,	/* Transmitter on, sending data */
	TXS_NEWFRAME,	/* reset CRC and send (next) frame */
	TXS_IDLE2,	/* Transmitter on, no data pending */
	TXS_WAIT,	/* Waiting for Mintime to expire */
	TXS_TIMEOUT	/* We had a transmission timeout */
};

typedef unsigned long io_port;	/* type definition for an 'io port address' */

/* SCC statistical information */

struct scc_stat {
        long rxints;            /* Receiver interrupts */
        long txints;            /* Transmitter interrupts */
        long exints;            /* External/status interrupts */
        long spints;            /* Special receiver interrupts */

        long txframes;          /* Packets sent */
        long rxframes;          /* Number of Frames Actually Received */
        long rxerrs;            /* CRC Errors */
        long txerrs;		/* KISS errors */
        
	unsigned int nospace;	/* "Out of buffers" */
	unsigned int rx_over;	/* Receiver Overruns */
	unsigned int tx_under;	/* Transmitter Underruns */

	unsigned int tx_state;	/* Transmitter state */
	int tx_queued;		/* tx frames enqueued */

	unsigned int maxqueue;	/* allocated tx_buffers */
	unsigned int bufsize;	/* used buffersize */
};

struct scc_modem {
	long speed;		/* Line speed, bps */
	char clocksrc;		/* 0 = DPLL, 1 = external, 2 = divider */
	char nrz;		/* NRZ instead of NRZI */	
};

struct scc_kiss_cmd {
	int  	 command;	/* one of the KISS-Commands defined above */
	unsigned param;		/* KISS-Param */
};

struct scc_hw_config {
	io_port data_a;		/* data port channel A */
	io_port ctrl_a;		/* control port channel A */
	io_port data_b;		/* data port channel B */
	io_port ctrl_b;		/* control port channel B */
	io_port vector_latch;	/* INTACK-Latch (#) */
	io_port	special;	/* special function port */

	int	irq;		/* irq */
	long	clock;		/* clock */
	char	option;		/* command for function port */

	char brand;		/* hardware type */
	char escc;		/* use ext. features of a 8580/85180/85280 */
};

/* (#) only one INTACK latch allowed. */


struct scc_mem_config {
	unsigned int dummy;
	unsigned int bufsize;
};

struct scc_calibrate {
	unsigned int time;
	unsigned char pattern;
};

#ifdef __KERNEL__

enum {TX_OFF, TX_ON};	/* command for scc_key_trx() */

/* Vector masks in RR2B */

#define VECTOR_MASK	0x06
#define TXINT		0x00
#define EXINT		0x02
#define RXINT		0x04
#define SPINT		0x06

#ifdef CONFIG_SCC_DELAY
#define Inb(port)	inb_p(port)
#define Outb(port, val)	outb_p(val, port)
#else
#define Inb(port)	inb(port)
#define Outb(port, val)	outb(val, port)
#endif

/* SCC channel control structure for KISS */

struct scc_kiss {
	unsigned char txdelay;		/* Transmit Delay 10 ms/cnt */
	unsigned char persist;		/* Persistence (0-255) as a % */
	unsigned char slottime;		/* Delay to wait on persistence hit */
	unsigned char tailtime;		/* Delay after last byte written */
	unsigned char fulldup;		/* Full Duplex mode 0=CSMA 1=DUP 2=ALWAYS KEYED */
	unsigned char waittime;		/* Waittime before any transmit attempt */
	unsigned int  maxkeyup;		/* Maximum time to transmit (seconds) */
	unsigned int  mintime;		/* Minimal offtime after MAXKEYUP timeout (seconds) */
	unsigned int  idletime;		/* Maximum idle time in ALWAYS KEYED mode (seconds) */
	unsigned int  maxdefer;		/* Timer for CSMA channel busy limit */
	unsigned char tx_inhibit;	/* Transmit is not allowed when set */	
	unsigned char group;		/* Group ID for AX.25 TX interlocking */
	unsigned char mode;		/* 'normal' or 'hwctrl' mode (unused) */
	unsigned char softdcd;		/* Use DPLL instead of DCD pin for carrier detect */
};


/* SCC channel structure */

struct scc_channel {
	int init;			/* channel exists? */

	struct net_device *dev;		/* link to device control structure */
	struct net_device_stats dev_stat;/* device statistics */

	char brand;			/* manufacturer of the board */
	long clock;			/* used clock */

	io_port ctrl;			/* I/O address of CONTROL register */
	io_port	data;			/* I/O address of DATA register */
	io_port special;		/* I/O address of special function port */
	int irq;			/* Number of Interrupt */

	char option;
	char enhanced;			/* Enhanced SCC support */

	unsigned char wreg[16]; 	/* Copy of last written value in WRx */
	unsigned char status;		/* Copy of R0 at last external interrupt */
	unsigned char dcd;		/* DCD status */

        struct scc_kiss kiss;		/* control structure for KISS params */
        struct scc_stat stat;		/* statistical information */
        struct scc_modem modem; 	/* modem information */

        struct sk_buff_head tx_queue;	/* next tx buffer */
        struct sk_buff *rx_buff;	/* pointer to frame currently received */
        struct sk_buff *tx_buff;	/* pointer to frame currently transmitted */

	/* Timer */
	struct timer_list tx_t;		/* tx timer for this channel */
	struct timer_list tx_wdog;	/* tx watchdogs */
	
	/* Channel lock */
	spinlock_t	lock;		/* Channel guard lock */
};

#endif /* defined(__KERNEL__) */
#endif /* defined(_SCC_H) */
