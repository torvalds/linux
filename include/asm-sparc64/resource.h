/* $Id: resource.h,v 1.8 2000/09/23 02:09:21 davem Exp $
 * resource.h: Resource definitions.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_RESOURCE_H
#define _SPARC64_RESOURCE_H

/*
 * These two resource limit IDs have a Sparc/Linux-specific ordering,
 * the rest comes from the generic header:
 */
#define RLIMIT_NOFILE		6	/* max number of open files */
#define RLIMIT_NPROC		7	/* max number of processes */

#include <asm-generic/resource.h>

#endif /* !(_SPARC64_RESOURCE_H) */
