#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include "build-id.h"
#include "util.h"
#include "debug.h"
#include "machine.h"
#include "symbol.h"
#include "strlist.h"

#include <elf.h>
#include <limits.h>
#include <sys/utsname.h>

#ifndef KSYM_NAME_LEN
#define KSYM_NAME_LEN 256
#endif

static int dso__load_kernel_sym(struct dso *dso, struct map *map,
				symbol_filter_t filter);
static int dso__load_guest_kernel_sym(struct dso *dso, struct map *map,
			symbol_filter_t filter);
int vmlinux_path__nr_entries;
char **vmlinux_path;

struct symbol_conf symbol_conf = {
	.use_modules	  = true,
	.try_vmlinux_path = true,
	.annotate_src	  = true,
	.demangle	  = true,
	.symfs            = "",
};

static enum dso_binary_type binary_type_symtab[] = {
	DSO_BINARY_TYPE__KALLSYMS,
	DSO_BINARY_TYPE__GUEST_KALLSYMS,
	DSO_BINARY_TYPE__JAVA_JIT,
	DSO_BINARY_TYPE__DEBUGLINK,
	DSO_BINARY_TYPE__BUILD_ID_CACHE,
	DSO_BINARY_TYPE__FEDORA_DEBUGINFO,
	DSO_BINARY_TYPE__UBUNTU_DEBUGINFO,
	DSO_BINARY_TYPE__BUILDID_DEBUGINFO,
	DSO_BINARY_TYPE__SYSTEM_PATH_DSO,
	DSO_BINARY_TYPE__GUEST_KMODULE,
	DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE,
	DSO_BINARY_TYPE__OPENEMBEDDED_DEBUGINFO,
	DSO_BINARY_TYPE__NOT_FOUND,
};

#define DSO_BINARY_TYPE__SYMTAB_CNT ARRAY_SIZE(binary_type_symtab)

bool symbol_type__is_a(char symbol_type, enum map_type map_type)
{
	symbol_type = toupper(symbol_type);

	switch (map_type) {
	case MAP__FUNCTION:
		return symbol_type == 'T' || symbol_type == 'W';
	case MAP__VARIABLE:
		return symbol_type == 'D';
	default:
		return false;
	}
}

static int prefix_underscores_count(const char *str)
{
	const char *tail = str;

	while (*tail == '_')
		tail++;

	return tail - str;
}

#define SYMBOL_A 0
#define SYMBOL_B 1

static int choose_best_symbol(struct symbol *syma, struct symbol *symb)
{
	s64 a;
	s64 b;
	size_t na, nb;

	/* Prefer a symbol with non zero length */
	a = syma->end - syma->start;
	b = symb->end - symb->start;
	if ((b == 0) && (a > 0))
		return SYMBOL_A;
	else if ((a == 0) && (b > 0))
		return SYMBOL_B;

	/* Prefer a non weak symbol over a weak one */
	a = syma->binding == STB_WEAK;
	b = symb->binding == STB_WEAK;
	if (b && !a)
		return SYMBOL_A;
	if (a && !b)
		return SYMBOL_B;

	/* Prefer a global symbol over a non global one */
	a = syma->binding == STB_GLOBAL;
	b = symb->binding == STB_GLOBAL;
	if (a && !b)
		return SYMBOL_A;
	if (b && !a)
		return SYMBOL_B;

	/* Prefer a symbol with less underscores */
	a = prefix_underscores_count(syma->name);
	b = prefix_underscores_count(symb->name);
	if (b > a)
		return SYMBOL_A;
	else if (a > b)
		return SYMBOL_B;

	/* Choose the symbol with the longest name */
	na = strlen(syma->name);
	nb = strlen(symb->name);
	if (na > nb)
		return SYMBOL_A;
	else if (na < nb)
		return SYMBOL_B;

	/* Avoid "SyS" kernel syscall aliases */
	if (na >= 3 && !strncmp(syma->name, "SyS", 3))
		return SYMBOL_B;
	if (na >= 10 && !strncmp(syma->name, "compat_SyS", 10))
		return SYMBOL_B;

	return SYMBOL_A;
}

void symbols__fixup_duplicate(struct rb_root *symbols)
{
	struct rb_node *nd;
	struct symbol *curr, *next;

	nd = rb_first(symbols);

	while (nd) {
		curr = rb_entry(nd, struct symbol, rb_node);
again:
		nd = rb_next(&curr->rb_node);
		next = rb_entry(nd, struct symbol, rb_node);

		if (!nd)
			break;

		if (curr->start != next->start)
			continue;

		if (choose_best_symbol(curr, next) == SYMBOL_A) {
			rb_erase(&next->rb_node, symbols);
			goto again;
		} else {
			nd = rb_next(&curr->rb_node);
			rb_erase(&curr->rb_node, symbols);
		}
	}
}

void symbols__fixup_end(struct rb_root *symbols)
{
	struct rb_node *nd, *prevnd = rb_first(symbols);
	struct symbol *curr, *prev;

	if (prevnd == NULL)
		return;

	curr = rb_entry(prevnd, struct symbol, rb_node);

	for (nd = rb_next(prevnd); nd; nd = rb_next(nd)) {
		prev = curr;
		curr = rb_entry(nd, struct symbol, rb_node);

		if (prev->end == prev->start && prev->end != curr->start)
			prev->end = curr->start - 1;
	}

	/* Last entry */
	if (curr->end == curr->start)
		curr->end = roundup(curr->start, 4096);
}

void __map_groups__fixup_end(struct map_groups *mg, enum map_type type)
{
	struct map *prev, *curr;
	struct rb_node *nd, *prevnd = rb_first(&mg->maps[type]);

	if (prevnd == NULL)
		return;

	curr = rb_entry(prevnd, struct map, rb_node);

	for (nd = rb_next(prevnd); nd; nd = rb_next(nd)) {
		prev = curr;
		curr = rb_entry(nd, struct map, rb_node);
		prev->end = curr->start - 1;
	}

	/*
	 * We still haven't the actual symbols, so guess the
	 * last map final address.
	 */
	curr->end = ~0ULL;
}

struct symbol *symbol__new(u64 start, u64 len, u8 binding, const char *name)
{
	size_t namelen = strlen(name) + 1;
	struct symbol *sym = calloc(1, (symbol_conf.priv_size +
					sizeof(*sym) + namelen));
	if (sym == NULL)
		return NULL;

	if (symbol_conf.priv_size)
		sym = ((void *)sym) + symbol_conf.priv_size;

	sym->start   = start;
	sym->end     = len ? start + len - 1 : start;
	sym->binding = binding;
	sym->namelen = namelen - 1;

	pr_debug4("%s: %s %#" PRIx64 "-%#" PRIx64 "\n",
		  __func__, name, start, sym->end);
	memcpy(sym->name, name, namelen);

	return sym;
}

void symbol__delete(struct symbol *sym)
{
	free(((void *)sym) - symbol_conf.priv_size);
}

size_t symbol__fprintf(struct symbol *sym, FILE *fp)
{
	return fprintf(fp, " %" PRIx64 "-%" PRIx64 " %c %s\n",
		       sym->start, sym->end,
		       sym->binding == STB_GLOBAL ? 'g' :
		       sym->binding == STB_LOCAL  ? 'l' : 'w',
		       sym->name);
}

size_t symbol__fprintf_symname_offs(const struct symbol *sym,
				    const struct addr_location *al, FILE *fp)
{
	unsigned long offset;
	size_t length;

	if (sym && sym->name) {
		length = fprintf(fp, "%s", sym->name);
		if (al) {
			if (al->addr < sym->end)
				offset = al->addr - sym->start;
			else
				offset = al->addr - al->map->start - sym->start;
			length += fprintf(fp, "+0x%lx", offset);
		}
		return length;
	} else
		return fprintf(fp, "[unknown]");
}

size_t symbol__fprintf_symname(const struct symbol *sym, FILE *fp)
{
	return symbol__fprintf_symname_offs(sym, NULL, fp);
}

void symbols__delete(struct rb_root *symbols)
{
	struct symbol *pos;
	struct rb_node *next = rb_first(symbols);

	while (next) {
		pos = rb_entry(next, struct symbol, rb_node);
		next = rb_next(&pos->rb_node);
		rb_erase(&pos->rb_node, symbols);
		symbol__delete(pos);
	}
}

void symbols__insert(struct rb_root *symbols, struct symbol *sym)
{
	struct rb_node **p = &symbols->rb_node;
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
	rb_insert_color(&sym->rb_node, symbols);
}

static struct symbol *symbols__find(struct rb_root *symbols, u64 ip)
{
	struct rb_node *n;

	if (symbols == NULL)
		return NULL;

	n = symbols->rb_node;

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

static struct symbol *symbols__first(struct rb_root *symbols)
{
	struct rb_node *n = rb_first(symbols);

	if (n)
		return rb_entry(n, struct symbol, rb_node);

	return NULL;
}

struct symbol_name_rb_node {
	struct rb_node	rb_node;
	struct symbol	sym;
};

static void symbols__insert_by_name(struct rb_root *symbols, struct symbol *sym)
{
	struct rb_node **p = &symbols->rb_node;
	struct rb_node *parent = NULL;
	struct symbol_name_rb_node *symn, *s;

	symn = container_of(sym, struct symbol_name_rb_node, sym);

	while (*p != NULL) {
		parent = *p;
		s = rb_entry(parent, struct symbol_name_rb_node, rb_node);
		if (strcmp(sym->name, s->sym.name) < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	rb_link_node(&symn->rb_node, parent, p);
	rb_insert_color(&symn->rb_node, symbols);
}

static void symbols__sort_by_name(struct rb_root *symbols,
				  struct rb_root *source)
{
	struct rb_node *nd;

	for (nd = rb_first(source); nd; nd = rb_next(nd)) {
		struct symbol *pos = rb_entry(nd, struct symbol, rb_node);
		symbols__insert_by_name(symbols, pos);
	}
}

static struct symbol *symbols__find_by_name(struct rb_root *symbols,
					    const char *name)
{
	struct rb_node *n;

	if (symbols == NULL)
		return NULL;

	n = symbols->rb_node;

	while (n) {
		struct symbol_name_rb_node *s;
		int cmp;

		s = rb_entry(n, struct symbol_name_rb_node, rb_node);
		cmp = strcmp(name, s->sym.name);

		if (cmp < 0)
			n = n->rb_left;
		else if (cmp > 0)
			n = n->rb_right;
		else
			return &s->sym;
	}

	return NULL;
}

struct symbol *dso__find_symbol(struct dso *dso,
				enum map_type type, u64 addr)
{
	return symbols__find(&dso->symbols[type], addr);
}

struct symbol *dso__first_symbol(struct dso *dso, enum map_type type)
{
	return symbols__first(&dso->symbols[type]);
}

struct symbol *dso__find_symbol_by_name(struct dso *dso, enum map_type type,
					const char *name)
{
	return symbols__find_by_name(&dso->symbol_names[type], name);
}

void dso__sort_by_name(struct dso *dso, enum map_type type)
{
	dso__set_sorted_by_name(dso, type);
	return symbols__sort_by_name(&dso->symbol_names[type],
				     &dso->symbols[type]);
}

size_t dso__fprintf_symbols_by_name(struct dso *dso,
				    enum map_type type, FILE *fp)
{
	size_t ret = 0;
	struct rb_node *nd;
	struct symbol_name_rb_node *pos;

	for (nd = rb_first(&dso->symbol_names[type]); nd; nd = rb_next(nd)) {
		pos = rb_entry(nd, struct symbol_name_rb_node, rb_node);
		fprintf(fp, "%s\n", pos->sym.name);
	}

	return ret;
}

int kallsyms__parse(const char *filename, void *arg,
		    int (*process_symbol)(void *arg, const char *name,
					  char type, u64 start))
{
	char *line = NULL;
	size_t n;
	int err = -1;
	FILE *file = fopen(filename, "r");

	if (file == NULL)
		goto out_failure;

	err = 0;

	while (!feof(file)) {
		u64 start;
		int line_len, len;
		char symbol_type;
		char *symbol_name;

		line_len = getline(&line, &n, file);
		if (line_len < 0 || !line)
			break;

		line[--line_len] = '\0'; /* \n */

		len = hex2u64(line, &start);

		len++;
		if (len + 2 >= line_len)
			continue;

		symbol_type = line[len];
		len += 2;
		symbol_name = line + len;
		len = line_len - len;

		if (len >= KSYM_NAME_LEN) {
			err = -1;
			break;
		}

		err = process_symbol(arg, symbol_name,
				     symbol_type, start);
		if (err)
			break;
	}

	free(line);
	fclose(file);
	return err;

out_failure:
	return -1;
}

int modules__parse(const char *filename, void *arg,
		   int (*process_module)(void *arg, const char *name,
					 u64 start))
{
	char *line = NULL;
	size_t n;
	FILE *file;
	int err = 0;

	file = fopen(filename, "r");
	if (file == NULL)
		return -1;

	while (1) {
		char name[PATH_MAX];
		u64 start;
		char *sep;
		ssize_t line_len;

		line_len = getline(&line, &n, file);
		if (line_len < 0) {
			if (feof(file))
				break;
			err = -1;
			goto out;
		}

		if (!line) {
			err = -1;
			goto out;
		}

		line[--line_len] = '\0'; /* \n */

		sep = strrchr(line, 'x');
		if (sep == NULL)
			continue;

		hex2u64(sep + 1, &start);

		sep = strchr(line, ' ');
		if (sep == NULL)
			continue;

		*sep = '\0';

		scnprintf(name, sizeof(name), "[%s]", line);

		err = process_module(arg, name, start);
		if (err)
			break;
	}
out:
	free(line);
	fclose(file);
	return err;
}

struct process_kallsyms_args {
	struct map *map;
	struct dso *dso;
};

static u8 kallsyms2elf_type(char type)
{
	if (type == 'W')
		return STB_WEAK;

	return isupper(type) ? STB_GLOBAL : STB_LOCAL;
}

static int map__process_kallsym_symbol(void *arg, const char *name,
				       char type, u64 start)
{
	struct symbol *sym;
	struct process_kallsyms_args *a = arg;
	struct rb_root *root = &a->dso->symbols[a->map->type];

	if (!symbol_type__is_a(type, a->map->type))
		return 0;

	/*
	 * module symbols are not sorted so we add all
	 * symbols, setting length to 0, and rely on
	 * symbols__fixup_end() to fix it up.
	 */
	sym = symbol__new(start, 0, kallsyms2elf_type(type), name);
	if (sym == NULL)
		return -ENOMEM;
	/*
	 * We will pass the symbols to the filter later, in
	 * map__split_kallsyms, when we have split the maps per module
	 */
	symbols__insert(root, sym);

	return 0;
}

/*
 * Loads the function entries in /proc/kallsyms into kernel_map->dso,
 * so that we can in the next step set the symbol ->end address and then
 * call kernel_maps__split_kallsyms.
 */
static int dso__load_all_kallsyms(struct dso *dso, const char *filename,
				  struct map *map)
{
	struct process_kallsyms_args args = { .map = map, .dso = dso, };
	return kallsyms__parse(filename, &args, map__process_kallsym_symbol);
}

static int dso__split_kallsyms_for_kcore(struct dso *dso, struct map *map,
					 symbol_filter_t filter)
{
	struct map_groups *kmaps = map__kmap(map)->kmaps;
	struct map *curr_map;
	struct symbol *pos;
	int count = 0, moved = 0;
	struct rb_root *root = &dso->symbols[map->type];
	struct rb_node *next = rb_first(root);

	while (next) {
		char *module;

		pos = rb_entry(next, struct symbol, rb_node);
		next = rb_next(&pos->rb_node);

		module = strchr(pos->name, '\t');
		if (module)
			*module = '\0';

		curr_map = map_groups__find(kmaps, map->type, pos->start);

		if (!curr_map || (filter && filter(curr_map, pos))) {
			rb_erase(&pos->rb_node, root);
			symbol__delete(pos);
		} else {
			pos->start -= curr_map->start - curr_map->pgoff;
			if (pos->end)
				pos->end -= curr_map->start - curr_map->pgoff;
			if (curr_map != map) {
				rb_erase(&pos->rb_node, root);
				symbols__insert(
					&curr_map->dso->symbols[curr_map->type],
					pos);
				++moved;
			} else {
				++count;
			}
		}
	}

	/* Symbols have been adjusted */
	dso->adjust_symbols = 1;

	return count + moved;
}

/*
 * Split the symbols into maps, making sure there are no overlaps, i.e. the
 * kernel range is broken in several maps, named [kernel].N, as we don't have
 * the original ELF section names vmlinux have.
 */
static int dso__split_kallsyms(struct dso *dso, struct map *map,
			       symbol_filter_t filter)
{
	struct map_groups *kmaps = map__kmap(map)->kmaps;
	struct machine *machine = kmaps->machine;
	struct map *curr_map = map;
	struct symbol *pos;
	int count = 0, moved = 0;	
	struct rb_root *root = &dso->symbols[map->type];
	struct rb_node *next = rb_first(root);
	int kernel_range = 0;

	while (next) {
		char *module;

		pos = rb_entry(next, struct symbol, rb_node);
		next = rb_next(&pos->rb_node);

		module = strchr(pos->name, '\t');
		if (module) {
			if (!symbol_conf.use_modules)
				goto discard_symbol;

			*module++ = '\0';

			if (strcmp(curr_map->dso->short_name, module)) {
				if (curr_map != map &&
				    dso->kernel == DSO_TYPE_GUEST_KERNEL &&
				    machine__is_default_guest(machine)) {
					/*
					 * We assume all symbols of a module are
					 * continuous in * kallsyms, so curr_map
					 * points to a module and all its
					 * symbols are in its kmap. Mark it as
					 * loaded.
					 */
					dso__set_loaded(curr_map->dso,
							curr_map->type);
				}

				curr_map = map_groups__find_by_name(kmaps,
							map->type, module);
				if (curr_map == NULL) {
					pr_debug("%s/proc/{kallsyms,modules} "
					         "inconsistency while looking "
						 "for \"%s\" module!\n",
						 machine->root_dir, module);
					curr_map = map;
					goto discard_symbol;
				}

				if (curr_map->dso->loaded &&
				    !machine__is_default_guest(machine))
					goto discard_symbol;
			}
			/*
			 * So that we look just like we get from .ko files,
			 * i.e. not prelinked, relative to map->start.
			 */
			pos->start = curr_map->map_ip(curr_map, pos->start);
			pos->end   = curr_map->map_ip(curr_map, pos->end);
		} else if (curr_map != map) {
			char dso_name[PATH_MAX];
			struct dso *ndso;

			if (count == 0) {
				curr_map = map;
				goto filter_symbol;
			}

			if (dso->kernel == DSO_TYPE_GUEST_KERNEL)
				snprintf(dso_name, sizeof(dso_name),
					"[guest.kernel].%d",
					kernel_range++);
			else
				snprintf(dso_name, sizeof(dso_name),
					"[kernel].%d",
					kernel_range++);

			ndso = dso__new(dso_name);
			if (ndso == NULL)
				return -1;

			ndso->kernel = dso->kernel;

			curr_map = map__new2(pos->start, ndso, map->type);
			if (curr_map == NULL) {
				dso__delete(ndso);
				return -1;
			}

			curr_map->map_ip = curr_map->unmap_ip = identity__map_ip;
			map_groups__insert(kmaps, curr_map);
			++kernel_range;
		}
filter_symbol:
		if (filter && filter(curr_map, pos)) {
discard_symbol:		rb_erase(&pos->rb_node, root);
			symbol__delete(pos);
		} else {
			if (curr_map != map) {
				rb_erase(&pos->rb_node, root);
				symbols__insert(&curr_map->dso->symbols[curr_map->type], pos);
				++moved;
			} else
				++count;
		}
	}

	if (curr_map != map &&
	    dso->kernel == DSO_TYPE_GUEST_KERNEL &&
	    machine__is_default_guest(kmaps->machine)) {
		dso__set_loaded(curr_map->dso, curr_map->type);
	}

	return count + moved;
}

bool symbol__restricted_filename(const char *filename,
				 const char *restricted_filename)
{
	bool restricted = false;

	if (symbol_conf.kptr_restrict) {
		char *r = realpath(filename, NULL);

		if (r != NULL) {
			restricted = strcmp(r, restricted_filename) == 0;
			free(r);
			return restricted;
		}
	}

	return restricted;
}

struct module_info {
	struct rb_node rb_node;
	char *name;
	u64 start;
};

static void add_module(struct module_info *mi, struct rb_root *modules)
{
	struct rb_node **p = &modules->rb_node;
	struct rb_node *parent = NULL;
	struct module_info *m;

	while (*p != NULL) {
		parent = *p;
		m = rb_entry(parent, struct module_info, rb_node);
		if (strcmp(mi->name, m->name) < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	rb_link_node(&mi->rb_node, parent, p);
	rb_insert_color(&mi->rb_node, modules);
}

static void delete_modules(struct rb_root *modules)
{
	struct module_info *mi;
	struct rb_node *next = rb_first(modules);

	while (next) {
		mi = rb_entry(next, struct module_info, rb_node);
		next = rb_next(&mi->rb_node);
		rb_erase(&mi->rb_node, modules);
		free(mi->name);
		free(mi);
	}
}

static struct module_info *find_module(const char *name,
				       struct rb_root *modules)
{
	struct rb_node *n = modules->rb_node;

	while (n) {
		struct module_info *m;
		int cmp;

		m = rb_entry(n, struct module_info, rb_node);
		cmp = strcmp(name, m->name);
		if (cmp < 0)
			n = n->rb_left;
		else if (cmp > 0)
			n = n->rb_right;
		else
			return m;
	}

	return NULL;
}

static int __read_proc_modules(void *arg, const char *name, u64 start)
{
	struct rb_root *modules = arg;
	struct module_info *mi;

	mi = zalloc(sizeof(struct module_info));
	if (!mi)
		return -ENOMEM;

	mi->name = strdup(name);
	mi->start = start;

	if (!mi->name) {
		free(mi);
		return -ENOMEM;
	}

	add_module(mi, modules);

	return 0;
}

static int read_proc_modules(const char *filename, struct rb_root *modules)
{
	if (symbol__restricted_filename(filename, "/proc/modules"))
		return -1;

	if (modules__parse(filename, modules, __read_proc_modules)) {
		delete_modules(modules);
		return -1;
	}

	return 0;
}

static int do_validate_kcore_modules(const char *filename, struct map *map,
				  struct map_groups *kmaps)
{
	struct rb_root modules = RB_ROOT;
	struct map *old_map;
	int err;

	err = read_proc_modules(filename, &modules);
	if (err)
		return err;

	old_map = map_groups__first(kmaps, map->type);
	while (old_map) {
		struct map *next = map_groups__next(old_map);
		struct module_info *mi;

		if (old_map == map || old_map->start == map->start) {
			/* The kernel map */
			old_map = next;
			continue;
		}

		/* Module must be in memory at the same address */
		mi = find_module(old_map->dso->short_name, &modules);
		if (!mi || mi->start != old_map->start) {
			err = -EINVAL;
			goto out;
		}

		old_map = next;
	}
out:
	delete_modules(&modules);
	return err;
}

/*
 * If kallsyms is referenced by name then we look for filename in the same
 * directory.
 */
static bool filename_from_kallsyms_filename(char *filename,
					    const char *base_name,
					    const char *kallsyms_filename)
{
	char *name;

	strcpy(filename, kallsyms_filename);
	name = strrchr(filename, '/');
	if (!name)
		return false;

	name += 1;

	if (!strcmp(name, "kallsyms")) {
		strcpy(name, base_name);
		return true;
	}

	return false;
}

static int validate_kcore_modules(const char *kallsyms_filename,
				  struct map *map)
{
	struct map_groups *kmaps = map__kmap(map)->kmaps;
	char modules_filename[PATH_MAX];

	if (!filename_from_kallsyms_filename(modules_filename, "modules",
					     kallsyms_filename))
		return -EINVAL;

	if (do_validate_kcore_modules(modules_filename, map, kmaps))
		return -EINVAL;

	return 0;
}

struct kcore_mapfn_data {
	struct dso *dso;
	enum map_type type;
	struct list_head maps;
};

static int kcore_mapfn(u64 start, u64 len, u64 pgoff, void *data)
{
	struct kcore_mapfn_data *md = data;
	struct map *map;

	map = map__new2(start, md->dso, md->type);
	if (map == NULL)
		return -ENOMEM;

	map->end = map->start + len;
	map->pgoff = pgoff;

	list_add(&map->node, &md->maps);

	return 0;
}

static int dso__load_kcore(struct dso *dso, struct map *map,
			   const char *kallsyms_filename)
{
	struct map_groups *kmaps = map__kmap(map)->kmaps;
	struct machine *machine = kmaps->machine;
	struct kcore_mapfn_data md;
	struct map *old_map, *new_map, *replacement_map = NULL;
	bool is_64_bit;
	int err, fd;
	char kcore_filename[PATH_MAX];
	struct symbol *sym;

	/* This function requires that the map is the kernel map */
	if (map != machine->vmlinux_maps[map->type])
		return -EINVAL;

	if (!filename_from_kallsyms_filename(kcore_filename, "kcore",
					     kallsyms_filename))
		return -EINVAL;

	/* All modules must be present at their original addresses */
	if (validate_kcore_modules(kallsyms_filename, map))
		return -EINVAL;

	md.dso = dso;
	md.type = map->type;
	INIT_LIST_HEAD(&md.maps);

	fd = open(kcore_filename, O_RDONLY);
	if (fd < 0)
		return -EINVAL;

	/* Read new maps into temporary lists */
	err = file__read_maps(fd, md.type == MAP__FUNCTION, kcore_mapfn, &md,
			      &is_64_bit);
	if (err)
		goto out_err;

	if (list_empty(&md.maps)) {
		err = -EINVAL;
		goto out_err;
	}

	/* Remove old maps */
	old_map = map_groups__first(kmaps, map->type);
	while (old_map) {
		struct map *next = map_groups__next(old_map);

		if (old_map != map)
			map_groups__remove(kmaps, old_map);
		old_map = next;
	}

	/* Find the kernel map using the first symbol */
	sym = dso__first_symbol(dso, map->type);
	list_for_each_entry(new_map, &md.maps, node) {
		if (sym && sym->start >= new_map->start &&
		    sym->start < new_map->end) {
			replacement_map = new_map;
			break;
		}
	}

	if (!replacement_map)
		replacement_map = list_entry(md.maps.next, struct map, node);

	/* Add new maps */
	while (!list_empty(&md.maps)) {
		new_map = list_entry(md.maps.next, struct map, node);
		list_del(&new_map->node);
		if (new_map == replacement_map) {
			map->start	= new_map->start;
			map->end	= new_map->end;
			map->pgoff	= new_map->pgoff;
			map->map_ip	= new_map->map_ip;
			map->unmap_ip	= new_map->unmap_ip;
			map__delete(new_map);
			/* Ensure maps are correctly ordered */
			map_groups__remove(kmaps, map);
			map_groups__insert(kmaps, map);
		} else {
			map_groups__insert(kmaps, new_map);
		}
	}

	/*
	 * Set the data type and long name so that kcore can be read via
	 * dso__data_read_addr().
	 */
	if (dso->kernel == DSO_TYPE_GUEST_KERNEL)
		dso->data_type = DSO_BINARY_TYPE__GUEST_KCORE;
	else
		dso->data_type = DSO_BINARY_TYPE__KCORE;
	dso__set_long_name(dso, strdup(kcore_filename));

	close(fd);

	if (map->type == MAP__FUNCTION)
		pr_debug("Using %s for kernel object code\n", kcore_filename);
	else
		pr_debug("Using %s for kernel data\n", kcore_filename);

	return 0;

out_err:
	while (!list_empty(&md.maps)) {
		map = list_entry(md.maps.next, struct map, node);
		list_del(&map->node);
		map__delete(map);
	}
	close(fd);
	return -EINVAL;
}

int dso__load_kallsyms(struct dso *dso, const char *filename,
		       struct map *map, symbol_filter_t filter)
{
	if (symbol__restricted_filename(filename, "/proc/kallsyms"))
		return -1;

	if (dso__load_all_kallsyms(dso, filename, map) < 0)
		return -1;

	symbols__fixup_duplicate(&dso->symbols[map->type]);
	symbols__fixup_end(&dso->symbols[map->type]);

	if (dso->kernel == DSO_TYPE_GUEST_KERNEL)
		dso->symtab_type = DSO_BINARY_TYPE__GUEST_KALLSYMS;
	else
		dso->symtab_type = DSO_BINARY_TYPE__KALLSYMS;

	if (!dso__load_kcore(dso, map, filename))
		return dso__split_kallsyms_for_kcore(dso, map, filter);
	else
		return dso__split_kallsyms(dso, map, filter);
}

static int dso__load_perf_map(struct dso *dso, struct map *map,
			      symbol_filter_t filter)
{
	char *line = NULL;
	size_t n;
	FILE *file;
	int nr_syms = 0;

	file = fopen(dso->long_name, "r");
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

		sym = symbol__new(start, size, STB_GLOBAL, line + len);

		if (sym == NULL)
			goto out_delete_line;

		if (filter && filter(map, sym))
			symbol__delete(sym);
		else {
			symbols__insert(&dso->symbols[map->type], sym);
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

int dso__load(struct dso *dso, struct map *map, symbol_filter_t filter)
{
	char *name;
	int ret = -1;
	u_int i;
	struct machine *machine;
	char *root_dir = (char *) "";
	int ss_pos = 0;
	struct symsrc ss_[2];
	struct symsrc *syms_ss = NULL, *runtime_ss = NULL;

	dso__set_loaded(dso, map->type);

	if (dso->kernel == DSO_TYPE_KERNEL)
		return dso__load_kernel_sym(dso, map, filter);
	else if (dso->kernel == DSO_TYPE_GUEST_KERNEL)
		return dso__load_guest_kernel_sym(dso, map, filter);

	if (map->groups && map->groups->machine)
		machine = map->groups->machine;
	else
		machine = NULL;

	dso->adjust_symbols = 0;

	if (strncmp(dso->name, "/tmp/perf-", 10) == 0) {
		struct stat st;

		if (lstat(dso->name, &st) < 0)
			return -1;

		if (st.st_uid && (st.st_uid != geteuid())) {
			pr_warning("File %s not owned by current user or root, "
				"ignoring it.\n", dso->name);
			return -1;
		}

		ret = dso__load_perf_map(dso, map, filter);
		dso->symtab_type = ret > 0 ? DSO_BINARY_TYPE__JAVA_JIT :
					     DSO_BINARY_TYPE__NOT_FOUND;
		return ret;
	}

	if (machine)
		root_dir = machine->root_dir;

	name = malloc(PATH_MAX);
	if (!name)
		return -1;

	/* Iterate over candidate debug images.
	 * Keep track of "interesting" ones (those which have a symtab, dynsym,
	 * and/or opd section) for processing.
	 */
	for (i = 0; i < DSO_BINARY_TYPE__SYMTAB_CNT; i++) {
		struct symsrc *ss = &ss_[ss_pos];
		bool next_slot = false;

		enum dso_binary_type symtab_type = binary_type_symtab[i];

		if (dso__binary_type_file(dso, symtab_type,
					  root_dir, name, PATH_MAX))
			continue;

		/* Name is now the name of the next image to try */
		if (symsrc__init(ss, dso, name, symtab_type) < 0)
			continue;

		if (!syms_ss && symsrc__has_symtab(ss)) {
			syms_ss = ss;
			next_slot = true;
		}

		if (!runtime_ss && symsrc__possibly_runtime(ss)) {
			runtime_ss = ss;
			next_slot = true;
		}

		if (next_slot) {
			ss_pos++;

			if (syms_ss && runtime_ss)
				break;
		}

	}

	if (!runtime_ss && !syms_ss)
		goto out_free;

	if (runtime_ss && !syms_ss) {
		syms_ss = runtime_ss;
	}

	/* We'll have to hope for the best */
	if (!runtime_ss && syms_ss)
		runtime_ss = syms_ss;

	if (syms_ss) {
		int km;

		km = dso->symtab_type == DSO_BINARY_TYPE__SYSTEM_PATH_KMODULE ||
		     dso->symtab_type == DSO_BINARY_TYPE__GUEST_KMODULE;
		ret = dso__load_sym(dso, map, syms_ss, runtime_ss, filter, km);
	} else {
		ret = -1;
	}

	if (ret > 0) {
		int nr_plt;

		nr_plt = dso__synthesize_plt_symbols(dso, runtime_ss, map, filter);
		if (nr_plt > 0)
			ret += nr_plt;
	}

	for (; ss_pos > 0; ss_pos--)
		symsrc__destroy(&ss_[ss_pos - 1]);
out_free:
	free(name);
	if (ret < 0 && strstr(dso->name, " (deleted)") != NULL)
		return 0;
	return ret;
}

struct map *map_groups__find_by_name(struct map_groups *mg,
				     enum map_type type, const char *name)
{
	struct rb_node *nd;

	for (nd = rb_first(&mg->maps[type]); nd; nd = rb_next(nd)) {
		struct map *map = rb_entry(nd, struct map, rb_node);

		if (map->dso && strcmp(map->dso->short_name, name) == 0)
			return map;
	}

	return NULL;
}

int dso__load_vmlinux(struct dso *dso, struct map *map,
		      const char *vmlinux, symbol_filter_t filter)
{
	int err = -1;
	struct symsrc ss;
	char symfs_vmlinux[PATH_MAX];
	enum dso_binary_type symtab_type;

	if (vmlinux[0] == '/')
		snprintf(symfs_vmlinux, sizeof(symfs_vmlinux), "%s", vmlinux);
	else
		snprintf(symfs_vmlinux, sizeof(symfs_vmlinux), "%s%s",
			 symbol_conf.symfs, vmlinux);

	if (dso->kernel == DSO_TYPE_GUEST_KERNEL)
		symtab_type = DSO_BINARY_TYPE__GUEST_VMLINUX;
	else
		symtab_type = DSO_BINARY_TYPE__VMLINUX;

	if (symsrc__init(&ss, dso, symfs_vmlinux, symtab_type))
		return -1;

	err = dso__load_sym(dso, map, &ss, &ss, filter, 0);
	symsrc__destroy(&ss);

	if (err > 0) {
		if (dso->kernel == DSO_TYPE_GUEST_KERNEL)
			dso->data_type = DSO_BINARY_TYPE__GUEST_VMLINUX;
		else
			dso->data_type = DSO_BINARY_TYPE__VMLINUX;
		dso__set_long_name(dso, (char *)vmlinux);
		dso__set_loaded(dso, map->type);
		pr_debug("Using %s for symbols\n", symfs_vmlinux);
	}

	return err;
}

int dso__load_vmlinux_path(struct dso *dso, struct map *map,
			   symbol_filter_t filter)
{
	int i, err = 0;
	char *filename;

	pr_debug("Looking at the vmlinux_path (%d entries long)\n",
		 vmlinux_path__nr_entries + 1);

	filename = dso__build_id_filename(dso, NULL, 0);
	if (filename != NULL) {
		err = dso__load_vmlinux(dso, map, filename, filter);
		if (err > 0) {
			dso->lname_alloc = 1;
			goto out;
		}
		free(filename);
	}

	for (i = 0; i < vmlinux_path__nr_entries; ++i) {
		err = dso__load_vmlinux(dso, map, vmlinux_path[i], filter);
		if (err > 0) {
			dso__set_long_name(dso, strdup(vmlinux_path[i]));
			dso->lname_alloc = 1;
			break;
		}
	}
out:
	return err;
}

static int dso__load_kernel_sym(struct dso *dso, struct map *map,
				symbol_filter_t filter)
{
	int err;
	const char *kallsyms_filename = NULL;
	char *kallsyms_allocated_filename = NULL;
	/*
	 * Step 1: if the user specified a kallsyms or vmlinux filename, use
	 * it and only it, reporting errors to the user if it cannot be used.
	 *
	 * For instance, try to analyse an ARM perf.data file _without_ a
	 * build-id, or if the user specifies the wrong path to the right
	 * vmlinux file, obviously we can't fallback to another vmlinux (a
	 * x86_86 one, on the machine where analysis is being performed, say),
	 * or worse, /proc/kallsyms.
	 *
	 * If the specified file _has_ a build-id and there is a build-id
	 * section in the perf.data file, we will still do the expected
	 * validation in dso__load_vmlinux and will bail out if they don't
	 * match.
	 */
	if (symbol_conf.kallsyms_name != NULL) {
		kallsyms_filename = symbol_conf.kallsyms_name;
		goto do_kallsyms;
	}

	if (!symbol_conf.ignore_vmlinux && symbol_conf.vmlinux_name != NULL) {
		err = dso__load_vmlinux(dso, map,
					symbol_conf.vmlinux_name, filter);
		if (err > 0) {
			dso__set_long_name(dso,
					   strdup(symbol_conf.vmlinux_name));
			dso->lname_alloc = 1;
			return err;
		}
		return err;
	}

	if (!symbol_conf.ignore_vmlinux && vmlinux_path != NULL) {
		err = dso__load_vmlinux_path(dso, map, filter);
		if (err > 0)
			return err;
	}

	/* do not try local files if a symfs was given */
	if (symbol_conf.symfs[0] != 0)
		return -1;

	/*
	 * Say the kernel DSO was created when processing the build-id header table,
	 * we have a build-id, so check if it is the same as the running kernel,
	 * using it if it is.
	 */
	if (dso->has_build_id) {
		u8 kallsyms_build_id[BUILD_ID_SIZE];
		char sbuild_id[BUILD_ID_SIZE * 2 + 1];

		if (sysfs__read_build_id("/sys/kernel/notes", kallsyms_build_id,
					 sizeof(kallsyms_build_id)) == 0) {
			if (dso__build_id_equal(dso, kallsyms_build_id)) {
				kallsyms_filename = "/proc/kallsyms";
				goto do_kallsyms;
			}
		}
		/*
		 * Now look if we have it on the build-id cache in
		 * $HOME/.debug/[kernel.kallsyms].
		 */
		build_id__sprintf(dso->build_id, sizeof(dso->build_id),
				  sbuild_id);

		if (asprintf(&kallsyms_allocated_filename,
			     "%s/.debug/[kernel.kallsyms]/%s",
			     getenv("HOME"), sbuild_id) == -1) {
			pr_err("Not enough memory for kallsyms file lookup\n");
			return -1;
		}

		kallsyms_filename = kallsyms_allocated_filename;

		if (access(kallsyms_filename, F_OK)) {
			pr_err("No kallsyms or vmlinux with build-id %s "
			       "was found\n", sbuild_id);
			free(kallsyms_allocated_filename);
			return -1;
		}
	} else {
		/*
		 * Last resort, if we don't have a build-id and couldn't find
		 * any vmlinux file, try the running kernel kallsyms table.
		 */
		kallsyms_filename = "/proc/kallsyms";
	}

do_kallsyms:
	err = dso__load_kallsyms(dso, kallsyms_filename, map, filter);
	if (err > 0)
		pr_debug("Using %s for symbols\n", kallsyms_filename);
	free(kallsyms_allocated_filename);

	if (err > 0 && !dso__is_kcore(dso)) {
		dso__set_long_name(dso, strdup("[kernel.kallsyms]"));
		map__fixup_start(map);
		map__fixup_end(map);
	}

	return err;
}

static int dso__load_guest_kernel_sym(struct dso *dso, struct map *map,
				      symbol_filter_t filter)
{
	int err;
	const char *kallsyms_filename = NULL;
	struct machine *machine;
	char path[PATH_MAX];

	if (!map->groups) {
		pr_debug("Guest kernel map hasn't the point to groups\n");
		return -1;
	}
	machine = map->groups->machine;

	if (machine__is_default_guest(machine)) {
		/*
		 * if the user specified a vmlinux filename, use it and only
		 * it, reporting errors to the user if it cannot be used.
		 * Or use file guest_kallsyms inputted by user on commandline
		 */
		if (symbol_conf.default_guest_vmlinux_name != NULL) {
			err = dso__load_vmlinux(dso, map,
				symbol_conf.default_guest_vmlinux_name, filter);
			return err;
		}

		kallsyms_filename = symbol_conf.default_guest_kallsyms;
		if (!kallsyms_filename)
			return -1;
	} else {
		sprintf(path, "%s/proc/kallsyms", machine->root_dir);
		kallsyms_filename = path;
	}

	err = dso__load_kallsyms(dso, kallsyms_filename, map, filter);
	if (err > 0)
		pr_debug("Using %s for symbols\n", kallsyms_filename);
	if (err > 0 && !dso__is_kcore(dso)) {
		machine__mmap_name(machine, path, sizeof(path));
		dso__set_long_name(dso, strdup(path));
		map__fixup_start(map);
		map__fixup_end(map);
	}

	return err;
}

static void vmlinux_path__exit(void)
{
	while (--vmlinux_path__nr_entries >= 0) {
		free(vmlinux_path[vmlinux_path__nr_entries]);
		vmlinux_path[vmlinux_path__nr_entries] = NULL;
	}

	free(vmlinux_path);
	vmlinux_path = NULL;
}

static int vmlinux_path__init(void)
{
	struct utsname uts;
	char bf[PATH_MAX];

	vmlinux_path = malloc(sizeof(char *) * 5);
	if (vmlinux_path == NULL)
		return -1;

	vmlinux_path[vmlinux_path__nr_entries] = strdup("vmlinux");
	if (vmlinux_path[vmlinux_path__nr_entries] == NULL)
		goto out_fail;
	++vmlinux_path__nr_entries;
	vmlinux_path[vmlinux_path__nr_entries] = strdup("/boot/vmlinux");
	if (vmlinux_path[vmlinux_path__nr_entries] == NULL)
		goto out_fail;
	++vmlinux_path__nr_entries;

	/* only try running kernel version if no symfs was given */
	if (symbol_conf.symfs[0] != 0)
		return 0;

	if (uname(&uts) < 0)
		return -1;

	snprintf(bf, sizeof(bf), "/boot/vmlinux-%s", uts.release);
	vmlinux_path[vmlinux_path__nr_entries] = strdup(bf);
	if (vmlinux_path[vmlinux_path__nr_entries] == NULL)
		goto out_fail;
	++vmlinux_path__nr_entries;
	snprintf(bf, sizeof(bf), "/lib/modules/%s/build/vmlinux", uts.release);
	vmlinux_path[vmlinux_path__nr_entries] = strdup(bf);
	if (vmlinux_path[vmlinux_path__nr_entries] == NULL)
		goto out_fail;
	++vmlinux_path__nr_entries;
	snprintf(bf, sizeof(bf), "/usr/lib/debug/lib/modules/%s/vmlinux",
		 uts.release);
	vmlinux_path[vmlinux_path__nr_entries] = strdup(bf);
	if (vmlinux_path[vmlinux_path__nr_entries] == NULL)
		goto out_fail;
	++vmlinux_path__nr_entries;

	return 0;

out_fail:
	vmlinux_path__exit();
	return -1;
}

static int setup_list(struct strlist **list, const char *list_str,
		      const char *list_name)
{
	if (list_str == NULL)
		return 0;

	*list = strlist__new(true, list_str);
	if (!*list) {
		pr_err("problems parsing %s list\n", list_name);
		return -1;
	}
	return 0;
}

static bool symbol__read_kptr_restrict(void)
{
	bool value = false;

	if (geteuid() != 0) {
		FILE *fp = fopen("/proc/sys/kernel/kptr_restrict", "r");
		if (fp != NULL) {
			char line[8];

			if (fgets(line, sizeof(line), fp) != NULL)
				value = atoi(line) != 0;

			fclose(fp);
		}
	}

	return value;
}

int symbol__init(void)
{
	const char *symfs;

	if (symbol_conf.initialized)
		return 0;

	symbol_conf.priv_size = PERF_ALIGN(symbol_conf.priv_size, sizeof(u64));

	symbol__elf_init();

	if (symbol_conf.sort_by_name)
		symbol_conf.priv_size += (sizeof(struct symbol_name_rb_node) -
					  sizeof(struct symbol));

	if (symbol_conf.try_vmlinux_path && vmlinux_path__init() < 0)
		return -1;

	if (symbol_conf.field_sep && *symbol_conf.field_sep == '.') {
		pr_err("'.' is the only non valid --field-separator argument\n");
		return -1;
	}

	if (setup_list(&symbol_conf.dso_list,
		       symbol_conf.dso_list_str, "dso") < 0)
		return -1;

	if (setup_list(&symbol_conf.comm_list,
		       symbol_conf.comm_list_str, "comm") < 0)
		goto out_free_dso_list;

	if (setup_list(&symbol_conf.sym_list,
		       symbol_conf.sym_list_str, "symbol") < 0)
		goto out_free_comm_list;

	/*
	 * A path to symbols of "/" is identical to ""
	 * reset here for simplicity.
	 */
	symfs = realpath(symbol_conf.symfs, NULL);
	if (symfs == NULL)
		symfs = symbol_conf.symfs;
	if (strcmp(symfs, "/") == 0)
		symbol_conf.symfs = "";
	if (symfs != symbol_conf.symfs)
		free((void *)symfs);

	symbol_conf.kptr_restrict = symbol__read_kptr_restrict();

	symbol_conf.initialized = true;
	return 0;

out_free_comm_list:
	strlist__delete(symbol_conf.comm_list);
out_free_dso_list:
	strlist__delete(symbol_conf.dso_list);
	return -1;
}

void symbol__exit(void)
{
	if (!symbol_conf.initialized)
		return;
	strlist__delete(symbol_conf.sym_list);
	strlist__delete(symbol_conf.dso_list);
	strlist__delete(symbol_conf.comm_list);
	vmlinux_path__exit();
	symbol_conf.sym_list = symbol_conf.dso_list = symbol_conf.comm_list = NULL;
	symbol_conf.initialized = false;
}
