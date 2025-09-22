/*	$OpenBSD: acpi_machdep.c,v 1.88 2024/09/01 03:08:56 jsg Exp $	*/
/*
 * Copyright (c) 2005 Thorsten Lockert <tholo@sigmasoft.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/memrange.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/reboot.h>
#include <sys/hibernate.h>

#include <uvm/uvm_extern.h>

#include <machine/biosvar.h>
#include <i386/isa/isa_machdep.h>

#include <machine/conf.h>
#include <machine/acpiapm.h>

#include <machine/cpuvar.h>
#include <machine/npx.h>

#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/isa/isareg.h>

#include "apm.h"
#include "isa.h"
#include "ioapic.h"
#include "lapic.h"

#if NIOAPIC > 0
#include <machine/i82093var.h>
#endif

#if NLAPIC > 0
#include <machine/i82489reg.h>
#include <machine/i82489var.h>
#endif

#if NAPM > 0
int haveacpibutusingapm;
#endif

extern u_char acpi_real_mode_resume[], acpi_resume_end[];
extern u_char acpi_tramp_data_start[], acpi_tramp_data_end[];

extern int acpi_savecpu(void) __returns_twice;
extern void intr_calculatemasks(void);

#define ACPI_BIOS_RSDP_WINDOW_BASE        0xe0000
#define ACPI_BIOS_RSDP_WINDOW_SIZE        0x20000

u_int8_t	*acpi_scan(struct acpi_mem_map *, paddr_t, size_t);

int	acpi_match(struct device *, void *, void *);
void	acpi_attach(struct device *, struct device *, void *);

const struct cfattach acpi_ca = {
	sizeof(struct acpi_softc), acpi_match, acpi_attach
};

int
acpi_match(struct device *parent, void *match, void *aux)
{
	struct bios_attach_args	*ba = aux;
	struct cfdata		*cf = match;

	/* sanity */
	if (strcmp(ba->ba_name, cf->cf_driver->cd_name))
		return (0);

	if (!acpi_probe(parent, cf, ba))
		return (0);

	return (1);
}

void
acpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpi_softc *sc = (struct acpi_softc *)self;
	struct bios_attach_args *ba = aux;

	sc->sc_iot = ba->ba_iot;
	sc->sc_memt = ba->ba_memt;
	sc->sc_cc_dmat = &pci_bus_dma_tag;
	sc->sc_ci_dmat = &pci_bus_dma_tag;

	acpi_attach_common(sc, ba->ba_acpipbase);
}

int
acpi_map(paddr_t pa, size_t len, struct acpi_mem_map *handle)
{
	paddr_t pgpa = trunc_page(pa);
	paddr_t endpa = round_page(pa + len);
	vaddr_t va = (vaddr_t)km_alloc(endpa - pgpa, &kv_any, &kp_none,
	    &kd_nowait);

	if (va == 0)
		return (ENOMEM);

	handle->baseva = va;
	handle->va = (u_int8_t *)(va + (u_long)(pa & PGOFSET));
	handle->vsize = endpa - pgpa;
	handle->pa = pa;

	do {
		pmap_kenter_pa(va, pgpa, PROT_READ | PROT_WRITE);
		va += NBPG;
		pgpa += NBPG;
	} while (pgpa < endpa);

	return 0;
}

void
acpi_unmap(struct acpi_mem_map *handle)
{
	pmap_kremove(handle->baseva, handle->vsize);
	km_free((void *)handle->baseva, handle->vsize, &kv_any, &kp_none);
}

int
acpi_bus_space_map(bus_space_tag_t t, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	return _bus_space_map(t, addr, size, flags, bshp);
}

void
acpi_bus_space_unmap(bus_space_tag_t t, bus_space_handle_t bsh,
    bus_size_t size)
{
	_bus_space_unmap(t, bsh, size, NULL);
}

u_int8_t *
acpi_scan(struct acpi_mem_map *handle, paddr_t pa, size_t len)
{
	size_t i;
	u_int8_t *ptr;
	struct acpi_rsdp1 *rsdp;

	if (acpi_map(pa, len, handle))
		return (NULL);
	for (ptr = handle->va, i = 0; i < len; ptr += 16, i += 16) {
		/* is there a valid signature? */
		if (memcmp(ptr, RSDP_SIG, sizeof(RSDP_SIG) - 1))
			continue;

		/* is the checksum valid? */
		if (acpi_checksum(ptr, sizeof(struct acpi_rsdp1)) != 0)
			continue;

		/* check the extended checksum as needed */
		rsdp = (struct acpi_rsdp1 *)ptr;
		if (rsdp->revision == 0)
			return (ptr);
		else if (rsdp->revision >= 2 && rsdp->revision <= 4)
			if (acpi_checksum(ptr, sizeof(struct acpi_rsdp)) == 0)
				return (ptr);
	}
	acpi_unmap(handle);

	return (NULL);
}

int
acpi_probe(struct device *parent, struct cfdata *match,
    struct bios_attach_args *ba)
{
	struct acpi_mem_map handle;
	u_int8_t *ptr;
	paddr_t ebda;
#if NAPM > 0
	extern int apm_attached;
#endif

	/*
	 * First try to find ACPI table entries in the EBDA
	 */
	if (acpi_map(0, NBPG, &handle))
		printf("acpi: failed to map BIOS data area\n");
	else {
		ebda = *(const u_int16_t *)(&handle.va[0x40e]);
		ebda <<= 4;
		acpi_unmap(&handle);

		if (ebda && ebda < IOM_BEGIN) {
			if ((ptr = acpi_scan(&handle, ebda, 1024)))
				goto havebase;
		}
	}

	/*
	 * Next try to find the ACPI table entries in the
	 * BIOS memory
	 */
	if ((ptr = acpi_scan(&handle, ACPI_BIOS_RSDP_WINDOW_BASE,
	    ACPI_BIOS_RSDP_WINDOW_SIZE)))
		goto havebase;

	return (0);

havebase:
	ba->ba_acpipbase = ptr - handle.va + handle.pa;
	acpi_unmap(&handle);
#if NAPM > 0
	if (apm_attached) {
		haveacpibutusingapm = 1;
		return (0);
	}
#endif

	return (1);
}

/*
 * Acquire the global lock.  If busy, set the pending bit.  The caller
 * will wait for notification from the BIOS that the lock is available
 * and then attempt to acquire it again.
 */
int
acpi_acquire_glk(uint32_t *lock)
{
	uint32_t	new, old;

	do {
		old = *lock;
		new = (old & ~GL_BIT_PENDING) | GL_BIT_OWNED;
		if ((old & GL_BIT_OWNED) != 0)
			new |= GL_BIT_PENDING;
	} while (atomic_cas_uint(lock, old, new) != old);

	return ((new & GL_BIT_PENDING) == 0);
}

/*
 * Release the global lock, returning whether there is a waiter pending.
 * If the BIOS set the pending bit, OSPM must notify the BIOS when it
 * releases the lock.
 */
int
acpi_release_glk(uint32_t *lock)
{
	uint32_t	new, old;

	do {
		old = *lock;
		new = old & ~(GL_BIT_PENDING | GL_BIT_OWNED);
	} while (atomic_cas_uint(lock, old, new) != old);

	return ((old & GL_BIT_PENDING) != 0);
}

void
acpi_attach_machdep(struct acpi_softc *sc)
{
	extern void (*cpuresetfn)(void);

	sc->sc_interrupt = isa_intr_establish(NULL, sc->sc_fadt->sci_int,
	    IST_LEVEL, IPL_BIO, acpi_interrupt, sc, sc->sc_dev.dv_xname);
	cpuresetfn = acpi_reset;

#ifndef SMALL_KERNEL
	acpiapm_open = acpiopen;
	acpiapm_close = acpiclose;
	acpiapm_ioctl = acpiioctl;
	acpiapm_kqfilter = acpikqfilter;

	/*
	 * Sanity check before setting up trampoline.
	 * Ensure the trampoline page sizes are < PAGE_SIZE
	 */
	KASSERT(acpi_resume_end - acpi_real_mode_resume < PAGE_SIZE);
	KASSERT(acpi_tramp_data_end - acpi_tramp_data_start < PAGE_SIZE);

	/* Map ACPI tramp code and data pages RW for copy */
	pmap_kenter_pa(ACPI_TRAMPOLINE, ACPI_TRAMPOLINE,
	    PROT_READ | PROT_WRITE);
	pmap_kenter_pa(ACPI_TRAMP_DATA, ACPI_TRAMP_DATA,
	    PROT_READ | PROT_WRITE);

	/* Fill the trampoline pages with int3 */
	memset((caddr_t)ACPI_TRAMPOLINE, 0xcc, PAGE_SIZE);
	memset((caddr_t)ACPI_TRAMP_DATA, 0xcc, PAGE_SIZE);

	/* Copy over real trampoline pages (code and data) */
	memcpy((caddr_t)ACPI_TRAMPOLINE, acpi_real_mode_resume,
	    acpi_resume_end - acpi_real_mode_resume);
	memcpy((caddr_t)ACPI_TRAMP_DATA, acpi_tramp_data_start,
	    acpi_tramp_data_end - acpi_tramp_data_start);

	/* Unmap, will be remapped in acpi_sleep_cpu */
	pmap_kremove(ACPI_TRAMPOLINE, PAGE_SIZE);
	pmap_kremove(ACPI_TRAMP_DATA, PAGE_SIZE);

#endif /* SMALL_KERNEL */
}

#ifndef SMALL_KERNEL

#if NLAPIC > 0
int	save_lapic_tpr;
#endif

/*
 * This function may not have local variables due to a bug between
 * acpi_savecpu() and the resume path.
 */
int
acpi_sleep_cpu(struct acpi_softc *sc, int state)
{
	rtcstop();
#if NLAPIC > 0
	save_lapic_tpr = lapic_tpr;
	lapic_disable();
#endif

	/* i386 does lazy pmap_activate: switch to kernel memory view */
	pmap_activate(curproc);

	/*
	 * ACPI defines two wakeup vectors. One is used for ACPI 1.0
	 * implementations - it's in the FACS table as wakeup_vector and
	 * indicates a 32-bit physical address containing real-mode wakeup
	 * code.
	 *
	 * The second wakeup vector is in the FACS table as
	 * x_wakeup_vector and indicates a 64-bit physical address
	 * containing protected-mode wakeup code.
	 */
	sc->sc_facs->wakeup_vector = (u_int32_t)ACPI_TRAMPOLINE;
	if (sc->sc_facs->length > 32 && sc->sc_facs->version >= 1)
		sc->sc_facs->x_wakeup_vector = 0;

	/* Map trampoline and data page */
	pmap_kenter_pa(ACPI_TRAMPOLINE, ACPI_TRAMPOLINE, PROT_READ | PROT_EXEC);
	pmap_kenter_pa(ACPI_TRAMP_DATA, ACPI_TRAMP_DATA,
	    PROT_READ | PROT_WRITE);

	/* Copy the current cpu registers into a safe place for resume.
	 * acpi_savecpu actually returns twice - once in the suspend
	 * path and once in the resume path (see setjmp(3)).
	 */
	if (acpi_savecpu()) {
		/* Suspend path */
		npxsave_cpu(curcpu(), 1);
		wbinvd();

#ifdef HIBERNATE
		if (state == ACPI_STATE_S4 || state == ACPI_STATE_S5) {
			if (hibernate_suspend()) {
				printf("%s: hibernate_suspend failed\n",
				    DEVNAME(sc));
				return (ECANCELED);
			}

			/* XXX
			 * Flag to disk drivers that they should "power down" the disk
			 * when we get to DVACT_POWERDOWN.
			 */
			boothowto |= RB_POWERDOWN;
			config_suspend_all(DVACT_POWERDOWN);
			boothowto &= ~RB_POWERDOWN;
		}
#endif

		acpi_sleep_pm(sc, state);
		printf("%s: acpi_sleep_pm failed", DEVNAME(sc));
		return (ECANCELED);
	}
	/* Resume path */

	/* Reset the vectors */
	sc->sc_facs->wakeup_vector = 0;
	if (sc->sc_facs->length > 32 && sc->sc_facs->version >= 1)
		sc->sc_facs->x_wakeup_vector = 0;

	pmap_kremove(ACPI_TRAMPOLINE, PAGE_SIZE);
	pmap_kremove(ACPI_TRAMP_DATA, PAGE_SIZE);

	return (0);
}

/*
 * First repair the interrupt hardware so that any events which occur
 * will cause the least number of unexpected side effects.  We re-start
 * the clocks early because we will soon run AML which might do DELAY.
 * Then PM, and then further system/CPU work for the BSP cpu.
 */ 
void
acpi_resume_cpu(struct acpi_softc *sc, int state)
{
#if NISA > 0
	isa_defaultirq();
#endif
	intr_calculatemasks();

#if NIOAPIC > 0
	ioapic_enable();
#endif

#if NLAPIC > 0
	lapic_tpr = save_lapic_tpr;
	lapic_enable();
	lapic_set_lvt();
#endif

	i8254_startclock();
	if (initclock_func == i8254_initclocks)
		rtcstart();		/* in i8254 mode, rtc is profclock */

	acpi_resume_pm(sc, state);

	npxinit(&cpu_info_primary);

	cpu_init(&cpu_info_primary);
	cpu_ucode_apply(&cpu_info_primary);
	cpu_tsx_disable(&cpu_info_primary);
	
	/* Re-initialise memory range handling on BSP */
	if (mem_range_softc.mr_op != NULL)
		mem_range_softc.mr_op->initAP(&mem_range_softc);
}

#ifdef MULTIPROCESSOR
void
sleep_mp(void)
{
	int i;

	/* 
	 * Wait for cpus to halt so we know their FPU state has been
	 * saved and their caches have been written back.
	 */
	i386_broadcast_ipi(I386_IPI_HALT);
	for (i = 0; i < ncpus; i++) {
		struct cpu_info *ci = cpu_info[i];

		if (CPU_IS_PRIMARY(ci))
			continue;
		while (ci->ci_flags & CPUF_RUNNING)
			;
	}
}

void
resume_mp(void)
{
	struct cpu_info *ci;
	struct proc *p;
	struct pcb *pcb;
	struct trapframe *tf;
	struct switchframe *sf;
	int i;

	/* XXX refactor with matching code in cpu.c */

	for (i = 0; i < MAXCPUS; i++) {
		ci = cpu_info[i];
		if (ci == NULL)
			continue;
		if (ci->ci_idle_pcb == NULL)
			continue;
		if (ci->ci_flags & (CPUF_BSP|CPUF_SP|CPUF_PRIMARY))
			continue;
		KASSERT((ci->ci_flags & CPUF_RUNNING) == 0);

		p = ci->ci_schedstate.spc_idleproc;
		pcb = &p->p_addr->u_pcb;

		tf = (struct trapframe *)pcb->pcb_kstack - 1;
		sf = (struct switchframe *)tf - 1;
		sf->sf_esi = (int)sched_idle;
		sf->sf_ebx = (int)ci;
		sf->sf_eip = (int)proc_trampoline;
		pcb->pcb_esp = (int)sf;

		ci->ci_idepth = 0;
	}
	cpu_boot_secondary_processors();
}
#endif /* MULTIPROCESSOR */

#endif /* ! SMALL_KERNEL */

bus_dma_tag_t
acpi_iommu_device_map(struct aml_node *node, bus_dma_tag_t dmat)
{
	return dmat;
}
