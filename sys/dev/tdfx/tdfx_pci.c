/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2000-2001 by Coleman Kane <cokane@FreeBSD.org>
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Gardner Buchanan.
 * 4. The name of Gardner Buchanan may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* 3dfx driver for FreeBSD 4.x - Finished 11 May 2000, 12:25AM ET
 *
 * Copyright (C) 2000-2001, by Coleman Kane <cokane@FreeBSD.org>, 
 * based upon the 3dfx driver written for linux, by Daryll Straus, Jon Taylor,
 * and Jens Axboe, located at http://linux.3dfx.com.
 */

#include <sys/param.h>

#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/filio.h>
#include <sys/ioccom.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

/* rman.h depends on machine/bus.h */
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/rman.h>

#include <dev/tdfx/tdfx_io.h>
#include <dev/tdfx/tdfx_vars.h>
#include <dev/tdfx/tdfx_pci.h>


static devclass_t tdfx_devclass;


static int tdfx_count = 0;


/* Set up the boot probe/attach routines */
static device_method_t tdfx_methods[] = {
	DEVMETHOD(device_probe,		tdfx_probe),
	DEVMETHOD(device_attach,	tdfx_attach),
	DEVMETHOD(device_detach,	tdfx_detach),
	DEVMETHOD(device_shutdown,	tdfx_shutdown),
	{ 0, 0 }
};

static MALLOC_DEFINE(M_TDFX,"tdfx_driver","3DFX Graphics[/2D]/3D Accelerators");

/* Char. Dev. file operations structure */
static struct cdevsw tdfx_cdev = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_open =	tdfx_open,
	.d_close =	tdfx_close,
	.d_ioctl =	tdfx_ioctl,
	.d_mmap =	tdfx_mmap,
	.d_name =	"tdfx",
};

static int
tdfx_probe(device_t dev)
{
	/*
	 * probe routine called on kernel boot to register supported devices. We get
	 * a device structure to work with, and we can test the VENDOR/DEVICE IDs to
	 * see if this PCI device is one that we support. Return BUS_PRROBE_DEFAULT
	 * if yes, ENXIO if not.
	 */
	switch(pci_get_devid(dev)) {
	case PCI_DEVICE_ALLIANCE_AT3D:
		device_set_desc(dev, "ProMotion At3D 3D Accelerator");
		return BUS_PROBE_DEFAULT;
	case PCI_DEVICE_3DFX_VOODOO2:
		device_set_desc(dev, "3DFX Voodoo II 3D Accelerator");
		return BUS_PROBE_DEFAULT;
	/*case PCI_DEVICE_3DFX_BANSHEE:
		device_set_desc(dev, "3DFX Voodoo Banshee 2D/3D Graphics Accelerator");
		return BUS_PROBE_DEFAULT;
	case PCI_DEVICE_3DFX_VOODOO3:
		device_set_desc(dev, "3DFX Voodoo3 2D/3D Graphics Accelerator");
		return BUS_PROBE_DEFAULT;*/
	case PCI_DEVICE_3DFX_VOODOO1:
		device_set_desc(dev, "3DFX Voodoo Graphics 3D Accelerator");
		return BUS_PROBE_DEFAULT;
	}

	return ENXIO;
}

static int
tdfx_attach(device_t dev) { 
	/*
	 * The attach routine is called after the probe routine successfully says it
	 * supports a given card. We now proceed to initialize this card for use with
	 * the system. I want to map the device memory for userland allocation and
	 * fill an information structure with information on this card. I'd also like
	 * to set Write Combining with the MTRR code so that we can hopefully speed
	 * up memory writes. The last thing is to register the character device
	 * interface to the card, so we can open it from /dev/3dfxN, where N is a
	 * small, whole number.
	 */
	struct tdfx_softc *tdfx_info;
	/* rid value tells bus_alloc_resource where to find the addresses of ports or
	 * of memory ranges in the PCI config space*/
	int rid = PCIR_BAR(0);

	/* Increment the card counter (for the ioctl code) */
	tdfx_count++;

	/* Fill the soft config struct with info about this device*/
	tdfx_info = device_get_softc(dev);
	tdfx_info->dev = dev;
	tdfx_info->vendor = pci_get_vendor(dev);
	tdfx_info->type = pci_get_devid(dev) >> 16;
	tdfx_info->bus = pci_get_bus(dev);
	tdfx_info->dv = pci_get_slot(dev);
	tdfx_info->curFile = NULL;

	/* 
	 *	Get the Memory Location from the PCI Config, mask out lower word, since
	 * the config space register is only one word long (this is nicer than a
	 * bitshift).
	 */
	tdfx_info->addr0 = (pci_read_config(dev, 0x10, 4) & 0xffff0000);
#ifdef DEBUG
	device_printf(dev, "Base0 @ 0x%x\n", tdfx_info->addr0);
#endif
	/* Notify the VM that we will be mapping some memory later */
	tdfx_info->memrange = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
		&rid, RF_ACTIVE | RF_SHAREABLE);
	if(tdfx_info->memrange == NULL) {
#ifdef DEBUG
		device_printf(dev, "Error mapping mem, won't be able to use mmap()\n");
#endif
		tdfx_info->memrid = 0;
	}
	else {
		tdfx_info->memrid = rid;
#ifdef DEBUG
		device_printf(dev, "Mapped to: 0x%x\n", 
				(unsigned int)rman_get_start(tdfx_info->memrange));
#endif
	}

	/* Setup for Voodoo3 and Banshee, PIO and an extram Memrange */
	if(pci_get_devid(dev) == PCI_DEVICE_3DFX_VOODOO3 ||
		pci_get_devid(dev) == PCI_DEVICE_3DFX_BANSHEE) {
		rid = 0x14;	/* 2nd mem map */
		tdfx_info->addr1 = (pci_read_config(dev, 0x14, 4) & 0xffff0000);
#ifdef DEBUG
		device_printf(dev, "Base1 @ 0x%x\n", tdfx_info->addr1);
#endif
		tdfx_info->memrange2 = bus_alloc_resource_any(dev,
			SYS_RES_MEMORY, &rid, RF_ACTIVE | RF_SHAREABLE);
		if(tdfx_info->memrange2 == NULL) {
#ifdef DEBUG
			device_printf(dev, "Mem1 couldn't be allocated, glide may not work.");
#endif
			tdfx_info->memrid2 = 0;
		}
		else {
			tdfx_info->memrid2 = rid;
		}
		/* Now to map the PIO stuff */
		rid = PCIR_IOBASE0_2;
		tdfx_info->pio0 = pci_read_config(dev, 0x2c, 2);
		tdfx_info->pio0max = pci_read_config(dev, 0x30, 2) + tdfx_info->pio0;
		tdfx_info->piorange = bus_alloc_resource_any(dev,
			SYS_RES_IOPORT, &rid, RF_ACTIVE | RF_SHAREABLE);
		if(tdfx_info->piorange == NULL) {
#ifdef DEBUG
			device_printf(dev, "Couldn't map PIO range.");
#endif
			tdfx_info->piorid = 0;
		}
		else {
			tdfx_info->piorid = rid;
		}
	} else {
	  tdfx_info->addr1 = 0;
	  tdfx_info->memrange2 = NULL;
	  tdfx_info->piorange = NULL;
	}

	/* 
	 *	Set Writecombining, or at least Uncacheable for the memory region, if we
	 * are able to
	 */

	if(tdfx_setmtrr(dev) != 0) {
#ifdef DEBUG
		device_printf(dev, "Some weird error setting MTRRs");
#endif
		return -1;
	}

	/* 
	 * make_dev registers the cdev to access the 3dfx card from /dev
	 *	use hex here for the dev num, simply to provide better support if > 10
	 * voodoo cards, for the mad. The user must set the link.
	 * Why would we want that many voodoo cards anyhow? 
	 */
	tdfx_info->devt = make_dev(&tdfx_cdev, device_get_unit(dev),
		UID_ROOT, GID_WHEEL, 0600, "3dfx%x", device_get_unit(dev));
	tdfx_info->devt->si_drv1 = tdfx_info;
	
	return 0;
}

static int
tdfx_detach(device_t dev) {
	struct tdfx_softc* tdfx_info;
	int retval;
	tdfx_info = device_get_softc(dev);
	
	/* Delete allocated resource, of course */
	bus_release_resource(dev, SYS_RES_MEMORY, tdfx_info->memrid,
			tdfx_info->memrange);

	/* Release extended Voodoo3/Banshee resources */
	if(pci_get_devid(dev) == PCI_DEVICE_3DFX_BANSHEE || 
			pci_get_devid(dev) == PCI_DEVICE_3DFX_VOODOO3) {
		if(tdfx_info->memrange2 != NULL)
			bus_release_resource(dev, SYS_RES_MEMORY, tdfx_info->memrid2,
				tdfx_info->memrange);
	/*	if(tdfx_info->piorange != NULL)
			bus_release_resource(dev, SYS_RES_IOPORT, tdfx_info->piorid,
				tdfx_info->piorange);*/
	}		

	/* Though it is safe to leave the WRCOMB support since the 
		mem driver checks for it, we should remove it in order
		to free an MTRR for another device */
	retval = tdfx_clrmtrr(dev);
#ifdef DEBUG
	if(retval != 0) 
		printf("tdfx: For some reason, I couldn't clear the mtrr\n");
#endif
	/* Remove device entry when it can no longer be accessed */
   destroy_dev(tdfx_info->devt);
	return(0);
}

static int
tdfx_shutdown(device_t dev) {
#ifdef DEBUG
	device_printf(dev, "tdfx: Device Shutdown\n");
#endif
	return 0;
}

static int
tdfx_clrmtrr(device_t dev) {
	/* This function removes the MTRR set by the attach call, so it can be used
	 * in the future by other drivers. 
	 */
	int retval, act;
	struct tdfx_softc *tdfx_info = device_get_softc(dev);
	
	act = MEMRANGE_SET_REMOVE;
	retval = mem_range_attr_set(&tdfx_info->mrdesc, &act);
	return retval;
}
	
static int
tdfx_setmtrr(device_t dev) {
	/*
	 * This is the MTRR setting function for the 3dfx card. It is called from
	 * tdfx_attach. If we can't set the MTRR properly, it's not the end of the
	 * world. We can still continue, just with slightly (very slightly) degraded
	 * performance.
	 */
	int retval = 0, act;
	struct tdfx_softc *tdfx_info = device_get_softc(dev);

	/* The older Voodoo cards have a shorter memrange than the newer ones */
	if((pci_get_devid(dev) == PCI_DEVICE_3DFX_VOODOO1) || (pci_get_devid(dev) ==
			PCI_DEVICE_3DFX_VOODOO2)) {
		tdfx_info->mrdesc.mr_len = 0x400000;

		/* The memory descriptor is described as the top 15 bits of the real
			address */
		tdfx_info->mrdesc.mr_base = tdfx_info->addr0 & 0xfffe0000;
	}
	else if((pci_get_devid(dev) == PCI_DEVICE_3DFX_VOODOO3) ||
			(pci_get_devid(dev) == PCI_DEVICE_3DFX_BANSHEE)) {
		tdfx_info->mrdesc.mr_len = 0x1000000;
		/* The Voodoo3 and Banshee LFB is the second memory address */
		/* The memory descriptor is described as the top 15 bits of the real
			address */
		tdfx_info->mrdesc.mr_base = tdfx_info->addr1 & 0xfffe0000;
	}
	else
		 return 0;	
	/* 
    *	The Alliance Pro Motion AT3D was not mentioned in the linux
	 * driver as far as MTRR support goes, so I just won't put the
	 * code in here for it. This is where it should go, though. 
	 */

	/* Firstly, try to set write combining */
	tdfx_info->mrdesc.mr_flags = MDF_WRITECOMBINE;
	bcopy("tdfx", &tdfx_info->mrdesc.mr_owner, 4);
	act = MEMRANGE_SET_UPDATE;
	retval = mem_range_attr_set(&tdfx_info->mrdesc, &act);

	if(retval == 0) {
#ifdef DEBUG
		device_printf(dev, "MTRR Set Correctly for tdfx\n");
#endif
	} else if((pci_get_devid(dev) == PCI_DEVICE_3DFX_VOODOO2) ||
		(pci_get_devid(dev) == PCI_DEVICE_3DFX_VOODOO1)) {
		/* if, for some reason we can't set the WRCOMB range with the V1/V2, we
		 * can still possibly use the UNCACHEABLE region for it instead, and help
		 * out in a small way */
		tdfx_info->mrdesc.mr_flags = MDF_UNCACHEABLE;
		/* This length of 1000h was taken from the linux device driver... */
		tdfx_info->mrdesc.mr_len = 0x1000;

		/*
		 * If, for some reason, we can't set the MTRR (N/A?) we may still continue
		 */
#ifdef DEBUG
		device_printf(dev, "MTRR Set Type Uncacheable %x\n",
		    (u_int32_t)tdfx_info->mrdesc.mr_base);
#endif
	}
#ifdef DEBUG
	else {
		device_printf(dev, "Couldn't Set MTRR\n");
		return 0;
	}
#endif
	return 0;
}
		
static int
tdfx_open(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	/* 
	 *	The open cdev method handles open(2) calls to /dev/3dfx[n] 
	 * We can pretty much allow any opening of the device.
	 */
	struct tdfx_softc *tdfx_info = dev->si_drv1;
	if(tdfx_info->busy != 0) return EBUSY;
#ifdef	DEBUG
	printf("3dfx: Opened by #%d\n", td->td_proc->p_pid);
#endif
	/* Set the driver as busy */
	tdfx_info->busy++;
	return 0;
}

static int 
tdfx_close(struct cdev *dev, int fflag, int devtype, struct thread *td) 
{
	/* 
	 *	The close cdev method handles close(2) calls to /dev/3dfx[n] 
	 * We'll always want to close the device when it's called.
	 */
	struct tdfx_softc *tdfx_info = dev->si_drv1;
	if(tdfx_info->busy == 0) return EBADF;
	tdfx_info->busy = 0;
#ifdef	DEBUG
	printf("Closed by #%d\n", td->td_proc->p_pid);
#endif
	return 0;
}

static int
tdfx_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr)
{
	/* 
	 * mmap(2) is called by a user process to request that an area of memory
	 * associated with this device be mapped for the process to work with. Nprot
	 * holds the protections requested, PROT_READ, PROT_WRITE, or both.
	 */

	/**** OLD GET CONFIG ****/
	/* struct tdfx_softc* tdfx_info; */
	
	/* Get the configuration for our card XXX*/
	/*tdfx_info = dev->si_drv1; */
	/************************/

	struct tdfx_softc* tdfx_info[2];
	
	tdfx_info[0] = (struct tdfx_softc*)devclass_get_softc(tdfx_devclass, 0);

	/* If, for some reason, its not configured, we bail out */
	if(tdfx_info[0] == NULL) {
#ifdef	DEBUG
	   printf("tdfx: tdfx_info (softc) is NULL\n");
#endif
	   return -1;
	}

	/* We must stay within the bound of our address space */
	if((offset & 0xff000000) == tdfx_info[0]->addr0) {
		offset &= 0xffffff;
		*paddr = rman_get_start(tdfx_info[0]->memrange) + offset;
		return 0;
	}
	
	if(tdfx_count > 1) {
		tdfx_info[1] = (struct tdfx_softc*)devclass_get_softc(tdfx_devclass, 1);
		if((offset & 0xff000000) == tdfx_info[1]->addr0) {
			offset &= 0xffffff;
			*paddr = rman_get_start(tdfx_info[1]->memrange) +
			    offset;
			return 0;
		}
	}

	/* See if the Banshee/V3 LFB is being requested */
	/*if(tdfx_info->memrange2 != NULL && (offset & 0xff000000) ==
			tdfx_info->addr1) {
	  	offset &= 0xffffff;
		return atop(rman_get_start(tdfx_info[1]->memrange2) + offset);
	}*/ /* VoodooNG code */

	/* The ret call */
	/* atop -> address to page
	 * rman_get_start, get the (struct resource*)->r_start member,
	 * the mapping base address.
	 */
	return -1;
}

static int
tdfx_query_boards(void) {
	/* 
    *	This returns the number of installed tdfx cards, we have been keeping
	 * count, look at tdfx_attach 
	 */
	return tdfx_count;
}

static int
tdfx_query_fetch(u_int cmd, struct tdfx_pio_data *piod)
{
	/* XXX Comment this later, after careful inspection and spring cleaning :) */
	/* Various return values 8bit-32bit */
	u_int8_t  ret_byte;
	u_int16_t ret_word;
	u_int32_t ret_dword;
	struct tdfx_softc* tdfx_info = NULL;	

	/* This one depend on the tdfx_* structs being properly initialized */

	/*piod->device &= 0xf;*/
	if((piod == NULL) ||(tdfx_count <= piod->device) ||
			(piod->device < 0)) {
#ifdef DEBUG
		printf("tdfx: Bad device or internal struct in tdfx_query_fetch\n");
#endif
		return -EINVAL;
	}

	tdfx_info = (struct tdfx_softc*)devclass_get_softc(tdfx_devclass,
			piod->device);

	if(tdfx_info == NULL) return -ENXIO;

	/* We must restrict the size reads from the port, since to high or low of a
	 * size witll result in wrong data being passed, and that's bad */
	/* A few of these were pulled during the attach phase */
	switch(piod->port) {
		case PCI_VENDOR_ID_FREEBSD:
			if(piod->size != 2) return -EINVAL;
			copyout(&tdfx_info->vendor, piod->value, piod->size);
			return 0;
		case PCI_DEVICE_ID_FREEBSD:
			if(piod->size != 2) return -EINVAL;
			copyout(&tdfx_info->type, piod->value, piod->size);
			return 0;
		case PCI_BASE_ADDRESS_0_FREEBSD:
			if(piod->size != 4) return -EINVAL;
			copyout(&tdfx_info->addr0, piod->value, piod->size);
			return 0;
		case PCI_BASE_ADDRESS_1_FREEBSD:
			if(piod->size != 4) return -EINVAL;
			copyout(&tdfx_info->addr1, piod->value, piod->size);
			return 0;
		case PCI_PRIBUS_FREEBSD:
			if(piod->size != 1) return -EINVAL;
			break;
		case PCI_IOBASE_0_FREEBSD:
			if(piod->size != 2) return -EINVAL;
			break;
		case PCI_IOLIMIT_0_FREEBSD:
			if(piod->size != 2) return -EINVAL;
			break;
		case SST1_PCI_SPECIAL1_FREEBSD:
			if(piod->size != 4) return -EINVAL;
			break;
		case PCI_REVISION_ID_FREEBSD:
			if(piod->size != 1) return -EINVAL;
			break;
		case SST1_PCI_SPECIAL4_FREEBSD:
			if(piod->size != 4) return -EINVAL;
			break;
		default:
			return -EINVAL;
	}

	
	/* Read the value and return */
	switch(piod->size) {
		case 1:
			ret_byte = pci_read_config(tdfx_info[piod->device].dev, 
					piod->port, 1);
			copyout(&ret_byte, piod->value, 1);
			break;
		case 2:
			ret_word = pci_read_config(tdfx_info[piod->device].dev, 
					piod->port, 2);
			copyout(&ret_word, piod->value, 2);
			break;
		case 4:
			ret_dword = pci_read_config(tdfx_info[piod->device].dev, 
					piod->port, 4);
			copyout(&ret_dword, piod->value, 4);
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

static int
tdfx_query_update(u_int cmd, struct tdfx_pio_data *piod)
{
	/* XXX Comment this later, after careful inspection and spring cleaning :) */
	/* Return vals */
	u_int8_t  ret_byte;
	u_int16_t ret_word;
	u_int32_t ret_dword;

	/* Port vals, mask */
	u_int32_t retval, preval, mask;
	struct tdfx_softc* tdfx_info = NULL;
			

	if((piod == NULL) || (piod->device >= (tdfx_count &
					0xf))) {
#ifdef DEBUG
		printf("tdfx: Bad struct or device in tdfx_query_update\n");
#endif
		return -EINVAL;
	}

	tdfx_info = (struct tdfx_softc*)devclass_get_softc(tdfx_devclass, 
			piod->device);
	if(tdfx_info == NULL) return -ENXIO;
	/* Code below this line in the fuction was taken from the 
	 * Linux driver and converted for freebsd. */

	/* Check the size for all the ports, to make sure stuff doesn't get messed up
	 * by poorly written clients */

	switch(piod->port) {
		case PCI_COMMAND_FREEBSD:
			if(piod->size != 2) return -EINVAL;
			break;
		case SST1_PCI_SPECIAL1_FREEBSD:
			if(piod->size != 4) return -EINVAL;
			break;
		case SST1_PCI_SPECIAL2_FREEBSD:
			if(piod->size != 4) return -EINVAL;
			break;
		case SST1_PCI_SPECIAL3_FREEBSD:
			if(piod->size != 4) return -EINVAL;
			break;
		case SST1_PCI_SPECIAL4_FREEBSD:
			if(piod->size != 4) return -EINVAL;
			break;
		default:
			return -EINVAL;
	}
	/* Read the current value */
	retval = pci_read_config(tdfx_info->dev, piod->port & ~3, 4);
			
	/* These set up a mask to use, since apparently they wanted to write 4 bytes
	 * at once to the ports */
	switch (piod->size) {
		case 1:
			copyin(piod->value, &ret_byte, 1);
			preval = ret_byte << (8 * (piod->port & 0x3));
			mask = 0xff << (8 * (piod->port & 0x3));
			break;
		case 2:
			copyin(piod->value, &ret_word, 2);
			preval = ret_word << (8 * (piod->port & 0x3));
			mask = 0xffff << (8 * (piod->port & 0x3));
			break;
		case 4:
			copyin(piod->value, &ret_dword, 4);
			preval = ret_dword;
			mask = ~0;
			break;
		default:
			return -EINVAL;
	}
	/* Finally, combine the values and write it to the port */
	retval = (retval & ~mask) | preval;
	pci_write_config(tdfx_info->dev, piod->port & ~3, retval, 4);
   
	return 0;
}

/* For both of these, I added a variable named workport of type u_int so
 * that I could eliminate the warning about my data type size. The
 * applications expect the port to be of type short, so I needed to change
 * this within the function */
static int
tdfx_do_pio_rd(struct tdfx_pio_data *piod)
{
	/* Return val */
	u_int8_t  ret_byte;
	u_int 	 workport;
	struct tdfx_softc *tdfx_info = 
		(struct tdfx_softc*)devclass_get_softc(tdfx_devclass, piod->device);
		
	/* Restricts the access of ports other than those we use */
	if(((piod->port != VGA_INPUT_STATUS_1C) || (piod->port != SC_INDEX) ||
		(piod->port != SC_DATA) || (piod->port != VGA_MISC_OUTPUT_READ)) &&
		(piod->port < tdfx_info->pio0) && (piod->port > tdfx_info->pio0max))
		return -EPERM;
	
	/* All VGA STATUS REGS are byte registers, size should never be > 1 */
	if(piod->size != 1) {
		return -EINVAL;
	}

	/* Write the data to the intended port */
	workport = piod->port;
	ret_byte = inb(workport);
	copyout(&ret_byte, piod->value, sizeof(u_int8_t));
	return 0;
}

static int
tdfx_do_pio_wt(struct tdfx_pio_data *piod) 
{
	/* return val */
	u_int8_t  ret_byte;
	u_int		 workport;
	struct tdfx_softc *tdfx_info = (struct
			tdfx_softc*)devclass_get_softc(tdfx_devclass, piod->device);
	/* Replace old switch w/ massive if(...) */
	/* Restricts the access of ports other than those we use */
	if(((piod->port != SC_INDEX) && (piod->port != SC_DATA) && 
		(piod->port != VGA_MISC_OUTPUT_READ)) /* Can't write VGA_ST_1C */ &&
		(piod->port < tdfx_info->pio0) && (piod->port > tdfx_info->pio0max))
		return -EPERM;
	
	/* All VGA STATUS REGS are byte registers, size should never be > 1 */
	if(piod->size != 1) {
		return -EINVAL;
	}

	/* Write the data to the intended port */
	copyin(piod->value, &ret_byte, sizeof(u_int8_t));
	workport = piod->port;
	outb(workport, ret_byte);
	return 0;
}

static int
tdfx_do_query(u_int cmd, struct tdfx_pio_data *piod)
{
	/* There are three sub-commands to the query 0x33 */
	switch(_IOC_NR(cmd)) {
		case 2:
			return tdfx_query_boards();
			break;
		case 3:
			return tdfx_query_fetch(cmd, piod);
			break;
		case 4:
			return tdfx_query_update(cmd, piod);
			break;
		default:
			/* In case we are thrown a bogus sub-command! */
#ifdef DEBUG
			printf("Bad Sub-cmd: 0x%x\n", _IOC_NR(cmd));
#endif
			return -EINVAL;
	}
}

static int
tdfx_do_pio(u_int cmd, struct tdfx_pio_data *piod) 
{
	/* Two types of PIO, INPUT and OUTPUT, as the name suggests */
	switch(_IOC_DIR(cmd)) {
		case IOCV_OUT: 
			return tdfx_do_pio_rd(piod);
			break;
		case IOCV_IN:
			return tdfx_do_pio_wt(piod);
			break;
		default:
			return -EINVAL;
	}
}

/* Calls to ioctl(2) eventually end up here. Unhandled ioctls return an ENXIO,
 * normally, you would read in the data pointed to by data, then write your
 * output to it. The ioctl *should* normally return zero if everything is
 * alright, but 3dfx didn't make it that way...
 *
 * For all of the ioctl code, in the event of a real error,
 * we return -Exxxx rather than simply Exxxx. The reason for this
 * is that the ioctls actually RET information back to the program
 * sometimes, rather than filling it in the passed structure. We
 * want to distinguish errors from useful data, and maintain compatibility.
 *
 * There is this portion of the proc struct called p_retval[], we can store a
 * return value in td->td_retval[0] and place the return value if it is positive
 * in there, then we can return 0 (good). If the return value is negative, we
 * can return -retval and the error should be properly handled.
 */
static int
tdfx_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
{
	int retval = 0;
	struct tdfx_pio_data *piod = (struct tdfx_pio_data*)data;
#ifdef	DEBUG
	printf("IOCTL'd by #%d, cmd: 0x%x, data: %p\n", td->td_proc->p_pid, (u_int32_t)cmd,
			piod);
#endif
	switch(_IOC_TYPE(cmd)) {
		/* Return the real error if negative, or simply stick the valid return
		 * in td->td_retval */
	case 0x33:
			/* The '3'(0x33) type IOCTL is for querying the installed cards */
			if((retval = tdfx_do_query(cmd, piod)) > 0) td->td_retval[0] = retval;
			else return -retval;
			break;
		case 0:
			/* The 0 type IOCTL is for programmed I/O methods */
			if((tdfx_do_pio(cmd, piod)) > 0) td->td_retval[0] = retval;
			else return -retval;
			break;
		default:
			/* Technically, we won't reach this from linux emu, but when glide
			 * finally gets ported, watch out! */
#ifdef DEBUG
			printf("Bad IOCTL from #%d\n", td->td_proc->p_pid);
#endif
			return ENXIO;
	}

	return 0;
}

/* This is the device driver struct. This is sent to the driver subsystem to
 * register the method structure and the info strcut space for this particular
 * instance of the driver.
 */
static driver_t tdfx_driver = {
	"tdfx", 
	tdfx_methods,
	sizeof(struct tdfx_softc),
};

/* Tell Mr. Kernel about us! */
DRIVER_MODULE(tdfx, pci, tdfx_driver, tdfx_devclass, 0, 0);
MODULE_DEPEND(tdfx, mem, 1, 1, 1);
MODULE_VERSION(tdfx, 1);
