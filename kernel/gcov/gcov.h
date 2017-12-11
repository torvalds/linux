/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Profiling infrastructure declarations.
 *
 *  This file is based on gcc-internal definitions. Data structures are
 *  defined to be compatible with gcc counterparts. For a better
 *  understanding, refer to gcc source: gcc/gcov-io.h.
 *
 *    Copyright IBM Corp. 2009
 *    Author(s): Peter Oberparleiter <oberpar@linux.vnet.ibm.com>
 *
 *    Uses gcc-internal data definitions.
 */

#ifndef GCOV_H
#define GCOV_H GCOV_H

#include <linux/types.h>

/*
 * Profiling data types used for gcc 3.4 and above - these are defined by
 * gcc and need to be kept as close to the original definition as possible to
 * remain compatible.
 */
#define GCOV_DATA_MAGIC		((unsigned int) 0x67636461)
#define GCOV_TAG_FUNCTION	((unsigned int) 0x01000000)
#define GCOV_TAG_COUNTER_BASE	((unsigned int) 0x01a10000)
#define GCOV_TAG_FOR_COUNTER(count)					\
	(GCOV_TAG_COUNTER_BASE + ((unsigned int) (count) << 17))

#if BITS_PER_LONG >= 64
typedef long gcov_type;
#else
typedef long long gcov_type;
#endif

/* Opaque gcov_info. The gcov structures can change as for example in gcc 4.7 so
 * we cannot use full definition here and they need to be placed in gcc specific
 * implementation of gcov. This also means no direct access to the members in
 * generic code and usage of the interface below.*/
struct gcov_info;

/* Interface to access gcov_info data  */
const char *gcov_info_filename(struct gcov_info *info);
unsigned int gcov_info_version(struct gcov_info *info);
struct gcov_info *gcov_info_next(struct gcov_info *info);
void gcov_info_link(struct gcov_info *info);
void gcov_info_unlink(struct gcov_info *prev, struct gcov_info *info);

/* Base interface. */
enum gcov_action {
	GCOV_ADD,
	GCOV_REMOVE,
};

void gcov_event(enum gcov_action action, struct gcov_info *info);
void gcov_enable_events(void);

/* Iterator control. */
struct seq_file;
struct gcov_iterator;

struct gcov_iterator *gcov_iter_new(struct gcov_info *info);
void gcov_iter_free(struct gcov_iterator *iter);
void gcov_iter_start(struct gcov_iterator *iter);
int gcov_iter_next(struct gcov_iterator *iter);
int gcov_iter_write(struct gcov_iterator *iter, struct seq_file *seq);
struct gcov_info *gcov_iter_get_info(struct gcov_iterator *iter);

/* gcov_info control. */
void gcov_info_reset(struct gcov_info *info);
int gcov_info_is_compatible(struct gcov_info *info1, struct gcov_info *info2);
void gcov_info_add(struct gcov_info *dest, struct gcov_info *source);
struct gcov_info *gcov_info_dup(struct gcov_info *info);
void gcov_info_free(struct gcov_info *info);

struct gcov_link {
	enum {
		OBJ_TREE,
		SRC_TREE,
	} dir;
	const char *ext;
};
extern const struct gcov_link gcov_link[];

#endif /* GCOV_H */
