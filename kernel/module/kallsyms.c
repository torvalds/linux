// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Module kallsyms support
 *
 * Copyright (C) 2010 Rusty Russell
 */

#include <linux/module.h>
#include <linux/module_symbol.h>
#include <linux/kallsyms.h>
#include <linux/buildid.h>
#include <linux/bsearch.h>
#include "internal.h"

/* Lookup exported symbol in given range of kernel_symbols */
static const struct kernel_symbol *lookup_exported_symbol(const char *name,
							  const struct kernel_symbol *start,
							  const struct kernel_symbol *stop)
{
	return bsearch(name, start, stop - start,
			sizeof(struct kernel_symbol), cmp_name);
}

static int is_exported(const char *name, unsigned long value,
		       const struct module *mod)
{
	const struct kernel_symbol *ks;

	if (!mod)
		ks = lookup_exported_symbol(name, __start___ksymtab, __stop___ksymtab);
	else
		ks = lookup_exported_symbol(name, mod->syms, mod->syms + mod->num_syms);

	return ks && kernel_symbol_value(ks) == value;
}

/* As per nm */
static char elf_type(const Elf_Sym *sym, const struct load_info *info)
{
	const Elf_Shdr *sechdrs = info->sechdrs;

	if (ELF_ST_BIND(sym->st_info) == STB_WEAK) {
		if (ELF_ST_TYPE(sym->st_info) == STT_OBJECT)
			return 'v';
		else
			return 'w';
	}
	if (sym->st_shndx == SHN_UNDEF)
		return 'U';
	if (sym->st_shndx == SHN_ABS || sym->st_shndx == info->index.pcpu)
		return 'a';
	if (sym->st_shndx >= SHN_LORESERVE)
		return '?';
	if (sechdrs[sym->st_shndx].sh_flags & SHF_EXECINSTR)
		return 't';
	if (sechdrs[sym->st_shndx].sh_flags & SHF_ALLOC &&
	    sechdrs[sym->st_shndx].sh_type != SHT_NOBITS) {
		if (!(sechdrs[sym->st_shndx].sh_flags & SHF_WRITE))
			return 'r';
		else if (sechdrs[sym->st_shndx].sh_flags & ARCH_SHF_SMALL)
			return 'g';
		else
			return 'd';
	}
	if (sechdrs[sym->st_shndx].sh_type == SHT_NOBITS) {
		if (sechdrs[sym->st_shndx].sh_flags & ARCH_SHF_SMALL)
			return 's';
		else
			return 'b';
	}
	if (strstarts(info->secstrings + sechdrs[sym->st_shndx].sh_name,
		      ".debug")) {
		return 'n';
	}
	return '?';
}

static bool is_core_symbol(const Elf_Sym *src, const Elf_Shdr *sechdrs,
			   unsigned int shnum, unsigned int pcpundx)
{
	const Elf_Shdr *sec;
	enum mod_mem_type type;

	if (src->st_shndx == SHN_UNDEF ||
	    src->st_shndx >= shnum ||
	    !src->st_name)
		return false;

#ifdef CONFIG_KALLSYMS_ALL
	if (src->st_shndx == pcpundx)
		return true;
#endif

	sec = sechdrs + src->st_shndx;
	type = sec->sh_entsize >> SH_ENTSIZE_TYPE_SHIFT;
	if (!(sec->sh_flags & SHF_ALLOC)
#ifndef CONFIG_KALLSYMS_ALL
	    || !(sec->sh_flags & SHF_EXECINSTR)
#endif
	    || mod_mem_type_is_init(type))
		return false;

	return true;
}

/*
 * We only allocate and copy the strings needed by the parts of symtab
 * we keep.  This is simple, but has the effect of making multiple
 * copies of duplicates.  We could be more sophisticated, see
 * linux-kernel thread starting with
 * <73defb5e4bca04a6431392cc341112b1@localhost>.
 */
void layout_symtab(struct module *mod, struct load_info *info)
{
	Elf_Shdr *symsect = info->sechdrs + info->index.sym;
	Elf_Shdr *strsect = info->sechdrs + info->index.str;
	const Elf_Sym *src;
	unsigned int i, nsrc, ndst, strtab_size = 0;
	struct module_memory *mod_mem_data = &mod->mem[MOD_DATA];
	struct module_memory *mod_mem_init_data = &mod->mem[MOD_INIT_DATA];

	/* Put symbol section at end of init part of module. */
	symsect->sh_flags |= SHF_ALLOC;
	symsect->sh_entsize = module_get_offset_and_type(mod, MOD_INIT_DATA,
							 symsect, info->index.sym);
	pr_debug("\t%s\n", info->secstrings + symsect->sh_name);

	src = (void *)info->hdr + symsect->sh_offset;
	nsrc = symsect->sh_size / sizeof(*src);

	/* Compute total space required for the core symbols' strtab. */
	for (ndst = i = 0; i < nsrc; i++) {
		if (i == 0 || is_livepatch_module(mod) ||
		    is_core_symbol(src + i, info->sechdrs, info->hdr->e_shnum,
				   info->index.pcpu)) {
			strtab_size += strlen(&info->strtab[src[i].st_name]) + 1;
			ndst++;
		}
	}

	/* Append room for core symbols at end of core part. */
	info->symoffs = ALIGN(mod_mem_data->size, symsect->sh_addralign ?: 1);
	info->stroffs = mod_mem_data->size = info->symoffs + ndst * sizeof(Elf_Sym);
	mod_mem_data->size += strtab_size;
	/* Note add_kallsyms() computes strtab_size as core_typeoffs - stroffs */
	info->core_typeoffs = mod_mem_data->size;
	mod_mem_data->size += ndst * sizeof(char);

	/* Put string table section at end of init part of module. */
	strsect->sh_flags |= SHF_ALLOC;
	strsect->sh_entsize = module_get_offset_and_type(mod, MOD_INIT_DATA,
							 strsect, info->index.str);
	pr_debug("\t%s\n", info->secstrings + strsect->sh_name);

	/* We'll tack temporary mod_kallsyms on the end. */
	mod_mem_init_data->size = ALIGN(mod_mem_init_data->size,
					__alignof__(struct mod_kallsyms));
	info->mod_kallsyms_init_off = mod_mem_init_data->size;

	mod_mem_init_data->size += sizeof(struct mod_kallsyms);
	info->init_typeoffs = mod_mem_init_data->size;
	mod_mem_init_data->size += nsrc * sizeof(char);
}

/*
 * We use the full symtab and strtab which layout_symtab arranged to
 * be appended to the init section.  Later we switch to the cut-down
 * core-only ones.
 */
void add_kallsyms(struct module *mod, const struct load_info *info)
{
	unsigned int i, ndst;
	const Elf_Sym *src;
	Elf_Sym *dst;
	char *s;
	Elf_Shdr *symsec = &info->sechdrs[info->index.sym];
	unsigned long strtab_size;
	void *data_base = mod->mem[MOD_DATA].base;
	void *init_data_base = mod->mem[MOD_INIT_DATA].base;

	/* Set up to point into init section. */
	mod->kallsyms = (void __rcu *)init_data_base +
		info->mod_kallsyms_init_off;

	rcu_read_lock();
	/* The following is safe since this pointer cannot change */
	rcu_dereference(mod->kallsyms)->symtab = (void *)symsec->sh_addr;
	rcu_dereference(mod->kallsyms)->num_symtab = symsec->sh_size / sizeof(Elf_Sym);
	/* Make sure we get permanent strtab: don't use info->strtab. */
	rcu_dereference(mod->kallsyms)->strtab =
		(void *)info->sechdrs[info->index.str].sh_addr;
	rcu_dereference(mod->kallsyms)->typetab = init_data_base + info->init_typeoffs;

	/*
	 * Now populate the cut down core kallsyms for after init
	 * and set types up while we still have access to sections.
	 */
	mod->core_kallsyms.symtab = dst = data_base + info->symoffs;
	mod->core_kallsyms.strtab = s = data_base + info->stroffs;
	mod->core_kallsyms.typetab = data_base + info->core_typeoffs;
	strtab_size = info->core_typeoffs - info->stroffs;
	src = rcu_dereference(mod->kallsyms)->symtab;
	for (ndst = i = 0; i < rcu_dereference(mod->kallsyms)->num_symtab; i++) {
		rcu_dereference(mod->kallsyms)->typetab[i] = elf_type(src + i, info);
		if (i == 0 || is_livepatch_module(mod) ||
		    is_core_symbol(src + i, info->sechdrs, info->hdr->e_shnum,
				   info->index.pcpu)) {
			ssize_t ret;

			mod->core_kallsyms.typetab[ndst] =
			    rcu_dereference(mod->kallsyms)->typetab[i];
			dst[ndst] = src[i];
			dst[ndst++].st_name = s - mod->core_kallsyms.strtab;
			ret = strscpy(s,
				      &rcu_dereference(mod->kallsyms)->strtab[src[i].st_name],
				      strtab_size);
			if (ret < 0)
				break;
			s += ret + 1;
			strtab_size -= ret + 1;
		}
	}
	rcu_read_unlock();
	mod->core_kallsyms.num_symtab = ndst;
}

#if IS_ENABLED(CONFIG_STACKTRACE_BUILD_ID)
void init_build_id(struct module *mod, const struct load_info *info)
{
	const Elf_Shdr *sechdr;
	unsigned int i;

	for (i = 0; i < info->hdr->e_shnum; i++) {
		sechdr = &info->sechdrs[i];
		if (!sect_empty(sechdr) && sechdr->sh_type == SHT_NOTE &&
		    !build_id_parse_buf((void *)sechdr->sh_addr, mod->build_id,
					sechdr->sh_size))
			break;
	}
}
#else
void init_build_id(struct module *mod, const struct load_info *info)
{
}
#endif

static const char *kallsyms_symbol_name(struct mod_kallsyms *kallsyms, unsigned int symnum)
{
	return kallsyms->strtab + kallsyms->symtab[symnum].st_name;
}

/*
 * Given a module and address, find the corresponding symbol and return its name
 * while providing its size and offset if needed.
 */
static const char *find_kallsyms_symbol(struct module *mod,
					unsigned long addr,
					unsigned long *size,
					unsigned long *offset)
{
	unsigned int i, best = 0;
	unsigned long nextval, bestval;
	struct mod_kallsyms *kallsyms = rcu_dereference_sched(mod->kallsyms);
	struct module_memory *mod_mem;

	/* At worse, next value is at end of module */
	if (within_module_init(addr, mod))
		mod_mem = &mod->mem[MOD_INIT_TEXT];
	else
		mod_mem = &mod->mem[MOD_TEXT];

	nextval = (unsigned long)mod_mem->base + mod_mem->size;

	bestval = kallsyms_symbol_value(&kallsyms->symtab[best]);

	/*
	 * Scan for closest preceding symbol, and next symbol. (ELF
	 * starts real symbols at 1).
	 */
	for (i = 1; i < kallsyms->num_symtab; i++) {
		const Elf_Sym *sym = &kallsyms->symtab[i];
		unsigned long thisval = kallsyms_symbol_value(sym);

		if (sym->st_shndx == SHN_UNDEF)
			continue;

		/*
		 * We ignore unnamed symbols: they're uninformative
		 * and inserted at a whim.
		 */
		if (*kallsyms_symbol_name(kallsyms, i) == '\0' ||
		    is_mapping_symbol(kallsyms_symbol_name(kallsyms, i), IS_ENABLED(CONFIG_RISCV)))
			continue;

		if (thisval <= addr && thisval > bestval) {
			best = i;
			bestval = thisval;
		}
		if (thisval > addr && thisval < nextval)
			nextval = thisval;
	}

	if (!best)
		return NULL;

	if (size)
		*size = nextval - bestval;
	if (offset)
		*offset = addr - bestval;

	return kallsyms_symbol_name(kallsyms, best);
}

void * __weak dereference_module_function_descriptor(struct module *mod,
						     void *ptr)
{
	return ptr;
}

/*
 * For kallsyms to ask for address resolution.  NULL means not found.  Careful
 * not to lock to avoid deadlock on oopses, simply disable preemption.
 */
const char *module_address_lookup(unsigned long addr,
				  unsigned long *size,
			    unsigned long *offset,
			    char **modname,
			    const unsigned char **modbuildid,
			    char *namebuf)
{
	const char *ret = NULL;
	struct module *mod;

	preempt_disable();
	mod = __module_address(addr);
	if (mod) {
		if (modname)
			*modname = mod->name;
		if (modbuildid) {
#if IS_ENABLED(CONFIG_STACKTRACE_BUILD_ID)
			*modbuildid = mod->build_id;
#else
			*modbuildid = NULL;
#endif
		}

		ret = find_kallsyms_symbol(mod, addr, size, offset);
	}
	/* Make a copy in here where it's safe */
	if (ret) {
		strncpy(namebuf, ret, KSYM_NAME_LEN - 1);
		ret = namebuf;
	}
	preempt_enable();

	return ret;
}

int lookup_module_symbol_name(unsigned long addr, char *symname)
{
	struct module *mod;

	preempt_disable();
	list_for_each_entry_rcu(mod, &modules, list) {
		if (mod->state == MODULE_STATE_UNFORMED)
			continue;
		if (within_module(addr, mod)) {
			const char *sym;

			sym = find_kallsyms_symbol(mod, addr, NULL, NULL);
			if (!sym)
				goto out;

			strscpy(symname, sym, KSYM_NAME_LEN);
			preempt_enable();
			return 0;
		}
	}
out:
	preempt_enable();
	return -ERANGE;
}

int module_get_kallsym(unsigned int symnum, unsigned long *value, char *type,
		       char *name, char *module_name, int *exported)
{
	struct module *mod;

	preempt_disable();
	list_for_each_entry_rcu(mod, &modules, list) {
		struct mod_kallsyms *kallsyms;

		if (mod->state == MODULE_STATE_UNFORMED)
			continue;
		kallsyms = rcu_dereference_sched(mod->kallsyms);
		if (symnum < kallsyms->num_symtab) {
			const Elf_Sym *sym = &kallsyms->symtab[symnum];

			*value = kallsyms_symbol_value(sym);
			*type = kallsyms->typetab[symnum];
			strscpy(name, kallsyms_symbol_name(kallsyms, symnum), KSYM_NAME_LEN);
			strscpy(module_name, mod->name, MODULE_NAME_LEN);
			*exported = is_exported(name, *value, mod);
			preempt_enable();
			return 0;
		}
		symnum -= kallsyms->num_symtab;
	}
	preempt_enable();
	return -ERANGE;
}

/* Given a module and name of symbol, find and return the symbol's value */
static unsigned long __find_kallsyms_symbol_value(struct module *mod, const char *name)
{
	unsigned int i;
	struct mod_kallsyms *kallsyms = rcu_dereference_sched(mod->kallsyms);

	for (i = 0; i < kallsyms->num_symtab; i++) {
		const Elf_Sym *sym = &kallsyms->symtab[i];

		if (strcmp(name, kallsyms_symbol_name(kallsyms, i)) == 0 &&
		    sym->st_shndx != SHN_UNDEF)
			return kallsyms_symbol_value(sym);
	}
	return 0;
}

static unsigned long __module_kallsyms_lookup_name(const char *name)
{
	struct module *mod;
	char *colon;

	colon = strnchr(name, MODULE_NAME_LEN, ':');
	if (colon) {
		mod = find_module_all(name, colon - name, false);
		if (mod)
			return __find_kallsyms_symbol_value(mod, colon + 1);
		return 0;
	}

	list_for_each_entry_rcu(mod, &modules, list) {
		unsigned long ret;

		if (mod->state == MODULE_STATE_UNFORMED)
			continue;
		ret = __find_kallsyms_symbol_value(mod, name);
		if (ret)
			return ret;
	}
	return 0;
}

/* Look for this name: can be of form module:name. */
unsigned long module_kallsyms_lookup_name(const char *name)
{
	unsigned long ret;

	/* Don't lock: we're in enough trouble already. */
	preempt_disable();
	ret = __module_kallsyms_lookup_name(name);
	preempt_enable();
	return ret;
}

unsigned long find_kallsyms_symbol_value(struct module *mod, const char *name)
{
	unsigned long ret;

	preempt_disable();
	ret = __find_kallsyms_symbol_value(mod, name);
	preempt_enable();
	return ret;
}

int module_kallsyms_on_each_symbol(const char *modname,
				   int (*fn)(void *, const char *, unsigned long),
				   void *data)
{
	struct module *mod;
	unsigned int i;
	int ret = 0;

	mutex_lock(&module_mutex);
	list_for_each_entry(mod, &modules, list) {
		struct mod_kallsyms *kallsyms;

		if (mod->state == MODULE_STATE_UNFORMED)
			continue;

		if (modname && strcmp(modname, mod->name))
			continue;

		/* Use rcu_dereference_sched() to remain compliant with the sparse tool */
		preempt_disable();
		kallsyms = rcu_dereference_sched(mod->kallsyms);
		preempt_enable();

		for (i = 0; i < kallsyms->num_symtab; i++) {
			const Elf_Sym *sym = &kallsyms->symtab[i];

			if (sym->st_shndx == SHN_UNDEF)
				continue;

			ret = fn(data, kallsyms_symbol_name(kallsyms, i),
				 kallsyms_symbol_value(sym));
			if (ret != 0)
				goto out;
		}

		/*
		 * The given module is found, the subsequent modules do not
		 * need to be compared.
		 */
		if (modname)
			break;
	}
out:
	mutex_unlock(&module_mutex);
	return ret;
}
