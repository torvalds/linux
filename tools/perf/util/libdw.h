/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_LIBDW_H
#define PERF_LIBDW_H

#include <linux/types.h>

struct dso;
struct inline_node;
struct symbol;

#ifdef HAVE_LIBDW_SUPPORT
/*
 * libdw__addr2line - Convert address to source location using libdw
 * @addr: Address to resolve
 * @file: Pointer to return filename (caller must free)
 * @line_nr: Pointer to return line number
 * @dso: The dso struct
 * @unwind_inlines: Whether to unwind inline function calls
 * @node: Inline node list to append to
 * @sym: The symbol associated with the address
 *
 * This function initializes a Dwfl context for the DSO if not already present,
 * finds the source line information for the given address, and optionally
 * resolves inline function call chains.
 *
 * Returns 1 on success (found), 0 on failure (not found).
 */
int libdw__addr2line(u64 addr, char **file,
		     unsigned int *line_nr, struct dso *dso,
		     bool unwind_inlines, struct inline_node *node,
		     struct symbol *sym);

/*
 * dso__free_libdw - Free libdw resources associated with the DSO
 * @dso: The dso to free resources for
 *
 * This function cleans up the Dwfl context used for addr2line lookups.
 */
void dso__free_libdw(struct dso *dso);

#else /* HAVE_LIBDW_SUPPORT */

static inline int libdw__addr2line(u64 addr __maybe_unused, char **file __maybe_unused,
				   unsigned int *line_nr __maybe_unused,
				   struct dso *dso __maybe_unused,
				   bool unwind_inlines __maybe_unused,
				   struct inline_node *node __maybe_unused,
				   struct symbol *sym __maybe_unused)
{
	return 0;
}

static inline void dso__free_libdw(struct dso *dso __maybe_unused)
{
}
#endif /* HAVE_LIBDW_SUPPORT */

#endif /* PERF_LIBDW_H */
