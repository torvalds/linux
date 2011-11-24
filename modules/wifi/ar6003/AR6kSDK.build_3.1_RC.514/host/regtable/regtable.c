/*
 * Copyright (c) 2010 Atheros Communications, Inc.
 * All rights reserved.
 *
 *
 * 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
 *
 */


#include "common_drv.h"
#include "bmi_msg.h"

#include "targetdef.h"
#include "hostdef.h"
#include "hif.h"


/* Target-dependent addresses and definitions */
struct targetdef_s *targetdef;
/* HIF-dependent addresses and definitions */
struct hostdef_s *hostdef;

void target_register_tbl_attach(A_UINT32 target_type)
{
    switch (target_type) {
    case TARGET_TYPE_AR6002:
        targetdef = AR6002_TARGETdef;
        break;
    case TARGET_TYPE_AR6003:
        targetdef = AR6003_TARGETdef;
        break;
    case TARGET_TYPE_MCKINLEY:
        targetdef = MCKINLEY_TARGETdef;
        break;
    default:
        break;
    }
}

void hif_register_tbl_attach(A_UINT32 hif_type)
{
    switch (hif_type) {
    case HIF_TYPE_AR6002:
        hostdef = AR6002_HOSTdef;
        break;
    case HIF_TYPE_AR6003:
        hostdef = AR6003_HOSTdef;
        break;
    case HIF_TYPE_MCKINLEY:
        hostdef = MCKINLEY_HOSTdef;
        break;
    default:
        break;
    }
}

EXPORT_SYMBOL(targetdef);
EXPORT_SYMBOL(hostdef);
