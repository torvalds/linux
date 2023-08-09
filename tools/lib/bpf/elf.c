// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

#include <libelf.h>
#include <gelf.h>
#include <fcntl.h>
#include <linux/kernel.h>

#include "libbpf_internal.h"
#include "str_error.h"

#define STRERR_BUFSIZE  128

/* Return next ELF section of sh_type after scn, or first of that type if scn is NULL. */
static Elf_Scn *elf_find_next_scn_by_type(Elf *elf, int sh_type, Elf_Scn *scn)
{
	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		GElf_Shdr sh;

		if (!gelf_getshdr(scn, &sh))
			continue;
		if (sh.sh_type == sh_type)
			return scn;
	}
	return NULL;
}

/* Find offset of function name in the provided ELF object. "binary_path" is
 * the path to the ELF binary represented by "elf", and only used for error
 * reporting matters. "name" matches symbol name or name@@LIB for library
 * functions.
 */
long elf_find_func_offset(Elf *elf, const char *binary_path, const char *name)
{
	int i, sh_types[2] = { SHT_DYNSYM, SHT_SYMTAB };
	bool is_shared_lib, is_name_qualified;
	long ret = -ENOENT;
	size_t name_len;
	GElf_Ehdr ehdr;

	if (!gelf_getehdr(elf, &ehdr)) {
		pr_warn("elf: failed to get ehdr from %s: %s\n", binary_path, elf_errmsg(-1));
		ret = -LIBBPF_ERRNO__FORMAT;
		goto out;
	}
	/* for shared lib case, we do not need to calculate relative offset */
	is_shared_lib = ehdr.e_type == ET_DYN;

	name_len = strlen(name);
	/* Does name specify "@@LIB"? */
	is_name_qualified = strstr(name, "@@") != NULL;

	/* Search SHT_DYNSYM, SHT_SYMTAB for symbol. This search order is used because if
	 * a binary is stripped, it may only have SHT_DYNSYM, and a fully-statically
	 * linked binary may not have SHT_DYMSYM, so absence of a section should not be
	 * reported as a warning/error.
	 */
	for (i = 0; i < ARRAY_SIZE(sh_types); i++) {
		size_t nr_syms, strtabidx, idx;
		Elf_Data *symbols = NULL;
		Elf_Scn *scn = NULL;
		int last_bind = -1;
		const char *sname;
		GElf_Shdr sh;

		scn = elf_find_next_scn_by_type(elf, sh_types[i], NULL);
		if (!scn) {
			pr_debug("elf: failed to find symbol table ELF sections in '%s'\n",
				 binary_path);
			continue;
		}
		if (!gelf_getshdr(scn, &sh))
			continue;
		strtabidx = sh.sh_link;
		symbols = elf_getdata(scn, 0);
		if (!symbols) {
			pr_warn("elf: failed to get symbols for symtab section in '%s': %s\n",
				binary_path, elf_errmsg(-1));
			ret = -LIBBPF_ERRNO__FORMAT;
			goto out;
		}
		nr_syms = symbols->d_size / sh.sh_entsize;

		for (idx = 0; idx < nr_syms; idx++) {
			int curr_bind;
			GElf_Sym sym;
			Elf_Scn *sym_scn;
			GElf_Shdr sym_sh;

			if (!gelf_getsym(symbols, idx, &sym))
				continue;

			if (GELF_ST_TYPE(sym.st_info) != STT_FUNC)
				continue;

			sname = elf_strptr(elf, strtabidx, sym.st_name);
			if (!sname)
				continue;

			curr_bind = GELF_ST_BIND(sym.st_info);

			/* User can specify func, func@@LIB or func@@LIB_VERSION. */
			if (strncmp(sname, name, name_len) != 0)
				continue;
			/* ...but we don't want a search for "foo" to match 'foo2" also, so any
			 * additional characters in sname should be of the form "@@LIB".
			 */
			if (!is_name_qualified && sname[name_len] != '\0' && sname[name_len] != '@')
				continue;

			if (ret >= 0) {
				/* handle multiple matches */
				if (last_bind != STB_WEAK && curr_bind != STB_WEAK) {
					/* Only accept one non-weak bind. */
					pr_warn("elf: ambiguous match for '%s', '%s' in '%s'\n",
						sname, name, binary_path);
					ret = -LIBBPF_ERRNO__FORMAT;
					goto out;
				} else if (curr_bind == STB_WEAK) {
					/* already have a non-weak bind, and
					 * this is a weak bind, so ignore.
					 */
					continue;
				}
			}

			/* Transform symbol's virtual address (absolute for
			 * binaries and relative for shared libs) into file
			 * offset, which is what kernel is expecting for
			 * uprobe/uretprobe attachment.
			 * See Documentation/trace/uprobetracer.rst for more
			 * details.
			 * This is done by looking up symbol's containing
			 * section's header and using it's virtual address
			 * (sh_addr) and corresponding file offset (sh_offset)
			 * to transform sym.st_value (virtual address) into
			 * desired final file offset.
			 */
			sym_scn = elf_getscn(elf, sym.st_shndx);
			if (!sym_scn)
				continue;
			if (!gelf_getshdr(sym_scn, &sym_sh))
				continue;

			ret = sym.st_value - sym_sh.sh_addr + sym_sh.sh_offset;
			last_bind = curr_bind;
		}
		if (ret > 0)
			break;
	}

	if (ret > 0) {
		pr_debug("elf: symbol address match for '%s' in '%s': 0x%lx\n", name, binary_path,
			 ret);
	} else {
		if (ret == 0) {
			pr_warn("elf: '%s' is 0 in symtab for '%s': %s\n", name, binary_path,
				is_shared_lib ? "should not be 0 in a shared library" :
						"try using shared library path instead");
			ret = -ENOENT;
		} else {
			pr_warn("elf: failed to find symbol '%s' in '%s'\n", name, binary_path);
		}
	}
out:
	return ret;
}

/* Find offset of function name in ELF object specified by path. "name" matches
 * symbol name or name@@LIB for library functions.
 */
long elf_find_func_offset_from_file(const char *binary_path, const char *name)
{
	char errmsg[STRERR_BUFSIZE];
	long ret = -ENOENT;
	Elf *elf;
	int fd;

	fd = open(binary_path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		ret = -errno;
		pr_warn("failed to open %s: %s\n", binary_path,
			libbpf_strerror_r(ret, errmsg, sizeof(errmsg)));
		return ret;
	}
	elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
	if (!elf) {
		pr_warn("elf: could not read elf from %s: %s\n", binary_path, elf_errmsg(-1));
		close(fd);
		return -LIBBPF_ERRNO__FORMAT;
	}

	ret = elf_find_func_offset(elf, binary_path, name);
	elf_end(elf);
	close(fd);
	return ret;
}

