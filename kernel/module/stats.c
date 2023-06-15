// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Debugging module statistics.
 *
 * Copyright (C) 2023 Luis Chamberlain <mcgrof@kernel.org>
 */

#include <linux/module.h>
#include <uapi/linux/module.h>
#include <linux/string.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/debugfs.h>
#include <linux/rculist.h>
#include <linux/math.h>

#include "internal.h"

/**
 * DOC: module debugging statistics overview
 *
 * Enabling CONFIG_MODULE_STATS enables module debugging statistics which
 * are useful to monitor and root cause memory pressure issues with module
 * loading. These statistics are useful to allow us to improve production
 * workloads.
 *
 * The current module debugging statistics supported help keep track of module
 * loading failures to enable improvements either for kernel module auto-loading
 * usage (request_module()) or interactions with userspace. Statistics are
 * provided to track all possible failures in the finit_module() path and memory
 * wasted in this process space.  Each of the failure counters are associated
 * to a type of module loading failure which is known to incur a certain amount
 * of memory allocation loss. In the worst case loading a module will fail after
 * a 3 step memory allocation process:
 *
 *   a) memory allocated with kernel_read_file_from_fd()
 *   b) module decompression processes the file read from
 *      kernel_read_file_from_fd(), and vmap() is used to map
 *      the decompressed module to a new local buffer which represents
 *      a copy of the decompressed module passed from userspace. The buffer
 *      from kernel_read_file_from_fd() is freed right away.
 *   c) layout_and_allocate() allocates space for the final resting
 *      place where we would keep the module if it were to be processed
 *      successfully.
 *
 * If a failure occurs after these three different allocations only one
 * counter will be incremented with the summation of the allocated bytes freed
 * incurred during this failure. Likewise, if module loading failed only after
 * step b) a separate counter is used and incremented for the bytes freed and
 * not used during both of those allocations.
 *
 * Virtual memory space can be limited, for example on x86 virtual memory size
 * defaults to 128 MiB. We should strive to limit and avoid wasting virtual
 * memory allocations when possible. These module debugging statistics help
 * to evaluate how much memory is being wasted on bootup due to module loading
 * failures.
 *
 * All counters are designed to be incremental. Atomic counters are used so to
 * remain simple and avoid delays and deadlocks.
 */

/**
 * DOC: dup_failed_modules - tracks duplicate failed modules
 *
 * Linked list of modules which failed to be loaded because an already existing
 * module with the same name was already being processed or already loaded.
 * The finit_module() system call incurs heavy virtual memory allocations. In
 * the worst case an finit_module() system call can end up allocating virtual
 * memory 3 times:
 *
 *   1) kernel_read_file_from_fd() call uses vmalloc()
 *   2) optional module decompression uses vmap()
 *   3) layout_and allocate() can use vzalloc() or an arch specific variation of
 *      vmalloc to deal with ELF sections requiring special permissions
 *
 * In practice on a typical boot today most finit_module() calls fail due to
 * the module with the same name already being loaded or about to be processed.
 * All virtual memory allocated to these failed modules will be freed with
 * no functional use.
 *
 * To help with this the dup_failed_modules allows us to track modules which
 * failed to load due to the fact that a module was already loaded or being
 * processed.  There are only two points at which we can fail such calls,
 * we list them below along with the number of virtual memory allocation
 * calls:
 *
 *   a) FAIL_DUP_MOD_BECOMING: at the end of early_mod_check() before
 *	layout_and_allocate().
 *	- with module decompression: 2 virtual memory allocation calls
 *	- without module decompression: 1 virtual memory allocation calls
 *   b) FAIL_DUP_MOD_LOAD: after layout_and_allocate() on add_unformed_module()
 *   	- with module decompression 3 virtual memory allocation calls
 *   	- without module decompression 2 virtual memory allocation calls
 *
 * We should strive to get this list to be as small as possible. If this list
 * is not empty it is a reflection of possible work or optimizations possible
 * either in-kernel or in userspace.
 */
static LIST_HEAD(dup_failed_modules);

/**
 * DOC: module statistics debugfs counters
 *
 * The total amount of wasted virtual memory allocation space during module
 * loading can be computed by adding the total from the summation:
 *
 *   * @invalid_kread_bytes +
 *     @invalid_decompress_bytes +
 *     @invalid_becoming_bytes +
 *     @invalid_mod_bytes
 *
 * The following debugfs counters are available to inspect module loading
 * failures:
 *
 *   * total_mod_size: total bytes ever used by all modules we've dealt with on
 *     this system
 *   * total_text_size: total bytes of the .text and .init.text ELF section
 *     sizes we've dealt with on this system
 *   * invalid_kread_bytes: bytes allocated and then freed on failures which
 *     happen due to the initial kernel_read_file_from_fd(). kernel_read_file_from_fd()
 *     uses vmalloc(). These should typically not happen unless your system is
 *     under memory pressure.
 *   * invalid_decompress_bytes: number of bytes allocated and freed due to
 *     memory allocations in the module decompression path that use vmap().
 *     These typically should not happen unless your system is under memory
 *     pressure.
 *   * invalid_becoming_bytes: total number of bytes allocated and freed used
 *     used to read the kernel module userspace wants us to read before we
 *     promote it to be processed to be added to our @modules linked list. These
 *     failures can happen if we had a check in between a successful kernel_read_file_from_fd()
 *     call and right before we allocate the our private memory for the module
 *     which would be kept if the module is successfully loaded. The most common
 *     reason for this failure is when userspace is racing to load a module
 *     which it does not yet see loaded. The first module to succeed in
 *     add_unformed_module() will add a module to our &modules list and
 *     subsequent loads of modules with the same name will error out at the
 *     end of early_mod_check(). The check for module_patient_check_exists()
 *     at the end of early_mod_check() prevents duplicate allocations
 *     on layout_and_allocate() for modules already being processed. These
 *     duplicate failed modules are non-fatal, however they typically are
 *     indicative of userspace not seeing a module in userspace loaded yet and
 *     unnecessarily trying to load a module before the kernel even has a chance
 *     to begin to process prior requests. Although duplicate failures can be
 *     non-fatal, we should try to reduce vmalloc() pressure proactively, so
 *     ideally after boot this will be close to as 0 as possible.  If module
 *     decompression was used we also add to this counter the cost of the
 *     initial kernel_read_file_from_fd() of the compressed module. If module
 *     decompression was not used the value represents the total allocated and
 *     freed bytes in kernel_read_file_from_fd() calls for these type of
 *     failures. These failures can occur because:
 *
 *    * module_sig_check() - module signature checks
 *    * elf_validity_cache_copy() - some ELF validation issue
 *    * early_mod_check():
 *
 *      * blacklisting
 *      * failed to rewrite section headers
 *      * version magic
 *      * live patch requirements didn't check out
 *      * the module was detected as being already present
 *
 *   * invalid_mod_bytes: these are the total number of bytes allocated and
 *     freed due to failures after we did all the sanity checks of the module
 *     which userspace passed to us and after our first check that the module
 *     is unique.  A module can still fail to load if we detect the module is
 *     loaded after we allocate space for it with layout_and_allocate(), we do
 *     this check right before processing the module as live and run its
 *     initialization routines. Note that you have a failure of this type it
 *     also means the respective kernel_read_file_from_fd() memory space was
 *     also freed and not used, and so we increment this counter with twice
 *     the size of the module. Additionally if you used module decompression
 *     the size of the compressed module is also added to this counter.
 *
 *  * modcount: how many modules we've loaded in our kernel life time
 *  * failed_kreads: how many modules failed due to failed kernel_read_file_from_fd()
 *  * failed_decompress: how many failed module decompression attempts we've had.
 *    These really should not happen unless your compression / decompression
 *    might be broken.
 *  * failed_becoming: how many modules failed after we kernel_read_file_from_fd()
 *    it and before we allocate memory for it with layout_and_allocate(). This
 *    counter is never incremented if you manage to validate the module and
 *    call layout_and_allocate() for it.
 *  * failed_load_modules: how many modules failed once we've allocated our
 *    private space for our module using layout_and_allocate(). These failures
 *    should hopefully mostly be dealt with already. Races in theory could
 *    still exist here, but it would just mean the kernel had started processing
 *    two threads concurrently up to early_mod_check() and one thread won.
 *    These failures are good signs the kernel or userspace is doing something
 *    seriously stupid or that could be improved. We should strive to fix these,
 *    but it is perhaps not easy to fix them. A recent example are the modules
 *    requests incurred for frequency modules, a separate module request was
 *    being issued for each CPU on a system.
 */

atomic_long_t total_mod_size;
atomic_long_t total_text_size;
atomic_long_t invalid_kread_bytes;
atomic_long_t invalid_decompress_bytes;
static atomic_long_t invalid_becoming_bytes;
static atomic_long_t invalid_mod_bytes;
atomic_t modcount;
atomic_t failed_kreads;
atomic_t failed_decompress;
static atomic_t failed_becoming;
static atomic_t failed_load_modules;

static const char *mod_fail_to_str(struct mod_fail_load *mod_fail)
{
	if (test_bit(FAIL_DUP_MOD_BECOMING, &mod_fail->dup_fail_mask) &&
	    test_bit(FAIL_DUP_MOD_LOAD, &mod_fail->dup_fail_mask))
		return "Becoming & Load";
	if (test_bit(FAIL_DUP_MOD_BECOMING, &mod_fail->dup_fail_mask))
		return "Becoming";
	if (test_bit(FAIL_DUP_MOD_LOAD, &mod_fail->dup_fail_mask))
		return "Load";
	return "Bug-on-stats";
}

void mod_stat_bump_invalid(struct load_info *info, int flags)
{
	atomic_long_add(info->len * 2, &invalid_mod_bytes);
	atomic_inc(&failed_load_modules);
#if defined(CONFIG_MODULE_DECOMPRESS)
	if (flags & MODULE_INIT_COMPRESSED_FILE)
		atomic_long_add(info->compressed_len, &invalid_mod_bytes);
#endif
}

void mod_stat_bump_becoming(struct load_info *info, int flags)
{
	atomic_inc(&failed_becoming);
	atomic_long_add(info->len, &invalid_becoming_bytes);
#if defined(CONFIG_MODULE_DECOMPRESS)
	if (flags & MODULE_INIT_COMPRESSED_FILE)
		atomic_long_add(info->compressed_len, &invalid_becoming_bytes);
#endif
}

int try_add_failed_module(const char *name, enum fail_dup_mod_reason reason)
{
	struct mod_fail_load *mod_fail;

	list_for_each_entry_rcu(mod_fail, &dup_failed_modules, list,
				lockdep_is_held(&module_mutex)) {
		if (!strcmp(mod_fail->name, name)) {
			atomic_long_inc(&mod_fail->count);
			__set_bit(reason, &mod_fail->dup_fail_mask);
			goto out;
		}
	}

	mod_fail = kzalloc(sizeof(*mod_fail), GFP_KERNEL);
	if (!mod_fail)
		return -ENOMEM;
	memcpy(mod_fail->name, name, strlen(name));
	__set_bit(reason, &mod_fail->dup_fail_mask);
	atomic_long_inc(&mod_fail->count);
	list_add_rcu(&mod_fail->list, &dup_failed_modules);
out:
	return 0;
}

/*
 * At 64 bytes per module and assuming a 1024 bytes preamble we can fit the
 * 112 module prints within 8k.
 *
 * 1024 + (64*112) = 8k
 */
#define MAX_PREAMBLE 1024
#define MAX_FAILED_MOD_PRINT 112
#define MAX_BYTES_PER_MOD 64
static ssize_t read_file_mod_stats(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct mod_fail_load *mod_fail;
	unsigned int len, size, count_failed = 0;
	char *buf;
	u32 live_mod_count, fkreads, fdecompress, fbecoming, floads;
	unsigned long total_size, text_size, ikread_bytes, ibecoming_bytes,
		idecompress_bytes, imod_bytes, total_virtual_lost;

	live_mod_count = atomic_read(&modcount);
	fkreads = atomic_read(&failed_kreads);
	fdecompress = atomic_read(&failed_decompress);
	fbecoming = atomic_read(&failed_becoming);
	floads = atomic_read(&failed_load_modules);

	total_size = atomic_long_read(&total_mod_size);
	text_size = atomic_long_read(&total_text_size);
	ikread_bytes = atomic_long_read(&invalid_kread_bytes);
	idecompress_bytes = atomic_long_read(&invalid_decompress_bytes);
	ibecoming_bytes = atomic_long_read(&invalid_becoming_bytes);
	imod_bytes = atomic_long_read(&invalid_mod_bytes);

	total_virtual_lost = ikread_bytes + idecompress_bytes + ibecoming_bytes + imod_bytes;

	size = MAX_PREAMBLE + min((unsigned int)(floads + fbecoming),
				  (unsigned int)MAX_FAILED_MOD_PRINT) * MAX_BYTES_PER_MOD;
	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	/* The beginning of our debug preamble */
	len = scnprintf(buf, size, "%25s\t%u\n", "Mods ever loaded", live_mod_count);

	len += scnprintf(buf + len, size - len, "%25s\t%u\n", "Mods failed on kread", fkreads);

	len += scnprintf(buf + len, size - len, "%25s\t%u\n", "Mods failed on decompress",
			 fdecompress);
	len += scnprintf(buf + len, size - len, "%25s\t%u\n", "Mods failed on becoming", fbecoming);

	len += scnprintf(buf + len, size - len, "%25s\t%u\n", "Mods failed on load", floads);

	len += scnprintf(buf + len, size - len, "%25s\t%lu\n", "Total module size", total_size);
	len += scnprintf(buf + len, size - len, "%25s\t%lu\n", "Total mod text size", text_size);

	len += scnprintf(buf + len, size - len, "%25s\t%lu\n", "Failed kread bytes", ikread_bytes);

	len += scnprintf(buf + len, size - len, "%25s\t%lu\n", "Failed decompress bytes",
			 idecompress_bytes);

	len += scnprintf(buf + len, size - len, "%25s\t%lu\n", "Failed becoming bytes", ibecoming_bytes);

	len += scnprintf(buf + len, size - len, "%25s\t%lu\n", "Failed kmod bytes", imod_bytes);

	len += scnprintf(buf + len, size - len, "%25s\t%lu\n", "Virtual mem wasted bytes", total_virtual_lost);

	if (live_mod_count && total_size) {
		len += scnprintf(buf + len, size - len, "%25s\t%lu\n", "Average mod size",
				 DIV_ROUND_UP(total_size, live_mod_count));
	}

	if (live_mod_count && text_size) {
		len += scnprintf(buf + len, size - len, "%25s\t%lu\n", "Average mod text size",
				 DIV_ROUND_UP(text_size, live_mod_count));
	}

	/*
	 * We use WARN_ON_ONCE() for the counters to ensure we always have parity
	 * for keeping tabs on a type of failure with one type of byte counter.
	 * The counters for imod_bytes does not increase for fkreads failures
	 * for example, and so on.
	 */

	WARN_ON_ONCE(ikread_bytes && !fkreads);
	if (fkreads && ikread_bytes) {
		len += scnprintf(buf + len, size - len, "%25s\t%lu\n", "Avg fail kread bytes",
				 DIV_ROUND_UP(ikread_bytes, fkreads));
	}

	WARN_ON_ONCE(ibecoming_bytes && !fbecoming);
	if (fbecoming && ibecoming_bytes) {
		len += scnprintf(buf + len, size - len, "%25s\t%lu\n", "Avg fail becoming bytes",
				 DIV_ROUND_UP(ibecoming_bytes, fbecoming));
	}

	WARN_ON_ONCE(idecompress_bytes && !fdecompress);
	if (fdecompress && idecompress_bytes) {
		len += scnprintf(buf + len, size - len, "%25s\t%lu\n", "Avg fail decomp bytes",
				 DIV_ROUND_UP(idecompress_bytes, fdecompress));
	}

	WARN_ON_ONCE(imod_bytes && !floads);
	if (floads && imod_bytes) {
		len += scnprintf(buf + len, size - len, "%25s\t%lu\n", "Average fail load bytes",
				 DIV_ROUND_UP(imod_bytes, floads));
	}

	/* End of our debug preamble header. */

	/* Catch when we've gone beyond our expected preamble */
	WARN_ON_ONCE(len >= MAX_PREAMBLE);

	if (list_empty(&dup_failed_modules))
		goto out;

	len += scnprintf(buf + len, size - len, "Duplicate failed modules:\n");
	len += scnprintf(buf + len, size - len, "%25s\t%15s\t%25s\n",
			 "Module-name", "How-many-times", "Reason");
	mutex_lock(&module_mutex);


	list_for_each_entry_rcu(mod_fail, &dup_failed_modules, list) {
		if (WARN_ON_ONCE(++count_failed >= MAX_FAILED_MOD_PRINT))
			goto out_unlock;
		len += scnprintf(buf + len, size - len, "%25s\t%15lu\t%25s\n", mod_fail->name,
				 atomic_long_read(&mod_fail->count), mod_fail_to_str(mod_fail));
	}
out_unlock:
	mutex_unlock(&module_mutex);
out:
	kfree(buf);
        return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}
#undef MAX_PREAMBLE
#undef MAX_FAILED_MOD_PRINT
#undef MAX_BYTES_PER_MOD

static const struct file_operations fops_mod_stats = {
	.read = read_file_mod_stats,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

#define mod_debug_add_ulong(name) debugfs_create_ulong(#name, 0400, mod_debugfs_root, (unsigned long *) &name.counter)
#define mod_debug_add_atomic(name) debugfs_create_atomic_t(#name, 0400, mod_debugfs_root, &name)
static int __init module_stats_init(void)
{
	mod_debug_add_ulong(total_mod_size);
	mod_debug_add_ulong(total_text_size);
	mod_debug_add_ulong(invalid_kread_bytes);
	mod_debug_add_ulong(invalid_decompress_bytes);
	mod_debug_add_ulong(invalid_becoming_bytes);
	mod_debug_add_ulong(invalid_mod_bytes);

	mod_debug_add_atomic(modcount);
	mod_debug_add_atomic(failed_kreads);
	mod_debug_add_atomic(failed_decompress);
	mod_debug_add_atomic(failed_becoming);
	mod_debug_add_atomic(failed_load_modules);

	debugfs_create_file("stats", 0400, mod_debugfs_root, mod_debugfs_root, &fops_mod_stats);

	return 0;
}
#undef mod_debug_add_ulong
#undef mod_debug_add_atomic
module_init(module_stats_init);
