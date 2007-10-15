/*
 *  Main header file for the ALSA sequencer
 *  Copyright (c) 1998-1999 by Frank van de Pol <fvdpol@coil.demon.nl>
 *            (c) 1998-1999 by Jaroslav Kysela <perex@perex.cz>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#ifndef __SOUND_ASEQUENCER_H
#define __SOUND_ASEQUENCER_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <sound/asound.h>
#endif

/** version of the sequencer */
#define SNDRV_SEQ_VERSION SNDRV_PROTOCOL_VERSION (1, 0, 1)

/**
 * definition of sequencer event types
 */

/** system messages
 * event data type = #snd_seq_result
 */
#define SNDRV_SEQ_EVENT_SYSTEM		0
#define SNDRV_SEQ_EVENT_RESULT		1

/** note messages (channel specific)
 * event data type = #snd_seq_ev_note
 */
#define SNDRV_SEQ_EVENT_NOTE		5
#define SNDRV_SEQ_EVENT_NOTEON		6
#define SNDRV_SEQ_EVENT_NOTEOFF		7
#define SNDRV_SEQ_EVENT_KEYPRESS	8
	
/** control messages (channel specific)
 * event data type = #snd_seq_ev_ctrl
 */
#define SNDRV_SEQ_EVENT_CONTROLLER	10
#define SNDRV_SEQ_EVENT_PGMCHANGE	11
#define SNDRV_SEQ_EVENT_CHANPRESS	12
#define SNDRV_SEQ_EVENT_PITCHBEND	13	/**< from -8192 to 8191 */
#define SNDRV_SEQ_EVENT_CONTROL14	14	/**< 14 bit controller value */
#define SNDRV_SEQ_EVENT_NONREGPARAM	15	/**< 14 bit NRPN address + 14 bit unsigned value */
#define SNDRV_SEQ_EVENT_REGPARAM	16	/**< 14 bit RPN address + 14 bit unsigned value */

/** synchronisation messages
 * event data type = #snd_seq_ev_ctrl
 */
#define SNDRV_SEQ_EVENT_SONGPOS		20	/* Song Position Pointer with LSB and MSB values */
#define SNDRV_SEQ_EVENT_SONGSEL		21	/* Song Select with song ID number */
#define SNDRV_SEQ_EVENT_QFRAME		22	/* midi time code quarter frame */
#define SNDRV_SEQ_EVENT_TIMESIGN	23	/* SMF Time Signature event */
#define SNDRV_SEQ_EVENT_KEYSIGN		24	/* SMF Key Signature event */
	        
/** timer messages
 * event data type = snd_seq_ev_queue_control
 */
#define SNDRV_SEQ_EVENT_START		30	/* midi Real Time Start message */
#define SNDRV_SEQ_EVENT_CONTINUE	31	/* midi Real Time Continue message */
#define SNDRV_SEQ_EVENT_STOP		32	/* midi Real Time Stop message */	
#define	SNDRV_SEQ_EVENT_SETPOS_TICK	33	/* set tick queue position */
#define SNDRV_SEQ_EVENT_SETPOS_TIME	34	/* set realtime queue position */
#define SNDRV_SEQ_EVENT_TEMPO		35	/* (SMF) Tempo event */
#define SNDRV_SEQ_EVENT_CLOCK		36	/* midi Real Time Clock message */
#define SNDRV_SEQ_EVENT_TICK		37	/* midi Real Time Tick message */
#define SNDRV_SEQ_EVENT_QUEUE_SKEW	38	/* skew queue tempo */

/** others
 * event data type = none
 */
#define SNDRV_SEQ_EVENT_TUNE_REQUEST	40	/* tune request */
#define SNDRV_SEQ_EVENT_RESET		41	/* reset to power-on state */
#define SNDRV_SEQ_EVENT_SENSING		42	/* "active sensing" event */

/** echo back, kernel private messages
 * event data type = any type
 */
#define SNDRV_SEQ_EVENT_ECHO		50	/* echo event */
#define SNDRV_SEQ_EVENT_OSS		51	/* OSS raw event */

/** system status messages (broadcast for subscribers)
 * event data type = snd_seq_addr
 */
#define SNDRV_SEQ_EVENT_CLIENT_START	60	/* new client has connected */
#define SNDRV_SEQ_EVENT_CLIENT_EXIT	61	/* client has left the system */
#define SNDRV_SEQ_EVENT_CLIENT_CHANGE	62	/* client status/info has changed */
#define SNDRV_SEQ_EVENT_PORT_START	63	/* new port was created */
#define SNDRV_SEQ_EVENT_PORT_EXIT	64	/* port was deleted from system */
#define SNDRV_SEQ_EVENT_PORT_CHANGE	65	/* port status/info has changed */

/** port connection changes
 * event data type = snd_seq_connect
 */
#define SNDRV_SEQ_EVENT_PORT_SUBSCRIBED	66	/* ports connected */
#define SNDRV_SEQ_EVENT_PORT_UNSUBSCRIBED 67	/* ports disconnected */

/** synthesizer events
 * event data type = snd_seq_eve_sample_control
 */
#define SNDRV_SEQ_EVENT_SAMPLE		70	/* sample select */
#define SNDRV_SEQ_EVENT_SAMPLE_CLUSTER	71	/* sample cluster select */
#define SNDRV_SEQ_EVENT_SAMPLE_START	72	/* voice start */
#define SNDRV_SEQ_EVENT_SAMPLE_STOP	73	/* voice stop */
#define SNDRV_SEQ_EVENT_SAMPLE_FREQ	74	/* playback frequency */
#define SNDRV_SEQ_EVENT_SAMPLE_VOLUME	75	/* volume and balance */
#define SNDRV_SEQ_EVENT_SAMPLE_LOOP	76	/* sample loop */
#define SNDRV_SEQ_EVENT_SAMPLE_POSITION	77	/* sample position */
#define SNDRV_SEQ_EVENT_SAMPLE_PRIVATE1	78	/* private (hardware dependent) event */

/** user-defined events with fixed length
 * event data type = any
 */
#define SNDRV_SEQ_EVENT_USR0		90
#define SNDRV_SEQ_EVENT_USR1		91
#define SNDRV_SEQ_EVENT_USR2		92
#define SNDRV_SEQ_EVENT_USR3		93
#define SNDRV_SEQ_EVENT_USR4		94
#define SNDRV_SEQ_EVENT_USR5		95
#define SNDRV_SEQ_EVENT_USR6		96
#define SNDRV_SEQ_EVENT_USR7		97
#define SNDRV_SEQ_EVENT_USR8		98
#define SNDRV_SEQ_EVENT_USR9		99

/** instrument layer
 * variable length data can be passed directly to the driver
 */
#define SNDRV_SEQ_EVENT_INSTR_BEGIN	100	/* begin of instrument management */
#define SNDRV_SEQ_EVENT_INSTR_END	101	/* end of instrument management */
#define SNDRV_SEQ_EVENT_INSTR_INFO	102	/* instrument interface info */
#define SNDRV_SEQ_EVENT_INSTR_INFO_RESULT 103	/* result */
#define SNDRV_SEQ_EVENT_INSTR_FINFO	104	/* get format info */
#define SNDRV_SEQ_EVENT_INSTR_FINFO_RESULT 105	/* get format info */
#define SNDRV_SEQ_EVENT_INSTR_RESET	106	/* reset instrument memory */
#define SNDRV_SEQ_EVENT_INSTR_STATUS	107	/* instrument interface status */
#define SNDRV_SEQ_EVENT_INSTR_STATUS_RESULT 108	/* result */
#define SNDRV_SEQ_EVENT_INSTR_PUT	109	/* put instrument to port */
#define SNDRV_SEQ_EVENT_INSTR_GET	110	/* get instrument from port */
#define SNDRV_SEQ_EVENT_INSTR_GET_RESULT 111	/* result */
#define SNDRV_SEQ_EVENT_INSTR_FREE	112	/* free instrument(s) */
#define SNDRV_SEQ_EVENT_INSTR_LIST	113	/* instrument list */
#define SNDRV_SEQ_EVENT_INSTR_LIST_RESULT 114	/* result */
#define SNDRV_SEQ_EVENT_INSTR_CLUSTER	115	/* cluster parameters */
#define SNDRV_SEQ_EVENT_INSTR_CLUSTER_GET 116	/* get cluster parameters */
#define SNDRV_SEQ_EVENT_INSTR_CLUSTER_RESULT 117 /* result */
#define SNDRV_SEQ_EVENT_INSTR_CHANGE	118	/* instrument change */
/* 119-129: reserved */

/* 130-139: variable length events
 * event data type = snd_seq_ev_ext
 * (SNDRV_SEQ_EVENT_LENGTH_VARIABLE must be set)
 */
#define SNDRV_SEQ_EVENT_SYSEX		130	/* system exclusive data (variable length) */
#define SNDRV_SEQ_EVENT_BOUNCE		131	/* error event */
/* 132-134: reserved */
#define SNDRV_SEQ_EVENT_USR_VAR0	135
#define SNDRV_SEQ_EVENT_USR_VAR1	136
#define SNDRV_SEQ_EVENT_USR_VAR2	137
#define SNDRV_SEQ_EVENT_USR_VAR3	138
#define SNDRV_SEQ_EVENT_USR_VAR4	139

/* 150-151: kernel events with quote - DO NOT use in user clients */
#define SNDRV_SEQ_EVENT_KERNEL_ERROR	150
#define SNDRV_SEQ_EVENT_KERNEL_QUOTE	151	/* obsolete */

/* 152-191: reserved */

/* 192-254: hardware specific events */

/* 255: special event */
#define SNDRV_SEQ_EVENT_NONE		255


typedef unsigned char snd_seq_event_type_t;

/** event address */
struct snd_seq_addr {
	unsigned char client;	/**< Client number:         0..255, 255 = broadcast to all clients */
	unsigned char port;	/**< Port within client:    0..255, 255 = broadcast to all ports */
};

/** port connection */
struct snd_seq_connect {
	struct snd_seq_addr sender;
	struct snd_seq_addr dest;
};


#define SNDRV_SEQ_ADDRESS_UNKNOWN	253	/* unknown source */
#define SNDRV_SEQ_ADDRESS_SUBSCRIBERS	254	/* send event to all subscribed ports */
#define SNDRV_SEQ_ADDRESS_BROADCAST	255	/* send event to all queues/clients/ports/channels */
#define SNDRV_SEQ_QUEUE_DIRECT		253	/* direct dispatch */

	/* event mode flag - NOTE: only 8 bits available! */
#define SNDRV_SEQ_TIME_STAMP_TICK	(0<<0) /* timestamp in clock ticks */
#define SNDRV_SEQ_TIME_STAMP_REAL	(1<<0) /* timestamp in real time */
#define SNDRV_SEQ_TIME_STAMP_MASK	(1<<0)

#define SNDRV_SEQ_TIME_MODE_ABS		(0<<1)	/* absolute timestamp */
#define SNDRV_SEQ_TIME_MODE_REL		(1<<1)	/* relative to current time */
#define SNDRV_SEQ_TIME_MODE_MASK	(1<<1)

#define SNDRV_SEQ_EVENT_LENGTH_FIXED	(0<<2)	/* fixed event size */
#define SNDRV_SEQ_EVENT_LENGTH_VARIABLE	(1<<2)	/* variable event size */
#define SNDRV_SEQ_EVENT_LENGTH_VARUSR	(2<<2)	/* variable event size - user memory space */
#define SNDRV_SEQ_EVENT_LENGTH_MASK	(3<<2)

#define SNDRV_SEQ_PRIORITY_NORMAL	(0<<4)	/* normal priority */
#define SNDRV_SEQ_PRIORITY_HIGH		(1<<4)	/* event should be processed before others */
#define SNDRV_SEQ_PRIORITY_MASK		(1<<4)


	/* note event */
struct snd_seq_ev_note {
	unsigned char channel;
	unsigned char note;
	unsigned char velocity;
	unsigned char off_velocity;	/* only for SNDRV_SEQ_EVENT_NOTE */
	unsigned int duration;		/* only for SNDRV_SEQ_EVENT_NOTE */
};

	/* controller event */
struct snd_seq_ev_ctrl {
	unsigned char channel;
	unsigned char unused1, unused2, unused3;	/* pad */
	unsigned int param;
	signed int value;
};

	/* generic set of bytes (12x8 bit) */
struct snd_seq_ev_raw8 {
	unsigned char d[12];	/* 8 bit value */
};

	/* generic set of integers (3x32 bit) */
struct snd_seq_ev_raw32 {
	unsigned int d[3];	/* 32 bit value */
};

	/* external stored data */
struct snd_seq_ev_ext {
	unsigned int len;	/* length of data */
	void *ptr;		/* pointer to data (note: maybe 64-bit) */
} __attribute__((packed));

/* Instrument cluster type */
typedef unsigned int snd_seq_instr_cluster_t;

/* Instrument type */
struct snd_seq_instr {
	snd_seq_instr_cluster_t cluster;
	unsigned int std;		/* the upper byte means a private instrument (owner - client #) */
	unsigned short bank;
	unsigned short prg;
};

	/* sample number */
struct snd_seq_ev_sample {
	unsigned int std;
	unsigned short bank;
	unsigned short prg;
};

	/* sample cluster */
struct snd_seq_ev_cluster {
	snd_seq_instr_cluster_t cluster;
};

	/* sample position */
typedef unsigned int snd_seq_position_t; /* playback position (in samples) * 16 */

	/* sample stop mode */
enum {
	SAMPLE_STOP_IMMEDIATELY = 0,	/* terminate playing immediately */
	SAMPLE_STOP_VENVELOPE = 1,	/* finish volume envelope */
	SAMPLE_STOP_LOOP = 2		/* terminate loop and finish wave */
};

	/* sample frequency */
typedef int snd_seq_frequency_t; /* playback frequency in HZ * 16 */

	/* sample volume control; if any value is set to -1 == do not change */
struct snd_seq_ev_volume {
	signed short volume;	/* range: 0-16383 */
	signed short lr;	/* left-right balance; range: 0-16383 */
	signed short fr;	/* front-rear balance; range: 0-16383 */
	signed short du;	/* down-up balance; range: 0-16383 */
};

	/* simple loop redefinition */
struct snd_seq_ev_loop {
	unsigned int start;	/* loop start (in samples) * 16 */
	unsigned int end;	/* loop end (in samples) * 16 */
};

struct snd_seq_ev_sample_control {
	unsigned char channel;
	unsigned char unused1, unused2, unused3;	/* pad */
	union {
		struct snd_seq_ev_sample sample;
		struct snd_seq_ev_cluster cluster;
		snd_seq_position_t position;
		int stop_mode;
		snd_seq_frequency_t frequency;
		struct snd_seq_ev_volume volume;
		struct snd_seq_ev_loop loop;
		unsigned char raw8[8];
	} param;
};



/* INSTR_BEGIN event */
struct snd_seq_ev_instr_begin {
	int timeout;		/* zero = forever, otherwise timeout in ms */
};

struct snd_seq_result {
	int event;		/* processed event type */
	int result;
};


struct snd_seq_real_time {
	unsigned int tv_sec;	/* seconds */
	unsigned int tv_nsec;	/* nanoseconds */
};

typedef unsigned int snd_seq_tick_time_t;	/* midi ticks */

union snd_seq_timestamp {
	snd_seq_tick_time_t tick;
	struct snd_seq_real_time time;
};

struct snd_seq_queue_skew {
	unsigned int value;
	unsigned int base;
};

	/* queue timer control */
struct snd_seq_ev_queue_control {
	unsigned char queue;			/* affected queue */
	unsigned char pad[3];			/* reserved */
	union {
		signed int value;		/* affected value (e.g. tempo) */
		union snd_seq_timestamp time;	/* time */
		unsigned int position;		/* sync position */
		struct snd_seq_queue_skew skew;
		unsigned int d32[2];
		unsigned char d8[8];
	} param;
};

	/* quoted event - inside the kernel only */
struct snd_seq_ev_quote {
	struct snd_seq_addr origin;		/* original sender */
	unsigned short value;		/* optional data */
	struct snd_seq_event *event;		/* quoted event */
} __attribute__((packed));


	/* sequencer event */
struct snd_seq_event {
	snd_seq_event_type_t type;	/* event type */
	unsigned char flags;		/* event flags */
	char tag;
	
	unsigned char queue;		/* schedule queue */
	union snd_seq_timestamp time;	/* schedule time */


	struct snd_seq_addr source;	/* source address */
	struct snd_seq_addr dest;	/* destination address */

	union {				/* event data... */
		struct snd_seq_ev_note note;
		struct snd_seq_ev_ctrl control;
		struct snd_seq_ev_raw8 raw8;
		struct snd_seq_ev_raw32 raw32;
		struct snd_seq_ev_ext ext;
		struct snd_seq_ev_queue_control queue;
		union snd_seq_timestamp time;
		struct snd_seq_addr addr;
		struct snd_seq_connect connect;
		struct snd_seq_result result;
		struct snd_seq_ev_instr_begin instr_begin;
		struct snd_seq_ev_sample_control sample;
		struct snd_seq_ev_quote quote;
	} data;
};


/*
 * bounce event - stored as variable size data
 */
struct snd_seq_event_bounce {
	int err;
	struct snd_seq_event event;
	/* external data follows here. */
};

#ifdef __KERNEL__

/* helper macro */
#define snd_seq_event_bounce_ext_data(ev) ((void*)((char *)(ev)->data.ext.ptr + sizeof(struct snd_seq_event_bounce)))

/*
 * type check macros
 */
/* result events: 0-4 */
#define snd_seq_ev_is_result_type(ev)	((ev)->type < 5)
/* channel specific events: 5-19 */
#define snd_seq_ev_is_channel_type(ev)	((ev)->type >= 5 && (ev)->type < 20)
/* note events: 5-9 */
#define snd_seq_ev_is_note_type(ev)	((ev)->type >= 5 && (ev)->type < 10)
/* control events: 10-19 */
#define snd_seq_ev_is_control_type(ev)	((ev)->type >= 10 && (ev)->type < 20)
/* queue control events: 30-39 */
#define snd_seq_ev_is_queue_type(ev)	((ev)->type >= 30 && (ev)->type < 40)
/* system status messages */
#define snd_seq_ev_is_message_type(ev)	((ev)->type >= 60 && (ev)->type < 69)
/* sample messages */
#define snd_seq_ev_is_sample_type(ev)	((ev)->type >= 70 && (ev)->type < 79)
/* user-defined messages */
#define snd_seq_ev_is_user_type(ev)	((ev)->type >= 90 && (ev)->type < 99)
/* fixed length events: 0-99 */
#define snd_seq_ev_is_fixed_type(ev)	((ev)->type < 100)
/* instrument layer events: 100-129 */
#define snd_seq_ev_is_instr_type(ev)	((ev)->type >= 100 && (ev)->type < 130)
/* variable length events: 130-139 */
#define snd_seq_ev_is_variable_type(ev)	((ev)->type >= 130 && (ev)->type < 140)
/* reserved for kernel */
#define snd_seq_ev_is_reserved(ev)	((ev)->type >= 150)

/* direct dispatched events */
#define snd_seq_ev_is_direct(ev)	((ev)->queue == SNDRV_SEQ_QUEUE_DIRECT)

/*
 * macros to check event flags
 */
/* prior events */
#define snd_seq_ev_is_prior(ev)		(((ev)->flags & SNDRV_SEQ_PRIORITY_MASK) == SNDRV_SEQ_PRIORITY_HIGH)

/* event length type */
#define snd_seq_ev_length_type(ev)	((ev)->flags & SNDRV_SEQ_EVENT_LENGTH_MASK)
#define snd_seq_ev_is_fixed(ev)		(snd_seq_ev_length_type(ev) == SNDRV_SEQ_EVENT_LENGTH_FIXED)
#define snd_seq_ev_is_variable(ev)	(snd_seq_ev_length_type(ev) == SNDRV_SEQ_EVENT_LENGTH_VARIABLE)
#define snd_seq_ev_is_varusr(ev)	(snd_seq_ev_length_type(ev) == SNDRV_SEQ_EVENT_LENGTH_VARUSR)

/* time-stamp type */
#define snd_seq_ev_timestamp_type(ev)	((ev)->flags & SNDRV_SEQ_TIME_STAMP_MASK)
#define snd_seq_ev_is_tick(ev)		(snd_seq_ev_timestamp_type(ev) == SNDRV_SEQ_TIME_STAMP_TICK)
#define snd_seq_ev_is_real(ev)		(snd_seq_ev_timestamp_type(ev) == SNDRV_SEQ_TIME_STAMP_REAL)

/* time-mode type */
#define snd_seq_ev_timemode_type(ev)	((ev)->flags & SNDRV_SEQ_TIME_MODE_MASK)
#define snd_seq_ev_is_abstime(ev)	(snd_seq_ev_timemode_type(ev) == SNDRV_SEQ_TIME_MODE_ABS)
#define snd_seq_ev_is_reltime(ev)	(snd_seq_ev_timemode_type(ev) == SNDRV_SEQ_TIME_MODE_REL)

/* queue sync port */
#define snd_seq_queue_sync_port(q)	((q) + 16)

#endif /* __KERNEL__ */

	/* system information */
struct snd_seq_system_info {
	int queues;			/* maximum queues count */
	int clients;			/* maximum clients count */
	int ports;			/* maximum ports per client */
	int channels;			/* maximum channels per port */
	int cur_clients;		/* current clients */
	int cur_queues;			/* current queues */
	char reserved[24];
};


	/* system running information */
struct snd_seq_running_info {
	unsigned char client;		/* client id */
	unsigned char big_endian;	/* 1 = big-endian */
	unsigned char cpu_mode;		/* 4 = 32bit, 8 = 64bit */
	unsigned char pad;		/* reserved */
	unsigned char reserved[12];
};


	/* known client numbers */
#define SNDRV_SEQ_CLIENT_SYSTEM		0
	/* internal client numbers */
#define SNDRV_SEQ_CLIENT_DUMMY		14	/* midi through */
#define SNDRV_SEQ_CLIENT_OSS		15	/* oss sequencer emulator */


	/* client types */
typedef int __bitwise snd_seq_client_type_t;
#define	NO_CLIENT	((__force snd_seq_client_type_t) 0)
#define	USER_CLIENT	((__force snd_seq_client_type_t) 1)
#define	KERNEL_CLIENT	((__force snd_seq_client_type_t) 2)
                        
	/* event filter flags */
#define SNDRV_SEQ_FILTER_BROADCAST	(1<<0)	/* accept broadcast messages */
#define SNDRV_SEQ_FILTER_MULTICAST	(1<<1)	/* accept multicast messages */
#define SNDRV_SEQ_FILTER_BOUNCE		(1<<2)	/* accept bounce event in error */
#define SNDRV_SEQ_FILTER_USE_EVENT	(1<<31)	/* use event filter */

struct snd_seq_client_info {
	int client;			/* client number to inquire */
	snd_seq_client_type_t type;	/* client type */
	char name[64];			/* client name */
	unsigned int filter;		/* filter flags */
	unsigned char multicast_filter[8]; /* multicast filter bitmap */
	unsigned char event_filter[32];	/* event filter bitmap */
	int num_ports;			/* RO: number of ports */
	int event_lost;			/* number of lost events */
	char reserved[64];		/* for future use */
};


/* client pool size */
struct snd_seq_client_pool {
	int client;			/* client number to inquire */
	int output_pool;		/* outgoing (write) pool size */
	int input_pool;			/* incoming (read) pool size */
	int output_room;		/* minimum free pool size for select/blocking mode */
	int output_free;		/* unused size */
	int input_free;			/* unused size */
	char reserved[64];
};


/* Remove events by specified criteria */

#define SNDRV_SEQ_REMOVE_INPUT		(1<<0)	/* Flush input queues */
#define SNDRV_SEQ_REMOVE_OUTPUT		(1<<1)	/* Flush output queues */
#define SNDRV_SEQ_REMOVE_DEST		(1<<2)	/* Restrict by destination q:client:port */
#define SNDRV_SEQ_REMOVE_DEST_CHANNEL	(1<<3)	/* Restrict by channel */
#define SNDRV_SEQ_REMOVE_TIME_BEFORE	(1<<4)	/* Restrict to before time */
#define SNDRV_SEQ_REMOVE_TIME_AFTER	(1<<5)	/* Restrict to time or after */
#define SNDRV_SEQ_REMOVE_TIME_TICK	(1<<6)	/* Time is in ticks */
#define SNDRV_SEQ_REMOVE_EVENT_TYPE	(1<<7)	/* Restrict to event type */
#define SNDRV_SEQ_REMOVE_IGNORE_OFF 	(1<<8)	/* Do not flush off events */
#define SNDRV_SEQ_REMOVE_TAG_MATCH 	(1<<9)	/* Restrict to events with given tag */

struct snd_seq_remove_events {
	unsigned int  remove_mode;	/* Flags that determine what gets removed */

	union snd_seq_timestamp time;

	unsigned char queue;	/* Queue for REMOVE_DEST */
	struct snd_seq_addr dest;	/* Address for REMOVE_DEST */
	unsigned char channel;	/* Channel for REMOVE_DEST */

	int  type;	/* For REMOVE_EVENT_TYPE */
	char  tag;	/* Tag for REMOVE_TAG */

	int  reserved[10];	/* To allow for future binary compatibility */

};


	/* known port numbers */
#define SNDRV_SEQ_PORT_SYSTEM_TIMER	0
#define SNDRV_SEQ_PORT_SYSTEM_ANNOUNCE	1

	/* port capabilities (32 bits) */
#define SNDRV_SEQ_PORT_CAP_READ		(1<<0)	/* readable from this port */
#define SNDRV_SEQ_PORT_CAP_WRITE	(1<<1)	/* writable to this port */

#define SNDRV_SEQ_PORT_CAP_SYNC_READ	(1<<2)
#define SNDRV_SEQ_PORT_CAP_SYNC_WRITE	(1<<3)

#define SNDRV_SEQ_PORT_CAP_DUPLEX	(1<<4)

#define SNDRV_SEQ_PORT_CAP_SUBS_READ	(1<<5)	/* allow read subscription */
#define SNDRV_SEQ_PORT_CAP_SUBS_WRITE	(1<<6)	/* allow write subscription */
#define SNDRV_SEQ_PORT_CAP_NO_EXPORT	(1<<7)	/* routing not allowed */

	/* port type */
#define SNDRV_SEQ_PORT_TYPE_SPECIFIC	(1<<0)	/* hardware specific */
#define SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC (1<<1)	/* generic MIDI device */
#define SNDRV_SEQ_PORT_TYPE_MIDI_GM	(1<<2)	/* General MIDI compatible device */
#define SNDRV_SEQ_PORT_TYPE_MIDI_GS	(1<<3)	/* GS compatible device */
#define SNDRV_SEQ_PORT_TYPE_MIDI_XG	(1<<4)	/* XG compatible device */
#define SNDRV_SEQ_PORT_TYPE_MIDI_MT32	(1<<5)	/* MT-32 compatible device */
#define SNDRV_SEQ_PORT_TYPE_MIDI_GM2	(1<<6)	/* General MIDI 2 compatible device */

/* other standards...*/
#define SNDRV_SEQ_PORT_TYPE_SYNTH	(1<<10)	/* Synth device (no MIDI compatible - direct wavetable) */
#define SNDRV_SEQ_PORT_TYPE_DIRECT_SAMPLE (1<<11)	/* Sampling device (support sample download) */
#define SNDRV_SEQ_PORT_TYPE_SAMPLE	(1<<12)	/* Sampling device (sample can be downloaded at any time) */
/*...*/
#define SNDRV_SEQ_PORT_TYPE_HARDWARE	(1<<16)	/* driver for a hardware device */
#define SNDRV_SEQ_PORT_TYPE_SOFTWARE	(1<<17)	/* implemented in software */
#define SNDRV_SEQ_PORT_TYPE_SYNTHESIZER	(1<<18)	/* generates sound */
#define SNDRV_SEQ_PORT_TYPE_PORT	(1<<19)	/* connects to other device(s) */
#define SNDRV_SEQ_PORT_TYPE_APPLICATION	(1<<20)	/* application (sequencer/editor) */

/* misc. conditioning flags */
#define SNDRV_SEQ_PORT_FLG_GIVEN_PORT	(1<<0)
#define SNDRV_SEQ_PORT_FLG_TIMESTAMP	(1<<1)
#define SNDRV_SEQ_PORT_FLG_TIME_REAL	(1<<2)

struct snd_seq_port_info {
	struct snd_seq_addr addr;	/* client/port numbers */
	char name[64];			/* port name */

	unsigned int capability;	/* port capability bits */
	unsigned int type;		/* port type bits */
	int midi_channels;		/* channels per MIDI port */
	int midi_voices;		/* voices per MIDI port */
	int synth_voices;		/* voices per SYNTH port */

	int read_use;			/* R/O: subscribers for output (from this port) */
	int write_use;			/* R/O: subscribers for input (to this port) */

	void *kernel;			/* reserved for kernel use (must be NULL) */
	unsigned int flags;		/* misc. conditioning */
	unsigned char time_queue;	/* queue # for timestamping */
	char reserved[59];		/* for future use */
};


/* queue flags */
#define SNDRV_SEQ_QUEUE_FLG_SYNC	(1<<0)	/* sync enabled */

/* queue information */
struct snd_seq_queue_info {
	int queue;		/* queue id */

	/*
	 *  security settings, only owner of this queue can start/stop timer
	 *  etc. if the queue is locked for other clients
	 */
	int owner;		/* client id for owner of the queue */
	unsigned locked:1;	/* timing queue locked for other queues */
	char name[64];		/* name of this queue */
	unsigned int flags;	/* flags */
	char reserved[60];	/* for future use */

};

/* queue info/status */
struct snd_seq_queue_status {
	int queue;			/* queue id */
	int events;			/* read-only - queue size */
	snd_seq_tick_time_t tick;	/* current tick */
	struct snd_seq_real_time time;	/* current time */
	int running;			/* running state of queue */
	int flags;			/* various flags */
	char reserved[64];		/* for the future */
};


/* queue tempo */
struct snd_seq_queue_tempo {
	int queue;			/* sequencer queue */
	unsigned int tempo;		/* current tempo, us/tick */
	int ppq;			/* time resolution, ticks/quarter */
	unsigned int skew_value;	/* queue skew */
	unsigned int skew_base;		/* queue skew base */
	char reserved[24];		/* for the future */
};


/* sequencer timer sources */
#define SNDRV_SEQ_TIMER_ALSA		0	/* ALSA timer */
#define SNDRV_SEQ_TIMER_MIDI_CLOCK	1	/* Midi Clock (CLOCK event) */
#define SNDRV_SEQ_TIMER_MIDI_TICK	2	/* Midi Timer Tick (TICK event) */

/* queue timer info */
struct snd_seq_queue_timer {
	int queue;			/* sequencer queue */
	int type;			/* source timer type */
	union {
		struct {
			struct snd_timer_id id;	/* ALSA's timer ID */
			unsigned int resolution;	/* resolution in Hz */
		} alsa;
	} u;
	char reserved[64];		/* for the future use */
};


struct snd_seq_queue_client {
	int queue;		/* sequencer queue */
	int client;		/* sequencer client */
	int used;		/* queue is used with this client
				   (must be set for accepting events) */
	/* per client watermarks */
	char reserved[64];	/* for future use */
};


#define SNDRV_SEQ_PORT_SUBS_EXCLUSIVE	(1<<0)	/* exclusive connection */
#define SNDRV_SEQ_PORT_SUBS_TIMESTAMP	(1<<1)
#define SNDRV_SEQ_PORT_SUBS_TIME_REAL	(1<<2)

struct snd_seq_port_subscribe {
	struct snd_seq_addr sender;	/* sender address */
	struct snd_seq_addr dest;	/* destination address */
	unsigned int voices;		/* number of voices to be allocated (0 = don't care) */
	unsigned int flags;		/* modes */
	unsigned char queue;		/* input time-stamp queue (optional) */
	unsigned char pad[3];		/* reserved */
	char reserved[64];
};

/* type of query subscription */
#define SNDRV_SEQ_QUERY_SUBS_READ	0
#define SNDRV_SEQ_QUERY_SUBS_WRITE	1

struct snd_seq_query_subs {
	struct snd_seq_addr root;	/* client/port id to be searched */
	int type;		/* READ or WRITE */
	int index;		/* 0..N-1 */
	int num_subs;		/* R/O: number of subscriptions on this port */
	struct snd_seq_addr addr;	/* R/O: result */
	unsigned char queue;	/* R/O: result */
	unsigned int flags;	/* R/O: result */
	char reserved[64];	/* for future use */
};


/*
 *  Instrument abstraction layer
 *     - based on events
 */

/* instrument types */
#define SNDRV_SEQ_INSTR_ATYPE_DATA	0	/* instrument data */
#define SNDRV_SEQ_INSTR_ATYPE_ALIAS	1	/* instrument alias */

/* instrument ASCII identifiers */
#define SNDRV_SEQ_INSTR_ID_DLS1		"DLS1"
#define SNDRV_SEQ_INSTR_ID_DLS2		"DLS2"
#define SNDRV_SEQ_INSTR_ID_SIMPLE	"Simple Wave"
#define SNDRV_SEQ_INSTR_ID_SOUNDFONT	"SoundFont"
#define SNDRV_SEQ_INSTR_ID_GUS_PATCH	"GUS Patch"
#define SNDRV_SEQ_INSTR_ID_INTERWAVE	"InterWave FFFF"
#define SNDRV_SEQ_INSTR_ID_OPL2_3	"OPL2/3 FM"
#define SNDRV_SEQ_INSTR_ID_OPL4		"OPL4"

/* instrument types */
#define SNDRV_SEQ_INSTR_TYPE0_DLS1	(1<<0)	/* MIDI DLS v1 */
#define SNDRV_SEQ_INSTR_TYPE0_DLS2	(1<<1)	/* MIDI DLS v2 */
#define SNDRV_SEQ_INSTR_TYPE1_SIMPLE	(1<<0)	/* Simple Wave */
#define SNDRV_SEQ_INSTR_TYPE1_SOUNDFONT	(1<<1)	/* EMU SoundFont */
#define SNDRV_SEQ_INSTR_TYPE1_GUS_PATCH	(1<<2)	/* Gravis UltraSound Patch */
#define SNDRV_SEQ_INSTR_TYPE1_INTERWAVE	(1<<3)	/* InterWave FFFF */
#define SNDRV_SEQ_INSTR_TYPE2_OPL2_3	(1<<0)	/* Yamaha OPL2/3 FM */
#define SNDRV_SEQ_INSTR_TYPE2_OPL4	(1<<1)	/* Yamaha OPL4 */

/* put commands */
#define SNDRV_SEQ_INSTR_PUT_CMD_CREATE	0
#define SNDRV_SEQ_INSTR_PUT_CMD_REPLACE	1
#define SNDRV_SEQ_INSTR_PUT_CMD_MODIFY	2
#define SNDRV_SEQ_INSTR_PUT_CMD_ADD	3
#define SNDRV_SEQ_INSTR_PUT_CMD_REMOVE	4

/* get commands */
#define SNDRV_SEQ_INSTR_GET_CMD_FULL	0
#define SNDRV_SEQ_INSTR_GET_CMD_PARTIAL	1

/* query flags */
#define SNDRV_SEQ_INSTR_QUERY_FOLLOW_ALIAS (1<<0)

/* free commands */
#define SNDRV_SEQ_INSTR_FREE_CMD_ALL		0
#define SNDRV_SEQ_INSTR_FREE_CMD_PRIVATE	1
#define SNDRV_SEQ_INSTR_FREE_CMD_CLUSTER	2
#define SNDRV_SEQ_INSTR_FREE_CMD_SINGLE		3

/* size of ROM/RAM */
typedef unsigned int snd_seq_instr_size_t;

/* INSTR_INFO */

struct snd_seq_instr_info {
	int result;			/* operation result */
	unsigned int formats[8];	/* bitmap of supported formats */
	int ram_count;			/* count of RAM banks */
	snd_seq_instr_size_t ram_sizes[16]; /* size of RAM banks */
	int rom_count;			/* count of ROM banks */
	snd_seq_instr_size_t rom_sizes[8]; /* size of ROM banks */
	char reserved[128];
};

/* INSTR_STATUS */

struct snd_seq_instr_status {
	int result;			/* operation result */
	snd_seq_instr_size_t free_ram[16]; /* free RAM in banks */
	int instrument_count;		/* count of downloaded instruments */
	char reserved[128];
};

/* INSTR_FORMAT_INFO */

struct snd_seq_instr_format_info {
	char format[16];		/* format identifier - SNDRV_SEQ_INSTR_ID_* */	
	unsigned int len;		/* max data length (without this structure) */
};

struct snd_seq_instr_format_info_result {
	int result;			/* operation result */
	char format[16];		/* format identifier */
	unsigned int len;		/* filled data length (without this structure) */
};

/* instrument data */
struct snd_seq_instr_data {
	char name[32];			/* instrument name */
	char reserved[16];		/* for the future use */
	int type;			/* instrument type */
	union {
		char format[16];	/* format identifier */
		struct snd_seq_instr alias;
	} data;
};

/* INSTR_PUT/GET, data are stored in one block (extended), header + data */

struct snd_seq_instr_header {
	union {
		struct snd_seq_instr instr;
		snd_seq_instr_cluster_t cluster;
	} id;				/* instrument identifier */
	unsigned int cmd;		/* get/put/free command */
	unsigned int flags;		/* query flags (only for get) */
	unsigned int len;		/* real instrument data length (without header) */
	int result;			/* operation result */
	char reserved[16];		/* for the future */
	struct snd_seq_instr_data data; /* instrument data (for put/get result) */
};

/* INSTR_CLUSTER_SET */

struct snd_seq_instr_cluster_set {
	snd_seq_instr_cluster_t cluster; /* cluster identifier */
	char name[32];			/* cluster name */
	int priority;			/* cluster priority */
	char reserved[64];		/* for the future use */
};

/* INSTR_CLUSTER_GET */

struct snd_seq_instr_cluster_get {
	snd_seq_instr_cluster_t cluster; /* cluster identifier */
	char name[32];			/* cluster name */
	int priority;			/* cluster priority */
	char reserved[64];		/* for the future use */
};

/*
 *  IOCTL commands
 */

#define SNDRV_SEQ_IOCTL_PVERSION	_IOR ('S', 0x00, int)
#define SNDRV_SEQ_IOCTL_CLIENT_ID	_IOR ('S', 0x01, int)
#define SNDRV_SEQ_IOCTL_SYSTEM_INFO	_IOWR('S', 0x02, struct snd_seq_system_info)
#define SNDRV_SEQ_IOCTL_RUNNING_MODE	_IOWR('S', 0x03, struct snd_seq_running_info)

#define SNDRV_SEQ_IOCTL_GET_CLIENT_INFO	_IOWR('S', 0x10, struct snd_seq_client_info)
#define SNDRV_SEQ_IOCTL_SET_CLIENT_INFO	_IOW ('S', 0x11, struct snd_seq_client_info)

#define SNDRV_SEQ_IOCTL_CREATE_PORT	_IOWR('S', 0x20, struct snd_seq_port_info)
#define SNDRV_SEQ_IOCTL_DELETE_PORT	_IOW ('S', 0x21, struct snd_seq_port_info)
#define SNDRV_SEQ_IOCTL_GET_PORT_INFO	_IOWR('S', 0x22, struct snd_seq_port_info)
#define SNDRV_SEQ_IOCTL_SET_PORT_INFO	_IOW ('S', 0x23, struct snd_seq_port_info)

#define SNDRV_SEQ_IOCTL_SUBSCRIBE_PORT	_IOW ('S', 0x30, struct snd_seq_port_subscribe)
#define SNDRV_SEQ_IOCTL_UNSUBSCRIBE_PORT _IOW ('S', 0x31, struct snd_seq_port_subscribe)

#define SNDRV_SEQ_IOCTL_CREATE_QUEUE	_IOWR('S', 0x32, struct snd_seq_queue_info)
#define SNDRV_SEQ_IOCTL_DELETE_QUEUE	_IOW ('S', 0x33, struct snd_seq_queue_info)
#define SNDRV_SEQ_IOCTL_GET_QUEUE_INFO	_IOWR('S', 0x34, struct snd_seq_queue_info)
#define SNDRV_SEQ_IOCTL_SET_QUEUE_INFO	_IOWR('S', 0x35, struct snd_seq_queue_info)
#define SNDRV_SEQ_IOCTL_GET_NAMED_QUEUE	_IOWR('S', 0x36, struct snd_seq_queue_info)
#define SNDRV_SEQ_IOCTL_GET_QUEUE_STATUS _IOWR('S', 0x40, struct snd_seq_queue_status)
#define SNDRV_SEQ_IOCTL_GET_QUEUE_TEMPO	_IOWR('S', 0x41, struct snd_seq_queue_tempo)
#define SNDRV_SEQ_IOCTL_SET_QUEUE_TEMPO	_IOW ('S', 0x42, struct snd_seq_queue_tempo)
#define SNDRV_SEQ_IOCTL_GET_QUEUE_OWNER	_IOWR('S', 0x43, struct snd_seq_queue_owner)
#define SNDRV_SEQ_IOCTL_SET_QUEUE_OWNER	_IOW ('S', 0x44, struct snd_seq_queue_owner)
#define SNDRV_SEQ_IOCTL_GET_QUEUE_TIMER	_IOWR('S', 0x45, struct snd_seq_queue_timer)
#define SNDRV_SEQ_IOCTL_SET_QUEUE_TIMER	_IOW ('S', 0x46, struct snd_seq_queue_timer)
/* XXX
#define SNDRV_SEQ_IOCTL_GET_QUEUE_SYNC	_IOWR('S', 0x53, struct snd_seq_queue_sync)
#define SNDRV_SEQ_IOCTL_SET_QUEUE_SYNC	_IOW ('S', 0x54, struct snd_seq_queue_sync)
*/
#define SNDRV_SEQ_IOCTL_GET_QUEUE_CLIENT	_IOWR('S', 0x49, struct snd_seq_queue_client)
#define SNDRV_SEQ_IOCTL_SET_QUEUE_CLIENT	_IOW ('S', 0x4a, struct snd_seq_queue_client)
#define SNDRV_SEQ_IOCTL_GET_CLIENT_POOL	_IOWR('S', 0x4b, struct snd_seq_client_pool)
#define SNDRV_SEQ_IOCTL_SET_CLIENT_POOL	_IOW ('S', 0x4c, struct snd_seq_client_pool)
#define SNDRV_SEQ_IOCTL_REMOVE_EVENTS	_IOW ('S', 0x4e, struct snd_seq_remove_events)
#define SNDRV_SEQ_IOCTL_QUERY_SUBS	_IOWR('S', 0x4f, struct snd_seq_query_subs)
#define SNDRV_SEQ_IOCTL_GET_SUBSCRIPTION	_IOWR('S', 0x50, struct snd_seq_port_subscribe)
#define SNDRV_SEQ_IOCTL_QUERY_NEXT_CLIENT	_IOWR('S', 0x51, struct snd_seq_client_info)
#define SNDRV_SEQ_IOCTL_QUERY_NEXT_PORT	_IOWR('S', 0x52, struct snd_seq_port_info)

#endif /* __SOUND_ASEQUENCER_H */
