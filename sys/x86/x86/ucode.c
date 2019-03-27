/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 The FreeBSD Foundation
 *
 * This software was developed by Mark Johnston under sponsorship from
 * the FreeBSD Foundation.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/cpuset.h>
#include <sys/kernel.h>
#include <sys/linker.h>
#include <sys/malloc.h>
#include <sys/pcpu.h>
#include <sys/smp.h>
#include <sys/systm.h>

#include <machine/atomic.h>
#include <machine/cpufunc.h>
#include <x86/specialreg.h>
#include <machine/stdarg.h>
#include <x86/ucode.h>
#include <x86/x86_smp.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_param.h>

static void	*ucode_intel_match(uint8_t *data, size_t *len);
static int	ucode_intel_verify(struct ucode_intel_header *hdr,
		    size_t resid);

static struct ucode_ops {
	const char *vendor;
	int (*load)(void *, bool, uint64_t *, uint64_t *);
	void *(*match)(uint8_t *, size_t *);
} loaders[] = {
	{
		.vendor = INTEL_VENDOR_ID,
		.load = ucode_intel_load,
		.match = ucode_intel_match,
	},
};

/* Selected microcode update data. */
static void *early_ucode_data;
static void *ucode_data;
static struct ucode_ops *ucode_loader;

/* Variables used for reporting success or failure. */
enum {
	NO_ERROR,
	NO_MATCH,
	VERIFICATION_FAILED,
} ucode_error = NO_ERROR;
static uint64_t ucode_nrev, ucode_orev;

static void
log_msg(void *arg __unused)
{

	if (ucode_nrev != 0) {
		printf("CPU microcode: updated from %#jx to %#jx\n",
		    (uintmax_t)ucode_orev, (uintmax_t)ucode_nrev);
		return;
	}

	switch (ucode_error) {
	case NO_MATCH:
		printf("CPU microcode: no matching update found\n");
		break;
	case VERIFICATION_FAILED:
		printf("CPU microcode: microcode verification failed\n");
		break;
	default:
		break;
	}
}
SYSINIT(ucode_log, SI_SUB_CPU, SI_ORDER_FIRST, log_msg, NULL);

int
ucode_intel_load(void *data, bool unsafe, uint64_t *nrevp, uint64_t *orevp)
{
	uint64_t nrev, orev;
	uint32_t cpuid[4];

	orev = rdmsr(MSR_BIOS_SIGN) >> 32;

	/*
	 * Perform update.  Flush caches first to work around seemingly
	 * undocumented errata applying to some Broadwell CPUs.
	 */
	wbinvd();
	if (unsafe)
		wrmsr_safe(MSR_BIOS_UPDT_TRIG, (uint64_t)(uintptr_t)data);
	else
		wrmsr(MSR_BIOS_UPDT_TRIG, (uint64_t)(uintptr_t)data);
	wrmsr(MSR_BIOS_SIGN, 0);

	/*
	 * Serialize instruction flow.
	 */
	do_cpuid(0, cpuid);

	/*
	 * Verify that the microcode revision changed.
	 */
	nrev = rdmsr(MSR_BIOS_SIGN) >> 32;
	if (nrevp != NULL)
		*nrevp = nrev;
	if (orevp != NULL)
		*orevp = orev;
	if (nrev <= orev)
		return (EEXIST);
	return (0);
}

static int
ucode_intel_verify(struct ucode_intel_header *hdr, size_t resid)
{
	uint32_t cksum, *data, size;
	int i;

	if (resid < sizeof(struct ucode_intel_header))
		return (1);
	size = hdr->total_size;
	if (size == 0)
		size = UCODE_INTEL_DEFAULT_DATA_SIZE +
		    sizeof(struct ucode_intel_header);

	if (hdr->header_version != 1)
		return (1);
	if (size % 16 != 0)
		return (1);
	if (resid < size)
		return (1);

	cksum = 0;
	data = (uint32_t *)hdr;
	for (i = 0; i < size / sizeof(uint32_t); i++)
		cksum += data[i];
	if (cksum != 0)
		return (1);
	return (0);
}

static void *
ucode_intel_match(uint8_t *data, size_t *len)
{
	struct ucode_intel_header *hdr;
	struct ucode_intel_extsig_table *table;
	struct ucode_intel_extsig *entry;
	uint64_t platformid;
	size_t resid;
	uint32_t data_size, flags, regs[4], sig, total_size;
	int i;

	do_cpuid(1, regs);
	sig = regs[0];

	platformid = rdmsr(MSR_IA32_PLATFORM_ID);
	flags = 1 << ((platformid >> 50) & 0x7);

	for (resid = *len; resid > 0; data += total_size, resid -= total_size) {
		hdr = (struct ucode_intel_header *)data;
		if (ucode_intel_verify(hdr, resid) != 0) {
			ucode_error = VERIFICATION_FAILED;
			break;
		}

		data_size = hdr->data_size;
		total_size = hdr->total_size;
		if (data_size == 0)
			data_size = UCODE_INTEL_DEFAULT_DATA_SIZE;
		if (total_size == 0)
			total_size = UCODE_INTEL_DEFAULT_DATA_SIZE +
			    sizeof(struct ucode_intel_header);
		if (data_size > total_size + sizeof(struct ucode_intel_header))
			table = (struct ucode_intel_extsig_table *)
			    ((uint8_t *)(hdr + 1) + data_size);
		else
			table = NULL;

		if (hdr->processor_signature == sig) {
			if ((hdr->processor_flags & flags) != 0) {
				*len = data_size;
				return (hdr + 1);
			}
		} else if (table != NULL) {
			for (i = 0; i < table->signature_count; i++) {
				entry = &table->entries[i];
				if (entry->processor_signature == sig &&
				    (entry->processor_flags & flags) != 0) {
					*len = data_size;
					return (hdr + 1);
				}
			}
		}
	}
	return (NULL);
}

/*
 * Release any memory backing unused microcode blobs back to the system.
 * We copy the selected update and free the entire microcode file.
 */
static void
ucode_release(void *arg __unused)
{
	char *name, *type;
	caddr_t file;
	int release;

	if (early_ucode_data == NULL)
		return;
	release = 1;
	TUNABLE_INT_FETCH("debug.ucode.release", &release);
	if (!release)
		return;

restart:
	file = 0;
	for (;;) {
		file = preload_search_next_name(file);
		if (file == 0)
			break;
		type = (char *)preload_search_info(file, MODINFO_TYPE);
		if (type == NULL || strcmp(type, "cpu_microcode") != 0)
			continue;

		name = preload_search_info(file, MODINFO_NAME);
		preload_delete_name(name);
		goto restart;
	}
}
SYSINIT(ucode_release, SI_SUB_KMEM + 1, SI_ORDER_ANY, ucode_release, NULL);

void
ucode_load_ap(int cpu)
{
#ifdef SMP
	KASSERT(cpu_info[cpu_apic_ids[cpu]].cpu_present,
	    ("cpu %d not present", cpu));

	if (cpu_info[cpu_apic_ids[cpu]].cpu_hyperthread)
		return;
#endif

	if (ucode_data != NULL)
		(void)ucode_loader->load(ucode_data, false, NULL, NULL);
}

static void *
map_ucode(uintptr_t free, size_t len)
{
#ifdef __i386__
	uintptr_t va;

	for (va = free; va < free + len; va += PAGE_SIZE)
		pmap_kenter(va, (vm_paddr_t)va);
#else
	(void)len;
#endif
	return ((void *)free);
}

static void
unmap_ucode(uintptr_t free, size_t len)
{
#ifdef __i386__
	uintptr_t va;

	for (va = free; va < free + len; va += PAGE_SIZE)
		pmap_kremove(va);
#else
	(void)free;
	(void)len;
#endif
}

/*
 * Search for an applicable microcode update, and load it.  APs will load the
 * selected update once they come online.
 *
 * "free" is the address of the next free physical page.  If a microcode update
 * is selected, it will be copied to this region prior to loading in order to
 * satisfy alignment requirements.
 */
size_t
ucode_load_bsp(uintptr_t free)
{
	union {
		uint32_t regs[4];
		char vendor[13];
	} cpuid;
	uint8_t *addr, *fileaddr, *match;
	char *type;
	uint64_t nrev, orev;
	caddr_t file;
	size_t i, len;
	int error;

	KASSERT(free % PAGE_SIZE == 0, ("unaligned boundary %p", (void *)free));

	do_cpuid(0, cpuid.regs);
	cpuid.regs[0] = cpuid.regs[1];
	cpuid.regs[1] = cpuid.regs[3];
	cpuid.vendor[12] = '\0';
	for (i = 0; i < nitems(loaders); i++)
		if (strcmp(cpuid.vendor, loaders[i].vendor) == 0) {
			ucode_loader = &loaders[i];
			break;
		}
	if (ucode_loader == NULL)
		return (0);

	file = 0;
	fileaddr = match = NULL;
	for (;;) {
		file = preload_search_next_name(file);
		if (file == 0)
			break;
		type = (char *)preload_search_info(file, MODINFO_TYPE);
		if (type == NULL || strcmp(type, "cpu_microcode") != 0)
			continue;

		fileaddr = preload_fetch_addr(file);
		len = preload_fetch_size(file);
		match = ucode_loader->match(fileaddr, &len);
		if (match != NULL) {
			addr = map_ucode(free, len);
			/* We can't use memcpy() before ifunc resolution. */
			memcpy_early(addr, match, len);
			match = addr;

			error = ucode_loader->load(match, false, &nrev, &orev);
			if (error == 0) {
				ucode_data = early_ucode_data = match;
				ucode_nrev = nrev;
				ucode_orev = orev;
				return (len);
			}
			unmap_ucode(free, len);
		}
	}
	if (fileaddr != NULL && ucode_error == NO_ERROR)
		ucode_error = NO_MATCH;
	return (0);
}

/*
 * Reload microcode following an ACPI resume.
 */
void
ucode_reload(void)
{

	ucode_load_ap(PCPU_GET(cpuid));
}

/*
 * Replace an existing microcode update.
 */
void *
ucode_update(void *newdata)
{

	newdata = (void *)atomic_swap_ptr((void *)&ucode_data,
	    (uintptr_t)newdata);
	if (newdata == early_ucode_data)
		newdata = NULL;
	return (newdata);
}
