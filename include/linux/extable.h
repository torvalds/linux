#ifndef _LINUX_EXTABLE_H
#define _LINUX_EXTABLE_H

#include <linux/stddef.h>	/* for NULL */

struct module;
struct exception_table_entry;

const struct exception_table_entry *
search_extable(const struct exception_table_entry *first,
	       const struct exception_table_entry *last,
	       unsigned long value);
void sort_extable(struct exception_table_entry *start,
		  struct exception_table_entry *finish);
void sort_main_extable(void);
void trim_init_extable(struct module *m);

/* Given an address, look for it in the exception tables */
const struct exception_table_entry *search_exception_tables(unsigned long add);

#ifdef CONFIG_MODULES
/* For extable.c to search modules' exception tables. */
const struct exception_table_entry *search_module_extables(unsigned long addr);
#else
static inline const struct exception_table_entry *
search_module_extables(unsigned long addr)
{
	return NULL;
}
#endif /*CONFIG_MODULES*/

#endif /* _LINUX_EXTABLE_H */
