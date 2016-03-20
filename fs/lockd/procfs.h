/*
 * Procfs support for lockd
 *
 * Copyright (c) 2014 Jeff Layton <jlayton@primarydata.com>
 */
#ifndef _LOCKD_PROCFS_H
#define _LOCKD_PROCFS_H

#include <linux/kconfig.h>

#if IS_ENABLED(CONFIG_PROC_FS)
int lockd_create_procfs(void);
void lockd_remove_procfs(void);
#else
static inline int
lockd_create_procfs(void)
{
	return 0;
}

static inline void
lockd_remove_procfs(void)
{
	return;
}
#endif /* IS_ENABLED(CONFIG_PROC_FS) */

#endif /* _LOCKD_PROCFS_H */
