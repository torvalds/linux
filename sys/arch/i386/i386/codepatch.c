/*      $OpenBSD: codepatch.c,v 1.6 2023/07/31 17:10:31 bluhm Exp $    */
/*
 * Copyright (c) 2014-2015 Stefan Fritsch <sf@sfritsch.de>
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
#include <machine/codepatch.h>
#include <machine/cpu.h>
#include <uvm/uvm_extern.h>

#ifdef CODEPATCH_DEBUG
#define DBGPRINT(fmt, args...)	printf("%s: " fmt "\n", __func__, ## args)
#else
#define DBGPRINT(fmt, args...)	do {} while (0)
#endif

struct codepatch {
	vaddr_t addr;
	uint16_t len;
	uint16_t tag;
};

extern struct codepatch codepatch_begin;
extern struct codepatch codepatch_end;

void
codepatch_fill_nop(void *caddr, uint16_t len)
{
	unsigned char *addr = caddr;
	uint16_t nop_len;

	if ((strcmp(cpu_vendor, "GenuineIntel") != 0) &&
	    (strcmp(cpu_vendor, "AuthenticAMD") != 0)) {
		/*
		 * Others don't support multi-byte NOPs.
		 * Except maybe some Via C3's, but I couldn't find
		 * definitive information, so better be safe.
		 */
		goto singlebyte;
	}
	/*
	 * Intel says family 0x6 or 0xf.
	 * AMD says "Athlon or newer", which happen to be the same families.
	 */
	switch (cpu_id & 0xf00) {
	case 0x600:
	case 0xf00:
		/* Multi-byte NOP supported */
		break;
	default:
		goto singlebyte;
	}

	while (len > 0) {
		nop_len = len < 127 ? len : 127;
		switch (nop_len) {
		case 1:
			addr[0] = 0x90;
			break;
		case 2:
			addr[0] = 0x66;
			addr[1] = 0x90;
			break;
		default:
			addr[0] = 0xEB;
			addr[1] = nop_len - 2;
			memset(addr + 2, 0xCC, nop_len - 2);
			break;
		}
		addr += nop_len;
		len -= nop_len;
	}
	return;

singlebyte:
	/* Use single-byte NOP */
	memset(caddr, 0x90, len);
}

/*
 * Create writeable aliases of memory we need
 * to write to as kernel is mapped read-only
 */
void *
codepatch_maprw(vaddr_t *nva, vaddr_t dest)
{
	paddr_t kva = trunc_page((paddr_t)dest);
	paddr_t po = (paddr_t)dest & PAGE_MASK;
	paddr_t pa1, pa2;

	if (*nva == 0)
		*nva = (vaddr_t)km_alloc(2 * PAGE_SIZE, &kv_any, &kp_none,
					&kd_waitok);

	pmap_extract(pmap_kernel(), kva, &pa1);
	pmap_extract(pmap_kernel(), kva + PAGE_SIZE, &pa2);
	pmap_kenter_pa(*nva, pa1, PROT_READ | PROT_WRITE);
	pmap_kenter_pa(*nva + PAGE_SIZE, pa2, PROT_READ | PROT_WRITE);
	pmap_update(pmap_kernel());

	return (void *)(*nva + po);
}

void
codepatch_unmaprw(vaddr_t nva)
{
	if (nva == 0)
		return;
	pmap_kremove(nva, 2 * PAGE_SIZE);
	km_free((void *)nva, 2 * PAGE_SIZE, &kv_any, &kp_none);
}

/* Patch with NOPs */
void
codepatch_nop(uint16_t tag)
{
	struct codepatch *patch;
	unsigned char *rwaddr;
	vaddr_t rwmap = 0;
	int i = 0;

	DBGPRINT("patching tag %u", tag);

	for (patch = &codepatch_begin; patch < &codepatch_end; patch++) {
		if (patch->tag != tag)
			continue;
		rwaddr = codepatch_maprw(&rwmap, patch->addr);
		codepatch_fill_nop(rwaddr, patch->len);
		i++;
	}
	codepatch_unmaprw(rwmap);
	DBGPRINT("patched %d places", i);
}

/* Patch with alternative code */
void
codepatch_replace(uint16_t tag, const void *code, size_t len)
{
	struct codepatch *patch;
	unsigned char *rwaddr;
	vaddr_t rwmap = 0;
	int i = 0;

	DBGPRINT("patching tag %u with %p", tag, code);

	for (patch = &codepatch_begin; patch < &codepatch_end; patch++) {
		if (patch->tag != tag)
			continue;

		if (len > patch->len) {
			panic("%s: can't replace len %u with %zu at %#lx",
			    __func__, patch->len, len, patch->addr);
		}
		rwaddr = codepatch_maprw(&rwmap, patch->addr);
		memcpy(rwaddr, code, len);
		codepatch_fill_nop(rwaddr + len, patch->len - len);
		i++;
	}
	codepatch_unmaprw(rwmap);
	DBGPRINT("patched %d places", i);
}

/* Patch with calls to func */
void
codepatch_call(uint16_t tag, void *func)
{
	struct codepatch *patch;
	unsigned char *rwaddr;
	int32_t offset;
	int i = 0;
	vaddr_t rwmap = 0;

	DBGPRINT("patching tag %u with call %p", tag, func);

	for (patch = &codepatch_begin; patch < &codepatch_end; patch++) {
		if (patch->tag != tag)
			continue;
		if (patch->len < 5)
			panic("%s: can't replace len %u with call at %#lx",
			    __func__, patch->len, patch->addr);

		offset = (vaddr_t)func - (patch->addr + 5);
		rwaddr = codepatch_maprw(&rwmap, patch->addr);
		rwaddr[0] = 0xe8; /* call near */
		memcpy(rwaddr + 1, &offset, sizeof(offset));
		codepatch_fill_nop(rwaddr + 5, patch->len - 5);
		i++;
	}
	codepatch_unmaprw(rwmap);
	DBGPRINT("patched %d places", i);
}
