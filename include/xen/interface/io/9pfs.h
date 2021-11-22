/* SPDX-License-Identifier: MIT */
/*
 * 9pfs.h -- Xen 9PFS transport
 *
 * Copyright (C) 2017 Stefano Stabellini <stefano@aporeto.com>
 */

#ifndef __XEN_PUBLIC_IO_9PFS_H__
#define __XEN_PUBLIC_IO_9PFS_H__

#include "xen/interface/io/ring.h"

/*
 * See docs/misc/9pfs.markdown in xen.git for the full specification:
 * https://xenbits.xen.org/docs/unstable/misc/9pfs.html
 */
DEFINE_XEN_FLEX_RING_AND_INTF(xen_9pfs);

#endif
