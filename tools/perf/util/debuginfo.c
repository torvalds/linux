// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DWARF debug information handling code.  Copied from probe-finder.c.
 *
 * Written by Masami Hiramatsu <mhiramat@redhat.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/zalloc.h>

#include "build-id.h"
#include "dso.h"
#include "debug.h"
#include "debuginfo.h"
#include "symbol.h"

#ifdef HAVE_DEBUGINFOD_SUPPORT
#include <elfutils/debuginfod.h>
#endif

/* Dwarf FL wrappers */
static char *debuginfo_path;	/* Currently dummy */

static const Dwfl_Callbacks offline_callbacks = {
	.find_debuginfo = dwfl_standard_find_debuginfo,
	.debuginfo_path = &debuginfo_path,

	.section_address = dwfl_offline_section_address,

	/* We use this table for core files too.  */
	.find_elf = dwfl_build_id_find_elf,
};

/* Get a Dwarf from offline image */
static int debuginfo__init_offline_dwarf(struct debuginfo *dbg,
					 const char *path)
{
	GElf_Addr dummy;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return fd;

	dbg->dwfl = dwfl_begin(&offline_callbacks);
	if (!dbg->dwfl)
		goto error;

	dwfl_report_begin(dbg->dwfl);
	dbg->mod = dwfl_report_offline(dbg->dwfl, "", "", fd);
	if (!dbg->mod)
		goto error;

	dbg->dbg = dwfl_module_getdwarf(dbg->mod, &dbg->bias);
	if (!dbg->dbg)
		goto error;

	dwfl_module_build_id(dbg->mod, &dbg->build_id, &dummy);

	dwfl_report_end(dbg->dwfl, NULL, NULL);

	return 0;
error:
	if (dbg->dwfl)
		dwfl_end(dbg->dwfl);
	else
		close(fd);
	memset(dbg, 0, sizeof(*dbg));

	return -ENOENT;
}

static struct debuginfo *__debuginfo__new(const char *path)
{
	struct debuginfo *dbg = zalloc(sizeof(*dbg));
	if (!dbg)
		return NULL;

	if (debuginfo__init_offline_dwarf(dbg, path) < 0)
		zfree(&dbg);
	if (dbg)
		pr_debug("Open Debuginfo file: %s\n", path);
	return dbg;
}

enum dso_binary_type distro_dwarf_types[] = {
	DSO_BINARY_TYPE__FEDORA_DEBUGINFO,
	DSO_BINARY_TYPE__UBUNTU_DEBUGINFO,
	DSO_BINARY_TYPE__OPENEMBEDDED_DEBUGINFO,
	DSO_BINARY_TYPE__BUILDID_DEBUGINFO,
	DSO_BINARY_TYPE__MIXEDUP_UBUNTU_DEBUGINFO,
	DSO_BINARY_TYPE__NOT_FOUND,
};

struct debuginfo *debuginfo__new(const char *path)
{
	enum dso_binary_type *type;
	char buf[PATH_MAX], nil = '\0';
	struct dso *dso;
	struct debuginfo *dinfo = NULL;
	struct build_id bid = { .size = 0};

	/* Try to open distro debuginfo files */
	dso = dso__new(path);
	if (!dso)
		goto out;

	/*
	 * Set the build id for DSO_BINARY_TYPE__BUILDID_DEBUGINFO. Don't block
	 * incase the path isn't for a regular file.
	 */
	assert(!dso__has_build_id(dso));
	if (filename__read_build_id(path, &bid, /*block=*/false) > 0)
		dso__set_build_id(dso, &bid);

	for (type = distro_dwarf_types;
	     !dinfo && *type != DSO_BINARY_TYPE__NOT_FOUND;
	     type++) {
		if (dso__read_binary_type_filename(dso, *type, &nil,
						   buf, PATH_MAX) < 0)
			continue;
		dinfo = __debuginfo__new(buf);
	}
	dso__put(dso);

out:
	if (dinfo)
		return dinfo;

	/* if failed to open all distro debuginfo, open given binary */
	symbol__join_symfs(buf, path);
	return __debuginfo__new(buf);
}

void debuginfo__delete(struct debuginfo *dbg)
{
	if (dbg) {
		if (dbg->dwfl)
			dwfl_end(dbg->dwfl);
		free(dbg);
	}
}

/* For the kernel module, we need a special code to get a DIE */
int debuginfo__get_text_offset(struct debuginfo *dbg, Dwarf_Addr *offs,
				bool adjust_offset)
{
	int n, i;
	Elf32_Word shndx;
	Elf_Scn *scn;
	Elf *elf;
	GElf_Shdr mem, *shdr;
	const char *p;

	elf = dwfl_module_getelf(dbg->mod, &dbg->bias);
	if (!elf)
		return -EINVAL;

	/* Get the number of relocations */
	n = dwfl_module_relocations(dbg->mod);
	if (n < 0)
		return -ENOENT;
	/* Search the relocation related .text section */
	for (i = 0; i < n; i++) {
		p = dwfl_module_relocation_info(dbg->mod, i, &shndx);
		if (strcmp(p, ".text") == 0) {
			/* OK, get the section header */
			scn = elf_getscn(elf, shndx);
			if (!scn)
				return -ENOENT;
			shdr = gelf_getshdr(scn, &mem);
			if (!shdr)
				return -ENOENT;
			*offs = shdr->sh_addr;
			if (adjust_offset)
				*offs -= shdr->sh_offset;
		}
	}
	return 0;
}

#ifdef HAVE_DEBUGINFOD_SUPPORT
int get_source_from_debuginfod(const char *raw_path,
			       const char *sbuild_id, char **new_path)
{
	debuginfod_client *c = debuginfod_begin();
	const char *p = raw_path;
	int fd;

	if (!c)
		return -ENOMEM;

	fd = debuginfod_find_source(c, (const unsigned char *)sbuild_id,
				0, p, new_path);
	pr_debug("Search %s from debuginfod -> %d\n", p, fd);
	if (fd >= 0)
		close(fd);
	debuginfod_end(c);
	if (fd < 0) {
		pr_debug("Failed to find %s in debuginfod (%s)\n",
			raw_path, sbuild_id);
		return -ENOENT;
	}
	pr_debug("Got a source %s\n", *new_path);

	return 0;
}
#endif /* HAVE_DEBUGINFOD_SUPPORT */
