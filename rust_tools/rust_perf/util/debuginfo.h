/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _PERF_DEBUGINFO_H
#define _PERF_DEBUGINFO_H

#include <errno.h>
#include <linux/compiler.h>

#ifdef HAVE_LIBDW_SUPPORT

#include "dwarf-aux.h"

/* debug information structure */
struct debuginfo {
	Dwarf		*dbg;
	Dwfl_Module	*mod;
	Dwfl		*dwfl;
	Dwarf_Addr	bias;
	const unsigned char	*build_id;
};

/* This also tries to open distro debuginfo */
struct debuginfo *debuginfo__new(const char *path);
void debuginfo__delete(struct debuginfo *dbg);

int debuginfo__get_text_offset(struct debuginfo *dbg, Dwarf_Addr *offs,
			       bool adjust_offset);

#else /* HAVE_LIBDW_SUPPORT */

/* dummy debug information structure */
struct debuginfo {
};

static inline struct debuginfo *debuginfo__new(const char *path __maybe_unused)
{
	return NULL;
}

static inline void debuginfo__delete(struct debuginfo *dbg __maybe_unused)
{
}

typedef void Dwarf_Addr;

static inline int debuginfo__get_text_offset(struct debuginfo *dbg __maybe_unused,
					     Dwarf_Addr *offs __maybe_unused,
					     bool adjust_offset __maybe_unused)
{
	return -EINVAL;
}

#endif /* HAVE_LIBDW_SUPPORT */

#ifdef HAVE_DEBUGINFOD_SUPPORT
int get_source_from_debuginfod(const char *raw_path, const char *sbuild_id,
			       char **new_path);
#else /* HAVE_DEBUGINFOD_SUPPORT */
static inline int get_source_from_debuginfod(const char *raw_path __maybe_unused,
					     const char *sbuild_id __maybe_unused,
					     char **new_path __maybe_unused)
{
	return -ENOTSUP;
}
#endif /* HAVE_DEBUGINFOD_SUPPORT */

#endif /* _PERF_DEBUGINFO_H */
