/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2004 Christian Limpach.
 * Copyright (c) 2004-2006,2008 Kip Macy
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
 * Copyright (c) 2013 Roger Pau Monné <roger.pau@citrix.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS AS IS'' AND
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_kstack_pages.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/reboot.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/linker.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/boot.h>
#include <sys/ctype.h>
#include <sys/mutex.h>
#include <sys/smp.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <vm/vm_param.h>

#include <machine/_inttypes.h>
#include <machine/intr_machdep.h>
#include <x86/apicvar.h>
#include <x86/init.h>
#include <machine/pc/bios.h>
#include <machine/smp.h>
#include <machine/intr_machdep.h>
#include <machine/metadata.h>

#include <xen/xen-os.h>
#include <xen/hvm.h>
#include <xen/hypervisor.h>
#include <xen/xenstore/xenstorevar.h>
#include <xen/xen_pv.h>
#include <xen/xen_msi.h>

#include <xen/interface/arch-x86/hvm/start_info.h>
#include <xen/interface/vcpu.h>

#include <dev/xen/timer/timer.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

/* Native initial function */
extern u_int64_t hammer_time(u_int64_t, u_int64_t);
/* Xen initial function */
uint64_t hammer_time_xen_legacy(start_info_t *, uint64_t);
uint64_t hammer_time_xen(vm_paddr_t);

#define MAX_E820_ENTRIES	128

/*--------------------------- Forward Declarations ---------------------------*/
static caddr_t xen_legacy_pvh_parse_preload_data(uint64_t);
static caddr_t xen_pvh_parse_preload_data(uint64_t);
static void xen_pvh_parse_memmap(caddr_t, vm_paddr_t *, int *);

#ifdef SMP
static int xen_pv_start_all_aps(void);
#endif

/*---------------------------- Extern Declarations ---------------------------*/
#ifdef SMP
/* Variables used by amd64 mp_machdep to start APs */
extern char *doublefault_stack;
extern char *mce_stack;
extern char *nmi_stack;
extern char *dbg_stack;
#endif

/*
 * Placed by the linker at the end of the bss section, which is the last
 * section loaded by Xen before loading the symtab and strtab.
 */
extern uint32_t end;

/*-------------------------------- Global Data -------------------------------*/
/* Xen init_ops implementation. */
struct init_ops xen_legacy_init_ops = {
	.parse_preload_data		= xen_legacy_pvh_parse_preload_data,
	.early_clock_source_init	= xen_clock_init,
	.early_delay			= xen_delay,
	.parse_memmap			= xen_pvh_parse_memmap,
#ifdef SMP
	.start_all_aps			= xen_pv_start_all_aps,
#endif
	.msi_init			= xen_msi_init,
};

struct init_ops xen_pvh_init_ops = {
	.parse_preload_data		= xen_pvh_parse_preload_data,
	.early_clock_source_init	= xen_clock_init,
	.early_delay			= xen_delay,
	.parse_memmap			= xen_pvh_parse_memmap,
#ifdef SMP
	.mp_bootaddress			= mp_bootaddress,
	.start_all_aps			= native_start_all_aps,
#endif
	.msi_init			= msi_init,
};

static struct bios_smap xen_smap[MAX_E820_ENTRIES];

static start_info_t *legacy_start_info;
static struct hvm_start_info *start_info;

/*----------------------- Legacy PVH start_info accessors --------------------*/
static vm_paddr_t
legacy_get_xenstore_mfn(void)
{

	return (legacy_start_info->store_mfn);
}

static evtchn_port_t
legacy_get_xenstore_evtchn(void)
{

	return (legacy_start_info->store_evtchn);
}

static vm_paddr_t
legacy_get_console_mfn(void)
{

	return (legacy_start_info->console.domU.mfn);
}

static evtchn_port_t
legacy_get_console_evtchn(void)
{

	return (legacy_start_info->console.domU.evtchn);
}

static uint32_t
legacy_get_start_flags(void)
{

	return (legacy_start_info->flags);
}

struct hypervisor_info legacy_info = {
	.get_xenstore_mfn		= legacy_get_xenstore_mfn,
	.get_xenstore_evtchn		= legacy_get_xenstore_evtchn,
	.get_console_mfn		= legacy_get_console_mfn,
	.get_console_evtchn		= legacy_get_console_evtchn,
	.get_start_flags		= legacy_get_start_flags,
};

/*-------------------------------- Xen PV init -------------------------------*/
/*
 * First function called by the Xen legacy PVH boot sequence.
 *
 * Set some Xen global variables and prepare the environment so it is
 * as similar as possible to what native FreeBSD init function expects.
 */
uint64_t
hammer_time_xen_legacy(start_info_t *si, uint64_t xenstack)
{
	uint64_t physfree;
	uint64_t *PT4 = (u_int64_t *)xenstack;
	uint64_t *PT3 = (u_int64_t *)(xenstack + PAGE_SIZE);
	uint64_t *PT2 = (u_int64_t *)(xenstack + 2 * PAGE_SIZE);
	int i;
	char *kenv;

	xen_domain_type = XEN_PV_DOMAIN;
	vm_guest = VM_GUEST_XEN;

	if ((si == NULL) || (xenstack == 0)) {
		xc_printf("ERROR: invalid start_info or xen stack, halting\n");
		HYPERVISOR_shutdown(SHUTDOWN_crash);
	}

	xc_printf("FreeBSD PVH running on %s\n", si->magic);

	/* We use 3 pages of xen stack for the boot pagetables */
	physfree = xenstack + 3 * PAGE_SIZE - KERNBASE;

	/* Setup Xen global variables */
	legacy_start_info = si;
	HYPERVISOR_shared_info =
	    (shared_info_t *)(si->shared_info + KERNBASE);

	/*
	 * Use the stack Xen gives us to build the page tables
	 * as native FreeBSD expects to find them (created
	 * by the boot trampoline).
	 */
	for (i = 0; i < (PAGE_SIZE / sizeof(uint64_t)); i++) {
		/*
		 * Each slot of the level 4 pages points
		 * to the same level 3 page
		 */
		PT4[i] = ((uint64_t)&PT3[0]) - KERNBASE;
		PT4[i] |= PG_V | PG_RW | PG_U;

		/*
		 * Each slot of the level 3 pages points
		 * to the same level 2 page
		 */
		PT3[i] = ((uint64_t)&PT2[0]) - KERNBASE;
		PT3[i] |= PG_V | PG_RW | PG_U;

		/*
		 * The level 2 page slots are mapped with
		 * 2MB pages for 1GB.
		 */
		PT2[i] = i * (2 * 1024 * 1024);
		PT2[i] |= PG_V | PG_RW | PG_PS | PG_U;
	}
	load_cr3(((uint64_t)&PT4[0]) - KERNBASE);

	/*
	 * Init an empty static kenv using a free page. The contents will be
	 * filled from the parse_preload_data hook.
	 */
	kenv = (void *)(physfree + KERNBASE);
	physfree += PAGE_SIZE;
	bzero_early(kenv, PAGE_SIZE);
	init_static_kenv(kenv, PAGE_SIZE);

	/* Set the hooks for early functions that diverge from bare metal */
	init_ops = xen_legacy_init_ops;
	apic_ops = xen_apic_ops;
	hypervisor_info = legacy_info;

	/* Now we can jump into the native init function */
	return (hammer_time(0, physfree));
}

uint64_t
hammer_time_xen(vm_paddr_t start_info_paddr)
{
	struct hvm_modlist_entry *mod;
	struct xen_add_to_physmap xatp;
	uint64_t physfree;
	char *kenv;
	int rc;

	xen_domain_type = XEN_HVM_DOMAIN;
	vm_guest = VM_GUEST_XEN;

	rc = xen_hvm_init_hypercall_stubs(XEN_HVM_INIT_EARLY);
	if (rc) {
		xc_printf("ERROR: failed to initialize hypercall page: %d\n",
		    rc);
		HYPERVISOR_shutdown(SHUTDOWN_crash);
	}

	start_info = (struct hvm_start_info *)(start_info_paddr + KERNBASE);
	if (start_info->magic != XEN_HVM_START_MAGIC_VALUE) {
		xc_printf("Unknown magic value in start_info struct: %#x\n",
		    start_info->magic);
		HYPERVISOR_shutdown(SHUTDOWN_crash);
	}

	/*
	 * The hvm_start_into structure is always appended after loading
	 * the kernel and modules.
	 */
	physfree = roundup2(start_info_paddr + PAGE_SIZE, PAGE_SIZE);

	xatp.domid = DOMID_SELF;
	xatp.idx = 0;
	xatp.space = XENMAPSPACE_shared_info;
	xatp.gpfn = atop(physfree);
	if (HYPERVISOR_memory_op(XENMEM_add_to_physmap, &xatp)) {
		xc_printf("ERROR: failed to setup shared_info page\n");
		HYPERVISOR_shutdown(SHUTDOWN_crash);
	}
	HYPERVISOR_shared_info = (shared_info_t *)(physfree + KERNBASE);
	physfree += PAGE_SIZE;

	/*
	 * Init a static kenv using a free page. The contents will be filled
	 * from the parse_preload_data hook.
	 */
	kenv = (void *)(physfree + KERNBASE);
	physfree += PAGE_SIZE;
	bzero_early(kenv, PAGE_SIZE);
	init_static_kenv(kenv, PAGE_SIZE);

	if (start_info->modlist_paddr != 0) {
		if (start_info->modlist_paddr >= physfree) {
			xc_printf(
			    "ERROR: unexpected module list memory address\n");
			HYPERVISOR_shutdown(SHUTDOWN_crash);
		}
		if (start_info->nr_modules == 0) {
			xc_printf(
			    "ERROR: modlist_paddr != 0 but nr_modules == 0\n");
			HYPERVISOR_shutdown(SHUTDOWN_crash);
		}
		mod = (struct hvm_modlist_entry *)
		    (vm_paddr_t)start_info->modlist_paddr + KERNBASE;
		if (mod[0].paddr >= physfree) {
			xc_printf("ERROR: unexpected module memory address\n");
			HYPERVISOR_shutdown(SHUTDOWN_crash);
		}
	}

	/* Set the hooks for early functions that diverge from bare metal */
	init_ops = xen_pvh_init_ops;
	hvm_start_flags = start_info->flags;

	/* Now we can jump into the native init function */
	return (hammer_time(0, physfree));
}

/*-------------------------------- PV specific -------------------------------*/
#ifdef SMP
static bool
start_xen_ap(int cpu)
{
	struct vcpu_guest_context *ctxt;
	int ms, cpus = mp_naps;
	const size_t stacksize = kstack_pages * PAGE_SIZE;

	/* allocate and set up an idle stack data page */
	bootstacks[cpu] = (void *)kmem_malloc(stacksize, M_WAITOK | M_ZERO);
	doublefault_stack = (char *)kmem_malloc(PAGE_SIZE, M_WAITOK | M_ZERO);
	mce_stack = (char *)kmem_malloc(PAGE_SIZE, M_WAITOK | M_ZERO);
	nmi_stack = (char *)kmem_malloc(PAGE_SIZE, M_WAITOK | M_ZERO);
	dbg_stack = (void *)kmem_malloc(PAGE_SIZE, M_WAITOK | M_ZERO);
	dpcpu = (void *)kmem_malloc(DPCPU_SIZE, M_WAITOK | M_ZERO);

	bootSTK = (char *)bootstacks[cpu] + kstack_pages * PAGE_SIZE - 8;
	bootAP = cpu;

	ctxt = malloc(sizeof(*ctxt), M_TEMP, M_WAITOK | M_ZERO);

	ctxt->flags = VGCF_IN_KERNEL;
	ctxt->user_regs.rip = (unsigned long) init_secondary;
	ctxt->user_regs.rsp = (unsigned long) bootSTK;

	/* Set the AP to use the same page tables */
	ctxt->ctrlreg[3] = KPML4phys;

	if (HYPERVISOR_vcpu_op(VCPUOP_initialise, cpu, ctxt))
		panic("unable to initialize AP#%d", cpu);

	free(ctxt, M_TEMP);

	/* Launch the vCPU */
	if (HYPERVISOR_vcpu_op(VCPUOP_up, cpu, NULL))
		panic("unable to start AP#%d", cpu);

	/* Wait up to 5 seconds for it to start. */
	for (ms = 0; ms < 5000; ms++) {
		if (mp_naps > cpus)
			return (true);
		DELAY(1000);
	}

	return (false);
}

static int
xen_pv_start_all_aps(void)
{
	int cpu;

	mtx_init(&ap_boot_mtx, "ap boot", NULL, MTX_SPIN);

	for (cpu = 1; cpu < mp_ncpus; cpu++) {

		/* attempt to start the Application Processor */
		if (!start_xen_ap(cpu))
			panic("AP #%d failed to start!", cpu);

		CPU_SET(cpu, &all_cpus);	/* record AP in CPU map */
	}

	return (mp_naps);
}
#endif /* SMP */

/*
 * When booted as a PVH guest FreeBSD needs to avoid using the RSDP address
 * hint provided by the loader because it points to the native set of ACPI
 * tables instead of the ones crafted by Xen. The acpi.rsdp env variable is
 * removed from kenv if present, and a new acpi.rsdp is added to kenv that
 * points to the address of the Xen crafted RSDP.
 */
static bool reject_option(const char *option)
{
	static const char *reject[] = {
		"acpi.rsdp",
	};
	unsigned int i;

	for (i = 0; i < nitems(reject); i++)
		if (strncmp(option, reject[i], strlen(reject[i])) == 0)
			return (true);

	return (false);
}

static void
xen_pvh_set_env(char *env, bool (*filter)(const char *))
{
	char *option;

	if (env == NULL)
		return;

	option = env;
	while (*option != 0) {
		char *value;

		if (filter != NULL && filter(option)) {
			option += strlen(option) + 1;
			continue;
		}

		value = option;
		option = strsep(&value, "=");
		if (kern_setenv(option, value) != 0)
			xc_printf("unable to add kenv %s=%s\n", option, value);
		option = value + strlen(value) + 1;
	}
}

#ifdef DDB
/*
 * The way Xen loads the symtab is different from the native boot loader,
 * because it's tailored for NetBSD. So we have to adapt and use the same
 * method as NetBSD. Portions of the code below have been picked from NetBSD:
 * sys/kern/kern_ksyms.c CVS Revision 1.71.
 */
static void
xen_pvh_parse_symtab(void)
{
	Elf_Ehdr *ehdr;
	Elf_Shdr *shdr;
	uint32_t size;
	int i, j;

	size = end;

	ehdr = (Elf_Ehdr *)(&end + 1);
	if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) ||
	    ehdr->e_ident[EI_CLASS] != ELF_TARG_CLASS ||
	    ehdr->e_version > 1) {
		xc_printf("Unable to load ELF symtab: invalid symbol table\n");
		return;
	}

	shdr = (Elf_Shdr *)((uint8_t *)ehdr + ehdr->e_shoff);
	/* Find the symbol table and the corresponding string table. */
	for (i = 1; i < ehdr->e_shnum; i++) {
		if (shdr[i].sh_type != SHT_SYMTAB)
			continue;
		if (shdr[i].sh_offset == 0)
			continue;
		ksymtab = (uintptr_t)((uint8_t *)ehdr + shdr[i].sh_offset);
		ksymtab_size = shdr[i].sh_size;
		j = shdr[i].sh_link;
		if (shdr[j].sh_offset == 0)
			continue; /* Can this happen? */
		kstrtab = (uintptr_t)((uint8_t *)ehdr + shdr[j].sh_offset);
		break;
	}

	if (ksymtab == 0 || kstrtab == 0)
		xc_printf(
    "Unable to load ELF symtab: could not find symtab or strtab\n");
}
#endif

static caddr_t
xen_legacy_pvh_parse_preload_data(uint64_t modulep)
{
	caddr_t		 kmdp;
	vm_ooffset_t	 off;
	vm_paddr_t	 metadata;
	char             *envp;

	if (legacy_start_info->mod_start != 0) {
		preload_metadata = (caddr_t)legacy_start_info->mod_start;

		kmdp = preload_search_by_type("elf kernel");
		if (kmdp == NULL)
			kmdp = preload_search_by_type("elf64 kernel");
		KASSERT(kmdp != NULL, ("unable to find kernel"));

		/*
		 * Xen has relocated the metadata and the modules,
		 * so we need to recalculate it's position. This is
		 * done by saving the original modulep address and
		 * then calculating the offset with mod_start,
		 * which contains the relocated modulep address.
		 */
		metadata = MD_FETCH(kmdp, MODINFOMD_MODULEP, vm_paddr_t);
		off = legacy_start_info->mod_start - metadata;

		preload_bootstrap_relocate(off);

		boothowto = MD_FETCH(kmdp, MODINFOMD_HOWTO, int);
		envp = MD_FETCH(kmdp, MODINFOMD_ENVP, char *);
		if (envp != NULL)
			envp += off;
		xen_pvh_set_env(envp, NULL);
	} else {
		/* Parse the extra boot information given by Xen */
		boot_parse_cmdline_delim(legacy_start_info->cmd_line, ",");
		kmdp = NULL;
	}

	boothowto |= boot_env_to_howto();

#ifdef DDB
	xen_pvh_parse_symtab();
#endif
	return (kmdp);
}

static caddr_t
xen_pvh_parse_preload_data(uint64_t modulep)
{
	caddr_t kmdp;
	vm_ooffset_t off;
	vm_paddr_t metadata;
	char *envp;
	char acpi_rsdp[19];

	if (start_info->modlist_paddr != 0) {
		struct hvm_modlist_entry *mod;

		mod = (struct hvm_modlist_entry *)
		    (start_info->modlist_paddr + KERNBASE);
		preload_metadata = (caddr_t)(mod[0].paddr + KERNBASE);

		kmdp = preload_search_by_type("elf kernel");
		if (kmdp == NULL)
			kmdp = preload_search_by_type("elf64 kernel");
		KASSERT(kmdp != NULL, ("unable to find kernel"));

		/*
		 * Xen has relocated the metadata and the modules,
		 * so we need to recalculate it's position. This is
		 * done by saving the original modulep address and
		 * then calculating the offset with mod_start,
		 * which contains the relocated modulep address.
		 */
		metadata = MD_FETCH(kmdp, MODINFOMD_MODULEP, vm_paddr_t);
		off = mod[0].paddr + KERNBASE - metadata;

		preload_bootstrap_relocate(off);

		boothowto = MD_FETCH(kmdp, MODINFOMD_HOWTO, int);
		envp = MD_FETCH(kmdp, MODINFOMD_ENVP, char *);
		if (envp != NULL)
			envp += off;
		xen_pvh_set_env(envp, reject_option);
	} else {
		/* Parse the extra boot information given by Xen */
		if (start_info->cmdline_paddr != 0)
			boot_parse_cmdline_delim(
			    (char *)(start_info->cmdline_paddr + KERNBASE),
			    ",");
		kmdp = NULL;
	}

	boothowto |= boot_env_to_howto();

	snprintf(acpi_rsdp, sizeof(acpi_rsdp), "%#" PRIx64,
	    start_info->rsdp_paddr);
	kern_setenv("acpi.rsdp", acpi_rsdp);

#ifdef DDB
	xen_pvh_parse_symtab();
#endif
	return (kmdp);
}

static void
xen_pvh_parse_memmap(caddr_t kmdp, vm_paddr_t *physmap, int *physmap_idx)
{
	struct xen_memory_map memmap;
	u_int32_t size;
	int rc;

	/* Fetch the E820 map from Xen */
	memmap.nr_entries = MAX_E820_ENTRIES;
	set_xen_guest_handle(memmap.buffer, xen_smap);
	rc = HYPERVISOR_memory_op(XENMEM_memory_map, &memmap);
	if (rc) {
		xc_printf("ERROR: unable to fetch Xen E820 memory map: %d\n",
		    rc);
		HYPERVISOR_shutdown(SHUTDOWN_crash);
	}

	size = memmap.nr_entries * sizeof(xen_smap[0]);

	bios_add_smap_entries(xen_smap, size, physmap, physmap_idx);
}
