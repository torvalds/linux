/*-
 * Copyright (c) 2009-2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Semihalf under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stand.h>
#include <libfdt.h>
#include <fdt.h>
#include <sys/param.h>
#include <sys/linker.h>
#include <machine/elf.h>

#include "bootstrap.h"
#include "fdt_platform.h"

#ifdef DEBUG
#define debugf(fmt, args...) do { printf("%s(): ", __func__);	\
    printf(fmt,##args); } while (0)
#else
#define debugf(fmt, args...)
#endif

#define FDT_CWD_LEN	256
#define FDT_MAX_DEPTH	12

#define FDT_PROP_SEP	" = "

#define COPYOUT(s,d,l)	archsw.arch_copyout(s, d, l)
#define COPYIN(s,d,l)	archsw.arch_copyin(s, d, l)

#define FDT_STATIC_DTB_SYMBOL	"fdt_static_dtb"

#define	CMD_REQUIRES_BLOB	0x01

/* Location of FDT yet to be loaded. */
/* This may be in read-only memory, so can't be manipulated directly. */
static struct fdt_header *fdt_to_load = NULL;
/* Location of FDT on heap. */
/* This is the copy we actually manipulate. */
static struct fdt_header *fdtp = NULL;
/* Size of FDT blob */
static size_t fdtp_size = 0;

static int fdt_load_dtb(vm_offset_t va);
static void fdt_print_overlay_load_error(int err, const char *filename);
static int fdt_check_overlay_compatible(void *base_fdt, void *overlay_fdt);

static int fdt_cmd_nyi(int argc, char *argv[]);
static int fdt_load_dtb_overlays_string(const char * filenames);

static int fdt_cmd_addr(int argc, char *argv[]);
static int fdt_cmd_mkprop(int argc, char *argv[]);
static int fdt_cmd_cd(int argc, char *argv[]);
static int fdt_cmd_hdr(int argc, char *argv[]);
static int fdt_cmd_ls(int argc, char *argv[]);
static int fdt_cmd_prop(int argc, char *argv[]);
static int fdt_cmd_pwd(int argc, char *argv[]);
static int fdt_cmd_rm(int argc, char *argv[]);
static int fdt_cmd_mknode(int argc, char *argv[]);
static int fdt_cmd_mres(int argc, char *argv[]);

typedef int cmdf_t(int, char *[]);

struct cmdtab {
	const char	*name;
	cmdf_t		*handler;
	int		flags;
};

static const struct cmdtab commands[] = {
	{ "addr", &fdt_cmd_addr,	0 },
	{ "alias", &fdt_cmd_nyi,	0 },
	{ "cd", &fdt_cmd_cd,		CMD_REQUIRES_BLOB },
	{ "header", &fdt_cmd_hdr,	CMD_REQUIRES_BLOB },
	{ "ls", &fdt_cmd_ls,		CMD_REQUIRES_BLOB },
	{ "mknode", &fdt_cmd_mknode,	CMD_REQUIRES_BLOB },
	{ "mkprop", &fdt_cmd_mkprop,	CMD_REQUIRES_BLOB },
	{ "mres", &fdt_cmd_mres,	CMD_REQUIRES_BLOB },
	{ "prop", &fdt_cmd_prop,	CMD_REQUIRES_BLOB },
	{ "pwd", &fdt_cmd_pwd,		CMD_REQUIRES_BLOB },
	{ "rm", &fdt_cmd_rm,		CMD_REQUIRES_BLOB },
	{ NULL, NULL }
};

static char cwd[FDT_CWD_LEN] = "/";

static vm_offset_t
fdt_find_static_dtb()
{
	Elf_Ehdr *ehdr;
	Elf_Shdr *shdr;
	Elf_Sym sym;
	vm_offset_t strtab, symtab, fdt_start;
	uint64_t offs;
	struct preloaded_file *kfp;
	struct file_metadata *md;
	char *strp;
	int i, sym_count;

	debugf("fdt_find_static_dtb()\n");

	sym_count = symtab = strtab = 0;
	strp = NULL;

	offs = __elfN(relocation_offset);

	kfp = file_findfile(NULL, NULL);
	if (kfp == NULL)
		return (0);

	/* Locate the dynamic symbols and strtab. */
	md = file_findmetadata(kfp, MODINFOMD_ELFHDR);
	if (md == NULL)
		return (0);
	ehdr = (Elf_Ehdr *)md->md_data;

	md = file_findmetadata(kfp, MODINFOMD_SHDR);
	if (md == NULL)
		return (0);
	shdr = (Elf_Shdr *)md->md_data;

	for (i = 0; i < ehdr->e_shnum; ++i) {
		if (shdr[i].sh_type == SHT_DYNSYM && symtab == 0) {
			symtab = shdr[i].sh_addr + offs;
			sym_count = shdr[i].sh_size / sizeof(Elf_Sym);
		} else if (shdr[i].sh_type == SHT_STRTAB && strtab == 0) {
			strtab = shdr[i].sh_addr + offs;
		}
	}

	/*
	 * The most efficient way to find a symbol would be to calculate a
	 * hash, find proper bucket and chain, and thus find a symbol.
	 * However, that would involve code duplication (e.g. for hash
	 * function). So we're using simpler and a bit slower way: we're
	 * iterating through symbols, searching for the one which name is
	 * 'equal' to 'fdt_static_dtb'. To speed up the process a little bit,
	 * we are eliminating symbols type of which is not STT_NOTYPE, or(and)
	 * those which binding attribute is not STB_GLOBAL.
	 */
	fdt_start = 0;
	while (sym_count > 0 && fdt_start == 0) {
		COPYOUT(symtab, &sym, sizeof(sym));
		symtab += sizeof(sym);
		--sym_count;
		if (ELF_ST_BIND(sym.st_info) != STB_GLOBAL ||
		    ELF_ST_TYPE(sym.st_info) != STT_NOTYPE)
			continue;
		strp = strdupout(strtab + sym.st_name);
		if (strcmp(strp, FDT_STATIC_DTB_SYMBOL) == 0)
			fdt_start = (vm_offset_t)sym.st_value + offs;
		free(strp);
	}
	return (fdt_start);
}

static int
fdt_load_dtb(vm_offset_t va)
{
	struct fdt_header header;
	int err;

	debugf("fdt_load_dtb(0x%08jx)\n", (uintmax_t)va);

	COPYOUT(va, &header, sizeof(header));
	err = fdt_check_header(&header);
	if (err < 0) {
		if (err == -FDT_ERR_BADVERSION) {
			snprintf(command_errbuf, sizeof(command_errbuf),
			    "incompatible blob version: %d, should be: %d",
			    fdt_version(fdtp), FDT_LAST_SUPPORTED_VERSION);
		} else {
			snprintf(command_errbuf, sizeof(command_errbuf),
			    "error validating blob: %s", fdt_strerror(err));
		}
		return (1);
	}

	/*
	 * Release previous blob
	 */
	if (fdtp)
		free(fdtp);

	fdtp_size = fdt_totalsize(&header);
	fdtp = malloc(fdtp_size);

	if (fdtp == NULL) {
		command_errmsg = "can't allocate memory for device tree copy";
		return (1);
	}

	COPYOUT(va, fdtp, fdtp_size);
	debugf("DTB blob found at 0x%jx, size: 0x%jx\n", (uintmax_t)va, (uintmax_t)fdtp_size);

	return (0);
}

int
fdt_load_dtb_addr(struct fdt_header *header)
{
	int err;

	debugf("fdt_load_dtb_addr(%p)\n", header);

	fdtp_size = fdt_totalsize(header);
	err = fdt_check_header(header);
	if (err < 0) {
		snprintf(command_errbuf, sizeof(command_errbuf),
		    "error validating blob: %s", fdt_strerror(err));
		return (err);
	}
	free(fdtp);
	if ((fdtp = malloc(fdtp_size)) == NULL) {
		command_errmsg = "can't allocate memory for device tree copy";
		return (1);
	}

	bcopy(header, fdtp, fdtp_size);
	return (0);
}

int
fdt_load_dtb_file(const char * filename)
{
	struct preloaded_file *bfp, *oldbfp;
	int err;

	debugf("fdt_load_dtb_file(%s)\n", filename);

	oldbfp = file_findfile(NULL, "dtb");

	/* Attempt to load and validate a new dtb from a file. */
	if ((bfp = file_loadraw(filename, "dtb", 1)) == NULL) {
		snprintf(command_errbuf, sizeof(command_errbuf),
		    "failed to load file '%s'", filename);
		return (1);
	}
	if ((err = fdt_load_dtb(bfp->f_addr)) != 0) {
		file_discard(bfp);
		return (err);
	}

	/* A new dtb was validated, discard any previous file. */
	if (oldbfp)
		file_discard(oldbfp);
	return (0);
}

static int
fdt_load_dtb_overlay(const char * filename)
{
	struct preloaded_file *bfp;
	struct fdt_header header;
	int err;

	debugf("fdt_load_dtb_overlay(%s)\n", filename);

	/* Attempt to load and validate a new dtb from a file. FDT_ERR_NOTFOUND
	 * is normally a libfdt error code, but libfdt would actually return
	 * -FDT_ERR_NOTFOUND. We re-purpose the error code here to convey a
	 * similar meaning: the file itself was not found, which can still be
	 * considered an error dealing with FDT pieces.
	 */
	if ((bfp = file_loadraw(filename, "dtbo", 1)) == NULL)
		return (FDT_ERR_NOTFOUND);

	COPYOUT(bfp->f_addr, &header, sizeof(header));
	err = fdt_check_header(&header);

	if (err < 0) {
		file_discard(bfp);
		return (err);
	}

	return (0);
}

static void
fdt_print_overlay_load_error(int err, const char *filename)
{

	switch (err) {
		case FDT_ERR_NOTFOUND:
			printf("%s: failed to load file\n", filename);
			break;
		case -FDT_ERR_BADVERSION:
			printf("%s: incompatible blob version: %d, should be: %d\n",
			    filename, fdt_version(fdtp),
			    FDT_LAST_SUPPORTED_VERSION);
			break;
		default:
			/* libfdt errs are negative */
			if (err < 0)
				printf("%s: error validating blob: %s\n",
				    filename, fdt_strerror(err));
			else
				printf("%s: unknown load error\n", filename);
			break;
	}
}

static int
fdt_load_dtb_overlays_string(const char * filenames)
{
	char *names;
	char *name, *name_ext;
	char *comaptr;
	int err, namesz;

	debugf("fdt_load_dtb_overlays_string(%s)\n", filenames);

	names = strdup(filenames);
	if (names == NULL)
		return (1);
	name = names;
	do {
		comaptr = strchr(name, ',');
		if (comaptr)
			*comaptr = '\0';
		err = fdt_load_dtb_overlay(name);
		if (err == FDT_ERR_NOTFOUND) {
			/* Allocate enough to append ".dtbo" */
			namesz = strlen(name) + 6;
			name_ext = malloc(namesz);
			if (name_ext == NULL) {
				fdt_print_overlay_load_error(err, name);
				name = comaptr + 1;
				continue;
			}
			snprintf(name_ext, namesz, "%s.dtbo", name);
			err = fdt_load_dtb_overlay(name_ext);
			free(name_ext);
		}
		/* Catch error with either initial load or fallback load */
		if (err != 0)
			fdt_print_overlay_load_error(err, name);
		name = comaptr + 1;
	} while(comaptr);

	free(names);
	return (0);
}

/*
 * fdt_check_overlay_compatible - check that the overlay_fdt is compatible with
 * base_fdt before we attempt to apply it. It will need to re-calculate offsets
 * in the base every time, rather than trying to cache them earlier in the
 * process, because the overlay application process can/will invalidate a lot of
 * offsets.
 */
static int
fdt_check_overlay_compatible(void *base_fdt, void *overlay_fdt)
{
	const char *compat;
	int compat_len, ocompat_len;
	int oroot_offset, root_offset;
	int slidx, sllen;

	oroot_offset = fdt_path_offset(overlay_fdt, "/");
	if (oroot_offset < 0)
		return (oroot_offset);
	/*
	 * If /compatible in the overlay does not exist or if it is empty, then
	 * we're automatically compatible. We do this for the sake of rapid
	 * overlay development for overlays that aren't intended to be deployed.
	 * The user assumes the risk of using an overlay without /compatible.
	 */
	if (fdt_get_property(overlay_fdt, oroot_offset, "compatible",
	    &ocompat_len) == NULL || ocompat_len == 0)
		return (0);
	root_offset = fdt_path_offset(base_fdt, "/");
	if (root_offset < 0)
		return (root_offset);
	/*
	 * However, an empty or missing /compatible on the base is an error,
	 * because allowing this offers no advantages.
	 */
	if (fdt_get_property(base_fdt, root_offset, "compatible",
	    &compat_len) == NULL)
		return (compat_len);
	else if(compat_len == 0)
		return (1);

	slidx = 0;
	compat = fdt_stringlist_get(overlay_fdt, oroot_offset, "compatible",
	    slidx, &sllen);
	while (compat != NULL) {
		if (fdt_stringlist_search(base_fdt, root_offset, "compatible",
		    compat) >= 0)
			return (0);
		++slidx;
		compat = fdt_stringlist_get(overlay_fdt, oroot_offset,
		    "compatible", slidx, &sllen);
	};

	/* We've exhausted the overlay's /compatible property... no match */
	return (1);
}

void
fdt_apply_overlays()
{
	struct preloaded_file *fp;
	size_t max_overlay_size, next_fdtp_size;
	size_t current_fdtp_size;
	void *current_fdtp;
	void *next_fdtp;
	void *overlay;
	int rv;

	if ((fdtp == NULL) || (fdtp_size == 0))
		return;

	max_overlay_size = 0;
	for (fp = file_findfile(NULL, "dtbo"); fp != NULL; fp = fp->f_next) {
		if (max_overlay_size < fp->f_size)
			max_overlay_size = fp->f_size;
	}

	/* Nothing to apply */
	if (max_overlay_size == 0)
		return;

	overlay = malloc(max_overlay_size);
	if (overlay == NULL) {
		printf("failed to allocate memory for DTB blob with overlays\n");
		return;
	}
	current_fdtp = fdtp;
	current_fdtp_size = fdtp_size;
	for (fp = file_findfile(NULL, "dtbo"); fp != NULL; fp = fp->f_next) {
		COPYOUT(fp->f_addr, overlay, fp->f_size);
		/* Check compatible first to avoid unnecessary allocation */
		rv = fdt_check_overlay_compatible(current_fdtp, overlay);
		if (rv != 0) {
			printf("DTB overlay '%s' not compatible\n", fp->f_name);
			continue;
		}
		printf("applying DTB overlay '%s'\n", fp->f_name);
		next_fdtp_size = current_fdtp_size + fp->f_size;
		next_fdtp = malloc(next_fdtp_size);
		if (next_fdtp == NULL) {
			/*
			 * Output warning, then move on to applying other
			 * overlays in case this one is simply too large.
			 */
			printf("failed to allocate memory for overlay base\n");
			continue;
		}
		rv = fdt_open_into(current_fdtp, next_fdtp, next_fdtp_size);
		if (rv != 0) {
			free(next_fdtp);
			printf("failed to open base dtb into overlay base\n");
			continue;
		}
		/* Both overlay and next_fdtp may be modified in place */
		rv = fdt_overlay_apply(next_fdtp, overlay);
		if (rv == 0) {
			/* Rotate next -> current */
			if (current_fdtp != fdtp)
				free(current_fdtp);
			current_fdtp = next_fdtp;
			current_fdtp_size = next_fdtp_size;
		} else {
			/*
			 * Assume here that the base we tried to apply on is
			 * either trashed or in an inconsistent state. Trying to
			 * load it might work, but it's better to discard it and
			 * play it safe. */
			free(next_fdtp);
			printf("failed to apply overlay: %s\n",
			    fdt_strerror(rv));
		}
	}
	/* We could have failed to apply all overlays; then we do nothing */
	if (current_fdtp != fdtp) {
		free(fdtp);
		fdtp = current_fdtp;
		fdtp_size = current_fdtp_size;
	}
	free(overlay);
}

int
fdt_setup_fdtp()
{
	struct preloaded_file *bfp;
	vm_offset_t va;
	
	debugf("fdt_setup_fdtp()\n");

	/* If we already loaded a file, use it. */
	if ((bfp = file_findfile(NULL, "dtb")) != NULL) {
		if (fdt_load_dtb(bfp->f_addr) == 0) {
			printf("Using DTB from loaded file '%s'.\n", 
			    bfp->f_name);
			return (0);
		}
	}

	/* If we were given the address of a valid blob in memory, use it. */
	if (fdt_to_load != NULL) {
		if (fdt_load_dtb_addr(fdt_to_load) == 0) {
			printf("Using DTB from memory address %p.\n",
			    fdt_to_load);
			return (0);
		}
	}

	if (fdt_platform_load_dtb() == 0)
		return (0);

	/* If there is a dtb compiled into the kernel, use it. */
	if ((va = fdt_find_static_dtb()) != 0) {
		if (fdt_load_dtb(va) == 0) {
			printf("Using DTB compiled into kernel.\n");
			return (0);
		}
	}
	
	command_errmsg = "No device tree blob found!\n";
	return (1);
}

#define fdt_strtovect(str, cellbuf, lim, cellsize) _fdt_strtovect((str), \
    (cellbuf), (lim), (cellsize), 0);

/* Force using base 16 */
#define fdt_strtovectx(str, cellbuf, lim, cellsize) _fdt_strtovect((str), \
    (cellbuf), (lim), (cellsize), 16);

static int
_fdt_strtovect(const char *str, void *cellbuf, int lim, unsigned char cellsize,
    uint8_t base)
{
	const char *buf = str;
	const char *end = str + strlen(str) - 2;
	uint32_t *u32buf = NULL;
	uint8_t *u8buf = NULL;
	int cnt = 0;

	if (cellsize == sizeof(uint32_t))
		u32buf = (uint32_t *)cellbuf;
	else
		u8buf = (uint8_t *)cellbuf;

	if (lim == 0)
		return (0);

	while (buf < end) {

		/* Skip white whitespace(s)/separators */
		while (!isxdigit(*buf) && buf < end)
			buf++;

		if (u32buf != NULL)
			u32buf[cnt] =
			    cpu_to_fdt32((uint32_t)strtol(buf, NULL, base));

		else
			u8buf[cnt] = (uint8_t)strtol(buf, NULL, base);

		if (cnt + 1 <= lim - 1)
			cnt++;
		else
			break;
		buf++;
		/* Find another number */
		while ((isxdigit(*buf) || *buf == 'x') && buf < end)
			buf++;
	}
	return (cnt);
}

void
fdt_fixup_ethernet(const char *str, char *ethstr, int len)
{
	uint8_t tmp_addr[6];

	/* Convert macaddr string into a vector of uints */
	fdt_strtovectx(str, &tmp_addr, 6, sizeof(uint8_t));
	/* Set actual property to a value from vect */
	fdt_setprop(fdtp, fdt_path_offset(fdtp, ethstr),
	    "local-mac-address", &tmp_addr, 6 * sizeof(uint8_t));
}

void
fdt_fixup_cpubusfreqs(unsigned long cpufreq, unsigned long busfreq)
{
	int lo, o = 0, o2, maxo = 0, depth;
	const uint32_t zero = 0;

	/* We want to modify every subnode of /cpus */
	o = fdt_path_offset(fdtp, "/cpus");
	if (o < 0)
		return;

	/* maxo should contain offset of node next to /cpus */
	depth = 0;
	maxo = o;
	while (depth != -1)
		maxo = fdt_next_node(fdtp, maxo, &depth);

	/* Find CPU frequency properties */
	o = fdt_node_offset_by_prop_value(fdtp, o, "clock-frequency",
	    &zero, sizeof(uint32_t));

	o2 = fdt_node_offset_by_prop_value(fdtp, o, "bus-frequency", &zero,
	    sizeof(uint32_t));

	lo = MIN(o, o2);

	while (o != -FDT_ERR_NOTFOUND && o2 != -FDT_ERR_NOTFOUND) {

		o = fdt_node_offset_by_prop_value(fdtp, lo,
		    "clock-frequency", &zero, sizeof(uint32_t));

		o2 = fdt_node_offset_by_prop_value(fdtp, lo, "bus-frequency",
		    &zero, sizeof(uint32_t));

		/* We're only interested in /cpus subnode(s) */
		if (lo > maxo)
			break;

		fdt_setprop_inplace_cell(fdtp, lo, "clock-frequency",
		    (uint32_t)cpufreq);

		fdt_setprop_inplace_cell(fdtp, lo, "bus-frequency",
		    (uint32_t)busfreq);

		lo = MIN(o, o2);
	}
}

#ifdef notyet
static int
fdt_reg_valid(uint32_t *reg, int len, int addr_cells, int size_cells)
{
	int cells_in_tuple, i, tuples, tuple_size;
	uint32_t cur_start, cur_size;

	cells_in_tuple = (addr_cells + size_cells);
	tuple_size = cells_in_tuple * sizeof(uint32_t);
	tuples = len / tuple_size;
	if (tuples == 0)
		return (EINVAL);

	for (i = 0; i < tuples; i++) {
		if (addr_cells == 2)
			cur_start = fdt64_to_cpu(reg[i * cells_in_tuple]);
		else
			cur_start = fdt32_to_cpu(reg[i * cells_in_tuple]);

		if (size_cells == 2)
			cur_size = fdt64_to_cpu(reg[i * cells_in_tuple + 2]);
		else
			cur_size = fdt32_to_cpu(reg[i * cells_in_tuple + 1]);

		if (cur_size == 0)
			return (EINVAL);

		debugf(" reg#%d (start: 0x%0x size: 0x%0x) valid!\n",
		    i, cur_start, cur_size);
	}
	return (0);
}
#endif

void
fdt_fixup_memory(struct fdt_mem_region *region, size_t num)
{
	struct fdt_mem_region *curmr;
	uint32_t addr_cells, size_cells;
	uint32_t *addr_cellsp, *size_cellsp;
	int err, i, len, memory, root;
	size_t realmrno;
	uint8_t *buf, *sb;
	uint64_t rstart, rsize;
	int reserved;

	root = fdt_path_offset(fdtp, "/");
	if (root < 0) {
		sprintf(command_errbuf, "Could not find root node !");
		return;
	}

	memory = fdt_path_offset(fdtp, "/memory");
	if (memory <= 0) {
		/* Create proper '/memory' node. */
		memory = fdt_add_subnode(fdtp, root, "memory");
		if (memory <= 0) {
			snprintf(command_errbuf, sizeof(command_errbuf),
			    "Could not fixup '/memory' "
			    "node, error code : %d!\n", memory);
			return;
		}

		err = fdt_setprop(fdtp, memory, "device_type", "memory",
		    sizeof("memory"));

		if (err < 0)
			return;
	}

	addr_cellsp = (uint32_t *)fdt_getprop(fdtp, root, "#address-cells",
	    NULL);
	size_cellsp = (uint32_t *)fdt_getprop(fdtp, root, "#size-cells", NULL);

	if (addr_cellsp == NULL || size_cellsp == NULL) {
		snprintf(command_errbuf, sizeof(command_errbuf),
		    "Could not fixup '/memory' node : "
		    "%s %s property not found in root node!\n",
		    (!addr_cellsp) ? "#address-cells" : "",
		    (!size_cellsp) ? "#size-cells" : "");
		return;
	}

	addr_cells = fdt32_to_cpu(*addr_cellsp);
	size_cells = fdt32_to_cpu(*size_cellsp);

	/*
	 * Convert memreserve data to memreserve property
	 * Check if property already exists
	 */
	reserved = fdt_num_mem_rsv(fdtp);
	if (reserved &&
	    (fdt_getprop(fdtp, root, "memreserve", NULL) == NULL)) {
		len = (addr_cells + size_cells) * reserved * sizeof(uint32_t);
		sb = buf = (uint8_t *)malloc(len);
		if (!buf)
			return;

		bzero(buf, len);

		for (i = 0; i < reserved; i++) {
			if (fdt_get_mem_rsv(fdtp, i, &rstart, &rsize))
				break;
			if (rsize) {
				/* Ensure endianness, and put cells into a buffer */
				if (addr_cells == 2)
					*(uint64_t *)buf =
					    cpu_to_fdt64(rstart);
				else
					*(uint32_t *)buf =
					    cpu_to_fdt32(rstart);

				buf += sizeof(uint32_t) * addr_cells;
				if (size_cells == 2)
					*(uint64_t *)buf =
					    cpu_to_fdt64(rsize);
				else
					*(uint32_t *)buf =
					    cpu_to_fdt32(rsize);

				buf += sizeof(uint32_t) * size_cells;
			}
		}

		/* Set property */
		if ((err = fdt_setprop(fdtp, root, "memreserve", sb, len)) < 0)
			printf("Could not fixup 'memreserve' property.\n");

		free(sb);
	} 

	/* Count valid memory regions entries in sysinfo. */
	realmrno = num;
	for (i = 0; i < num; i++)
		if (region[i].start == 0 && region[i].size == 0)
			realmrno--;

	if (realmrno == 0) {
		sprintf(command_errbuf, "Could not fixup '/memory' node : "
		    "sysinfo doesn't contain valid memory regions info!\n");
		return;
	}

	len = (addr_cells + size_cells) * realmrno * sizeof(uint32_t);
	sb = buf = (uint8_t *)malloc(len);
	if (!buf)
		return;

	bzero(buf, len);

	for (i = 0; i < num; i++) {
		curmr = &region[i];
		if (curmr->size != 0) {
			/* Ensure endianness, and put cells into a buffer */
			if (addr_cells == 2)
				*(uint64_t *)buf =
				    cpu_to_fdt64(curmr->start);
			else
				*(uint32_t *)buf =
				    cpu_to_fdt32(curmr->start);

			buf += sizeof(uint32_t) * addr_cells;
			if (size_cells == 2)
				*(uint64_t *)buf =
				    cpu_to_fdt64(curmr->size);
			else
				*(uint32_t *)buf =
				    cpu_to_fdt32(curmr->size);

			buf += sizeof(uint32_t) * size_cells;
		}
	}

	/* Set property */
	if ((err = fdt_setprop(fdtp, memory, "reg", sb, len)) < 0)
		sprintf(command_errbuf, "Could not fixup '/memory' node.\n");

	free(sb);
}

void
fdt_fixup_stdout(const char *str)
{
	char *ptr;
	int len, no, sero;
	const struct fdt_property *prop;
	char *tmp[10];

	ptr = (char *)str + strlen(str) - 1;
	while (ptr > str && isdigit(*(str - 1)))
		str--;

	if (ptr == str)
		return;

	no = fdt_path_offset(fdtp, "/chosen");
	if (no < 0)
		return;

	prop = fdt_get_property(fdtp, no, "stdout", &len);

	/* If /chosen/stdout does not extist, create it */
	if (prop == NULL || (prop != NULL && len == 0)) {

		bzero(tmp, 10 * sizeof(char));
		strcpy((char *)&tmp, "serial");
		if (strlen(ptr) > 3)
			/* Serial number too long */
			return;

		strncpy((char *)tmp + 6, ptr, 3);
		sero = fdt_path_offset(fdtp, (const char *)tmp);
		if (sero < 0)
			/*
			 * If serial device we're trying to assign
			 * stdout to doesn't exist in DT -- return.
			 */
			return;

		fdt_setprop(fdtp, no, "stdout", &tmp,
		    strlen((char *)&tmp) + 1);
		fdt_setprop(fdtp, no, "stdin", &tmp,
		    strlen((char *)&tmp) + 1);
	}
}

void
fdt_load_dtb_overlays(const char *extras)
{
	const char *s;

	/* Any extra overlays supplied by pre-loader environment */
	if (extras != NULL && *extras != '\0') {
		printf("Loading DTB overlays: '%s'\n", extras);
		fdt_load_dtb_overlays_string(extras);
	}

	/* Any overlays supplied by loader environment */
	s = getenv("fdt_overlays");
	if (s != NULL && *s != '\0') {
		printf("Loading DTB overlays: '%s'\n", s);
		fdt_load_dtb_overlays_string(s);
	}
}

/*
 * Locate the blob, fix it up and return its location.
 */
static int
fdt_fixup(void)
{
	int chosen;

	debugf("fdt_fixup()\n");

	if (fdtp == NULL && fdt_setup_fdtp() != 0)
		return (0);

	/* Create /chosen node (if not exists) */
	if ((chosen = fdt_subnode_offset(fdtp, 0, "chosen")) ==
	    -FDT_ERR_NOTFOUND)
		chosen = fdt_add_subnode(fdtp, 0, "chosen");

	/* Value assigned to fixup-applied does not matter. */
	if (fdt_getprop(fdtp, chosen, "fixup-applied", NULL))
		return (1);

	fdt_platform_fixups();

	/*
	 * Re-fetch the /chosen subnode; our fixups may apply overlays or add
	 * nodes/properties that invalidate the offset we grabbed or created
	 * above, so we can no longer trust it.
	 */
	chosen = fdt_subnode_offset(fdtp, 0, "chosen");
	fdt_setprop(fdtp, chosen, "fixup-applied", NULL, 0);
	return (1);
}

/*
 * Copy DTB blob to specified location and return size
 */
int
fdt_copy(vm_offset_t va)
{
	int err;
	debugf("fdt_copy va 0x%08x\n", va);
	if (fdtp == NULL) {
		err = fdt_setup_fdtp();
		if (err) {
			printf("No valid device tree blob found!\n");
			return (0);
		}
	}

	if (fdt_fixup() == 0)
		return (0);

	COPYIN(fdtp, va, fdtp_size);
	return (fdtp_size);
}



int
command_fdt_internal(int argc, char *argv[])
{
	cmdf_t *cmdh;
	int flags;
	int i, err;

	if (argc < 2) {
		command_errmsg = "usage is 'fdt <command> [<args>]";
		return (CMD_ERROR);
	}

	/*
	 * Validate fdt <command>.
	 */
	i = 0;
	cmdh = NULL;
	while (!(commands[i].name == NULL)) {
		if (strcmp(argv[1], commands[i].name) == 0) {
			/* found it */
			cmdh = commands[i].handler;
			flags = commands[i].flags;
			break;
		}
		i++;
	}
	if (cmdh == NULL) {
		command_errmsg = "unknown command";
		return (CMD_ERROR);
	}

	if (flags & CMD_REQUIRES_BLOB) {
		/*
		 * Check if uboot env vars were parsed already. If not, do it now.
		 */
		if (fdt_fixup() == 0)
			return (CMD_ERROR);
	}

	/*
	 * Call command handler.
	 */
	err = (*cmdh)(argc, argv);

	return (err);
}

static int
fdt_cmd_addr(int argc, char *argv[])
{
	struct preloaded_file *fp;
	struct fdt_header *hdr;
	const char *addr;
	char *cp;

	fdt_to_load = NULL;

	if (argc > 2)
		addr = argv[2];
	else {
		sprintf(command_errbuf, "no address specified");
		return (CMD_ERROR);
	}

	hdr = (struct fdt_header *)strtoul(addr, &cp, 16);
	if (cp == addr) {
		snprintf(command_errbuf, sizeof(command_errbuf),
		    "Invalid address: %s", addr);
		return (CMD_ERROR);
	}

	while ((fp = file_findfile(NULL, "dtb")) != NULL) {
		file_discard(fp);
	}

	fdt_to_load = hdr;
	return (CMD_OK);
}

static int
fdt_cmd_cd(int argc, char *argv[])
{
	char *path;
	char tmp[FDT_CWD_LEN];
	int len, o;

	path = (argc > 2) ? argv[2] : "/";

	if (path[0] == '/') {
		len = strlen(path);
		if (len >= FDT_CWD_LEN)
			goto fail;
	} else {
		/* Handle path specification relative to cwd */
		len = strlen(cwd) + strlen(path) + 1;
		if (len >= FDT_CWD_LEN)
			goto fail;

		strcpy(tmp, cwd);
		strcat(tmp, "/");
		strcat(tmp, path);
		path = tmp;
	}

	o = fdt_path_offset(fdtp, path);
	if (o < 0) {
		snprintf(command_errbuf, sizeof(command_errbuf),
		    "could not find node: '%s'", path);
		return (CMD_ERROR);
	}

	strcpy(cwd, path);
	return (CMD_OK);

fail:
	snprintf(command_errbuf, sizeof(command_errbuf),
	    "path too long: %d, max allowed: %d", len, FDT_CWD_LEN - 1);
	return (CMD_ERROR);
}

static int
fdt_cmd_hdr(int argc __unused, char *argv[] __unused)
{
	char line[80];
	int ver;

	if (fdtp == NULL) {
		command_errmsg = "no device tree blob pointer?!";
		return (CMD_ERROR);
	}

	ver = fdt_version(fdtp);
	pager_open();
	sprintf(line, "\nFlattened device tree header (%p):\n", fdtp);
	if (pager_output(line))
		goto out;
	sprintf(line, " magic                   = 0x%08x\n", fdt_magic(fdtp));
	if (pager_output(line))
		goto out;
	sprintf(line, " size                    = %d\n", fdt_totalsize(fdtp));
	if (pager_output(line))
		goto out;
	sprintf(line, " off_dt_struct           = 0x%08x\n",
	    fdt_off_dt_struct(fdtp));
	if (pager_output(line))
		goto out;
	sprintf(line, " off_dt_strings          = 0x%08x\n",
	    fdt_off_dt_strings(fdtp));
	if (pager_output(line))
		goto out;
	sprintf(line, " off_mem_rsvmap          = 0x%08x\n",
	    fdt_off_mem_rsvmap(fdtp));
	if (pager_output(line))
		goto out;
	sprintf(line, " version                 = %d\n", ver); 
	if (pager_output(line))
		goto out;
	sprintf(line, " last compatible version = %d\n",
	    fdt_last_comp_version(fdtp));
	if (pager_output(line))
		goto out;
	if (ver >= 2) {
		sprintf(line, " boot_cpuid              = %d\n",
		    fdt_boot_cpuid_phys(fdtp));
		if (pager_output(line))
			goto out;
	}
	if (ver >= 3) {
		sprintf(line, " size_dt_strings         = %d\n",
		    fdt_size_dt_strings(fdtp));
		if (pager_output(line))
			goto out;
	}
	if (ver >= 17) {
		sprintf(line, " size_dt_struct          = %d\n",
		    fdt_size_dt_struct(fdtp));
		if (pager_output(line))
			goto out;
	}
out:
	pager_close();

	return (CMD_OK);
}

static int
fdt_cmd_ls(int argc, char *argv[])
{
	const char *prevname[FDT_MAX_DEPTH] = { NULL };
	const char *name;
	char *path;
	int i, o, depth;

	path = (argc > 2) ? argv[2] : NULL;
	if (path == NULL)
		path = cwd;

	o = fdt_path_offset(fdtp, path);
	if (o < 0) {
		snprintf(command_errbuf, sizeof(command_errbuf),
		    "could not find node: '%s'", path);
		return (CMD_ERROR);
	}

	for (depth = 0;
	    (o >= 0) && (depth >= 0);
	    o = fdt_next_node(fdtp, o, &depth)) {

		name = fdt_get_name(fdtp, o, NULL);

		if (depth > FDT_MAX_DEPTH) {
			printf("max depth exceeded: %d\n", depth);
			continue;
		}

		prevname[depth] = name;

		/* Skip root (i = 1) when printing devices */
		for (i = 1; i <= depth; i++) {
			if (prevname[i] == NULL)
				break;

			if (strcmp(cwd, "/") == 0)
				printf("/");
			printf("%s", prevname[i]);
		}
		printf("\n");
	}

	return (CMD_OK);
}

static __inline int
isprint(int c)
{

	return (c >= ' ' && c <= 0x7e);
}

static int
fdt_isprint(const void *data, int len, int *count)
{
	const char *d;
	char ch;
	int yesno, i;

	if (len == 0)
		return (0);

	d = (const char *)data;
	if (d[len - 1] != '\0')
		return (0);

	*count = 0;
	yesno = 1;
	for (i = 0; i < len; i++) {
		ch = *(d + i);
		if (isprint(ch) || (ch == '\0' && i > 0)) {
			/* Count strings */
			if (ch == '\0')
				(*count)++;
			continue;
		}

		yesno = 0;
		break;
	}

	return (yesno);
}

static int
fdt_data_str(const void *data, int len, int count, char **buf)
{
	char *b, *tmp;
	const char *d;
	int buf_len, i, l;

	/*
	 * Calculate the length for the string and allocate memory.
	 *
	 * Note that 'len' already includes at least one terminator.
	 */
	buf_len = len;
	if (count > 1) {
		/*
		 * Each token had already a terminator buried in 'len', but we
		 * only need one eventually, don't count space for these.
		 */
		buf_len -= count - 1;

		/* Each consecutive token requires a ", " separator. */
		buf_len += count * 2;
	}

	/* Add some space for surrounding double quotes. */
	buf_len += count * 2;

	/* Note that string being put in 'tmp' may be as big as 'buf_len'. */
	b = (char *)malloc(buf_len);
	tmp = (char *)malloc(buf_len);
	if (b == NULL)
		goto error;

	if (tmp == NULL) {
		free(b);
		goto error;
	}

	b[0] = '\0';

	/*
	 * Now that we have space, format the string.
	 */
	i = 0;
	do {
		d = (const char *)data + i;
		l = strlen(d) + 1;

		sprintf(tmp, "\"%s\"%s", d,
		    (i + l) < len ?  ", " : "");
		strcat(b, tmp);

		i += l;

	} while (i < len);
	*buf = b;

	free(tmp);

	return (0);
error:
	return (1);
}

static int
fdt_data_cell(const void *data, int len, char **buf)
{
	char *b, *tmp;
	const uint32_t *c;
	int count, i, l;

	/* Number of cells */
	count = len / 4;

	/*
	 * Calculate the length for the string and allocate memory.
	 */

	/* Each byte translates to 2 output characters */
	l = len * 2;
	if (count > 1) {
		/* Each consecutive cell requires a " " separator. */
		l += (count - 1) * 1;
	}
	/* Each cell will have a "0x" prefix */
	l += count * 2;
	/* Space for surrounding <> and terminator */
	l += 3;

	b = (char *)malloc(l);
	tmp = (char *)malloc(l);
	if (b == NULL)
		goto error;

	if (tmp == NULL) {
		free(b);
		goto error;
	}

	b[0] = '\0';
	strcat(b, "<");

	for (i = 0; i < len; i += 4) {
		c = (const uint32_t *)((const uint8_t *)data + i);
		sprintf(tmp, "0x%08x%s", fdt32_to_cpu(*c),
		    i < (len - 4) ? " " : "");
		strcat(b, tmp);
	}
	strcat(b, ">");
	*buf = b;

	free(tmp);

	return (0);
error:
	return (1);
}

static int
fdt_data_bytes(const void *data, int len, char **buf)
{
	char *b, *tmp;
	const char *d;
	int i, l;

	/*
	 * Calculate the length for the string and allocate memory.
	 */

	/* Each byte translates to 2 output characters */
	l = len * 2;
	if (len > 1)
		/* Each consecutive byte requires a " " separator. */
		l += (len - 1) * 1;
	/* Each byte will have a "0x" prefix */
	l += len * 2;
	/* Space for surrounding [] and terminator. */
	l += 3;

	b = (char *)malloc(l);
	tmp = (char *)malloc(l);
	if (b == NULL)
		goto error;

	if (tmp == NULL) {
		free(b);
		goto error;
	}

	b[0] = '\0';
	strcat(b, "[");

	for (i = 0, d = data; i < len; i++) {
		sprintf(tmp, "0x%02x%s", d[i], i < len - 1 ? " " : "");
		strcat(b, tmp);
	}
	strcat(b, "]");
	*buf = b;

	free(tmp);

	return (0);
error:
	return (1);
}

static int
fdt_data_fmt(const void *data, int len, char **buf)
{
	int count;

	if (len == 0) {
		*buf = NULL;
		return (1);
	}

	if (fdt_isprint(data, len, &count))
		return (fdt_data_str(data, len, count, buf));

	else if ((len % 4) == 0)
		return (fdt_data_cell(data, len, buf));

	else
		return (fdt_data_bytes(data, len, buf));
}

static int
fdt_prop(int offset)
{
	char *line, *buf;
	const struct fdt_property *prop;
	const char *name;
	const void *data;
	int len, rv;

	line = NULL;
	prop = fdt_offset_ptr(fdtp, offset, sizeof(*prop));
	if (prop == NULL)
		return (1);

	name = fdt_string(fdtp, fdt32_to_cpu(prop->nameoff));
	len = fdt32_to_cpu(prop->len);

	rv = 0;
	buf = NULL;
	if (len == 0) {
		/* Property without value */
		line = (char *)malloc(strlen(name) + 2);
		if (line == NULL) {
			rv = 2;
			goto out2;
		}
		sprintf(line, "%s\n", name);
		goto out1;
	}

	/*
	 * Process property with value
	 */
	data = prop->data;

	if (fdt_data_fmt(data, len, &buf) != 0) {
		rv = 3;
		goto out2;
	}

	line = (char *)malloc(strlen(name) + strlen(FDT_PROP_SEP) +
	    strlen(buf) + 2);
	if (line == NULL) {
		sprintf(command_errbuf, "could not allocate space for string");
		rv = 4;
		goto out2;
	}

	sprintf(line, "%s" FDT_PROP_SEP "%s\n", name, buf);

out1:
	pager_open();
	pager_output(line);
	pager_close();

out2:
	if (buf)
		free(buf);

	if (line)
		free(line);

	return (rv);
}

static int
fdt_modprop(int nodeoff, char *propname, void *value, char mode)
{
	uint32_t cells[100];
	const char *buf;
	int len, rv;
	const struct fdt_property *p;

	p = fdt_get_property(fdtp, nodeoff, propname, NULL);

	if (p != NULL) {
		if (mode == 1) {
			 /* Adding inexistant value in mode 1 is forbidden */
			sprintf(command_errbuf, "property already exists!");
			return (CMD_ERROR);
		}
	} else if (mode == 0) {
		sprintf(command_errbuf, "property does not exist!");
		return (CMD_ERROR);
	}
	rv = 0;
	buf = value;

	switch (*buf) {
	case '&':
		/* phandles */
		break;
	case '<':
		/* Data cells */
		len = fdt_strtovect(buf, (void *)&cells, 100,
		    sizeof(uint32_t));

		rv = fdt_setprop(fdtp, nodeoff, propname, &cells,
		    len * sizeof(uint32_t));
		break;
	case '[':
		/* Data bytes */
		len = fdt_strtovect(buf, (void *)&cells, 100,
		    sizeof(uint8_t));

		rv = fdt_setprop(fdtp, nodeoff, propname, &cells,
		    len * sizeof(uint8_t));
		break;
	case '"':
	default:
		/* Default -- string */
		rv = fdt_setprop_string(fdtp, nodeoff, propname, value);
		break;
	}

	if (rv != 0) {
		if (rv == -FDT_ERR_NOSPACE)
			sprintf(command_errbuf,
			    "Device tree blob is too small!\n");
		else
			sprintf(command_errbuf,
			    "Could not add/modify property!\n");
	}
	return (rv);
}

/* Merge strings from argv into a single string */
static int
fdt_merge_strings(int argc, char *argv[], int start, char **buffer)
{
	char *buf;
	int i, idx, sz;

	*buffer = NULL;
	sz = 0;

	for (i = start; i < argc; i++)
		sz += strlen(argv[i]);

	/* Additional bytes for whitespaces between args */
	sz += argc - start;

	buf = (char *)malloc(sizeof(char) * sz);
	if (buf == NULL) {
		sprintf(command_errbuf, "could not allocate space "
		    "for string");
		return (1);
	}
	bzero(buf, sizeof(char) * sz);

	idx = 0;
	for (i = start, idx = 0; i < argc; i++) {
		strcpy(buf + idx, argv[i]);
		idx += strlen(argv[i]);
		buf[idx] = ' ';
		idx++;
	}
	buf[sz - 1] = '\0';
	*buffer = buf;
	return (0);
}

/* Extract offset and name of node/property from a given path */
static int
fdt_extract_nameloc(char **pathp, char **namep, int *nodeoff)
{
	int o;
	char *path = *pathp, *name = NULL, *subpath = NULL;

	subpath = strrchr(path, '/');
	if (subpath == NULL) {
		o = fdt_path_offset(fdtp, cwd);
		name = path;
		path = (char *)&cwd;
	} else {
		*subpath = '\0';
		if (strlen(path) == 0)
			path = cwd;

		name = subpath + 1;
		o = fdt_path_offset(fdtp, path);
	}

	if (strlen(name) == 0) {
		sprintf(command_errbuf, "name not specified");
		return (1);
	}
	if (o < 0) {
		snprintf(command_errbuf, sizeof(command_errbuf),
		    "could not find node: '%s'", path);
		return (1);
	}
	*namep = name;
	*nodeoff = o;
	*pathp = path;
	return (0);
}

static int
fdt_cmd_prop(int argc, char *argv[])
{
	char *path, *propname, *value;
	int o, next, depth, rv;
	uint32_t tag;

	path = (argc > 2) ? argv[2] : NULL;

	value = NULL;

	if (argc > 3) {
		/* Merge property value strings into one */
		if (fdt_merge_strings(argc, argv, 3, &value) != 0)
			return (CMD_ERROR);
	} else
		value = NULL;

	if (path == NULL)
		path = cwd;

	rv = CMD_OK;

	if (value) {
		/* If value is specified -- try to modify prop. */
		if (fdt_extract_nameloc(&path, &propname, &o) != 0)
			return (CMD_ERROR);

		rv = fdt_modprop(o, propname, value, 0);
		if (rv)
			return (CMD_ERROR);
		return (CMD_OK);

	}
	/* User wants to display properties */
	o = fdt_path_offset(fdtp, path);

	if (o < 0) {
		snprintf(command_errbuf, sizeof(command_errbuf),
		    "could not find node: '%s'", path);
		rv = CMD_ERROR;
		goto out;
	}

	depth = 0;
	while (depth >= 0) {
		tag = fdt_next_tag(fdtp, o, &next);
		switch (tag) {
		case FDT_NOP:
			break;
		case FDT_PROP:
			if (depth > 1)
				/* Don't process properties of nested nodes */
				break;

			if (fdt_prop(o) != 0) {
				sprintf(command_errbuf, "could not process "
				    "property");
				rv = CMD_ERROR;
				goto out;
			}
			break;
		case FDT_BEGIN_NODE:
			depth++;
			if (depth > FDT_MAX_DEPTH) {
				printf("warning: nesting too deep: %d\n",
				    depth);
				goto out;
			}
			break;
		case FDT_END_NODE:
			depth--;
			if (depth == 0)
				/*
				 * This is the end of our starting node, force
				 * the loop finish.
				 */
				depth--;
			break;
		}
		o = next;
	}
out:
	return (rv);
}

static int
fdt_cmd_mkprop(int argc, char *argv[])
{
	int o;
	char *path, *propname, *value;

	path = (argc > 2) ? argv[2] : NULL;

	value = NULL;

	if (argc > 3) {
		/* Merge property value strings into one */
		if (fdt_merge_strings(argc, argv, 3, &value) != 0)
			return (CMD_ERROR);
	} else
		value = NULL;

	if (fdt_extract_nameloc(&path, &propname, &o) != 0)
		return (CMD_ERROR);

	if (fdt_modprop(o, propname, value, 1))
		return (CMD_ERROR);

	return (CMD_OK);
}

static int
fdt_cmd_rm(int argc, char *argv[])
{
	int o, rv;
	char *path = NULL, *propname;

	if (argc > 2)
		path = argv[2];
	else {
		sprintf(command_errbuf, "no node/property name specified");
		return (CMD_ERROR);
	}

	o = fdt_path_offset(fdtp, path);
	if (o < 0) {
		/* If node not found -- try to find & delete property */
		if (fdt_extract_nameloc(&path, &propname, &o) != 0)
			return (CMD_ERROR);

		if ((rv = fdt_delprop(fdtp, o, propname)) != 0) {
			snprintf(command_errbuf, sizeof(command_errbuf),
			    "could not delete %s\n",
			    (rv == -FDT_ERR_NOTFOUND) ?
			    "(property/node does not exist)" : "");
			return (CMD_ERROR);

		} else
			return (CMD_OK);
	}
	/* If node exists -- remove node */
	rv = fdt_del_node(fdtp, o);
	if (rv) {
		sprintf(command_errbuf, "could not delete node");
		return (CMD_ERROR);
	}
	return (CMD_OK);
}

static int
fdt_cmd_mknode(int argc, char *argv[])
{
	int o, rv;
	char *path = NULL, *nodename = NULL;

	if (argc > 2)
		path = argv[2];
	else {
		sprintf(command_errbuf, "no node name specified");
		return (CMD_ERROR);
	}

	if (fdt_extract_nameloc(&path, &nodename, &o) != 0)
		return (CMD_ERROR);

	rv = fdt_add_subnode(fdtp, o, nodename);

	if (rv < 0) {
		if (rv == -FDT_ERR_NOSPACE)
			sprintf(command_errbuf,
			    "Device tree blob is too small!\n");
		else
			sprintf(command_errbuf,
			    "Could not add node!\n");
		return (CMD_ERROR);
	}
	return (CMD_OK);
}

static int
fdt_cmd_pwd(int argc, char *argv[])
{
	char line[FDT_CWD_LEN];

	pager_open();
	sprintf(line, "%s\n", cwd);
	pager_output(line);
	pager_close();
	return (CMD_OK);
}

static int
fdt_cmd_mres(int argc, char *argv[])
{
	uint64_t start, size;
	int i, total;
	char line[80];

	pager_open();
	total = fdt_num_mem_rsv(fdtp);
	if (total > 0) {
		if (pager_output("Reserved memory regions:\n"))
			goto out;
		for (i = 0; i < total; i++) {
			fdt_get_mem_rsv(fdtp, i, &start, &size);
			sprintf(line, "reg#%d: (start: 0x%jx, size: 0x%jx)\n", 
			    i, start, size);
			if (pager_output(line))
				goto out;
		}
	} else
		pager_output("No reserved memory regions\n");
out:
	pager_close();

	return (CMD_OK);
}

static int
fdt_cmd_nyi(int argc, char *argv[])
{

	printf("command not yet implemented\n");
	return (CMD_ERROR);
}
