/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) Comtrol Corporation <support@comtrol.com>
 * All rights reserved.
 *
 * PCI-specific part separated from:
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

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

/* PCI IDs  */
#define RP_VENDOR_ID		0x11FE
#define RP_DEVICE_ID_32I	0x0001
#define RP_DEVICE_ID_8I		0x0002
#define RP_DEVICE_ID_16I	0x0003
#define RP_DEVICE_ID_4Q		0x0004
#define RP_DEVICE_ID_8O		0x0005
#define RP_DEVICE_ID_8J		0x0006
#define RP_DEVICE_ID_4J		0x0007
#define RP_DEVICE_ID_6M		0x000C
#define RP_DEVICE_ID_4M		0x000D
#define RP_DEVICE_ID_UPCI_32	0x0801
#define RP_DEVICE_ID_UPCI_16	0x0803
#define RP_DEVICE_ID_UPCI_8O	0x0805

/**************************************************************************
  MUDBAC remapped for PCI
**************************************************************************/

#define _CFG_INT_PCI	0x40
#define _PCI_INT_FUNC	0x3A

#define PCI_STROB	0x2000
#define INTR_EN_PCI	0x0010

/***************************************************************************
Function: sPCIControllerEOI
Purpose:  Strobe the MUDBAC's End Of Interrupt bit.
Call:	  sPCIControllerEOI(CtlP)
	  CONTROLLER_T *CtlP; Ptr to controller structure
*/
#define sPCIControllerEOI(CtlP) rp_writeio2(CtlP, 0, _PCI_INT_FUNC, PCI_STROB)

/***************************************************************************
Function: sPCIGetControllerIntStatus
Purpose:  Get the controller interrupt status
Call:	  sPCIGetControllerIntStatus(CtlP)
	  CONTROLLER_T *CtlP; Ptr to controller structure
Return:   Byte_t: The controller interrupt status in the lower 4
			 bits.	Bits 0 through 3 represent AIOP's 0
			 through 3 respectively.  If a bit is set that
			 AIOP is interrupting.	Bits 4 through 7 will
			 always be cleared.
*/
#define sPCIGetControllerIntStatus(CTLP) ((rp_readio2(CTLP, 0, _PCI_INT_FUNC) >> 8) & 0x1f)

static devclass_t rp_devclass;

static int rp_pciprobe(device_t dev);
static int rp_pciattach(device_t dev);
#ifdef notdef
static int rp_pcidetach(device_t dev);
static int rp_pcishutdown(device_t dev);
#endif /* notdef */
static void rp_pcireleaseresource(CONTROLLER_t *ctlp);
static int sPCIInitController( CONTROLLER_t *CtlP,
			       int AiopNum,
			       int IRQNum,
			       Byte_t Frequency,
			       int PeriodicOnly,
			       int VendorDevice);
static rp_aiop2rid_t rp_pci_aiop2rid;
static rp_aiop2off_t rp_pci_aiop2off;
static rp_ctlmask_t rp_pci_ctlmask;

/*
 * The following functions are the pci-specific part
 * of rp driver.
 */

static int
rp_pciprobe(device_t dev)
{
	char *s;

	s = NULL;
	if (pci_get_vendor(dev) == RP_VENDOR_ID)
		s = "RocketPort PCI";

	if (s != NULL) {
		device_set_desc(dev, s);
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
rp_pciattach(device_t dev)
{
	int	num_ports, num_aiops;
	int	aiop;
	CONTROLLER_t	*ctlp;
	int	unit;
	int	retval;

	ctlp = device_get_softc(dev);
	bzero(ctlp, sizeof(*ctlp));
	ctlp->dev = dev;
	unit = device_get_unit(dev);
	ctlp->aiop2rid = rp_pci_aiop2rid;
	ctlp->aiop2off = rp_pci_aiop2off;
	ctlp->ctlmask = rp_pci_ctlmask;

	/* The IO ports of AIOPs for a PCI controller are continuous. */
	ctlp->io_num = 1;
	ctlp->io_rid = malloc(sizeof(*(ctlp->io_rid)) * ctlp->io_num, M_DEVBUF, M_NOWAIT | M_ZERO);
	ctlp->io = malloc(sizeof(*(ctlp->io)) * ctlp->io_num, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ctlp->io_rid == NULL || ctlp->io == NULL) {
		device_printf(dev, "rp_pciattach: Out of memory.\n");
		retval = ENOMEM;
		goto nogo;
	}

	ctlp->bus_ctlp = NULL;

	switch (pci_get_device(dev)) {
	case RP_DEVICE_ID_UPCI_16:
	case RP_DEVICE_ID_UPCI_32:
	case RP_DEVICE_ID_UPCI_8O:
		ctlp->io_rid[0] = PCIR_BAR(2);
		break;
	default:
		ctlp->io_rid[0] = PCIR_BAR(0);
		break;
	}
	ctlp->io[0] = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
		&ctlp->io_rid[0], RF_ACTIVE);
	if(ctlp->io[0] == NULL) {
		device_printf(dev, "ioaddr mapping failed for RocketPort(PCI).\n");
		retval = ENXIO;
		goto nogo;
	}

	num_aiops = sPCIInitController(ctlp,
				       MAX_AIOPS_PER_BOARD, 0,
				       FREQ_DIS, 0, pci_get_device(dev));

	num_ports = 0;
	for(aiop=0; aiop < num_aiops; aiop++) {
		sResetAiopByNum(ctlp, aiop);
		num_ports += sGetAiopNumChan(ctlp, aiop);
	}

	retval = rp_attachcommon(ctlp, num_aiops, num_ports);
	if (retval != 0)
		goto nogo;

	return (0);

nogo:
	rp_pcireleaseresource(ctlp);

	return (retval);
}

static int
rp_pcidetach(device_t dev)
{
	CONTROLLER_t	*ctlp;

	ctlp = device_get_softc(dev);
	rp_pcireleaseresource(ctlp);

	return (0);
}

static int
rp_pcishutdown(device_t dev)
{
	CONTROLLER_t	*ctlp;

	ctlp = device_get_softc(dev);
	rp_pcireleaseresource(ctlp);

	return (0);
}

static void
rp_pcireleaseresource(CONTROLLER_t *ctlp)
{
	rp_releaseresource(ctlp);
	if (ctlp->io != NULL) {
		if (ctlp->io[0] != NULL)
			bus_release_resource(ctlp->dev, SYS_RES_IOPORT, ctlp->io_rid[0], ctlp->io[0]);
		free(ctlp->io, M_DEVBUF);
		ctlp->io = NULL;
	}
	if (ctlp->io_rid != NULL) {
		free(ctlp->io_rid, M_DEVBUF);
		ctlp->io = NULL;
	}
}

static int
sPCIInitController( CONTROLLER_t *CtlP,
		    int AiopNum,
		    int IRQNum,
		    Byte_t Frequency,
		    int PeriodicOnly,
		    int VendorDevice)
{
	int		i;

	CtlP->CtlID = CTLID_0001;	/* controller release 1 */

	sPCIControllerEOI(CtlP);

	/* Init AIOPs */
	CtlP->NumAiop = 0;
	for(i=0; i < AiopNum; i++)
	{
		/*device_printf(CtlP->dev, "aiop %d.\n", i);*/
		CtlP->AiopID[i] = sReadAiopID(CtlP, i);	/* read AIOP ID */
		/*device_printf(CtlP->dev, "ID = %d.\n", CtlP->AiopID[i]);*/
		if(CtlP->AiopID[i] == AIOPID_NULL)	/* if AIOP does not exist */
		{
			break;				/* done looking for AIOPs */
		}

		switch( VendorDevice ) {
		case RP_DEVICE_ID_4Q:
		case RP_DEVICE_ID_4J:
		case RP_DEVICE_ID_4M:
      			CtlP->AiopNumChan[i] = 4;
			break;
		case RP_DEVICE_ID_6M:
      			CtlP->AiopNumChan[i] = 6;
			break;
		case RP_DEVICE_ID_8O:
		case RP_DEVICE_ID_8J:
		case RP_DEVICE_ID_8I:
		case RP_DEVICE_ID_16I:
		case RP_DEVICE_ID_32I:
      			CtlP->AiopNumChan[i] = 8;
			break;
		default:
#ifdef notdef
      			CtlP->AiopNumChan[i] = 8;
#else
      			CtlP->AiopNumChan[i] = sReadAiopNumChan(CtlP, i);
#endif /* notdef */
			break;
		}
		/*device_printf(CtlP->dev, "%d channels.\n", CtlP->AiopNumChan[i]);*/
		rp_writeaiop2(CtlP, i, _INDX_ADDR,_CLK_PRE);	/* clock prescaler */
		/*device_printf(CtlP->dev, "configuring clock prescaler.\n");*/
		rp_writeaiop1(CtlP, i, _INDX_DATA,CLOCK_PRESC);
		/*device_printf(CtlP->dev, "configured clock prescaler.\n");*/
		CtlP->NumAiop++;				/* bump count of AIOPs */
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
rp_pci_aiop2rid(int aiop, int offset)
{
	/* Always return zero for a PCI controller. */
	return 0;
}

/*
 * ARGSUSED
 * Maps (aiop, offset) to the offset of resource.
 */
static int
rp_pci_aiop2off(int aiop, int offset)
{
	/* Each AIOP reserves 0x40 bytes. */
	return aiop * 0x40 + offset;
}

/* Read the int status for a PCI controller. */
static unsigned char
rp_pci_ctlmask(CONTROLLER_t *ctlp)
{
	return sPCIGetControllerIntStatus(ctlp);
}

static device_method_t rp_pcimethods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rp_pciprobe),
	DEVMETHOD(device_attach,	rp_pciattach),
	DEVMETHOD(device_detach,	rp_pcidetach),
	DEVMETHOD(device_shutdown,	rp_pcishutdown),

	{ 0, 0 }
};

static driver_t rp_pcidriver = {
	"rp",
	rp_pcimethods,
	sizeof(CONTROLLER_t),
};

/*
 * rp can be attached to a pci bus.
 */
DRIVER_MODULE(rp, pci, rp_pcidriver, rp_devclass, 0, 0);
