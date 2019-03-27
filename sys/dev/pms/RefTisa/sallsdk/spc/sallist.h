/*******************************************************************************
*Copyright (c) 2014 PMC-Sierra, Inc.  All rights reserved. 
*
*Redistribution and use in source and binary forms, with or without modification, are permitted provided 
*that the following conditions are met: 
*1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
*following disclaimer. 
*2. Redistributions in binary form must reproduce the above copyright notice, 
*this list of conditions and the following disclaimer in the documentation and/or other materials provided
*with the distribution. 
*
*THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED 
*WARRANTIES,INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
*FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
*NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR 
*BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
*LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
*SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
*
* $FreeBSD$
*
********************************************************************************/
/*******************************************************************************/
/*! \file sallist.h
 *  \brief The file contains link list manipulation helper routines
 *
 */
/*******************************************************************************/

#ifndef __SALLIST_H__
#define __SALLIST_H__


/********************************************************************
*********************************************************************
**   DATA STRUCTURES
********************************************************************/

/** \brief Structure of Link Data
 *
 *  link data, need to be included at the start (offset 0)
 *  of any strutures that are to be stored in the link list
 *
 */
typedef struct _SALINK
{
  struct _SALINK *pNext;
  struct _SALINK *pPrev;

  /*
  ** for assertion purpose only
  */
  struct _SALINK * pHead;     /* track the link list the link is a member of */

} SALINK, * PSALINK;

/** \brief Structure of Link List
 *
 * link list basic pointers
 *
 */
typedef struct _SALINK_LIST
{
  PSALINK pHead;
  bit32   Count;

  SALINK  Head; /* allways one link to speed up insert and delete */

} SALINK_LIST, * PSALINK_LIST;


/********************************************************************
*********************************************************************
** MACROS
********************************************************************/

/*! \def saLlistInitialize(pList)
* \brief saLlistInitialize macro
*
* use to initialize a Link List
*/
/*******************************************************************************
********************************************************************************
**
** MODULE NAME: saLlistInitialize
**
** PURPOSE:     Initialize a link list.
**
** PARAMETERS:  PSALINK_LIST  OUT - Link list definition.
**
** SIDE EFFECTS & CAVEATS:
**
** ALGORITHM:
**
********************************************************************************/
/*lint -emacro(613,saLlistInitialize) */

#define saLlistInitialize(pList) {(pList)->pHead        = &((pList)->Head); \
                                  (pList)->pHead->pNext = (pList)->pHead;   \
                                  (pList)->pHead->pPrev = (pList)->pHead;   \
                                  (pList)->Count        = 0;                \
                                 }

#define saLlistIOInitialize(pList){(pList)->pHead        = &((pList)->Head); \
                                  (pList)->pHead->pNext = (pList)->pHead;   \
                                  (pList)->pHead->pPrev = (pList)->pHead;   \
                                  (pList)->Count        = 0;                \
                                 }
/*! \def saLlinkInitialize(pLink)
* \brief saLlinkInitialize macro
*
* use to initialize a Link
*/
/********************************************************************************
********************************************************************************
**
** MODULE NAME: saLlinkInitialize
**
** PURPOSE:     Initialize a link.
**              This function should be used to initialize a new link before it
**              is used in the linked list. This will initialize the link so
**              the assertion will work
**
** PARAMETERS:  PSALINK      IN  - Link to be initialized.
**
** SIDE EFFECTS & CAVEATS:
**
** ALGORITHM:
**
********************************************************************************
*******************************************************************************/

/*lint -emacro(613,saLlinkInitialize) */

#define saLlinkInitialize(pLink) { (pLink)->pHead = agNULL;    \
                                   (pLink)->pNext = agNULL;    \
                                   (pLink)->pPrev = agNULL;    \
                                 }

#define saLlinkIOInitialize(pLink) { (pLink)->pHead = agNULL;    \
                                   (pLink)->pNext = agNULL;    \
                                   (pLink)->pPrev = agNULL;    \
                                 }
/*! \def saLlistAdd(pList, pLink)
* \brief saLlistAdd macro
*
* use to add a link to the tail of list
*/
/********************************************************************************
********************************************************************************
**
** MODULE NAME: saLlistAdd
**
** PURPOSE:     add a link at the tail of the list
**
** PARAMETERS:  PSALINK_LIST OUT - Link list definition.
**              PSALINK      IN  - Link to be inserted.
**
** SIDE EFFECTS & CAVEATS:
**   !!! assumes that fcllistInitialize has been called on the linklist
**   !!! if not, this function behavior is un-predictable
**
**   The OS_ASSERT() is an assignment for debug code only
**
** ALGORITHM:
**
********************************************************************************
*******************************************************************************/

/*lint -emacro(506,saLlistAdd) */
/*lint -emacro(613,saLlistAdd) */
/*lint -emacro(666,saLlistAdd) */
/*lint -emacro(720,saLlistAdd) */

#define saLlistAdd(pList, pLink) {                                          \
                             (pLink)->pNext        = (pList)->pHead;        \
                             (pLink)->pPrev        = (pList)->pHead->pPrev; \
                             (pLink)->pPrev->pNext = (pLink);               \
                             (pList)->pHead->pPrev = (pLink);               \
                             (pList)->Count ++;                             \
                             (pLink)->pHead = (pList)->pHead;               \
                             }

#define saLlistIOAdd(pList, pLink) {                                        \
                             (pLink)->pNext        = (pList)->pHead;        \
                             (pLink)->pPrev        = (pList)->pHead->pPrev; \
                             (pLink)->pPrev->pNext = (pLink);               \
                             (pList)->pHead->pPrev = (pLink);               \
                             (pList)->Count ++;                             \
                             (pLink)->pHead = (pList)->pHead;               \
                             }

/*! \def saLlistInsert(pList, pLink, pNew)
* \brief saLlistInsert macro
*
* use to insert a link preceding the given one
*/
/********************************************************************************
********************************************************************************
**
** MODULE NAME: saLlistInsert
**
** PURPOSE:     insert a link preceding the given one
**
** PARAMETERS:  PSALINK_LIST OUT - Link list definition.
**              PSALINK      IN  - Link to be inserted after.
**              PSALINK      IN  - Link to be inserted.
**
** SIDE EFFECTS & CAVEATS:
**   !!! assumes that fcllistInitialize has been called on the linklist
**   !!! if not, this function behavior is un-predictable
**
**   The OS_ASSERT() is an assignment for debug code only
**
** ALGORITHM:
**
********************************************************************************
*******************************************************************************/

/*lint -emacro(506,saLlistInsert) */
/*lint -emacro(613,saLlistInsert) */
/*lint -emacro(666,saLlistInsert) */
/*lint -emacro(720,saLlistInsert) */

#define saLlistInsert(pList, pLink, pNew) {                                 \
                                 (pNew)->pNext        = (pLink);            \
                                 (pNew)->pPrev        = (pLink)->pPrev;     \
                                 (pNew)->pPrev->pNext = (pNew);             \
                                 (pLink)->pPrev       = (pNew);             \
                                 (pList)->Count ++;                         \
                                 (pNew)->pHead = (pList)->pHead;            \
                                 }

/*! \def saLlistRemove(pList, pLink)
* \brief saLlistRemove macro
*
* use to remove the link from the list
*/
/********************************************************************************
********************************************************************************
**
** MODULE NAME: saLlistRemove
**
** PURPOSE:     remove the link from the list.
**
** PARAMETERS:  PSALINK_LIST OUT  - Link list definition.
**              PSALINK      IN   - Link to delet from list
**
** SIDE EFFECTS & CAVEATS:
**   !!! assumes that fcllistInitialize has been called on the linklist
**   !!! if not, this function behavior is un-predictable
**
**   !!! No validation is made on the list or the validity of the link
**   !!! the caller must make sure that the link is in the list
**
**
** ALGORITHM:
**
********************************************************************************
*******************************************************************************/

/*lint -emacro(506,saLlistRemove) */
/*lint -emacro(613,saLlistRemove) */
/*lint -emacro(666,saLlistRemove) */
/*lint -emacro(720,saLlistRemove) */

#define saLlistRemove(pList, pLink) {                                   \
                           (pLink)->pPrev->pNext = (pLink)->pNext;      \
                           (pLink)->pNext->pPrev = (pLink)->pPrev;      \
                           (pLink)->pHead = agNULL;                     \
                           (pList)->Count --;                           \
                           }

#define saLlistIORemove(pList, pLink) {                                 \
                           (pLink)->pPrev->pNext = (pLink)->pNext;      \
                           (pLink)->pNext->pPrev = (pLink)->pPrev;      \
                           (pLink)->pHead = agNULL;                     \
                           (pList)->Count --;                           \
                           }
/*! \def saLlistGetHead(pList)
* \brief saLlistGetHead macro
*
* use to get the link following the head link
*/
/********************************************************************************
********************************************************************************
**
** MODULE NAME: saLlistGetHead
**
** PURPOSE:     get the link following the head link.
**
** PARAMETERS:  PSALINK_LIST  OUT - Link list definition.
**              RETURNS - PSALINK   the link following the head
**                                  agNULL if the following link is the head
**
** SIDE EFFECTS & CAVEATS:
**   !!! assumes that fcllistInitialize has been called on the linklist
**   !!! if not, this function behavior is un-predictable
**
** ALGORITHM:
**
********************************************************************************
*******************************************************************************/
#define saLlistGetHead(pList) saLlistGetNext(pList,(pList)->pHead)

#define saLlistIOGetHead(pList) saLlistGetNext(pList,(pList)->pHead)

/*! \def saLlistGetTail(pList)
* \brief saLlistGetTail macro
*
* use to get the link preceding the tail link
*/
/********************************************************************************
********************************************************************************
**
** MODULE NAME: saLlistGetTail
**
** PURPOSE:     get the link preceding the tail link.
**
** PARAMETERS:  PSALINK_LIST  OUT - Link list definition.
**              RETURNS - PSALINK   the link preceding the head
**                                  agNULL if the preceding link is the head
**
** SIDE EFFECTS & CAVEATS:
**
** ALGORITHM:
**
********************************************************************************
*******************************************************************************/
#define saLlistGetTail(pList) saLlistGetPrev((pList), (pList)->pHead)

/*! \def saLlistGetCount(pList)
* \brief saLlistGetCount macro
*
* use to get the number of links in the list excluding head and tail
*/
/********************************************************************************
********************************************************************************
**
** MODULE NAME: saLlistGetCount
**
** PURPOSE:     get the number of links in the list excluding head and tail.
**
** PARAMETERS:  PSALINK_LIST  OUT - Link list definition.
**
** SIDE EFFECTS & CAVEATS:
**   !!! assumes that fcllistInitialize has been called on the linklist
**   !!! if not, this function behavior is un-predictable
**
** ALGORITHM:
**
********************************************************************************
*******************************************************************************/

/*lint -emacro(613,saLlistGetCount) */
/*lint -emacro(666,saLlistGetCount) */

#define saLlistGetCount(pList) ((pList)->Count)

#define saLlistIOGetCount(pList) ((pList)->Count)

/*! \def saLlistGetNext(pList, pLink)
* \brief saLlistGetNext macro
*
* use to get the next link in the list
*/
/********************************************************************************
********************************************************************************
**
** MODULE NAME: saLlistGetNext
**
** PURPOSE:     get the next link in the list. (one toward tail)
**
** PARAMETERS:  PSALINK_LIST  OUT - Link list definition.
**              PSALINK       IN  - Link to get next to
**
**           return PLINK  - points to next link
**                           agNULL if next link is head
**
** SIDE EFFECTS & CAVEATS:
**   !!! assumes that fcllistInitialize has been called on the linklist
**   !!! if not, this function behavior is un-predictable
**
**   !!! No validation is made on the list or the validity of the link
**   !!! the caller must make sure that the link is in the list
**
** ALGORITHM:
**
********************************************************************************
*******************************************************************************/

/*lint -emacro(613,saLlistGetNext) */

#define saLlistGetNext(pList, pLink) (((pLink)->pNext == (pList)->pHead) ?  \
                                      agNULL : (pLink)->pNext)

#define saLlistIOGetNext(pList, pLink) (((pLink)->pNext == (pList)->pHead) ?  \
                                        agNULL : (pLink)->pNext)

/*! \def saLlistGetPrev(pList, pLink)
* \brief saLlistGetPrev macro
*
* use to get the previous link in the list
*/
/********************************************************************************
********************************************************************************
**
** MODULE NAME: saLlistGetPrev
**
** PURPOSE:     get the previous link in the list. (one toward head)
**
** PARAMETERS:  PSALINK_LIST  OUT - Link list definition.
**              PSALINK       IN  - Link to get prev to
**
**           return PLINK  - points to previous link
**                           agNULL if previous link is head
**
** SIDE EFFECTS & CAVEATS:
**   !!! assumes that fcllistInitialize has been called on the linklist
**   !!! if not, this function behavior is un-predictable
**
**   !!! No validation is made on the list or the validity of the link
**   !!! the caller must make sure that the link is in the list
**
** ALGORITHM:
**
********************************************************************************
*******************************************************************************/

/*lint -emacro(613,saLlistGetPrev) */

#define saLlistGetPrev(pList, pLink) (((pLink)->pPrev == (pList)->pHead) ?  \
                                      agNULL : (pLink)->pPrev)



#define agObjectBase(baseType,fieldName,fieldPtr) \
            (void * ) fieldPtr == (void *) 0 ? (baseType *) 0 : \
            ((baseType *)((bit8 *)(fieldPtr) - ((bitptr)(&(((baseType *)0)->fieldName)))))


#endif /* #ifndef __SALLIST_H__*/
