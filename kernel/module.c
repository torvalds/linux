/* Rewritten by Rusty Russell, on the backs of many others...
   Copyright (C) 2002 Richard Henderson
   Copyright (C) 2001 Rusty Russell, 2002 Rusty Russell IBM.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleloader.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/elf.h>
#include <linux/seq_file.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <linux/rcupdate.h>
#include <linux/capability.h>
#include <linux/cpu.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/vermagic.h>
#include <linux/notifier.h>
#include <linux/stop_machine.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <asm/cacheflush.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt , a...)
#endif

#ifndef ARCH_SHF_SMALL
#define ARCH_SHF_SMALL 0
#endif

/* If this is set, the section belongs in the init part of the module */
#define INIT_OFFSET_MASK (1UL << (BITS_PER_LONG-1))

/* Protects module list */
static DEFINE_SPINLOCK(modlist_lock);

/* List of modules, protected by module_mutex AND modlist_lock */
static DECLARE_MUTEX(module_mutex);
static LIST_HEAD(modules);

static DECLARE_MUTEX(notify_mutex);
static struct notifier_block * module_notify_list;

int register_module_notifier(struct notifier_block * nb)
{
	int err;
	down(&notify_mutex);
	err = notifier_chain_register(&module_notify_list, nb);
	up(&notify_mutex);
	return err;
}
EXPORT_SYMBOL(register_module_notifier);

int unregister_module_notifier(struct notifier_block * nb)
{
	int err;
	down(&notify_mutex);
	err = notifier_chain_unregister(&module_notify_list, nb);
	up(&notify_mutex);
	return err;
}
EXPORT_SYMBOL(unregister_module_notifier);

/* We require a truly strong try_module_get() */
static inline int strong_try_module_get(struct module *mod)
{
	if (mod && mod->state == MODULE_STATE_COMING)
		return 0;
	return try_module_get(mod);
}

/* A thread that wants to hold a reference to a module only while it
 * is running can call ths to safely exit.
 * nfsd and lockd use this.
 */
void __module_put_and_exit(struct module *mod, long code)
{
	module_put(mod);
	do_exit(code);
}
EXPORT_SYMBOL(__module_put_and_exit);
	
/* Find a module section: 0 means not found. */
static unsigned int find_sec(Elf_Ehdr *hdr,
			     Elf_Shdr *sechdrs,
			     const char *secstrings,
			     const char *name)
{
	unsigned int i;

	for (i = 1; i < hdr->e_shnum; i++)
		/* Alloc bit cleared means "ignore it." */
		if ((sechdrs[i].sh_flags & SHF_ALLOC)
		    && strcmp(secstrings+sechdrs[i].sh_name, name) == 0)
			return i;
	return 0;
}

/* Provided by the linker */
extern const struct kernel_symbol __start___ksymtab[];
extern const struct kernel_symbol __stop___ksymtab[];
extern const struct kernel_symbol __start___ksymtab_gpl[];
extern const struct kernel_symbol __stop___ksymtab_gpl[];
extern const struct kernel_symbol __start___ksymtab_gpl_future[];
extern const struct kernel_symbol __stop___ksymtab_gpl_future[];
extern const unsigned long __start___kcrctab[];
extern const unsigned long __start___kcrctab_gpl[];
extern const unsigned long __start___kcrctab_gpl_future[];

#ifndef CONFIG_MODVERSIONS
#define symversion(base, idx) NULL
#else
#define symversion(base, idx) ((base) ? ((base) + (idx)) : NULL)
#endif

/* lookup symbol in given range of kernel_symbols */
static const struct kernel_symbol *lookup_symbol(const char *name,
	const struct kernel_symbol *start,
	const struct kernel_symbol *stop)
{
	const struct kernel_symbol *ks = start;
	for (; ks < stop; ks++)
		if (strcmp(ks->name, name) == 0)
			return ks;
	return NULL;
}

/* Find a symbol, return value, crc and module which owns it */
static unsigned long __find_symbol(const char *name,
				   struct module **owner,
				   const unsigned long **crc,
				   int gplok)
{
	struct module *mod;
	const struct kernel_symbol *ks;

	/* Core kernel first. */ 
	*owner = NULL;
	ks = lookup_symbol(name, __start___ksymtab, __stop___ksymtab);
	if (ks) {
		*crc = symversion(__start___kcrctab, (ks - __start___ksymtab));
		return ks->value;
	}
	if (gplok) {
		ks = lookup_symbol(name, __start___ksymtab_gpl,
					 __stop___ksymtab_gpl);
		if (ks) {
			*crc = symversion(__start___kcrctab_gpl,
					  (ks - __start___ksymtab_gpl));
			return ks->value;
		}
	}
	ks = lookup_symbol(name, __start___ksymtab_gpl_future,
				 __stop___ksymtab_gpl_future);
	if (ks) {
		if (!gplok) {
			printk(KERN_WARNING "Symbol %s is being used "
			       "by a non-GPL module, which will not "
			       "be allowed in the future\n", name);
			printk(KERN_WARNING "Please see the file "
			       "Documentation/feature-removal-schedule.txt "
			       "in the kernel source tree for more "
			       "details.\n");
		}
		*crc = symversion(__start___kcrctab_gpl_future,
				  (ks - __start___ksymtab_gpl_future));
		return ks->value;
	}

	/* Now try modules. */ 
	list_for_each_entry(mod, &modules, list) {
		*owner = mod;
		ks = lookup_symbol(name, mod->syms, mod->syms + mod->num_syms);
		if (ks) {
			*crc = symversion(mod->crcs, (ks - mod->syms));
			return ks->value;
		}

		if (gplok) {
			ks = lookup_symbol(name, mod->gpl_syms,
					   mod->gpl_syms + mod->num_gpl_syms);
			if (ks) {
				*crc = symversion(mod->gpl_crcs,
						  (ks - mod->gpl_syms));
				return ks->value;
			}
		}
		ks = lookup_symbol(name, mod->gpl_future_syms,
				   (mod->gpl_future_syms +
				    mod->num_gpl_future_syms));
		if (ks) {
			if (!gplok) {
				printk(KERN_WARNING "Symbol %s is being used "
				       "by a non-GPL module, which will not "
				       "be allowed in the future\n", name);
				printk(KERN_WARNING "Please see the file "
				       "Documentation/feature-removal-schedule.txt "
				       "in the kernel source tree for more "
				       "details.\n");
			}
			*crc = symversion(mod->gpl_future_crcs,
					  (ks - mod->gpl_future_syms));
			return ks->value;
		}
	}
	DEBUGP("Failed to find symbol %s\n", name);
 	return 0;
}

/* Find a symbol in this elf symbol table */
static unsigned long find_local_symbol(Elf_Shdr *sechdrs,
				       unsigned int symindex,
				       const char *strtab,
				       const char *name)
{
	unsigned int i;
	Elf_Sym *sym = (void *)sechdrs[symindex].sh_addr;

	/* Search (defined) internal symbols first. */
	for (i = 1; i < sechdrs[symindex].sh_size/sizeof(*sym); i++) {
		if (sym[i].st_shndx != SHN_UNDEF
		    && strcmp(name, strtab + sym[i].st_name) == 0)
			return sym[i].st_value;
	}
	return 0;
}

/* Search for module by name: must hold module_mutex. */
static struct module *find_module(const char *name)
{
	struct module *mod;

	list_for_each_entry(mod, &modules, list) {
		if (strcmp(mod->name, name) == 0)
			return mod;
	}
	return NULL;
}

#ifdef CONFIG_SMP
/* Number of blocks used and allocated. */
static unsigned int pcpu_num_used, pcpu_num_allocated;
/* Size of each block.  -ve means used. */
static int *pcpu_size;

static int split_block(unsigned int i, unsigned short size)
{
	/* Reallocation required? */
	if (pcpu_num_used + 1 > pcpu_num_allocated) {
		int *new = kmalloc(sizeof(new[0]) * pcpu_num_allocated*2,
				   GFP_KERNEL);
		if (!new)
			return 0;

		memcpy(new, pcpu_size, sizeof(new[0])*pcpu_num_allocated);
		pcpu_num_allocated *= 2;
		kfree(pcpu_size);
		pcpu_size = new;
	}

	/* Insert a new subblock */
	memmove(&pcpu_size[i+1], &pcpu_size[i],
		sizeof(pcpu_size[0]) * (pcpu_num_used - i));
	pcpu_num_used++;

	pcpu_size[i+1] -= size;
	pcpu_size[i] = size;
	return 1;
}

static inline unsigned int block_size(int val)
{
	if (val < 0)
		return -val;
	return val;
}

/* Created by linker magic */
extern char __per_cpu_start[], __per_cpu_end[];

static void *percpu_modalloc(unsigned long size, unsigned long align,
			     const char *name)
{
	unsigned long extra;
	unsigned int i;
	void *ptr;

	if (align > SMP_CACHE_BYTES) {
		printk(KERN_WARNING "%s: per-cpu alignment %li > %i\n",
		       name, align, SMP_CACHE_BYTES);
		align = SMP_CACHE_BYTES;
	}

	ptr = __per_cpu_start;
	for (i = 0; i < pcpu_num_used; ptr += block_size(pcpu_size[i]), i++) {
		/* Extra for alignment requirement. */
		extra = ALIGN((unsigned long)ptr, align) - (unsigned long)ptr;
		BUG_ON(i == 0 && extra != 0);

		if (pcpu_size[i] < 0 || pcpu_size[i] < extra + size)
			continue;

		/* Transfer extra to previous block. */
		if (pcpu_size[i-1] < 0)
			pcpu_size[i-1] -= extra;
		else
			pcpu_size[i-1] += extra;
		pcpu_size[i] -= extra;
		ptr += extra;

		/* Split block if warranted */
		if (pcpu_size[i] - size > sizeof(unsigned long))
			if (!split_block(i, size))
				return NULL;

		/* Mark allocated */
		pcpu_size[i] = -pcpu_size[i];
		return ptr;
	}

	printk(KERN_WARNING "Could not allocate %lu bytes percpu data\n",
	       size);
	return NULL;
}

static void percpu_modfree(void *freeme)
{
	unsigned int i;
	void *ptr = __per_cpu_start + block_size(pcpu_size[0]);

	/* First entry is core kernel percpu data. */
	for (i = 1; i < pcpu_num_used; ptr += block_size(pcpu_size[i]), i++) {
		if (ptr == freeme) {
			pcpu_size[i] = -pcpu_size[i];
			goto free;
		}
	}
	BUG();

 free:
	/* Merge with previous? */
	if (pcpu_size[i-1] >= 0) {
		pcpu_size[i-1] += pcpu_size[i];
		pcpu_num_used--;
		memmove(&pcpu_size[i], &pcpu_size[i+1],
			(pcpu_num_used - i) * sizeof(pcpu_size[0]));
		i--;
	}
	/* Merge with next? */
	if (i+1 < pcpu_num_used && pcpu_size[i+1] >= 0) {
		pcpu_size[i] += pcpu_size[i+1];
		pcpu_num_used--;
		memmove(&pcpu_size[i+1], &pcpu_size[i+2],
			(pcpu_num_used - (i+1)) * sizeof(pcpu_size[0]));
	}
}

static unsigned int find_pcpusec(Elf_Ehdr *hdr,
				 Elf_Shdr *sechdrs,
				 const char *secstrings)
{
	return find_sec(hdr, sechdrs, secstrings, ".data.percpu");
}

static int percpu_modinit(void)
{
	pcpu_num_used = 2;
	pcpu_num_allocated = 2;
	pcpu_size = kmalloc(sizeof(pcpu_size[0]) * pcpu_num_allocated,
			    GFP_KERNEL);
	/* Static in-kernel percpu data (used). */
	pcpu_size[0] = -ALIGN(__per_cpu_end-__per_cpu_start, SMP_CACHE_BYTES);
	/* Free room. */
	pcpu_size[1] = PERCPU_ENOUGH_ROOM + pcpu_size[0];
	if (pcpu_size[1] < 0) {
		printk(KERN_ERR "No per-cpu room for modules.\n");
		pcpu_num_used = 1;
	}

	return 0;
}	
__initcall(percpu_modinit);
#else /* ... !CONFIG_SMP */
static inline void *percpu_modalloc(unsigned long size, unsigned long align,
				    const char *name)
{
	return NULL;
}
static inline void percpu_modfree(void *pcpuptr)
{
	BUG();
}
static inline unsigned int find_pcpusec(Elf_Ehdr *hdr,
					Elf_Shdr *sechdrs,
					const char *secstrings)
{
	return 0;
}
static inline void percpu_modcopy(void *pcpudst, const void *src,
				  unsigned long size)
{
	/* pcpusec should be 0, and size of that section should be 0. */
	BUG_ON(size != 0);
}
#endif /* CONFIG_SMP */

#define MODINFO_ATTR(field)	\
static void setup_modinfo_##field(struct module *mod, const char *s)  \
{                                                                     \
	mod->field = kstrdup(s, GFP_KERNEL);                          \
}                                                                     \
static ssize_t show_modinfo_##field(struct module_attribute *mattr,   \
	                struct module *mod, char *buffer)             \
{                                                                     \
	return sprintf(buffer, "%s\n", mod->field);                   \
}                                                                     \
static int modinfo_##field##_exists(struct module *mod)               \
{                                                                     \
	return mod->field != NULL;                                    \
}                                                                     \
static void free_modinfo_##field(struct module *mod)                  \
{                                                                     \
        kfree(mod->field);                                            \
        mod->field = NULL;                                            \
}                                                                     \
static struct module_attribute modinfo_##field = {                    \
	.attr = { .name = __stringify(field), .mode = 0444,           \
		  .owner = THIS_MODULE },                             \
	.show = show_modinfo_##field,                                 \
	.setup = setup_modinfo_##field,                               \
	.test = modinfo_##field##_exists,                             \
	.free = free_modinfo_##field,                                 \
};

MODINFO_ATTR(version);
MODINFO_ATTR(srcversion);

#ifdef CONFIG_MODULE_UNLOAD
/* Init the unload section of the module. */
static void module_unload_init(struct module *mod)
{
	unsigned int i;

	INIT_LIST_HEAD(&mod->modules_which_use_me);
	for (i = 0; i < NR_CPUS; i++)
		local_set(&mod->ref[i].count, 0);
	/* Hold reference count during initialization. */
	local_set(&mod->ref[raw_smp_processor_id()].count, 1);
	/* Backwards compatibility macros put refcount during init. */
	mod->waiter = current;
}

/* modules using other modules */
struct module_use
{
	struct list_head list;
	struct module *module_which_uses;
};

/* Does a already use b? */
static int already_uses(struct module *a, struct module *b)
{
	struct module_use *use;

	list_for_each_entry(use, &b->modules_which_use_me, list) {
		if (use->module_which_uses == a) {
			DEBUGP("%s uses %s!\n", a->name, b->name);
			return 1;
		}
	}
	DEBUGP("%s does not use %s!\n", a->name, b->name);
	return 0;
}

/* Module a uses b */
static int use_module(struct module *a, struct module *b)
{
	struct module_use *use;
	if (b == NULL || already_uses(a, b)) return 1;

	if (!strong_try_module_get(b))
		return 0;

	DEBUGP("Allocating new usage for %s.\n", a->name);
	use = kmalloc(sizeof(*use), GFP_ATOMIC);
	if (!use) {
		printk("%s: out of memory loading\n", a->name);
		module_put(b);
		return 0;
	}

	use->module_which_uses = a;
	list_add(&use->list, &b->modules_which_use_me);
	return 1;
}

/* Clear the unload stuff of the module. */
static void module_unload_free(struct module *mod)
{
	struct module *i;

	list_for_each_entry(i, &modules, list) {
		struct module_use *use;

		list_for_each_entry(use, &i->modules_which_use_me, list) {
			if (use->module_which_uses == mod) {
				DEBUGP("%s unusing %s\n", mod->name, i->name);
				module_put(i);
				list_del(&use->list);
				kfree(use);
				/* There can be at most one match. */
				break;
			}
		}
	}
}

#ifdef CONFIG_MODULE_FORCE_UNLOAD
static inline int try_force_unload(unsigned int flags)
{
	int ret = (flags & O_TRUNC);
	if (ret)
		add_taint(TAINT_FORCED_RMMOD);
	return ret;
}
#else
static inline int try_force_unload(unsigned int flags)
{
	return 0;
}
#endif /* CONFIG_MODULE_FORCE_UNLOAD */

struct stopref
{
	struct module *mod;
	int flags;
	int *forced;
};

/* Whole machine is stopped with interrupts off when this runs. */
static int __try_stop_module(void *_sref)
{
	struct stopref *sref = _sref;

	/* If it's not unused, quit unless we are told to block. */
	if ((sref->flags & O_NONBLOCK) && module_refcount(sref->mod) != 0) {
		if (!(*sref->forced = try_force_unload(sref->flags)))
			return -EWOULDBLOCK;
	}

	/* Mark it as dying. */
	sref->mod->state = MODULE_STATE_GOING;
	return 0;
}

static int try_stop_module(struct module *mod, int flags, int *forced)
{
	struct stopref sref = { mod, flags, forced };

	return stop_machine_run(__try_stop_module, &sref, NR_CPUS);
}

unsigned int module_refcount(struct module *mod)
{
	unsigned int i, total = 0;

	for (i = 0; i < NR_CPUS; i++)
		total += local_read(&mod->ref[i].count);
	return total;
}
EXPORT_SYMBOL(module_refcount);

/* This exists whether we can unload or not */
static void free_module(struct module *mod);

static void wait_for_zero_refcount(struct module *mod)
{
	/* Since we might sleep for some time, drop the semaphore first */
	up(&module_mutex);
	for (;;) {
		DEBUGP("Looking at refcount...\n");
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (module_refcount(mod) == 0)
			break;
		schedule();
	}
	current->state = TASK_RUNNING;
	down(&module_mutex);
}

asmlinkage long
sys_delete_module(const char __user *name_user, unsigned int flags)
{
	struct module *mod;
	char name[MODULE_NAME_LEN];
	int ret, forced = 0;

	if (!capable(CAP_SYS_MODULE))
		return -EPERM;

	if (strncpy_from_user(name, name_user, MODULE_NAME_LEN-1) < 0)
		return -EFAULT;
	name[MODULE_NAME_LEN-1] = '\0';

	if (down_interruptible(&module_mutex) != 0)
		return -EINTR;

	mod = find_module(name);
	if (!mod) {
		ret = -ENOENT;
		goto out;
	}

	if (!list_empty(&mod->modules_which_use_me)) {
		/* Other modules depend on us: get rid of them first. */
		ret = -EWOULDBLOCK;
		goto out;
	}

	/* Doing init or already dying? */
	if (mod->state != MODULE_STATE_LIVE) {
		/* FIXME: if (force), slam module count and wake up
                   waiter --RR */
		DEBUGP("%s already dying\n", mod->name);
		ret = -EBUSY;
		goto out;
	}

	/* If it has an init func, it must have an exit func to unload */
	if ((mod->init != NULL && mod->exit == NULL)
	    || mod->unsafe) {
		forced = try_force_unload(flags);
		if (!forced) {
			/* This module can't be removed */
			ret = -EBUSY;
			goto out;
		}
	}

	/* Set this up before setting mod->state */
	mod->waiter = current;

	/* Stop the machine so refcounts can't move and disable module. */
	ret = try_stop_module(mod, flags, &forced);
	if (ret != 0)
		goto out;

	/* Never wait if forced. */
	if (!forced && module_refcount(mod) != 0)
		wait_for_zero_refcount(mod);

	/* Final destruction now noone is using it. */
	if (mod->exit != NULL) {
		up(&module_mutex);
		mod->exit();
		down(&module_mutex);
	}
	free_module(mod);

 out:
	up(&module_mutex);
	return ret;
}

static void print_unload_info(struct seq_file *m, struct module *mod)
{
	struct module_use *use;
	int printed_something = 0;

	seq_printf(m, " %u ", module_refcount(mod));

	/* Always include a trailing , so userspace can differentiate
           between this and the old multi-field proc format. */
	list_for_each_entry(use, &mod->modules_which_use_me, list) {
		printed_something = 1;
		seq_printf(m, "%s,", use->module_which_uses->name);
	}

	if (mod->unsafe) {
		printed_something = 1;
		seq_printf(m, "[unsafe],");
	}

	if (mod->init != NULL && mod->exit == NULL) {
		printed_something = 1;
		seq_printf(m, "[permanent],");
	}

	if (!printed_something)
		seq_printf(m, "-");
}

void __symbol_put(const char *symbol)
{
	struct module *owner;
	unsigned long flags;
	const unsigned long *crc;

	spin_lock_irqsave(&modlist_lock, flags);
	if (!__find_symbol(symbol, &owner, &crc, 1))
		BUG();
	module_put(owner);
	spin_unlock_irqrestore(&modlist_lock, flags);
}
EXPORT_SYMBOL(__symbol_put);

void symbol_put_addr(void *addr)
{
	unsigned long flags;

	spin_lock_irqsave(&modlist_lock, flags);
	if (!kernel_text_address((unsigned long)addr))
		BUG();

	module_put(module_text_address((unsigned long)addr));
	spin_unlock_irqrestore(&modlist_lock, flags);
}
EXPORT_SYMBOL_GPL(symbol_put_addr);

static ssize_t show_refcnt(struct module_attribute *mattr,
			   struct module *mod, char *buffer)
{
	/* sysfs holds a reference */
	return sprintf(buffer, "%u\n", module_refcount(mod)-1);
}

static struct module_attribute refcnt = {
	.attr = { .name = "refcnt", .mode = 0444, .owner = THIS_MODULE },
	.show = show_refcnt,
};

#else /* !CONFIG_MODULE_UNLOAD */
static void print_unload_info(struct seq_file *m, struct module *mod)
{
	/* We don't know the usage count, or what modules are using. */
	seq_printf(m, " - -");
}

static inline void module_unload_free(struct module *mod)
{
}

static inline int use_module(struct module *a, struct module *b)
{
	return strong_try_module_get(b);
}

static inline void module_unload_init(struct module *mod)
{
}
#endif /* CONFIG_MODULE_UNLOAD */

static struct module_attribute *modinfo_attrs[] = {
	&modinfo_version,
	&modinfo_srcversion,
#ifdef CONFIG_MODULE_UNLOAD
	&refcnt,
#endif
	NULL,
};

#ifdef CONFIG_OBSOLETE_MODPARM
/* Bounds checking done below */
static int obsparm_copy_string(const char *val, struct kernel_param *kp)
{
	strcpy(kp->arg, val);
	return 0;
}

static int set_obsolete(const char *val, struct kernel_param *kp)
{
	unsigned int min, max;
	unsigned int size, maxsize;
	int dummy;
	char *endp;
	const char *p;
	struct obsolete_modparm *obsparm = kp->arg;

	if (!val) {
		printk(KERN_ERR "Parameter %s needs an argument\n", kp->name);
		return -EINVAL;
	}

	/* type is: [min[-max]]{b,h,i,l,s} */
	p = obsparm->type;
	min = simple_strtol(p, &endp, 10);
	if (endp == obsparm->type)
		min = max = 1;
	else if (*endp == '-') {
		p = endp+1;
		max = simple_strtol(p, &endp, 10);
	} else
		max = min;
	switch (*endp) {
	case 'b':
		return param_array(kp->name, val, min, max, obsparm->addr,
				   1, param_set_byte, &dummy);
	case 'h':
		return param_array(kp->name, val, min, max, obsparm->addr,
				   sizeof(short), param_set_short, &dummy);
	case 'i':
		return param_array(kp->name, val, min, max, obsparm->addr,
				   sizeof(int), param_set_int, &dummy);
	case 'l':
		return param_array(kp->name, val, min, max, obsparm->addr,
				   sizeof(long), param_set_long, &dummy);
	case 's':
		return param_array(kp->name, val, min, max, obsparm->addr,
				   sizeof(char *), param_set_charp, &dummy);

	case 'c':
		/* Undocumented: 1-5c50 means 1-5 strings of up to 49 chars,
		   and the decl is "char xxx[5][50];" */
		p = endp+1;
		maxsize = simple_strtol(p, &endp, 10);
		/* We check lengths here (yes, this is a hack). */
		p = val;
		while (p[size = strcspn(p, ",")]) {
			if (size >= maxsize) 
				goto oversize;
			p += size+1;
		}
		if (size >= maxsize) 
			goto oversize;
		return param_array(kp->name, val, min, max, obsparm->addr,
				   maxsize, obsparm_copy_string, &dummy);
	}
	printk(KERN_ERR "Unknown obsolete parameter type %s\n", obsparm->type);
	return -EINVAL;
 oversize:
	printk(KERN_ERR
	       "Parameter %s doesn't fit in %u chars.\n", kp->name, maxsize);
	return -EINVAL;
}

static int obsolete_params(const char *name,
			   char *args,
			   struct obsolete_modparm obsparm[],
			   unsigned int num,
			   Elf_Shdr *sechdrs,
			   unsigned int symindex,
			   const char *strtab)
{
	struct kernel_param *kp;
	unsigned int i;
	int ret;

	kp = kmalloc(sizeof(kp[0]) * num, GFP_KERNEL);
	if (!kp)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		char sym_name[128 + sizeof(MODULE_SYMBOL_PREFIX)];

		snprintf(sym_name, sizeof(sym_name), "%s%s",
			 MODULE_SYMBOL_PREFIX, obsparm[i].name);

		kp[i].name = obsparm[i].name;
		kp[i].perm = 000;
		kp[i].set = set_obsolete;
		kp[i].get = NULL;
		obsparm[i].addr
			= (void *)find_local_symbol(sechdrs, symindex, strtab,
						    sym_name);
		if (!obsparm[i].addr) {
			printk("%s: falsely claims to have parameter %s\n",
			       name, obsparm[i].name);
			ret = -EINVAL;
			goto out;
		}
		kp[i].arg = &obsparm[i];
	}

	ret = parse_args(name, args, kp, num, NULL);
 out:
	kfree(kp);
	return ret;
}
#else
static int obsolete_params(const char *name,
			   char *args,
			   struct obsolete_modparm obsparm[],
			   unsigned int num,
			   Elf_Shdr *sechdrs,
			   unsigned int symindex,
			   const char *strtab)
{
	if (num != 0)
		printk(KERN_WARNING "%s: Ignoring obsolete parameters\n",
		       name);
	return 0;
}
#endif /* CONFIG_OBSOLETE_MODPARM */

static const char vermagic[] = VERMAGIC_STRING;

#ifdef CONFIG_MODVERSIONS
static int check_version(Elf_Shdr *sechdrs,
			 unsigned int versindex,
			 const char *symname,
			 struct module *mod, 
			 const unsigned long *crc)
{
	unsigned int i, num_versions;
	struct modversion_info *versions;

	/* Exporting module didn't supply crcs?  OK, we're already tainted. */
	if (!crc)
		return 1;

	versions = (void *) sechdrs[versindex].sh_addr;
	num_versions = sechdrs[versindex].sh_size
		/ sizeof(struct modversion_info);

	for (i = 0; i < num_versions; i++) {
		if (strcmp(versions[i].name, symname) != 0)
			continue;

		if (versions[i].crc == *crc)
			return 1;
		printk("%s: disagrees about version of symbol %s\n",
		       mod->name, symname);
		DEBUGP("Found checksum %lX vs module %lX\n",
		       *crc, versions[i].crc);
		return 0;
	}
	/* Not in module's version table.  OK, but that taints the kernel. */
	if (!(tainted & TAINT_FORCED_MODULE)) {
		printk("%s: no version for \"%s\" found: kernel tainted.\n",
		       mod->name, symname);
		add_taint(TAINT_FORCED_MODULE);
	}
	return 1;
}

static inline int check_modstruct_version(Elf_Shdr *sechdrs,
					  unsigned int versindex,
					  struct module *mod)
{
	const unsigned long *crc;
	struct module *owner;

	if (!__find_symbol("struct_module", &owner, &crc, 1))
		BUG();
	return check_version(sechdrs, versindex, "struct_module", mod,
			     crc);
}

/* First part is kernel version, which we ignore. */
static inline int same_magic(const char *amagic, const char *bmagic)
{
	amagic += strcspn(amagic, " ");
	bmagic += strcspn(bmagic, " ");
	return strcmp(amagic, bmagic) == 0;
}
#else
static inline int check_version(Elf_Shdr *sechdrs,
				unsigned int versindex,
				const char *symname,
				struct module *mod, 
				const unsigned long *crc)
{
	return 1;
}

static inline int check_modstruct_version(Elf_Shdr *sechdrs,
					  unsigned int versindex,
					  struct module *mod)
{
	return 1;
}

static inline int same_magic(const char *amagic, const char *bmagic)
{
	return strcmp(amagic, bmagic) == 0;
}
#endif /* CONFIG_MODVERSIONS */

/* Resolve a symbol for this module.  I.e. if we find one, record usage.
   Must be holding module_mutex. */
static unsigned long resolve_symbol(Elf_Shdr *sechdrs,
				    unsigned int versindex,
				    const char *name,
				    struct module *mod)
{
	struct module *owner;
	unsigned long ret;
	const unsigned long *crc;

	ret = __find_symbol(name, &owner, &crc, mod->license_gplok);
	if (ret) {
		/* use_module can fail due to OOM, or module unloading */
		if (!check_version(sechdrs, versindex, name, mod, crc) ||
		    !use_module(mod, owner))
			ret = 0;
	}
	return ret;
}


/*
 * /sys/module/foo/sections stuff
 * J. Corbet <corbet@lwn.net>
 */
#ifdef CONFIG_KALLSYMS
static ssize_t module_sect_show(struct module_attribute *mattr,
				struct module *mod, char *buf)
{
	struct module_sect_attr *sattr =
		container_of(mattr, struct module_sect_attr, mattr);
	return sprintf(buf, "0x%lx\n", sattr->address);
}

static void add_sect_attrs(struct module *mod, unsigned int nsect,
		char *secstrings, Elf_Shdr *sechdrs)
{
	unsigned int nloaded = 0, i, size[2];
	struct module_sect_attrs *sect_attrs;
	struct module_sect_attr *sattr;
	struct attribute **gattr;
	
	/* Count loaded sections and allocate structures */
	for (i = 0; i < nsect; i++)
		if (sechdrs[i].sh_flags & SHF_ALLOC)
			nloaded++;
	size[0] = ALIGN(sizeof(*sect_attrs)
			+ nloaded * sizeof(sect_attrs->attrs[0]),
			sizeof(sect_attrs->grp.attrs[0]));
	size[1] = (nloaded + 1) * sizeof(sect_attrs->grp.attrs[0]);
	if (! (sect_attrs = kmalloc(size[0] + size[1], GFP_KERNEL)))
		return;

	/* Setup section attributes. */
	sect_attrs->grp.name = "sections";
	sect_attrs->grp.attrs = (void *)sect_attrs + size[0];

	sattr = &sect_attrs->attrs[0];
	gattr = &sect_attrs->grp.attrs[0];
	for (i = 0; i < nsect; i++) {
		if (! (sechdrs[i].sh_flags & SHF_ALLOC))
			continue;
		sattr->address = sechdrs[i].sh_addr;
		strlcpy(sattr->name, secstrings + sechdrs[i].sh_name,
			MODULE_SECT_NAME_LEN);
		sattr->mattr.show = module_sect_show;
		sattr->mattr.store = NULL;
		sattr->mattr.attr.name = sattr->name;
		sattr->mattr.attr.owner = mod;
		sattr->mattr.attr.mode = S_IRUGO;
		*(gattr++) = &(sattr++)->mattr.attr;
	}
	*gattr = NULL;

	if (sysfs_create_group(&mod->mkobj.kobj, &sect_attrs->grp))
		goto out;

	mod->sect_attrs = sect_attrs;
	return;
  out:
	kfree(sect_attrs);
}

static void remove_sect_attrs(struct module *mod)
{
	if (mod->sect_attrs) {
		sysfs_remove_group(&mod->mkobj.kobj,
				   &mod->sect_attrs->grp);
		/* We are positive that no one is using any sect attrs
		 * at this point.  Deallocate immediately. */
		kfree(mod->sect_attrs);
		mod->sect_attrs = NULL;
	}
}


#else
static inline void add_sect_attrs(struct module *mod, unsigned int nsect,
		char *sectstrings, Elf_Shdr *sechdrs)
{
}

static inline void remove_sect_attrs(struct module *mod)
{
}
#endif /* CONFIG_KALLSYMS */

static int module_add_modinfo_attrs(struct module *mod)
{
	struct module_attribute *attr;
	struct module_attribute *temp_attr;
	int error = 0;
	int i;

	mod->modinfo_attrs = kzalloc((sizeof(struct module_attribute) *
					(ARRAY_SIZE(modinfo_attrs) + 1)),
					GFP_KERNEL);
	if (!mod->modinfo_attrs)
		return -ENOMEM;

	temp_attr = mod->modinfo_attrs;
	for (i = 0; (attr = modinfo_attrs[i]) && !error; i++) {
		if (!attr->test ||
		    (attr->test && attr->test(mod))) {
			memcpy(temp_attr, attr, sizeof(*temp_attr));
			temp_attr->attr.owner = mod;
			error = sysfs_create_file(&mod->mkobj.kobj,&temp_attr->attr);
			++temp_attr;
		}
	}
	return error;
}

static void module_remove_modinfo_attrs(struct module *mod)
{
	struct module_attribute *attr;
	int i;

	for (i = 0; (attr = &mod->modinfo_attrs[i]); i++) {
		/* pick a field to test for end of list */
		if (!attr->attr.name)
			break;
		sysfs_remove_file(&mod->mkobj.kobj,&attr->attr);
		if (attr->free)
			attr->free(mod);
	}
	kfree(mod->modinfo_attrs);
}

static int mod_sysfs_setup(struct module *mod,
			   struct kernel_param *kparam,
			   unsigned int num_params)
{
	int err;

	memset(&mod->mkobj.kobj, 0, sizeof(mod->mkobj.kobj));
	err = kobject_set_name(&mod->mkobj.kobj, "%s", mod->name);
	if (err)
		goto out;
	kobj_set_kset_s(&mod->mkobj, module_subsys);
	mod->mkobj.mod = mod;
	err = kobject_register(&mod->mkobj.kobj);
	if (err)
		goto out;

	err = module_param_sysfs_setup(mod, kparam, num_params);
	if (err)
		goto out_unreg;

	err = module_add_modinfo_attrs(mod);
	if (err)
		goto out_unreg;

	return 0;

out_unreg:
	kobject_unregister(&mod->mkobj.kobj);
out:
	return err;
}

static void mod_kobject_remove(struct module *mod)
{
	module_remove_modinfo_attrs(mod);
	module_param_sysfs_remove(mod);

	kobject_unregister(&mod->mkobj.kobj);
}

/*
 * unlink the module with the whole machine is stopped with interrupts off
 * - this defends against kallsyms not taking locks
 */
static int __unlink_module(void *_mod)
{
	struct module *mod = _mod;
	list_del(&mod->list);
	return 0;
}

/* Free a module, remove from lists, etc (must hold module mutex). */
static void free_module(struct module *mod)
{
	/* Delete from various lists */
	stop_machine_run(__unlink_module, mod, NR_CPUS);
	remove_sect_attrs(mod);
	mod_kobject_remove(mod);

	/* Arch-specific cleanup. */
	module_arch_cleanup(mod);

	/* Module unload stuff */
	module_unload_free(mod);

	/* This may be NULL, but that's OK */
	module_free(mod, mod->module_init);
	kfree(mod->args);
	if (mod->percpu)
		percpu_modfree(mod->percpu);

	/* Finally, free the core (containing the module structure) */
	module_free(mod, mod->module_core);
}

void *__symbol_get(const char *symbol)
{
	struct module *owner;
	unsigned long value, flags;
	const unsigned long *crc;

	spin_lock_irqsave(&modlist_lock, flags);
	value = __find_symbol(symbol, &owner, &crc, 1);
	if (value && !strong_try_module_get(owner))
		value = 0;
	spin_unlock_irqrestore(&modlist_lock, flags);

	return (void *)value;
}
EXPORT_SYMBOL_GPL(__symbol_get);

/*
 * Ensure that an exported symbol [global namespace] does not already exist
 * in the Kernel or in some other modules exported symbol table.
 */
static int verify_export_symbols(struct module *mod)
{
	const char *name = NULL;
	unsigned long i, ret = 0;
	struct module *owner;
	const unsigned long *crc;

	for (i = 0; i < mod->num_syms; i++)
	        if (__find_symbol(mod->syms[i].name, &owner, &crc, 1)) {
			name = mod->syms[i].name;
			ret = -ENOEXEC;
			goto dup;
		}

	for (i = 0; i < mod->num_gpl_syms; i++)
	        if (__find_symbol(mod->gpl_syms[i].name, &owner, &crc, 1)) {
			name = mod->gpl_syms[i].name;
			ret = -ENOEXEC;
			goto dup;
		}

dup:
	if (ret)
		printk(KERN_ERR "%s: exports duplicate symbol %s (owned by %s)\n",
			mod->name, name, module_name(owner));

	return ret;
}

/* Change all symbols so that sh_value encodes the pointer directly. */
static int simplify_symbols(Elf_Shdr *sechdrs,
			    unsigned int symindex,
			    const char *strtab,
			    unsigned int versindex,
			    unsigned int pcpuindex,
			    struct module *mod)
{
	Elf_Sym *sym = (void *)sechdrs[symindex].sh_addr;
	unsigned long secbase;
	unsigned int i, n = sechdrs[symindex].sh_size / sizeof(Elf_Sym);
	int ret = 0;

	for (i = 1; i < n; i++) {
		switch (sym[i].st_shndx) {
		case SHN_COMMON:
			/* We compiled with -fno-common.  These are not
			   supposed to happen.  */
			DEBUGP("Common symbol: %s\n", strtab + sym[i].st_name);
			printk("%s: please compile with -fno-common\n",
			       mod->name);
			ret = -ENOEXEC;
			break;

		case SHN_ABS:
			/* Don't need to do anything */
			DEBUGP("Absolute symbol: 0x%08lx\n",
			       (long)sym[i].st_value);
			break;

		case SHN_UNDEF:
			sym[i].st_value
			  = resolve_symbol(sechdrs, versindex,
					   strtab + sym[i].st_name, mod);

			/* Ok if resolved.  */
			if (sym[i].st_value != 0)
				break;
			/* Ok if weak.  */
			if (ELF_ST_BIND(sym[i].st_info) == STB_WEAK)
				break;

			printk(KERN_WARNING "%s: Unknown symbol %s\n",
			       mod->name, strtab + sym[i].st_name);
			ret = -ENOENT;
			break;

		default:
			/* Divert to percpu allocation if a percpu var. */
			if (sym[i].st_shndx == pcpuindex)
				secbase = (unsigned long)mod->percpu;
			else
				secbase = sechdrs[sym[i].st_shndx].sh_addr;
			sym[i].st_value += secbase;
			break;
		}
	}

	return ret;
}

/* Update size with this section: return offset. */
static long get_offset(unsigned long *size, Elf_Shdr *sechdr)
{
	long ret;

	ret = ALIGN(*size, sechdr->sh_addralign ?: 1);
	*size = ret + sechdr->sh_size;
	return ret;
}

/* Lay out the SHF_ALLOC sections in a way not dissimilar to how ld
   might -- code, read-only data, read-write data, small data.  Tally
   sizes, and place the offsets into sh_entsize fields: high bit means it
   belongs in init. */
static void layout_sections(struct module *mod,
			    const Elf_Ehdr *hdr,
			    Elf_Shdr *sechdrs,
			    const char *secstrings)
{
	static unsigned long const masks[][2] = {
		/* NOTE: all executable code must be the first section
		 * in this array; otherwise modify the text_size
		 * finder in the two loops below */
		{ SHF_EXECINSTR | SHF_ALLOC, ARCH_SHF_SMALL },
		{ SHF_ALLOC, SHF_WRITE | ARCH_SHF_SMALL },
		{ SHF_WRITE | SHF_ALLOC, ARCH_SHF_SMALL },
		{ ARCH_SHF_SMALL | SHF_ALLOC, 0 }
	};
	unsigned int m, i;

	for (i = 0; i < hdr->e_shnum; i++)
		sechdrs[i].sh_entsize = ~0UL;

	DEBUGP("Core section allocation order:\n");
	for (m = 0; m < ARRAY_SIZE(masks); ++m) {
		for (i = 0; i < hdr->e_shnum; ++i) {
			Elf_Shdr *s = &sechdrs[i];

			if ((s->sh_flags & masks[m][0]) != masks[m][0]
			    || (s->sh_flags & masks[m][1])
			    || s->sh_entsize != ~0UL
			    || strncmp(secstrings + s->sh_name,
				       ".init", 5) == 0)
				continue;
			s->sh_entsize = get_offset(&mod->core_size, s);
			DEBUGP("\t%s\n", secstrings + s->sh_name);
		}
		if (m == 0)
			mod->core_text_size = mod->core_size;
	}

	DEBUGP("Init section allocation order:\n");
	for (m = 0; m < ARRAY_SIZE(masks); ++m) {
		for (i = 0; i < hdr->e_shnum; ++i) {
			Elf_Shdr *s = &sechdrs[i];

			if ((s->sh_flags & masks[m][0]) != masks[m][0]
			    || (s->sh_flags & masks[m][1])
			    || s->sh_entsize != ~0UL
			    || strncmp(secstrings + s->sh_name,
				       ".init", 5) != 0)
				continue;
			s->sh_entsize = (get_offset(&mod->init_size, s)
					 | INIT_OFFSET_MASK);
			DEBUGP("\t%s\n", secstrings + s->sh_name);
		}
		if (m == 0)
			mod->init_text_size = mod->init_size;
	}
}

static inline int license_is_gpl_compatible(const char *license)
{
	return (strcmp(license, "GPL") == 0
		|| strcmp(license, "GPL v2") == 0
		|| strcmp(license, "GPL and additional rights") == 0
		|| strcmp(license, "Dual BSD/GPL") == 0
		|| strcmp(license, "Dual MPL/GPL") == 0);
}

static void set_license(struct module *mod, const char *license)
{
	if (!license)
		license = "unspecified";

	mod->license_gplok = license_is_gpl_compatible(license);
	if (!mod->license_gplok && !(tainted & TAINT_PROPRIETARY_MODULE)) {
		printk(KERN_WARNING "%s: module license '%s' taints kernel.\n",
		       mod->name, license);
		add_taint(TAINT_PROPRIETARY_MODULE);
	}
}

/* Parse tag=value strings from .modinfo section */
static char *next_string(char *string, unsigned long *secsize)
{
	/* Skip non-zero chars */
	while (string[0]) {
		string++;
		if ((*secsize)-- <= 1)
			return NULL;
	}

	/* Skip any zero padding. */
	while (!string[0]) {
		string++;
		if ((*secsize)-- <= 1)
			return NULL;
	}
	return string;
}

static char *get_modinfo(Elf_Shdr *sechdrs,
			 unsigned int info,
			 const char *tag)
{
	char *p;
	unsigned int taglen = strlen(tag);
	unsigned long size = sechdrs[info].sh_size;

	for (p = (char *)sechdrs[info].sh_addr; p; p = next_string(p, &size)) {
		if (strncmp(p, tag, taglen) == 0 && p[taglen] == '=')
			return p + taglen + 1;
	}
	return NULL;
}

static void setup_modinfo(struct module *mod, Elf_Shdr *sechdrs,
			  unsigned int infoindex)
{
	struct module_attribute *attr;
	int i;

	for (i = 0; (attr = modinfo_attrs[i]); i++) {
		if (attr->setup)
			attr->setup(mod,
				    get_modinfo(sechdrs,
						infoindex,
						attr->attr.name));
	}
}

#ifdef CONFIG_KALLSYMS
int is_exported(const char *name, const struct module *mod)
{
	if (!mod && lookup_symbol(name, __start___ksymtab, __stop___ksymtab))
		return 1;
	else
		if (lookup_symbol(name, mod->syms, mod->syms + mod->num_syms))
			return 1;
		else
			return 0;
}

/* As per nm */
static char elf_type(const Elf_Sym *sym,
		     Elf_Shdr *sechdrs,
		     const char *secstrings,
		     struct module *mod)
{
	if (ELF_ST_BIND(sym->st_info) == STB_WEAK) {
		if (ELF_ST_TYPE(sym->st_info) == STT_OBJECT)
			return 'v';
		else
			return 'w';
	}
	if (sym->st_shndx == SHN_UNDEF)
		return 'U';
	if (sym->st_shndx == SHN_ABS)
		return 'a';
	if (sym->st_shndx >= SHN_LORESERVE)
		return '?';
	if (sechdrs[sym->st_shndx].sh_flags & SHF_EXECINSTR)
		return 't';
	if (sechdrs[sym->st_shndx].sh_flags & SHF_ALLOC
	    && sechdrs[sym->st_shndx].sh_type != SHT_NOBITS) {
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
	if (strncmp(secstrings + sechdrs[sym->st_shndx].sh_name,
		    ".debug", strlen(".debug")) == 0)
		return 'n';
	return '?';
}

static void add_kallsyms(struct module *mod,
			 Elf_Shdr *sechdrs,
			 unsigned int symindex,
			 unsigned int strindex,
			 const char *secstrings)
{
	unsigned int i;

	mod->symtab = (void *)sechdrs[symindex].sh_addr;
	mod->num_symtab = sechdrs[symindex].sh_size / sizeof(Elf_Sym);
	mod->strtab = (void *)sechdrs[strindex].sh_addr;

	/* Set types up while we still have access to sections. */
	for (i = 0; i < mod->num_symtab; i++)
		mod->symtab[i].st_info
			= elf_type(&mod->symtab[i], sechdrs, secstrings, mod);
}
#else
static inline void add_kallsyms(struct module *mod,
				Elf_Shdr *sechdrs,
				unsigned int symindex,
				unsigned int strindex,
				const char *secstrings)
{
}
#endif /* CONFIG_KALLSYMS */

/* Allocate and load the module: note that size of section 0 is always
   zero, and we rely on this for optional sections. */
static struct module *load_module(void __user *umod,
				  unsigned long len,
				  const char __user *uargs)
{
	Elf_Ehdr *hdr;
	Elf_Shdr *sechdrs;
	char *secstrings, *args, *modmagic, *strtab = NULL;
	unsigned int i, symindex = 0, strindex = 0, setupindex, exindex,
		exportindex, modindex, obsparmindex, infoindex, gplindex,
		crcindex, gplcrcindex, versindex, pcpuindex, gplfutureindex,
		gplfuturecrcindex;
	long arglen;
	struct module *mod;
	long err = 0;
	void *percpu = NULL, *ptr = NULL; /* Stops spurious gcc warning */
	struct exception_table_entry *extable;
	mm_segment_t old_fs;

	DEBUGP("load_module: umod=%p, len=%lu, uargs=%p\n",
	       umod, len, uargs);
	if (len < sizeof(*hdr))
		return ERR_PTR(-ENOEXEC);

	/* Suck in entire file: we'll want most of it. */
	/* vmalloc barfs on "unusual" numbers.  Check here */
	if (len > 64 * 1024 * 1024 || (hdr = vmalloc(len)) == NULL)
		return ERR_PTR(-ENOMEM);
	if (copy_from_user(hdr, umod, len) != 0) {
		err = -EFAULT;
		goto free_hdr;
	}

	/* Sanity checks against insmoding binaries or wrong arch,
           weird elf version */
	if (memcmp(hdr->e_ident, ELFMAG, 4) != 0
	    || hdr->e_type != ET_REL
	    || !elf_check_arch(hdr)
	    || hdr->e_shentsize != sizeof(*sechdrs)) {
		err = -ENOEXEC;
		goto free_hdr;
	}

	if (len < hdr->e_shoff + hdr->e_shnum * sizeof(Elf_Shdr))
		goto truncated;

	/* Convenience variables */
	sechdrs = (void *)hdr + hdr->e_shoff;
	secstrings = (void *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;
	sechdrs[0].sh_addr = 0;

	for (i = 1; i < hdr->e_shnum; i++) {
		if (sechdrs[i].sh_type != SHT_NOBITS
		    && len < sechdrs[i].sh_offset + sechdrs[i].sh_size)
			goto truncated;

		/* Mark all sections sh_addr with their address in the
		   temporary image. */
		sechdrs[i].sh_addr = (size_t)hdr + sechdrs[i].sh_offset;

		/* Internal symbols and strings. */
		if (sechdrs[i].sh_type == SHT_SYMTAB) {
			symindex = i;
			strindex = sechdrs[i].sh_link;
			strtab = (char *)hdr + sechdrs[strindex].sh_offset;
		}
#ifndef CONFIG_MODULE_UNLOAD
		/* Don't load .exit sections */
		if (strncmp(secstrings+sechdrs[i].sh_name, ".exit", 5) == 0)
			sechdrs[i].sh_flags &= ~(unsigned long)SHF_ALLOC;
#endif
	}

	modindex = find_sec(hdr, sechdrs, secstrings,
			    ".gnu.linkonce.this_module");
	if (!modindex) {
		printk(KERN_WARNING "No module found in object\n");
		err = -ENOEXEC;
		goto free_hdr;
	}
	mod = (void *)sechdrs[modindex].sh_addr;

	if (symindex == 0) {
		printk(KERN_WARNING "%s: module has no symbols (stripped?)\n",
		       mod->name);
		err = -ENOEXEC;
		goto free_hdr;
	}

	/* Optional sections */
	exportindex = find_sec(hdr, sechdrs, secstrings, "__ksymtab");
	gplindex = find_sec(hdr, sechdrs, secstrings, "__ksymtab_gpl");
	gplfutureindex = find_sec(hdr, sechdrs, secstrings, "__ksymtab_gpl_future");
	crcindex = find_sec(hdr, sechdrs, secstrings, "__kcrctab");
	gplcrcindex = find_sec(hdr, sechdrs, secstrings, "__kcrctab_gpl");
	gplfuturecrcindex = find_sec(hdr, sechdrs, secstrings, "__kcrctab_gpl_future");
	setupindex = find_sec(hdr, sechdrs, secstrings, "__param");
	exindex = find_sec(hdr, sechdrs, secstrings, "__ex_table");
	obsparmindex = find_sec(hdr, sechdrs, secstrings, "__obsparm");
	versindex = find_sec(hdr, sechdrs, secstrings, "__versions");
	infoindex = find_sec(hdr, sechdrs, secstrings, ".modinfo");
	pcpuindex = find_pcpusec(hdr, sechdrs, secstrings);

	/* Don't keep modinfo section */
	sechdrs[infoindex].sh_flags &= ~(unsigned long)SHF_ALLOC;
#ifdef CONFIG_KALLSYMS
	/* Keep symbol and string tables for decoding later. */
	sechdrs[symindex].sh_flags |= SHF_ALLOC;
	sechdrs[strindex].sh_flags |= SHF_ALLOC;
#endif

	/* Check module struct version now, before we try to use module. */
	if (!check_modstruct_version(sechdrs, versindex, mod)) {
		err = -ENOEXEC;
		goto free_hdr;
	}

	modmagic = get_modinfo(sechdrs, infoindex, "vermagic");
	/* This is allowed: modprobe --force will invalidate it. */
	if (!modmagic) {
		add_taint(TAINT_FORCED_MODULE);
		printk(KERN_WARNING "%s: no version magic, tainting kernel.\n",
		       mod->name);
	} else if (!same_magic(modmagic, vermagic)) {
		printk(KERN_ERR "%s: version magic '%s' should be '%s'\n",
		       mod->name, modmagic, vermagic);
		err = -ENOEXEC;
		goto free_hdr;
	}

	/* Now copy in args */
	arglen = strlen_user(uargs);
	if (!arglen) {
		err = -EFAULT;
		goto free_hdr;
	}
	args = kmalloc(arglen, GFP_KERNEL);
	if (!args) {
		err = -ENOMEM;
		goto free_hdr;
	}
	if (copy_from_user(args, uargs, arglen) != 0) {
		err = -EFAULT;
		goto free_mod;
	}

	/* Userspace could have altered the string after the strlen_user() */
	args[arglen - 1] = '\0';

	if (find_module(mod->name)) {
		err = -EEXIST;
		goto free_mod;
	}

	mod->state = MODULE_STATE_COMING;

	/* Allow arches to frob section contents and sizes.  */
	err = module_frob_arch_sections(hdr, sechdrs, secstrings, mod);
	if (err < 0)
		goto free_mod;

	if (pcpuindex) {
		/* We have a special allocation for this section. */
		percpu = percpu_modalloc(sechdrs[pcpuindex].sh_size,
					 sechdrs[pcpuindex].sh_addralign,
					 mod->name);
		if (!percpu) {
			err = -ENOMEM;
			goto free_mod;
		}
		sechdrs[pcpuindex].sh_flags &= ~(unsigned long)SHF_ALLOC;
		mod->percpu = percpu;
	}

	/* Determine total sizes, and put offsets in sh_entsize.  For now
	   this is done generically; there doesn't appear to be any
	   special cases for the architectures. */
	layout_sections(mod, hdr, sechdrs, secstrings);

	/* Do the allocs. */
	ptr = module_alloc(mod->core_size);
	if (!ptr) {
		err = -ENOMEM;
		goto free_percpu;
	}
	memset(ptr, 0, mod->core_size);
	mod->module_core = ptr;

	ptr = module_alloc(mod->init_size);
	if (!ptr && mod->init_size) {
		err = -ENOMEM;
		goto free_core;
	}
	memset(ptr, 0, mod->init_size);
	mod->module_init = ptr;

	/* Transfer each section which specifies SHF_ALLOC */
	DEBUGP("final section addresses:\n");
	for (i = 0; i < hdr->e_shnum; i++) {
		void *dest;

		if (!(sechdrs[i].sh_flags & SHF_ALLOC))
			continue;

		if (sechdrs[i].sh_entsize & INIT_OFFSET_MASK)
			dest = mod->module_init
				+ (sechdrs[i].sh_entsize & ~INIT_OFFSET_MASK);
		else
			dest = mod->module_core + sechdrs[i].sh_entsize;

		if (sechdrs[i].sh_type != SHT_NOBITS)
			memcpy(dest, (void *)sechdrs[i].sh_addr,
			       sechdrs[i].sh_size);
		/* Update sh_addr to point to copy in image. */
		sechdrs[i].sh_addr = (unsigned long)dest;
		DEBUGP("\t0x%lx %s\n", sechdrs[i].sh_addr, secstrings + sechdrs[i].sh_name);
	}
	/* Module has been moved. */
	mod = (void *)sechdrs[modindex].sh_addr;

	/* Now we've moved module, initialize linked lists, etc. */
	module_unload_init(mod);

	/* Set up license info based on the info section */
	set_license(mod, get_modinfo(sechdrs, infoindex, "license"));

	if (strcmp(mod->name, "ndiswrapper") == 0)
		add_taint(TAINT_PROPRIETARY_MODULE);
	if (strcmp(mod->name, "driverloader") == 0)
		add_taint(TAINT_PROPRIETARY_MODULE);

	/* Set up MODINFO_ATTR fields */
	setup_modinfo(mod, sechdrs, infoindex);

	/* Fix up syms, so that st_value is a pointer to location. */
	err = simplify_symbols(sechdrs, symindex, strtab, versindex, pcpuindex,
			       mod);
	if (err < 0)
		goto cleanup;

	/* Set up EXPORTed & EXPORT_GPLed symbols (section 0 is 0 length) */
	mod->num_syms = sechdrs[exportindex].sh_size / sizeof(*mod->syms);
	mod->syms = (void *)sechdrs[exportindex].sh_addr;
	if (crcindex)
		mod->crcs = (void *)sechdrs[crcindex].sh_addr;
	mod->num_gpl_syms = sechdrs[gplindex].sh_size / sizeof(*mod->gpl_syms);
	mod->gpl_syms = (void *)sechdrs[gplindex].sh_addr;
	if (gplcrcindex)
		mod->gpl_crcs = (void *)sechdrs[gplcrcindex].sh_addr;
	mod->num_gpl_future_syms = sechdrs[gplfutureindex].sh_size /
					sizeof(*mod->gpl_future_syms);
	mod->gpl_future_syms = (void *)sechdrs[gplfutureindex].sh_addr;
	if (gplfuturecrcindex)
		mod->gpl_future_crcs = (void *)sechdrs[gplfuturecrcindex].sh_addr;

#ifdef CONFIG_MODVERSIONS
	if ((mod->num_syms && !crcindex) || 
	    (mod->num_gpl_syms && !gplcrcindex) ||
	    (mod->num_gpl_future_syms && !gplfuturecrcindex)) {
		printk(KERN_WARNING "%s: No versions for exported symbols."
		       " Tainting kernel.\n", mod->name);
		add_taint(TAINT_FORCED_MODULE);
	}
#endif

	/* Now do relocations. */
	for (i = 1; i < hdr->e_shnum; i++) {
		const char *strtab = (char *)sechdrs[strindex].sh_addr;
		unsigned int info = sechdrs[i].sh_info;

		/* Not a valid relocation section? */
		if (info >= hdr->e_shnum)
			continue;

		/* Don't bother with non-allocated sections */
		if (!(sechdrs[info].sh_flags & SHF_ALLOC))
			continue;

		if (sechdrs[i].sh_type == SHT_REL)
			err = apply_relocate(sechdrs, strtab, symindex, i,mod);
		else if (sechdrs[i].sh_type == SHT_RELA)
			err = apply_relocate_add(sechdrs, strtab, symindex, i,
						 mod);
		if (err < 0)
			goto cleanup;
	}

        /* Find duplicate symbols */
	err = verify_export_symbols(mod);

	if (err < 0)
		goto cleanup;

  	/* Set up and sort exception table */
	mod->num_exentries = sechdrs[exindex].sh_size / sizeof(*mod->extable);
	mod->extable = extable = (void *)sechdrs[exindex].sh_addr;
	sort_extable(extable, extable + mod->num_exentries);

	/* Finally, copy percpu area over. */
	percpu_modcopy(mod->percpu, (void *)sechdrs[pcpuindex].sh_addr,
		       sechdrs[pcpuindex].sh_size);

	add_kallsyms(mod, sechdrs, symindex, strindex, secstrings);

	err = module_finalize(hdr, sechdrs, mod);
	if (err < 0)
		goto cleanup;

	/* flush the icache in correct context */
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	/*
	 * Flush the instruction cache, since we've played with text.
	 * Do it before processing of module parameters, so the module
	 * can provide parameter accessor functions of its own.
	 */
	if (mod->module_init)
		flush_icache_range((unsigned long)mod->module_init,
				   (unsigned long)mod->module_init
				   + mod->init_size);
	flush_icache_range((unsigned long)mod->module_core,
			   (unsigned long)mod->module_core + mod->core_size);

	set_fs(old_fs);

	mod->args = args;
	if (obsparmindex) {
		err = obsolete_params(mod->name, mod->args,
				      (struct obsolete_modparm *)
				      sechdrs[obsparmindex].sh_addr,
				      sechdrs[obsparmindex].sh_size
				      / sizeof(struct obsolete_modparm),
				      sechdrs, symindex,
				      (char *)sechdrs[strindex].sh_addr);
		if (setupindex)
			printk(KERN_WARNING "%s: Ignoring new-style "
			       "parameters in presence of obsolete ones\n",
			       mod->name);
	} else {
		/* Size of section 0 is 0, so this works well if no params */
		err = parse_args(mod->name, mod->args,
				 (struct kernel_param *)
				 sechdrs[setupindex].sh_addr,
				 sechdrs[setupindex].sh_size
				 / sizeof(struct kernel_param),
				 NULL);
	}
	if (err < 0)
		goto arch_cleanup;

	err = mod_sysfs_setup(mod, 
			      (struct kernel_param *)
			      sechdrs[setupindex].sh_addr,
			      sechdrs[setupindex].sh_size
			      / sizeof(struct kernel_param));
	if (err < 0)
		goto arch_cleanup;
	add_sect_attrs(mod, hdr->e_shnum, secstrings, sechdrs);

	/* Get rid of temporary copy */
	vfree(hdr);

	/* Done! */
	return mod;

 arch_cleanup:
	module_arch_cleanup(mod);
 cleanup:
	module_unload_free(mod);
	module_free(mod, mod->module_init);
 free_core:
	module_free(mod, mod->module_core);
 free_percpu:
	if (percpu)
		percpu_modfree(percpu);
 free_mod:
	kfree(args);
 free_hdr:
	vfree(hdr);
	return ERR_PTR(err);

 truncated:
	printk(KERN_ERR "Module len %lu truncated\n", len);
	err = -ENOEXEC;
	goto free_hdr;
}

/*
 * link the module with the whole machine is stopped with interrupts off
 * - this defends against kallsyms not taking locks
 */
static int __link_module(void *_mod)
{
	struct module *mod = _mod;
	list_add(&mod->list, &modules);
	return 0;
}

/* This is where the real work happens */
asmlinkage long
sys_init_module(void __user *umod,
		unsigned long len,
		const char __user *uargs)
{
	struct module *mod;
	int ret = 0;

	/* Must have permission */
	if (!capable(CAP_SYS_MODULE))
		return -EPERM;

	/* Only one module load at a time, please */
	if (down_interruptible(&module_mutex) != 0)
		return -EINTR;

	/* Do all the hard work */
	mod = load_module(umod, len, uargs);
	if (IS_ERR(mod)) {
		up(&module_mutex);
		return PTR_ERR(mod);
	}

	/* Now sew it into the lists.  They won't access us, since
           strong_try_module_get() will fail. */
	stop_machine_run(__link_module, mod, NR_CPUS);

	/* Drop lock so they can recurse */
	up(&module_mutex);

	down(&notify_mutex);
	notifier_call_chain(&module_notify_list, MODULE_STATE_COMING, mod);
	up(&notify_mutex);

	/* Start the module */
	if (mod->init != NULL)
		ret = mod->init();
	if (ret < 0) {
		/* Init routine failed: abort.  Try to protect us from
                   buggy refcounters. */
		mod->state = MODULE_STATE_GOING;
		synchronize_sched();
		if (mod->unsafe)
			printk(KERN_ERR "%s: module is now stuck!\n",
			       mod->name);
		else {
			module_put(mod);
			down(&module_mutex);
			free_module(mod);
			up(&module_mutex);
		}
		return ret;
	}

	/* Now it's a first class citizen! */
	down(&module_mutex);
	mod->state = MODULE_STATE_LIVE;
	/* Drop initial reference. */
	module_put(mod);
	module_free(mod, mod->module_init);
	mod->module_init = NULL;
	mod->init_size = 0;
	mod->init_text_size = 0;
	up(&module_mutex);

	return 0;
}

static inline int within(unsigned long addr, void *start, unsigned long size)
{
	return ((void *)addr >= start && (void *)addr < start + size);
}

#ifdef CONFIG_KALLSYMS
/*
 * This ignores the intensely annoying "mapping symbols" found
 * in ARM ELF files: $a, $t and $d.
 */
static inline int is_arm_mapping_symbol(const char *str)
{
	return str[0] == '$' && strchr("atd", str[1]) 
	       && (str[2] == '\0' || str[2] == '.');
}

static const char *get_ksymbol(struct module *mod,
			       unsigned long addr,
			       unsigned long *size,
			       unsigned long *offset)
{
	unsigned int i, best = 0;
	unsigned long nextval;

	/* At worse, next value is at end of module */
	if (within(addr, mod->module_init, mod->init_size))
		nextval = (unsigned long)mod->module_init+mod->init_text_size;
	else 
		nextval = (unsigned long)mod->module_core+mod->core_text_size;

	/* Scan for closest preceeding symbol, and next symbol. (ELF
           starts real symbols at 1). */
	for (i = 1; i < mod->num_symtab; i++) {
		if (mod->symtab[i].st_shndx == SHN_UNDEF)
			continue;

		/* We ignore unnamed symbols: they're uninformative
		 * and inserted at a whim. */
		if (mod->symtab[i].st_value <= addr
		    && mod->symtab[i].st_value > mod->symtab[best].st_value
		    && *(mod->strtab + mod->symtab[i].st_name) != '\0'
		    && !is_arm_mapping_symbol(mod->strtab + mod->symtab[i].st_name))
			best = i;
		if (mod->symtab[i].st_value > addr
		    && mod->symtab[i].st_value < nextval
		    && *(mod->strtab + mod->symtab[i].st_name) != '\0'
		    && !is_arm_mapping_symbol(mod->strtab + mod->symtab[i].st_name))
			nextval = mod->symtab[i].st_value;
	}

	if (!best)
		return NULL;

	*size = nextval - mod->symtab[best].st_value;
	*offset = addr - mod->symtab[best].st_value;
	return mod->strtab + mod->symtab[best].st_name;
}

/* For kallsyms to ask for address resolution.  NULL means not found.
   We don't lock, as this is used for oops resolution and races are a
   lesser concern. */
const char *module_address_lookup(unsigned long addr,
				  unsigned long *size,
				  unsigned long *offset,
				  char **modname)
{
	struct module *mod;

	list_for_each_entry(mod, &modules, list) {
		if (within(addr, mod->module_init, mod->init_size)
		    || within(addr, mod->module_core, mod->core_size)) {
			*modname = mod->name;
			return get_ksymbol(mod, addr, size, offset);
		}
	}
	return NULL;
}

struct module *module_get_kallsym(unsigned int symnum,
				  unsigned long *value,
				  char *type,
				  char namebuf[128])
{
	struct module *mod;

	down(&module_mutex);
	list_for_each_entry(mod, &modules, list) {
		if (symnum < mod->num_symtab) {
			*value = mod->symtab[symnum].st_value;
			*type = mod->symtab[symnum].st_info;
			strncpy(namebuf,
				mod->strtab + mod->symtab[symnum].st_name,
				127);
			up(&module_mutex);
			return mod;
		}
		symnum -= mod->num_symtab;
	}
	up(&module_mutex);
	return NULL;
}

static unsigned long mod_find_symname(struct module *mod, const char *name)
{
	unsigned int i;

	for (i = 0; i < mod->num_symtab; i++)
		if (strcmp(name, mod->strtab+mod->symtab[i].st_name) == 0 &&
		    mod->symtab[i].st_info != 'U')
			return mod->symtab[i].st_value;
	return 0;
}

/* Look for this name: can be of form module:name. */
unsigned long module_kallsyms_lookup_name(const char *name)
{
	struct module *mod;
	char *colon;
	unsigned long ret = 0;

	/* Don't lock: we're in enough trouble already. */
	if ((colon = strchr(name, ':')) != NULL) {
		*colon = '\0';
		if ((mod = find_module(name)) != NULL)
			ret = mod_find_symname(mod, colon+1);
		*colon = ':';
	} else {
		list_for_each_entry(mod, &modules, list)
			if ((ret = mod_find_symname(mod, name)) != 0)
				break;
	}
	return ret;
}
#endif /* CONFIG_KALLSYMS */

/* Called by the /proc file system to return a list of modules. */
static void *m_start(struct seq_file *m, loff_t *pos)
{
	struct list_head *i;
	loff_t n = 0;

	down(&module_mutex);
	list_for_each(i, &modules) {
		if (n++ == *pos)
			break;
	}
	if (i == &modules)
		return NULL;
	return i;
}

static void *m_next(struct seq_file *m, void *p, loff_t *pos)
{
	struct list_head *i = p;
	(*pos)++;
	if (i->next == &modules)
		return NULL;
	return i->next;
}

static void m_stop(struct seq_file *m, void *p)
{
	up(&module_mutex);
}

static int m_show(struct seq_file *m, void *p)
{
	struct module *mod = list_entry(p, struct module, list);
	seq_printf(m, "%s %lu",
		   mod->name, mod->init_size + mod->core_size);
	print_unload_info(m, mod);

	/* Informative for users. */
	seq_printf(m, " %s",
		   mod->state == MODULE_STATE_GOING ? "Unloading":
		   mod->state == MODULE_STATE_COMING ? "Loading":
		   "Live");
	/* Used by oprofile and other similar tools. */
	seq_printf(m, " 0x%p", mod->module_core);

	seq_printf(m, "\n");
	return 0;
}

/* Format: modulename size refcount deps address

   Where refcount is a number or -, and deps is a comma-separated list
   of depends or -.
*/
struct seq_operations modules_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= m_show
};

/* Given an address, look for it in the module exception tables. */
const struct exception_table_entry *search_module_extables(unsigned long addr)
{
	unsigned long flags;
	const struct exception_table_entry *e = NULL;
	struct module *mod;

	spin_lock_irqsave(&modlist_lock, flags);
	list_for_each_entry(mod, &modules, list) {
		if (mod->num_exentries == 0)
			continue;
				
		e = search_extable(mod->extable,
				   mod->extable + mod->num_exentries - 1,
				   addr);
		if (e)
			break;
	}
	spin_unlock_irqrestore(&modlist_lock, flags);

	/* Now, if we found one, we are running inside it now, hence
           we cannot unload the module, hence no refcnt needed. */
	return e;
}

/* Is this a valid kernel address?  We don't grab the lock: we are oopsing. */
struct module *__module_text_address(unsigned long addr)
{
	struct module *mod;

	list_for_each_entry(mod, &modules, list)
		if (within(addr, mod->module_init, mod->init_text_size)
		    || within(addr, mod->module_core, mod->core_text_size))
			return mod;
	return NULL;
}

struct module *module_text_address(unsigned long addr)
{
	struct module *mod;
	unsigned long flags;

	spin_lock_irqsave(&modlist_lock, flags);
	mod = __module_text_address(addr);
	spin_unlock_irqrestore(&modlist_lock, flags);

	return mod;
}

/* Don't grab lock, we're oopsing. */
void print_modules(void)
{
	struct module *mod;

	printk("Modules linked in:");
	list_for_each_entry(mod, &modules, list)
		printk(" %s", mod->name);
	printk("\n");
}

void module_add_driver(struct module *mod, struct device_driver *drv)
{
	if (!mod || !drv)
		return;

	/* Don't check return code; this call is idempotent */
	sysfs_create_link(&drv->kobj, &mod->mkobj.kobj, "module");
}
EXPORT_SYMBOL(module_add_driver);

void module_remove_driver(struct device_driver *drv)
{
	if (!drv)
		return;
	sysfs_remove_link(&drv->kobj, "module");
}
EXPORT_SYMBOL(module_remove_driver);

#ifdef CONFIG_MODVERSIONS
/* Generate the signature for struct module here, too, for modversions. */
void struct_module(struct module *mod) { return; }
EXPORT_SYMBOL(struct_module);
#endif
