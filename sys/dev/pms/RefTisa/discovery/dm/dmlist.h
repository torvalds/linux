/*******************************************************************************
**
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

#ifndef __DMLIST_H__
#define __DMLIST_H__

typedef struct dmList_s dmList_t;

struct dmList_s {
  dmList_t  *flink;
  dmList_t  *blink;
};

#define DMLIST_INIT_HDR(hdr)                        \
  do {                                              \
    ((dmList_t *)(hdr))->flink = (dmList_t *)(hdr); \
    ((dmList_t *)(hdr))->blink = (dmList_t *)(hdr); \
  } while (0)

#define DMLIST_INIT_ELEMENT(hdr)                     \
  do {                                               \
    ((dmList_t *)(hdr))->flink = (dmList_t *)agNULL; \
    ((dmList_t *)(hdr))->blink = (dmList_t *)agNULL; \
  } while (0)

#define DMLIST_ENQUEUE_AT_HEAD(toAddHdr,listHdr)                                \
  do {                                                                          \
    ((dmList_t *)(toAddHdr))->flink           = ((dmList_t *)(listHdr))->flink; \
    ((dmList_t *)(toAddHdr))->blink           = (dmList_t *)(listHdr) ;         \
    ((dmList_t *)(listHdr))->flink->blink     = (dmList_t *)(toAddHdr);         \
    ((dmList_t *)(listHdr))->flink            = (dmList_t *)(toAddHdr);         \
  } while (0)

#define DMLIST_ENQUEUE_AT_TAIL(toAddHdr,listHdr)                                \
  do {                                                                          \
    ((dmList_t *)(toAddHdr))->flink           = (dmList_t *)(listHdr);          \
    ((dmList_t *)(toAddHdr))->blink           = ((dmList_t *)(listHdr))->blink; \
    ((dmList_t *)(listHdr))->blink->flink     = (dmList_t *)(toAddHdr);         \
    ((dmList_t *)(listHdr))->blink            = (dmList_t *)(toAddHdr);         \
  } while (0)

#define DMLIST_EMPTY(listHdr) \
  (((dmList_t *)(listHdr))->flink == ((dmList_t *)(listHdr)))

#define DMLIST_NOT_EMPTY(listHdr) \
  (!DMLIST_EMPTY(listHdr))

#define DMLIST_DEQUEUE_THIS(hdr)                                      \
  do {                                                                \
    ((dmList_t *)(hdr))->blink->flink = ((dmList_t *)(hdr))->flink;   \
    ((dmList_t *)(hdr))->flink->blink = ((dmList_t *)(hdr))->blink;   \
    ((dmList_t *)(hdr))->flink = ((dmList_t *)(hdr))->blink = agNULL; \
  } while (0)

#define DMLIST_DEQUEUE_FROM_HEAD_FAST(atHeadHdr,listHdr)                              \
  do {                                                                                \
    *((dmList_t **)(atHeadHdr))                 = ((dmList_t *)(listHdr))->flink;     \
    (*((dmList_t **)(atHeadHdr)))->flink->blink = (dmList_t *)(listHdr);              \
    ((dmList_t *)(listHdr))->flink              = (*(dmList_t **)(atHeadHdr))->flink; \
  } while (0)

#define DMLIST_DEQUEUE_FROM_HEAD(atHeadHdr,listHdr)             \
do {                                                            \
  if (DMLIST_NOT_EMPTY((listHdr)))                              \
  {                                                             \
    DMLIST_DEQUEUE_FROM_HEAD_FAST(atHeadHdr,listHdr);           \
  }                                                             \
  else                                                          \
  {                                                             \
    (*((dmList_t **)(atHeadHdr))) = (dmList_t *)agNULL;         \
  }                                                             \
} while (0)
  
#define DMLIST_DEQUEUE_FROM_TAIL_FAST(atTailHdr,listHdr)                                \
  do {                                                                                  \
    (*((dmList_t **)(atTailHdr)))               = ((dmList_t *)(listHdr))->blink;       \
    (*((dmList_t **)(atTailHdr)))->blink->flink = (dmList_t *)(listHdr);                \
    ((dmList_t *)(listHdr))->blink              = (*((dmList_t **)(atTailHdr)))->blink; \
  } while (0)

#define DMLIST_DEQUEUE_FROM_TAIL(atTailHdr,listHdr)               \
  do {                                                            \
    if (DMLIST_NOT_EMPTY((listHdr)))                              \
    {                                                             \
      DMLIST_DEQUEUE_FROM_TAIL_FAST(atTailHdr,listHdr);           \
    }                                                             \
    else                                                          \
    {                                                             \
      (*((dmList_t **)(atTailHdr))) = (dmList_t *)agNULL;         \
    }                                                             \
  } while (0)

#define DMLIST_ENQUEUE_LIST_AT_TAIL_FAST(toAddListHdr, listHdr)               \
  do {                                                                        \
    ((dmList_t *)toAddListHdr)->blink->flink = ((dmList_t *)listHdr);         \
    ((dmList_t *)toAddListHdr)->flink->blink = ((dmList_t *)listHdr)->blink;  \
    ((dmList_t *)listHdr)->blink->flink = ((dmList_t *)toAddListHdr)->flink;  \
    ((dmList_t *)listHdr)->blink = ((dmList_t *)toAddListHdr)->blink;         \
    DMLIST_INIT_HDR(toAddListHdr);                                            \
  } while (0)

#define DMLIST_ENQUEUE_LIST_AT_TAIL(toAddListHdr, listHdr)                    \
  do {                                                                        \
    if (DMLIST_NOT_EMPTY(toAddListHdr))                                       \
    {                                                                         \
      DMLIST_ENQUEUE_LIST_AT_TAIL_FAST(toAddListHdr, listHdr);                \
    }                                                                         \
  } while (0)

#define DMLIST_ENQUEUE_LIST_AT_HEAD_FAST(toAddListHdr, listHdr)               \
  do {                                                                        \
    ((dmList_t *)toAddListHdr)->blink->flink = ((dmList_t *)listHdr)->flink;  \
    ((dmList_t *)toAddListHdr)->flink->blink = ((dmList_t *)listHdr);         \
    ((dmList_t *)listHdr)->flink->blink = ((dmList_t *)toAddListHdr)->blink;  \
    ((dmList_t *)listHdr)->flink = ((dmList_t *)toAddListHdr)->flink;         \
    DMLIST_INIT_HDR(toAddListHdr);                                            \
  } while (0)

#define DMLIST_ENQUEUE_LIST_AT_HEAD(toAddListHdr, listHdr)                    \
  do {                                                                        \
    if (DMLIST_NOT_EMPTY(toAddListHdr))                                       \
    {                                                                         \
      DMLIST_ENQUEUE_LIST_AT_HEAD_FAST(toAddListHdr, listHdr);                \
    }                                                                         \
  } while (0)

#define TD_FIELD_OFFSET(baseType,fieldName) \
                    ((bit32)((bitptr)(&(((baseType *)0)->fieldName))))

#define DMLIST_OBJECT_BASE(baseType,fieldName,fieldPtr)         \
                    (void *)fieldPtr == (void *)0 ? (baseType *)0 :             \
                    ((baseType *)((bit8 *)(fieldPtr) - ((bitptr)(&(((baseType *)0)->fieldName)))))




#endif /* __DMLIST_H__ */


