/*-
 * Ioctl interface to Cronyx serial drivers.
 *
 * Copyright (C) 1997-2002 Cronyx Engineering.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * Copyright (C) 2001-2005 Cronyx Engineering.
 * Author: Roman Kurakin, <rik@FreeBSD.org>
 *
 * Copyright (C) 2004-2005 Cronyx Engineering.
 * Author: Leo Yuriev, <ly@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * Cronyx Id: cserial.h,v 1.4.2.2 2005/11/09 13:01:35 rik Exp $
 * $FreeBSD$
 */

/*
 * General channel statistics.
 */
struct serial_statistics {
	unsigned long rintr;		/* receive interrupts */
	unsigned long tintr;		/* transmit interrupts */
	unsigned long mintr;		/* modem interrupts */
	unsigned long ibytes;		/* input bytes */
	unsigned long ipkts;		/* input packets */
	unsigned long ierrs;		/* input errors */
	unsigned long obytes;		/* output bytes */
	unsigned long opkts;		/* output packets */
	unsigned long oerrs;		/* output errors */
};

/*
 * Statistics for E1/G703 channels.
 */
struct e1_counters {
	unsigned long bpv;		/* bipolar violations */
	unsigned long fse;		/* frame sync errors */
	unsigned long crce;		/* CRC errors */
	unsigned long rcrce;		/* remote CRC errors (E-bit) */
	unsigned long uas;		/* unavailable seconds */
	unsigned long les;		/* line errored seconds */
	unsigned long es;		/* errored seconds */
	unsigned long bes;		/* bursty errored seconds */
	unsigned long ses;		/* severely errored seconds */
	unsigned long oofs;		/* out of frame seconds */
	unsigned long css;		/* controlled slip seconds */
	unsigned long dm;		/* degraded minutes */
};

struct e1_statistics {
	unsigned long status;		/* line status bit mask */
	unsigned long cursec;		/* seconds in current interval */
	unsigned long totsec;		/* total seconds elapsed */
	struct e1_counters currnt;	/* current 15-min interval data */
	struct e1_counters total;	/* total statistics data */
	struct e1_counters interval [48]; /* 12 hour period data */
};

struct e3_statistics {
	unsigned long status;
	unsigned long cursec;
	unsigned long totsec;
	unsigned long ccv;
	unsigned long tcv;
	unsigned long icv[48];
};

#define M_ASYNC         0		/* asynchronous mode */
#define M_HDLC          1		/* bit-sync mode (HDLC) */
#define M_G703          2
#define M_E1            3

/*
 * Receive error codes.
 */
#define ER_FRAMING	1		/* framing error */
#define ER_CHECKSUM	2		/* parity/CRC error */
#define ER_BREAK	3		/* break state */
#define ER_OVERFLOW	4		/* receive buffer overflow */
#define ER_OVERRUN	5		/* receive fifo overrun */
#define ER_UNDERRUN	6		/* transmit fifo underrun */
#define ER_SCC_FRAMING	7		/* subchannel framing error */
#define ER_SCC_OVERFLOW	8		/* subchannel receive buffer overflow */
#define ER_SCC_OVERRUN	9		/* subchannel receiver overrun */
#define ER_SCC_UNDERRUN	10		/* subchannel transmitter underrun */
#define ER_BUS		11		/* system bus is too busy (e.g PCI) */

/*
 * E1 channel status.
 */
#define E1_NOALARM	0x0001          /* no alarm present */
#define E1_FARLOF	0x0002          /* receiving far loss of framing */
#define E1_CRC4E	0x0004		/* crc4 errors */
#define E1_AIS		0x0008          /* receiving all ones */
#define E1_LOF		0x0020          /* loss of framing */
#define E1_LOS		0x0040          /* loss of signal */
#define E1_AIS16	0x0100          /* receiving all ones in timeslot 16 */
#define E1_FARLOMF	0x0200          /* receiving alarm in timeslot 16 */
#define E1_LOMF		0x0400          /* loss of multiframe sync */
#define E1_TSTREQ	0x0800          /* test code detected */
#define E1_TSTERR	0x1000          /* test error */

#define E3_LOS		0x00000002	/* Lost of synchronization */
#define E3_TXE		0x00000004	/* Transmit error */

/*
 * Query the mask of all registered channels, max 128.
 */
#define SERIAL_GETREGISTERED	_IOR ('x', 0, char[16])

/*
 * Attach/detach the protocol to the channel.
 * The protocol is given by its name, char[8].
 * For example "async", "hdlc", "cisco", "fr", "ppp".
 */
#define SERIAL_GETPROTO		_IOR ('x', 1, char [8])
#define SERIAL_SETPROTO		_IOW ('x', 1, char [8])

/*
 * Query/set the hardware mode for the channel.
 */
#define SERIAL_GETMODE		_IOR ('x', 2, int)
#define SERIAL_SETMODE		_IOW ('x', 2, int)

#define SERIAL_ASYNC		1
#define SERIAL_HDLC		2
#define SERIAL_RAW		3

/*
 * Get/clear the channel statistics.
 */
#define SERIAL_GETSTAT		_IOR ('x', 3, struct serial_statistics)
#define SERIAL_GETESTAT		_IOR ('x', 3, struct e1_statistics)
#define SERIAL_GETE3STAT	_IOR ('x', 3, struct e3_statistics)
#define SERIAL_CLRSTAT		_IO  ('x', 3)

/*
 * Query/set the synchronization mode and baud rate.
 * If baud==0 then the external clock is used.
 */
#define SERIAL_GETBAUD		_IOR ('x', 4, long)
#define SERIAL_SETBAUD		_IOW ('x', 4, long)

/*
 * Query/set the internal loopback mode,
 * useful for debugging purposes.
 */
#define SERIAL_GETLOOP		_IOR ('x', 5, int)
#define SERIAL_SETLOOP		_IOW ('x', 5, int)

/*
 * Query/set the DPLL mode, commonly used with NRZI
 * for channels lacking synchro signals.
 */
#define SERIAL_GETDPLL		_IOR ('x', 6, int)
#define SERIAL_SETDPLL		_IOW ('x', 6, int)

/*
 * Query/set the NRZI encoding (default is NRZ).
 */
#define SERIAL_GETNRZI		_IOR ('x', 7, int)
#define SERIAL_SETNRZI		_IOW ('x', 7, int)

/*
 * Invert receive and transmit clock.
 */
#define SERIAL_GETINVCLK	_IOR ('x', 8, int)
#define SERIAL_SETINVCLK	_IOW ('x', 8, int)

/*
 * Query/set the E1/G703 synchronization mode.
 */
#define SERIAL_GETCLK		_IOR ('x', 9, int)
#define SERIAL_SETCLK		_IOW ('x', 9, int)

#define E1CLK_RECOVERY		-1
#define E1CLK_INTERNAL		0
#define E1CLK_RECEIVE		1
#define E1CLK_RECEIVE_CHAN0	2
#define E1CLK_RECEIVE_CHAN1	3
#define E1CLK_RECEIVE_CHAN2	4
#define E1CLK_RECEIVE_CHAN3	5

/*
 * Query/set the E1 timeslot mask.
 */
#define SERIAL_GETTIMESLOTS	_IOR ('x', 10, long)
#define SERIAL_SETTIMESLOTS	_IOW ('x', 10, long)

/*
 * Query/set the E1 subchannel timeslot mask.
 */
#define SERIAL_GETSUBCHAN	_IOR ('x', 11, long)
#define SERIAL_SETSUBCHAN	_IOW ('x', 11, long)

/*
 * Query/set the high input sensitivity mode (E1).
 */
#define SERIAL_GETHIGAIN	_IOR ('x', 12, int)
#define SERIAL_SETHIGAIN	_IOW ('x', 12, int)

/*
 * Query the input signal level in santibells.
 */
#define SERIAL_GETLEVEL		_IOR ('x', 13, int)

/*
 * Get the channel name.
 */
#define SERIAL_GETNAME		_IOR ('x', 14, char [32])

/*
 * Get version string.
 */
#define SERIAL_GETVERSIONSTRING _IOR ('x', 15, char [256])

/*
 * Query/set master channel.
 */
#define SERIAL_GETMASTER	_IOR ('x', 16, char [16])
#define SERIAL_SETMASTER 	_IOW ('x', 16, char [16])

/*
 * Query/set keepalive.
 */
#define SERIAL_GETKEEPALIVE 	_IOR ('x', 17, int)
#define SERIAL_SETKEEPALIVE 	_IOW ('x', 17, int)

/*
 * Query/set E1 configuration.
 */
#define SERIAL_GETCFG		_IOR ('x', 18, char)
#define SERIAL_SETCFG		_IOW ('x', 18, char)

/*
 * Query/set debug.
 */
#define SERIAL_GETDEBUG		_IOR ('x', 19, int)
#define SERIAL_SETDEBUG		_IOW ('x', 19, int)

/*
 * Query/set phony mode (E1).
 */
#define SERIAL_GETPHONY		_IOR ('x', 20, int)
#define SERIAL_SETPHONY		_IOW ('x', 20, int)

/*
 * Query/set timeslot 16 usage mode (E1).
 */
#define SERIAL_GETUSE16		_IOR ('x', 21, int)
#define SERIAL_SETUSE16		_IOW ('x', 21, int)

/*
 * Query/set crc4 mode (E1).
 */
#define SERIAL_GETCRC4		_IOR ('x', 22, int)
#define SERIAL_SETCRC4		_IOW ('x', 22, int)

/*
 * Query/set the timeout to recover after transmit interrupt loss.
 * If timo==0 recover will be disabled.
 */
#define SERIAL_GETTIMO		_IOR ('x', 23, long)
#define SERIAL_SETTIMO		_IOW ('x', 23, long)

/*
 * Query/set port type for old models of Sigma
 * -1 Fixed or cable select
 * 0  RS-232
 * 1  V35
 * 2  RS-449
 * 3  E1	(only for Windows 2000)
 * 4  G.703	(only for Windows 2000)
 * 5  DATA	(only for Windows 2000)
 * 6  E3	(only for Windows 2000)
 * 7  T3	(only for Windows 2000)
 * 8  STS1	(only for Windows 2000)
 */
#define SERIAL_GETPORT		_IOR ('x', 25, int)
#define SERIAL_SETPORT		_IOW ('x', 25, int)

/*
 * Add the virtual channel DLCI (Frame Relay).
 */
#define SERIAL_ADDDLCI		_IOW ('x', 26, int)

/*
 * Invert receive clock.
 */
#define SERIAL_GETINVRCLK	_IOR ('x', 27, int)
#define SERIAL_SETINVRCLK	_IOW ('x', 27, int)

/*
 * Invert transmit clock.
 */
#define SERIAL_GETINVTCLK	_IOR ('x', 28, int)
#define SERIAL_SETINVTCLK	_IOW ('x', 28, int)

/*
 * Unframed E1 mode.
 */
#define SERIAL_GETUNFRAM	_IOR ('x', 29, int)
#define SERIAL_SETUNFRAM	_IOW ('x', 29, int)

/*
 * E1 monitoring mode.
 */
#define SERIAL_GETMONITOR	_IOR ('x', 30, int)
#define SERIAL_SETMONITOR	_IOW ('x', 30, int)

/*
 * Interrupt number.
 */
#define SERIAL_GETIRQ		_IOR ('x', 31, int)

/*
 * Reset.
 */
#define SERIAL_RESET		_IO ('x', 32)

/*
 * Hard reset.
 */
#define SERIAL_HARDRESET	_IO ('x', 33)

/*
 * Query cable type.
 */
#define SERIAL_GETCABLE		_IOR ('x', 34, int)

/*
 * Assignment of HDLC ports to E1 channels.
 */
#define SERIAL_GETDIR		_IOR ('x', 35, int)
#define SERIAL_SETDIR		_IOW ('x', 35, int)

struct dxc_table {			/* cross-connector parameters */
	unsigned char ts [32];		/* timeslot number */
	unsigned char link [32];	/* E1 link number */
};

/*
 * DXC cross-connector settings for E1 channels.
 */
#define SERIAL_GETDXC		_IOR ('x', 36, struct dxc_table)
#define SERIAL_SETDXC		_IOW ('x', 36, struct dxc_table)

/*
 * Scrambler for G.703.
 */
#define SERIAL_GETSCRAMBLER	_IOR ('x', 37, int)
#define SERIAL_SETSCRAMBLER	_IOW ('x', 37, int)

/*
 * Length of cable for T3 and STS-1.
 */
#define SERIAL_GETCABLEN	_IOR ('x', 38, int)
#define SERIAL_SETCABLEN	_IOW ('x', 38, int)

/*
 * Remote loopback for E3, T3 and STS-1.
 */
#define SERIAL_GETRLOOP		_IOR ('x', 39, int)
#define SERIAL_SETRLOOP		_IOW ('x', 39, int)

/*
 * G.703 line code
 */
#define SERIAL_GETLCODE		_IOR ('x', 40, int)
#define SERIAL_SETLCODE		_IOW ('x', 40, int)

/*
 * MTU
 */
#define SERIAL_GETMTU		_IOR ('x', 41, int)
#define SERIAL_SETMTU		_IOW ('x', 41, int)

/*
 * Receive Queue Length
 */
#define SERIAL_GETRQLEN		_IOR ('x', 42, int)
#define SERIAL_SETRQLEN		_IOW ('x', 42, int)

#ifdef __KERNEL__
#ifdef CRONYX_LYSAP
#	define LYSAP_PEER_ADD		_IOWR('x', 101, lysap_peer_config_t)
#	define LYSAP_PEER_REMOVE	_IOW('x', 102, unsigned)
#	define LYSAP_PEER_INFO		_IOWR('x', 103, lysap_peer_info_t)
#	define LYSAP_PEER_COUNT		_IOR('x', 104, unsigned)
#	define LYSAP_PEER_ENUM		_IOWR('x', 105, unsigned)
#	define LYSAP_PEER_CLEAR		_IOW('x', 106, unsigned)

#	define LYSAP_CHAN_ADD		_IOWR('x', 111, lysap_channel_config_t)
#	define LYSAP_CHAN_REMOVE	_IO('x', 112)
#	define LYSAP_CHAN_INFO		_IOR('x', 113, lysap_channel_info_t)
#	define LYSAP_CHAN_COUNT		_IOR('x', 114, unsigned)
#	define LYSAP_CHAN_ENUM		_IOWR('x', 115, unsigned)
#	define LYSAP_CHAN_CLEAR		_IO('x', 116)
#	include "lysap-linux.h"
#else /* CRONYX_LYSAP */
	typedef struct _lysap_channel_t lysap_channel_t;
	typedef struct _lysap_channel_config_t lysap_channel_config_t;
	typedef struct _LYSAP_DeviceInterfaceConfig LYSAP_DeviceInterfaceConfig;
	typedef struct _LYSAP_ChannelConfig LYSAP_ChannelConfig;
	typedef struct _lysap_buf_t lysap_buf_t;
#endif /* !CRONYX_LYSAP */

/*
 * Dynamic binder interface.
 */
typedef struct _chan_t chan_t;
typedef struct _proto_t proto_t;

void binder_register_protocol (proto_t *p);
void binder_unregister_protocol (proto_t *p);

int binder_register_channel (chan_t *h, char *prefix, int minor);
void binder_unregister_channel (chan_t *h);

/*
 * Hardware channel driver structure.
 */
struct sk_buff;

struct _chan_t {
	char name [16];
	int mtu;			/* max packet size */
	int fifosz;			/* total hardware i/o buffer size */
	int port;			/* hardware base i/o port */
	int irq;			/* hardware interrupt line */
	int minor;			/* minor number 0..127, assigned by binder */
	int debug;			/* debug level, 0..2 */
	int running;			/* running, 0..1 */
	struct _proto_t *proto;		/* protocol interface data */
	void *sw;			/* protocol private data */
	void *hw;			/* hardware layer private data */

	/* Interface to protocol */
	int (*up) (chan_t *h);
	void (*down) (chan_t *h);
	int (*transmit) (chan_t *h, struct sk_buff *skb);
	void (*set_dtr) (chan_t *h, int val);
	void (*set_rts) (chan_t *h, int val);
	int (*query_dtr) (chan_t *h);
	int (*query_rts) (chan_t *h);
	int (*query_dsr) (chan_t *h);
	int (*query_cts) (chan_t *h);
	int (*query_dcd) (chan_t *h);

	/* Interface to async protocol */
	void (*set_async_param) (chan_t *h, int baud, int bits, int parity,
		int stop2, int ignpar, int rtscts,
		int ixon, int ixany, int symstart, int symstop);
	void (*send_break) (chan_t *h, int msec);
	void (*send_xon) (chan_t *h);
	void (*send_xoff) (chan_t *h);
	void (*start_transmitter) (chan_t *h);
	void (*stop_transmitter) (chan_t *h);
	void (*flush_transmit_buffer) (chan_t *h);

	/* Control interface */
	int (*control) (chan_t *h, unsigned int cmd, unsigned long arg);

	/* LYSAP interface */
	struct lysap_t
	{
		lysap_channel_t *link;
		int (*inspect_config)(chan_t *h, lysap_channel_config_t *,
			LYSAP_DeviceInterfaceConfig *, LYSAP_ChannelConfig *);
		unsigned long (*probe_freq)(chan_t *h, unsigned long freq);
		unsigned long (*set_freq)(chan_t *h, unsigned long freq);
		unsigned (*get_status)(chan_t *h);
		int (*transmit) (chan_t *h, lysap_buf_t *b);
		lysap_buf_t* (*alloc_buf) (chan_t *h, unsigned len);
		int (*set_clock_master)(chan_t *h, int enable);
		unsigned long (*get_master_freq)(chan_t *h);
	} lysap;
};

/*
 * Protocol driver structure.
 */
struct _proto_t {
	char *name;
	struct _proto_t *next;

	/* Interface to channel */
	void (*receive) (chan_t *h, struct sk_buff *skb);
	void (*receive_error) (chan_t *h, int errcode);
	void (*transmit) (chan_t *h);
	void (*modem_event) (chan_t *h);

	/* Interface to binder */
	int (*open) (chan_t *h);
	void (*close) (chan_t *h);
	int (*read) (chan_t *h, unsigned short flg, char *buf, int len);
	int (*write) (chan_t *h, unsigned short flg, const char *buf, int len);
	int (*select) (chan_t *h, int type, void *st, struct file *filp);
	struct fasync_struct *fasync;

	/* Control interface */
	int (*attach) (chan_t *h);
	int (*detach) (chan_t *h);
	int (*control) (chan_t *h, unsigned int cmd, unsigned long arg);

	/* LYSAP interface */
	void (*transmit_error) (chan_t *h, int errcode);
	void (*lysap_notify_receive) (chan_t *h, lysap_buf_t *b);
	void (*lysap_notify_transmit) (chan_t *h);
	lysap_buf_t* (*lysap_get_data)(chan_t *h);
};
#endif /* KERNEL */
