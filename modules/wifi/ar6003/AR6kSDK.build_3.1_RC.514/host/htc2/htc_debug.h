//------------------------------------------------------------------------------
// <copyright file="htc_debug.h" company="Atheros">
//    Copyright (c) 2007-2010 Atheros Corporation.  All rights reserved.
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
#ifndef HTC_DEBUG_H_
#define HTC_DEBUG_H_

#define ATH_MODULE_NAME htc
#include "a_debug.h"

/* ------- Debug related stuff ------- */

#define  ATH_DEBUG_SEND ATH_DEBUG_MAKE_MODULE_MASK(0)
#define  ATH_DEBUG_RECV ATH_DEBUG_MAKE_MODULE_MASK(1)
#define  ATH_DEBUG_SYNC ATH_DEBUG_MAKE_MODULE_MASK(2)
#define  ATH_DEBUG_DUMP ATH_DEBUG_MAKE_MODULE_MASK(3)
#define  ATH_DEBUG_IRQ  ATH_DEBUG_MAKE_MODULE_MASK(4)


#endif /*HTC_DEBUG_H_*/
