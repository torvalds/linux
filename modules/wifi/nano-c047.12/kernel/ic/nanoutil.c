/* $Id: nanoutil.c 16108 2010-09-20 08:06:34Z anbg $ */
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
#include <linux/config.h>
#endif
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "nanoutil.h"
#include "driverenv.h"

#ifdef WIFI_DEBUG_ON
unsigned int nrx_debug = DEBUG_TRACE | DEBUG_ERROR;
EXPORT_SYMBOL(nrx_debug);
#endif

/* Workaround for freescale 2.6.24 */ 
#if defined(FORCE_SYSCTL_SYSCALL_CHECK) && (LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 24))
#define CONFIG_SYSCTL_SYSCALL_CHECK
#endif

#ifdef HAVE_STATIC_SYSCTL_NUMBERING
static int max_name;
#endif
struct nano_util_sysctl {
    struct ctl_table root[2];
    struct ctl_table_header *header;
    struct nano_util_sysctl *next;
} *head;

int
nano_util_register_sysctl(struct ctl_table *table)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33)
    struct ctl_table *p;
#endif
    struct  nano_util_sysctl *root = kmalloc(sizeof(*root), GFP_KERNEL);
    if(root == NULL)
	return -ENOMEM;

    memset(root, 0, sizeof(*root));

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33)
    for(p = table; p->ctl_name != 0; p++) {
#ifndef HAVE_STATIC_SYSCTL_NUMBERING
       p->ctl_name = CTL_UNNUMBERED;
#else
       p->ctl_name = ++max_name;
#endif
    }

#ifndef HAVE_STATIC_SYSCTL_NUMBERING
    root->root[0].ctl_name = CTL_UNNUMBERED;
#else
    root->root[0].ctl_name = 1001;
#endif
#endif /* < 2.6.33 */
    root->root[0].procname = "nano";
    root->root[0].mode = 0555;
    root->root[0].child = table;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 21)
    root->header = register_sysctl_table(root->root);
#else
    root->header = register_sysctl_table(root->root, 0);
#endif

    if(root->header == NULL) {
	kfree(root);
	return -ENOMEM;
    }
    root->next = head;
    head = root;

    return 0;
}
EXPORT_SYMBOL(nano_util_register_sysctl);

int
nano_util_unregister_sysctl(ctl_table *table)
{
    struct nano_util_sysctl *p, **q;
    KDEBUG(TRACE, "ENTRY");

    for(p = head, q = &head; p != NULL; q = &p->next, p = p->next) {
	if(p->root[0].child == table) {
	    *q = p->next;
	    unregister_sysctl_table(p->header);
	    memset(p, 0, sizeof(*p));
	    kfree(p);
	    return 0;
	}
    }
    KDEBUG(TRACE, "EXIT -ENOENT");
    return -ENOENT;
}
EXPORT_SYMBOL(nano_util_unregister_sysctl);

static ctl_table nano_util_ctable[] = {
#ifdef WIFI_DEBUG_ON
  { SYSCTLENTRY(debug, nrx_debug, proc_dointvec) },
  { SYSCTLENTRY(de_debug, trace_mask, proc_dointvec) },
#endif
  { SYSCTLEND }
};

void
nano_util_init(void)
{
    nano_util_register_sysctl(nano_util_ctable);
}

void
nano_util_cleanup(void)
{
    nano_util_unregister_sysctl(nano_util_ctable);
}


void
nano_util_printbuf(const void *data, size_t len, const char *prefix)
{
    size_t i, j;
    const unsigned char *p = data;

    for(i = 0; i < len; i += 16) {
		printk(PRINTK_LEVEL "%s %04zx: ", prefix, i);
		for(j = 0; j < 16; j++) {
	    	if(i + j < len)
				printk("%02x ", p[i+j]);
	    	else
				printk("   ");
		}
		printk(" : ");
		for(j = 0; j < 16; j++) {
	    	if(i + j < len) {
#define isprint(c) ((c) >= 32 && (c) <= 126)
				if(isprint(p[i+j]))
					printk("%c", p[i+j]);
				else
					printk(".");
			}
			else
				printk(" ");
		}
		printk("\n");
    }
}

EXPORT_SYMBOL(nano_util_printbuf);
