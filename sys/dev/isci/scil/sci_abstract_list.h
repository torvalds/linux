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
 * @brief This file contains the interface to the abstract list class.
 *        This class will allow for the same item to occur multiple times in
 *        a list or multiple lists.  It will provide an interface that is
 *        similar to the C++ standard template list interface.
 *        Methods Provided:
 *        - sci_abstract_list_front()
 *        - sci_abstract_list_back()
 *        - sci_abstract_list_is_empty()
 *        - sci_abstract_list_size()
 *        - sci_abstract_list_print()
 *        - sci_abstract_list_find()
 *        - sci_abstract_list_popback()
 *        - sci_abstract_list_popfront()
 *        - sci_abstract_list_erase()
 *        - sci_abstract_list_pushback()
 *        - sci_abstract_list_pushfront()
 *        - sci_abstract_list_get_object()
 *        - sci_abstract_list_get_next()
 *        - sci_abstract_list_insert() UNIMPLEMENTED
 *        - sci_abstract_list_clear()
 */

#ifndef _SCI_ABSTRACT_LIST_H_
#define _SCI_ABSTRACT_LIST_H_

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

/**
 * @struct SCI_ABSTRACT_ELEMENT
 *
 * @brief This object represents an element of a abstract list.
 *        NOTE: This structure does not evenly align on a cache line
 *              boundary.  If SSP specific code ends up using this list,
 *              then it may be a good idea to force the alignment.  Now
 *              it is more important to save the space.
 */
typedef struct SCI_ABSTRACT_ELEMENT
{
   /**
    * This field points to the next item in the abstract_list.
    */
   struct SCI_ABSTRACT_ELEMENT * next_p;

   /**
    * This field points to the previous item in the abstract_list.
    */
   struct SCI_ABSTRACT_ELEMENT * previous_p;

   /**
    * This field points to the object the list is managing (i.e. the thing
    * being listed).
    */
   void * object_p;

} SCI_ABSTRACT_ELEMENT_T;

/**
 * @struct SCI_ABSTRACT_ELEMENT_LIST
 *
 * @brief This object represents an element list object.  It can have
 *        elements added and removed from it.
 */
typedef struct SCI_ABSTRACT_ELEMENT_LIST
{
   /**
    * Pointer to the front (head) of the list.
    */
   SCI_ABSTRACT_ELEMENT_T * front_p;

   /**
    * Pointer to the back (tail) of the list.
    */
   SCI_ABSTRACT_ELEMENT_T * back_p;

   /**
    * This field depicts the number of elements in this list.
    * NOTE: It is possible to remove this field and replace it with a
    *       linear walking of the list to determine the size, but since
    *       there aren't many lists in the system we don't utilize much
    *       space.
    */
   U32 size;

} SCI_ABSTRACT_ELEMENT_LIST_T;

/**
 * @struct SCI_ABSTRACT_ELEMENT_POOL
 *
 * @brief This structure provides the pool of free abstract elements to be
 *        utilized by an SCI_ABSTRACT_LIST.
 */
typedef struct SCI_ABSTRACT_ELEMENT_POOL
{
   /**
    * Pointer to an array of elements to be managed by this pool.  This
    * array acts as the memory store for the elements in the free pool or
    * allocated out of the pool into an SCI_ABSTRACT_LIST.
    */
   SCI_ABSTRACT_ELEMENT_T * elements;

   /**
    * This field contains the maximum number of free elements for the pool.
    * It is set at creation of the pool and should not be changed afterward.
    */
   U32 max_elements;

   /**
    * Pointer to the list of free elements that can be allocated from
    * the pool.
    */
   struct SCI_ABSTRACT_ELEMENT_LIST  free_list;

} SCI_ABSTRACT_ELEMENT_POOL_T;

/**
 * @struct SCI_ABSTRACT_LIST
 *
 * @brief This object provides the ability to queue any type of object or
 *        even the same object multiple times.  The object must be provided
 *        an element pool from which to draw free elements.
 */
typedef struct SCI_ABSTRACT_LIST
{
   /**
    * This represents the elements currently managed by the list.
    */
   SCI_ABSTRACT_ELEMENT_LIST_T  elements;

   /**
    * This field contains elements that are currently available for
    * allocation into the list of elements;
    */
   SCI_ABSTRACT_ELEMENT_POOL_T * free_pool;

} SCI_ABSTRACT_LIST_T;

//******************************************************************************
//*
//*     P R O T E C T E D   M E T H O D S
//*
//******************************************************************************

void sci_abstract_element_pool_construct(
   SCI_ABSTRACT_ELEMENT_POOL_T * pool,
   SCI_ABSTRACT_ELEMENT_T      * list_elements,
   int                           element_count
);

void sci_abstract_list_construct(
   SCI_ABSTRACT_LIST_T         * list,
   SCI_ABSTRACT_ELEMENT_POOL_T * free_pool
);



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
);


/**
 * This method simply returns the object pointed to by the head (front) of
 * the list.
 */
void * sci_abstract_list_front(
   SCI_ABSTRACT_LIST_T * list_p
);


/**
 * This method simply returns the object pointed to by the tail (back) of
 * the list.
 */
void * sci_abstract_list_back(
   SCI_ABSTRACT_LIST_T * list_p
);


/**
 * This method will return FALSE if the list is not empty.
 */
BOOL sci_abstract_list_is_empty(
   SCI_ABSTRACT_LIST_T * list_p
);


/**
 * This method will return the number of elements queued in the list.
 */
U32 sci_abstract_list_size(
   SCI_ABSTRACT_LIST_T * list_p
);


/**
 * This method simply returns the next list element in the list.
 */
SCI_ABSTRACT_ELEMENT_T * sci_abstract_list_get_next(
   SCI_ABSTRACT_ELEMENT_T * alElement_p
);


/**
 * This method simply prints the contents of the list.
 */
void  sci_abstract_list_print(
   SCI_ABSTRACT_LIST_T * list_p
);


/**
 * This method will simply search the supplied list for the desired object.
 * It will return a pointer to the object, if it is found.  Otherwise
 * it will return NULL.
 */
void * sci_abstract_list_find(
   SCI_ABSTRACT_LIST_T * list_p,
   void * obj_p
);


/**
 * This method will simply remove the element at the back (tail) of the list.
 * It will return a pointer to the object that was removed or NULL if not
 * found.
 */
void * sci_abstract_list_popback(
   SCI_ABSTRACT_LIST_T * list_p
);


/**
 * This method simply removes the list element at the head of the list
 * and returns the pointer to the object that was removed.
 */
void * sci_abstract_list_popfront(
   SCI_ABSTRACT_LIST_T * list_p
);



/**
 * This method will erase (remove) all instances of the supplied object from
 * anywhere in the list.
 */
void sci_abstract_list_erase(
   SCI_ABSTRACT_LIST_T * list_p,
   void * obj_p
);


/**
 * This method simply adds a LIST_ELEMENT for the supplied object to the back
 * (tail) of the supplied list.
 */
void sci_abstract_list_pushback(
   SCI_ABSTRACT_LIST_T * list_p,
   void * obj_p
);



/**
 * This method simply adds a LIST_ELEMENT for the supplied object to the front
 * (head) of the supplied list.
 */
void sci_abstract_list_pushfront(
   SCI_ABSTRACT_LIST_T * list_p,
   void * obj_p
);


/**
 * This method will add the objToAdd_p object to the list before the obj_p.
 * NOTE: UNIMPLEMENTED
 */
void sci_abstract_list_insert(
   SCI_ABSTRACT_LIST_T * list_p,
   void * obj_p,
   void * objToAdd_p
);


/**
 * This method simply frees all the items from the list.
 */
void sci_abstract_list_clear(
   SCI_ABSTRACT_LIST_T * list_p
);


/**
 * This method simply returns the object being pointed to by the list element
 * (The item being listed).
 */
void * sci_abstract_list_get_object(
   SCI_ABSTRACT_ELEMENT_T * alElement_p
);


/**
 * This method is simply a wrapper to provide the number of elements in
 * the free list.
 */
U32 sci_abstract_list_freeList_size(
   SCI_ABSTRACT_LIST_T * freeList
);


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
);


/**
 * This method simply performs the common portion of popping a list element
 * from a list.
 *
 * WARNING: This is a private helper method that should not be called directly
 *          by any users.
 */
SCI_ABSTRACT_ELEMENT_T * private_pop_front(
   SCI_ABSTRACT_ELEMENT_LIST_T * privateList_p
);


/**
 * This method will simply search the supplied list for the desired object.
 * It will return a pointer to the abstract_list_element if found, otherwise
 * it will return NULL.
 */
SCI_ABSTRACT_ELEMENT_T * private_find(
   SCI_ABSTRACT_ELEMENT_LIST_T * list_p,
   void * obj_p
);


/**
 * This private method will free the supplied list element back to the pool
 * of free list elements.
 */
void private_pool_free(
   SCI_ABSTRACT_ELEMENT_POOL_T * free_pool,
   SCI_ABSTRACT_ELEMENT_T * alElement_p
);


/**
 * This private method will allocate a list element from the pool of free
 * list elements.
 */
SCI_ABSTRACT_ELEMENT_T * private_pool_allocate(
   SCI_ABSTRACT_ELEMENT_POOL_T * free_pool
);


#else

//******************************************************************************
//*
//*     P U B L I C   M E T H O D S
//*
//******************************************************************************

/**
 * Simply return the front element pointer of the list.  This returns an element
 * element as opposed to what the element is pointing to.
 */
#define sci_abstract_list_get_front(                                           \
   list_p                                                                      \
)                                                                              \
((list_p)->elements.front_p)

/**
 * This method simply returns the object pointed to by the head (front) of
 * the list.
 */
#define sci_abstract_list_front(                                               \
   list_p                                                                      \
)                                                                              \
( ( (list_p)->elements.front_p ) ? ((list_p)->elements.front_p->object_p) : NULL )

/**
 * This method simply returns the object pointed to by the tail (back) of
 * the list.
 */
#define sci_abstract_list_back(                                                \
   list_p                                                                      \
)                                                                              \
( ( (list_p)->elements.back_p ) ? ((list_p)->elements.back_p->object_p) : NULL )

/**
 * This method will return FALSE if the list is not empty.
 */
#define sci_abstract_list_is_empty(                                            \
   list_p                                                                      \
)                                                                              \
( (list_p)->elements.front_p == NULL )

/**
 * This method will return the number of elements queued in the list.
 */
#define sci_abstract_list_size(                                                \
   list_p                                                                      \
)                                                                              \
( (list_p)->elements.size )

/**
 * This method simply returns the next list element in the list.
 */
#define sci_abstract_list_get_next(                                            \
   alElement_p                                                                 \
)                                                                              \
( (alElement_p)->next_p )

/**
 * This method simply prints the contents of the list.
 */
#define sci_abstract_list_print(                                               \
   list_p                                                                      \
)                                                                              \
{                                                                              \
   SCI_ABSTRACT_ELEMENT_T * alElement_p = list_p->elements.front_p;            \
                                                                               \
   while (alElement_p != NULL)                                                 \
   {                                                                           \
      /* Check to see if we found the object for which we are searching. */    \
      printf("ITEM next_p 0x%x prev_p 0x%x obj_p 0x%x, 0x%x\n",                \
             alElement_p->next_p,                                              \
             alElement_p->previous_p,                                          \
             (U32*) (alElement_p->object_p));                                  \
                                                                               \
      alElement_p = alElement_p->next_p;                                       \
   }                                                                           \
}

/**
 * This method will simply search the supplied list for the desired object.
 * It will return a pointer to the object, if it is found.  Otherwise
 * it will return NULL.
 */
#define sci_abstract_list_find(                                                \
   list_p,                                                                     \
   obj_p                                                                       \
)                                                                              \
({                                                                             \
   sci_abstract_list_get_object(private_find(&(list_p)->elements, (obj_p)));   \
})

/**
 * This method will simply remove the element at the back (tail) of the list.
 * It will return a pointer to the object that was removed or NULL if not
 * found.
 */
#define sci_abstract_list_popback(                                             \
   list_p                                                                      \
)                                                                              \
({                                                                             \
   SCI_ABSTRACT_ELEMENT_LIST_T * elem_list = &(list_p)->elements;              \
   SCI_ABSTRACT_ELEMENT_T      * alElement_p = elem_list->back_p;              \
   void * obj_p = NULL;                                                        \
                                                                               \
   if (alElement_p != NULL)                                                    \
   {                                                                           \
      obj_p = alElement_p->object_p;                                           \
      if (elem_list->back_p == elem_list->front_p)                             \
      {                                                                        \
         elem_list->back_p = elem_list->front_p = NULL;                        \
      }                                                                        \
      else                                                                     \
      {                                                                        \
         elem_list->back_p = elem_list->back_p->previous_p;                    \
         elem_list->back_p->next_p = NULL;                                     \
      }                                                                        \
                                                                               \
      elem_list->size--;                                                       \
      private_pool_free((list_p)->free_pool, alElement_p);                     \
   }                                                                           \
                                                                               \
   obj_p;                                                                      \
})

/**
 * This method simply removes the list element at the head of the list
 * and returns the pointer to the object that was removed.
 */
#define sci_abstract_list_popfront(                                            \
   list_p                                                                      \
)                                                                              \
({                                                                             \
   SCI_ABSTRACT_ELEMENT_T * alElement_p =                                      \
                              private_pop_front(&(list_p)->elements);          \
   void * obj_p = NULL;                                                        \
                                                                               \
   if (alElement_p != NULL)                                                    \
   {                                                                           \
      obj_p = alElement_p->object_p;                                           \
      private_pool_free((list_p)->free_pool, alElement_p);                     \
   }                                                                           \
                                                                               \
   obj_p;                                                                      \
})

/**
 * This method will erase (remove) all instances of the supplied object from
 * anywhere in the list.
 */
#define sci_abstract_list_erase(                                               \
   list_p,                                                                     \
   obj_p                                                                       \
)                                                                              \
{                                                                              \
   SCI_ABSTRACT_ELEMENT_LIST_T * elem_list = &(list_p)->elements;              \
   SCI_ABSTRACT_ELEMENT_T      * alElement_p;                                  \
                                                                               \
   while ((alElement_p = private_find(elem_list, (obj_p))) != NULL)            \
   {                                                                           \
      if (alElement_p == elem_list->front_p)                                   \
      {                                                                        \
         sci_abstract_list_popfront(list_p);                                   \
      }                                                                        \
      else if (alElement_p == elem_list->back_p)                               \
      {                                                                        \
         sci_abstract_list_popback(list_p);                                    \
      }                                                                        \
      else                                                                     \
      {                                                                        \
         alElement_p->previous_p->next_p = alElement_p->next_p;                \
         alElement_p->next_p->previous_p = alElement_p->previous_p;            \
         elem_list->size--;                                                    \
         private_pool_free((list_p)->free_pool, alElement_p);                  \
      }                                                                        \
   }                                                                           \
}

/**
 * This method simply adds a LIST_ELEMENT for the supplied object to the back
 * (tail) of the supplied list.
 */
#define sci_abstract_list_pushback(                                            \
   list_p,                                                                     \
   obj_p                                                                       \
)                                                                              \
{                                                                              \
   SCI_ABSTRACT_ELEMENT_LIST_T * elem_list = &(list_p)->elements;              \
   SCI_ABSTRACT_ELEMENT_T      * alElement_p                                   \
      = private_pool_allocate((list_p)->free_pool);                            \
   assert(alElement_p != NULL);                                                \
                                                                               \
   alElement_p->object_p = (obj_p);                                            \
                                                                               \
   if (elem_list->front_p == NULL)                                             \
   {                                                                           \
      elem_list->front_p = elem_list->back_p = alElement_p;                    \
   }                                                                           \
   else                                                                        \
   {                                                                           \
      elem_list->back_p->next_p = alElement_p;                                 \
      alElement_p->previous_p   = elem_list->back_p;                           \
      elem_list->back_p         = alElement_p;                                 \
   }                                                                           \
                                                                               \
   elem_list->size++;                                                          \
}

/**
 * This method simply adds a LIST_ELEMENT for the supplied object to the front
 * (head) of the supplied list.
 */
#define sci_abstract_list_pushfront(                                           \
   list_p,                                                                     \
   obj_p                                                                       \
)                                                                              \
{                                                                              \
   SCI_ABSTRACT_ELEMENT_T * alElement_p =                                      \
         private_pool_allocate((list_p)->free_pool);                           \
   alElement_p->object_p = (obj_p);                                            \
   private_push_front(&(list_p)->elements, alElement_p);                       \
}

/**
 * This method will add the objToAdd_p object to the list before the obj_p.
 * NOTE: UNIMPLEMENTED
 */
#define sci_abstract_list_insert(                                              \
   list_p,                                                                     \
   obj_p,                                                                      \
   objToAdd_p                                                                  \
)                                                                              \
{                                                                              \
   SCI_ABSTRACT_ELEMENT_LIST_T * elem_list = &(list_p)->elements;              \
                                                                               \
   SCI_ABSTRACT_ELEMENT_T * obj_element = private_find(elem_list, obj_p);      \
                                                                               \
   SCI_ABSTRACT_ELEMENT_T * objToAdd_element =                                 \
      private_pool_allocate((list_p)->free_pool);                              \
                                                                               \
   objToAdd_element->object_p = objToAdd_p;                                    \
                                                                               \
   ASSERT(obj_element != NULL);                                                \
   ASSERT(objToAdd_element != NULL);                                           \
                                                                               \
   if (obj_element == elem_list->front_p)                                      \
   {                                                                           \
      objToAdd_element->object_p = (objToAdd_p);                               \
      private_push_front(&(list_p)->elements, objToAdd_element);               \
   }                                                                           \
   else                                                                        \
   {                                                                           \
      obj_element->previous_p->next_p = objToAdd_element;                      \
      objToAdd_element->previous_p = obj_element->previous_p;                  \
                                                                               \
      obj_element->previous_p = objToAdd_element;                              \
      objToAdd_element->next_p = obj_element;                                  \
                                                                               \
      elem_list->size++;                                                       \
   }                                                                           \
}

/**
 * This method simply frees all the items from the list.
 */
#define sci_abstract_list_clear(                                               \
   list_p                                                                      \
)                                                                              \
{                                                                              \
   while ((list_p)->elements.size > 0)                                         \
      sci_abstract_list_popfront((list_p));                                    \
}

/**
 * This method simply returns the object being pointed to by the list element
 * (The item being listed).
 */
#define sci_abstract_list_get_object(                                          \
   alElement_p                                                                 \
)                                                                              \
({                                                                             \
   void * obj_p = NULL;                                                        \
   if ((alElement_p) != NULL)                                                  \
      obj_p = (alElement_p)->object_p;                                         \
                                                                               \
   obj_p;                                                                      \
})

/**
 * This method is simply a wrapper to provide the number of elements in
 * the free list.
 */
#define sci_abstract_list_freeList_size(freeList) (sci_abstract_list_size(freeList))

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
#define private_push_front(                                                    \
   privateList_p,                                                              \
   alElement_p                                                                 \
)                                                                              \
{                                                                              \
   if ((privateList_p)->front_p == NULL)                                       \
   {                                                                           \
      (privateList_p)->front_p = (privateList_p)->back_p = (alElement_p);      \
      (alElement_p)->next_p = (alElement_p)->previous_p = NULL;                \
   }                                                                           \
   else                                                                        \
   {                                                                           \
      (alElement_p)->next_p                = (privateList_p)->front_p;         \
      (alElement_p)->previous_p            = NULL;                             \
      (privateList_p)->front_p->previous_p = (alElement_p);                    \
      (privateList_p)->front_p             = (alElement_p);                    \
   }                                                                           \
                                                                               \
   (privateList_p)->size++;                                                    \
}

/**
 * This method simply performs the common portion of popping a list element
 * from a list.
 *
 * WARNING: This is a private helper method that should not be called directly
 *          by any users.
 */
#define private_pop_front(                                                     \
   privateList_p                                                               \
)                                                                              \
({                                                                             \
   SCI_ABSTRACT_ELEMENT_T * alElement_p = (privateList_p)->front_p;            \
                                                                               \
   if (alElement_p != NULL)                                                    \
   {                                                                           \
      if ((privateList_p)->front_p == (privateList_p)->back_p)                 \
      {                                                                        \
         (privateList_p)->front_p = (privateList_p)->back_p = NULL;            \
      }                                                                        \
      else                                                                     \
      {                                                                        \
         (privateList_p)->front_p = (privateList_p)->front_p->next_p;          \
         (privateList_p)->front_p->previous_p = NULL;                          \
      }                                                                        \
                                                                               \
      (privateList_p)->size--;                                                 \
   }                                                                           \
                                                                               \
   alElement_p;                                                                \
})

/**
 * This method will simply search the supplied list for the desired object.
 * It will return a pointer to the abstract_list_element if found, otherwise
 * it will return NULL.
 */
#define private_find(                                                          \
   list_p,                                                                     \
   obj_p                                                                       \
)                                                                              \
({                                                                             \
   SCI_ABSTRACT_ELEMENT_T * alElement_p = (list_p)->front_p;                   \
                                                                               \
   while (alElement_p != NULL)                                                 \
   {                                                                           \
      /* Check to see if we found the object for which we are searching. */    \
      if (alElement_p->object_p == (void*) (obj_p))                            \
      {                                                                        \
         break;                                                                \
      }                                                                        \
                                                                               \
      alElement_p = alElement_p->next_p;                                       \
   }                                                                           \
                                                                               \
   alElement_p;                                                                \
})

/**
 * This private method will free the supplied list element back to the pool
 * of free list elements.
 */
#define private_pool_free(                                                     \
   free_pool,                                                                  \
   alElement_p                                                                 \
)                                                                              \
{                                                                              \
   /* Push the list element back to the head to get better locality of */      \
   /* reference with the cache.                                        */      \
   private_push_front(&(free_pool)->free_list, (alElement_p));                 \
}

/**
 * This private method will allocate a list element from the pool of free
 * list elements.
 */
#define private_pool_allocate(free_pool)                                       \
({                                                                             \
   SCI_ABSTRACT_ELEMENT_T * alElement_p;                                       \
                                                                               \
   alElement_p = private_pop_front(&(free_pool)->free_list);                   \
                                                                               \
   memset(alElement_p, 0, sizeof(SCI_ABSTRACT_ELEMENT_T));                     \
   alElement_p;                                                                \
})

#endif
#endif // _ABSTRACT_LIST_H_
