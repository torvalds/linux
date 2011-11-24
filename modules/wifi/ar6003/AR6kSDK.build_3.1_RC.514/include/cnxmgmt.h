//------------------------------------------------------------------------------
// <copyright file="cnxmgmt.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
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
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================

#ifndef _CNXMGMT_H_
#define _CNXMGMT_H_

typedef enum {
    CM_CONNECT_WITHOUT_SCAN             = 0x0001,
    CM_CONNECT_ASSOC_POLICY_USER        = 0x0002,
    CM_CONNECT_SEND_REASSOC             = 0x0004,
    CM_CONNECT_WITHOUT_ROAMTABLE_UPDATE = 0x0008,
    CM_CONNECT_DO_WPA_OFFLOAD           = 0x0010,
    CM_CONNECT_DO_NOT_DEAUTH            = 0x0020,
} CM_CONNECT_TYPE;

#endif  /* _CNXMGMT_H_ */
