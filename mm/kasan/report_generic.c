// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains generic KASAN specific error reporting code.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <ryabinin.a.a@gmail.com>
 *
 * Some code borrowed from https://github.com/xairy/kasan-prototype by
 *        Andrey Konovalov <andreyknvl@gmail.com>
 */

#include <linux/bitops.h>
#include <linux/ftrace.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/slab.h>
#include <linux/stackdepot.h>
#include <linux/stacktrace.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/kasan.h>
#include <linux/module.h>

#include <asm/sections.h>

#include "kasan.h"
#include "../slab.h"

void *kasan_find_first_bad_addr(void *addr, size_t size)
{
	void *p = addr;

	if (!addr_has_metadata(p))
		return p;

	while (p < addr + size && !(*(u8 *)kasan_mem_to_shadow(p)))
		p += KASAN_GRANULE_SIZE;

	return p;
}

static const char *get_shadow_bug_type(struct kasan_report_info *info)
{
	const char *bug_type = "unknown-crash";
	u8 *shadow_addr;

	shadow_addr = (u8 *)kasan_mem_to_shadow(info->first_bad_addr);

	/*
	 * If shadow byte value is in [0, KASAN_GRANULE_SIZE) we can look
	 * at the next shadow byte to determine the type of the bad access.
	 */
	if (*shadow_addr > 0 && *shadow_addr <= KASAN_GRANULE_SIZE - 1)
		shadow_addr++;

	switch (*shadow_addr) {
	case 0 ... KASAN_GRANULE_SIZE - 1:
		/*
		 * In theory it's still possible to see these shadow values
		 * due to a data race in the kernel code.
		 */
		bug_type = "out-of-bounds";
		break;
	case KASAN_PAGE_REDZONE:
	case KASAN_KMALLOC_REDZONE:
		bug_type = "slab-out-of-bounds";
		break;
	case KASAN_GLOBAL_REDZONE:
		bug_type = "global-out-of-bounds";
		break;
	case KASAN_STACK_LEFT:
	case KASAN_STACK_MID:
	case KASAN_STACK_RIGHT:
	case KASAN_STACK_PARTIAL:
		bug_type = "stack-out-of-bounds";
		break;
	case KASAN_FREE_PAGE:
	case KASAN_KMALLOC_FREE:
	case KASAN_KMALLOC_FREETRACK:
		bug_type = "use-after-free";
		break;
	case KASAN_ALLOCA_LEFT:
	case KASAN_ALLOCA_RIGHT:
		bug_type = "alloca-out-of-bounds";
		break;
	case KASAN_VMALLOC_INVALID:
		bug_type = "vmalloc-out-of-bounds";
		break;
	}

	return bug_type;
}

static const char *get_wild_bug_type(struct kasan_report_info *info)
{
	const char *bug_type = "unknown-crash";

	if ((unsigned long)info->access_addr < PAGE_SIZE)
		bug_type = "null-ptr-deref";
	else if ((unsigned long)info->access_addr < TASK_SIZE)
		bug_type = "user-memory-access";
	else
		bug_type = "wild-memory-access";

	return bug_type;
}

const char *kasan_get_bug_type(struct kasan_report_info *info)
{
	/*
	 * If access_size is a negative number, then it has reason to be
	 * defined as out-of-bounds bug type.
	 *
	 * Casting negative numbers to size_t would indeed turn up as
	 * a large size_t and its value will be larger than ULONG_MAX/2,
	 * so that this can qualify as out-of-bounds.
	 */
	if (info->access_addr + info->access_size < info->access_addr)
		return "out-of-bounds";

	if (addr_has_metadata(info->access_addr))
		return get_shadow_bug_type(info);
	return get_wild_bug_type(info);
}

void kasan_metadata_fetch_row(char *buffer, void *row)
{
	memcpy(buffer, kasan_mem_to_shadow(row), META_BYTES_PER_ROW);
}

#ifdef CONFIG_KASAN_STACK
static bool __must_check tokenize_frame_descr(const char **frame_descr,
					      char *token, size_t max_tok_len,
					      unsigned long *value)
{
	const char *sep = strchr(*frame_descr, ' ');

	if (sep == NULL)
		sep = *frame_descr + strlen(*frame_descr);

	if (token != NULL) {
		const size_t tok_len = sep - *frame_descr;

		if (tok_len + 1 > max_tok_len) {
			pr_err("KASAN internal error: frame description too long: %s\n",
			       *frame_descr);
			return false;
		}

		/* Copy token (+ 1 byte for '\0'). */
		strscpy(token, *frame_descr, tok_len + 1);
	}

	/* Advance frame_descr past separator. */
	*frame_descr = sep + 1;

	if (value != NULL && kstrtoul(token, 10, value)) {
		pr_err("KASAN internal error: not a valid number: %s\n", token);
		return false;
	}

	return true;
}

static void print_decoded_frame_descr(const char *frame_descr)
{
	/*
	 * We need to parse the following string:
	 *    "n alloc_1 alloc_2 ... alloc_n"
	 * where alloc_i looks like
	 *    "offset size len name"
	 * or "offset size len name:line".
	 */

	char token[64];
	unsigned long num_objects;

	if (!tokenize_frame_descr(&frame_descr, token, sizeof(token),
				  &num_objects))
		return;

	pr_err("\n");
	pr_err("This frame has %lu %s:\n", num_objects,
	       num_objects == 1 ? "object" : "objects");

	while (num_objects--) {
		unsigned long offset;
		unsigned long size;

		/* access offset */
		if (!tokenize_frame_descr(&frame_descr, token, sizeof(token),
					  &offset))
			return;
		/* access size */
		if (!tokenize_frame_descr(&frame_descr, token, sizeof(token),
					  &size))
			return;
		/* name length (unused) */
		if (!tokenize_frame_descr(&frame_descr, NULL, 0, NULL))
			return;
		/* object name */
		if (!tokenize_frame_descr(&frame_descr, token, sizeof(token),
					  NULL))
			return;

		/* Strip line number; without filename it's not very helpful. */
		strreplace(token, ':', '\0');

		/* Finally, print object information. */
		pr_err(" [%lu, %lu) '%s'", offset, offset + size, token);
	}
}

/* Returns true only if the address is on the current task's stack. */
static bool __must_check get_address_stack_frame_info(const void *addr,
						      unsigned long *offset,
						      const char **frame_descr,
						      const void **frame_pc)
{
	unsigned long aligned_addr;
	unsigned long mem_ptr;
	const u8 *shadow_bottom;
	const u8 *shadow_ptr;
	const unsigned long *frame;

	BUILD_BUG_ON(IS_ENABLED(CONFIG_STACK_GROWSUP));

	aligned_addr = round_down((unsigned long)addr, sizeof(long));
	mem_ptr = round_down(aligned_addr, KASAN_GRANULE_SIZE);
	shadow_ptr = kasan_mem_to_shadow((void *)aligned_addr);
	shadow_bottom = kasan_mem_to_shadow(end_of_stack(current));

	while (shadow_ptr >= shadow_bottom && *shadow_ptr != KASAN_STACK_LEFT) {
		shadow_ptr--;
		mem_ptr -= KASAN_GRANULE_SIZE;
	}

	while (shadow_ptr >= shadow_bottom && *shadow_ptr == KASAN_STACK_LEFT) {
		shadow_ptr--;
		mem_ptr -= KASAN_GRANULE_SIZE;
	}

	if (shadow_ptr < shadow_bottom)
		return false;

	frame = (const unsigned long *)(mem_ptr + KASAN_GRANULE_SIZE);
	if (frame[0] != KASAN_CURRENT_STACK_FRAME_MAGIC) {
		pr_err("KASAN internal error: frame info validation failed; invalid marker: %lu\n",
		       frame[0]);
		return false;
	}

	*offset = (unsigned long)addr - (unsigned long)frame;
	*frame_descr = (const char *)frame[1];
	*frame_pc = (void *)frame[2];

	return true;
}

void kasan_print_address_stack_frame(const void *addr)
{
	unsigned long offset;
	const char *frame_descr;
	const void *frame_pc;

	if (WARN_ON(!object_is_on_stack(addr)))
		return;

	pr_err("The buggy address belongs to stack of task %s/%d\n",
	       current->comm, task_pid_nr(current));

	if (!get_address_stack_frame_info(addr, &offset, &frame_descr,
					  &frame_pc))
		return;

	pr_err(" and is located at offset %lu in frame:\n", offset);
	pr_err(" %pS\n", frame_pc);

	if (!frame_descr)
		return;

	print_decoded_frame_descr(frame_descr);
}
#endif /* CONFIG_KASAN_STACK */

#define DEFINE_ASAN_REPORT_LOAD(size)                     \
void __asan_report_load##size##_noabort(unsigned long addr) \
{                                                         \
	kasan_report(addr, size, false, _RET_IP_);	  \
}                                                         \
EXPORT_SYMBOL(__asan_report_load##size##_noabort)

#define DEFINE_ASAN_REPORT_STORE(size)                     \
void __asan_report_store##size##_noabort(unsigned long addr) \
{                                                          \
	kasan_report(addr, size, true, _RET_IP_);	   \
}                                                          \
EXPORT_SYMBOL(__asan_report_store##size##_noabort)

DEFINE_ASAN_REPORT_LOAD(1);
DEFINE_ASAN_REPORT_LOAD(2);
DEFINE_ASAN_REPORT_LOAD(4);
DEFINE_ASAN_REPORT_LOAD(8);
DEFINE_ASAN_REPORT_LOAD(16);
DEFINE_ASAN_REPORT_STORE(1);
DEFINE_ASAN_REPORT_STORE(2);
DEFINE_ASAN_REPORT_STORE(4);
DEFINE_ASAN_REPORT_STORE(8);
DEFINE_ASAN_REPORT_STORE(16);

void __asan_report_load_n_noabort(unsigned long addr, size_t size)
{
	kasan_report(addr, size, false, _RET_IP_);
}
EXPORT_SYMBOL(__asan_report_load_n_noabort);

void __asan_report_store_n_noabort(unsigned long addr, size_t size)
{
	kasan_report(addr, size, true, _RET_IP_);
}
EXPORT_SYMBOL(__asan_report_store_n_noabort);
