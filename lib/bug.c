// SPDX-License-Identifier: GPL-2.0
/*
  Generic support for ()

  This respects the following config options:

  CONFIG_ - emit  traps.  Nothing happens without this.
  CONFIG_GENERIC_ - enable this code.
  CONFIG_GENERIC__RELATIVE_POINTERS - use 32-bit pointers relative to
	the containing struct _entry for _addr and file.
  CONFIG_DE_VERBOSE - emit full file+line information for each 

  CONFIG_ and CONFIG_DE_VERBOSE are potentially user-settable
  (though they're generally always on).

  CONFIG_GENERIC_ is set by each architecture using this code.

  To use this, your architecture must:

  1. Set up the config options:
     - Enable CONFIG_GENERIC_ if CONFIG_

  2. Implement  (and optionally _ON, WARN, WARN_ON)
     - Define HAVE_ARCH_
     - Implement () to generate a faulting instruction
     - NOTE: struct _entry does not have "file" or "line" entries
       when CONFIG_DE_VERBOSE is not enabled, so you must generate
       the values accordingly.

  3. Implement the trap
     - In the illegal instruction trap handler (typically), verify
       that the fault was in kernel mode, and call report_()
     - report_() will return whether it was a false alarm, a warning,
       or an actual .
     - You must implement the is_valid_addr(addr) callback which
       returns true if the eip is a real kernel address, and it points
       to the expected  trap instruction.

    Jeremy Fitzhardinge <jeremy@goop.org> 2006
 */

#define pr_fmt(fmt) fmt

#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/.h>
#include <linux/sched.h>
#include <linux/rculist.h>

extern struct _entry __start____table[], __stop____table[];

static inline unsigned long _addr(const struct _entry *)
{
#ifndef CONFIG_GENERIC__RELATIVE_POINTERS
	return ->_addr;
#else
	return (unsigned long) + ->_addr_disp;
#endif
}

#ifdef CONFIG_MODULES
/* Updates are protected by module mutex */
static LIST_HEAD(module__list);

static struct _entry *module_find_(unsigned long addr)
{
	struct module *mod;
	struct _entry * = NULL;

	rcu_read_lock_sched();
	list_for_each_entry_rcu(mod, &module__list, _list) {
		unsigned i;

		 = mod->_table;
		for (i = 0; i < mod->num_s; ++i, ++)
			if (addr == _addr())
				goto out;
	}
	 = NULL;
out:
	rcu_read_unlock_sched();

	return ;
}

void module__finalize(const Elf_Ehdr *hdr, const Elf_Shdr *sechdrs,
			 struct module *mod)
{
	char *secstrings;
	unsigned int i;

	lockdep_assert_held(&module_mutex);

	mod->_table = NULL;
	mod->num_s = 0;

	/* Find the ___table section, if present */
	secstrings = (char *)hdr + sechdrs[hdr->e_shstrndx].sh_offset;
	for (i = 1; i < hdr->e_shnum; i++) {
		if (strcmp(secstrings+sechdrs[i].sh_name, "___table"))
			continue;
		mod->_table = (void *) sechdrs[i].sh_addr;
		mod->num_s = sechdrs[i].sh_size / sizeof(struct _entry);
		break;
	}

	/*
	 * Strictly speaking this should have a spinlock to protect against
	 * traversals, but since we only traverse on ()s, a spinlock
	 * could potentially lead to deadlock and thus be counter-productive.
	 * Thus, this uses RCU to safely manipulate the  list, since 
	 * must run in non-interruptive state.
	 */
	list_add_rcu(&mod->_list, &module__list);
}

void module__cleanup(struct module *mod)
{
	lockdep_assert_held(&module_mutex);
	list_del_rcu(&mod->_list);
}

#else

static inline struct _entry *module_find_(unsigned long addr)
{
	return NULL;
}
#endif

struct _entry *find_(unsigned long addr)
{
	struct _entry *;

	for ( = __start____table;  < __stop____table; ++)
		if (addr == _addr())
			return ;

	return module_find_(addr);
}

enum _trap_type report_(unsigned long addr, struct pt_regs *regs)
{
	struct _entry *;
	const char *file;
	unsigned line, warning, once, done;

	if (!is_valid_addr(addr))
		return _TRAP_TYPE_NONE;

	 = find_(addr);
	if (!)
		return _TRAP_TYPE_NONE;

	file = NULL;
	line = 0;
	warning = 0;

	if () {
#ifdef CONFIG_DE_VERBOSE
#ifndef CONFIG_GENERIC__RELATIVE_POINTERS
		file = ->file;
#else
		file = (const char *) + ->file_disp;
#endif
		line = ->line;
#endif
		warning = (->flags & FLAG_WARNING) != 0;
		once = (->flags & FLAG_ONCE) != 0;
		done = (->flags & FLAG_DONE) != 0;

		if (warning && once) {
			if (done)
				return _TRAP_TYPE_WARN;

			/*
			 * Since this is the only store, concurrency is not an issue.
			 */
			->flags |= FLAG_DONE;
		}
	}

	if (warning) {
		/* this is a WARN_ON rather than /_ON */
		__warn(file, line, (void *)addr, _GET_TAINT(), regs,
		       NULL);
		return _TRAP_TYPE_WARN;
	}

	printk(KERN_DEFAULT CUT_HERE);

	if (file)
		pr_crit("kernel  at %s:%u!\n", file, line);
	else
		pr_crit("Kernel  at %pB [verbose de info unavailable]\n",
			(void *)addr);

	return _TRAP_TYPE_;
}

static void clear_once_table(struct _entry *start, struct _entry *end)
{
	struct _entry *;

	for ( = start;  < end; ++)
		->flags &= ~FLAG_DONE;
}

void generic__clear_once(void)
{
#ifdef CONFIG_MODULES
	struct module *mod;

	rcu_read_lock_sched();
	list_for_each_entry_rcu(mod, &module__list, _list)
		clear_once_table(mod->_table,
				 mod->_table + mod->num_s);
	rcu_read_unlock_sched();
#endif

	clear_once_table(__start____table, __stop____table);
}
