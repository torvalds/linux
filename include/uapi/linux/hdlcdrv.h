/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * hdlcdrv.h  -- HDLC packet radio network driver.
 * The Linux soundcard driver for 1200 baud and 9600 baud packet radio
 * (C) 1996-1998 by Thomas Sailer, HB9JNX/AE4WA
 */

#ifndef _UAPI_HDLCDRV_H
#define _UAPI_HDLCDRV_H

/* -------------------------------------------------------------------- */
/*
 * structs for the IOCTL commands
 */

struct hdlcdrv_params {
	int iobase;
	int irq;
	int dma;
	int dma2;
	int seriobase;
	int pariobase;
	int midiiobase;
};	

struct hdlcdrv_channel_params {
	int tx_delay;  /* the transmitter keyup delay in 10ms units */
	int tx_tail;   /* the transmitter keyoff delay in 10ms units */
	int slottime;  /* the slottime in 10ms; usually 10 = 100ms */
	int ppersist;  /* the p-persistence 0..255 */
	int fulldup;   /* some driver do not support full duplex, setting */
	               /* this just makes them send even if DCD is on */
};	

struct hdlcdrv_old_channel_state {
  	int ptt;
  	int dcd;
  	int ptt_keyed;
};

struct hdlcdrv_channel_state {
 	int ptt;
 	int dcd;
 	int ptt_keyed;
 	unsigned long tx_packets;
 	unsigned long tx_errors;
 	unsigned long rx_packets;
 	unsigned long rx_errors;
};

struct hdlcdrv_ioctl {
	int cmd;
	union {
		struct hdlcdrv_params mp;
		struct hdlcdrv_channel_params cp;
		struct hdlcdrv_channel_state cs;
		struct hdlcdrv_old_channel_state ocs;
		unsigned int calibrate;
		unsigned char bits;
		char modename[128];
		char drivername[32];
	} data;
};

/* -------------------------------------------------------------------- */

/*
 * ioctl values
 */
#define HDLCDRVCTL_GETMODEMPAR       0
#define HDLCDRVCTL_SETMODEMPAR       1
#define HDLCDRVCTL_MODEMPARMASK      2  /* not handled by hdlcdrv */
#define HDLCDRVCTL_GETCHANNELPAR    10
#define HDLCDRVCTL_SETCHANNELPAR    11
#define HDLCDRVCTL_OLDGETSTAT       20
#define HDLCDRVCTL_CALIBRATE        21
#define HDLCDRVCTL_GETSTAT          22

/*
 * these are mainly for debugging purposes
 */
#define HDLCDRVCTL_GETSAMPLES       30
#define HDLCDRVCTL_GETBITS          31

/*
 * not handled by hdlcdrv, but by its depending drivers
 */
#define HDLCDRVCTL_GETMODE          40
#define HDLCDRVCTL_SETMODE          41
#define HDLCDRVCTL_MODELIST         42
#define HDLCDRVCTL_DRIVERNAME       43

/*
 * mask of needed modem parameters, returned by HDLCDRVCTL_MODEMPARMASK
 */
#define HDLCDRV_PARMASK_IOBASE      (1<<0)
#define HDLCDRV_PARMASK_IRQ         (1<<1)
#define HDLCDRV_PARMASK_DMA         (1<<2)
#define HDLCDRV_PARMASK_DMA2        (1<<3)
#define HDLCDRV_PARMASK_SERIOBASE   (1<<4)
#define HDLCDRV_PARMASK_PARIOBASE   (1<<5)
#define HDLCDRV_PARMASK_MIDIIOBASE  (1<<6)

/* -------------------------------------------------------------------- */


/* -------------------------------------------------------------------- */

#endif /* _UAPI_HDLCDRV_H */

/* -------------------------------------------------------------------- */
