#ifndef LINUX_KMEMCHECK_H
#define LINUX_KMEMCHECK_H

#include <linux/mm_types.h>
#include <linux/types.h>

#ifdef CONFIG_KMEMCHECK
extern int kmemcheck_enabled;

int kmemcheck_show_addr(unsigned long address);
int kmemcheck_hide_addr(unsigned long address);
#else
#define kmemcheck_enabled 0

#endif /* CONFIG_KMEMCHECK */

#endif /* LINUX_KMEMCHECK_H */
