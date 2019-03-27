/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998, 1999 Nicolas Souchu
 * Copyright (c) 2001 Alcove - Nicolas Souchu
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

/*
 * Iomega ZIP+ Matchmaker Parallel Port Interface driver
 *
 * Thanks to David Campbell work on the Linux driver and the Iomega specs
 * Thanks to Thiebault Moeglin for the drive
 */
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/malloc.h>

#endif	/* _KERNEL */

#include "opt_vpo.h"

#include <dev/ppbus/ppbio.h>
#include <dev/ppbus/ppbconf.h>
#include <dev/ppbus/ppb_msq.h>
#include <dev/ppbus/vpoio.h>
#include <dev/ppbus/ppb_1284.h>

#include "ppbus_if.h"

#define VP0_SELTMO		5000	/* select timeout */
#define VP0_FAST_SPINTMO	500000	/* wait status timeout */
#define VP0_LOW_SPINTMO		5000000	/* wait status timeout */

#define VP0_SECTOR_SIZE	512

/*
 * Microcode to execute very fast I/O sequences at the lowest bus level.
 */

#define WAIT_RET		MS_PARAM(7, 2, MS_TYP_PTR)
#define WAIT_TMO		MS_PARAM(1, 0, MS_TYP_INT)

#define DECLARE_WAIT_MICROSEQUENCE \
struct ppb_microseq wait_microseq[] = {					\
	MS_CASS(0x0c),							\
	MS_SET(MS_UNKNOWN),						\
	/* loop */							\
	MS_BRSET(nBUSY, 4 /* ready */),					\
	MS_DBRA(-2 /* loop */),						\
	MS_CASS(0x04),							\
	MS_RET(1), /* timed out */					\
	/* ready */							\
	MS_CASS(0x04),							\
	MS_RFETCH(MS_REG_STR, 0xb8, MS_UNKNOWN ),			\
	MS_RET(0) /* no error */					\
}

#define SELECT_TARGET		MS_PARAM(6, 1, MS_TYP_CHA)

#define DECLARE_SELECT_MICROSEQUENCE \
struct ppb_microseq select_microseq[] = {				\
	MS_CASS(0xc),							\
	/* first, check there is nothing holding onto the bus */	\
	MS_SET(VP0_SELTMO),						\
/* _loop: */								\
	MS_BRCLEAR(0x8, 2 /* _ready */),				\
	MS_DBRA(-2 /* _loop */),					\
	MS_RET(2),			/* bus busy */			\
/* _ready: */								\
	MS_CASS(0x4),							\
	MS_DASS(MS_UNKNOWN /* 0x80 | 1 << target */),			\
	MS_DELAY(1),							\
	MS_CASS(0xc),							\
	MS_CASS(0xd),							\
	/* now, wait until the drive is ready */			\
	MS_SET(VP0_SELTMO),						\
/* loop: */								\
	MS_BRSET(0x8, 3 /* ready */),					\
	MS_DBRA(-2 /* loop */),						\
/* error: */								\
	MS_CASS(0xc),							\
	MS_RET(VP0_ESELECT_TIMEOUT),					\
/* ready: */								\
	MS_CASS(0xc),							\
	MS_RET(0)							\
}

static struct ppb_microseq transfer_epilog[] = {
	MS_CASS(0x4),
	MS_CASS(0xc),
	MS_CASS(0xe),
	MS_CASS(0x4),
	MS_RET(0)
};

#define CPP_S1		MS_PARAM(10, 2, MS_TYP_PTR)
#define CPP_S2		MS_PARAM(13, 2, MS_TYP_PTR)
#define CPP_S3		MS_PARAM(16, 2, MS_TYP_PTR)
#define CPP_PARAM	MS_PARAM(17, 1, MS_TYP_CHA)

#define DECLARE_CPP_MICROSEQ \
struct ppb_microseq cpp_microseq[] = {					\
	MS_CASS(0x0c), MS_DELAY(2),					\
	MS_DASS(0xaa), MS_DELAY(10),					\
	MS_DASS(0x55), MS_DELAY(10),					\
	MS_DASS(0x00), MS_DELAY(10),					\
	MS_DASS(0xff), MS_DELAY(10),					\
	MS_RFETCH(MS_REG_STR, 0xb8, MS_UNKNOWN /* &s1 */),		\
	MS_DASS(0x87), MS_DELAY(10),					\
	MS_RFETCH(MS_REG_STR, 0xb8, MS_UNKNOWN /* &s2 */),		\
	MS_DASS(0x78), MS_DELAY(10),					\
	MS_RFETCH(MS_REG_STR, 0x38, MS_UNKNOWN /* &s3 */),		\
	MS_DASS(MS_UNKNOWN /* param */),				\
	MS_DELAY(2),							\
	MS_CASS(0x0c), MS_DELAY(10),					\
	MS_CASS(0x0d), MS_DELAY(2),					\
	MS_CASS(0x0c), MS_DELAY(10),					\
	MS_DASS(0xff), MS_DELAY(10),					\
	MS_RET(0)							\
}

#define NEGOCIATED_MODE		MS_PARAM(2, 1, MS_TYP_CHA)

#define DECLARE_NEGOCIATE_MICROSEQ \
struct ppb_microseq negociate_microseq[] = {				\
	MS_CASS(0x4),							\
	MS_DELAY(5),							\
	MS_DASS(MS_UNKNOWN /* mode */),					\
	MS_DELAY(100),							\
	MS_CASS(0x6),							\
	MS_DELAY(5),							\
	MS_BRSET(0x20, 5 /* continue */),				\
	MS_DELAY(5),							\
	MS_CASS(0x7),							\
	MS_DELAY(5),							\
	MS_CASS(0x6),							\
	MS_RET(VP0_ENEGOCIATE),						\
/* continue: */								\
	MS_DELAY(5),							\
	MS_CASS(0x7),							\
	MS_DELAY(5),							\
	MS_CASS(0x6),							\
	MS_RET(0)							\
}

#define INB_NIBBLE_L MS_PARAM(3, 2, MS_TYP_PTR)
#define INB_NIBBLE_H MS_PARAM(6, 2, MS_TYP_PTR)
#define INB_NIBBLE_F MS_PARAM(9, 0, MS_TYP_FUN)
#define INB_NIBBLE_P MS_PARAM(9, 1, MS_TYP_PTR)

/*
 * This is the sub-microseqence for MS_GET in NIBBLE mode
 * Retrieve the two nibbles and call the C function to generate the character
 * and store it in the buffer (see nibble_inbyte_hook())
 */

#define DECLARE_NIBBLE_INBYTE_SUBMICROSEQ \
struct ppb_microseq nibble_inbyte_submicroseq[] = {			\
	MS_CASS(0x4),							\
/* loop: */								\
	MS_CASS(0x6),							\
	MS_DELAY(1),							\
	MS_RFETCH(MS_REG_STR, MS_FETCH_ALL, MS_UNKNOWN /* low nibble */),\
	MS_CASS(0x5),							\
	MS_DELAY(1),							\
	MS_RFETCH(MS_REG_STR, MS_FETCH_ALL, MS_UNKNOWN /* high nibble */),\
	MS_CASS(0x4),							\
	MS_DELAY(1),							\
	/* do a C call to format the received nibbles */		\
	MS_C_CALL(MS_UNKNOWN /* C hook */, MS_UNKNOWN /* param */),	\
	MS_DBRA(-7 /* loop */),						\
	MS_RET(0)							\
}

static struct ppb_microseq reset_microseq[] = {
	MS_CASS(0x04),
	MS_DASS(0x40),
	MS_DELAY(1),
	MS_CASS(0x0c),
	MS_CASS(0x0d),
	MS_DELAY(50),
	MS_CASS(0x0c),
	MS_CASS(0x04),
	MS_RET(0)
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
	*ptr = ((s->l >> 4) & 0x0f) + (s->h & 0xf0);

	return (0);
}

/*
 * This is the sub-microseqence for MS_GET in PS2 mode
 */
static struct ppb_microseq ps2_inbyte_submicroseq[] = {
	  MS_CASS(0x4),

/* loop: */
	  MS_CASS(PCD | 0x6),
	  MS_RFETCH_P(1, MS_REG_DTR, MS_FETCH_ALL),
	  MS_CASS(PCD | 0x5),
	  MS_DBRA(-4 /* loop */),

	  MS_RET(0)
};

/*
 * This is the sub-microsequence for MS_PUT in both NIBBLE and PS2 modes
 */
static struct ppb_microseq spp_outbyte_submicroseq[] = {
	  MS_CASS(0x4),

/* loop: */
	  MS_RASSERT_P(1, MS_REG_DTR),
	  MS_CASS(0x5),
	  MS_DBRA(0),				/* decrement counter */
	  MS_RASSERT_P(1, MS_REG_DTR),
	  MS_CASS(0x0),
	  MS_DBRA(-6 /* loop */),

	  /* return from the put call */
	  MS_CASS(0x4),
	  MS_RET(0)
};

/* EPP 1.7 microsequences, ptr and len set at runtime */
static struct ppb_microseq epp17_outstr[] = {
	  MS_CASS(0x4),
	  MS_RASSERT_P(MS_ACCUM, MS_REG_EPP_D),
	  MS_CASS(0xc),
	  MS_RET(0),
};

static struct ppb_microseq epp17_instr[] = {
	  MS_CASS(PCD | 0x4),
	  MS_RFETCH_P(MS_ACCUM, MS_REG_EPP_D, MS_FETCH_ALL),
	  MS_CASS(PCD | 0xc),
	  MS_RET(0),
};

static int
imm_disconnect(struct vpoio_data *vpo, int *connected, int release_bus)
{
	DECLARE_CPP_MICROSEQ;

	device_t ppbus = device_get_parent(vpo->vpo_dev);
	char s1, s2, s3;
	int ret;

	/* all should be ok */
	if (connected)
		*connected = 0;

	ppb_MS_init_msq(cpp_microseq, 4, CPP_S1, (void *)&s1,
			CPP_S2, (void *)&s2, CPP_S3, (void *)&s3,
			CPP_PARAM, 0x30);

	ppb_MS_microseq(ppbus, vpo->vpo_dev, cpp_microseq, &ret);

	if ((s1 != (char)0xb8 || s2 != (char)0x18 || s3 != (char)0x38)) {
		if (bootverbose)
			device_printf(vpo->vpo_dev,
			    "(disconnect) s1=0x%x s2=0x%x, s3=0x%x\n",
			    s1 & 0xff, s2 & 0xff, s3 & 0xff);
		if (connected)
			*connected = VP0_ECONNECT;
	}

	if (release_bus)
		return (ppb_release_bus(ppbus, vpo->vpo_dev));
	else
		return (0);
}

/*
 * how	: PPB_WAIT or PPB_DONTWAIT
 */
static int
imm_connect(struct vpoio_data *vpo, int how, int *disconnected, int request_bus)
{
	DECLARE_CPP_MICROSEQ;

	device_t ppbus = device_get_parent(vpo->vpo_dev);
	char s1, s2, s3;
	int error;
	int ret;

	/* all should be ok */
	if (disconnected)
		*disconnected = 0;

	if (request_bus)
		if ((error = ppb_request_bus(ppbus, vpo->vpo_dev, how)))
			return (error);

	ppb_MS_init_msq(cpp_microseq, 3, CPP_S1, (void *)&s1,
			CPP_S2, (void *)&s2, CPP_S3, (void *)&s3);

	/* select device 0 in compatible mode */
	ppb_MS_init_msq(cpp_microseq, 1, CPP_PARAM, 0xe0);
	ppb_MS_microseq(ppbus, vpo->vpo_dev, cpp_microseq, &ret);

	/* disconnect all devices */
	ppb_MS_init_msq(cpp_microseq, 1, CPP_PARAM, 0x30);
	ppb_MS_microseq(ppbus, vpo->vpo_dev, cpp_microseq, &ret);

	if (PPB_IN_EPP_MODE(ppbus))
		ppb_MS_init_msq(cpp_microseq, 1, CPP_PARAM, 0x28);
	else
		ppb_MS_init_msq(cpp_microseq, 1, CPP_PARAM, 0xe0);

	ppb_MS_microseq(ppbus, vpo->vpo_dev, cpp_microseq, &ret);

	if ((s1 != (char)0xb8 || s2 != (char)0x18 || s3 != (char)0x30)) {
		if (bootverbose)
			device_printf(vpo->vpo_dev,
			    "(connect) s1=0x%x s2=0x%x, s3=0x%x\n",
			    s1 & 0xff, s2 & 0xff, s3 & 0xff);
		if (disconnected)
			*disconnected = VP0_ECONNECT;
	}

	return (0);
}

/*
 * imm_detect()
 *
 * Detect and initialise the VP0 adapter.
 */
static int
imm_detect(struct vpoio_data *vpo)
{
	device_t ppbus = device_get_parent(vpo->vpo_dev);
	int error;

	if ((error = ppb_request_bus(ppbus, vpo->vpo_dev, PPB_DONTWAIT)))
		return (error);

	/* disconnect the drive, keep the bus */
	imm_disconnect(vpo, NULL, 0);

	vpo->vpo_mode_found = VP0_MODE_UNDEFINED;
	error = 1;

	/* try to enter EPP mode since vpoio failure put the bus in NIBBLE */
	if (ppb_set_mode(ppbus, PPB_EPP) != -1) {
		imm_connect(vpo, PPB_DONTWAIT, &error, 0);
	}

	/* if connection failed try PS/2 then NIBBLE modes */
	if (error) {
		if (ppb_set_mode(ppbus, PPB_PS2) != -1) {
			imm_connect(vpo, PPB_DONTWAIT, &error, 0);
		}
		if (error) {
			if (ppb_set_mode(ppbus, PPB_NIBBLE) != -1) {
				imm_connect(vpo, PPB_DONTWAIT, &error, 0);
				if (error)
					goto error;
				vpo->vpo_mode_found = VP0_MODE_NIBBLE;
			} else {
				device_printf(vpo->vpo_dev,
				    "NIBBLE mode unavailable!\n");
				goto error;
			}
		} else {
			vpo->vpo_mode_found = VP0_MODE_PS2;
		}
	} else {
		vpo->vpo_mode_found = VP0_MODE_EPP;
	}

	/* send SCSI reset signal */
	ppb_MS_microseq(ppbus, vpo->vpo_dev, reset_microseq, NULL);

	/* release the bus now */
	imm_disconnect(vpo, &error, 1);

	/* ensure we are disconnected or daisy chained peripheral
	 * may cause serious problem to the disk */

	if (error) {
		if (bootverbose)
			device_printf(vpo->vpo_dev,
			    "can't disconnect from the drive\n");
		goto error;
	}

	return (0);

error:
	ppb_release_bus(ppbus, vpo->vpo_dev);
	return (VP0_EINITFAILED);
}

/*
 * imm_outstr()
 */
static int
imm_outstr(struct vpoio_data *vpo, char *buffer, int size)
{
	device_t ppbus = device_get_parent(vpo->vpo_dev);
	int error = 0;

	if (PPB_IN_EPP_MODE(ppbus))
		ppb_reset_epp_timeout(ppbus);

	ppb_MS_exec(ppbus, vpo->vpo_dev, MS_OP_PUT, (union ppb_insarg)buffer,
		(union ppb_insarg)size, (union ppb_insarg)MS_UNKNOWN, &error);

	return (error);
}

/*
 * imm_instr()
 */
static int
imm_instr(struct vpoio_data *vpo, char *buffer, int size)
{
	device_t ppbus = device_get_parent(vpo->vpo_dev);
	int error = 0;

	if (PPB_IN_EPP_MODE(ppbus))
		ppb_reset_epp_timeout(ppbus);

	ppb_MS_exec(ppbus, vpo->vpo_dev, MS_OP_GET, (union ppb_insarg)buffer,
		(union ppb_insarg)size, (union ppb_insarg)MS_UNKNOWN, &error);

	return (error);
}

static char
imm_select(struct vpoio_data *vpo, int initiator, int target)
{
	DECLARE_SELECT_MICROSEQUENCE;
	device_t ppbus = device_get_parent(vpo->vpo_dev);
	int ret;

	/* initialize the select microsequence */
	ppb_MS_init_msq(select_microseq, 1,
			SELECT_TARGET, 1 << initiator | 1 << target);

	ppb_MS_microseq(ppbus, vpo->vpo_dev, select_microseq, &ret);

	return (ret);
}

/*
 * imm_wait()
 *
 * H_SELIN must be low.
 *
 */
static char
imm_wait(struct vpoio_data *vpo, int tmo)
{
	DECLARE_WAIT_MICROSEQUENCE;

	device_t ppbus = device_get_parent(vpo->vpo_dev);
	int ret, err;

	/*
	 * Return some status information.
	 * Semantics :	0x88 = ZIP+ wants more data
	 *		0x98 = ZIP+ wants to send more data
	 *		0xa8 = ZIP+ wants command
	 *		0xb8 = end of transfer, ZIP+ is sending status
	 */

	ppb_MS_init_msq(wait_microseq, 2,
			WAIT_RET, (void *)&ret,
			WAIT_TMO, tmo);

	ppb_MS_microseq(ppbus, vpo->vpo_dev, wait_microseq, &err);

	if (err)
		return (0);			   /* command timed out */

	return(ret);
}

static int
imm_negociate(struct vpoio_data *vpo)
{
	DECLARE_NEGOCIATE_MICROSEQ;
	device_t ppbus = device_get_parent(vpo->vpo_dev);
	int negociate_mode;
	int ret;

	if (PPB_IN_NIBBLE_MODE(ppbus))
		negociate_mode = 0;
	else if (PPB_IN_PS2_MODE(ppbus))
		negociate_mode = 1;
	else
		return (0);

#if 0 /* XXX use standalone code not to depend on ppb_1284 code yet */
	ret = ppb_1284_negociate(ppbus, negociate_mode);

	if (ret)
		return (VP0_ENEGOCIATE);
#endif

	ppb_MS_init_msq(negociate_microseq, 1,
			NEGOCIATED_MODE, negociate_mode);

	ppb_MS_microseq(ppbus, vpo->vpo_dev, negociate_microseq, &ret);

	return (ret);
}

/*
 * imm_probe()
 *
 * Low level probe of vpo device
 *
 */
int
imm_probe(device_t dev, struct vpoio_data *vpo)
{
	int error;

	/* ppbus dependent initialisation */
	vpo->vpo_dev = dev;

	/* now, try to initialise the drive */
	if ((error = imm_detect(vpo))) {
		return (error);
	}

	return (0);
}

/*
 * imm_attach()
 *
 * Low level attachment of vpo device
 *
 */
int
imm_attach(struct vpoio_data *vpo)
{
	DECLARE_NIBBLE_INBYTE_SUBMICROSEQ;
	device_t ppbus = device_get_parent(vpo->vpo_dev);
	int error = 0;

	/*
	 * Initialize microsequence code
	 */
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

	/* ppbus automatically restore the last mode entered during detection */
	switch (vpo->vpo_mode_found) {
	case VP0_MODE_EPP:
		ppb_MS_GET_init(ppbus, vpo->vpo_dev, epp17_instr);
		ppb_MS_PUT_init(ppbus, vpo->vpo_dev, epp17_outstr);
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
		panic("imm: unknown mode %d", vpo->vpo_mode_found);
	}

	ppb_release_bus(ppbus, vpo->vpo_dev);
 error:
	ppb_unlock(ppbus);
	return (error);
}

/*
 * imm_reset_bus()
 *
 */
int
imm_reset_bus(struct vpoio_data *vpo)
{
	device_t ppbus = device_get_parent(vpo->vpo_dev);
	int disconnected;

	/* first, connect to the drive and request the bus */
	imm_connect(vpo, PPB_WAIT|PPB_INTR, &disconnected, 1);

	if (!disconnected) {

		/* reset the SCSI bus */
		ppb_MS_microseq(ppbus, vpo->vpo_dev, reset_microseq, NULL);

		/* then disconnect */
		imm_disconnect(vpo, NULL, 1);
	}

	return (0);
}

/*
 * imm_do_scsi()
 *
 * Send an SCSI command
 *
 */
int
imm_do_scsi(struct vpoio_data *vpo, int host, int target, char *command,
		int clen, char *buffer, int blen, int *result, int *count,
		int *ret)
{
	device_t ppbus = device_get_parent(vpo->vpo_dev);
	char r;
	char l, h = 0;
	int len, error = 0, not_connected = 0;
	int k;
	int negociated = 0;

	/*
	 * enter disk state, allocate the ppbus
	 *
	 * XXX
	 * Should we allow this call to be interruptible?
	 * The only way to report the interruption is to return
	 * EIO to upper SCSI code :^(
	 */
	if ((error = imm_connect(vpo, PPB_WAIT|PPB_INTR, &not_connected, 1)))
		return (error);

	if (not_connected) {
		*ret = VP0_ECONNECT;
		goto error;
	}

	/*
	 * Select the drive ...
	 */
	if ((*ret = imm_select(vpo,host,target)))
		goto error;

	/*
	 * Send the command ...
	 */
	for (k = 0; k < clen; k+=2) {
		if (imm_wait(vpo, VP0_FAST_SPINTMO) != (char)0xa8) {
			*ret = VP0_ECMD_TIMEOUT;
			goto error;
		}
		if (imm_outstr(vpo, &command[k], 2)) {
			*ret = VP0_EPPDATA_TIMEOUT;
			goto error;
		}
	}

	if (!(r = imm_wait(vpo, VP0_LOW_SPINTMO))) {
		*ret = VP0_ESTATUS_TIMEOUT;
		goto error;
	}

	if ((r & 0x30) == 0x10) {
		if (imm_negociate(vpo)) {
			*ret = VP0_ENEGOCIATE;
			goto error;
		} else
			negociated = 1;
	}

	/*
	 * Complete transfer ...
	 */
	*count = 0;
	for (;;) {

		if (!(r = imm_wait(vpo, VP0_LOW_SPINTMO))) {
			*ret = VP0_ESTATUS_TIMEOUT;
			goto error;
		}

		/* stop when the ZIP+ wants to send status */
		if (r == (char)0xb8)
			break;

		if (*count >= blen) {
			*ret = VP0_EDATA_OVERFLOW;
			goto error;
		}

		/* ZIP+ wants to send data? */
		if (r == (char)0x88) {
			len = (((blen - *count) >= VP0_SECTOR_SIZE)) ?
				VP0_SECTOR_SIZE : 2;

			error = imm_outstr(vpo, &buffer[*count], len);
		} else {
			if (!PPB_IN_EPP_MODE(ppbus))
				len = 1;
			else
				len = (((blen - *count) >= VP0_SECTOR_SIZE)) ?
					VP0_SECTOR_SIZE : 1;

			error = imm_instr(vpo, &buffer[*count], len);
		}

		if (error) {
			*ret = error;
			goto error;
		}

		*count += len;
	}

	if ((PPB_IN_NIBBLE_MODE(ppbus) ||
			PPB_IN_PS2_MODE(ppbus)) && negociated)
		ppb_MS_microseq(ppbus, vpo->vpo_dev, transfer_epilog, NULL);

	/*
	 * Retrieve status ...
	 */
	if (imm_negociate(vpo)) {
		*ret = VP0_ENEGOCIATE;
		goto error;
	} else
		negociated = 1;

	if (imm_instr(vpo, &l, 1)) {
		*ret = VP0_EOTHER;
		goto error;
	}

	/* check if the ZIP+ wants to send more status */
	if (imm_wait(vpo, VP0_FAST_SPINTMO) == (char)0xb8)
		if (imm_instr(vpo, &h, 1)) {
			*ret = VP0_EOTHER + 2;
			goto error;
		}

	/* Experience showed that we should discard this */
	if (h == (char) -1)
		h = 0;

	*result = ((int) h << 8) | ((int) l & 0xff);

error:
	if ((PPB_IN_NIBBLE_MODE(ppbus) ||
			PPB_IN_PS2_MODE(ppbus)) && negociated)
		ppb_MS_microseq(ppbus, vpo->vpo_dev, transfer_epilog, NULL);

	/* return to printer state, release the ppbus */
	imm_disconnect(vpo, NULL, 1);

	return (0);
}
