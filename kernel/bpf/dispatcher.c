// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2019 Intel Corporation. */

#include <linux/hash.h>
#include <linux/bpf.h>
#include <linux/filter.h>

/* The BPF dispatcher is a multiway branch code generator. The
 * dispatcher is a mechanism to avoid the performance penalty of an
 * indirect call, which is expensive when retpolines are enabled. A
 * dispatch client registers a BPF program into the dispatcher, and if
 * there is available room in the dispatcher a direct call to the BPF
 * program will be generated. All calls to the BPF programs called via
 * the dispatcher will then be a direct call, instead of an
 * indirect. The dispatcher hijacks a trampoline function it via the
 * __fentry__ of the trampoline. The trampoline function has the
 * following signature:
 *
 * unsigned int trampoline(const void *ctx, const struct bpf_insn *insnsi,
 *                         unsigned int (*bpf_func)(const void *,
 *                                                  const struct bpf_insn *));
 */

static struct bpf_dispatcher_prog *bpf_dispatcher_find_prog(
	struct bpf_dispatcher *d, struct bpf_prog *prog)
{
	int i;

	for (i = 0; i < BPF_DISPATCHER_MAX; i++) {
		if (prog == d->progs[i].prog)
			return &d->progs[i];
	}
	return NULL;
}

static struct bpf_dispatcher_prog *bpf_dispatcher_find_free(
	struct bpf_dispatcher *d)
{
	return bpf_dispatcher_find_prog(d, NULL);
}

static bool bpf_dispatcher_add_prog(struct bpf_dispatcher *d,
				    struct bpf_prog *prog)
{
	struct bpf_dispatcher_prog *entry;

	if (!prog)
		return false;

	entry = bpf_dispatcher_find_prog(d, prog);
	if (entry) {
		refcount_inc(&entry->users);
		return false;
	}

	entry = bpf_dispatcher_find_free(d);
	if (!entry)
		return false;

	bpf_prog_inc(prog);
	entry->prog = prog;
	refcount_set(&entry->users, 1);
	d->num_progs++;
	return true;
}

static bool bpf_dispatcher_remove_prog(struct bpf_dispatcher *d,
				       struct bpf_prog *prog)
{
	struct bpf_dispatcher_prog *entry;

	if (!prog)
		return false;

	entry = bpf_dispatcher_find_prog(d, prog);
	if (!entry)
		return false;

	if (refcount_dec_and_test(&entry->users)) {
		entry->prog = NULL;
		bpf_prog_put(prog);
		d->num_progs--;
		return true;
	}
	return false;
}

int __weak arch_prepare_bpf_dispatcher(void *image, void *buf, s64 *funcs, int num_funcs)
{
	return -ENOTSUPP;
}

static int bpf_dispatcher_prepare(struct bpf_dispatcher *d, void *image, void *buf)
{
	s64 ips[BPF_DISPATCHER_MAX] = {}, *ipsp = &ips[0];
	int i;

	for (i = 0; i < BPF_DISPATCHER_MAX; i++) {
		if (d->progs[i].prog)
			*ipsp++ = (s64)(uintptr_t)d->progs[i].prog->bpf_func;
	}
	return arch_prepare_bpf_dispatcher(image, buf, &ips[0], d->num_progs);
}

static void bpf_dispatcher_update(struct bpf_dispatcher *d, int prev_num_progs)
{
	void *old, *new, *tmp;
	u32 noff;
	int err;

	if (!prev_num_progs) {
		old = NULL;
		noff = 0;
	} else {
		old = d->image + d->image_off;
		noff = d->image_off ^ (PAGE_SIZE / 2);
	}

	new = d->num_progs ? d->image + noff : NULL;
	tmp = d->num_progs ? d->rw_image + noff : NULL;
	if (new) {
		/* Prepare the dispatcher in d->rw_image. Then use
		 * bpf_arch_text_copy to update d->image, which is RO+X.
		 */
		if (bpf_dispatcher_prepare(d, new, tmp))
			return;
		if (IS_ERR(bpf_arch_text_copy(new, tmp, PAGE_SIZE / 2)))
			return;
	}

	err = bpf_arch_text_poke(d->func, BPF_MOD_JUMP, old, new);
	if (err || !new)
		return;

	d->image_off = noff;
}

void bpf_dispatcher_change_prog(struct bpf_dispatcher *d, struct bpf_prog *from,
				struct bpf_prog *to)
{
	bool changed = false;
	int prev_num_progs;

	if (from == to)
		return;

	mutex_lock(&d->mutex);
	if (!d->image) {
		d->image = bpf_prog_pack_alloc(PAGE_SIZE, bpf_jit_fill_hole_with_zero);
		if (!d->image)
			goto out;
		d->rw_image = bpf_jit_alloc_exec(PAGE_SIZE);
		if (!d->rw_image) {
			u32 size = PAGE_SIZE;

			bpf_arch_text_copy(d->image, &size, sizeof(size));
			bpf_prog_pack_free((struct bpf_binary_header *)d->image);
			d->image = NULL;
			goto out;
		}
		bpf_image_ksym_add(d->image, &d->ksym);
	}

	prev_num_progs = d->num_progs;
	changed |= bpf_dispatcher_remove_prog(d, from);
	changed |= bpf_dispatcher_add_prog(d, to);

	if (!changed)
		goto out;

	bpf_dispatcher_update(d, prev_num_progs);
out:
	mutex_unlock(&d->mutex);
}
