/*	$OpenBSD: ucode.c,v 1.6 2023/09/10 09:32:31 jsg Exp $	*/
/*
 * Copyright (c) 2018 Stefan Fritsch <fritsch@genua.de>
 * Copyright (c) 2018 Patrick Wildt <patrick@blueri.se>
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
#include <sys/mutex.h>
#include <sys/malloc.h>

#include <uvm/uvm_extern.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/specialreg.h>
#include <machine/biosvar.h>

/* #define UCODE_DEBUG */
#ifdef UCODE_DEBUG
#define DPRINTF(x)	do { if (cpu_ucode_debug > 0) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (cpu_ucode_debug >= (n)) printf x; } while (0)
int cpu_ucode_debug = 1;
#else
#define DPRINTF(x)	do { ; } while (0)
#define DPRINTFN(n, x)	do { ; } while (0)
#endif

struct intel_ucode_header {
	uint32_t	header_version;
	uint32_t	update_revision;
	uint32_t	date;
	uint32_t	processor_sig;
	uint32_t	checksum;
	uint32_t	loader_rev;
	uint32_t	processor_flags;
	uint32_t	data_size;
	uint32_t	total_size;
	uint32_t	reserved[3];
};

struct intel_ucode_ext_sig_header {
	uint32_t	ext_sig_count;
	uint32_t	checksum;
	uint32_t	reserved[3];
};

struct intel_ucode_ext_sig {
	uint32_t	processor_sig;
	uint32_t	processor_flags;
	uint32_t	checksum;
};

#define INTEL_UCODE_DEFAULT_DATA_SIZE		2000

/* Generic */
char *	 cpu_ucode_data;
size_t	 cpu_ucode_size;

void	 cpu_ucode_setup(void);
void	 cpu_ucode_apply(struct cpu_info *);

struct mutex	cpu_ucode_mtx = MUTEX_INITIALIZER(IPL_HIGH);

/* Intel */
void	 cpu_ucode_intel_apply(struct cpu_info *);
struct intel_ucode_header *
	 cpu_ucode_intel_find(char *, size_t, uint32_t);
int	 cpu_ucode_intel_verify(struct intel_ucode_header *);
int	 cpu_ucode_intel_match(struct intel_ucode_header *, uint32_t, uint32_t,
	    uint32_t);
uint32_t cpu_ucode_intel_rev(void);

struct intel_ucode_header	*cpu_ucode_intel_applied;

void cpu_ucode_amd_apply(struct cpu_info *);

void
cpu_ucode_setup(void)
{
	vaddr_t va;
	paddr_t pa;
	int i, npages;
	size_t size;

	if (bios_ucode == NULL)
		return;

	if (!bios_ucode->uc_addr || !bios_ucode->uc_size)
		return;

	cpu_ucode_size = bios_ucode->uc_size;
	size = round_page(bios_ucode->uc_size);
	npages = size / PAGE_SIZE;

	va = (vaddr_t)km_alloc(size, &kv_any, &kp_none, &kd_nowait);
	if (va == 0)
		return;
	for (i = 0; i < npages; i++) {
		pa = bios_ucode->uc_addr + (i * PAGE_SIZE);
		pmap_enter(pmap_kernel(), va + (i * PAGE_SIZE), pa,
		    PROT_READ,
		    PROT_READ | PMAP_WIRED);
		pmap_update(pmap_kernel());
	}

	cpu_ucode_data = malloc(cpu_ucode_size, M_DEVBUF, M_WAITOK);

	memcpy((void *)cpu_ucode_data, (void *)va, cpu_ucode_size);

	pmap_remove(pmap_kernel(), va, va + size);
	pmap_update(pmap_kernel());
	km_free((void *)va, size, &kv_any, &kp_none);
}

/*
 * Called per-CPU.
 */
void
cpu_ucode_apply(struct cpu_info *ci)
{
	if (strcmp(cpu_vendor, "GenuineIntel") == 0)
		cpu_ucode_intel_apply(ci);
	else if (strcmp(cpu_vendor, "AuthenticAMD") == 0)
		cpu_ucode_amd_apply(ci);
}

#define AMD_MAGIC 0x00414d44

struct amd_equiv {
	uint32_t id;
	uint32_t a;
	uint32_t b;
	uint16_t eid;
	uint16_t c;
} __packed;

struct amd_patch {
	uint32_t type;
	uint32_t len;
	uint32_t a;
	uint32_t level;
	uint8_t c[16];
	uint16_t eid;
} __packed;

void
cpu_ucode_amd_apply(struct cpu_info *ci)
{
	uint64_t level;
	uint32_t magic, tlen, i;
	uint16_t eid = 0;
	uint32_t sig, ebx, ecx, edx;
	uint64_t start = 0;
	uint32_t patch_len = 0;

	if (cpu_ucode_data == NULL || cpu_ucode_size == 0) {
		DPRINTF(("%s: no microcode provided\n", __func__));
		return;
	}

	/*
	 * Grab a mutex, because we are not allowed to run updates
	 * simultaneously on HT siblings.
	 */
	mtx_enter(&cpu_ucode_mtx);

	CPUID(1, sig, ebx, ecx, edx);

	level = rdmsr(MSR_PATCH_LEVEL);
	DPRINTF(("%s: cur patch level 0x%llx\n", __func__, level));

	memcpy(&magic, cpu_ucode_data, 4);
	if (magic != AMD_MAGIC) {
		DPRINTF(("%s: bad magic %x\n", __func__, magic));
		goto out;
	}

	memcpy(&tlen, &cpu_ucode_data[8], 4);

	/* find equivalence id matching our cpu signature */
	for (i = 12; i < 12 + tlen;) {
		struct amd_equiv ae;
		if (i + sizeof(ae) > cpu_ucode_size) {
			DPRINTF(("%s: truncated etable\n", __func__));
			goto out;
		}
		memcpy(&ae, &cpu_ucode_data[i], sizeof(ae));
		i += sizeof(ae);
		if (ae.id == sig)
			eid = ae.eid;
	}

	/* look for newer patch with the equivalence id */
	while (i < cpu_ucode_size) {
		struct amd_patch ap;
		if (i + sizeof(ap) > cpu_ucode_size) {
			DPRINTF(("%s: truncated ptable\n", __func__));
			goto out;
		}
		memcpy(&ap, &cpu_ucode_data[i], sizeof(ap));
		if (ap.type == 1 && ap.eid == eid && ap.level > level) {
			start = (uint64_t)&cpu_ucode_data[i + 8];
			patch_len = ap.len;
		}
		if (i + ap.len + 8 > cpu_ucode_size) {
			DPRINTF(("%s: truncated patch\n", __func__));
			goto out;
		}
		i += ap.len + 8;
	}

	if (start != 0) {
		/* alignment required on fam 15h */
		uint8_t *p = malloc(patch_len, M_TEMP, M_NOWAIT);
		if (p == NULL)
			goto out;
		memcpy(p, (uint8_t *)start, patch_len);
		start = (uint64_t)p;
		wrmsr(MSR_PATCH_LOADER, start);
		level = rdmsr(MSR_PATCH_LEVEL);
		DPRINTF(("%s: new patch level 0x%llx\n", __func__, level));
		free(p, M_TEMP, patch_len);
	}
out:
	mtx_leave(&cpu_ucode_mtx);
}

void
cpu_ucode_intel_apply(struct cpu_info *ci)
{
	struct intel_ucode_header *update;
	uint32_t old_rev, new_rev;
	paddr_t data;

	if (cpu_ucode_data == NULL || cpu_ucode_size == 0) {
		DPRINTF(("%s: no microcode provided\n", __func__));
		return;
	}

	/*
	 * Grab a mutex, because we are not allowed to run updates
	 * simultaneously on HT siblings.
	 */
	mtx_enter(&cpu_ucode_mtx);

	old_rev = cpu_ucode_intel_rev();
	update = cpu_ucode_intel_applied;
	if (update == NULL)
		update = cpu_ucode_intel_find(cpu_ucode_data,
		    cpu_ucode_size, old_rev);
	if (update == NULL) {
		DPRINTF(("%s: no microcode update found\n", __func__));
		goto out;
	}
	if (update->update_revision == old_rev) {
		DPRINTF(("%s: microcode already up-to-date\n", __func__));
		goto out;
	}

	/* Apply microcode. */
	data = (paddr_t)update;
	data += sizeof(struct intel_ucode_header);
	wbinvd();
	wrmsr(MSR_BIOS_UPDT_TRIG, data);

	new_rev = cpu_ucode_intel_rev();
	if (new_rev != old_rev) {
		DPRINTF(("%s: microcode updated cpu %ld rev %#x->%#x (%x)\n",
		    __func__, ci->ci_cpuid, old_rev, new_rev, update->date));
		if (cpu_ucode_intel_applied == NULL)
			cpu_ucode_intel_applied = update;
	} else {
		DPRINTF(("%s: microcode update failed cpu %ld rev %#x->%#x != %#x\n",
		     __func__, ci->ci_cpuid, old_rev, update->update_revision, new_rev));
	}

out:
	mtx_leave(&cpu_ucode_mtx);
}

struct intel_ucode_header *
cpu_ucode_intel_find(char *data, size_t left, uint32_t current)
{
	uint64_t platform_id = (rdmsr(MSR_PLATFORM_ID) >> 50) & 7;
	uint32_t sig, dummy1, dummy2, dummy3;
	uint32_t mask = 1UL << platform_id;
	struct intel_ucode_header *hdr;
	uint32_t total_size;
	int n = 0;

	CPUID(1, sig, dummy1, dummy2, dummy3);

	while (left > 0) {
		hdr = (struct intel_ucode_header *)data;
		if (left < sizeof(struct intel_ucode_header)) {
			DPRINTF(("%s:%d: not enough data for header (%zd)\n",
			    __func__, n, left));
			break;
		}
		/*
		 * Older microcode has an empty length.  In that case we
		 * have to use the default length of 2000.
		 */
		if (hdr->data_size)
			total_size = hdr->total_size;
		else
			total_size = INTEL_UCODE_DEFAULT_DATA_SIZE +
			     sizeof(struct intel_ucode_header);
		if (total_size > left) {
			DPRINTF(("%s:%d: size %u out of range (%zd)\n",
			    __func__, n, total_size, left));
			break;
		}
		if (cpu_ucode_intel_verify(hdr)) {
			DPRINTF(("%s:%d: broken data\n", __func__, n));
			break;
		}
		if (cpu_ucode_intel_match(hdr, sig, mask, current))
			return hdr;
		n++;
		left -= total_size;
		data += total_size;
	}
	DPRINTF(("%s: no update found\n", __func__));
	return NULL;
}

int
cpu_ucode_intel_verify(struct intel_ucode_header *hdr)
{
	uint32_t *data = (uint32_t *)hdr;
	size_t total_size;
	uint32_t sum;
	int i;

	CTASSERT(sizeof(struct intel_ucode_header) == 48);

	if ((paddr_t)data % 16 != 0) {
		DPRINTF(("%s: misaligned microcode update\n", __func__));
		return 1;
	}
	if (hdr->loader_rev != 1) {
		DPRINTF(("%s: unsupported loader rev\n", __func__));
		return 1;
	}

	if (hdr->data_size)
		total_size = hdr->total_size;
	else
		total_size = INTEL_UCODE_DEFAULT_DATA_SIZE +
		    sizeof(struct intel_ucode_header);
	if (total_size % 4 != 0) {
		DPRINTF(("%s: inconsistent size\n", __func__));
		return 1;
	}

	sum = 0;
	for (i = 0; i < total_size / 4; i++)
		sum += data[i];
	if (sum != 0) {
		DPRINTF(("%s: wrong checksum (%#x)\n", __func__, sum));
		return 1;
	}

	return 0;
}

int
cpu_ucode_intel_match(struct intel_ucode_header *hdr,
    uint32_t processor_sig, uint32_t processor_mask,
    uint32_t ucode_revision)
{
	struct intel_ucode_ext_sig_header *ehdr;
	struct intel_ucode_ext_sig *esig;
	uint32_t data_size, total_size;
	unsigned i;

	data_size = hdr->data_size;
	total_size = hdr->total_size;

	/*
	 * Older microcode has an empty length.  In that case we
	 * have to use the default length of 2000.
	 */
	if (!data_size) {
		data_size = INTEL_UCODE_DEFAULT_DATA_SIZE;
		total_size = INTEL_UCODE_DEFAULT_DATA_SIZE +
		    sizeof(struct intel_ucode_header);
	}

	if (ucode_revision > hdr->update_revision)
		return 0;
	if (hdr->processor_sig == processor_sig &&
	    (hdr->processor_flags & processor_mask))
		return 1;
	if (total_size <= sizeof(struct intel_ucode_header) +
	    data_size + sizeof(struct intel_ucode_ext_sig_header))
		return 0;

	ehdr = (void *)((char *)hdr + sizeof(struct intel_ucode_header) +
	    data_size);
	esig = (void *)&ehdr[1];
	for (i = 0; i < ehdr->ext_sig_count; i++) {
		if (esig[i].processor_sig == processor_sig &&
		    (esig[i].processor_flags & processor_mask))
			return 1;
	}

	return 0;
}

uint32_t
cpu_ucode_intel_rev(void)
{
	uint32_t eax, ebx, ecx, edx;
	uint64_t rev;

	wrmsr(MSR_BIOS_SIGN, 0);
	CPUID(1, eax, ebx, ecx, edx);
	rev = rdmsr(MSR_BIOS_SIGN);
	return rev >> 32;
}
