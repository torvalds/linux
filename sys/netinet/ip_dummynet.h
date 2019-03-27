/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998-2010 Luigi Rizzo, Universita` di Pisa
 * Portions Copyright (c) 2000 Akamba Corp.
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _IP_DUMMYNET_H
#define _IP_DUMMYNET_H
#define NEW_AQM
/*
 * Definition of the kernel-userland API for dummynet.
 *
 * Setsockopt() and getsockopt() pass a batch of objects, each
 * of them starting with a "struct dn_id" which should fully identify
 * the object and its relation with others in the sequence.
 * The first object in each request should have
 *	 type= DN_CMD_*, id = DN_API_VERSION.
 * For other objects, type and subtype specify the object, len indicates
 * the total length including the header, and 'id' identifies the specific
 * object.
 *
 * Most objects are numbered with an identifier in the range 1..65535.
 * DN_MAX_ID indicates the first value outside the range.
 */

#define	DN_API_VERSION	12500000
#define	DN_MAX_ID	0x10000

struct dn_id {
	uint16_t	len;	/* total obj len including this header */
	uint8_t		type;
	uint8_t		subtype;
	uint32_t	id;	/* generic id */
};

/*
 * These values are in the type field of struct dn_id.
 * To preserve the ABI, never rearrange the list or delete
 * entries with the exception of DN_LAST
 */
enum {
	DN_NONE = 0,
	DN_LINK = 1,
	DN_FS,
	DN_SCH,
	DN_SCH_I,
	DN_QUEUE,
	DN_DELAY_LINE,
	DN_PROFILE,
	DN_FLOW,		/* struct dn_flow */
	DN_TEXT,		/* opaque text is the object */

	DN_CMD_CONFIG = 0x80,	/* objects follow */
	DN_CMD_DELETE,		/* subtype + list of entries */
	DN_CMD_GET,		/* subtype + list of entries */
	DN_CMD_FLUSH,
	/* for compatibility with FreeBSD 7.2/8 */
	DN_COMPAT_PIPE,
	DN_COMPAT_QUEUE,
	DN_GET_COMPAT,

	/* special commands for emulation of sysctl variables */
	DN_SYSCTL_GET,
	DN_SYSCTL_SET,
#ifdef NEW_AQM
	/* subtypes used for setting/getting extra parameters.
	 * these subtypes used with IP_DUMMYNET3 command (get)
	 * and DN_TEXT (set). */
	DN_AQM_PARAMS, /* AQM extra params */
	DN_SCH_PARAMS, /* scheduler extra params */
#endif
	DN_LAST,
};

enum { /* subtype for schedulers, flowset and the like */
	DN_SCHED_UNKNOWN = 0,
	DN_SCHED_FIFO = 1,
	DN_SCHED_WF2QP = 2,
	/* others are in individual modules */
};

enum {	/* user flags */
	DN_HAVE_MASK	= 0x0001,	/* fs or sched has a mask */
	DN_NOERROR	= 0x0002,	/* do not report errors */
	DN_QHT_HASH	= 0x0004,	/* qht is a hash table */
	DN_QSIZE_BYTES	= 0x0008,	/* queue size is in bytes */
	DN_HAS_PROFILE	= 0x0010,	/* a link has a profile */
	DN_IS_RED	= 0x0020,
	DN_IS_GENTLE_RED= 0x0040,
	DN_IS_ECN	= 0x0080,
	#ifdef NEW_AQM
	DN_IS_AQM = 0x0100,     /* AQMs: e.g Codel & PIE */
	#endif
	DN_PIPE_CMD	= 0x1000,	/* pipe config... */
};

/*
 * link template.
 */
struct dn_link {
	struct dn_id oid;

	/*
	 * Userland sets bw and delay in bits/s and milliseconds.
	 * The kernel converts this back and forth to bits/tick and ticks.
	 * XXX what about burst ?
	 */
	int32_t		link_nr;
	int		bandwidth;	/* bit/s or bits/tick.   */
	int		delay;		/* ms and ticks */
	uint64_t	burst;		/* scaled. bits*Hz  XXX */
};

/*
 * A flowset, which is a template for flows. Contains parameters
 * from the command line: id, target scheduler, queue sizes, plr,
 * flow masks, buckets for the flow hash, and possibly scheduler-
 * specific parameters (weight, quantum and so on).
 */
struct dn_fs {
	struct dn_id oid;
	uint32_t fs_nr;		/* the flowset number */
	uint32_t flags;		/* userland flags */
	int qsize;		/* queue size in slots or bytes */
	int32_t plr;		/* PLR, pkt loss rate (2^31-1 means 100%) */
	uint32_t buckets;	/* buckets used for the queue hash table */

	struct ipfw_flow_id flow_mask;
	uint32_t sched_nr;	/* the scheduler we attach to */
	/* generic scheduler parameters. Leave them at -1 if unset.
	 * Now we use 0: weight, 1: lmax, 2: priority
	 */
	int par[4];

	/* RED/GRED parameters.
	 * weight and probabilities are in the range 0..1 represented
	 * in fixed point arithmetic with SCALE_RED decimal bits.
	 */
#define SCALE_RED	16
#define SCALE(x)	( (x) << SCALE_RED )
#define SCALE_VAL(x)	( (x) >> SCALE_RED )
#define SCALE_MUL(x,y)	( ( (x) * (y) ) >> SCALE_RED )
	int w_q ;		/* queue weight (scaled) */
	int max_th ;		/* maximum threshold for queue (scaled) */
	int min_th ;		/* minimum threshold for queue (scaled) */
	int max_p ;		/* maximum value for p_b (scaled) */

};

/*
 * dn_flow collects flow_id and stats for queues and scheduler
 * instances, and is used to pass these info to userland.
 * oid.type/oid.subtype describe the object, oid.id is number
 * of the parent object.
 */
struct dn_flow {
	struct dn_id	oid;
	struct ipfw_flow_id fid;
	uint64_t	tot_pkts; /* statistics counters  */
	uint64_t	tot_bytes;
	uint32_t	length; /* Queue length, in packets */
	uint32_t	len_bytes; /* Queue length, in bytes */
	uint32_t	drops;
};


/*
 * Scheduler template, mostly indicating the name, number,
 * sched_mask and buckets.
 */
struct dn_sch {
	struct dn_id	oid;
	uint32_t	sched_nr; /* N, scheduler number */
	uint32_t	buckets; /* number of buckets for the instances */
	uint32_t	flags;	/* have_mask, ... */

	char name[16];	/* null terminated */
	/* mask to select the appropriate scheduler instance */
	struct ipfw_flow_id sched_mask; /* M */
};


/* A delay profile is attached to a link.
 * Note that a profile, as any other object, cannot be longer than 2^16
 */
#define	ED_MAX_SAMPLES_NO	1024
struct dn_profile {
	struct dn_id	oid;
	/* fields to simulate a delay profile */
#define ED_MAX_NAME_LEN		32
	char	name[ED_MAX_NAME_LEN];
	int	link_nr;
	int	loss_level;
	int	bandwidth;			// XXX use link bandwidth?
	int	samples_no;			/* actual len of samples[] */
	int	samples[ED_MAX_SAMPLES_NO];	/* may be shorter */
};

#ifdef NEW_AQM
/* Extra parameters for AQM and scheduler.
 * This struct is used to pass and retrieve parameters (configurations)
 * to/from AQM and Scheduler.
 */
struct dn_extra_parms {
	struct dn_id oid;
	char name[16];
	uint32_t nr;
#define DN_MAX_EXTRA_PARM	10
	int64_t par[DN_MAX_EXTRA_PARM];
};
#endif

/*
 * Overall structure of dummynet

In dummynet, packets are selected with the firewall rules, and passed
to two different objects: PIPE or QUEUE (bad name).

A QUEUE defines a classifier, which groups packets into flows
according to a 'mask', puts them into independent queues (one
per flow) with configurable size and queue management policy,
and passes flows to a scheduler:

                 (flow_mask|sched_mask)  sched_mask
	 +---------+   weight Wx  +-------------+
         |         |->-[flow]-->--|             |-+
    -->--| QUEUE x |   ...        |             | |
         |         |->-[flow]-->--| SCHEDuler N | |
	 +---------+              |             | |
	     ...                  |             +--[LINK N]-->--
	 +---------+   weight Wy  |             | +--[LINK N]-->--
         |         |->-[flow]-->--|             | |
    -->--| QUEUE y |   ...        |             | |
         |         |->-[flow]-->--|             | |
	 +---------+              +-------------+ |
	                            +-------------+

Many QUEUE objects can connect to the same scheduler, each
QUEUE object can have its own set of parameters.

In turn, the SCHEDuler 'forks' multiple instances according
to a 'sched_mask', each instance manages its own set of queues
and transmits on a private instance of a configurable LINK.

A PIPE is a simplified version of the above, where there
is no flow_mask, and each scheduler instance handles a single queue.

The following data structures (visible from userland) describe
the objects used by dummynet:

 + dn_link, contains the main configuration parameters related
   to delay and bandwidth;
 + dn_profile describes a delay profile;
 + dn_flow describes the flow status (flow id, statistics)
   
 + dn_sch describes a scheduler
 + dn_fs describes a flowset (msk, weight, queue parameters)

 *
 */

#endif /* _IP_DUMMYNET_H */
