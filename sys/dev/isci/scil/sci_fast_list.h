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
#ifndef _SCI_FAST_LIST_HEADER_
#define _SCI_FAST_LIST_HEADER_

/**
 * @file
 *
 * @brief Header file that contains basic Linked List manipulation macros.
 *        These macros implement a double linked list (SCI_FAST_LIST_T) that is
 *        circular in nature.  This means that the next and prev pointers for
 *        an empty queue head always the address of the queue head
 *        SCI_FAST_LIST_T.  Likewise an element that has been removed from
 *        a queue will have its next and prev pointer set to the address of
 *        the SCI_FAST_LIST_T found in the structure just removed from the
 *        queue.   Pointers in this implementation never == NULL.
 *
 *        Definitions:
 *        - anchor : This is the list container and has a
 *                   pointer to both the head and tail of the
 *                   list elements
 *        - element: This is the list element not the actual
 *                   object but the list element which has a
 *                   pointer to the object.
 */

//******************************************************************************
//*
//*     P U B L I C   M E T H O D S
//*
//******************************************************************************

/**
 * Initialize the double linked list anchor.  The other macros require the list
 * anchor to be set up using this macro.
 */
#define sci_fast_list_init(anchor)                                            \
{                                                                             \
   (anchor)->list_head = NULL;                                                \
   (anchor)->list_tail = NULL;                                                \
   (anchor)->element_count = 0;                                               \
}

/**
 * Initialize the sci_fast_list_element to point to its owning object
 */
#define sci_fast_list_element_init(list_object, element)                      \
{                                                                             \
   (element)->object = (list_object);                                         \
   (element)->next = (element)->prev = NULL;                                  \
   (element)->owning_list = NULL;                                             \
}

/**
 * See if there is anything on the list by checking the list anchor.
 */
#define sci_fast_list_is_empty(anchor) ((anchor)->list_head == NULL)

/**
 * Return a pointer to the element at the head of the sci_fast_list.  The
 * item is NOT removed from the list.
 *
 * NOTE: This macro will always return a value, even if the list is empty.
 *       You must insure the list is not empty or use Dlist_safeGetHead.
 *
 * element - A pointer into which to save the address of the structure
 *           containing the SCI_FAST_LIST at the list head.
 */
#define sci_fast_list_get_head(anchor)                                        \
   ((anchor)->list_head == NULL ? NULL: (anchor)->list_head->object)

/**
 * Return a pointer to the element at the tail of the sci_fast_list.  The item
 * is NOT removed from the list.
 *
 * NOTE: This macro will always return a value, even if the list is empty.
 *       You must insure the list is not empty or use Dlist_safeGetHead.
 *
 * element - A pointer into which to save the address of the structure
 *           containing the SCI_FAST_LIST at the list head.
 */
#define sci_fast_list_get_tail(anchor)                                        \
   ((anchor)->list_tail == NULL ? NULL: (anchor)->list_head->object)

/**
 * This method will get the next dListField in the SCI_FAST_LIST.  This method
 * returns a pointer to a SCI_FAST_LIST object.
 */
#define sci_fast_list_get_next(element) ((element)->next)

/**
 * This method will get the prev dListField in the SCI_FAST_LIST.  This method
 * returns a pointer to a SCI_FAST_LIST object.
 */
#define sci_fast_list_get_prev(element) ((element)->prev)


/**
 * This method returns the object that is represented by this
 * sci_fast_list_element
 */
#define sci_fast_list_get_object(element) ((element)->object)

/**
 * This method will determine if the supplied dListField is on a SCI_FAST_LIST.
 * If the element has only one dListField but can be on more than one list,
 * this will only tell you that it is on one of them.  If the element has
 * multiple dListFields and can exist on multiple lists at the same time, this
 * macro can tell you exactly which list it is on.
 */
#define sci_fast_list_is_on_a_list(element) ((element)->owning_list != NULL)

/**
 * This method will determine if the supplied dListFieldName is on the given
 * specified list?  If the element can be on more than one list, this
 * allows you to determine exactly which list it is on.  Performs a linear
 * search through the list.
 * result - BOOL_T that will contain the result of the search.
 *          TRUE - item is on the list described by head.
 *          FALSE - item is not on the list.
 */
#define sci_fast_list_is_on_this_list(anchor, element) \
   ((element)->owning_list == (anchor))

//******************************************************************************
//*
//*     T Y P E S
//*
//******************************************************************************

/**
 * @struct SCI_FAST_LIST
 *
 * @brief the list owner or list anchor for a set of SCI_FAST_LIST
 *        elements.
 */
typedef struct SCI_FAST_LIST
{
   struct SCI_FAST_LIST_ELEMENT *list_head;
   struct SCI_FAST_LIST_ELEMENT *list_tail;
   int                           element_count;
} SCI_FAST_LIST_T;

/**
 * @struct SCI_FAST_LIST_ELEMENT
 *
 * @brief This structure defines what a doubly linked list element contains.
 */
typedef struct SCI_FAST_LIST_ELEMENT
{
   struct SCI_FAST_LIST_ELEMENT *next;
   struct SCI_FAST_LIST_ELEMENT *prev;
   struct SCI_FAST_LIST         *owning_list;
   void                         *object;
} SCI_FAST_LIST_ELEMENT_T;


/**
 * Insert an element to be the new head of the list hanging off of the list
 * anchor.  An empty list has the list anchor pointing to itself.
 * dListAnchor - The name of the SCI_FAST_LIST_T element that is the anchor
 *               of the queue.
 * dListFieldBeingInserted - The SCI_FAST_LIST_T field in the data structure
 *                           being queued.  This SCI_FAST_LIST will become
 *                           the new list head.
 */
INLINE
static void sci_fast_list_insert_head(
    SCI_FAST_LIST_T *anchor,
    SCI_FAST_LIST_ELEMENT_T *element
)
{
    element->owning_list = anchor;
    element->prev = NULL;
    if ( anchor->list_head == NULL )
        anchor->list_tail = element;
    else
        anchor->list_head->prev = element;
    element->next = anchor->list_head;
    anchor->list_head = element;
    anchor->element_count++;
}

/**
 * Insert an element at the tail of the list.  Since the list is circular we
 * can add the element at the tail through use the list anchors previous
 * pointer.
 * dListAnchor - The name of the SCI_FAST_LIST_T element that is the anchor
 *               of the queue.
 * dListFieldBeingInserted - The SCI_FAST_LIST_T field in the data structure
 *                           being queued.  This SCI_FAST_LIST will become
 *                           the new list head.
 */
INLINE
static void sci_fast_list_insert_tail(
    SCI_FAST_LIST_T *anchor,
    SCI_FAST_LIST_ELEMENT_T *element
)
{
    element->owning_list = anchor;
    element->next = NULL;
    if ( anchor->list_tail == NULL ) {
        anchor->list_head = element;
    } else {
        anchor->list_tail->next = element;
    }
    element->prev = anchor->list_tail;
    anchor->list_tail = element;
    anchor->element_count++;
}

/**
 * This method will remove a dListFieldName from the head of the list.
 *
 * NOTE: This macro will always return a value, even if the list is empty.
 *       You must insure the list is not empty or use Dlist_safeRemoveHead.
 *
 * element - A pointer into which to save the address of the structure
 *           containing the SCI_FAST_LIST at the list head.
 */
INLINE
static void *sci_fast_list_remove_head(
    SCI_FAST_LIST_T *anchor
)
{
    void *object = NULL;
    SCI_FAST_LIST_ELEMENT_T *element;
    if ( anchor->list_head != NULL )
    {
        element = anchor->list_head;
        object = anchor->list_head->object;
        anchor->list_head = anchor->list_head->next;
        if ( anchor->list_head == NULL )
        {
            anchor->list_tail = NULL;
        }
        anchor->element_count--;
        element->next = element->prev = NULL;
        element->owning_list = NULL;
    }
    return object;
}

INLINE
static void *sci_fast_list_remove_tail(
    SCI_FAST_LIST_T *anchor
)
{
    void *object = NULL;
    SCI_FAST_LIST_ELEMENT_T *element;
    if ( anchor->list_tail != NULL )
    {
        element = anchor->list_tail;
        object = element->object;
        anchor->list_tail = element->prev;
        if ( anchor->list_tail == NULL )
            anchor->list_head = NULL;
        anchor->element_count--;
        element->next = element->prev = NULL;
        element->owning_list = NULL;
    }
    return object;
}

/**
 * Remove an element from anywhere in the list referenced by name.
 */
INLINE
static void sci_fast_list_remove_element(
    SCI_FAST_LIST_ELEMENT_T *element
)
{
    if ( element->next == NULL )
        element->owning_list->list_tail = element->prev;
    else
        element->next->prev = element->prev;

    if ( element->prev == NULL )
        element->owning_list->list_head = element->next;
    else
        element->prev->next = element->next;

    element->owning_list->element_count--;
    element->next = element->prev = NULL;
    element->owning_list = NULL;
}

#endif // _SCI_FAST_LIST_HEADER_
