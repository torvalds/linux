//------------------------------------------------------------------------------
// <copyright file="htc_packet.h" company="Atheros">
//    Copyright (c) 2007-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================
#ifndef HTC_PACKET_H_
#define HTC_PACKET_H_


#include "dl_list.h"

/* ------ Endpoint IDS ------ */
typedef enum
{
    ENDPOINT_UNUSED = -1,
    ENDPOINT_0 = 0,
    ENDPOINT_1 = 1,
    ENDPOINT_2 = 2,
    ENDPOINT_3,
    ENDPOINT_4,
    ENDPOINT_5,
    ENDPOINT_6,
    ENDPOINT_7,
    ENDPOINT_8,
    ENDPOINT_MAX,
} HTC_ENDPOINT_ID;

struct _HTC_PACKET;

typedef void (* HTC_PACKET_COMPLETION)(void *,struct _HTC_PACKET *);

typedef A_UINT16 HTC_TX_TAG;

typedef struct _HTC_TX_PACKET_INFO {
    HTC_TX_TAG    Tag;            /* tag used to selective flush packets */
    int           CreditsUsed;    /* number of credits used for this TX packet (HTC internal) */
    A_UINT8       SendFlags;      /* send flags (HTC internal) */
    int           SeqNo;          /* internal seq no for debugging (HTC internal) */
} HTC_TX_PACKET_INFO;

#define HTC_TX_PACKET_TAG_ALL          0    /* a tag of zero is reserved and used to flush ALL packets */
#define HTC_TX_PACKET_TAG_INTERNAL     1                                /* internal tags start here */
#define HTC_TX_PACKET_TAG_USER_DEFINED (HTC_TX_PACKET_TAG_INTERNAL + 9) /* user-defined tags start here */

typedef struct _HTC_RX_PACKET_INFO {
    A_UINT32    ExpectedHdr;        /* HTC internal use */
    A_UINT32    HTCRxFlags;         /* HTC internal use */
    A_UINT32    IndicationFlags;    /* indication flags set on each RX packet indication */
} HTC_RX_PACKET_INFO;

#define HTC_RX_FLAGS_INDICATE_MORE_PKTS  (1 << 0)   /* more packets on this endpoint are being fetched */

/* wrapper around endpoint-specific packets */
typedef struct _HTC_PACKET {
    DL_LIST         ListLink;       /* double link */
    void            *pPktContext;   /* caller's per packet specific context */

    A_UINT8         *pBufferStart;  /* the true buffer start , the caller can
                                       store the real buffer start here.  In
                                       receive callbacks, the HTC layer sets pBuffer
                                       to the start of the payload past the header. This
                                       field allows the caller to reset pBuffer when it
                                       recycles receive packets back to HTC */
    /*
     * Pointer to the start of the buffer. In the transmit
     * direction this points to the start of the payload. In the
     * receive direction, however, the buffer when queued up
     * points to the start of the HTC header but when returned
     * to the caller points to the start of the payload
     */
    A_UINT8         *pBuffer;       /* payload start (RX/TX) */
    A_UINT32        BufferLength;   /* length of buffer */
    A_UINT32        ActualLength;   /* actual length of payload */
    HTC_ENDPOINT_ID Endpoint;       /* endpoint that this packet was sent/recv'd from */
    A_STATUS        Status;         /* completion status */
    union {
        HTC_TX_PACKET_INFO  AsTx;   /* Tx Packet specific info */
        HTC_RX_PACKET_INFO  AsRx;   /* Rx Packet specific info */
    } PktInfo;

    /* the following fields are for internal HTC use */
    HTC_PACKET_COMPLETION Completion;   /* completion */
    void                  *pContext;    /* HTC private completion context */
} HTC_PACKET;



#define COMPLETE_HTC_PACKET(p,status)        \
{                                            \
    (p)->Status = (status);                  \
    (p)->Completion((p)->pContext,(p));      \
}

#define INIT_HTC_PACKET_INFO(p,b,len)             \
{                                                 \
    (p)->pBufferStart = (b);                      \
    (p)->BufferLength = (len);                    \
}

/* macro to set an initial RX packet for refilling HTC */
#define SET_HTC_PACKET_INFO_RX_REFILL(p,c,b,len,ep) \
{                                                 \
    (p)->pPktContext = (c);                       \
    (p)->pBuffer = (b);                           \
    (p)->pBufferStart = (b);                      \
    (p)->BufferLength = (len);                    \
    (p)->Endpoint = (ep);                         \
}

/* fast macro to recycle an RX packet that will be re-queued to HTC */
#define HTC_PACKET_RESET_RX(p)              \
    { (p)->pBuffer = (p)->pBufferStart; (p)->ActualLength = 0; }  

/* macro to set packet parameters for TX */
#define SET_HTC_PACKET_INFO_TX(p,c,b,len,ep,tag)  \
{                                                 \
    (p)->pPktContext = (c);                       \
    (p)->pBuffer = (b);                           \
    (p)->ActualLength = (len);                    \
    (p)->Endpoint = (ep);                         \
    (p)->PktInfo.AsTx.Tag = (tag);                \
}

/* HTC Packet Queueing Macros */
typedef struct _HTC_PACKET_QUEUE {
    DL_LIST     QueueHead;
    int         Depth;    
} HTC_PACKET_QUEUE;
 
/* initialize queue */
#define INIT_HTC_PACKET_QUEUE(pQ)   \
{                                   \
    DL_LIST_INIT(&(pQ)->QueueHead); \
    (pQ)->Depth = 0;                \
}

/* enqueue HTC packet to the tail of the queue */
#define HTC_PACKET_ENQUEUE(pQ,p)                        \
{   DL_ListInsertTail(&(pQ)->QueueHead,&(p)->ListLink); \
    (pQ)->Depth++;                                      \
}

/* enqueue HTC packet to the tail of the queue */
#define HTC_PACKET_ENQUEUE_TO_HEAD(pQ,p)                \
{   DL_ListInsertHead(&(pQ)->QueueHead,&(p)->ListLink); \
    (pQ)->Depth++;                                      \
}
/* test if a queue is empty */
#define HTC_QUEUE_EMPTY(pQ)       ((pQ)->Depth == 0)
/* get packet at head without removing it */
static INLINE HTC_PACKET *HTC_GET_PKT_AT_HEAD(HTC_PACKET_QUEUE *queue)   {
    if (queue->Depth == 0) {
        return NULL; 
    }  
    return A_CONTAINING_STRUCT((DL_LIST_GET_ITEM_AT_HEAD(&queue->QueueHead)),HTC_PACKET,ListLink);
}
/* remove a packet from a queue, where-ever it is in the queue */
#define HTC_PACKET_REMOVE(pQ,p)     \
{                                   \
    DL_ListRemove(&(p)->ListLink);  \
    (pQ)->Depth--;                  \
}

/* dequeue an HTC packet from the head of the queue */
static INLINE HTC_PACKET *HTC_PACKET_DEQUEUE(HTC_PACKET_QUEUE *queue) {
    DL_LIST    *pItem = DL_ListRemoveItemFromHead(&queue->QueueHead);
    if (pItem != NULL) {
        queue->Depth--;
        return A_CONTAINING_STRUCT(pItem, HTC_PACKET, ListLink);
    }
    return NULL;
}

/* dequeue an HTC packet from the tail of the queue */
static INLINE HTC_PACKET *HTC_PACKET_DEQUEUE_TAIL(HTC_PACKET_QUEUE *queue) {
    DL_LIST    *pItem = DL_ListRemoveItemFromTail(&queue->QueueHead);
    if (pItem != NULL) {
        queue->Depth--;
        return A_CONTAINING_STRUCT(pItem, HTC_PACKET, ListLink);
    }
    return NULL;
}

#define HTC_PACKET_QUEUE_DEPTH(pQ) (pQ)->Depth


#define HTC_GET_ENDPOINT_FROM_PKT(p) (p)->Endpoint
#define HTC_GET_TAG_FROM_PKT(p)      (p)->PktInfo.AsTx.Tag

    /* transfer the packets from one queue to the tail of another queue */
#define HTC_PACKET_QUEUE_TRANSFER_TO_TAIL(pQDest,pQSrc) \
{                                                                           \
    DL_ListTransferItemsToTail(&(pQDest)->QueueHead,&(pQSrc)->QueueHead);   \
    (pQDest)->Depth += (pQSrc)->Depth;                                      \
    (pQSrc)->Depth = 0;                                                     \
}

    /* fast version to init and add a single packet to a queue */
#define INIT_HTC_PACKET_QUEUE_AND_ADD(pQ,pP) \
{                                            \
    DL_LIST_INIT_AND_ADD(&(pQ)->QueueHead,&(pP)->ListLink)  \
    (pQ)->Depth = 1;                                        \
}
    
#define HTC_PACKET_QUEUE_ITERATE_ALLOW_REMOVE(pQ, pPTemp) \
    ITERATE_OVER_LIST_ALLOW_REMOVE(&(pQ)->QueueHead,(pPTemp), HTC_PACKET, ListLink) 

#define HTC_PACKET_QUEUE_ITERATE_END ITERATE_END
        
#endif /*HTC_PACKET_H_*/
