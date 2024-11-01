/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copied from the kernel sources to tools/perf/:
 *
 * Generic barrier definitions.
 *
 * It should be possible to use these on really simple architectures,
 * but it serves more as a starting point for new ports.
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#ifndef __TOOLS_LINUX_ASM_GENERIC_BARRIER_H
#define __TOOLS_LINUX_ASM_GENERIC_BARRIER_H

#ifndef __ASSEMBLY__

#include <linux/compiler.h>

/*
 * Force strict CPU ordering. And yes, this is required on UP too when we're
 * talking to devices.
 *
 * Fall back to compiler barriers if nothing better is provided.
 */

#ifndef mb
#define mb()	barrier()
#endif

#ifndef rmb
#define rmb()	mb()
#endif

#ifndef wmb
#define wmb()	mb()
#endif

#endif /* !__ASSEMBLY__ */
#endif /* __TOOLS_LINUX_ASM_GENERIC_BARRIER_H */
