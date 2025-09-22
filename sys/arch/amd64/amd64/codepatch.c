/*      $OpenBSD: codepatch.c,v 1.11 2024/02/04 20:18:48 guenther Exp $    */
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
#include <uvm/uvm_extern.h> /* round_page */

#ifdef CODEPATCH_DEBUG
#define DBGPRINT(fmt, args...)	printf("%s: " fmt "\n", __func__, ## args)
#else
#define DBGPRINT(fmt, args...)	do {} while (0)
#endif

struct codepatch {
	vaddr_t addr;
	uint16_t len;
	uint16_t tag;
	uint32_t _padding;
};
CTASSERT(sizeof(struct codepatch) % 8 == 0);

extern struct codepatch codepatch_begin;
extern struct codepatch codepatch_end;
extern char __cptext_start[];
extern char __cptext_end[];

enum op_type { OP_CALL, OP_JMP };
__cptext void	codepatch_control_flow(uint16_t _tag, void *_func,
			enum op_type _type);

void
codepatch_fill_nop(void *caddr, uint16_t len)
{
	unsigned char *addr = caddr;
	uint16_t nop_len;

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

void
codepatch_call(uint16_t tag, void *func)
{
	codepatch_control_flow(tag, func, OP_CALL);
}

void
codepatch_jmp(uint16_t tag, void *func)
{
	codepatch_control_flow(tag, func, OP_JMP);
}

/* Patch with call or jump to func */
void
codepatch_control_flow(uint16_t tag, void *func, enum op_type type)
{
	struct codepatch *patch;
	unsigned char *rwaddr;
	int32_t offset;
	int i = 0;
	vaddr_t rwmap = 0;
	const char *op = type == OP_JMP ? "jmp" : "call";
	char opcode = type == OP_JMP ? 0xe9 /* jmp near */
	    : 0xe8 /* call near */;

	DBGPRINT("patching tag %u with %s %p", tag, op, func);

	for (patch = &codepatch_begin; patch < &codepatch_end; patch++) {
		if (patch->tag != tag)
			continue;
		if (patch->len < 5)
			panic("%s: can't replace len %u with %s at %#lx",
			    __func__, patch->len, op, patch->addr);

		offset = (vaddr_t)func - (patch->addr + 5);
		rwaddr = codepatch_maprw(&rwmap, patch->addr);
		rwaddr[0] = opcode;
		memcpy(rwaddr + 1, &offset, sizeof(offset));
		if (type == OP_CALL)
			codepatch_fill_nop(rwaddr + 5, patch->len - 5);
		else /* OP_JMP */
			memset(rwaddr + 5, 0xCC /* int3 */, patch->len - 5);
		i++;
	}
	codepatch_unmaprw(rwmap);
	DBGPRINT("patched %d places", i);
}

void
codepatch_disable(void)
{
	size_t size = round_page(__cptext_end - __cptext_start);
	/* If this assert fails, something is wrong with the cptext section */
	KASSERT(size > 0);
	pmap_kremove((vaddr_t)__cptext_start, size);
	pmap_update(pmap_kernel());
	DBGPRINT("%s: Unmapped %#zx bytes\n", __func__, size);
}
