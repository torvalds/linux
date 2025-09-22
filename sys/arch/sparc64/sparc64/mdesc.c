/*	$OpenBSD: mdesc.c,v 1.9 2023/04/10 04:21:20 jsg Exp $	*/
/*
 * Copyright (c) 2009 Mark Kettenis
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
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/hypervisor.h>
#include <machine/mdesc.h>
#include <machine/sparc64.h>

#define HSVC_GROUP_PARALLEL_BOOT	0x105

caddr_t mdesc;
paddr_t mdesc_pa;
size_t mdesc_len;

caddr_t pri;
paddr_t pri_pa;
size_t pri_len;

void pri_init(void);

void
mdesc_init(void)
{
	struct pglist mlist;
	struct vm_page *m;
	psize_t len = 0, size;
	paddr_t pa;
	vaddr_t va;
	int err;

	err = hv_mach_desc((paddr_t)NULL, &len);
	KASSERT(err == H_EINVAL);
	KASSERT(len != 0);

again:
	size = round_page(len);

	TAILQ_INIT(&mlist);
	err = uvm_pglistalloc(len, 0, -1, PAGE_SIZE, 0, &mlist, 1,
	    UVM_PLA_NOWAIT);
	if (err)
		panic("%s: out of memory", __func__);
 
	len = size;
	pa = VM_PAGE_TO_PHYS(TAILQ_FIRST(&mlist));
	err = hv_mach_desc(pa, &len);
	if (err != H_EOK)
		goto fail;

	va = (vaddr_t)km_alloc(size, &kv_any, &kp_none, &kd_nowait);
	if (va == 0)
		panic("%s: out of memory", __func__);

	mdesc = (caddr_t)va;
	mdesc_pa = pa;
	mdesc_len = len;

	m = TAILQ_FIRST(&mlist);
	for (; m != NULL; m = TAILQ_NEXT(m,pageq)) {
		pa = VM_PAGE_TO_PHYS(m);
		pmap_enter(pmap_kernel(), va, pa,
		    PROT_READ | PROT_WRITE,
		    PROT_READ | PROT_WRITE | PMAP_WIRED);
		va += PAGE_SIZE;
	}
	pmap_update(pmap_kernel());

	pri_init();
	return;

fail:
	uvm_pglistfree(&mlist);

	/*
	 * If the machine description was updated while we were trying
	 * to fetch it, the allocated buffer may have been too small.
	 * Try again in that case.
	 */
	if (err == H_EINVAL && len > size)
		goto again;

	return;
}

void
pri_init(void)
{
	struct pglist mlist;
	struct vm_page *m;
	uint64_t minor;
	psize_t len = 0, size;
	paddr_t pa;
	vaddr_t va;
	int err;

	/*
	 * We can only fetch the physical resource inventory this way
	 * if the firmware supports parallel boot.
	 */
	if (prom_set_sun4v_api_version(HSVC_GROUP_PARALLEL_BOOT, 1, 0, &minor))
		return;

	err = hv_mach_pri((paddr_t)NULL, &len);
	if (err != H_EINVAL)
		return;

again:
	size = round_page(len);

	TAILQ_INIT(&mlist);
	err = uvm_pglistalloc(len, 0, -1, PAGE_SIZE, 0, &mlist, 1,
	    UVM_PLA_NOWAIT);
	if (err)
		panic("%s: out of memory", __func__);
 
	len = size;
	pa = VM_PAGE_TO_PHYS(TAILQ_FIRST(&mlist));
	err = hv_mach_pri(pa, &len);
	if (err != H_EOK)
		goto fail;

	va = (vaddr_t)km_alloc(size, &kv_any, &kp_none, &kd_nowait);
	if (va == 0)
		panic("%s: out of memory", __func__);

	pri = (caddr_t)va;
	pri_pa = pa;
	pri_len = len;

	m = TAILQ_FIRST(&mlist);
	for (; m != NULL; m = TAILQ_NEXT(m,pageq)) {
		pa = VM_PAGE_TO_PHYS(m);
		pmap_enter(pmap_kernel(), va, pa,
		    PROT_READ | PROT_WRITE,
		    PROT_READ | PROT_WRITE | PMAP_WIRED);
		va += PAGE_SIZE;
	}
	pmap_update(pmap_kernel());

	return;

fail:
	uvm_pglistfree(&mlist);

	/*
	 * If the physical resource inventory was updated while we
	 * were trying to fetch it, the allocated buffer may have been
	 * too small.  Try again in that case.
	 */
	if (err == H_EINVAL && len > size)
		goto again;

	return;
}

uint64_t
mdesc_get_prop_val(int idx, const char *name)
{
	struct md_header *hdr;
	struct md_element *elem;
	const char *name_blk;
	const char *str;

	hdr = (struct md_header *)mdesc;
	elem = (struct md_element *)(mdesc + sizeof(struct md_header));
	name_blk = mdesc + sizeof(struct md_header) + hdr->node_blk_sz;

	while (elem[idx].tag != 'E') {
		str = name_blk + elem[idx].name_offset;
		if (elem[idx].tag == 'v' && strcmp(str, name) == 0)
			return (elem[idx].d.val);
		idx++;
	}

	return (-1);
}

const char *
mdesc_get_prop_str(int idx, const char *name)
{
	struct md_header *hdr;
	struct md_element *elem;
	const char *name_blk;
	const char *data_blk;
	const char *str;

	hdr = (struct md_header *)mdesc;
	elem = (struct md_element *)(mdesc + sizeof(struct md_header));
	name_blk = mdesc + sizeof(struct md_header) + hdr->node_blk_sz;
	data_blk = name_blk + hdr->name_blk_sz;

	while (elem[idx].tag != 'E') {
		str = name_blk + elem[idx].name_offset;
		if (elem[idx].tag == 's' && strcmp(str, name) == 0)
			return (data_blk + elem[idx].d.y.data_offset);
		idx++;
	}

	return (NULL);
}

const char *
mdesc_get_prop_data(int idx, const char *name, size_t *len)
{
	struct md_header *hdr;
	struct md_element *elem;
	const char *name_blk;
	const char *data_blk;
	const char *str;

	hdr = (struct md_header *)mdesc;
	elem = (struct md_element *)(mdesc + sizeof(struct md_header));
	name_blk = mdesc + sizeof(struct md_header) + hdr->node_blk_sz;
	data_blk = name_blk + hdr->name_blk_sz;

	while (elem[idx].tag != 'E') {
		str = name_blk + elem[idx].name_offset;
		if (elem[idx].tag == 'd' && strcmp(str, name) == 0) {
			*len = elem[idx].d.y.data_len;
			return (data_blk + elem[idx].d.y.data_offset);
		}
		idx++;
	}

	return (NULL);
}

int
mdesc_find(const char *name, uint64_t cfg_handle)
{
	struct md_header *hdr;
	struct md_element *elem;
	const char *str;
	uint64_t val;
	int idx;

	hdr = (struct md_header *)mdesc;
	elem = (struct md_element *)(mdesc + sizeof(struct md_header));

	for (idx = 0; elem[idx].tag == 'N'; idx = elem[idx].d.val) {
		str = mdesc_get_prop_str(idx, "name");
		val = mdesc_get_prop_val(idx, "cfg-handle");
		if (str && strcmp(str, name) == 0 && val == cfg_handle)
			return (idx);
	}

	return (-1);
}

int
mdesc_find_child(int idx, const char *name, uint64_t cfg_handle)
{
	struct md_header *hdr;
	struct md_element *elem;
	const char *name_blk;
	const char *str;
	uint64_t val;
	int arc;

	hdr = (struct md_header *)mdesc;
	elem = (struct md_element *)(mdesc + sizeof(struct md_header));
	name_blk = mdesc + sizeof(struct md_header) + hdr->node_blk_sz;

	for (; elem[idx].tag != 'E'; idx++) {
		str = name_blk + elem[idx].name_offset;
		if (elem[idx].tag != 'a' || strcmp(str, "fwd") != 0)
			continue;

		arc = elem[idx].d.val;
		str = mdesc_get_prop_str(arc, "name");
		val = mdesc_get_prop_val(arc, "cfg-handle");
		if (str && strcmp(str, name) == 0 && val == cfg_handle)
			return (arc);
	}

	return (-1);
}

int
mdesc_find_node(const char *name)
{
	struct md_header *hdr;
	struct md_element *elem;
	const char *name_blk;
	const char *str;
	int idx;

	hdr = (struct md_header *)mdesc;
	elem = (struct md_element *)(mdesc + sizeof(struct md_header));
	name_blk = mdesc + sizeof(struct md_header) + hdr->node_blk_sz;

	for (idx = 0; elem[idx].tag == 'N'; idx = elem[idx].d.val) {
		str = name_blk + elem[idx].name_offset;
		if (str && strcmp(str, name) == 0)
			return (idx);
	}

	return (-1);
}
