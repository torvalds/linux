//------------------------------------------------------------------------------
// Copyright (c) 2010 Atheros Corporation.  All rights reserved.
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

#ifndef __WAC_API_H__
#define __WAC_API_H__

typedef enum {
    WAC_SET,
    WAC_GET,
} WAC_REQUEST_TYPE;

typedef enum {
    WAC_ADD,
    WAC_DEL,
    WAC_GET_STATUS,
    WAC_GET_IE,
} WAC_COMMAND;

typedef enum {
    PRBREQ,
    PRBRSP,
    BEACON,
} WAC_FRAME_TYPE;

typedef enum {
    WAC_FAILED_NO_WAC_AP = -4,
    WAC_FAILED_LOW_RSSI = -3,
    WAC_FAILED_INVALID_PARAM = -2,
    WAC_FAILED_REJECTED = -1,
    WAC_SUCCESS = 0,
    WAC_DISABLED = 1,
    WAC_PROCEED_FIRST_PHASE,
    WAC_PROCEED_SECOND_PHASE,
} WAC_STATUS;


#endif
