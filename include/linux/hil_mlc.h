/*
 * HP Human Interface Loop Master Link Controller driver.
 *
 * Copyright (c) 2001 Brian S. Julin
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *
 * References:
 * HP-HIL Technical Reference Manual.  Hewlett Packard Product No. 45918A
 *
 */

#include <linux/hil.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/semaphore.h>
#include <linux/serio.h>
#include <linux/list.h>

typedef struct hil_mlc hil_mlc;

/* The HIL has a complicated state engine.
 * We define the structure of nodes in the state engine here.
 */
enum hilse_act {
  	/* HILSE_OUT prepares to receive input if the next node
	 * is an IN or EXPECT, and then sends the given packet.
	 */
	HILSE_OUT = 0,

  	/* HILSE_CTS checks if the loop is busy. */
	HILSE_CTS,

	/* HILSE_OUT_LAST sends the given command packet to 
	 * the last configured/running device on the loop.
	 */
	HILSE_OUT_LAST,

	/* HILSE_OUT_DISC sends the given command packet to
	 * the next device past the last configured/running one.
	 */
	HILSE_OUT_DISC,

	/* HILSE_FUNC runs a callback function with given arguments.
	 * a positive return value causes the "ugly" branch to be taken.
	 */
	HILSE_FUNC,

  	/* HILSE_IN simply expects any non-errored packet to arrive 
	 * within arg usecs.
	 */
	HILSE_IN		= 0x100,

  	/* HILSE_EXPECT expects a particular packet to arrive 
	 * within arg usecs, any other packet is considered an error.
	 */
	HILSE_EXPECT,

  	/* HILSE_EXPECT_LAST as above but dev field should be last 
	 * discovered/operational device.
	 */
	HILSE_EXPECT_LAST,

  	/* HILSE_EXPECT_LAST as above but dev field should be first 
	 * undiscovered/inoperational device.
	 */
	HILSE_EXPECT_DISC
};

typedef int	(hilse_func) (hil_mlc *mlc, int arg);
struct hilse_node {
	enum hilse_act		act;	/* How to process this node         */
	union {
		hilse_func	*func;	/* Function to call if HILSE_FUNC   */
		hil_packet	packet;	/* Packet to send or to compare     */
	} object;
	int			arg;	/* Timeout in usec or parm for func */
	int			good;	/* Node to jump to on success       */
	int			bad;	/* Node to jump to on error         */
	int			ugly;	/* Node to jump to on timeout       */
};

/* Methods for back-end drivers, e.g. hp_sdc_mlc */
typedef int	(hil_mlc_cts) (hil_mlc *mlc);
typedef int	(hil_mlc_out) (hil_mlc *mlc);
typedef int	(hil_mlc_in)  (hil_mlc *mlc, suseconds_t timeout);

struct hil_mlc_devinfo {
	uint8_t	idd[16];	/* Device ID Byte and Describe Record */
	uint8_t	rsc[16];	/* Security Code Header and Record */
	uint8_t	exd[16];	/* Extended Describe Record */
	uint8_t	rnm[16];	/* Device name as returned by RNM command */
};

struct hil_mlc_serio_map {
	hil_mlc *mlc;
	int di_revmap;
	int didx;
};

/* How many (possibly old/detached) devices the we try to keep track of */
#define HIL_MLC_DEVMEM 16

struct hil_mlc {
	struct list_head	list;	/* hil_mlc is organized as linked list */

	rwlock_t		lock;

	void *priv; /* Data specific to a particular type of MLC */

	int 			seidx;	/* Current node in state engine */
	int			istarted, ostarted;

	hil_mlc_cts		*cts;
	struct semaphore	csem;   /* Raised when loop idle */

	hil_mlc_out		*out;
	struct semaphore	osem;   /* Raised when outpacket dispatched */
	hil_packet		opacket;

	hil_mlc_in		*in;
	struct semaphore	isem;   /* Raised when a packet arrives */
	hil_packet		ipacket[16];
	hil_packet		imatch;
	int			icount;
	unsigned long		instart;
	unsigned long		intimeout;

	int			ddi;	/* Last operational device id */
	int			lcv;	/* LCV to throttle loops */
	time64_t		lcv_time; /* Time loop was started */

	int			di_map[7]; /* Maps below items to live devs */
	struct hil_mlc_devinfo	di[HIL_MLC_DEVMEM];
	struct serio		*serio[HIL_MLC_DEVMEM];
	struct hil_mlc_serio_map serio_map[HIL_MLC_DEVMEM];
	hil_packet		serio_opacket[HIL_MLC_DEVMEM];
	int			serio_oidx[HIL_MLC_DEVMEM];
	struct hil_mlc_devinfo	di_scratch; /* Temporary area */

	int			opercnt;

	struct tasklet_struct	*tasklet;
};

int hil_mlc_register(hil_mlc *mlc);
int hil_mlc_unregister(hil_mlc *mlc);
