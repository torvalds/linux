/*
 * Copyright (c) 2005-2006 Atheros Communications, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


#ifndef _DFS_PROJECT_H_
#define _DFS_PROJECT_H_

#ifdef ATH_SUPPORT_DFS

#include <a_config.h>
#include <athdefs.h>
#include <a_types.h>
#include <a_osapi.h>
#include <a_debug.h>
#include <queue.h> /* XXX: This is in target dir */
#include "dfs_common.h"
#include "ar6000_drv.h"
#include "project.h"

#define ATH_DFS_CAPINFO WMI_DFS_HOST_ATTACH_EVENT

#define OS_HDL void *
#define DEV_HDL void *

#define DFS_MALLOC(os_hdl, nbytes) A_MALLOC(nbytes)

#define DFS_DPRINTK(pDfs, _m, _fmt, ...) do {             \
    if ((_m) <= pDfs->dfs_debug_level) {               \
        A_PRINTF(_fmt, __VA_ARGS__);  \
    }        \
} while (0)


void dfs_radar_task (unsigned long arg);

#define adf_os_packed

typedef enum {
    AH_FALSE = 0,       /* NB: lots of code assumes false is zero */
    AH_TRUE  = 1,
} HAL_BOOL;
#endif /* ATH_SUPPORT_DFS */

#endif  /* _DFS_PROJECT_H_ */
