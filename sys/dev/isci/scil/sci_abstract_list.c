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
 * @brief This file contains the implementation of an abstract list class.
 *        This class will allow for the same item to occur multiple times in
 *        the list.  It will provide an interface that is similar to the
 *        C++ standard template list interface.
 */

//******************************************************************************
//*
//*     I N C L U D E S
//*
//******************************************************************************

#include <dev/isci/scil/sci_abstract_list.h>


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
 * @brief Initialize the abstract list
 *
 * @pre The supplied free pool should be constructed prior to utilization
 *      of this abstract list.  It isn't mandatory for the free pool to be
 *      constructed before invoking this method, but suggested.
 *
 * @param[in] list This parameter specifies the abstract list to be
 *            constructed.
 * @param[in] free_pool This parameter specifies the free pool to be
 *            utilized as the repository of free elements for list usage.
 *
 * @return none
 */
void sci_abstract_list_construct(
   SCI_ABSTRACT_LIST_T         * list,
   SCI_ABSTRACT_ELEMENT_POOL_T * free_pool
)
{
   memset(list, 0, sizeof(SCI_ABSTRACT_LIST_T));
   list->free_pool = free_pool;
}

/**
 * Initialize the abstract list with its free pool
 *
 * @param[in] pool
 *    the free pool from which the elements will be extracted
 * @param[in] list_elements
 *    the array of list elements to be added to the free list
 * @param[in] element_count
 *    the count of the elements to be added to the free list these should be
 *    the same as the array size of list elements
 *
 * @return none
 */
void sci_abstract_element_pool_construct(
   SCI_ABSTRACT_ELEMENT_POOL_T * pool,
   SCI_ABSTRACT_ELEMENT_T      * list_elements,
   int                           element_count
)
{
   int index;

   memset(pool, 0, sizeof(SCI_ABSTRACT_ELEMENT_POOL_T));
   memset(list_elements, 0, sizeof(SCI_ABSTRACT_ELEMENT_T) * element_count);

   pool->elements     = list_elements;
   pool->max_elements = element_count;

   // Loop through all of the elements in the array and push them onto the
   // pool's free list.
   for (index = element_count - 1; index >= 0; index--)
   {
      private_pool_free(pool, &(list_elements[index]));
   }
}


#ifdef USE_ABSTRACT_LIST_FUNCTIONS

//******************************************************************************
//*
//*     P U B L I C   M E T H O D S
//*
//******************************************************************************

/**
 * Simply return the front element pointer of the list.  This returns an element
 * element as opposed to what the element is pointing to.
 */
SCI_ABSTRACT_ELEMENT_T * sci_abstract_list_get_front(
   SCI_ABSTRACT_LIST_T * list_p
)
{
   return (list_p)->elements.front_p;
}

/**
 * This method simply returns the object pointed to by the head (front) of
 * the list.
 */
void * sci_abstract_list_front(
   SCI_ABSTRACT_LIST_T * list_p
)
{
   return
      ( ( (list_p)->elements.front_p ) ? ((list_p)->elements.front_p->object_p) : NULL );
}

/**
 * This method simply returns the object pointed to by the tail (back) of
 * the list.
 */
void * sci_abstract_list_back(
   SCI_ABSTRACT_LIST_T * list_p
)
{
   return
      ( ( (list_p)->elements.back_p ) ? ((list_p)->elements.back_p->object_p) : NULL );
}

/**
 * This method will return FALSE if the list is not empty.
 */
BOOL sci_abstract_list_is_empty(
   SCI_ABSTRACT_LIST_T * list_p
)
{
   return ( (list_p)->elements.front_p == NULL );
}


/**
 * This method will return the number of elements queued in the list.
 */
U32 sci_abstract_list_size(
   SCI_ABSTRACT_LIST_T * list_p
)
{
   return ( (list_p)->elements.size );
}


/**
 * This method simply returns the next list element in the list.
 */
SCI_ABSTRACT_ELEMENT_T * sci_abstract_list_get_next(
   SCI_ABSTRACT_ELEMENT_T * alElement_p
)
{
   return ( (alElement_p)->next_p );
}


#if defined(SCI_LOGGING)
/**
 * This method simply prints the contents of the list.
 */
void  sci_abstract_list_print(
   SCI_ABSTRACT_LIST_T * list_p
)
{
   SCI_ABSTRACT_ELEMENT_T * alElement_p = list_p->elements.front_p;

   while (alElement_p != NULL)
   {
#ifdef UNIT_TEST_DEBUG
      /* Check to see if we found the object for which we are searching. */
      printf("ITEM next_p 0x%x prev_p 0x%x obj_p 0x%x, 0x%x\n",
             alElement_p->next_p,
             alElement_p->previous_p,
             (U32*) (alElement_p->object_p));
#endif
      alElement_p = alElement_p->next_p;
   }
}
#endif // defined(SCI_LOGGING)


/**
 * This method will simply search the supplied list for the desired object.
 * It will return a pointer to the object, if it is found.  Otherwise
 * it will return NULL.
 */
void * sci_abstract_list_find(
   SCI_ABSTRACT_LIST_T * list_p,
   void * obj_p
)
{
   return
      sci_abstract_list_get_object(private_find(&(list_p)->elements, (obj_p)));
}


/**
 * This method will simply remove the element at the back (tail) of the list.
 * It will return a pointer to the object that was removed or NULL if not
 * found.
 */
void * sci_abstract_list_popback(
   SCI_ABSTRACT_LIST_T * list_p
)
{
   SCI_ABSTRACT_ELEMENT_LIST_T * elem_list = &(list_p)->elements;
   SCI_ABSTRACT_ELEMENT_T      * alElement_p = elem_list->back_p;
   void * obj_p = NULL;

   if (alElement_p != NULL)
   {
      obj_p = alElement_p->object_p;
      if (elem_list->back_p == elem_list->front_p)
      {
         elem_list->back_p = elem_list->front_p = NULL;
      }
      else
      {
         elem_list->back_p = elem_list->back_p->previous_p;
         elem_list->back_p->next_p = NULL;
      }

      elem_list->size--;
      private_pool_free((list_p)->free_pool, alElement_p);
   }

   return obj_p;
}

/**
 * This method simply removes the list element at the head of the list
 * and returns the pointer to the object that was removed.
 */
void * sci_abstract_list_popfront(
   SCI_ABSTRACT_LIST_T * list_p
)
{
   SCI_ABSTRACT_ELEMENT_T * alElement_p =
                              private_pop_front(&(list_p)->elements);
   void * obj_p = NULL;

   if (alElement_p != NULL)
   {
      obj_p = alElement_p->object_p;
      private_pool_free((list_p)->free_pool, alElement_p);
   }

   return obj_p;
}



/**
 * This method will erase (remove) all instances of the supplied object from
 * anywhere in the list.
 */
void sci_abstract_list_erase(
   SCI_ABSTRACT_LIST_T * list_p,
   void * obj_p
)
{
   SCI_ABSTRACT_ELEMENT_LIST_T * elem_list = &(list_p)->elements;
   SCI_ABSTRACT_ELEMENT_T      * alElement_p;

   while ((alElement_p = private_find(elem_list, (obj_p))) != NULL)
   {
      if (alElement_p == elem_list->front_p)
      {
         sci_abstract_list_popfront(list_p);
      }
      else if (alElement_p == elem_list->back_p)
      {
         sci_abstract_list_popback(list_p);
      }
      else
      {
         alElement_p->previous_p->next_p = alElement_p->next_p;
         alElement_p->next_p->previous_p = alElement_p->previous_p;
         elem_list->size--;
         private_pool_free((list_p)->free_pool, alElement_p);
      }
   }
   return;
}

/**
 * This method simply adds a LIST_ELEMENT for the supplied object to the back
 * (tail) of the supplied list.
 */
void sci_abstract_list_pushback(
   SCI_ABSTRACT_LIST_T * list_p,
   void * obj_p
)
{
   SCI_ABSTRACT_ELEMENT_LIST_T * elem_list = &(list_p)->elements;
   SCI_ABSTRACT_ELEMENT_T      * alElement_p
      = private_pool_allocate((list_p)->free_pool);
//   assert(alElement_p != NULL);

   alElement_p->object_p = (obj_p);

   if (elem_list->front_p == NULL)
   {
      elem_list->front_p = elem_list->back_p = alElement_p;
   }
   else
   {
      elem_list->back_p->next_p = alElement_p;
      alElement_p->previous_p   = elem_list->back_p;
      elem_list->back_p         = alElement_p;
   }

   elem_list->size++;
}



/**
 * This method simply adds a LIST_ELEMENT for the supplied object to the front
 * (head) of the supplied list.
 */
void sci_abstract_list_pushfront(
   SCI_ABSTRACT_LIST_T * list_p,
   void * obj_p
)
{
   SCI_ABSTRACT_ELEMENT_T * alElement_p =
         private_pool_allocate((list_p)->free_pool);
   alElement_p->object_p = (obj_p);
   private_push_front(&(list_p)->elements, alElement_p);
}


/**
 * This method will add the objToAdd_p object to the list before the obj_p.
 *
 */
void sci_abstract_list_insert(
   SCI_ABSTRACT_LIST_T * list_p,
   void * obj_p,
   void * objToAdd_p
)
{
   SCI_ABSTRACT_ELEMENT_LIST_T * elem_list = &(list_p)->elements;

   SCI_ABSTRACT_ELEMENT_T * obj_element = private_find(elem_list, obj_p);

   SCI_ABSTRACT_ELEMENT_T * objToAdd_element =
      private_pool_allocate((list_p)->free_pool);

   objToAdd_element->object_p = objToAdd_p;

   ASSERT(obj_element != NULL);
   ASSERT(objToAdd_element != NULL);

   if (obj_element == elem_list->front_p)
   {
      objToAdd_element->object_p = (objToAdd_p);
      private_push_front(&(list_p)->elements, objToAdd_element);
   }
   else
   {
      obj_element->previous_p->next_p = objToAdd_element;
      objToAdd_element->previous_p = obj_element->previous_p;

      obj_element->previous_p = objToAdd_element;
      objToAdd_element->next_p = obj_element;

      elem_list->size++;
   }
}

/**
 * This method simply frees all the items from the list.
 */
void sci_abstract_list_clear(
   SCI_ABSTRACT_LIST_T * list_p
)
{
   while ((list_p)->elements.size > 0)
      sci_abstract_list_popfront((list_p));
}

/**
 * This method simply returns the object being pointed to by the list element
 * (The item being listed).
 */
void * sci_abstract_list_get_object(
   SCI_ABSTRACT_ELEMENT_T * alElement_p
)
{
   void * obj_p = NULL;
   if ((alElement_p) != NULL)
      obj_p = (alElement_p)->object_p;

   return obj_p;
}


/**
 * This method is simply a wrapper to provide the number of elements in
 * the free list.
 */
U32 sci_abstract_list_freeList_size(
   SCI_ABSTRACT_LIST_T * freeList
)
{
   return (sci_abstract_list_size(freeList));
}

//******************************************************************************
//*
//*     P R I V A T E   M E T H O D S
//*
//******************************************************************************

/**
 * This method simply performs the common portion of pushing a list element
 * onto a list.
 *
 * WARNING: This is a private helper method that should not be called directly
 *          by any users.
 */
void private_push_front(
   SCI_ABSTRACT_ELEMENT_LIST_T * privateList_p,
   SCI_ABSTRACT_ELEMENT_T * alElement_p
)
{
   if ((privateList_p)->front_p == NULL)
   {
      (privateList_p)->front_p = (privateList_p)->back_p = (alElement_p);
      (alElement_p)->next_p = (alElement_p)->previous_p = NULL;
   }
   else
   {
      (alElement_p)->next_p                = (privateList_p)->front_p;
      (alElement_p)->previous_p            = NULL;
      (privateList_p)->front_p->previous_p = (alElement_p);
      (privateList_p)->front_p             = (alElement_p);
   }

   (privateList_p)->size++;
}

/**
 * This method simply performs the common portion of popping a list element
 * from a list.
 *
 * WARNING: This is a private helper method that should not be called directly
 *          by any users.
 */
SCI_ABSTRACT_ELEMENT_T * private_pop_front(
   SCI_ABSTRACT_ELEMENT_LIST_T * privateList_p
)
{
   SCI_ABSTRACT_ELEMENT_T * alElement_p = (privateList_p)->front_p;

   if (alElement_p != NULL)
   {
      if ((privateList_p)->front_p == (privateList_p)->back_p)
      {
         (privateList_p)->front_p = (privateList_p)->back_p = NULL;
      }
      else
      {
         (privateList_p)->front_p = (privateList_p)->front_p->next_p;
         (privateList_p)->front_p->previous_p = NULL;
      }

      (privateList_p)->size--;
   }

   return alElement_p;
}

/**
 * This method will simply search the supplied list for the desired object.
 * It will return a pointer to the abstract_list_element if found, otherwise
 * it will return NULL.
 */
SCI_ABSTRACT_ELEMENT_T * private_find(
   SCI_ABSTRACT_ELEMENT_LIST_T * list_p,
   void * obj_p
)
{
   SCI_ABSTRACT_ELEMENT_T * alElement_p = (list_p)->front_p;

   while (alElement_p != NULL)
   {
      /* Check to see if we found the object for which we are searching. */
      if (alElement_p->object_p == (void*) (obj_p))
      {
         break;
      }

      alElement_p = alElement_p->next_p;
   }

   return alElement_p;
}

/**
 * This private method will free the supplied list element back to the pool
 * of free list elements.
 */
void private_pool_free(
   SCI_ABSTRACT_ELEMENT_POOL_T * free_pool,
   SCI_ABSTRACT_ELEMENT_T * alElement_p
)
{
   /* Push the list element back to the head to get better locality of */
   /* reference with the cache.                                        */
   private_push_front(&(free_pool)->free_list, (alElement_p));
}

/**
 * This private method will allocate a list element from the pool of free
 * list elements.
 */
SCI_ABSTRACT_ELEMENT_T * private_pool_allocate(
   SCI_ABSTRACT_ELEMENT_POOL_T * free_pool
)
{
   SCI_ABSTRACT_ELEMENT_T * alElement_p;

   alElement_p = private_pop_front(&(free_pool)->free_list);

   alElement_p->next_p     = NULL;
   alElement_p->previous_p = NULL;
   alElement_p->object_p   = NULL;

   return alElement_p;
}

#endif // USE_ABSTRACT_LIST_FUNCTIONS
