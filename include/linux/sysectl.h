#ifndef _LINUX_SYSECTL_H_
#define _LINUX_SYSECTL_H_

#ifdef CONFIG_SYSECTL
#include "sysectl_types.h"


// Entry from system call to sysectl filtering
long sysectl_entry(long nbr);

void sysectl_release(struct sysectl *sysectl);

#endif /* CONFIG_SYSECTL */
#endif /* _LINUX_SYSECTL_H_ */

