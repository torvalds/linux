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
/** \file
 *
 * The file defines list data structures for SAS/SATA TD layer
 *
 */

#ifndef __TDLIST_H__
#define __TDLIST_H__


typedef struct tdList_s tdList_t;

struct tdList_s {
  tdList_t  *flink;
  tdList_t  *blink;
};

#define TDLIST_NEXT_ENTRY(ptr, type, member)        \
  container_of((ptr)->flink, type, member)  

#define TDLIST_INIT_HDR(hdr)                        \
  do {                                              \
    ((tdList_t *)(hdr))->flink = (tdList_t *)(hdr); \
    ((tdList_t *)(hdr))->blink = (tdList_t *)(hdr); \
  } while (0)

#define TDLIST_INIT_ELEMENT(hdr)                     \
  do {                                               \
    ((tdList_t *)(hdr))->flink = (tdList_t *)agNULL; \
    ((tdList_t *)(hdr))->blink = (tdList_t *)agNULL; \
  } while (0)

#define TDLIST_ENQUEUE_AT_HEAD(toAddHdr,listHdr)                                \
  do {                                                                          \
    ((tdList_t *)(toAddHdr))->flink           = ((tdList_t *)(listHdr))->flink; \
    ((tdList_t *)(toAddHdr))->blink           = (tdList_t *)(listHdr) ;         \
    ((tdList_t *)(listHdr))->flink->blink     = (tdList_t *)(toAddHdr);         \
    ((tdList_t *)(listHdr))->flink            = (tdList_t *)(toAddHdr);         \
  } while (0)

#define TDLIST_ENQUEUE_AT_TAIL(toAddHdr,listHdr)                                \
  do {                                                                          \
    ((tdList_t *)(toAddHdr))->flink           = (tdList_t *)(listHdr);          \
    ((tdList_t *)(toAddHdr))->blink           = ((tdList_t *)(listHdr))->blink; \
    ((tdList_t *)(listHdr))->blink->flink     = (tdList_t *)(toAddHdr);         \
    ((tdList_t *)(listHdr))->blink            = (tdList_t *)(toAddHdr);         \
  } while (0)

#define TDLIST_EMPTY(listHdr) \
  (((tdList_t *)(listHdr))->flink == ((tdList_t *)(listHdr)))

#define TDLIST_NOT_EMPTY(listHdr) \
  (!TDLIST_EMPTY(listHdr))

#define TDLIST_DEQUEUE_THIS(hdr)                                      \
  do {                                                                \
    ((tdList_t *)(hdr))->blink->flink = ((tdList_t *)(hdr))->flink;   \
    ((tdList_t *)(hdr))->flink->blink = ((tdList_t *)(hdr))->blink;   \
    ((tdList_t *)(hdr))->flink = ((tdList_t *)(hdr))->blink = agNULL; \
  } while (0)

#define TDLIST_DEQUEUE_FROM_HEAD_FAST(atHeadHdr,listHdr)                              \
  do {                                                                                \
    *((tdList_t **)(atHeadHdr))                 = ((tdList_t *)(listHdr))->flink;     \
    (*((tdList_t **)(atHeadHdr)))->flink->blink = (tdList_t *)(listHdr);              \
    ((tdList_t *)(listHdr))->flink              = (*(tdList_t **)(atHeadHdr))->flink; \
  } while (0)

#define TDLIST_DEQUEUE_FROM_HEAD(atHeadHdr,listHdr)             \
do {                                                            \
  if (TDLIST_NOT_EMPTY((listHdr)))                              \
  {                                                             \
    TDLIST_DEQUEUE_FROM_HEAD_FAST(atHeadHdr,listHdr);           \
  }                                                             \
  else                                                          \
  {                                                             \
    (*((tdList_t **)(atHeadHdr))) = (tdList_t *)agNULL;         \
  }                                                             \
} while (0)
  
#define TDLIST_DEQUEUE_FROM_TAIL_FAST(atTailHdr,listHdr)                                \
  do {                                                                                  \
    (*((tdList_t **)(atTailHdr)))               = ((tdList_t *)(listHdr))->blink;       \
    (*((tdList_t **)(atTailHdr)))->blink->flink = (tdList_t *)(listHdr);                \
    ((tdList_t *)(listHdr))->blink              = (*((tdList_t **)(atTailHdr)))->blink; \
  } while (0)

#define TDLIST_DEQUEUE_FROM_TAIL(atTailHdr,listHdr)               \
  do {                                                            \
    if (TDLIST_NOT_EMPTY((listHdr)))                              \
    {                                                             \
      TDLIST_DEQUEUE_FROM_TAIL_FAST(atTailHdr,listHdr);           \
    }                                                             \
    else                                                          \
    {                                                             \
      (*((tdList_t **)(atTailHdr))) = (tdList_t *)agNULL;         \
    }                                                             \
  } while (0)

#define TDLIST_ENQUEUE_LIST_AT_TAIL_FAST(toAddListHdr, listHdr)               \
  do {                                                                        \
    ((tdList_t *)toAddListHdr)->blink->flink = ((tdList_t *)listHdr);         \
    ((tdList_t *)toAddListHdr)->flink->blink = ((tdList_t *)listHdr)->blink;  \
    ((tdList_t *)listHdr)->blink->flink = ((tdList_t *)toAddListHdr)->flink;  \
    ((tdList_t *)listHdr)->blink = ((tdList_t *)toAddListHdr)->blink;         \
    TDLIST_INIT_HDR(toAddListHdr);                                            \
  } while (0)

#define TDLIST_ENQUEUE_LIST_AT_TAIL(toAddListHdr, listHdr)                    \
  do {                                                                        \
    if (TDLIST_NOT_EMPTY(toAddListHdr))                                       \
    {                                                                         \
      TDLIST_ENQUEUE_LIST_AT_TAIL_FAST(toAddListHdr, listHdr);                \
    }                                                                         \
  } while (0)

#define TDLIST_ENQUEUE_LIST_AT_HEAD_FAST(toAddListHdr, listHdr)               \
  do {                                                                        \
    ((tdList_t *)toAddListHdr)->blink->flink = ((tdList_t *)listHdr)->flink;  \
    ((tdList_t *)toAddListHdr)->flink->blink = ((tdList_t *)listHdr);         \
    ((tdList_t *)listHdr)->flink->blink = ((tdList_t *)toAddListHdr)->blink;  \
    ((tdList_t *)listHdr)->flink = ((tdList_t *)toAddListHdr)->flink;         \
    TDLIST_INIT_HDR(toAddListHdr);                                            \
  } while (0)

#define TDLIST_ENQUEUE_LIST_AT_HEAD(toAddListHdr, listHdr)                    \
  do {                                                                        \
    if (TDLIST_NOT_EMPTY(toAddListHdr))                                       \
    {                                                                         \
      TDLIST_ENQUEUE_LIST_AT_HEAD_FAST(toAddListHdr, listHdr);                \
    }                                                                         \
  } while (0)

#define TD_FIELD_OFFSET(baseType,fieldName) \
                    ((bit32)((bitptr)(&(((baseType *)0)->fieldName))))

#define TDLIST_OBJECT_BASE(baseType,fieldName,fieldPtr)         \
                    (void *)fieldPtr == (void *)0 ? (baseType *)0 :             \
                    ((baseType *)((bit8 *)(fieldPtr) - ((bitptr)(&(((baseType *)0)->fieldName)))))



#endif /* __TDLIST_H__ */

