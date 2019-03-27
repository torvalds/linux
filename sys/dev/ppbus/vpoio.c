/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998, 1999 Nicolas Souchu
 * Copyright (c) 2000 Alcove - Nicolas Souchu
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/malloc.h>


#endif

#include "opt_vpo.h"

#include <dev/ppbus/ppbio.h>
#include <dev/ppbus/ppbconf.h>
#include <dev/ppbus/ppb_msq.h>
#include <dev/ppbus/vpoio.h>

#include "ppbus_if.h"

/*
 * The driver pools the drive. We may add a timeout queue to avoid
 * active polling on nACK. I've tried this but it leads to unreliable
 * transfers
 */
#define VP0_SELTMO		5000	/* select timeout */
#define VP0_FAST_SPINTMO	500000	/* wait status timeout */
#define VP0_LOW_SPINTMO		5000000	/* wait status timeout */

/*
 * Actually, VP0 timings are more accurate (about few 16MHZ cycles),
 * but succeeding in respecting such timings leads to architecture
 * dependent considerations.
 */
#define VP0_PULSE		1

#define VP0_SECTOR_SIZE	512
#define VP0_BUFFER_SIZE	0x12000

#define n(flags) (~(flags) & (flags))

/*
 * VP0 connections.
 */
#define H_AUTO		n(AUTOFEED)
#define H_nAUTO		AUTOFEED
#define H_STROBE	n(STROBE)
#define H_nSTROBE	STROBE
#define H_BSY		n(nBUSY)
#define H_nBSY		nBUSY
#define H_SEL		SELECT
#define H_nSEL		n(SELECT)
#define H_ERR		PERROR
#define H_nERR		n(PERROR)
#define H_ACK		nACK
#define H_nACK		n(nACK)
#define H_FLT		nFAULT
#define H_nFLT		n(nFAULT)
#define H_SELIN		n(SELECTIN)
#define H_nSELIN	SELECTIN
#define H_INIT		nINIT
#define H_nINIT		n(nINIT)

/*
 * Microcode to execute very fast I/O sequences at the lowest bus level.
 */

#define WAIT_RET	MS_PARAM(4, 2, MS_TYP_PTR)
#define WAIT_TMO	MS_PARAM(0, 0, MS_TYP_INT)

#define DECLARE_WAIT_MICROSEQUENCE			\
struct ppb_microseq wait_microseq[] = {			\
	MS_SET(MS_UNKNOWN),				\
	/* loop */					\
	MS_BRSET(nBUSY, 2 /* ready */),			\
	MS_DBRA(-2 /* loop */),				\
	MS_RET(1), /* timed out */			\
	/* ready */					\
	MS_RFETCH(MS_REG_STR, 0xf0, MS_UNKNOWN),	\
	MS_RET(0) /* no error */			\
}

/* call this macro to initialize connect/disconnect microsequences */
#define INIT_TRIG_MICROSEQ {						\
	int i;								\
	for (i=1; i <= 7; i+=2) {					\
		disconnect_microseq[i].arg[2] = (union ppb_insarg)d_pulse; \
		connect_epp_microseq[i].arg[2] = 			\
		connect_spp_microseq[i].arg[2] = (union ppb_insarg)c_pulse; \
	}								\
}

#define trig_d_pulse MS_TRIG(MS_REG_CTR,5,MS_UNKNOWN /* d_pulse */)
static char d_pulse[] = {
	 H_AUTO | H_nSELIN | H_INIT | H_STROBE, 0,
	H_nAUTO | H_nSELIN | H_INIT | H_STROBE, VP0_PULSE,
	 H_AUTO | H_nSELIN | H_INIT | H_STROBE, 0,
	 H_AUTO |  H_SELIN | H_INIT | H_STROBE, VP0_PULSE,
	 H_AUTO | H_nSELIN | H_INIT | H_STROBE, VP0_PULSE
};

#define trig_c_pulse MS_TRIG(MS_REG_CTR,5,MS_UNKNOWN /* c_pulse */)
static char c_pulse[] = {
	 H_AUTO | H_nSELIN | H_INIT | H_STROBE, 0,
	 H_AUTO |  H_SELIN | H_INIT | H_STROBE, 0,
	H_nAUTO |  H_SELIN | H_INIT | H_STROBE, VP0_PULSE,
	 H_AUTO |  H_SELIN | H_INIT | H_STROBE, 0,
	 H_AUTO | H_nSELIN | H_INIT | H_STROBE, VP0_PULSE
};

static struct ppb_microseq disconnect_microseq[] = {
	  MS_DASS(0x0), trig_d_pulse, MS_DASS(0x3c), trig_d_pulse,
	  MS_DASS(0x20), trig_d_pulse, MS_DASS(0xf), trig_d_pulse, MS_RET(0)
};

static struct ppb_microseq connect_epp_microseq[] = {
	  MS_DASS(0x0), trig_c_pulse, MS_DASS(0x3c), trig_c_pulse,
	  MS_DASS(0x20), trig_c_pulse, MS_DASS(0xcf), trig_c_pulse, MS_RET(0)
};

static struct ppb_microseq connect_spp_microseq[] = {
	  MS_DASS(0x0), trig_c_pulse, MS_DASS(0x3c), trig_c_pulse,
	  MS_DASS(0x20), trig_c_pulse, MS_DASS(0x8f), trig_c_pulse, MS_RET(0)
};

/*
 * nibble_inbyte_hook()
 *
 * Formats high and low nibble into a character
 */
static int
nibble_inbyte_hook (void *p, char *ptr)
{
	struct vpo_nibble *s = (struct vpo_nibble *)p;

	/* increment the buffer pointer */
	*ptr++ = ((s->l >> 4) & 0x0f) + (s->h & 0xf0);

	return (0);
}

#define INB_NIBBLE_H MS_PARAM(2, 2, MS_TYP_PTR)
#define INB_NIBBLE_L MS_PARAM(4, 2, MS_TYP_PTR)
#define INB_NIBBLE_F MS_PARAM(5, 0, MS_TYP_FUN)
#define INB_NIBBLE_P MS_PARAM(5, 1, MS_TYP_PTR)

/*
 * This is the sub-microseqence for MS_GET in NIBBLE mode
 * Retrieve the two nibbles and call the C function to generate the character
 * and store it in the buffer (see nibble_inbyte_hook())
 */

#define DECLARE_NIBBLE_INBYTE_SUBMICROSEQ			\
struct ppb_microseq nibble_inbyte_submicroseq[] = {		\
/* loop: */							\
	  MS_CASS( H_AUTO | H_SELIN | H_INIT | H_STROBE),	\
	  MS_DELAY(VP0_PULSE),					\
	  MS_RFETCH(MS_REG_STR, MS_FETCH_ALL, MS_UNKNOWN /* high nibble */),\
	  MS_CASS(H_nAUTO | H_SELIN | H_INIT | H_STROBE),	\
	  MS_RFETCH(MS_REG_STR, MS_FETCH_ALL, MS_UNKNOWN /* low nibble */),\
	  /* do a C call to format the received nibbles */	\
	  MS_C_CALL(MS_UNKNOWN /* C hook */, MS_UNKNOWN /* param */),\
	  MS_DBRA(-7 /* loop */),				\
	  MS_CASS(H_AUTO | H_nSELIN | H_INIT | H_STROBE),	\
	  MS_RET(0)						\
}

/*
 * This is the sub-microseqence for MS_GET in PS2 mode
 */
static struct ppb_microseq ps2_inbyte_submicroseq[] = {
	  MS_CASS(PCD | H_AUTO | H_SELIN | H_INIT | H_nSTROBE),

/* loop: */
	  MS_RFETCH_P(1, MS_REG_DTR, MS_FETCH_ALL),
	  MS_CASS(PCD | H_nAUTO | H_SELIN | H_INIT | H_nSTROBE),
	  MS_CASS(PCD |  H_AUTO | H_SELIN | H_INIT | H_nSTROBE),
	  MS_DBRA(-4 /* loop */),

	  MS_CASS(H_AUTO | H_nSELIN | H_INIT | H_STROBE),
	  MS_RET(0)
};

/*
 * This is the sub-microsequence for MS_PUT in both NIBBLE and PS2 modes
 */
static struct ppb_microseq spp_outbyte_submicroseq[] = {

/* loop: */
	  MS_RASSERT_P(1, MS_REG_DTR),
	  MS_CASS(H_nAUTO | H_nSELIN | H_INIT | H_STROBE),
	  MS_CASS( H_AUTO | H_nSELIN | H_INIT | H_STROBE),
	  MS_DELAY(VP0_PULSE),
	  MS_DBRA(-5 /* loop */),

	  /* return from the put call */
	  MS_RET(0)
};

/* EPP 1.7 microsequences, ptr and len set at runtime */
static struct ppb_microseq epp17_outstr_body[] = {
	  MS_CASS(H_AUTO | H_SELIN | H_INIT | H_STROBE),

/* loop: */
	  MS_RASSERT_P(1, MS_REG_EPP_D),
	  MS_BRSET(TIMEOUT, 3 /* error */),	/* EPP timeout? */
	  MS_DBRA(-3 /* loop */),

	  MS_CASS(H_AUTO | H_nSELIN | H_INIT | H_STROBE),
	  MS_RET(0),
/* error: */
	  MS_CASS(H_AUTO | H_nSELIN | H_INIT | H_STROBE),
	  MS_RET(1)
};

static struct ppb_microseq epp17_instr_body[] = {
	  MS_CASS(PCD | H_AUTO | H_SELIN | H_INIT | H_STROBE),

/* loop: */
	  MS_RFETCH_P(1, MS_REG_EPP_D, MS_FETCH_ALL),
	  MS_BRSET(TIMEOUT, 3 /* error */),	/* EPP timeout? */
	  MS_DBRA(-3 /* loop */),

	  MS_CASS(PCD | H_AUTO | H_nSELIN | H_INIT | H_STROBE),
	  MS_RET(0),
/* error: */
	  MS_CASS(PCD | H_AUTO | H_nSELIN | H_INIT | H_STROBE),
	  MS_RET(1)
};

static struct ppb_microseq in_disk_mode[] = {
	  MS_CASS( H_AUTO | H_nSELIN | H_INIT | H_STROBE),
	  MS_CASS(H_nAUTO | H_nSELIN | H_INIT | H_STROBE),

	  MS_BRCLEAR(H_FLT, 3 /* error */),
	  MS_CASS( H_AUTO | H_nSELIN | H_INIT | H_STROBE),
	  MS_BRSET(H_FLT, 1 /* error */),

	  MS_RET(1),
/* error: */
	  MS_RET(0)
};

static int
vpoio_disconnect(struct vpoio_data *vpo)
{
	device_t ppbus = device_get_parent(vpo->vpo_dev);
	int ret;

	ppb_MS_microseq(ppbus, vpo->vpo_dev, disconnect_microseq, &ret);
	return (ppb_release_bus(ppbus, vpo->vpo_dev));
}

/*
 * how	: PPB_WAIT or PPB_DONTWAIT
 */
static int
vpoio_connect(struct vpoio_data *vpo, int how)
{
	device_t ppbus = device_get_parent(vpo->vpo_dev);
	int error;
	int ret;

	if ((error = ppb_request_bus(ppbus, vpo->vpo_dev, how))) {

#ifdef VP0_DEBUG
		printf("%s: can't request bus!\n", __func__);
#endif
		return (error);
	}

	if (PPB_IN_EPP_MODE(ppbus))
		ppb_MS_microseq(ppbus, vpo->vpo_dev, connect_epp_microseq, &ret);
	else
		ppb_MS_microseq(ppbus, vpo->vpo_dev, connect_spp_microseq, &ret);

	return (0);
}

/*
 * vpoio_reset()
 *
 * SCSI reset signal, the drive must be in disk mode
 */
static void
vpoio_reset(struct vpoio_data *vpo)
{
	device_t ppbus = device_get_parent(vpo->vpo_dev);
	int ret;

	struct ppb_microseq reset_microseq[] = {

		#define INITIATOR	MS_PARAM(0, 1, MS_TYP_INT)

		MS_DASS(MS_UNKNOWN),
		MS_CASS(H_AUTO | H_nSELIN | H_nINIT | H_STROBE),
		MS_DELAY(25),
		MS_CASS(H_AUTO | H_nSELIN |  H_INIT | H_STROBE),
		MS_RET(0)
	};

	ppb_MS_init_msq(reset_microseq, 1, INITIATOR, 1 << VP0_INITIATOR);
	ppb_MS_microseq(ppbus, vpo->vpo_dev, reset_microseq, &ret);

	return;
}

/*
 * vpoio_in_disk_mode()
 */
static int
vpoio_in_disk_mode(struct vpoio_data *vpo)
{
	device_t ppbus = device_get_parent(vpo->vpo_dev);
	int ret;

	ppb_MS_microseq(ppbus, vpo->vpo_dev, in_disk_mode, &ret);

	return (ret);
}

/*
 * vpoio_detect()
 *
 * Detect and initialise the VP0 adapter.
 */
static int
vpoio_detect(struct vpoio_data *vpo)
{
	device_t ppbus = device_get_parent(vpo->vpo_dev);
	int error, ret;

	/* allocate the bus, then apply microsequences */
	if ((error = ppb_request_bus(ppbus, vpo->vpo_dev, PPB_DONTWAIT)))
		return (error);

	/* Force disconnection */
	ppb_MS_microseq(ppbus, vpo->vpo_dev, disconnect_microseq, &ret);

	/* Try to enter EPP mode, then connect to the drive in EPP mode */
	if (ppb_set_mode(ppbus, PPB_EPP) != -1) {
		/* call manually the microseq instead of using the appropriate function
		 * since we already requested the ppbus */
		ppb_MS_microseq(ppbus, vpo->vpo_dev, connect_epp_microseq, &ret);
	}

	/* If EPP mode switch failed or ZIP connection in EPP mode failed,
	 * try to connect in NIBBLE mode */
	if (!vpoio_in_disk_mode(vpo)) {

		/* The interface must be at least PS/2 or NIBBLE capable.
		 * There is no way to know if the ZIP will work with
		 * PS/2 mode since PS/2 and SPP both use the same connect
		 * sequence. One must suppress PS/2 with boot flags if
		 * PS/2 mode fails (see ppc(4)).
		 */
		if (ppb_set_mode(ppbus, PPB_PS2) != -1) {
			vpo->vpo_mode_found = VP0_MODE_PS2;
		} else {
			if (ppb_set_mode(ppbus, PPB_NIBBLE) == -1)
				goto error;

			vpo->vpo_mode_found = VP0_MODE_NIBBLE;
		}

		/* Can't know if the interface is capable of PS/2 yet */
		ppb_MS_microseq(ppbus, vpo->vpo_dev, connect_spp_microseq, &ret);
		if (!vpoio_in_disk_mode(vpo)) {
			vpo->vpo_mode_found = VP0_MODE_UNDEFINED;
			if (bootverbose)
				device_printf(vpo->vpo_dev,
				    "can't connect to the drive\n");

			/* disconnect and release the bus */
			ppb_MS_microseq(ppbus, vpo->vpo_dev, disconnect_microseq,
					&ret);
			goto error;
		}
	} else {
		vpo->vpo_mode_found = VP0_MODE_EPP;
	}

	/* send SCSI reset signal */
	vpoio_reset(vpo);

	ppb_MS_microseq(ppbus, vpo->vpo_dev, disconnect_microseq, &ret);

	/* ensure we are disconnected or daisy chained peripheral
	 * may cause serious problem to the disk */
	if (vpoio_in_disk_mode(vpo)) {
		if (bootverbose)
			device_printf(vpo->vpo_dev,
			    "can't disconnect from the drive\n");
		goto error;
	}

	ppb_release_bus(ppbus, vpo->vpo_dev);
	return (0);

error:
	ppb_release_bus(ppbus, vpo->vpo_dev);
	return (VP0_EINITFAILED);
}

/*
 * vpoio_outstr()
 */
static int
vpoio_outstr(struct vpoio_data *vpo, char *buffer, int size)
{
	device_t ppbus = device_get_parent(vpo->vpo_dev);
	int error = 0;

	ppb_MS_exec(ppbus, vpo->vpo_dev, MS_OP_PUT, (union ppb_insarg)buffer,
		(union ppb_insarg)size, (union ppb_insarg)MS_UNKNOWN, &error);

	ppb_ecp_sync(ppbus);

	return (error);
}

/*
 * vpoio_instr()
 */
static int
vpoio_instr(struct vpoio_data *vpo, char *buffer, int size)
{
	device_t ppbus = device_get_parent(vpo->vpo_dev);
	int error = 0;

	ppb_MS_exec(ppbus, vpo->vpo_dev, MS_OP_GET, (union ppb_insarg)buffer,
		(union ppb_insarg)size, (union ppb_insarg)MS_UNKNOWN, &error);

	ppb_ecp_sync(ppbus);

	return (error);
}

static char
vpoio_select(struct vpoio_data *vpo, int initiator, int target)
{
	device_t ppbus = device_get_parent(vpo->vpo_dev);
	int ret;

	struct ppb_microseq select_microseq[] = {

		/* parameter list
		 */
		#define SELECT_TARGET		MS_PARAM(0, 1, MS_TYP_INT)
		#define SELECT_INITIATOR	MS_PARAM(3, 1, MS_TYP_INT)

		/* send the select command to the drive */
		MS_DASS(MS_UNKNOWN),
		MS_CASS(H_nAUTO | H_nSELIN |  H_INIT | H_STROBE),
		MS_CASS( H_AUTO | H_nSELIN |  H_INIT | H_STROBE),
		MS_DASS(MS_UNKNOWN),
		MS_CASS( H_AUTO | H_nSELIN | H_nINIT | H_STROBE),

		/* now, wait until the drive is ready */
		MS_SET(VP0_SELTMO),
/* loop: */	MS_BRSET(H_ACK, 2 /* ready */),
		MS_DBRA(-2 /* loop */),
/* error: */	MS_RET(1),
/* ready: */	MS_RET(0)
	};

	/* initialize the select microsequence */
	ppb_MS_init_msq(select_microseq, 2,
			SELECT_TARGET, 1 << target,
			SELECT_INITIATOR, 1 << initiator);

	ppb_MS_microseq(ppbus, vpo->vpo_dev, select_microseq, &ret);

	if (ret)
		return (VP0_ESELECT_TIMEOUT);

	return (0);
}

/*
 * vpoio_wait()
 *
 * H_SELIN must be low.
 *
 * XXX should be ported to microseq
 */
static char
vpoio_wait(struct vpoio_data *vpo, int tmo)
{
	DECLARE_WAIT_MICROSEQUENCE;

	device_t ppbus = device_get_parent(vpo->vpo_dev);
	int ret, err;

#if 0	/* broken */
	if (ppb_poll_device(ppbus, 150, nBUSY, nBUSY, PPB_INTR))
		return (0);

	return (ppb_rstr(ppbus) & 0xf0);
#endif

	/*
	 * Return some status information.
	 * Semantics :	0xc0 = ZIP wants more data
	 *		0xd0 = ZIP wants to send more data
	 *		0xe0 = ZIP wants command
	 *		0xf0 = end of transfer, ZIP is sending status
	 */

	ppb_MS_init_msq(wait_microseq, 2,
			WAIT_RET, (void *)&ret,
			WAIT_TMO, tmo);

	ppb_MS_microseq(ppbus, vpo->vpo_dev, wait_microseq, &err);

	if (err)
		return (0);	 /* command timed out */

	return(ret);
}

/*
 * vpoio_probe()
 *
 * Low level probe of vpo device
 *
 */
int
vpoio_probe(device_t dev, struct vpoio_data *vpo)
{
	int error;

	/* ppbus dependent initialisation */
	vpo->vpo_dev = dev;

	/*
	 * Initialize microsequence code
	 */
	INIT_TRIG_MICROSEQ;

	/* now, try to initialise the drive */
	if ((error = vpoio_detect(vpo))) {
		return (error);
	}

	return (0);
}

/*
 * vpoio_attach()
 *
 * Low level attachment of vpo device
 *
 */
int
vpoio_attach(struct vpoio_data *vpo)
{
	DECLARE_NIBBLE_INBYTE_SUBMICROSEQ;
	device_t ppbus = device_get_parent(vpo->vpo_dev);
	int error = 0;

	vpo->vpo_nibble_inbyte_msq = (struct ppb_microseq *)malloc(
		sizeof(nibble_inbyte_submicroseq), M_DEVBUF, M_NOWAIT);

	if (!vpo->vpo_nibble_inbyte_msq)
		return (ENXIO);

	bcopy((void *)nibble_inbyte_submicroseq,
		(void *)vpo->vpo_nibble_inbyte_msq,
		sizeof(nibble_inbyte_submicroseq));

	ppb_MS_init_msq(vpo->vpo_nibble_inbyte_msq, 4,
		INB_NIBBLE_H, (void *)&(vpo)->vpo_nibble.h,
		INB_NIBBLE_L, (void *)&(vpo)->vpo_nibble.l,
		INB_NIBBLE_F, nibble_inbyte_hook,
		INB_NIBBLE_P, (void *)&(vpo)->vpo_nibble);

	/*
	 * Initialize mode dependent in/out microsequences
	 */
	ppb_lock(ppbus);
	if ((error = ppb_request_bus(ppbus, vpo->vpo_dev, PPB_WAIT)))
		goto error;

	/* ppbus sets automatically the last mode entered during detection */
	switch (vpo->vpo_mode_found) {
	case VP0_MODE_EPP:
		ppb_MS_GET_init(ppbus, vpo->vpo_dev, epp17_instr_body);
		ppb_MS_PUT_init(ppbus, vpo->vpo_dev, epp17_outstr_body);
		device_printf(vpo->vpo_dev, "EPP mode\n");
		break;
	case VP0_MODE_PS2:
		ppb_MS_GET_init(ppbus, vpo->vpo_dev, ps2_inbyte_submicroseq);
		ppb_MS_PUT_init(ppbus, vpo->vpo_dev, spp_outbyte_submicroseq);
		device_printf(vpo->vpo_dev, "PS2 mode\n");
		break;
	case VP0_MODE_NIBBLE:
		ppb_MS_GET_init(ppbus, vpo->vpo_dev, vpo->vpo_nibble_inbyte_msq);
		ppb_MS_PUT_init(ppbus, vpo->vpo_dev, spp_outbyte_submicroseq);
		device_printf(vpo->vpo_dev, "NIBBLE mode\n");
		break;
	default:
		panic("vpo: unknown mode %d", vpo->vpo_mode_found);
	}

	ppb_release_bus(ppbus, vpo->vpo_dev);

error:
	ppb_unlock(ppbus);
	return (error);
}

/*
 * vpoio_reset_bus()
 *
 */
int
vpoio_reset_bus(struct vpoio_data *vpo)
{
	/* first, connect to the drive */
	if (vpoio_connect(vpo, PPB_WAIT|PPB_INTR) || !vpoio_in_disk_mode(vpo)) {

#ifdef VP0_DEBUG
		printf("%s: not in disk mode!\n", __func__);
#endif
		/* release ppbus */
		vpoio_disconnect(vpo);
		return (1);
	}

	/* reset the SCSI bus */
	vpoio_reset(vpo);

	/* then disconnect */
	vpoio_disconnect(vpo);

	return (0);
}

/*
 * vpoio_do_scsi()
 *
 * Send an SCSI command
 *
 */
int
vpoio_do_scsi(struct vpoio_data *vpo, int host, int target, char *command,
		int clen, char *buffer, int blen, int *result, int *count,
		int *ret)
{
	device_t ppbus = device_get_parent(vpo->vpo_dev);
	char r;
	char l, h = 0;
	int len, error = 0;
	int k;

	/*
	 * enter disk state, allocate the ppbus
	 *
	 * XXX
	 * Should we allow this call to be interruptible?
	 * The only way to report the interruption is to return
	 * EIO do upper SCSI code :^(
	 */
	if ((error = vpoio_connect(vpo, PPB_WAIT|PPB_INTR)))
		return (error);

	if (!vpoio_in_disk_mode(vpo)) {
		*ret = VP0_ECONNECT;
		goto error;
	}

	if ((*ret = vpoio_select(vpo,host,target)))
		goto error;

	/*
	 * Send the command ...
	 *
	 * set H_SELIN low for vpoio_wait().
	 */
	ppb_wctr(ppbus, H_AUTO | H_nSELIN | H_INIT | H_STROBE);

	for (k = 0; k < clen; k++) {
		if (vpoio_wait(vpo, VP0_FAST_SPINTMO) != (char)0xe0) {
			*ret = VP0_ECMD_TIMEOUT;
			goto error;
		}
		if (vpoio_outstr(vpo, &command[k], 1)) {
			*ret = VP0_EPPDATA_TIMEOUT;
			goto error;
		}
	}

	/*
	 * Completion ...
	 */

	*count = 0;
	for (;;) {

		if (!(r = vpoio_wait(vpo, VP0_LOW_SPINTMO))) {
			*ret = VP0_ESTATUS_TIMEOUT;
			goto error;
		}

		/* stop when the ZIP wants to send status */
		if (r == (char)0xf0)
			break;

		if (*count >= blen) {
			*ret = VP0_EDATA_OVERFLOW;
			goto error;
		}

		/* if in EPP mode or writing bytes, try to transfer a sector
		 * otherwise, just send one byte
		 */
		if (PPB_IN_EPP_MODE(ppbus) || r == (char)0xc0)
			len = (((blen - *count) >= VP0_SECTOR_SIZE)) ?
				VP0_SECTOR_SIZE : 1;
		else
			len = 1;

		/* ZIP wants to send data? */
		if (r == (char)0xc0)
			error = vpoio_outstr(vpo, &buffer[*count], len);
		else
			error = vpoio_instr(vpo, &buffer[*count], len);

		if (error) {
			*ret = error;
			goto error;
		}

		*count += len;
	}

	if (vpoio_instr(vpo, &l, 1)) {
		*ret = VP0_EOTHER;
		goto error;
	}

	/* check if the ZIP wants to send more status */
	if (vpoio_wait(vpo, VP0_FAST_SPINTMO) == (char)0xf0)
		if (vpoio_instr(vpo, &h, 1)) {
			*ret = VP0_EOTHER + 2;
			goto error;
		}

	*result = ((int) h << 8) | ((int) l & 0xff);

error:
	/* return to printer state, release the ppbus */
	vpoio_disconnect(vpo);
	return (0);
}
