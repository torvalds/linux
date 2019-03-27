/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) Comtrol Corporation <support@comtrol.com>
 * All rights reserved.
 *
 * ISA-specific part separated from:
 * sys/i386/isa/rp.c,v 1.33 1999/09/28 11:45:27 phk Exp
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted prodived that the follwoing conditions
 * are met.
 * 1. Redistributions of source code must retain the above copyright 
 *    notive, this list of conditions and the following disclainer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials prodided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *       This product includes software developed by Comtrol Corporation.
 * 4. The name of Comtrol Corporation may not be used to endorse or 
 *    promote products derived from this software without specific 
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY COMTROL CORPORATION ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL COMTROL CORPORATION BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, LIFE OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/tty.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/bus.h>
#include <sys/rman.h>

#define ROCKET_C
#include <dev/rp/rpreg.h>
#include <dev/rp/rpvar.h>

#include <isa/isavar.h>

/* ISA-specific part of CONTROLLER_t */
struct ISACONTROLLER_T {
	int		MBaseIO;	/* rid of the Mudbac controller for this controller */
	int		MReg0IO;	/* offset0 of the Mudbac controller for this controller */
	int		MReg1IO;	/* offset1 of the Mudbac controller for this controller */
	int		MReg2IO;	/* offset2 of the Mudbac controller for this controller */
	int		MReg3IO;	/* offset3 of the Mudbac controller for this controller */
	Byte_t		MReg2;
	Byte_t		MReg3;
};
typedef struct ISACONTROLLER_T ISACONTROLLER_t;

#define ISACTL(ctlp) ((ISACONTROLLER_t *)((ctlp)->bus_ctlp))

/***************************************************************************
Function: sControllerEOI
Purpose:  Strobe the MUDBAC's End Of Interrupt bit.
Call:	  sControllerEOI(MudbacCtlP,CtlP)
	  CONTROLLER_T *MudbacCtlP; Ptr to Mudbac controller structure
	  CONTROLLER_T *CtlP; Ptr to controller structure
*/
#define sControllerEOI(MudbacCtlP,CtlP) \
	rp_writeio1(MudbacCtlP,ISACTL(CtlP)->MBaseIO,ISACTL(CtlP)->MReg2IO,ISACTL(CtlP)->MReg2 | INT_STROB)

/***************************************************************************
Function: sDisAiop
Purpose:  Disable I/O access to an AIOP
Call:	  sDisAiop(MudbacCtlP,CtlP)
	  CONTROLLER_T *MudbacCtlP; Ptr to Mudbac controller structure
	  CONTROLLER_T *CtlP; Ptr to controller structure
	  int AiopNum; Number of AIOP on controller
*/
#define sDisAiop(MudbacCtlP,CtlP,AIOPNUM) \
{ \
   ISACTL(CtlP)->MReg3 &= rp_sBitMapClrTbl[AIOPNUM]; \
   rp_writeio1(MudbacCtlP,ISACTL(CtlP)->MBaseIO,ISACTL(CtlP)->MReg3IO,ISACTL(CtlP)->MReg3); \
}

/***************************************************************************
Function: sEnAiop
Purpose:  Enable I/O access to an AIOP
Call:	  sEnAiop(MudbacCtlP,CtlP)
	  CONTROLLER_T *MudbacCtlP; Ptr to Mudbac controller structure
	  CONTROLLER_T *CtlP; Ptr to controller structure
	  int AiopNum; Number of AIOP on controller
*/
#define sEnAiop(MudbacCtlP,CtlP,AIOPNUM) \
{ \
   ISACTL(CtlP)->MReg3 |= rp_sBitMapSetTbl[AIOPNUM]; \
   rp_writeio1(MudbacCtlP,ISACTL(CtlP)->MBaseIO,ISACTL(CtlP)->MReg3IO,ISACTL(CtlP)->MReg3); \
}

/***************************************************************************
Function: sGetControllerIntStatus
Purpose:  Get the controller interrupt status
Call:	  sGetControllerIntStatus(MudbacCtlP,CtlP)
	  CONTROLLER_T *MudbacCtlP; Ptr to Mudbac controller structure
	  CONTROLLER_T *CtlP; Ptr to controller structure
Return:   Byte_t: The controller interrupt status in the lower 4
			 bits.	Bits 0 through 3 represent AIOP's 0
			 through 3 respectively.  If a bit is set that
			 AIOP is interrupting.	Bits 4 through 7 will
			 always be cleared.
*/
#define sGetControllerIntStatus(MudbacCtlP,CtlP) \
	(rp_readio1(MudbacCtlP,ISACTL(CtlP)->MBaseIO,ISACTL(CtlP)->MReg1IO) & 0x0f)

static devclass_t rp_devclass;
static CONTROLLER_t *rp_controller;
static int rp_nisadevs;

static int rp_probe(device_t dev);
static int rp_attach(device_t dev);
static void rp_isareleaseresource(CONTROLLER_t *ctlp);
static int sInitController(CONTROLLER_T *CtlP,
			   CONTROLLER_T *MudbacCtlP,
			   int AiopNum,
			   int IRQNum,
			   Byte_t Frequency,
			   int PeriodicOnly);
static rp_aiop2rid_t rp_isa_aiop2rid;
static rp_aiop2off_t rp_isa_aiop2off;
static rp_ctlmask_t rp_isa_ctlmask;

static int
rp_probe(device_t dev)
{
	int unit;
	CONTROLLER_t *controller;
	int num_aiops;
	CONTROLLER_t *ctlp;
	int retval;

	/*
	 * We have no PnP RocketPort cards.
	 * (At least according to LINT)
	 */
	if (isa_get_logicalid(dev) != 0)
		return (ENXIO);

	/* We need IO port resource to configure an ISA device. */
	if (bus_get_resource_count(dev, SYS_RES_IOPORT, 0) == 0)
		return (ENXIO);

	unit = device_get_unit(dev);
	if (unit >= 4) {
		device_printf(dev, "rpprobe: unit number %d invalid.\n", unit);
		return (ENXIO);
	}
	device_printf(dev, "probing for RocketPort(ISA) unit %d.\n", unit);

	ctlp = device_get_softc(dev);
	bzero(ctlp, sizeof(*ctlp));
	ctlp->dev = dev;
	ctlp->aiop2rid = rp_isa_aiop2rid;
	ctlp->aiop2off = rp_isa_aiop2off;
	ctlp->ctlmask = rp_isa_ctlmask;

	/* The IO ports of AIOPs for an ISA controller are discrete. */
	ctlp->io_num = 1;
	ctlp->io_rid = malloc(sizeof(*(ctlp->io_rid)) * MAX_AIOPS_PER_BOARD, M_DEVBUF, M_NOWAIT | M_ZERO);
	ctlp->io = malloc(sizeof(*(ctlp->io)) * MAX_AIOPS_PER_BOARD, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ctlp->io_rid == NULL || ctlp->io == NULL) {
		device_printf(dev, "rp_attach: Out of memory.\n");
		retval = ENOMEM;
		goto nogo;
	}

	ctlp->bus_ctlp = malloc(sizeof(ISACONTROLLER_t) * 1, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ctlp->bus_ctlp == NULL) {
		device_printf(dev, "rp_attach: Out of memory.\n");
		retval = ENOMEM;
		goto nogo;
	}

	ctlp->io_rid[0] = 0;
	if (rp_controller != NULL) {
		controller = rp_controller;
		ctlp->io[0] = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT, &ctlp->io_rid[0], 0x40, RF_ACTIVE);
	} else {
		controller = rp_controller = ctlp;
		ctlp->io[0] = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT, &ctlp->io_rid[0], 0x44, RF_ACTIVE);
	}
	if (ctlp->io[0] == NULL) {
		device_printf(dev, "rp_attach: Resource not available.\n");
		retval = ENXIO;
		goto nogo;
	}

	num_aiops = sInitController(ctlp,
				controller,
				MAX_AIOPS_PER_BOARD, 0,
				FREQ_DIS, 0);
	if (num_aiops <= 0) {
		device_printf(dev, "board%d init failed.\n", unit);
		retval = ENXIO;
		goto nogo;
	}

	if (rp_controller == NULL)
		rp_controller = controller;
	rp_nisadevs++;

	device_set_desc(dev, "RocketPort ISA");

	return (0);

nogo:
	rp_isareleaseresource(ctlp);

	return (retval);
}

static int
rp_attach(device_t dev)
{
	int	unit;
	int	num_ports, num_aiops;
	int	aiop;
	CONTROLLER_t	*ctlp;
	int	retval;

	unit = device_get_unit(dev);

	ctlp = device_get_softc(dev);

#ifdef notdef
	num_aiops = sInitController(ctlp,
				rp_controller,
				MAX_AIOPS_PER_BOARD, 0,
				FREQ_DIS, 0);
#else
	num_aiops = ctlp->NumAiop;
#endif /* notdef */

	num_ports = 0;
	for(aiop=0; aiop < num_aiops; aiop++) {
		sResetAiopByNum(ctlp, aiop);
		sEnAiop(rp_controller, ctlp, aiop);
		num_ports += sGetAiopNumChan(ctlp, aiop);
	}

	retval = rp_attachcommon(ctlp, num_aiops, num_ports);
	if (retval != 0)
		goto nogo;

	return (0);

nogo:
	rp_isareleaseresource(ctlp);

	return (retval);
}

static void
rp_isareleaseresource(CONTROLLER_t *ctlp)
{
	int i;

	rp_releaseresource(ctlp);

	if (ctlp == rp_controller)
		rp_controller = NULL;
	if (ctlp->io != NULL) {
		for (i = 0 ; i < MAX_AIOPS_PER_BOARD ; i++)
			if (ctlp->io[i] != NULL)
				bus_release_resource(ctlp->dev, SYS_RES_IOPORT, ctlp->io_rid[i], ctlp->io[i]);
		free(ctlp->io, M_DEVBUF);
	}
	if (ctlp->io_rid != NULL)
		free(ctlp->io_rid, M_DEVBUF);
	if (rp_controller != NULL && rp_controller->io[ISACTL(ctlp)->MBaseIO] != NULL) {
		bus_release_resource(rp_controller->dev, SYS_RES_IOPORT, rp_controller->io_rid[ISACTL(ctlp)->MBaseIO], rp_controller->io[ISACTL(ctlp)->MBaseIO]);
		rp_controller->io[ISACTL(ctlp)->MBaseIO] = NULL;
		rp_controller->io_rid[ISACTL(ctlp)->MBaseIO] = 0;
	}
	if (ctlp->bus_ctlp != NULL)
		free(ctlp->bus_ctlp, M_DEVBUF);
}

/***************************************************************************
Function: sInitController
Purpose:  Initialization of controller global registers and controller
	  structure.
Call:	  sInitController(CtlP,MudbacCtlP,AiopNum,
			  IRQNum,Frequency,PeriodicOnly)
	  CONTROLLER_T *CtlP; Ptr to controller structure
	  CONTROLLER_T *MudbacCtlP; Ptr to Mudbac controller structure
	  int AiopNum; Number of Aiops
	  int IRQNum; Interrupt Request number.  Can be any of the following:
			 0: Disable global interrupts
			 3: IRQ 3
			 4: IRQ 4
			 5: IRQ 5
			 9: IRQ 9
			 10: IRQ 10
			 11: IRQ 11
			 12: IRQ 12
			 15: IRQ 15
	  Byte_t Frequency: A flag identifying the frequency
		   of the periodic interrupt, can be any one of the following:
		      FREQ_DIS - periodic interrupt disabled
		      FREQ_137HZ - 137 Hertz
		      FREQ_69HZ - 69 Hertz
		      FREQ_34HZ - 34 Hertz
		      FREQ_17HZ - 17 Hertz
		      FREQ_9HZ - 9 Hertz
		      FREQ_4HZ - 4 Hertz
		   If IRQNum is set to 0 the Frequency parameter is
		   overidden, it is forced to a value of FREQ_DIS.
	  int PeriodicOnly: TRUE if all interrupts except the periodic
			       interrupt are to be blocked.
			    FALSE is both the periodic interrupt and
			       other channel interrupts are allowed.
			    If IRQNum is set to 0 the PeriodicOnly parameter is
			       overidden, it is forced to a value of FALSE.
Return:   int: Number of AIOPs on the controller, or CTLID_NULL if controller
	       initialization failed.

Comments:
	  If periodic interrupts are to be disabled but AIOP interrupts
	  are allowed, set Frequency to FREQ_DIS and PeriodicOnly to FALSE.

	  If interrupts are to be completely disabled set IRQNum to 0.

	  Setting Frequency to FREQ_DIS and PeriodicOnly to TRUE is an
	  invalid combination.

	  This function performs initialization of global interrupt modes,
	  but it does not actually enable global interrupts.  To enable
	  and disable global interrupts use functions sEnGlobalInt() and
	  sDisGlobalInt().  Enabling of global interrupts is normally not
	  done until all other initializations are complete.

	  Even if interrupts are globally enabled, they must also be
	  individually enabled for each channel that is to generate
	  interrupts.

Warnings: No range checking on any of the parameters is done.

	  No context switches are allowed while executing this function.

	  After this function all AIOPs on the controller are disabled,
	  they can be enabled with sEnAiop().
*/
static int
sInitController(	CONTROLLER_T *CtlP,
			CONTROLLER_T *MudbacCtlP,
			int AiopNum,
			int IRQNum,
			Byte_t Frequency,
			int PeriodicOnly)
{
	int		i;
	int		ctl_base, aiop_base, aiop_size;

	CtlP->CtlID = CTLID_0001;		/* controller release 1 */

	ISACTL(CtlP)->MBaseIO = rp_nisadevs;
	if (MudbacCtlP->io[ISACTL(CtlP)->MBaseIO] != NULL) {
		ISACTL(CtlP)->MReg0IO = 0x40 + 0;
		ISACTL(CtlP)->MReg1IO = 0x40 + 1;
		ISACTL(CtlP)->MReg2IO = 0x40 + 2;
		ISACTL(CtlP)->MReg3IO = 0x40 + 3;
	} else {
		MudbacCtlP->io_rid[ISACTL(CtlP)->MBaseIO] = ISACTL(CtlP)->MBaseIO;
		ctl_base = rman_get_start(MudbacCtlP->io[0]) + 0x40 + 0x400 * rp_nisadevs;
		MudbacCtlP->io[ISACTL(CtlP)->MBaseIO] = bus_alloc_resource(MudbacCtlP->dev, SYS_RES_IOPORT, &CtlP->io_rid[ISACTL(CtlP)->MBaseIO], ctl_base, ctl_base + 3, 4, RF_ACTIVE);
		ISACTL(CtlP)->MReg0IO = 0;
		ISACTL(CtlP)->MReg1IO = 1;
		ISACTL(CtlP)->MReg2IO = 2;
		ISACTL(CtlP)->MReg3IO = 3;
	}
#if 1
	ISACTL(CtlP)->MReg2 = 0;			/* interrupt disable */
	ISACTL(CtlP)->MReg3 = 0;			/* no periodic interrupts */
#else
	if(sIRQMap[IRQNum] == 0)		/* interrupts globally disabled */
	{
		ISACTL(CtlP)->MReg2 = 0;		/* interrupt disable */
		ISACTL(CtlP)->MReg3 = 0;		/* no periodic interrupts */
	}
	else
	{
		ISACTL(CtlP)->MReg2 = sIRQMap[IRQNum];	/* set IRQ number */
		ISACTL(CtlP)->MReg3 = Frequency;	/* set frequency */
		if(PeriodicOnly)		/* periodic interrupt only */
		{
			ISACTL(CtlP)->MReg3 |= PERIODIC_ONLY;
		}
	}
#endif
	rp_writeio1(MudbacCtlP,ISACTL(CtlP)->MBaseIO,ISACTL(CtlP)->MReg2IO,ISACTL(CtlP)->MReg2);
	rp_writeio1(MudbacCtlP,ISACTL(CtlP)->MBaseIO,ISACTL(CtlP)->MReg3IO,ISACTL(CtlP)->MReg3);
	sControllerEOI(MudbacCtlP,CtlP);			/* clear EOI if warm init */

	/* Init AIOPs */
	CtlP->NumAiop = 0;
	for(i=0; i < AiopNum; i++)
	{
		if (CtlP->io[i] == NULL) {
			CtlP->io_rid[i] = i;
			aiop_base = rman_get_start(CtlP->io[0]) + 0x400 * i;
			if (rp_nisadevs == 0)
				aiop_size = 0x44;
			else
				aiop_size = 0x40;
			CtlP->io[i] = bus_alloc_resource(CtlP->dev, SYS_RES_IOPORT, &CtlP->io_rid[i], aiop_base, aiop_base + aiop_size - 1, aiop_size, RF_ACTIVE);
		} else
			aiop_base = rman_get_start(CtlP->io[i]);
		rp_writeio1(MudbacCtlP,ISACTL(CtlP)->MBaseIO,
			    ISACTL(CtlP)->MReg2IO,
			    ISACTL(CtlP)->MReg2 | (i & 0x03));	/* AIOP index */
		rp_writeio1(MudbacCtlP,ISACTL(CtlP)->MBaseIO,
			    ISACTL(CtlP)->MReg0IO,
			    (Byte_t)(aiop_base >> 6));		/* set up AIOP I/O in MUDBAC */
		sEnAiop(MudbacCtlP,CtlP,i);			/* enable the AIOP */

		CtlP->AiopID[i] = sReadAiopID(CtlP, i);		/* read AIOP ID */
		if(CtlP->AiopID[i] == AIOPID_NULL)		/* if AIOP does not exist */
		{
			sDisAiop(MudbacCtlP,CtlP,i);		/* disable AIOP */
			bus_release_resource(CtlP->dev, SYS_RES_IOPORT, CtlP->io_rid[i], CtlP->io[i]);
			CtlP->io[i] = NULL;
			break;					/* done looking for AIOPs */
		}

		CtlP->AiopNumChan[i] = sReadAiopNumChan(CtlP, i);	/* num channels in AIOP */
		rp_writeaiop2(CtlP,i,_INDX_ADDR,_CLK_PRE);	/* clock prescaler */
		rp_writeaiop1(CtlP,i,_INDX_DATA,CLOCK_PRESC);
		CtlP->NumAiop++;				/* bump count of AIOPs */
		sDisAiop(MudbacCtlP,CtlP,i);			/* disable AIOP */
	}

	if(CtlP->NumAiop == 0)
		return(-1);
	else
		return(CtlP->NumAiop);
}

/*
 * ARGSUSED
 * Maps (aiop, offset) to rid.
 */
static int
rp_isa_aiop2rid(int aiop, int offset)
{
	/* rid equals to aiop for an ISA controller. */
	return aiop;
}

/*
 * ARGSUSED
 * Maps (aiop, offset) to the offset of resource.
 */
static int
rp_isa_aiop2off(int aiop, int offset)
{
	/* Each aiop has its own resource. */
	return offset;
}

/* Read the int status for an ISA controller. */
static unsigned char
rp_isa_ctlmask(CONTROLLER_t *ctlp)
{
	return sGetControllerIntStatus(rp_controller,ctlp);
}

static device_method_t rp_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rp_probe),
	DEVMETHOD(device_attach,	rp_attach),

	{ 0, 0 }
};

static driver_t rp_driver = {
	"rp",
	rp_methods,
	sizeof(CONTROLLER_t),
};

/*
 * rp can be attached to an isa bus.
 */
DRIVER_MODULE(rp, isa, rp_driver, rp_devclass, 0, 0);
