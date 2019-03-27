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
#ifndef _SCI_SIMPLE_LIST_HEADER_
#define _SCI_SIMPLE_LIST_HEADER_

/**
 * @file
 *
 * @brief This header file contains simple linked list manipulation macros.
 *        These macros differ from the SCI_FAST_LIST in that deletion of
 *        an element from the list is O(n).
 *        The reason for using this implementation over the SCI_FAST_LIST
 *        is
 *           1) space savings as there is only a single link element instead
 *              of the 2 link elements used in the SCI_FAST_LIST and
 *           2) it is possible to detach the entire list from its anchor
 *              element for processing.
 *
 * @note Do not use the SCI_SIMPLE_LIST if you need to remove elements from
 *       random locations within the list use instead the SCI_FAST_LIST.
 */


//******************************************************************************
//*
//*     P U B L I C    M E T H O D S
//*
//******************************************************************************

/**
 * Initialize the singely linked list anchor.  The other macros require the
 * list anchor to be properly initialized.
 */
#define sci_simple_list_init(anchor) \
{ \
   (anchor)->list_head = NULL; \
   (anchor)->list_tail = NULL; \
   (anchor)->list_count = 0; \
}

/**
 * Initialze the singely linked list element. The other macros require the
 * list element to be properly initialized.
 */
#define sci_simple_list_element_init(list_object, element) \
{ \
   (element)->next = NULL; \
   (element)->object = (list_object); \
}

/**
 * See if there are any list elements on this list.
 */
#define sci_simple_list_is_empty(anchor)  ((anchor)->list_head == NULL)

/**
 * Return a pointer to the list element at the head of the list.  The list
 * element is not removed from the list.
 */
#define sci_simple_list_get_head(anchor) ((anchor)->list_head)

/**
 * Retuen a pointer to the lsit element at the tail of the list.  The list
 * element is not removed from the list.
 */
#define sci_simple_list_get_tail(anchor) ((anchor)->list_tail)

/**
 * Return the count of the number of elements in this list.
 */
#define sci_simple_list_get_count(anchor) ((anchor)->list_count)

/**
 * Return a pointer to the list element following this list element.
 * If this is the last element in the list then NULL is returned.
 */
#define sci_simple_list_get_next(element) ((element)->next)

/**
 * Return the object represented by the list element.
 */
#define sci_simple_list_get_object(element) ((element)->object)


//******************************************************************************
//*
//*     T Y P E S
//*
//******************************************************************************

/**
 * @struct
 *
 * @brief This structure defines the list owner for singely linked list.
 */
typedef struct SCI_SIMPLE_LIST
{
   struct SCI_SIMPLE_LIST_ELEMENT *list_head;
   struct SCI_SIMPLE_LIST_ELEMENT *list_tail;
   U32                             list_count;
} SCI_SIMPLE_LIST_T;

/**
 * @struct SCI_SIMPLE_LIST_ELEMENT
 *
 * @brief This structure defines what a singely linked list element contains.
 */
typedef struct SCI_SIMPLE_LIST_ELEMENT
{
   struct SCI_SIMPLE_LIST_ELEMENT *next;
   void                           *object;
} SCI_SIMPLE_LIST_ELEMENT_T;

/**
 * This method will insert the list element to the head of the list contained
 * by the anchor.
 *
 * @note Pushing new elements onto a list is more efficient than inserting
 *       them to the tail of the list though both are O(1) operations.
 */
INLINE
static void sci_simple_list_insert_head(
   SCI_SIMPLE_LIST_T * anchor,
   SCI_SIMPLE_LIST_ELEMENT_T *element
)
{
   if (anchor->list_tail == NULL)
   {
      anchor->list_tail = element;
   }

   element->next = anchor->list_head;
   anchor->list_head = element;
   anchor->list_count++;
}

/**
 * This methos will insert the list element to the tail of the list contained
 * by the anchor.
 *
 * @param[in, out] anchor this is the list into which the element is to be
 *                 inserted
 * @param[in] element this is the element which to insert into the list.
 *
 * @note Pushing new elements onto a list is more efficient than inserting
 *       them to the tail of the list though both are O(1) operations.
 */
INLINE
static void sci_simple_list_insert_tail(
   SCI_SIMPLE_LIST_T * anchor,
   SCI_SIMPLE_LIST_ELEMENT_T *element
)
{
   if (anchor->list_tail == NULL)
   {
      anchor->list_head = element;
   }
   else
   {
      anchor->list_tail->next = element;
   }

   anchor->list_tail = element;
   anchor->list_count++;
}

/**
 * This method will remove the list element from the anchor and return the
 * object pointed to by that list element.
 *
 * @param[in, out] anchor this is the list into which the element is to be
 *                 inserted
 *
 * @return the list element at the head of the list.
 */
INLINE
static void * sci_simple_list_remove_head(
   SCI_SIMPLE_LIST_T * anchor
)
{
   void * object = NULL;

   if (anchor->list_head != NULL)
   {
      object = anchor->list_head->object;

      anchor->list_head = anchor->list_head->next;

      if (anchor->list_head == NULL)
      {
         anchor->list_tail = NULL;
      }

      anchor->list_count--;
   }

   return object;
}

/**
 * Move all the list elements from source anchor to the dest anchor.
 * The source anchor will have all of its elements removed making it
 * an empty list and the dest anchor will contain all of the source
 * and dest list elements.
 *
 * @param[in, out] dest_anchor this is the list into which all elements from
 *                 the source list are to be moved.
 * @param[in, out] source_anchor this is the list which is to be moved to the
 *                 destination list.  This list will be empty on return.
 *
 * @return the list element at the head of the list.
 * @note If the destination has list elements use the insert at head
 *       or tail routines instead.
 */
INLINE
static void sci_simple_list_move_list(
   SCI_SIMPLE_LIST_T * dest_anchor,
   SCI_SIMPLE_LIST_T * source_anchor
)
{
   *dest_anchor = *source_anchor;

   sci_simple_list_init(source_anchor);
}

/**
 * This method will insert the list elements from the source anchor to the
 * destination list before all previous elements on the destination list.
 *
 * @param[in, out] dest_anchor this is the list into which all elements from
 *                 the source list are to be moved. The destination list will
 *                 now contain both sets of list elements.
 * @param[in, out] source_anchor this is the list which is to be moved to the
 *                 destination list.  This list will be empty on return.
 */
INLINE
static void sci_simple_list_insert_list_at_head(
   SCI_SIMPLE_LIST_T * dest_anchor,
   SCI_SIMPLE_LIST_T * source_anchor
)
{
   if (!sci_simple_list_is_empty(source_anchor))
   {
      if (sci_simple_list_is_empty(dest_anchor))
      {
         // Destination is empty just copy the source on over
         *dest_anchor = *source_anchor;
      }
      else
      {
         source_anchor->list_tail->next = dest_anchor->list_head;
         dest_anchor->list_head = source_anchor->list_head;
         dest_anchor->list_count += source_anchor->list_count;
      }

      // Wipe the source list to make sure the list elements can not be accessed
      // from two separate lists at the same time.
      sci_simple_list_init(source_anchor);
   }
}

/**
 * This method will insert the list elements from the source anchor to the
 * destination anchor after all list elements on the destination anchor.
 *
 * @param[in, out] dest_anchor this is the list into which all elements from
 *                 the source list are to be moved. The destination list will
 *                 contain both the source and destination list elements.
 * @param[in, out] source_anchor this is the list which is to be moved to the
 *                 destination list.  This list will be empty on return.
 */
INLINE
static void sci_simple_list_insert_list_at_tail(
   SCI_SIMPLE_LIST_T * dest_anchor,
   SCI_SIMPLE_LIST_T * source_anchor
)
{
   if (!sci_simple_list_is_empty(source_anchor))
   {
      if (sci_simple_list_is_empty(dest_anchor))
      {
         // Destination is empty just copy the source on over
         *dest_anchor = *source_anchor;
      }
      else
      {
         // If the source list is empty the desination list is the result.
         dest_anchor->list_tail->next = source_anchor->list_head;
         dest_anchor->list_tail = source_anchor->list_tail;
         dest_anchor->list_count += source_anchor->list_count;
      }

      // Wipe the source list to make sure the list elements can not be accessed
      // from two separate lists at the same time.
      sci_simple_list_init(source_anchor);
   }
}

#endif // _SCI_SIMPLE_LIST_HEADER_
