/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1996-1999
 * Kazutaka YOKOTA (yokota@zodiac.mech.utsunomiya-u.ac.jp)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote 
 *    products derived from this software without specific prior written 
 *    permission.
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
 * from kbdio.h,v 1.8 1998/09/25 11:55:46 yokota Exp
 */

#ifndef _DEV_ATKBDC_ATKBDCREG_H_
#define	_DEV_ATKBDC_ATKBDCREG_H_

#include "opt_kbd.h"	/* Structures depend on the value if KBDIO_DEBUG */

/* constants */

/* I/O ports */
#define KBD_STATUS_PORT 	4	/* status port, read */
#define KBD_COMMAND_PORT	4	/* controller command port, write */
#define KBD_DATA_PORT		0	/* data port, read/write 
					 * also used as keyboard command
					 * and mouse command port 
					 */

/* controller commands (sent to KBD_COMMAND_PORT) */
#define KBDC_SET_COMMAND_BYTE 	0x0060
#define KBDC_GET_COMMAND_BYTE 	0x0020
#define KBDC_WRITE_TO_AUX_MUX	0x0090
#define KBDC_FORCE_AUX_OUTPUT	0x00d3
#define KBDC_WRITE_TO_AUX    	0x00d4
#define KBDC_DISABLE_AUX_PORT 	0x00a7
#define KBDC_ENABLE_AUX_PORT 	0x00a8
#define KBDC_TEST_AUX_PORT   	0x00a9
#define KBDC_DIAGNOSE	     	0x00aa
#define KBDC_TEST_KBD_PORT   	0x00ab
#define KBDC_DISABLE_KBD_PORT 	0x00ad
#define KBDC_ENABLE_KBD_PORT 	0x00ae

/* controller command byte (set by KBDC_SET_COMMAND_BYTE) */
#define KBD_TRANSLATION		0x0040
#define KBD_RESERVED_BITS	0x0004
#define KBD_OVERRIDE_KBD_LOCK	0x0008
#define KBD_ENABLE_KBD_PORT    	0x0000
#define KBD_DISABLE_KBD_PORT   	0x0010
#define KBD_ENABLE_AUX_PORT	0x0000
#define KBD_DISABLE_AUX_PORT	0x0020
#define KBD_ENABLE_AUX_INT	0x0002
#define KBD_DISABLE_AUX_INT	0x0000
#define KBD_ENABLE_KBD_INT     	0x0001
#define KBD_DISABLE_KBD_INT    	0x0000
#define KBD_KBD_CONTROL_BITS	(KBD_DISABLE_KBD_PORT | KBD_ENABLE_KBD_INT)
#define KBD_AUX_CONTROL_BITS	(KBD_DISABLE_AUX_PORT | KBD_ENABLE_AUX_INT)

/* keyboard device commands (sent to KBD_DATA_PORT) */
#define KBDC_RESET_KBD	     	0x00ff
#define KBDC_ENABLE_KBD		0x00f4
#define KBDC_DISABLE_KBD	0x00f5
#define KBDC_SET_DEFAULTS	0x00f6
#define KBDC_SEND_DEV_ID	0x00f2
#define KBDC_SET_LEDS		0x00ed
#define KBDC_ECHO		0x00ee
#define KBDC_SET_SCANCODE_SET	0x00f0
#define KBDC_SET_TYPEMATIC	0x00f3

/* aux device commands (sent to KBD_DATA_PORT) */
#define PSMC_RESET_DEV	     	0x00ff
#define PSMC_ENABLE_DEV      	0x00f4
#define PSMC_DISABLE_DEV     	0x00f5
#define PSMC_SET_DEFAULTS	0x00f6
#define PSMC_SEND_DEV_ID     	0x00f2
#define PSMC_SEND_DEV_STATUS 	0x00e9
#define PSMC_SEND_DEV_DATA	0x00eb
#define PSMC_SET_SCALING11	0x00e6
#define PSMC_SET_SCALING21	0x00e7
#define PSMC_SET_RESOLUTION	0x00e8
#define PSMC_SET_STREAM_MODE	0x00ea
#define PSMC_SET_REMOTE_MODE	0x00f0
#define PSMC_SET_SAMPLING_RATE	0x00f3

/* PSMC_SET_RESOLUTION argument */
#define PSMD_RES_LOW		0	/* typically 25ppi */
#define PSMD_RES_MEDIUM_LOW	1	/* typically 50ppi */
#define PSMD_RES_MEDIUM_HIGH	2	/* typically 100ppi (default) */
#define PSMD_RES_HIGH		3	/* typically 200ppi */
#define PSMD_MAX_RESOLUTION	PSMD_RES_HIGH

/* PSMC_SET_SAMPLING_RATE */
#define PSMD_MAX_RATE		255	/* FIXME: not sure if it's possible */

/* status bits (KBD_STATUS_PORT) */
#define KBDS_BUFFER_FULL	0x0021
#define KBDS_ANY_BUFFER_FULL	0x0001
#define KBDS_KBD_BUFFER_FULL	0x0001
#define KBDS_AUX_BUFFER_FULL	0x0021
#define KBDS_INPUT_BUFFER_FULL	0x0002

/* return code */
#define KBD_ACK 		0x00fa
#define KBD_RESEND		0x00fe
#define KBD_RESET_DONE		0x00aa
#define KBD_RESET_FAIL		0x00fc
#define KBD_DIAG_DONE		0x0055
#define KBD_DIAG_FAIL		0x00fd
#define KBD_ECHO		0x00ee

#define PSM_ACK 		0x00fa
#define PSM_RESEND		0x00fe
#define PSM_RESET_DONE		0x00aa
#define PSM_RESET_FAIL		0x00fc

/* aux device ID */
#define PSM_MOUSE_ID		0
#define PSM_BALLPOINT_ID	2
#define PSM_INTELLI_ID		3
#define PSM_EXPLORER_ID		4
#define PSM_4DMOUSE_ID		6
#define PSM_4DPLUS_ID		8
#define PSM_4DPLUS_RFSW35_ID	24

#ifdef _KERNEL

#define ATKBDC_DRIVER_NAME	"atkbdc"

/* 
 * driver specific options: the following options may be set by
 * `options' statements in the kernel configuration file. 
 */

/* retry count */
#ifndef KBD_MAXRETRY
#define KBD_MAXRETRY	3
#endif

/* timing parameters */
#ifndef KBD_RESETDELAY
#define KBD_RESETDELAY  200     /* wait 200msec after kbd/mouse reset */
#endif
#ifndef KBD_MAXWAIT
#define KBD_MAXWAIT	5 	/* wait 5 times at most after reset */
#endif

/* I/O recovery time */
#define KBDC_DELAYTIME	20
#define KBDD_DELAYTIME	7

/* debug option */
#ifndef KBDIO_DEBUG
#define KBDIO_DEBUG	0
#endif

/* end of driver specific options */

/* types/structures */

#define KBDQ_BUFSIZE	32

typedef struct _kqueue {
    int head;
    int tail;
    unsigned char q[KBDQ_BUFSIZE];
#if KBDIO_DEBUG >= 2
    int call_count;
    int qcount;
    int max_qcount;
#endif
} kqueue;

struct resource;

typedef struct atkbdc_softc {
    struct resource *port0;	/* data port */
    struct resource *port1;	/* status port */
    struct resource *irq;
    bus_space_tag_t iot;
    bus_space_handle_t ioh0;
    bus_space_handle_t ioh1;
    int command_byte;		/* current command byte value */
    int command_mask;		/* command byte mask bits for kbd/aux devices */
    int lock;			/* FIXME: XXX not quite a semaphore... */
    kqueue kbd;			/* keyboard data queue */
    kqueue aux;			/* auxiliary data queue */
    int retry;
    int quirks;			/* controller doesn't like deactivate */
#define KBDC_QUIRK_KEEP_ACTIVATED	(1 << 0)
#define KBDC_QUIRK_IGNORE_PROBE_RESULT	(1 << 1)
#define KBDC_QUIRK_RESET_AFTER_PROBE	(1 << 2)
#define KBDC_QUIRK_SETLEDS_ON_INIT	(1 << 3)
    int aux_mux_enabled;	/* active PS/2 multiplexing is enabled */
    int aux_mux_port;		/* current aux mux port */
} atkbdc_softc_t; 

enum kbdc_device_ivar {
	KBDC_IVAR_VENDORID,
	KBDC_IVAR_SERIAL,
	KBDC_IVAR_LOGICALID,
	KBDC_IVAR_COMPATID, 
};

typedef caddr_t KBDC;

#define KBDC_RID_KBD	0
#define KBDC_RID_AUX	1

#define KBDC_AUX_MUX_NUM_PORTS	4

/* function prototypes */

atkbdc_softc_t *atkbdc_get_softc(int unit);
int atkbdc_probe_unit(int unit, struct resource *port0, struct resource *port1);
int atkbdc_attach_unit(int unit, atkbdc_softc_t *sc, struct resource *port0,
		       struct resource *port1);
int atkbdc_configure(void);

KBDC atkbdc_open(int unit);
int kbdc_lock(KBDC kbdc, int lock);
int kbdc_data_ready(KBDC kbdc);

int write_controller_command(KBDC kbdc,int c);
int write_controller_data(KBDC kbdc,int c);

int write_kbd_command(KBDC kbdc,int c);
int write_aux_command(KBDC kbdc,int c);
int send_kbd_command(KBDC kbdc,int c);
int send_aux_command(KBDC kbdc,int c);
int send_kbd_command_and_data(KBDC kbdc,int c,int d);
int send_aux_command_and_data(KBDC kbdc,int c,int d);

int read_controller_data(KBDC kbdc);
int read_kbd_data(KBDC kbdc);
int read_kbd_data_no_wait(KBDC kbdc);
int read_aux_data(KBDC kbdc);
int read_aux_data_no_wait(KBDC kbdc);

void empty_kbd_buffer(KBDC kbdc, int t);
void empty_aux_buffer(KBDC kbdc, int t);
void empty_both_buffers(KBDC kbdc, int t);

int reset_kbd(KBDC kbdc);
int reset_aux_dev(KBDC kbdc);

int test_controller(KBDC kbdc);
int test_kbd_port(KBDC kbdc);
int test_aux_port(KBDC kbdc);

int kbdc_get_device_mask(KBDC kbdc);
void kbdc_set_device_mask(KBDC kbdc, int mask);

int get_controller_command_byte(KBDC kbdc);
int set_controller_command_byte(KBDC kbdc, int command, int flag);

int set_active_aux_mux_port(KBDC p, int port);
int enable_aux_mux(KBDC p);
int disable_aux_mux(KBDC p);
int aux_mux_is_enabled(KBDC p);

#endif /* _KERNEL */

#endif /* !_DEV_ATKBDC_ATKBDCREG_H_ */
