/* Copyright (c) 2008-2012 Freescale Semiconductor, Inc
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/**************************************************************************//**

 @File          list_ext.h

 @Description   External prototypes for list.c
*//***************************************************************************/

#ifndef __LIST_EXT_H
#define __LIST_EXT_H


#include "std_ext.h"


/**************************************************************************//**
 @Group         etc_id   Utility Library Application Programming Interface

 @Description   External routines.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         list_id List

 @Description   List module functions,definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   List structure.
*//***************************************************************************/
typedef struct List
{
    struct List *p_Next;  /**< A pointer to the next list object     */
    struct List *p_Prev;  /**< A pointer to the previous list object */
} t_List;


/**************************************************************************//**
 @Function      NCSW_LIST_FIRST/NCSW_LIST_LAST/NCSW_LIST_NEXT/NCSW_LIST_PREV

 @Description   Macro to get first/last/next/previous entry in a list.

 @Param[in]     p_List - A pointer to a list.
*//***************************************************************************/
#define NCSW_LIST_FIRST(p_List) (p_List)->p_Next
#define NCSW_LIST_LAST(p_List)  (p_List)->p_Prev
#define NCSW_LIST_NEXT          NCSW_LIST_FIRST
#define NCSW_LIST_PREV          NCSW_LIST_LAST


/**************************************************************************//**
 @Function      NCSW_LIST_INIT

 @Description   Macro for initialization of a list struct.

 @Param[in]     lst - The t_List object to initialize.
*//***************************************************************************/
#define NCSW_LIST_INIT(lst) {&(lst), &(lst)}


/**************************************************************************//**
 @Function      NCSW_LIST

 @Description   Macro to declare of a list.

 @Param[in]     listName - The list object name.
*//***************************************************************************/
#define NCSW_LIST(listName) t_List listName = NCSW_LIST_INIT(listName)


/**************************************************************************//**
 @Function      INIT_LIST

 @Description   Macro to initialize a list pointer.

 @Param[in]     p_List - The list pointer.
*//***************************************************************************/
#define INIT_LIST(p_List)   NCSW_LIST_FIRST(p_List) = NCSW_LIST_LAST(p_List) = (p_List)


/**************************************************************************//**
 @Function      NCSW_LIST_OBJECT

 @Description   Macro to get the struct (object) for this entry.

 @Param[in]     type   - The type of the struct (object) this list is embedded in.
 @Param[in]     member - The name of the t_List object within the struct.

 @Return        The structure pointer for this entry.
*//***************************************************************************/
#define MEMBER_OFFSET(type, member) (PTR_TO_UINT(&((type *)0)->member))
#define NCSW_LIST_OBJECT(p_List, type, member) \
    ((type *)((char *)(p_List)-MEMBER_OFFSET(type, member)))


/**************************************************************************//**
 @Function      NCSW_LIST_FOR_EACH

 @Description   Macro to iterate over a list.

 @Param[in]     p_Pos  - A pointer to a list to use as a loop counter.
 @Param[in]     p_Head - A pointer to the head for your list pointer.

 @Cautions      You can't delete items with this routine.
                For deletion use NCSW_LIST_FOR_EACH_SAFE().
*//***************************************************************************/
#define NCSW_LIST_FOR_EACH(p_Pos, p_Head) \
    for (p_Pos = NCSW_LIST_FIRST(p_Head); p_Pos != (p_Head); p_Pos = NCSW_LIST_NEXT(p_Pos))


/**************************************************************************//**
 @Function      NCSW_LIST_FOR_EACH_SAFE

 @Description   Macro to iterate over a list safe against removal of list entry.

 @Param[in]     p_Pos  - A pointer to a list to use as a loop counter.
 @Param[in]     p_Tmp  - Another pointer to a list to use as temporary storage.
 @Param[in]     p_Head - A pointer to the head for your list pointer.
*//***************************************************************************/
#define NCSW_LIST_FOR_EACH_SAFE(p_Pos, p_Tmp, p_Head)                \
    for (p_Pos = NCSW_LIST_FIRST(p_Head), p_Tmp = NCSW_LIST_FIRST(p_Pos); \
         p_Pos != (p_Head);                                     \
         p_Pos = p_Tmp, p_Tmp = NCSW_LIST_NEXT(p_Pos))


/**************************************************************************//**
 @Function      NCSW_LIST_FOR_EACH_OBJECT_SAFE

 @Description   Macro to iterate over list of given type safely.

 @Param[in]     p_Pos  - A pointer to a list to use as a loop counter.
 @Param[in]     p_Tmp  - Another pointer to a list to use as temporary storage.
 @Param[in]     type   - The type of the struct this is embedded in.
 @Param[in]     p_Head - A pointer to the head for your list pointer.
 @Param[in]     member - The name of the list_struct within the struct.

 @Cautions      You can't delete items with this routine.
                For deletion use NCSW_LIST_FOR_EACH_SAFE().
*//***************************************************************************/
#define NCSW_LIST_FOR_EACH_OBJECT_SAFE(p_Pos, p_Tmp, p_Head, type, member)      \
    for (p_Pos = NCSW_LIST_OBJECT(NCSW_LIST_FIRST(p_Head), type, member),            \
         p_Tmp = NCSW_LIST_OBJECT(NCSW_LIST_FIRST(&p_Pos->member), type, member);    \
         &p_Pos->member != (p_Head);                                       \
         p_Pos = p_Tmp,                                                    \
         p_Tmp = NCSW_LIST_OBJECT(NCSW_LIST_FIRST(&p_Pos->member), type, member))

/**************************************************************************//**
 @Function      NCSW_LIST_FOR_EACH_OBJECT

 @Description   Macro to iterate over list of given type.

 @Param[in]     p_Pos  - A pointer to a list to use as a loop counter.
 @Param[in]     type   - The type of the struct this is embedded in.
 @Param[in]     p_Head - A pointer to the head for your list pointer.
 @Param[in]     member - The name of the list_struct within the struct.

 @Cautions      You can't delete items with this routine.
                For deletion use NCSW_LIST_FOR_EACH_SAFE().
*//***************************************************************************/
#define NCSW_LIST_FOR_EACH_OBJECT(p_Pos, type, p_Head, member)                  \
    for (p_Pos = NCSW_LIST_OBJECT(NCSW_LIST_FIRST(p_Head), type, member);            \
         &p_Pos->member != (p_Head);                                       \
         p_Pos = NCSW_LIST_OBJECT(NCSW_LIST_FIRST(&(p_Pos->member)), type, member))


/**************************************************************************//**
 @Function      NCSW_LIST_Add

 @Description   Add a new entry to a list.

                Insert a new entry after the specified head.
                This is good for implementing stacks.

 @Param[in]     p_New  - A pointer to a new list entry to be added.
 @Param[in]     p_Head - A pointer to a list head to add it after.

 @Return        none.
*//***************************************************************************/
static __inline__ void NCSW_LIST_Add(t_List *p_New, t_List *p_Head)
{
    NCSW_LIST_PREV(NCSW_LIST_NEXT(p_Head)) = p_New;
    NCSW_LIST_NEXT(p_New)             = NCSW_LIST_NEXT(p_Head);
    NCSW_LIST_PREV(p_New)             = p_Head;
    NCSW_LIST_NEXT(p_Head)            = p_New;
}


/**************************************************************************//**
 @Function      NCSW_LIST_AddToTail

 @Description   Add a new entry to a list.

                Insert a new entry before the specified head.
                This is useful for implementing queues.

 @Param[in]     p_New  - A pointer to a new list entry to be added.
 @Param[in]     p_Head - A pointer to a list head to add it before.

 @Return        none.
*//***************************************************************************/
static __inline__ void NCSW_LIST_AddToTail(t_List *p_New, t_List *p_Head)
{
    NCSW_LIST_NEXT(NCSW_LIST_PREV(p_Head)) = p_New;
    NCSW_LIST_PREV(p_New)             = NCSW_LIST_PREV(p_Head);
    NCSW_LIST_NEXT(p_New)             = p_Head;
    NCSW_LIST_PREV(p_Head)            = p_New;
}


/**************************************************************************//**
 @Function      NCSW_LIST_Del

 @Description   Deletes entry from a list.

 @Param[in]     p_Entry - A pointer to the element to delete from the list.

 @Return        none.

 @Cautions      NCSW_LIST_IsEmpty() on entry does not return true after this,
                the entry is in an undefined state.
*//***************************************************************************/
static __inline__ void NCSW_LIST_Del(t_List *p_Entry)
{
    NCSW_LIST_PREV(NCSW_LIST_NEXT(p_Entry)) = NCSW_LIST_PREV(p_Entry);
    NCSW_LIST_NEXT(NCSW_LIST_PREV(p_Entry)) = NCSW_LIST_NEXT(p_Entry);
}


/**************************************************************************//**
 @Function      NCSW_LIST_DelAndInit

 @Description   Deletes entry from list and reinitialize it.

 @Param[in]     p_Entry - A pointer to the element to delete from the list.

 @Return        none.
*//***************************************************************************/
static __inline__ void NCSW_LIST_DelAndInit(t_List *p_Entry)
{
    NCSW_LIST_Del(p_Entry);
    INIT_LIST(p_Entry);
}


/**************************************************************************//**
 @Function      NCSW_LIST_Move

 @Description   Delete from one list and add as another's head.

 @Param[in]     p_Entry - A pointer to the list entry to move.
 @Param[in]     p_Head  - A pointer to the list head that will precede our entry.

 @Return        none.
*//***************************************************************************/
static __inline__ void NCSW_LIST_Move(t_List *p_Entry, t_List *p_Head)
{
    NCSW_LIST_Del(p_Entry);
    NCSW_LIST_Add(p_Entry, p_Head);
}


/**************************************************************************//**
 @Function      NCSW_LIST_MoveToTail

 @Description   Delete from one list and add as another's tail.

 @Param[in]     p_Entry - A pointer to the entry to move.
 @Param[in]     p_Head  - A pointer to the list head that will follow our entry.

 @Return        none.
*//***************************************************************************/
static __inline__ void NCSW_LIST_MoveToTail(t_List *p_Entry, t_List *p_Head)
{
    NCSW_LIST_Del(p_Entry);
    NCSW_LIST_AddToTail(p_Entry, p_Head);
}


/**************************************************************************//**
 @Function      NCSW_LIST_IsEmpty

 @Description   Tests whether a list is empty.

 @Param[in]     p_List - A pointer to the list to test.

 @Return        1 if the list is empty, 0 otherwise.
*//***************************************************************************/
static __inline__ int NCSW_LIST_IsEmpty(t_List *p_List)
{
    return (NCSW_LIST_FIRST(p_List) == p_List);
}


/**************************************************************************//**
 @Function      NCSW_LIST_Append

 @Description   Join two lists.

 @Param[in]     p_NewList - A pointer to the new list to add.
 @Param[in]     p_Head    - A pointer to the place to add it in the first list.

 @Return        none.
*//***************************************************************************/
void NCSW_LIST_Append(t_List *p_NewList, t_List *p_Head);


/**************************************************************************//**
 @Function      NCSW_LIST_NumOfObjs

 @Description   Counts number of objects in the list

 @Param[in]     p_List - A pointer to the list which objects are to be counted.

 @Return        Number of objects in the list.
*//***************************************************************************/
int NCSW_LIST_NumOfObjs(t_List *p_List);

/** @} */ /* end of list_id group */
/** @} */ /* end of etc_id group */


#endif /* __LIST_EXT_H */
