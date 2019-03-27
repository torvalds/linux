/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
/**
 * @file
 *
 * @brief This file contains the interface to the iterator class.
 *        Methods Provided:
 *        - sci_iterator_get_object_size()
 *        - sci_iterator_get_current()
 *        - sci_iterator_first()
 *        - sci_iterator_next()
 */

#ifndef _SCI_ITERATOR_H_
#define _SCI_ITERATOR_H_

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#if !defined(DISABLE_SCI_ITERATORS)

//******************************************************************************
//*
//*     I N C L U D E S
//*
//******************************************************************************

#include <dev/isci/scil/sci_types.h>

//******************************************************************************
//*
//*     C O N S T A N T S
//*
//******************************************************************************

//******************************************************************************
//*
//*     T Y P E S
//*
//******************************************************************************

//******************************************************************************
//*
//*     P U B L I C       M E T H O D S
//*
//******************************************************************************

U32 sci_iterator_get_object_size(
   void
);

void * sci_iterator_get_current(
   SCI_ITERATOR_HANDLE_T iterator_handle
);

void sci_iterator_first(
   SCI_ITERATOR_HANDLE_T iterator_handle
);

void sci_iterator_next(
   SCI_ITERATOR_HANDLE_T iterator_handle
);

#else // !defined(DISABLE_SCI_ITERATORS)

#define sci_iterator_get_object_size() 0
#define sci_iterator_get_current(the_iterator) NULL
#define sci_iterator_first(the_iterator)
#define sci_iterator_next(the_iterator)

#endif // !defined(DISABLE_SCI_ITERATORS)

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _ITERATOR_H_
