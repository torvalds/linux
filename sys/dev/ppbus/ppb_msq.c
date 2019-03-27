/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998, 1999 Nicolas Souchu
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
#include <machine/stdarg.h>

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <dev/ppbus/ppbconf.h>
#include <dev/ppbus/ppb_msq.h>

#include "ppbus_if.h"

/* msq index (see PPB_MAX_XFER)
 * These are device modes
 */
#define COMPAT_MSQ	0x0
#define NIBBLE_MSQ	0x1
#define PS2_MSQ		0x2
#define EPP17_MSQ	0x3
#define EPP19_MSQ	0x4
#define ECP_MSQ		0x5

/*
 * Device mode to submsq conversion
 */
static struct ppb_xfer *
mode2xfer(device_t bus, struct ppb_device *ppbdev, int opcode)
{
	int index, epp, mode;
	struct ppb_xfer *table;

	switch (opcode) {
	case MS_OP_GET:
		table = ppbdev->get_xfer;
		break;

	case MS_OP_PUT:
		table = ppbdev->put_xfer;
		break;

	default:
		panic("%s: unknown opcode (%d)", __func__, opcode);
	}

	/* retrieve the device operating mode */
	mode = ppb_get_mode(bus);
	switch (mode) {
	case PPB_COMPATIBLE:
		index = COMPAT_MSQ;
		break;
	case PPB_NIBBLE:
		index = NIBBLE_MSQ;
		break;
	case PPB_PS2:
		index = PS2_MSQ;
		break;
	case PPB_EPP:
		switch ((epp = ppb_get_epp_protocol(bus))) {
		case EPP_1_7:
			index = EPP17_MSQ;
			break;
		case EPP_1_9:
			index = EPP19_MSQ;
			break;
		default:
			panic("%s: unknown EPP protocol (0x%x)!", __func__,
				epp);
		}
		break;
	case PPB_ECP:
		index = ECP_MSQ;
		break;
	default:
		panic("%s: unknown mode (%d)", __func__, mode);
	}

	return (&table[index]);
}

/*
 * ppb_MS_init()
 *
 * Initialize device dependent submicrosequence of the current mode
 *
 */
int
ppb_MS_init(device_t bus, device_t dev, struct ppb_microseq *loop, int opcode)
{
	struct ppb_device *ppbdev = (struct ppb_device *)device_get_ivars(dev);
	struct ppb_xfer *xfer = mode2xfer(bus, ppbdev, opcode);

	ppb_assert_locked(bus);
	xfer->loop = loop;

	return (0);
}

/*
 * ppb_MS_exec()
 *
 * Execute any microsequence opcode - expensive
 *
 */
int
ppb_MS_exec(device_t bus, device_t dev, int opcode, union ppb_insarg param1,
		union ppb_insarg param2, union ppb_insarg param3, int *ret)
{
	struct ppb_microseq msq[] = {
		  { MS_UNKNOWN, { { MS_UNKNOWN }, { MS_UNKNOWN }, { MS_UNKNOWN } } },
		  MS_RET(0)
	};

	/* initialize the corresponding microseq */
	msq[0].opcode = opcode;
	msq[0].arg[0] = param1;
	msq[0].arg[1] = param2;
	msq[0].arg[2] = param3;

	/* execute the microseq */
	return (ppb_MS_microseq(bus, dev, msq, ret));
}

/*
 * ppb_MS_loop()
 *
 * Execute a microseq loop
 *
 */
int
ppb_MS_loop(device_t bus, device_t dev, struct ppb_microseq *prolog,
		struct ppb_microseq *body, struct ppb_microseq *epilog,
		int iter, int *ret)
{
	struct ppb_microseq loop_microseq[] = {
		  MS_CALL(0),			/* execute prolog */

		  MS_SET(MS_UNKNOWN),		/* set size of transfer */
	/* loop: */
		  MS_CALL(0),			/* execute body */
		  MS_DBRA(-1 /* loop: */),

		  MS_CALL(0),			/* execute epilog */
		  MS_RET(0)
	};

	/* initialize the structure */
	loop_microseq[0].arg[0].p = (void *)prolog;
	loop_microseq[1].arg[0].i = iter;
	loop_microseq[2].arg[0].p = (void *)body;
	loop_microseq[4].arg[0].p = (void *)epilog;

	/* execute the loop */
	return (ppb_MS_microseq(bus, dev, loop_microseq, ret));
}

/*
 * ppb_MS_init_msq()
 *
 * Initialize a microsequence - see macros in ppb_msq.h
 *
 */
int
ppb_MS_init_msq(struct ppb_microseq *msq, int nbparam, ...)
{
	int i;
	int param, ins, arg, type;
	va_list p_list;

	va_start(p_list, nbparam);

	for (i=0; i<nbparam; i++) {
		/* retrieve the parameter descriptor */
		param = va_arg(p_list, int);

		ins  = MS_INS(param);
		arg  = MS_ARG(param);
		type = MS_TYP(param);

		/* check the instruction position */
		if (arg >= PPB_MS_MAXARGS)
			panic("%s: parameter out of range (0x%x)!",
				__func__, param);

#if 0
		printf("%s: param = %d, ins = %d, arg = %d, type = %d\n",
			__func__, param, ins, arg, type);
#endif

		/* properly cast the parameter */
		switch (type) {
		case MS_TYP_INT:
			msq[ins].arg[arg].i = va_arg(p_list, int);
			break;

		case MS_TYP_CHA:
			msq[ins].arg[arg].i = (int)va_arg(p_list, int);
			break;

		case MS_TYP_PTR:
			msq[ins].arg[arg].p = va_arg(p_list, void *);
			break;

		case MS_TYP_FUN:
			msq[ins].arg[arg].f = va_arg(p_list, void *);
			break;

		default:
			panic("%s: unknown parameter (0x%x)!", __func__,
				param);
		}
	}

	va_end(p_list);
	return (0);
}

/*
 * ppb_MS_microseq()
 *
 * Interprete a microsequence. Some microinstructions are executed at adapter
 * level to avoid function call overhead between ppbus and the adapter
 */
int
ppb_MS_microseq(device_t bus, device_t dev, struct ppb_microseq *msq, int *ret)
{
	struct ppb_data *ppb = (struct ppb_data *)device_get_softc(bus);
	struct ppb_device *ppbdev = (struct ppb_device *)device_get_ivars(dev);

	struct ppb_microseq *mi;		/* current microinstruction */
	int error;

	struct ppb_xfer *xfer;

	/* microsequence executed to initialize the transfer */
	struct ppb_microseq initxfer[] = {
		MS_PTR(MS_UNKNOWN), 	/* set ptr to buffer */
		MS_SET(MS_UNKNOWN),	/* set transfer size */
		MS_RET(0)
	};

	mtx_assert(ppb->ppc_lock, MA_OWNED);
	if (ppb->ppb_owner != dev)
		return (EACCES);

#define INCR_PC (mi ++)

	mi = msq;
	for (;;) {
		switch (mi->opcode) {
		case MS_OP_PUT:
		case MS_OP_GET:

			/* attempt to choose the best mode for the device */
			xfer = mode2xfer(bus, ppbdev, mi->opcode);

			/* figure out if we should use ieee1284 code */
			if (!xfer->loop) {
				if (mi->opcode == MS_OP_PUT) {
					if ((error = PPBUS_WRITE(
						device_get_parent(bus),
						(char *)mi->arg[0].p,
						mi->arg[1].i, 0)))
							goto error;

					INCR_PC;
					goto next;
				} else
					panic("%s: IEEE1284 read not supported", __func__);
			}

			/* XXX should use ppb_MS_init_msq() */
			initxfer[0].arg[0].p = mi->arg[0].p;
			initxfer[1].arg[0].i = mi->arg[1].i;

			/* initialize transfer */
			ppb_MS_microseq(bus, dev, initxfer, &error);

			if (error)
				goto error;

			/* the xfer microsequence should not contain any
			 * MS_OP_PUT or MS_OP_GET!
			 */
			ppb_MS_microseq(bus, dev, xfer->loop, &error);

			if (error)
				goto error;

			INCR_PC;
			break;

		case MS_OP_RET:
			if (ret)
				*ret = mi->arg[0].i;	/* return code */
			return (0);

		default:
			/* executing microinstructions at ppc level is
			 * faster. This is the default if the microinstr
			 * is unknown here
			 */
			if ((error = PPBUS_EXEC_MICROSEQ(
						device_get_parent(bus), &mi)))
				goto error;
			break;
		}
	next:
		continue;
	}
error:
	return (error);
}

