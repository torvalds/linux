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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/**
 * @file
 *
 * @brief This file contains the implementation of an iterator class.
 *        This class will allow for iterating across the elements of a
 *        container.
 */

#if !defined(DISABLE_SCI_ITERATORS)

//******************************************************************************
//*
//*     I N C L U D E S
//*
//******************************************************************************

#include <dev/isci/scil/sci_base_iterator.h>

//******************************************************************************
//*
//*     P R I V A T E   M E M B E R S
//*
//******************************************************************************

//******************************************************************************
//*
//*     P R O T E C T E D   M E T H O D S
//*
//******************************************************************************

/**
 * @brief Return the size of an iterator object.
 *
 * @return U32 : size of iterator object in bytes.
 *
 */
U32 sci_iterator_get_object_size(
   void
)
{
    return sizeof(SCI_BASE_ITERATOR_T);
}

/**
 * @brief Initialize the interator.
 *
 * @param[in] iterator This parameter specifies the iterator to be
 *            constructed.
 * @param[in] list This parameter specifies the abstract list that will be
 *            iterated on by this iterator.  The iterator will by initialized
 *            to point to the first element in this abstract list.
 *
 * @return none
 */
void sci_base_iterator_construct(
   SCI_ITERATOR_HANDLE_T   iterator_handle,
   SCI_ABSTRACT_LIST_T   * list
)
{
    SCI_BASE_ITERATOR_T * iterator = (SCI_BASE_ITERATOR_T *) iterator_handle;

    memset(iterator, 0, sizeof(SCI_BASE_ITERATOR_T));
    iterator->list = list;
    sci_iterator_first(iterator);
}

/**
 * @brief Get the object currently pointed to by this iterator.
 *
 * @param[in] iterator_handle Handle to an iterator.
 *
 * @return void * : Object pointed to by this iterator.
 * @retval NULL If iterator is not currently pointing to a valid element.
 */
void * sci_iterator_get_current(
   SCI_ITERATOR_HANDLE_T iterator_handle
)
{
   SCI_BASE_ITERATOR_T * iterator = (SCI_BASE_ITERATOR_T *)iterator_handle;

   void *current_object = NULL;

   if (iterator->current != NULL)
   {
      current_object = sci_abstract_list_get_object(iterator->current);
   }

   return current_object;
}

/**
 * @brief Modify the iterator to point to the first element in the list.
 *
 * @param[in] iterator
 *
 * @return none
 */
void sci_iterator_first(
   SCI_ITERATOR_HANDLE_T iterator_handle
)
{
   SCI_BASE_ITERATOR_T * iterator = (SCI_BASE_ITERATOR_T *)iterator_handle;

   iterator->current = sci_abstract_list_get_front(iterator->list);
}

/**
 * @brief Modify the iterator to point to the next element in the list.
 *
 * @param[in] iterator
 *
 * @return none
 */
void sci_iterator_next(
   SCI_ITERATOR_HANDLE_T iterator_handle
)
{
   SCI_BASE_ITERATOR_T * iterator = (SCI_BASE_ITERATOR_T *)iterator_handle;

   if (iterator->current != NULL)
   {
      iterator->current = sci_abstract_list_get_next(iterator->current);
   }
}

#endif // !defined(DISABLE_SCI_ITERATORS)

