#ifndef _UAPI_LINUX_TTY_H
#define _UAPI_LINUX_TTY_H

/*
 * 'tty.h' defines some structures used by tty_io.c and some defines.
 */

#define NR_LDISCS		30

/* line disciplines */
#define N_TTY		0
#define N_SLIP		1
#define N_MOUSE		2
#define N_PPP		3
#define N_STRIP		4
#define N_AX25		5
#define N_X25		6	/* X.25 async */
#define N_6PACK		7
#define N_MASC		8	/* Reserved for Mobitex module <kaz@cafe.net> */
#define N_R3964		9	/* Reserved for Simatic R3964 module */
#define N_PROFIBUS_FDL	10	/* Reserved for Profibus */
#define N_IRDA		11	/* Linux IrDa - http://irda.sourceforge.net/ */
#define N_SMSBLOCK	12	/* SMS block mode - for talking to GSM data */
				/* cards about SMS messages */
#define N_HDLC		13	/* synchronous HDLC */
#define N_SYNC_PPP	14	/* synchronous PPP */
#define N_HCI		15	/* Bluetooth HCI UART */
#define N_GIGASET_M101	16	/* Siemens Gigaset M101 serial DECT adapter */
#define N_SLCAN		17	/* Serial / USB serial CAN Adaptors */
#define N_PPS		18	/* Pulse per Second */
#define N_V253		19	/* Codec control over voice modem */
#define N_CAIF		20      /* CAIF protocol for talking to modems */
#define N_GSM0710	21	/* GSM 0710 Mux */
#define N_TI_WL		22	/* for TI's WL BT, FM, GPS combo chips */
#define N_TRACESINK	23	/* Trace data routing for MIPI P1149.7 */
#define N_TRACEROUTER	24	/* Trace data routing for MIPI P1149.7 */
#define N_NCI		25	/* NFC NCI UART */
#define N_SPEAKUP	26	/* Speakup communication with synths */

#endif /* _UAPI_LINUX_TTY_H */
