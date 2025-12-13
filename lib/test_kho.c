// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Test module for KHO
 * Copyright (c) 2025 Microsoft Corporation.
 *
 * Authors:
 *   Saurabh Sengar <ssengar@microsoft.com>
 *   Mike Rapoport <rppt@kernel.org>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/kexec.h>
#include <linux/libfdt.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/vmalloc.h>
#include <linux/kexec_handover.h>

#include <net/checksum.h>

#define KHO_TEST_MAGIC	0x4b484f21	/* KHO! */
#define KHO_TEST_FDT	"kho_test"
#define KHO_TEST_COMPAT "kho-test-v1"

static long max_mem = (PAGE_SIZE << MAX_PAGE_ORDER) * 2;
module_param(max_mem, long, 0644);

struct kho_test_state {
	unsigned int nr_folios;
	struct folio **folios;
	phys_addr_t *folios_info;
	struct folio *fdt;
	__wsum csum;
};

static struct kho_test_state kho_test_state;

static int kho_test_notifier(struct notifier_block *self, unsigned long cmd,
			     void *v)
{
	struct kho_test_state *state = &kho_test_state;
	struct kho_serialization *ser = v;
	int err = 0;

	switch (cmd) {
	case KEXEC_KHO_ABORT:
		return NOTIFY_DONE;
	case KEXEC_KHO_FINALIZE:
		/* Handled below */
		break;
	default:
		return NOTIFY_BAD;
	}

	err |= kho_preserve_folio(state->fdt);
	err |= kho_add_subtree(ser, KHO_TEST_FDT, folio_address(state->fdt));

	return err ? NOTIFY_BAD : NOTIFY_DONE;
}

static struct notifier_block kho_test_nb = {
	.notifier_call = kho_test_notifier,
};

static int kho_test_save_data(struct kho_test_state *state, void *fdt)
{
	phys_addr_t *folios_info __free(kvfree) = NULL;
	struct kho_vmalloc folios_info_phys;
	int err = 0;

	folios_info = vmalloc_array(state->nr_folios, sizeof(*folios_info));
	if (!folios_info)
		return -ENOMEM;

	err = kho_preserve_vmalloc(folios_info, &folios_info_phys);
	if (err)
		return err;

	for (int i = 0; i < state->nr_folios; i++) {
		struct folio *folio = state->folios[i];
		unsigned int order = folio_order(folio);

		folios_info[i] = virt_to_phys(folio_address(folio)) | order;

		err = kho_preserve_folio(folio);
		if (err)
			break;
	}

	err |= fdt_begin_node(fdt, "data");
	err |= fdt_property(fdt, "nr_folios", &state->nr_folios,
			    sizeof(state->nr_folios));
	err |= fdt_property(fdt, "folios_info", &folios_info_phys,
			    sizeof(folios_info_phys));
	err |= fdt_property(fdt, "csum", &state->csum, sizeof(state->csum));
	err |= fdt_end_node(fdt);

	if (!err)
		state->folios_info = no_free_ptr(folios_info);

	return err;
}

static int kho_test_prepare_fdt(struct kho_test_state *state)
{
	const char compatible[] = KHO_TEST_COMPAT;
	unsigned int magic = KHO_TEST_MAGIC;
	ssize_t fdt_size;
	int err = 0;
	void *fdt;

	fdt_size = state->nr_folios * sizeof(phys_addr_t) + PAGE_SIZE;
	state->fdt = folio_alloc(GFP_KERNEL, get_order(fdt_size));
	if (!state->fdt)
		return -ENOMEM;

	fdt = folio_address(state->fdt);

	err |= fdt_create(fdt, fdt_size);
	err |= fdt_finish_reservemap(fdt);

	err |= fdt_begin_node(fdt, "");
	err |= fdt_property(fdt, "compatible", compatible, sizeof(compatible));
	err |= fdt_property(fdt, "magic", &magic, sizeof(magic));
	err |= kho_test_save_data(state, fdt);
	err |= fdt_end_node(fdt);

	err |= fdt_finish(fdt);

	if (err)
		folio_put(state->fdt);

	return err;
}

static int kho_test_generate_data(struct kho_test_state *state)
{
	size_t alloc_size = 0;
	__wsum csum = 0;

	while (alloc_size < max_mem) {
		int order = get_random_u32() % NR_PAGE_ORDERS;
		struct folio *folio;
		unsigned int size;
		void *addr;

		/*
		 * Since get_order() rounds up, make sure that actual
		 * allocation is smaller so that we won't exceed max_mem
		 */
		if (alloc_size + (PAGE_SIZE << order) > max_mem) {
			order = get_order(max_mem - alloc_size);
			if (order)
				order--;
		}
		size = PAGE_SIZE << order;

		folio = folio_alloc(GFP_KERNEL | __GFP_NORETRY, order);
		if (!folio)
			goto err_free_folios;

		state->folios[state->nr_folios++] = folio;
		addr = folio_address(folio);
		get_random_bytes(addr, size);
		csum = csum_partial(addr, size, csum);
		alloc_size += size;
	}

	state->csum = csum;
	return 0;

err_free_folios:
	for (int i = 0; i < state->nr_folios; i++)
		folio_put(state->folios[i]);
	state->nr_folios = 0;
	return -ENOMEM;
}

static int kho_test_save(void)
{
	struct kho_test_state *state = &kho_test_state;
	struct folio **folios;
	unsigned long max_nr;
	int err;

	max_mem = PAGE_ALIGN(max_mem);
	max_nr = max_mem >> PAGE_SHIFT;

	folios = kvmalloc_array(max_nr, sizeof(*state->folios), GFP_KERNEL);
	if (!folios)
		return -ENOMEM;
	state->folios = folios;

	err = kho_test_generate_data(state);
	if (err)
		goto err_free_folios;

	err = kho_test_prepare_fdt(state);
	if (err)
		goto err_free_folios;

	err = register_kho_notifier(&kho_test_nb);
	if (err)
		goto err_free_fdt;

	return 0;

err_free_fdt:
	folio_put(state->fdt);
err_free_folios:
	kvfree(folios);
	return err;
}

static int kho_test_restore_data(const void *fdt, int node)
{
	const struct kho_vmalloc *folios_info_phys;
	const unsigned int *nr_folios;
	phys_addr_t *folios_info;
	const __wsum *old_csum;
	__wsum csum = 0;
	int len;

	node = fdt_path_offset(fdt, "/data");

	nr_folios = fdt_getprop(fdt, node, "nr_folios", &len);
	if (!nr_folios || len != sizeof(*nr_folios))
		return -EINVAL;

	old_csum = fdt_getprop(fdt, node, "csum", &len);
	if (!old_csum || len != sizeof(*old_csum))
		return -EINVAL;

	folios_info_phys = fdt_getprop(fdt, node, "folios_info", &len);
	if (!folios_info_phys || len != sizeof(*folios_info_phys))
		return -EINVAL;

	folios_info = kho_restore_vmalloc(folios_info_phys);
	if (!folios_info)
		return -EINVAL;

	for (int i = 0; i < *nr_folios; i++) {
		unsigned int order = folios_info[i] & ~PAGE_MASK;
		phys_addr_t phys = folios_info[i] & PAGE_MASK;
		unsigned int size = PAGE_SIZE << order;
		struct folio *folio;

		folio = kho_restore_folio(phys);
		if (!folio)
			break;

		if (folio_order(folio) != order)
			break;

		csum = csum_partial(folio_address(folio), size, csum);
		folio_put(folio);
	}

	vfree(folios_info);

	if (csum != *old_csum)
		return -EINVAL;

	return 0;
}

static int kho_test_restore(phys_addr_t fdt_phys)
{
	void *fdt = phys_to_virt(fdt_phys);
	const unsigned int *magic;
	int node, len, err;

	node = fdt_path_offset(fdt, "/");
	if (node < 0)
		return -EINVAL;

	if (fdt_node_check_compatible(fdt, node, KHO_TEST_COMPAT))
		return -EINVAL;

	magic = fdt_getprop(fdt, node, "magic", &len);
	if (!magic || len != sizeof(*magic))
		return -EINVAL;

	if (*magic != KHO_TEST_MAGIC)
		return -EINVAL;

	err = kho_test_restore_data(fdt, node);
	if (err)
		return err;

	pr_info("KHO restore succeeded\n");
	return 0;
}

static int __init kho_test_init(void)
{
	phys_addr_t fdt_phys;
	int err;

	if (!kho_is_enabled())
		return 0;

	err = kho_retrieve_subtree(KHO_TEST_FDT, &fdt_phys);
	if (!err)
		return kho_test_restore(fdt_phys);

	if (err != -ENOENT) {
		pr_warn("failed to retrieve %s FDT: %d\n", KHO_TEST_FDT, err);
		return err;
	}

	return kho_test_save();
}
module_init(kho_test_init);

static void kho_test_cleanup(void)
{
	for (int i = 0; i < kho_test_state.nr_folios; i++)
		folio_put(kho_test_state.folios[i]);

	kvfree(kho_test_state.folios);
	vfree(kho_test_state.folios_info);
	folio_put(kho_test_state.fdt);
}

static void __exit kho_test_exit(void)
{
	unregister_kho_notifier(&kho_test_nb);
	kho_test_cleanup();
}
module_exit(kho_test_exit);

MODULE_AUTHOR("Mike Rapoport <rppt@kernel.org>");
MODULE_DESCRIPTION("KHO test module");
MODULE_LICENSE("GPL");
