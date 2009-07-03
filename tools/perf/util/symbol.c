#include "util.h"
#include "../perf.h"
#include "string.h"
#include "symbol.h"

#include <libelf.h>
#include <gelf.h>
#include <elf.h>

const char *sym_hist_filter;

static struct symbol *symbol__new(u64 start, u64 len,
				  const char *name, unsigned int priv_size,
				  u64 obj_start, int verbose)
{
	size_t namelen = strlen(name) + 1;
	struct symbol *self = calloc(1, priv_size + sizeof(*self) + namelen);

	if (!self)
		return NULL;

	if (verbose >= 2)
		printf("new symbol: %016Lx [%08lx]: %s, hist: %p, obj_start: %p\n",
			(u64)start, (unsigned long)len, name, self->hist, (void *)(unsigned long)obj_start);

	self->obj_start= obj_start;
	self->hist = NULL;
	self->hist_sum = 0;

	if (sym_hist_filter && !strcmp(name, sym_hist_filter))
		self->hist = calloc(sizeof(u64), len);

	if (priv_size) {
		memset(self, 0, priv_size);
		self = ((void *)self) + priv_size;
	}
	self->start = start;
	self->end   = len ? start + len - 1 : start;
	memcpy(self->name, name, namelen);

	return self;
}

static void symbol__delete(struct symbol *self, unsigned int priv_size)
{
	free(((void *)self) - priv_size);
}

static size_t symbol__fprintf(struct symbol *self, FILE *fp)
{
	if (!self->module)
		return fprintf(fp, " %llx-%llx %s\n",
		       self->start, self->end, self->name);
	else
		return fprintf(fp, " %llx-%llx %s \t[%s]\n",
		       self->start, self->end, self->name, self->module->name);
}

struct dso *dso__new(const char *name, unsigned int sym_priv_size)
{
	struct dso *self = malloc(sizeof(*self) + strlen(name) + 1);

	if (self != NULL) {
		strcpy(self->name, name);
		self->syms = RB_ROOT;
		self->sym_priv_size = sym_priv_size;
		self->find_symbol = dso__find_symbol;
	}

	return self;
}

static void dso__delete_symbols(struct dso *self)
{
	struct symbol *pos;
	struct rb_node *next = rb_first(&self->syms);

	while (next) {
		pos = rb_entry(next, struct symbol, rb_node);
		next = rb_next(&pos->rb_node);
		rb_erase(&pos->rb_node, &self->syms);
		symbol__delete(pos, self->sym_priv_size);
	}
}

void dso__delete(struct dso *self)
{
	dso__delete_symbols(self);
	free(self);
}

static void dso__insert_symbol(struct dso *self, struct symbol *sym)
{
	struct rb_node **p = &self->syms.rb_node;
	struct rb_node *parent = NULL;
	const u64 ip = sym->start;
	struct symbol *s;

	while (*p != NULL) {
		parent = *p;
		s = rb_entry(parent, struct symbol, rb_node);
		if (ip < s->start)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	rb_link_node(&sym->rb_node, parent, p);
	rb_insert_color(&sym->rb_node, &self->syms);
}

struct symbol *dso__find_symbol(struct dso *self, u64 ip)
{
	struct rb_node *n;

	if (self == NULL)
		return NULL;

	n = self->syms.rb_node;

	while (n) {
		struct symbol *s = rb_entry(n, struct symbol, rb_node);

		if (ip < s->start)
			n = n->rb_left;
		else if (ip > s->end)
			n = n->rb_right;
		else
			return s;
	}

	return NULL;
}

size_t dso__fprintf(struct dso *self, FILE *fp)
{
	size_t ret = fprintf(fp, "dso: %s\n", self->name);

	struct rb_node *nd;
	for (nd = rb_first(&self->syms); nd; nd = rb_next(nd)) {
		struct symbol *pos = rb_entry(nd, struct symbol, rb_node);
		ret += symbol__fprintf(pos, fp);
	}

	return ret;
}

static int dso__load_kallsyms(struct dso *self, symbol_filter_t filter, int verbose)
{
	struct rb_node *nd, *prevnd;
	char *line = NULL;
	size_t n;
	FILE *file = fopen("/proc/kallsyms", "r");
	int count = 0;

	if (file == NULL)
		goto out_failure;

	while (!feof(file)) {
		u64 start;
		struct symbol *sym;
		int line_len, len;
		char symbol_type;

		line_len = getline(&line, &n, file);
		if (line_len < 0)
			break;

		if (!line)
			goto out_failure;

		line[--line_len] = '\0'; /* \n */

		len = hex2u64(line, &start);

		len++;
		if (len + 2 >= line_len)
			continue;

		symbol_type = toupper(line[len]);
		/*
		 * We're interested only in code ('T'ext)
		 */
		if (symbol_type != 'T' && symbol_type != 'W')
			continue;
		/*
		 * Well fix up the end later, when we have all sorted.
		 */
		sym = symbol__new(start, 0xdead, line + len + 2,
				  self->sym_priv_size, 0, verbose);

		if (sym == NULL)
			goto out_delete_line;

		if (filter && filter(self, sym))
			symbol__delete(sym, self->sym_priv_size);
		else {
			dso__insert_symbol(self, sym);
			count++;
		}
	}

	/*
	 * Now that we have all sorted out, just set the ->end of all
	 * symbols
	 */
	prevnd = rb_first(&self->syms);

	if (prevnd == NULL)
		goto out_delete_line;

	for (nd = rb_next(prevnd); nd; nd = rb_next(nd)) {
		struct symbol *prev = rb_entry(prevnd, struct symbol, rb_node),
			      *curr = rb_entry(nd, struct symbol, rb_node);

		prev->end = curr->start - 1;
		prevnd = nd;
	}

	free(line);
	fclose(file);

	return count;

out_delete_line:
	free(line);
out_failure:
	return -1;
}

static int dso__load_perf_map(struct dso *self, symbol_filter_t filter, int verbose)
{
	char *line = NULL;
	size_t n;
	FILE *file;
	int nr_syms = 0;

	file = fopen(self->name, "r");
	if (file == NULL)
		goto out_failure;

	while (!feof(file)) {
		u64 start, size;
		struct symbol *sym;
		int line_len, len;

		line_len = getline(&line, &n, file);
		if (line_len < 0)
			break;

		if (!line)
			goto out_failure;

		line[--line_len] = '\0'; /* \n */

		len = hex2u64(line, &start);

		len++;
		if (len + 2 >= line_len)
			continue;

		len += hex2u64(line + len, &size);

		len++;
		if (len + 2 >= line_len)
			continue;

		sym = symbol__new(start, size, line + len,
				  self->sym_priv_size, start, verbose);

		if (sym == NULL)
			goto out_delete_line;

		if (filter && filter(self, sym))
			symbol__delete(sym, self->sym_priv_size);
		else {
			dso__insert_symbol(self, sym);
			nr_syms++;
		}
	}

	free(line);
	fclose(file);

	return nr_syms;

out_delete_line:
	free(line);
out_failure:
	return -1;
}

/**
 * elf_symtab__for_each_symbol - iterate thru all the symbols
 *
 * @self: struct elf_symtab instance to iterate
 * @index: uint32_t index
 * @sym: GElf_Sym iterator
 */
#define elf_symtab__for_each_symbol(syms, nr_syms, index, sym) \
	for (index = 0, gelf_getsym(syms, index, &sym);\
	     index < nr_syms; \
	     index++, gelf_getsym(syms, index, &sym))

static inline uint8_t elf_sym__type(const GElf_Sym *sym)
{
	return GELF_ST_TYPE(sym->st_info);
}

static inline int elf_sym__is_function(const GElf_Sym *sym)
{
	return elf_sym__type(sym) == STT_FUNC &&
	       sym->st_name != 0 &&
	       sym->st_shndx != SHN_UNDEF &&
	       sym->st_size != 0;
}

static inline int elf_sym__is_label(const GElf_Sym *sym)
{
	return elf_sym__type(sym) == STT_NOTYPE &&
		sym->st_name != 0 &&
		sym->st_shndx != SHN_UNDEF &&
		sym->st_shndx != SHN_ABS;
}

static inline const char *elf_sec__name(const GElf_Shdr *shdr,
					const Elf_Data *secstrs)
{
	return secstrs->d_buf + shdr->sh_name;
}

static inline int elf_sec__is_text(const GElf_Shdr *shdr,
					const Elf_Data *secstrs)
{
	return strstr(elf_sec__name(shdr, secstrs), "text") != NULL;
}

static inline const char *elf_sym__name(const GElf_Sym *sym,
					const Elf_Data *symstrs)
{
	return symstrs->d_buf + sym->st_name;
}

static Elf_Scn *elf_section_by_name(Elf *elf, GElf_Ehdr *ep,
				    GElf_Shdr *shp, const char *name,
				    size_t *index)
{
	Elf_Scn *sec = NULL;
	size_t cnt = 1;

	while ((sec = elf_nextscn(elf, sec)) != NULL) {
		char *str;

		gelf_getshdr(sec, shp);
		str = elf_strptr(elf, ep->e_shstrndx, shp->sh_name);
		if (!strcmp(name, str)) {
			if (index)
				*index = cnt;
			break;
		}
		++cnt;
	}

	return sec;
}

#define elf_section__for_each_rel(reldata, pos, pos_mem, idx, nr_entries) \
	for (idx = 0, pos = gelf_getrel(reldata, 0, &pos_mem); \
	     idx < nr_entries; \
	     ++idx, pos = gelf_getrel(reldata, idx, &pos_mem))

#define elf_section__for_each_rela(reldata, pos, pos_mem, idx, nr_entries) \
	for (idx = 0, pos = gelf_getrela(reldata, 0, &pos_mem); \
	     idx < nr_entries; \
	     ++idx, pos = gelf_getrela(reldata, idx, &pos_mem))

static int dso__synthesize_plt_symbols(struct  dso *self, Elf *elf,
				       GElf_Ehdr *ehdr, Elf_Scn *scn_dynsym,
				       GElf_Shdr *shdr_dynsym,
				       size_t dynsym_idx, int verbose)
{
	uint32_t nr_rel_entries, idx;
	GElf_Sym sym;
	u64 plt_offset;
	GElf_Shdr shdr_plt;
	struct symbol *f;
	GElf_Shdr shdr_rel_plt;
	Elf_Data *reldata, *syms, *symstrs;
	Elf_Scn *scn_plt_rel, *scn_symstrs;
	char sympltname[1024];
	int nr = 0, symidx;

	scn_plt_rel = elf_section_by_name(elf, ehdr, &shdr_rel_plt,
					  ".rela.plt", NULL);
	if (scn_plt_rel == NULL) {
		scn_plt_rel = elf_section_by_name(elf, ehdr, &shdr_rel_plt,
						  ".rel.plt", NULL);
		if (scn_plt_rel == NULL)
			return 0;
	}

	if (shdr_rel_plt.sh_link != dynsym_idx)
		return 0;

	if (elf_section_by_name(elf, ehdr, &shdr_plt, ".plt", NULL) == NULL)
		return 0;

	/*
	 * Fetch the relocation section to find the indexes to the GOT
	 * and the symbols in the .dynsym they refer to.
	 */
	reldata = elf_getdata(scn_plt_rel, NULL);
	if (reldata == NULL)
		return -1;

	syms = elf_getdata(scn_dynsym, NULL);
	if (syms == NULL)
		return -1;

	scn_symstrs = elf_getscn(elf, shdr_dynsym->sh_link);
	if (scn_symstrs == NULL)
		return -1;

	symstrs = elf_getdata(scn_symstrs, NULL);
	if (symstrs == NULL)
		return -1;

	nr_rel_entries = shdr_rel_plt.sh_size / shdr_rel_plt.sh_entsize;
	plt_offset = shdr_plt.sh_offset;

	if (shdr_rel_plt.sh_type == SHT_RELA) {
		GElf_Rela pos_mem, *pos;

		elf_section__for_each_rela(reldata, pos, pos_mem, idx,
					   nr_rel_entries) {
			symidx = GELF_R_SYM(pos->r_info);
			plt_offset += shdr_plt.sh_entsize;
			gelf_getsym(syms, symidx, &sym);
			snprintf(sympltname, sizeof(sympltname),
				 "%s@plt", elf_sym__name(&sym, symstrs));

			f = symbol__new(plt_offset, shdr_plt.sh_entsize,
					sympltname, self->sym_priv_size, 0, verbose);
			if (!f)
				return -1;

			dso__insert_symbol(self, f);
			++nr;
		}
	} else if (shdr_rel_plt.sh_type == SHT_REL) {
		GElf_Rel pos_mem, *pos;
		elf_section__for_each_rel(reldata, pos, pos_mem, idx,
					  nr_rel_entries) {
			symidx = GELF_R_SYM(pos->r_info);
			plt_offset += shdr_plt.sh_entsize;
			gelf_getsym(syms, symidx, &sym);
			snprintf(sympltname, sizeof(sympltname),
				 "%s@plt", elf_sym__name(&sym, symstrs));

			f = symbol__new(plt_offset, shdr_plt.sh_entsize,
					sympltname, self->sym_priv_size, 0, verbose);
			if (!f)
				return -1;

			dso__insert_symbol(self, f);
			++nr;
		}
	} else {
		/*
		 * TODO: There are still one more shdr_rel_plt.sh_type
		 * I have to investigate, but probably should be ignored.
		 */
	}

	return nr;
}

static int dso__load_sym(struct dso *self, int fd, const char *name,
			 symbol_filter_t filter, int verbose, struct module *mod)
{
	Elf_Data *symstrs, *secstrs;
	uint32_t nr_syms;
	int err = -1;
	uint32_t index;
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;
	Elf_Data *syms;
	GElf_Sym sym;
	Elf_Scn *sec, *sec_dynsym, *sec_strndx;
	Elf *elf;
	size_t dynsym_idx;
	int nr = 0;

	elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
	if (elf == NULL) {
		if (verbose)
			fprintf(stderr, "%s: cannot read %s ELF file.\n",
				__func__, name);
		goto out_close;
	}

	if (gelf_getehdr(elf, &ehdr) == NULL) {
		if (verbose)
			fprintf(stderr, "%s: cannot get elf header.\n", __func__);
		goto out_elf_end;
	}

	/*
	 * We need to check if we have a .dynsym, so that we can handle the
	 * .plt, synthesizing its symbols, that aren't on the symtabs (be it
	 * .dynsym or .symtab)
	 */
	sec_dynsym = elf_section_by_name(elf, &ehdr, &shdr,
					 ".dynsym", &dynsym_idx);
	if (sec_dynsym != NULL) {
		nr = dso__synthesize_plt_symbols(self, elf, &ehdr,
						 sec_dynsym, &shdr,
						 dynsym_idx, verbose);
		if (nr < 0)
			goto out_elf_end;
	}

	/*
	 * But if we have a full .symtab (that is a superset of .dynsym) we
	 * should add the symbols not in the .dynsyn
	 */
	sec = elf_section_by_name(elf, &ehdr, &shdr, ".symtab", NULL);
	if (sec == NULL) {
		if (sec_dynsym == NULL)
			goto out_elf_end;

		sec = sec_dynsym;
		gelf_getshdr(sec, &shdr);
	}

	syms = elf_getdata(sec, NULL);
	if (syms == NULL)
		goto out_elf_end;

	sec = elf_getscn(elf, shdr.sh_link);
	if (sec == NULL)
		goto out_elf_end;

	symstrs = elf_getdata(sec, NULL);
	if (symstrs == NULL)
		goto out_elf_end;

	sec_strndx = elf_getscn(elf, ehdr.e_shstrndx);
	if (sec_strndx == NULL)
		goto out_elf_end;

	secstrs = elf_getdata(sec_strndx, NULL);
	if (symstrs == NULL)
		goto out_elf_end;

	nr_syms = shdr.sh_size / shdr.sh_entsize;

	memset(&sym, 0, sizeof(sym));
	self->adjust_symbols = (ehdr.e_type == ET_EXEC ||
				elf_section_by_name(elf, &ehdr, &shdr,
						     ".gnu.prelink_undo",
						     NULL) != NULL);
	elf_symtab__for_each_symbol(syms, nr_syms, index, sym) {
		struct symbol *f;
		u64 obj_start;
		struct section *section = NULL;
		int is_label = elf_sym__is_label(&sym);
		const char *section_name;

		if (!is_label && !elf_sym__is_function(&sym))
			continue;

		sec = elf_getscn(elf, sym.st_shndx);
		if (!sec)
			goto out_elf_end;

		gelf_getshdr(sec, &shdr);

		if (is_label && !elf_sec__is_text(&shdr, secstrs))
			continue;

		section_name = elf_sec__name(&shdr, secstrs);
		obj_start = sym.st_value;

		if (self->adjust_symbols) {
			if (verbose >= 2)
				printf("adjusting symbol: st_value: %Lx sh_addr: %Lx sh_offset: %Lx\n",
					(u64)sym.st_value, (u64)shdr.sh_addr, (u64)shdr.sh_offset);

			sym.st_value -= shdr.sh_addr - shdr.sh_offset;
		}

		if (mod) {
			section = mod->sections->find_section(mod->sections, section_name);
			if (section)
				sym.st_value += section->vma;
			else {
				fprintf(stderr, "dso__load_sym() module %s lookup of %s failed\n",
					mod->name, section_name);
				goto out_elf_end;
			}
		}

		f = symbol__new(sym.st_value, sym.st_size,
				elf_sym__name(&sym, symstrs),
				self->sym_priv_size, obj_start, verbose);
		if (!f)
			goto out_elf_end;

		if (filter && filter(self, f))
			symbol__delete(f, self->sym_priv_size);
		else {
			f->module = mod;
			dso__insert_symbol(self, f);
			nr++;
		}
	}

	err = nr;
out_elf_end:
	elf_end(elf);
out_close:
	return err;
}

int dso__load(struct dso *self, symbol_filter_t filter, int verbose)
{
	int size = strlen(self->name) + sizeof("/usr/lib/debug%s.debug");
	char *name = malloc(size);
	int variant = 0;
	int ret = -1;
	int fd;

	if (!name)
		return -1;

	self->adjust_symbols = 0;

	if (strncmp(self->name, "/tmp/perf-", 10) == 0)
		return dso__load_perf_map(self, filter, verbose);

more:
	do {
		switch (variant) {
		case 0: /* Fedora */
			snprintf(name, size, "/usr/lib/debug%s.debug", self->name);
			break;
		case 1: /* Ubuntu */
			snprintf(name, size, "/usr/lib/debug%s", self->name);
			break;
		case 2: /* Sane people */
			snprintf(name, size, "%s", self->name);
			break;

		default:
			goto out;
		}
		variant++;

		fd = open(name, O_RDONLY);
	} while (fd < 0);

	ret = dso__load_sym(self, fd, name, filter, verbose, NULL);
	close(fd);

	/*
	 * Some people seem to have debuginfo files _WITHOUT_ debug info!?!?
	 */
	if (!ret)
		goto more;

out:
	free(name);
	return ret;
}

static int dso__load_module(struct dso *self, struct mod_dso *mods, const char *name,
			     symbol_filter_t filter, int verbose)
{
	struct module *mod = mod_dso__find_module(mods, name);
	int err = 0, fd;

	if (mod == NULL || !mod->active)
		return err;

	fd = open(mod->path, O_RDONLY);

	if (fd < 0)
		return err;

	err = dso__load_sym(self, fd, name, filter, verbose, mod);
	close(fd);

	return err;
}

int dso__load_modules(struct dso *self, symbol_filter_t filter, int verbose)
{
	struct mod_dso *mods = mod_dso__new_dso("modules");
	struct module *pos;
	struct rb_node *next;
	int err;

	err = mod_dso__load_modules(mods);

	if (err <= 0)
		return err;

	/*
	 * Iterate over modules, and load active symbols.
	 */
	next = rb_first(&mods->mods);
	while (next) {
		pos = rb_entry(next, struct module, rb_node);
		err = dso__load_module(self, mods, pos->name, filter, verbose);

		if (err < 0)
			break;

		next = rb_next(&pos->rb_node);
	}

	if (err < 0) {
		mod_dso__delete_modules(mods);
		mod_dso__delete_self(mods);
	}

	return err;
}

static inline void dso__fill_symbol_holes(struct dso *self)
{
	struct symbol *prev = NULL;
	struct rb_node *nd;

	for (nd = rb_last(&self->syms); nd; nd = rb_prev(nd)) {
		struct symbol *pos = rb_entry(nd, struct symbol, rb_node);

		if (prev) {
			u64 hole = 0;
			int alias = pos->start == prev->start;

			if (!alias)
				hole = prev->start - pos->end - 1;

			if (hole || alias) {
				if (alias)
					pos->end = prev->end;
				else if (hole)
					pos->end = prev->start - 1;
			}
		}
		prev = pos;
	}
}

static int dso__load_vmlinux(struct dso *self, const char *vmlinux,
			     symbol_filter_t filter, int verbose)
{
	int err, fd = open(vmlinux, O_RDONLY);

	if (fd < 0)
		return -1;

	err = dso__load_sym(self, fd, vmlinux, filter, verbose, NULL);

	if (err > 0)
		dso__fill_symbol_holes(self);

	close(fd);

	return err;
}

int dso__load_kernel(struct dso *self, const char *vmlinux,
		     symbol_filter_t filter, int verbose, int modules)
{
	int err = -1;

	if (vmlinux) {
		err = dso__load_vmlinux(self, vmlinux, filter, verbose);
		if (err > 0 && modules)
			err = dso__load_modules(self, filter, verbose);
	}

	if (err <= 0)
		err = dso__load_kallsyms(self, filter, verbose);

	return err;
}

void symbol__init(void)
{
	elf_version(EV_CURRENT);
}
