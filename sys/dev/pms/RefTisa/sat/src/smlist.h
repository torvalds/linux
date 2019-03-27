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

#ifndef __SMLIST_H__
#define __SMLIST_H__

typedef struct smList_s smList_t;

struct smList_s {
  smList_t  *flink;
  smList_t  *blink;
};

#define SMLIST_INIT_HDR(hdr)                        \
  do {                                              \
    ((smList_t *)(hdr))->flink = (smList_t *)(hdr); \
    ((smList_t *)(hdr))->blink = (smList_t *)(hdr); \
  } while (0)

#define SMLIST_INIT_ELEMENT(hdr)                     \
  do {                                               \
    ((smList_t *)(hdr))->flink = (smList_t *)agNULL; \
    ((smList_t *)(hdr))->blink = (smList_t *)agNULL; \
  } while (0)

#define SMLIST_ENQUEUE_AT_HEAD(toAddHdr,listHdr)                                \
  do {                                                                          \
    ((smList_t *)(toAddHdr))->flink           = ((smList_t *)(listHdr))->flink; \
    ((smList_t *)(toAddHdr))->blink           = (smList_t *)(listHdr) ;         \
    ((smList_t *)(listHdr))->flink->blink     = (smList_t *)(toAddHdr);         \
    ((smList_t *)(listHdr))->flink            = (smList_t *)(toAddHdr);         \
  } while (0)

#define SMLIST_ENQUEUE_AT_TAIL(toAddHdr,listHdr)                                \
  do {                                                                          \
    ((smList_t *)(toAddHdr))->flink           = (smList_t *)(listHdr);          \
    ((smList_t *)(toAddHdr))->blink           = ((smList_t *)(listHdr))->blink; \
    ((smList_t *)(listHdr))->blink->flink     = (smList_t *)(toAddHdr);         \
    ((smList_t *)(listHdr))->blink            = (smList_t *)(toAddHdr);         \
  } while (0)

#define SMLIST_EMPTY(listHdr) \
  (((smList_t *)(listHdr))->flink == ((smList_t *)(listHdr)))

#define SMLIST_NOT_EMPTY(listHdr) \
  (!SMLIST_EMPTY(listHdr))

#define SMLIST_DEQUEUE_THIS(hdr)                                      \
  do {                                                                \
    ((smList_t *)(hdr))->blink->flink = ((smList_t *)(hdr))->flink;   \
    ((smList_t *)(hdr))->flink->blink = ((smList_t *)(hdr))->blink;   \
    ((smList_t *)(hdr))->flink = ((smList_t *)(hdr))->blink = agNULL; \
  } while (0)

#define SMLIST_DEQUEUE_FROM_HEAD_FAST(atHeadHdr,listHdr)                              \
  do {                                                                                \
    *((smList_t **)(atHeadHdr))                 = ((smList_t *)(listHdr))->flink;     \
    (*((smList_t **)(atHeadHdr)))->flink->blink = (smList_t *)(listHdr);              \
    ((smList_t *)(listHdr))->flink              = (*(smList_t **)(atHeadHdr))->flink; \
  } while (0)

#define SMLIST_DEQUEUE_FROM_HEAD(atHeadHdr,listHdr)             \
do {                                                            \
  if (SMLIST_NOT_EMPTY((listHdr)))                              \
  {                                                             \
    SMLIST_DEQUEUE_FROM_HEAD_FAST(atHeadHdr,listHdr);           \
  }                                                             \
  else                                                          \
  {                                                             \
    (*((smList_t **)(atHeadHdr))) = (smList_t *)agNULL;         \
  }                                                             \
} while (0)
  
#define SMLIST_DEQUEUE_FROM_TAIL_FAST(atTailHdr,listHdr)                                \
  do {                                                                                  \
    (*((smList_t **)(atTailHdr)))               = ((smList_t *)(listHdr))->blink;       \
    (*((smList_t **)(atTailHdr)))->blink->flink = (smList_t *)(listHdr);                \
    ((smList_t *)(listHdr))->blink              = (*((smList_t **)(atTailHdr)))->blink; \
  } while (0)

#define SMLIST_DEQUEUE_FROM_TAIL(atTailHdr,listHdr)               \
  do {                                                            \
    if (SMLIST_NOT_EMPTY((listHdr)))                              \
    {                                                             \
      SMLIST_DEQUEUE_FROM_TAIL_FAST(atTailHdr,listHdr);           \
    }                                                             \
    else                                                          \
    {                                                             \
      (*((smList_t **)(atTailHdr))) = (smList_t *)agNULL;         \
    }                                                             \
  } while (0)

#define SMLIST_ENQUEUE_LIST_AT_TAIL_FAST(toAddListHdr, listHdr)               \
  do {                                                                        \
    ((smList_t *)toAddListHdr)->blink->flink = ((smList_t *)listHdr);         \
    ((smList_t *)toAddListHdr)->flink->blink = ((smList_t *)listHdr)->blink;  \
    ((smList_t *)listHdr)->blink->flink = ((smList_t *)toAddListHdr)->flink;  \
    ((smList_t *)listHdr)->blink = ((smList_t *)toAddListHdr)->blink;         \
    SMLIST_INIT_HDR(toAddListHdr);                                            \
  } while (0)

#define SMLIST_ENQUEUE_LIST_AT_TAIL(toAddListHdr, listHdr)                    \
  do {                                                                        \
    if (SMLIST_NOT_EMPTY(toAddListHdr))                                       \
    {                                                                         \
      SMLIST_ENQUEUE_LIST_AT_TAIL_FAST(toAddListHdr, listHdr);                \
    }                                                                         \
  } while (0)

#define SMLIST_ENQUEUE_LIST_AT_HEAD_FAST(toAddListHdr, listHdr)               \
  do {                                                                        \
    ((smList_t *)toAddListHdr)->blink->flink = ((smList_t *)listHdr)->flink;  \
    ((smList_t *)toAddListHdr)->flink->blink = ((smList_t *)listHdr);         \
    ((smList_t *)listHdr)->flink->blink = ((smList_t *)toAddListHdr)->blink;  \
    ((smList_t *)listHdr)->flink = ((smList_t *)toAddListHdr)->flink;         \
    SMLIST_INIT_HDR(toAddListHdr);                                            \
  } while (0)

#define SMLIST_ENQUEUE_LIST_AT_HEAD(toAddListHdr, listHdr)                    \
  do {                                                                        \
    if (SMLIST_NOT_EMPTY(toAddListHdr))                                       \
    {                                                                         \
      SMLIST_ENQUEUE_LIST_AT_HEAD_FAST(toAddListHdr, listHdr);                \
    }                                                                         \
  } while (0)

#define TD_FIELD_OFFSET(baseType,fieldName) \
                    ((bit32)((bitptr)(&(((baseType *)0)->fieldName))))

#define SMLIST_OBJECT_BASE(baseType,fieldName,fieldPtr)         \
                    (void *)fieldPtr == (void *)0 ? (baseType *)0 :             \
                    ((baseType *)((bit8 *)(fieldPtr) - ((bitptr)(&(((baseType *)0)->fieldName)))))




#endif /* __SMLIST_H__ */



