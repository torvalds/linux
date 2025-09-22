/* Public domain. */

#ifndef _LINUX_KGDB_H
#define _LINUX_KGDB_H

#include <sys/types.h>
#include <sys/systm.h>
#include <linux/ftrace.h> /* via linux/kprobes.h */

static inline int
in_dbg_master(void)
{
#ifdef DDB
	return (db_active);
#endif
	return (0);
}

#endif
