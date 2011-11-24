/*******************************************************************************

            Copyright (c) 2004 by Nanoradio AB 

This software is copyrighted by and is the sole property of Nanoradio AB.
 All rights, title, ownership, or other interests in the
software remain the property of Nanoradio AB.  This software may
only be used in accordance with the corresponding license agreement.  Any
unauthorized use, duplication, transmission, distribution, or disclosure of
this software is expressly forbidden.

This Copyright notice may not be removed or modified without prior written
consent of Nanoradio AB.

Nanoradio AB reserves the right to modify this software without
notice.

Nanoradio AB
Torshamnsgatan 39                       info@nanoradio.se
164 40 Kista                       http://www.nanoradio.se
SWEDEN
*******************************************************************************/
/*----------------------------------------------------------------------------*/
/*! \file

\brief [this module handles things related to life, universe and everythig]

This module is part of the macll block.
Thing are coming in and things are coming out, bla bla bla.
]
*/
/*----------------------------------------------------------------------------*/
#ifndef UCOS_IDEFS_H
#define UCOS_IDEFS_H
#include "ucos_defs.h"

/* M A C R O S  **************************************************************/

/* E X P O R T E D  D A T A T Y P E S ****************************************/

/*--------------------------*/
/* Buffer related typedef's */
/*--------------------------*/
typedef struct SmallXBufferType
{
   struct SmallXBufferType  * next;
#ifdef UCOS_XMALLOC_DEBUG
   char*                      file;
   int                        line;
#endif
   char                     buffer[UCOS_SMALL_XBUFFER_SIZE];
}SmallXBufferType;

typedef struct LargeXBufferType
{
   struct LargeXBufferType  * next;
#ifdef UCOS_XMALLOC_DEBUG
   char*                      file;
   int                        line;
#endif
   char buffer[UCOS_LARGE_XBUFFER_SIZE];
}LargeXBufferType;

typedef struct XBufRefType
{
   uint8_t  * next;
#ifdef UCOS_XMALLOC_DEBUG
   char*      file;
   int        line;
#endif
   uint8_t buffer[1];
}XBufRefType;




/*-----------------------------*/
/* Scheduler related typedef's */
/*-----------------------------*/
typedef struct msg_fifo_entry_tag
{
   ucos_msg_param_t            param;
   ucos_object_entry_t         obj_entry;
   struct msg_fifo_entry_tag * next;
   ucos_msg_id_t               msg;
   SYSDEF_ObjectType           obj_id;
}ucos_msg_fifo_entry_t;

typedef ucos_msg_fifo_entry_t ucos_msg_fifo_t[SYSCFG_UCOS_MSGFIFO_SIZE];

typedef struct
{
   bool_t             empty;
   ucos_msg_fifo_entry_t * next_in;
   ucos_msg_fifo_entry_t * next_out;
}ucos_msg_queue_t;

typedef struct
{
   ucos_object_entry_t   obj_entry;
   ucos_msg_queue_t    * ucos_msg_queue;
   char                * name;
}ucos_object_info_t;





/* G L O B A L  V A R I A B L E S ********************************************/
#if UCOS_NUM_SMALL_XBUFFERS != 0
extern SmallXBufferType     smallxBufferPool[UCOS_NUM_SMALL_XBUFFERS];
extern SmallXBufferType  *  nextFreeSmallxBuffer;
extern SmallXBufferType  *  smallxBufferPoolStart;
extern SmallXBufferType  *  smallxBufferPoolEnd;
#endif

#if UCOS_NUM_LARGE_XBUFFERS != 0
extern LargeXBufferType     largexBufferPool[UCOS_NUM_LARGE_XBUFFERS];
extern LargeXBufferType  *  nextFreeLargexBuffer;
extern LargeXBufferType  *  largexBufferPoolStart;
extern LargeXBufferType  *  largexBufferPoolEnd;
#endif

extern ucos_msg_fifo_t       ucos_msg_fifo[SYSCFG_UCOS_OBJECT_PRIO_NUM_LEVELS];
extern ucos_msg_queue_t      ucos_msg_queue[SYSCFG_UCOS_OBJECT_PRIO_NUM_LEVELS];

/* I N T E R F A C E  F U N C T I O N S **************************************/

#endif    /* UCOS_IDEFS_H */
/* END OF FILE ***************************************************************/
