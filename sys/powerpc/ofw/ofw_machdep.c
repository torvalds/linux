/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 1996 Wolfgang Solfrank.
 * Copyright (C) 1996 TooLs GmbH.
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
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $NetBSD: ofw_machdep.c,v 1.5 2000/05/23 13:25:43 tsubai Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/smp.h>
#include <sys/stat.h>
#include <sys/endian.h>

#include <net/ethernet.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_pci.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_subr.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_page.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/md_var.h>
#include <machine/platform.h>
#include <machine/ofw_machdep.h>
#include <machine/trap.h>

#include <contrib/libfdt/libfdt.h>

#ifdef POWERNV
#include <powerpc/powernv/opal.h>
#endif

static void	*fdt;
int		ofw_real_mode;

#ifdef AIM
extern register_t ofmsr[5];
extern void	*openfirmware_entry;
char		save_trap_init[0x2f00];          /* EXC_LAST */
char		save_trap_of[0x2f00];            /* EXC_LAST */

int		ofwcall(void *);
static int	openfirmware(void *args);

__inline void
ofw_save_trap_vec(char *save_trap_vec)
{
	if (!ofw_real_mode || !hw_direct_map)
                return;

	bcopy((void *)PHYS_TO_DMAP(EXC_RST), save_trap_vec, EXC_LAST - EXC_RST);
}

static __inline void
ofw_restore_trap_vec(char *restore_trap_vec)
{
	if (!ofw_real_mode || !hw_direct_map)
                return;

	bcopy(restore_trap_vec, (void *)PHYS_TO_DMAP(EXC_RST),
	    EXC_LAST - EXC_RST);
	__syncicache((void *)PHYS_TO_DMAP(EXC_RSVD), EXC_LAST - EXC_RSVD);
}

/*
 * Saved SPRG0-3 from OpenFirmware. Will be restored prior to the callback.
 */
register_t	ofw_sprg0_save;

static __inline void
ofw_sprg_prepare(void)
{
	if (ofw_real_mode)
		return;
	
	/*
	 * Assume that interrupt are disabled at this point, or
	 * SPRG1-3 could be trashed
	 */
#ifdef __powerpc64__
	__asm __volatile("mtsprg1 %0\n\t"
	    		 "mtsprg2 %1\n\t"
			 "mtsprg3 %2\n\t"
			 :
			 : "r"(ofmsr[2]),
			 "r"(ofmsr[3]),
			 "r"(ofmsr[4]));
#else
	__asm __volatile("mfsprg0 %0\n\t"
			 "mtsprg0 %1\n\t"
	    		 "mtsprg1 %2\n\t"
	    		 "mtsprg2 %3\n\t"
			 "mtsprg3 %4\n\t"
			 : "=&r"(ofw_sprg0_save)
			 : "r"(ofmsr[1]),
			 "r"(ofmsr[2]),
			 "r"(ofmsr[3]),
			 "r"(ofmsr[4]));
#endif
}

static __inline void
ofw_sprg_restore(void)
{
	if (ofw_real_mode)
		return;
	
	/*
	 * Note that SPRG1-3 contents are irrelevant. They are scratch
	 * registers used in the early portion of trap handling when
	 * interrupts are disabled.
	 *
	 * PCPU data cannot be used until this routine is called !
	 */
#ifndef __powerpc64__
	__asm __volatile("mtsprg0 %0" :: "r"(ofw_sprg0_save));
#endif
}
#endif

static int
parse_ofw_memory(phandle_t node, const char *prop, struct mem_region *output)
{
	cell_t address_cells, size_cells;
	cell_t OFmem[4 * PHYS_AVAIL_SZ];
	int sz, i, j;
	phandle_t phandle;

	sz = 0;

	/*
	 * Get #address-cells from root node, defaulting to 1 if it cannot
	 * be found.
	 */
	phandle = OF_finddevice("/");
	if (OF_getencprop(phandle, "#address-cells", &address_cells, 
	    sizeof(address_cells)) < (ssize_t)sizeof(address_cells))
		address_cells = 1;
	if (OF_getencprop(phandle, "#size-cells", &size_cells, 
	    sizeof(size_cells)) < (ssize_t)sizeof(size_cells))
		size_cells = 1;

	/*
	 * Get memory.
	 */
	if (node == -1 || (sz = OF_getencprop(node, prop,
	    OFmem, sizeof(OFmem))) <= 0)
		panic("Physical memory map not found");

	i = 0;
	j = 0;
	while (i < sz/sizeof(cell_t)) {
		output[j].mr_start = OFmem[i++];
		if (address_cells == 2) {
			output[j].mr_start <<= 32;
			output[j].mr_start += OFmem[i++];
		}
			
		output[j].mr_size = OFmem[i++];
		if (size_cells == 2) {
			output[j].mr_size <<= 32;
			output[j].mr_size += OFmem[i++];
		}

		if (output[j].mr_start > BUS_SPACE_MAXADDR)
			continue;

		/*
		 * Constrain memory to that which we can access.
		 * 32-bit AIM can only reference 32 bits of address currently,
		 * but Book-E can access 36 bits.
		 */
		if (((uint64_t)output[j].mr_start +
		    (uint64_t)output[j].mr_size - 1) >
		    BUS_SPACE_MAXADDR) {
			output[j].mr_size = BUS_SPACE_MAXADDR -
			    output[j].mr_start + 1;
		}

		j++;
	}
	sz = j*sizeof(output[0]);

	return (sz);
}

#ifdef FDT
static int
excise_reserved_regions(struct mem_region *avail, int asz,
			struct mem_region *exclude, int esz)
{
	int i, j, k;

	for (i = 0; i < asz; i++) {
		for (j = 0; j < esz; j++) {
			/*
			 * Case 1: Exclusion region encloses complete
			 * available entry. Drop it and move on.
			 */
			if (exclude[j].mr_start <= avail[i].mr_start &&
			    exclude[j].mr_start + exclude[j].mr_size >=
			    avail[i].mr_start + avail[i].mr_size) {
				for (k = i+1; k < asz; k++)
					avail[k-1] = avail[k];
				asz--;
				i--; /* Repeat some entries */
				continue;
			}

			/*
			 * Case 2: Exclusion region starts in available entry.
			 * Trim it to where the entry begins and append
			 * a new available entry with the region after
			 * the excluded region, if any.
			 */
			if (exclude[j].mr_start >= avail[i].mr_start &&
			    exclude[j].mr_start < avail[i].mr_start +
			    avail[i].mr_size) {
				if (exclude[j].mr_start + exclude[j].mr_size <
				    avail[i].mr_start + avail[i].mr_size) {
					avail[asz].mr_start =
					    exclude[j].mr_start + exclude[j].mr_size;
					avail[asz].mr_size = avail[i].mr_start +
					     avail[i].mr_size -
					     avail[asz].mr_start;
					asz++;
				}

				avail[i].mr_size = exclude[j].mr_start -
				    avail[i].mr_start;
			}

			/*
			 * Case 3: Exclusion region ends in available entry.
			 * Move start point to where the exclusion zone ends.
			 * The case of a contained exclusion zone has already
			 * been caught in case 2.
			 */
			if (exclude[j].mr_start + exclude[j].mr_size >=
			    avail[i].mr_start && exclude[j].mr_start +
			    exclude[j].mr_size < avail[i].mr_start +
			    avail[i].mr_size) {
				avail[i].mr_size += avail[i].mr_start;
				avail[i].mr_start =
				    exclude[j].mr_start + exclude[j].mr_size;
				avail[i].mr_size -= avail[i].mr_start;
			}
		}
	}

	return (asz);
}

static int
excise_initrd_region(struct mem_region *avail, int asz)
{
	phandle_t chosen;
	uint64_t start, end;
	ssize_t size;
	struct mem_region initrdmap[1];
	pcell_t cell[2];

	chosen = OF_finddevice("/chosen");

	size = OF_getencprop(chosen, "linux,initrd-start", cell, sizeof(cell));
	if (size < 0)
		return (asz);
	else if (size == 4)
		start = cell[0];
	else if (size == 8)
		start = (uint64_t)cell[0] << 32 | cell[1];
	else {
		/* Invalid value length */
		printf("WARNING: linux,initrd-start must be either 4 or 8 bytes long\n");
		return (asz);
	}

	size = OF_getencprop(chosen, "linux,initrd-end", cell, sizeof(cell));
	if (size < 0)
		return (asz);
	else if (size == 4)
		end = cell[0];
	else if (size == 8)
		end = (uint64_t)cell[0] << 32 | cell[1];
	else {
		/* Invalid value length */
		printf("WARNING: linux,initrd-end must be either 4 or 8 bytes long\n");
		return (asz);
	}

	if (end <= start)
		return (asz);

	initrdmap[0].mr_start = start;
	initrdmap[0].mr_size = end - start;

	asz = excise_reserved_regions(avail, asz, initrdmap, 1);

	return (asz);
}

#ifdef POWERNV
static int
excise_msi_region(struct mem_region *avail, int asz)
{
        uint64_t start, end;
        struct mem_region initrdmap[1];

	/*
	 * This range of physical addresses is used to implement optimized
	 * 32 bit MSI interrupts on POWER9. Exclude it to avoid accidentally
	 * using it for DMA, as this will cause an immediate PHB fence.
	 * While we could theoretically turn off this behavior in the ETU,
	 * doing so would break 32-bit MSI, so just reserve the range in 
	 * the physical map instead.
	 * See section 4.4.2.8 of the PHB4 specification.
	 */
	start	= 0x00000000ffff0000ul;
	end	= 0x00000000fffffffful;

	initrdmap[0].mr_start = start;
	initrdmap[0].mr_size = end - start;

	asz = excise_reserved_regions(avail, asz, initrdmap, 1);

	return (asz);
}
#endif

static int
excise_fdt_reserved(struct mem_region *avail, int asz)
{
	struct mem_region fdtmap[32];
	ssize_t fdtmapsize;
	phandle_t chosen;
	int j, fdtentries;

	chosen = OF_finddevice("/chosen");
	fdtmapsize = OF_getprop(chosen, "fdtmemreserv", fdtmap, sizeof(fdtmap));

	for (j = 0; j < fdtmapsize/sizeof(fdtmap[0]); j++) {
		fdtmap[j].mr_start = be64toh(fdtmap[j].mr_start) & ~PAGE_MASK;
		fdtmap[j].mr_size = round_page(be64toh(fdtmap[j].mr_size));
	}

	KASSERT(j*sizeof(fdtmap[0]) < sizeof(fdtmap),
	    ("Exceeded number of FDT reservations"));
	/* Add a virtual entry for the FDT itself */
	if (fdt != NULL) {
		fdtmap[j].mr_start = (vm_offset_t)fdt & ~PAGE_MASK;
		fdtmap[j].mr_size = round_page(fdt_totalsize(fdt));
		fdtmapsize += sizeof(fdtmap[0]);
	}

	fdtentries = fdtmapsize/sizeof(fdtmap[0]);
	asz = excise_reserved_regions(avail, asz, fdtmap, fdtentries);

	return (asz);
}
#endif

/*
 * This is called during powerpc_init, before the system is really initialized.
 * It shall provide the total and the available regions of RAM.
 * The available regions need not take the kernel into account.
 */
void
ofw_mem_regions(struct mem_region *memp, int *memsz,
		struct mem_region *availp, int *availsz)
{
	phandle_t phandle;
	int asz, msz;
	int res;
	char name[31];

	asz = msz = 0;

	/*
	 * Get memory from all the /memory nodes.
	 */
	for (phandle = OF_child(OF_peer(0)); phandle != 0;
	    phandle = OF_peer(phandle)) {
		if (OF_getprop(phandle, "name", name, sizeof(name)) <= 0)
			continue;
		if (strncmp(name, "memory", sizeof(name)) != 0 &&
		    strncmp(name, "memory@", strlen("memory@")) != 0)
			continue;

		res = parse_ofw_memory(phandle, "reg", &memp[msz]);
		msz += res/sizeof(struct mem_region);

		/*
		 * On POWER9 Systems we might have both linux,usable-memory and
		 * reg properties.  'reg' denotes all available memory, but we
		 * must use 'linux,usable-memory', a subset, as some memory
		 * regions are reserved for NVLink.
		 */
		if (OF_getproplen(phandle, "linux,usable-memory") >= 0)
			res = parse_ofw_memory(phandle, "linux,usable-memory",
			    &availp[asz]);
		else if (OF_getproplen(phandle, "available") >= 0)
			res = parse_ofw_memory(phandle, "available",
			    &availp[asz]);
		else
			res = parse_ofw_memory(phandle, "reg", &availp[asz]);
		asz += res/sizeof(struct mem_region);
	}

#ifdef FDT
	phandle = OF_finddevice("/chosen");
	if (OF_hasprop(phandle, "fdtmemreserv"))
		asz = excise_fdt_reserved(availp, asz);

	/* If the kernel is being loaded through kexec, initrd region is listed
	 * in /chosen but the region is not marked as reserved, so, we might exclude
	 * it here.
	 */
	if (OF_hasprop(phandle, "linux,initrd-start"))
		asz = excise_initrd_region(availp, asz);
#endif

#ifdef POWERNV
	if (opal_check() == 0)
		asz = excise_msi_region(availp, asz);
#endif

	*memsz = msz;
	*availsz = asz;
}

void
OF_initial_setup(void *fdt_ptr, void *junk, int (*openfirm)(void *))
{
#ifdef AIM
	ofmsr[0] = mfmsr();
	#ifdef __powerpc64__
	ofmsr[0] &= ~PSL_SF;
	#else
	__asm __volatile("mfsprg0 %0" : "=&r"(ofmsr[1]));
	#endif
	__asm __volatile("mfsprg1 %0" : "=&r"(ofmsr[2]));
	__asm __volatile("mfsprg2 %0" : "=&r"(ofmsr[3]));
	__asm __volatile("mfsprg3 %0" : "=&r"(ofmsr[4]));
	openfirmware_entry = openfirm;

	if (ofmsr[0] & PSL_DR)
		ofw_real_mode = 0;
	else
		ofw_real_mode = 1;

	ofw_save_trap_vec(save_trap_init);
#else
	ofw_real_mode = 1;
#endif

	fdt = fdt_ptr;
}

boolean_t
OF_bootstrap()
{
	boolean_t status = FALSE;
	int err = 0;

#ifdef AIM
	if (openfirmware_entry != NULL) {
		if (ofw_real_mode) {
			status = OF_install(OFW_STD_REAL, 0);
		} else {
			#ifdef __powerpc64__
			status = OF_install(OFW_STD_32BIT, 0);
			#else
			status = OF_install(OFW_STD_DIRECT, 0);
			#endif
		}

		if (status != TRUE)
			return status;

		err = OF_init(openfirmware);
	} else
#endif
	if (fdt != NULL) {
#ifdef FDT
#ifdef AIM
		bus_space_tag_t fdt_bt;
		vm_offset_t tmp_fdt_ptr;
		vm_size_t fdt_size;
		uintptr_t fdt_va;
#endif

		status = OF_install(OFW_FDT, 0);
		if (status != TRUE)
			return status;

#ifdef AIM /* AIM-only for now -- Book-E does this remapping in early init */
		/* Get the FDT size for mapping if we can */
		tmp_fdt_ptr = pmap_early_io_map((vm_paddr_t)fdt, PAGE_SIZE);
		if (fdt_check_header((void *)tmp_fdt_ptr) != 0) {
			pmap_early_io_unmap(tmp_fdt_ptr, PAGE_SIZE);
			return FALSE;
		}
		fdt_size = fdt_totalsize((void *)tmp_fdt_ptr);
		pmap_early_io_unmap(tmp_fdt_ptr, PAGE_SIZE);

		/*
		 * Map this for real. Use bus_space_map() to take advantage
		 * of its auto-remapping function once the kernel is loaded.
		 * This is a dirty hack, but what we have.
		 */
#ifdef _LITTLE_ENDIAN
		fdt_bt = &bs_le_tag;
#else
		fdt_bt = &bs_be_tag;
#endif
		bus_space_map(fdt_bt, (vm_paddr_t)fdt, fdt_size, 0, &fdt_va);
		 
		err = OF_init((void *)fdt_va);
#else
		err = OF_init(fdt);
#endif
#endif
	} 

	#ifdef FDT_DTB_STATIC
	/*
	 * Check for a statically included blob already in the kernel and
	 * needing no mapping.
	 */
	else {
		status = OF_install(OFW_FDT, 0);
		if (status != TRUE)
			return status;
		err = OF_init(&fdt_static_dtb);
	}
	#endif

	if (err != 0) {
		OF_install(NULL, 0);
		status = FALSE;
	}

	return (status);
}

#ifdef AIM
void
ofw_quiesce(void)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
	} args;

	KASSERT(!pmap_bootstrapped, ("Cannot call ofw_quiesce after VM is up"));

	args.name = (cell_t)(uintptr_t)"quiesce";
	args.nargs = 0;
	args.nreturns = 0;
	openfirmware(&args);
}

static int
openfirmware_core(void *args)
{
	int		result;
	register_t	oldmsr;

	if (openfirmware_entry == NULL)
		return (-1);

	/*
	 * Turn off exceptions - we really don't want to end up
	 * anywhere unexpected with PCPU set to something strange
	 * or the stack pointer wrong.
	 */
	oldmsr = intr_disable();

	ofw_sprg_prepare();

	/* Save trap vectors */
	ofw_save_trap_vec(save_trap_of);

	/* Restore initially saved trap vectors */
	ofw_restore_trap_vec(save_trap_init);

#ifndef __powerpc64__
	/*
	 * Clear battable[] translations
	 */
	if (!(cpu_features & PPC_FEATURE_64))
		__asm __volatile("mtdbatu 2, %0\n"
				 "mtdbatu 3, %0" : : "r" (0));
	isync();
#endif

	result = ofwcall(args);

	/* Restore trap vecotrs */
	ofw_restore_trap_vec(save_trap_of);

	ofw_sprg_restore();

	intr_restore(oldmsr);

	return (result);
}

#ifdef SMP
struct ofw_rv_args {
	void *args;
	int retval;
	volatile int in_progress;
};

static void
ofw_rendezvous_dispatch(void *xargs)
{
	struct ofw_rv_args *rv_args = xargs;

	/* NOTE: Interrupts are disabled here */

	if (PCPU_GET(cpuid) == 0) {
		/*
		 * Execute all OF calls on CPU 0
		 */
		rv_args->retval = openfirmware_core(rv_args->args);
		rv_args->in_progress = 0;
	} else {
		/*
		 * Spin with interrupts off on other CPUs while OF has
		 * control of the machine.
		 */
		while (rv_args->in_progress)
			cpu_spinwait();
	}
}
#endif

static int
openfirmware(void *args)
{
	int result;
	#ifdef SMP
	struct ofw_rv_args rv_args;
	#endif

	if (openfirmware_entry == NULL)
		return (-1);

	#ifdef SMP
	if (cold) {
		result = openfirmware_core(args);
	} else {
		rv_args.args = args;
		rv_args.in_progress = 1;
		smp_rendezvous(smp_no_rendezvous_barrier,
		    ofw_rendezvous_dispatch, smp_no_rendezvous_barrier,
		    &rv_args);
		result = rv_args.retval;
	}
	#else
	result = openfirmware_core(args);
	#endif

	return (result);
}

void
OF_reboot()
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t arg;
	} args;

	args.name = (cell_t)(uintptr_t)"interpret";
	args.nargs = 1;
	args.nreturns = 0;
	args.arg = (cell_t)(uintptr_t)"reset-all";
	openfirmware_core(&args); /* Don't do rendezvous! */

	for (;;);	/* just in case */
}

#endif /* AIM */

void
OF_getetheraddr(device_t dev, u_char *addr)
{
	phandle_t	node;

	node = ofw_bus_get_node(dev);
	OF_getprop(node, "local-mac-address", addr, ETHER_ADDR_LEN);
}

/*
 * Return a bus handle and bus tag that corresponds to the register
 * numbered regno for the device referenced by the package handle
 * dev. This function is intended to be used by console drivers in
 * early boot only. It works by mapping the address of the device's
 * register in the address space of its parent and recursively walk
 * the device tree upward this way.
 */
int
OF_decode_addr(phandle_t dev, int regno, bus_space_tag_t *tag,
    bus_space_handle_t *handle, bus_size_t *sz)
{
	bus_addr_t addr;
	bus_size_t size;
	pcell_t pci_hi;
	int flags, res;

	res = ofw_reg_to_paddr(dev, regno, &addr, &size, &pci_hi);
	if (res < 0)
		return (res);

	if (pci_hi == OFW_PADDR_NOT_PCI) {
		*tag = &bs_be_tag;
		flags = 0;
	} else {
		*tag = &bs_le_tag;
		flags = (pci_hi & OFW_PCI_PHYS_HI_PREFETCHABLE) ? 
		    BUS_SPACE_MAP_PREFETCHABLE: 0;
	}

	if (sz != NULL)
		*sz = size;

	return (bus_space_map(*tag, addr, size, flags, handle));
}

