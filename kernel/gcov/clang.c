// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Google, Inc.
 * modified from kernel/gcov/gcc_4_7.c
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * LLVM uses profiling data that's deliberately similar to GCC, but has a
 * very different way of exporting that data.  LLVM calls llvm_gcov_init() once
 * per module, and provides a couple of callbacks that we can use to ask for
 * more data.
 *
 * We care about the "writeout" callback, which in turn calls back into
 * compiler-rt/this module to dump all the gathered coverage data to disk:
 *
 *    llvm_gcda_start_file()
 *      llvm_gcda_emit_function()
 *      llvm_gcda_emit_arcs()
 *      llvm_gcda_emit_function()
 *      llvm_gcda_emit_arcs()
 *      [... repeats for each function ...]
 *    llvm_gcda_summary_info()
 *    llvm_gcda_end_file()
 *
 * This design is much more stateless and unstructured than gcc's, and is
 * intended to run at process exit.  This forces us to keep some local state
 * about which module we're dealing with at the moment.  On the other hand, it
 * also means we don't depend as much on how LLVM represents profiling data
 * internally.
 *
 * See LLVM's lib/Transforms/Instrumentation/GCOVProfiling.cpp for more
 * details on how this works, particularly GCOVProfiler::emitProfileArcs(),
 * GCOVProfiler::insertCounterWriteout(), and
 * GCOVProfiler::insertFlush().
 */

#define pr_fmt(fmt)	"gcov: " fmt

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "gcov.h"

typedef void (*llvm_gcov_callback)(void);

struct gcov_info {
	struct list_head head;

	const char *filename;
	unsigned int version;
	u32 checksum;

	struct list_head functions;
};

struct gcov_fn_info {
	struct list_head head;

	u32 ident;
	u32 checksum;
	u8 use_extra_checksum;
	u32 cfg_checksum;

	u32 num_counters;
	u64 *counters;
	const char *function_name;
};

static struct gcov_info *current_info;

static LIST_HEAD(clang_gcov_list);

void llvm_gcov_init(llvm_gcov_callback writeout, llvm_gcov_callback flush)
{
	struct gcov_info *info = kzalloc(sizeof(*info), GFP_KERNEL);

	if (!info)
		return;

	INIT_LIST_HEAD(&info->head);
	INIT_LIST_HEAD(&info->functions);

	mutex_lock(&gcov_lock);

	list_add_tail(&info->head, &clang_gcov_list);
	current_info = info;
	writeout();
	current_info = NULL;
	if (gcov_events_enabled)
		gcov_event(GCOV_ADD, info);

	mutex_unlock(&gcov_lock);
}
EXPORT_SYMBOL(llvm_gcov_init);

void llvm_gcda_start_file(const char *orig_filename, const char version[4],
		u32 checksum)
{
	current_info->filename = orig_filename;
	memcpy(&current_info->version, version, sizeof(current_info->version));
	current_info->checksum = checksum;
}
EXPORT_SYMBOL(llvm_gcda_start_file);

void llvm_gcda_emit_function(u32 ident, const char *function_name,
		u32 func_checksum, u8 use_extra_checksum, u32 cfg_checksum)
{
	struct gcov_fn_info *info = kzalloc(sizeof(*info), GFP_KERNEL);

	if (!info)
		return;

	INIT_LIST_HEAD(&info->head);
	info->ident = ident;
	info->checksum = func_checksum;
	info->use_extra_checksum = use_extra_checksum;
	info->cfg_checksum = cfg_checksum;
	if (function_name)
		info->function_name = kstrdup(function_name, GFP_KERNEL);

	list_add_tail(&info->head, &current_info->functions);
}
EXPORT_SYMBOL(llvm_gcda_emit_function);

void llvm_gcda_emit_arcs(u32 num_counters, u64 *counters)
{
	struct gcov_fn_info *info = list_last_entry(&current_info->functions,
			struct gcov_fn_info, head);

	info->num_counters = num_counters;
	info->counters = counters;
}
EXPORT_SYMBOL(llvm_gcda_emit_arcs);

void llvm_gcda_summary_info(void)
{
}
EXPORT_SYMBOL(llvm_gcda_summary_info);

void llvm_gcda_end_file(void)
{
}
EXPORT_SYMBOL(llvm_gcda_end_file);

/**
 * gcov_info_filename - return info filename
 * @info: profiling data set
 */
const char *gcov_info_filename(struct gcov_info *info)
{
	return info->filename;
}

/**
 * gcov_info_version - return info version
 * @info: profiling data set
 */
unsigned int gcov_info_version(struct gcov_info *info)
{
	return info->version;
}

/**
 * gcov_info_next - return next profiling data set
 * @info: profiling data set
 *
 * Returns next gcov_info following @info or first gcov_info in the chain if
 * @info is %NULL.
 */
struct gcov_info *gcov_info_next(struct gcov_info *info)
{
	if (!info)
		return list_first_entry_or_null(&clang_gcov_list,
				struct gcov_info, head);
	if (list_is_last(&info->head, &clang_gcov_list))
		return NULL;
	return list_next_entry(info, head);
}

/**
 * gcov_info_link - link/add profiling data set to the list
 * @info: profiling data set
 */
void gcov_info_link(struct gcov_info *info)
{
	list_add_tail(&info->head, &clang_gcov_list);
}

/**
 * gcov_info_unlink - unlink/remove profiling data set from the list
 * @prev: previous profiling data set
 * @info: profiling data set
 */
void gcov_info_unlink(struct gcov_info *prev, struct gcov_info *info)
{
	/* Generic code unlinks while iterating. */
	__list_del_entry(&info->head);
}

/**
 * gcov_info_within_module - check if a profiling data set belongs to a module
 * @info: profiling data set
 * @mod: module
 *
 * Returns true if profiling data belongs module, false otherwise.
 */
bool gcov_info_within_module(struct gcov_info *info, struct module *mod)
{
	return within_module((unsigned long)info->filename, mod);
}

/* Symbolic links to be created for each profiling data file. */
const struct gcov_link gcov_link[] = {
	{ OBJ_TREE, "gcno" },	/* Link to .gcno file in $(objtree). */
	{ 0, NULL},
};

/**
 * gcov_info_reset - reset profiling data to zero
 * @info: profiling data set
 */
void gcov_info_reset(struct gcov_info *info)
{
	struct gcov_fn_info *fn;

	list_for_each_entry(fn, &info->functions, head)
		memset(fn->counters, 0,
				sizeof(fn->counters[0]) * fn->num_counters);
}

/**
 * gcov_info_is_compatible - check if profiling data can be added
 * @info1: first profiling data set
 * @info2: second profiling data set
 *
 * Returns non-zero if profiling data can be added, zero otherwise.
 */
int gcov_info_is_compatible(struct gcov_info *info1, struct gcov_info *info2)
{
	struct gcov_fn_info *fn_ptr1 = list_first_entry_or_null(
			&info1->functions, struct gcov_fn_info, head);
	struct gcov_fn_info *fn_ptr2 = list_first_entry_or_null(
			&info2->functions, struct gcov_fn_info, head);

	if (info1->checksum != info2->checksum)
		return false;
	if (!fn_ptr1)
		return fn_ptr1 == fn_ptr2;
	while (!list_is_last(&fn_ptr1->head, &info1->functions) &&
		!list_is_last(&fn_ptr2->head, &info2->functions)) {
		if (fn_ptr1->checksum != fn_ptr2->checksum)
			return false;
		if (fn_ptr1->use_extra_checksum != fn_ptr2->use_extra_checksum)
			return false;
		if (fn_ptr1->use_extra_checksum &&
			fn_ptr1->cfg_checksum != fn_ptr2->cfg_checksum)
			return false;
		fn_ptr1 = list_next_entry(fn_ptr1, head);
		fn_ptr2 = list_next_entry(fn_ptr2, head);
	}
	return list_is_last(&fn_ptr1->head, &info1->functions) &&
		list_is_last(&fn_ptr2->head, &info2->functions);
}

/**
 * gcov_info_add - add up profiling data
 * @dest: profiling data set to which data is added
 * @source: profiling data set which is added
 *
 * Adds profiling counts of @source to @dest.
 */
void gcov_info_add(struct gcov_info *dst, struct gcov_info *src)
{
	struct gcov_fn_info *dfn_ptr;
	struct gcov_fn_info *sfn_ptr = list_first_entry_or_null(&src->functions,
			struct gcov_fn_info, head);

	list_for_each_entry(dfn_ptr, &dst->functions, head) {
		u32 i;

		for (i = 0; i < sfn_ptr->num_counters; i++)
			dfn_ptr->counters[i] += sfn_ptr->counters[i];
	}
}

static struct gcov_fn_info *gcov_fn_info_dup(struct gcov_fn_info *fn)
{
	size_t cv_size; /* counter values size */
	struct gcov_fn_info *fn_dup = kmemdup(fn, sizeof(*fn),
			GFP_KERNEL);
	if (!fn_dup)
		return NULL;
	INIT_LIST_HEAD(&fn_dup->head);

	fn_dup->function_name = kstrdup(fn->function_name, GFP_KERNEL);
	if (!fn_dup->function_name)
		goto err_name;

	cv_size = fn->num_counters * sizeof(fn->counters[0]);
	fn_dup->counters = vmalloc(cv_size);
	if (!fn_dup->counters)
		goto err_counters;
	memcpy(fn_dup->counters, fn->counters, cv_size);

	return fn_dup;

err_counters:
	kfree(fn_dup->function_name);
err_name:
	kfree(fn_dup);
	return NULL;
}

/**
 * gcov_info_dup - duplicate profiling data set
 * @info: profiling data set to duplicate
 *
 * Return newly allocated duplicate on success, %NULL on error.
 */
struct gcov_info *gcov_info_dup(struct gcov_info *info)
{
	struct gcov_info *dup;
	struct gcov_fn_info *fn;

	dup = kmemdup(info, sizeof(*dup), GFP_KERNEL);
	if (!dup)
		return NULL;
	INIT_LIST_HEAD(&dup->head);
	INIT_LIST_HEAD(&dup->functions);
	dup->filename = kstrdup(info->filename, GFP_KERNEL);
	if (!dup->filename)
		goto err;

	list_for_each_entry(fn, &info->functions, head) {
		struct gcov_fn_info *fn_dup = gcov_fn_info_dup(fn);

		if (!fn_dup)
			goto err;
		list_add_tail(&fn_dup->head, &dup->functions);
	}

	return dup;

err:
	gcov_info_free(dup);
	return NULL;
}

/**
 * gcov_info_free - release memory for profiling data set duplicate
 * @info: profiling data set duplicate to free
 */
void gcov_info_free(struct gcov_info *info)
{
	struct gcov_fn_info *fn, *tmp;

	list_for_each_entry_safe(fn, tmp, &info->functions, head) {
		kfree(fn->function_name);
		vfree(fn->counters);
		list_del(&fn->head);
		kfree(fn);
	}
	kfree(info->filename);
	kfree(info);
}

#define ITER_STRIDE	PAGE_SIZE

/**
 * struct gcov_iterator - specifies current file position in logical records
 * @info: associated profiling data
 * @buffer: buffer containing file data
 * @size: size of buffer
 * @pos: current position in file
 */
struct gcov_iterator {
	struct gcov_info *info;
	void *buffer;
	size_t size;
	loff_t pos;
};

/**
 * store_gcov_u32 - store 32 bit number in gcov format to buffer
 * @buffer: target buffer or NULL
 * @off: offset into the buffer
 * @v: value to be stored
 *
 * Number format defined by gcc: numbers are recorded in the 32 bit
 * unsigned binary form of the endianness of the machine generating the
 * file. Returns the number of bytes stored. If @buffer is %NULL, doesn't
 * store anything.
 */
static size_t store_gcov_u32(void *buffer, size_t off, u32 v)
{
	u32 *data;

	if (buffer) {
		data = buffer + off;
		*data = v;
	}

	return sizeof(*data);
}

/**
 * store_gcov_u64 - store 64 bit number in gcov format to buffer
 * @buffer: target buffer or NULL
 * @off: offset into the buffer
 * @v: value to be stored
 *
 * Number format defined by gcc: numbers are recorded in the 32 bit
 * unsigned binary form of the endianness of the machine generating the
 * file. 64 bit numbers are stored as two 32 bit numbers, the low part
 * first. Returns the number of bytes stored. If @buffer is %NULL, doesn't store
 * anything.
 */
static size_t store_gcov_u64(void *buffer, size_t off, u64 v)
{
	u32 *data;

	if (buffer) {
		data = buffer + off;

		data[0] = (v & 0xffffffffUL);
		data[1] = (v >> 32);
	}

	return sizeof(*data) * 2;
}

/**
 * convert_to_gcda - convert profiling data set to gcda file format
 * @buffer: the buffer to store file data or %NULL if no data should be stored
 * @info: profiling data set to be converted
 *
 * Returns the number of bytes that were/would have been stored into the buffer.
 */
static size_t convert_to_gcda(char *buffer, struct gcov_info *info)
{
	struct gcov_fn_info *fi_ptr;
	size_t pos = 0;

	/* File header. */
	pos += store_gcov_u32(buffer, pos, GCOV_DATA_MAGIC);
	pos += store_gcov_u32(buffer, pos, info->version);
	pos += store_gcov_u32(buffer, pos, info->checksum);

	list_for_each_entry(fi_ptr, &info->functions, head) {
		u32 i;
		u32 len = 2;

		if (fi_ptr->use_extra_checksum)
			len++;

		pos += store_gcov_u32(buffer, pos, GCOV_TAG_FUNCTION);
		pos += store_gcov_u32(buffer, pos, len);
		pos += store_gcov_u32(buffer, pos, fi_ptr->ident);
		pos += store_gcov_u32(buffer, pos, fi_ptr->checksum);
		if (fi_ptr->use_extra_checksum)
			pos += store_gcov_u32(buffer, pos, fi_ptr->cfg_checksum);

		pos += store_gcov_u32(buffer, pos, GCOV_TAG_COUNTER_BASE);
		pos += store_gcov_u32(buffer, pos, fi_ptr->num_counters * 2);
		for (i = 0; i < fi_ptr->num_counters; i++)
			pos += store_gcov_u64(buffer, pos, fi_ptr->counters[i]);
	}

	return pos;
}

/**
 * gcov_iter_new - allocate and initialize profiling data iterator
 * @info: profiling data set to be iterated
 *
 * Return file iterator on success, %NULL otherwise.
 */
struct gcov_iterator *gcov_iter_new(struct gcov_info *info)
{
	struct gcov_iterator *iter;

	iter = kzalloc(sizeof(struct gcov_iterator), GFP_KERNEL);
	if (!iter)
		goto err_free;

	iter->info = info;
	/* Dry-run to get the actual buffer size. */
	iter->size = convert_to_gcda(NULL, info);
	iter->buffer = vmalloc(iter->size);
	if (!iter->buffer)
		goto err_free;

	convert_to_gcda(iter->buffer, info);

	return iter;

err_free:
	kfree(iter);
	return NULL;
}


/**
 * gcov_iter_get_info - return profiling data set for given file iterator
 * @iter: file iterator
 */
void gcov_iter_free(struct gcov_iterator *iter)
{
	vfree(iter->buffer);
	kfree(iter);
}

/**
 * gcov_iter_get_info - return profiling data set for given file iterator
 * @iter: file iterator
 */
struct gcov_info *gcov_iter_get_info(struct gcov_iterator *iter)
{
	return iter->info;
}

/**
 * gcov_iter_start - reset file iterator to starting position
 * @iter: file iterator
 */
void gcov_iter_start(struct gcov_iterator *iter)
{
	iter->pos = 0;
}

/**
 * gcov_iter_next - advance file iterator to next logical record
 * @iter: file iterator
 *
 * Return zero if new position is valid, non-zero if iterator has reached end.
 */
int gcov_iter_next(struct gcov_iterator *iter)
{
	if (iter->pos < iter->size)
		iter->pos += ITER_STRIDE;

	if (iter->pos >= iter->size)
		return -EINVAL;

	return 0;
}

/**
 * gcov_iter_write - write data for current pos to seq_file
 * @iter: file iterator
 * @seq: seq_file handle
 *
 * Return zero on success, non-zero otherwise.
 */
int gcov_iter_write(struct gcov_iterator *iter, struct seq_file *seq)
{
	size_t len;

	if (iter->pos >= iter->size)
		return -EINVAL;

	len = ITER_STRIDE;
	if (iter->pos + len > iter->size)
		len = iter->size - iter->pos;

	seq_write(seq, iter->buffer + iter->pos, len);

	return 0;
}
