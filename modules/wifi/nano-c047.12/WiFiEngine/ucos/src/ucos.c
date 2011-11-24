/**************************************************************************/
/* Copyright Nanoradio AB 2004                                            */
/*                                                                        */
/* Module Name   : ucos.c                                                 */
/* Revision      : PA1                                                    */
/*                                                                        */
/*                                                                        */
/*                                                                        */
/*------------------------------------------------------------------------*/
/*                                                                        */
/* Module Description :                                                   */
/* ==================                                                     */
/*                                                                        */
/* This module bla, bla bla                                               */ 
 /*                                                                        */
/* Restrictions/special remarks:                                          */
/* ============================                                           */
/*                                                                        */
/*                                                                        */
/* Revision History:                                                      */
/* ====================================================================   */
/*                                                                        */
/* Rev/Date/By                                                            */
/* Purpose     : Module created                                           */
/* Solution    : N/A                                                      */
/*                                                                        */
/* --------------------------------------------------------------------   */
/**************************************************************************/
#include "sysdef.h"
#include "ucos_defs.h"
#include "ucos_idefs.h"
#include "ucos_port.h"
#ifndef WIFI_ENGINE
#include "scb.h"
#include "uheap.h"
#ifdef CPU_LOAD_PIN
#include "gpio_def.h"
#endif /* CPU_LOAD_PIN */
#include "wdg_hw.h"
#include "logger.h"
#else
#define LOGG_OS_TASK(x)
#endif /* WIFI_ENGINE */
/**********************************************/
/* T E M P O R A Y  T E S T V A R I A B L E S */
/**********************************************/



/*******************************************************************************
C O N S T A N T S / M A C R O S
*******************************************************************************/
#define OBJID_MYSELF       SYSDEF_OBJID_UCOS
#define HIGHPRI_SIGNAL_QUEUE_SIZE 16

/*********************************************/
/* E X T E R N A L   D E C L A R A T I O N S */
/*********************************************/
#ifdef WIFI_ENGINE
#define SCB_error(_a,_b)
#endif /* WIFI_ENGINE */


/*********************************************/
/* C O N S T A N T   D E C L A R A T I O N S */
/*********************************************/

/* Flags in buffer status field */
#define BUFMARK_ALLOCATED 0xCDAB




/**********************************/
/* SW timer controlling constants */
/**********************************/
#ifndef UCOS_MAX_NUM_TIMERS
   #define UCOS_MAX_NUM_TIMERS 10
#endif






/***********************************/
/* L O C A L     T Y P E D E F ' S */
/***********************************/







/*-------------------------*/
/* Timer related typedef's */
/*-------------------------*/
typedef struct timer_entry_tag
{
   ucos_timer_id_t          id;
   SYSDEF_ObjectType        obj_id;
   uint32_t                 expiration_time;
   struct timer_entry_tag * next;
}timer_entry_t;

typedef struct
{
   timer_entry_t * free;
   timer_entry_t * next_to_expire;
}timer_list_t;




typedef struct
{
   uint32_t num_alloc;
   uint32_t num_free;
   uint32_t accum_alloc_size;
   uint32_t accum_free_size;
}ucos_dyn_mem_ststistics_t;


typedef struct
{
   ucos_dyn_mem_ststistics_t dyn_mem;
}ucos_ststistics_t;







/*******************************************************/
/*  L O C A L   F U N C T I O N   P R O T O T Y P E S  */
/*******************************************************/
#ifndef SMALL_UCOS
static void initBufferPool(void);
#endif
void        handleTickTimeout(void);
static void ucos_init_timer(void);
static void msg_drain(ucos_msg_id_t        msg,
                      ucos_msg_param_t   param);
static void invalid_msg_destination(ucos_msg_id_t        msg,
                                    ucos_msg_param_t   param);
static bool_t check_timers_on_msg_queue(SYSDEF_ObjectType dest,
                                        ucos_timer_id_t   id,
                                        uint8_t           kill);

/***********************************/
/* M O D U L E   V A R I A B L E S */
/***********************************/
static bool_t  idle_enabled;

static ucos_object_info_t    object_info[SYSDEF_NUM_OBJECTS];



/**********************/
/* SW timer variables */
/**********************/
static timer_list_t            timer_list;
static timer_entry_t           timer_heap[SYSCFG_UCOS_NUM_TIMERS];


static ucos_ststistics_t ucos_statistics;


/*****************************************************/
/*         C O D E   S T A R T S   H E R E           */
/*****************************************************/


/*****************************************************************************
L O C A L    F U N C T I O N S
*****************************************************************************/


/**************************************************************************/
/* ucos_init Function Description:                                         */
/* =============================                                          */
/*                                                                        */
/* Initialises UCOS.                                                      */
/*                                                                        */
/*                                                                        */
/* Input parameters:                                                      */
/* =================                                                      */
/* None                                                                   */
/*                                                                        */
/*                                                                        */
/* Otput:                                                                 */
/* ======                                                                 */
/* None                                                                   */
/*                                                                        */
/*                                                                        */
/* Returns:                                                               */
/* =======                                                                */
/* Always UCOS_RC_OK.                                                      */
/*                                                                        */
/*                                                                        */
/* Restrictions/Special marks                                             */
/* ==========================                                             */
/* Must be called before any UCOS services are requested.                  */
/**************************************************************************/
ucos_rc_t ucos_init(void)
{
   int i,j;

#ifndef SMALL_UCOS
   initBufferPool();
#endif

   for (i=0; i<NR_UCOS_OBJECTS; i++)
   {
      object_info[i].name      = NULL;
      object_info[i].ucos_msg_queue = &ucos_msg_queue[SYSCFG_UCOS_OBJECT_PRIO_NUM_LEVELS-1];
      object_info[i].obj_entry = invalid_msg_destination;
   }

   for (i=0;i<SYSCFG_UCOS_OBJECT_PRIO_NUM_LEVELS;i++)
   {
      ucos_msg_fifo_entry_t * f_entry;

      ucos_msg_queue[i].empty    = TRUE;
      f_entry = &ucos_msg_fifo[i][0];
      ucos_msg_queue[i].next_in  = ucos_msg_queue[i].next_out = f_entry;
      for(j=0;j<(SYSCFG_UCOS_MSGFIFO_SIZE-1);j++,f_entry++)
      {
         f_entry->next = &f_entry[1];
      }
      f_entry->next = &ucos_msg_fifo[i][0];
   }
   ucos_init_timer();


   MEMSET(&ucos_statistics, 0x00, sizeof(ucos_statistics));
#ifdef POWER_MEASUREMENT
   idle_enabled = TRUE;   
#else
	idle_enabled = FALSE;
#endif
   return UCOS_RC_OK;
} /* End ucos_init */


void ucos_re_init(void)
{
   int i,j;
   for (i=0;i<SYSCFG_UCOS_OBJECT_PRIO_NUM_LEVELS;i++)
   {
      ucos_msg_fifo_entry_t * f_entry;

#ifndef SMALL_UCOS
      initBufferPool();
#endif
      ucos_msg_queue[i].empty    = TRUE;
      f_entry = &ucos_msg_fifo[i][0];
      ucos_msg_queue[i].next_in  = ucos_msg_queue[i].next_out = f_entry;
      for(j=0;j<(SYSCFG_UCOS_MSGFIFO_SIZE-1);j++,f_entry++)
      {
         f_entry->next = &f_entry[1];
      }
      f_entry->next = &ucos_msg_fifo[i][0];
   }
}

/**************************************************************************/
/* ucos_startup Function Description:                                      */
/* ================================                                       */
/*                                                                        */
/* This function does nothing. Present as platform expects it.            */
/*                                                                        */
/* Input parameters:                                                      */
/* ================                                                       */
/* None                                                                   */
/*                                                                        */
/*                                                                        */
/* Otput:                                                                 */
/* =====                                                                  */
/* N/A                                                                    */
/*                                                                        */
/*                                                                        */
/* Returns:                                                               */
/* =======                                                                */
/* void                                                                   */
/*                                                                        */
/*                                                                        */
/* Restrictions/Special remarks:                                          */
/* ============================                                           */
/* N/A                                                                    */
/**************************************************************************/
void ucos_startup(void)
{
#ifdef CPU_LOAD_PIN
   GPIO_PIN_DIRECTION_SET_OUTPUT(CPU_LOAD_PIN);
#endif
   UCOS_PORT_SETUP_TIMER;
} /* End ucos_startup */ 


void ucos_idle_enable(void)
{
   idle_enabled = TRUE;
}

void ucos_idle_disable(void)
{
   idle_enabled = FALSE;
}


void ucos_register_object(SYSDEF_ObjectType          object,
                          syscfg_ucos_object_prio_t  prio,
                          ucos_object_entry_t        entry,
                          char                     * name)
{
    if (
         (object < NR_UCOS_OBJECTS) &&
         (prio < SYSCFG_UCOS_OBJECT_PRIO_NUM_LEVELS)
      )
   {
      object_info[object].obj_entry = entry;
      object_info[object].ucos_msg_queue = &ucos_msg_queue[prio];
      object_info[object].name      = name;
   }
   else
   {
      SCB_error(OBJID_MYSELF, UCOS_RC_OBJECT_REGISTRATION_ERROR);
   }
}


void ucos_executive(ucos_mode_t mode)
{
   SYSIO_USING_MONITOR;

   for(;;)
   {
      int           i;
      ucos_msg_queue_t * m_queue;
      bool_t        no_messages;

      no_messages = TRUE;

      SYSIO_ENTER_MONITOR;

      for(i=0,m_queue=&ucos_msg_queue[0];i<SYSCFG_UCOS_OBJECT_PRIO_NUM_LEVELS;i++,m_queue++)
      {
         if (!m_queue->empty)
         {
            ucos_msg_fifo_entry_t * f_entry;

            SYSIO_EXIT_MONITOR;

#ifdef CPU_LOAD_PIN
            GPIO_PIN_SET(CPU_LOAD_PIN);
#endif
            no_messages = FALSE;
            f_entry     = m_queue->next_out;
            LOGG_OS_TASK(f_entry->obj_id);
            f_entry->obj_entry(f_entry->msg, f_entry->param);
            LOGG_OS_TASK(SYSDEF_OBJID_NOOBJECT);

            SYSIO_ENTER_MONITOR;

            m_queue->next_out = m_queue->next_out->next;

            if (m_queue->next_out == m_queue->next_in)
            {
               m_queue->empty = TRUE;
            }
            break;
         }
      }
#ifdef WDG_ACTIVE
      {
         /* Restart watchdog */
         WDG_PRELOAD;
      }
#endif
      if (mode == UCOS_MODE_FOREVER)
      {
         if (no_messages)
         {
#ifdef CPU_LOAD_PIN
            GPIO_PIN_CLEAR(CPU_LOAD_PIN);
#endif
            if (idle_enabled)
            {
               UCOS_PORT_OS_IDLE;
            }
         }
         SYSIO_EXIT_MONITOR;
      }
      else if (mode == UCOS_MODE_ONE_SHOT)
      {
         break;
      }
      else  /* UCOS_MODE_UNTIL_EMPTY */
      {
         if (no_messages)
         {
#ifdef CPU_LOAD_PIN
            GPIO_PIN_CLEAR(CPU_LOAD_PIN);
#endif
            break;
         }
         else
         {
            SYSIO_EXIT_MONITOR;
         }
      }
   }
   SYSIO_EXIT_MONITOR;
}  /* End UCOS_executive */


void ucos_send_msg(ucos_msg_id_t     msg,
                   SYSDEF_ObjectType dest,
                   ucos_msg_param_t  param)
{
   SYSIO_USING_MONITOR;
   if (dest != SYSDEF_OBJID_NOOBJECT)
   {
      if (dest < NR_UCOS_OBJECTS)
      {
         ucos_msg_queue_t * m_queue;
         ucos_object_info_t * obj_info;

         obj_info = &object_info[dest];
         m_queue = obj_info->ucos_msg_queue;

         SYSIO_ENTER_MONITOR;

         if (
               (m_queue->empty) ||
               (m_queue->next_in != m_queue->next_out)
            )
         {
            ucos_msg_fifo_entry_t * m_entry;
            
            m_entry          = m_queue->next_in;
            m_queue->next_in = m_queue->next_in->next;


            m_queue->empty      = FALSE;
            m_entry->msg        = msg;
            m_entry->param      = param;
            m_entry->obj_id     = dest;
            m_entry->obj_entry = obj_info->obj_entry;
            
            SYSIO_EXIT_MONITOR;
         }
         else
         {
            SYSIO_EXIT_MONITOR;
            SCB_error(OBJID_MYSELF, UCOS_RC_MESSAGE_QUEUE_EXHAUSTED);
         }
      }
      else
      {
         SCB_error(OBJID_MYSELF, UCOS_RC_INVALID_DESTINATION);
      }
   }
}


void ucos_kill_msg(SYSDEF_ObjectType dest,
                   ucos_msg_id_t     msg)
{
   if (dest < NR_UCOS_OBJECTS)
   {
      ucos_object_info_t       * obj_info;
      ucos_msg_queue_t         * m_queue;
      ucos_object_entry_t   obj_entry;
      
      obj_info  = &object_info[dest];
      m_queue   = obj_info->ucos_msg_queue;
      obj_entry = obj_info->obj_entry;

      if (!m_queue->empty)
      {
         ucos_msg_fifo_entry_t * this_entry;
         
         this_entry = m_queue->next_out;
         for(;;)
         {
            if (
                  (this_entry->obj_entry == obj_entry) &&
                  (this_entry->msg       == msg)
               )
            {
               this_entry->obj_entry = msg_drain;
            }
            this_entry = this_entry->next;
            if (this_entry == m_queue->next_in)
            {
               break;
            }
         }
      }
   }
   else
   {
      SCB_error(OBJID_MYSELF, UCOS_RC_MSG_KILL_ERROR);
   }
}



/*********************************************************/
/*   B U F F E R    H A N D L I N G    F U N C T I O N S */  
/*********************************************************/





   
#ifndef SMALL_UCOS
/**************************************************************************/
/* initBufferPool Function Description:                                   */
/* ===================================                                    */
/*                                                                        */
/* This function performs all necessary action for initiating and         */
/* starting the dynamic memory handler. For debugging purposes, each of   */
/* the buffers are filled with a pattern (0x66 - 0x99) to make it easy    */
/* to find the pools in debugger.                                         */
/*                                                                        */
/*                                                                        */
/* Input parameters:                                                      */
/* =================                                                      */
/* None                                                                   */
/*                                                                        */
/*                                                                        */
/* Otput:                                                                 */
/* ======                                                                 */
/* N/A                                                                    */
/*                                                                        */
/*                                                                        */
/*                                                                        */
/* Returns:                                                               */
/* =======                                                                */
/* void                                                                   */
/*                                                                        */
/*                                                                        */
/* Restrictions/Special remarks                                           */
/* ============================                                           */
/* None                                                                   */
/**************************************************************************/
void initBufferPool(void)
{  
   char              * tmpPtr;        
   uint16_t i;
#ifndef UCOS_SKIP_FILL_BUFFERS
   uint16_t j;
#endif
   SYSIO_USING_MONITOR;
   SYSIO_ENTER_MONITOR;

   #if UCOS_NUM_SMALL_XBUFFERS != 0

   smallxBufferPoolStart = nextFreeSmallxBuffer = &smallxBufferPool[0];
   
   smallxBufferPoolEnd = &smallxBufferPool[UCOS_NUM_SMALL_XBUFFERS];
   
   for (i=0;i<UCOS_NUM_SMALL_XBUFFERS;i++)
   {
      smallxBufferPool[i].next = &smallxBufferPool[i+1];
      tmpPtr = &smallxBufferPool[i].buffer[0];
#ifndef UCOS_SKIP_FILL_BUFFERS
      for (j=0;j<sizeof(smallxBufferPool[0].buffer);j++)
      {
	 tmpPtr[j] = (uint8_t)0x88;
      }
#endif
   }
   smallxBufferPool[UCOS_NUM_SMALL_XBUFFERS-1].next = NULL;
   
#endif
   
#if UCOS_NUM_LARGE_XBUFFERS != 0
   largexBufferPoolStart = nextFreeLargexBuffer  = &largexBufferPool[0];
   
   largexBufferPoolEnd = &largexBufferPool[UCOS_NUM_LARGE_XBUFFERS];
   
   for (i=0;i<UCOS_NUM_LARGE_XBUFFERS;i++)
   {
      largexBufferPool[i].next = &largexBufferPool[i+1];
      tmpPtr = &largexBufferPool[i].buffer[0];
#ifndef UCOS_SKIP_FILL_BUFFERS
      for (j=0;j<sizeof(largexBufferPool[0].buffer);j++)
      {
	 tmpPtr[j] = (uint8_t)0x99;
      }
#endif
   }
   largexBufferPool[UCOS_NUM_LARGE_XBUFFERS-1].next = NULL;
   
#endif
   
#ifndef SMALL_UCOS
   HEAP_init();
#endif
   SYSIO_EXIT_MONITOR;
}  /* End UCOS_initBufferPool */





/**************************************************************************/
/* UCOS_xintmalloc Function Description:                                  */
/* ================================                                       */
/*                                                                        */
/* UCOS_xintmalloc is used to allocate a dynamic buffer in  memory space. */
/* If the requested buffer size is less than UCOS_SMALL_XBUFFER_SIZE, a   */
/* small buffer is retunred, otherwise a large buffer is returned.        */
/*                                                                        */
/*                                                                        */
/*                                                                        */
/*                                                                        */
/*                                                                        */
/* Input parameters:                                                      */
/* =================                                                      */
/*                                                                        */
/* size        : Requested size of the buffer                             */
/*                                                                        */
/*                                                                        */
/* Otput:                                                                 */
/* ======                                                                 */
/* UCOS_RC_OK               : The buffer was successfully allocated        */
/* UCOS_RC_OUT_OF_BUFFERS        : Out of memory                                */
/* UCOS_RC_REQUESTED_BUFFER_TOO_LARGE : Guess what?? Severe error,         */
/*                                     System should be stopped!          */
/*                                     stopped                            */
/* UCOS_RC_CORRUPTED_MEMORY : A status field inside the memory pool had    */
/*                           a status differing from FREE or ALLOCATED.   */
/*                           This is a severe error and result in system  */
/*                           stop!                                        */
/*                                                                        */
/*                                                                        */
/*                                                                        */
/*                                                                        */
/* Returns:                                                               */
/* =======                                                                */
/* buffer      : A pointer to the allocated buffer. NULL if allocation is */
/*               not successful.                                          */
/*                                                                        */
/*                                                                        */
/* Restrictions/Special remarks                                           */
/* ============================                                           */
/* The maximum size for a buffer is defined by UCOS_LARGE_XBUFFER_SIZE.    */
/* For best functionality, not bigger buffers than actually needed should */
/* be requested.                                                          */
/*                                                                        */
/* The total number of small and large buffers ar defined by              */
/* UCOS_NUM_SMALL_XBUFFERS and UCOS_NUM_LARGE_XBUFFERS.                     */
/*                                                                        */
/**************************************************************************/
#ifdef UCOS_XMALLOC_DEBUG
UCOS_XBufType UCOS_xmalloc_dbg(char* file, int line, uint16_t reqSize)
#else
UCOS_XBufType UCOS_xmalloc(uint16_t reqSize)
#endif
{
   ucos_rc_t      rc;
   XBufRefType  * tmp;
   uint8_t dummy;
   UCOS_XBufType        retBuf;
   SYSIO_USING_MONITOR;
   SYSIO_ENTER_MONITOR;
   
   rc = UCOS_RC_OUT_OF_BUFFERS;

#if UCOS_NUM_SMALL_XBUFFERS != 0
   if (reqSize <= sizeof(smallxBufferPool[0].buffer))
   {
      if (nextFreeSmallxBuffer != NULL)
      {
         rc                   = UCOS_RC_OK;
         tmp                  = (XBufRefType  *)nextFreeSmallxBuffer;
         nextFreeSmallxBuffer = (SmallXBufferType  *)tmp->next;
      }
   }
 #if UCOS_NUM_LARGE_XBUFFERS != 0
   else
 #endif
#endif
   
#if UCOS_NUM_LARGE_XBUFFERS != 0
   if (reqSize <= sizeof(largexBufferPool[0].buffer))
   {
      if (nextFreeLargexBuffer != NULL)
      {
         rc                   = UCOS_RC_OK;
         tmp                  = (XBufRefType  *)nextFreeLargexBuffer;
         nextFreeLargexBuffer = (LargeXBufferType  *)tmp->next;
      }
   }
#endif

   else
   {
      rc  = UCOS_RC_REQUESTED_BUFFER_TOO_LARGE;
      tmp = NULL;
   }
   
   if (rc == UCOS_RC_OK)
   {
      tmp->next = (uint8_t *)BUFMARK_ALLOCATED;
      retBuf    = (UCOS_XBufType)&tmp->buffer;
#ifdef UCOS_XMALLOC_DEBUG
      tmp->file = file;
      tmp->line = line;
#endif
   }
   else
   {
      dummy     = 0;
      SCB_error(SYSDEF_OBJID_UCOS,rc);
      retBuf    = NULL;
   }
   SYSIO_EXIT_MONITOR;

   return retBuf;
}  /* End UCOS_xmalloc */


/**************************************************************************/
/* UCOS_intMalloc Function Description:                                       */
/* ================================                                       */
/*                                                                        */
/* UCOS_intMalloc is a wrapper around the standard alloc. It us used for      */
/* detecting out-of-memory conditions.                                    */
/*                                                                        */
/*                                                                        */
/*                                                                        */
/*                                                                        */
/*                                                                        */
/* Input parameters:                                                      */
/* =================                                                      */
/*                                                                        */
/* size        : Requested size of the buffer                             */
/*                                                                        */
/*                                                                        */
/* Output:                                                                */
/* ======                                                                 */
/*                                                                        */
/*                                                                        */
/*                                                                        */
/*                                                                        */
/* Returns:                                                               */
/* =======                                                                */
/* buffer      : A pointer to the allocated buffer. NULL if allocation is */
/*               not successful.                                          */
/*                                                                        */
/*                                                                        */
/* Restrictions/Special remarks                                           */
/* ============================                                           */
/*                                                                        */
/**************************************************************************/
void *UCOS_intMalloc(uint16_t reqSize)
{
   void * p;
   SYSIO_USING_MONITOR;
   SYSIO_ENTER_MONITOR;
 
   p = UCOS_PORT_MALLOC( reqSize );

   if(!p ) 
   {
      SCB_error( SYSDEF_OBJID_UCOS, UCOS_RC_OUT_OF_CORE );
   }
   ucos_statistics.dyn_mem.accum_alloc_size += HEAP_get_block_size(p);
   ucos_statistics.dyn_mem.num_alloc++;
   SYSIO_EXIT_MONITOR;
   return p;
}  /* End UCOS_intMalloc */



void UCOS_intFree( void *ptr )
{
   SYSIO_USING_MONITOR;
   SYSIO_ENTER_MONITOR;
   ucos_statistics.dyn_mem.accum_free_size += HEAP_get_block_size(ptr);
   ucos_statistics.dyn_mem.num_free++;
   UCOS_PORT_FREE( ptr );
   SYSIO_EXIT_MONITOR;
}



/**************************************************************************/
/* UCOS_xfree Function Description:                                        */
/* =============================                                          */
/*                                                                        */
/* UCOS_free is used to free a previously dynamically allocated buffer     */
/* in  memory space.                                                 */
/*                                                                        */
/* Input parameters:                                                      */
/* =================                                                      */
/*                                                                        */
/* buffer      : A pointer to the buffer to free.                         */
/*                                                                        */
/*                                                                        */
/* Otput:                                                                 */
/* ======                                                                 */
/* N/A                                                                    */
/*                                                                        */
/*                                                                        */
/*                                                                        */
/* Returns:                                                               */
/* =======                                                                */
/* UCOS_RC_OK                : The buffer was successfully released        */
/* UCOS_RC_BAD_BUFFER_STATUS : The buffer had a status differing from      */
/*                            ALLOCATED. This is severe and should cause  */
/*                            system to be stopped.                       */
/*                                                                        */
/* UCOS_RC_INVALID_BUFFER : The input pointer does not point ta a location */
/*                         within the memory pool. Severe and should      */
/*                         in system stop.                                */
/*                                                                        */
/*                                                                        */
/*                                                                        */
/* Restrictions/Special remarks                                           */
/* ============================                                           */
/* A NULL pointer as input will be treated as an invalid pointer and      */
/* result in returncode UCOS_RC_INVALID_BUFFER                             */
/*                                                                        */
/**************************************************************************/
ucos_rc_t UCOS_xfree(UCOS_XBufType buffer)
{
   XBufRefType  * tmp;
   ucos_rc_t rc;
   SYSIO_USING_MONITOR;
   SYSIO_ENTER_MONITOR;
   
   rc  = UCOS_RC_OK;
   tmp = NULL;
   tmp = (XBufRefType  *)((uintptr_t)buffer - ((char*)&tmp->buffer - (char*)&tmp->next));
   if (tmp->next != (uint8_t *)BUFMARK_ALLOCATED)
   {
      rc = UCOS_RC_BAD_BUFFER_STATUS;
   }
   else
   {
      #if UCOS_NUM_SMALL_XBUFFERS != 0
         if (
               ( (uintptr_t)buffer > (uintptr_t)smallxBufferPoolStart) &&
               ( (uintptr_t)buffer < (uintptr_t)smallxBufferPoolEnd  )
            )
         {
            tmp->next            = (uint8_t *)nextFreeSmallxBuffer;
            nextFreeSmallxBuffer = (SmallXBufferType  *)tmp;
         }
         #if UCOS_NUM_LARGE_XBUFFERS != 0
         else
         #endif
      #endif
   
      #if UCOS_NUM_LARGE_XBUFFERS != 0
         if (
               ( (uintptr_t)buffer > (uintptr_t)largexBufferPoolStart) &&
               ( (uintptr_t)buffer < (uintptr_t)largexBufferPoolEnd  )
            )
         {
            tmp->next            = (uint8_t *)nextFreeLargexBuffer;
            nextFreeLargexBuffer = (LargeXBufferType  *)tmp;
         }
      #endif
   
      else
      { 
         rc = UCOS_RC_INVALID_BUFFER;
      }
   }
   if (rc != UCOS_RC_OK)
   {
      SCB_error(SYSDEF_OBJID_UCOS,rc);
   }
   SYSIO_EXIT_MONITOR;
   return rc;
}  /* End UCOS_xfree */
#endif /* SMALL_UCOS */



/*********************************************************/
/*   T I M E R      H A N D L I N G    F U N C T I O N S */  
/*********************************************************/


static void ucos_init_timer(void)
{  
   int             i;
   timer_entry_t * t_entry;

   MEMSET(&timer_heap[0], 0x00, sizeof(timer_heap));
   for(i=0,t_entry=&timer_heap[0];
       i<(SYSCFG_UCOS_NUM_TIMERS-1);
       i++,t_entry++)
   {
      t_entry->next = &t_entry[1];
   }
   t_entry->next             = NULL;
   timer_list.free           = &timer_heap[0];
   timer_list.next_to_expire = NULL;
}  /* End ucos_init_timer */



void ucos_get_timer(SYSDEF_ObjectType object, ucos_timeout_t ticks, ucos_timer_id_t id)
{
   timer_entry_t * scan_entry;
   timer_entry_t * prev_entry;
   timer_entry_t * new_entry;
   
   uint32_t        expiration_time;
   uint32_t        norm;
   uint32_t        normalized_expiration_time;
   SYSIO_USING_MONITOR;

   SYSIO_ENTER_MONITOR;
   expiration_time = UCOS_PORT_GET_CURRENT_TIME + ticks;
   if (timer_list.free == NULL)
   {
      SCB_error(OBJID_MYSELF, UCOS_RC_OUT_OF_TIMERS);
      goto error;
   }

   if ((scan_entry = timer_list.next_to_expire) == NULL)
   {
      norm = 0;
   }
   else
   {
      norm = scan_entry->expiration_time;
   }

   while(scan_entry != NULL)
   {
      if (
            (scan_entry->id     == id) &&
            (scan_entry->obj_id == object)
         )
      {
         SCB_error(OBJID_MYSELF, UCOS_RC_DUPLICATE_TIMER);
         SYSIO_EXIT_MONITOR;
         goto error;
      }
      scan_entry = scan_entry->next;
   }
   
   new_entry                  = timer_list.free;
   timer_list.free            = timer_list.free->next;
   new_entry->id              = id;
   new_entry->obj_id          = object;
   new_entry->expiration_time = expiration_time;
   normalized_expiration_time = expiration_time-norm;

   scan_entry = timer_list.next_to_expire;
   prev_entry = NULL;

   while(scan_entry != NULL)
   {
      if ((int32_t)normalized_expiration_time <= (int32_t)(scan_entry->expiration_time-norm))
      {
         break;
      }
      prev_entry = scan_entry;
      scan_entry = scan_entry->next;
   }

   new_entry->next  = scan_entry;
   if (prev_entry == NULL)
   {
      timer_list.next_to_expire  = new_entry;
      UCOS_PORT_START_TIMER(expiration_time, (uintptr_t)new_entry);
   }
   else
   {
      prev_entry->next = new_entry;
   }
   
   SYSIO_EXIT_MONITOR;
error:
   return;
}  /* End ucos_get_timer */



void ucos_kill_timer(SYSDEF_ObjectType object, ucos_timer_id_t id)
{
   timer_entry_t * scan_entry;
   timer_entry_t * prev_entry;
   SYSIO_USING_MONITOR;

   SYSIO_ENTER_MONITOR;

   scan_entry = timer_list.next_to_expire;
   prev_entry = NULL;

   while(scan_entry != NULL)
   {
      if (
            (scan_entry->id     == id) &&
            (scan_entry->obj_id == object)
         )
      {
         break;
      }
      prev_entry = scan_entry;
      scan_entry = scan_entry->next;
   }
   
   if (scan_entry != NULL)
   {
      if (prev_entry == NULL)
      {
         timer_list.next_to_expire = scan_entry->next;
         if (timer_list.next_to_expire == NULL)
         {
            UCOS_PORT_STOP_TIMER;
         }
         else
         {
            UCOS_PORT_START_TIMER(timer_list.next_to_expire->expiration_time,
                                  (uintptr_t)timer_list.next_to_expire);
         }
      }
      else
      {
         prev_entry->next = scan_entry->next;
      }
      scan_entry->next = timer_list.free;
      timer_list.free = scan_entry;
   }
   else
   {
      check_timers_on_msg_queue(object,id,TRUE);
   }

   SYSIO_EXIT_MONITOR;
}  /* End ucos_kill_timer */


void ucos_timer_timeout(uintptr_t trans_id)
{
   timer_entry_t * exp_entry;

   exp_entry = timer_list.next_to_expire;

   if (
         (exp_entry            == NULL) ||
         ((uintptr_t)exp_entry != trans_id)
      )
   {
      SCB_error(OBJID_MYSELF, UCOS_RC_INVALID_TIMEOUT);
      goto error;
   }
   ucos_send_msg(SIG_UCOS_TIMEOUT,
                 exp_entry->obj_id,
                 exp_entry->id);

   timer_list.next_to_expire = exp_entry->next;
   if (timer_list.next_to_expire != NULL)
   {
      UCOS_PORT_START_TIMER(timer_list.next_to_expire->expiration_time,
                            (uintptr_t)timer_list.next_to_expire);
   }
   exp_entry->next = timer_list.free;
   timer_list.free = exp_entry;
error:
   return;
}  /* End ucos_kill_timer */



static bool_t check_timers_on_msg_queue(SYSDEF_ObjectType dest,
                                        ucos_timer_id_t   id,
                                        uint8_t           kill)
{
   ucos_object_info_t       * obj_info;
   ucos_msg_queue_t         * m_queue;
   ucos_object_entry_t   obj_entry;
   bool_t                rc;

   rc = FALSE;
   obj_info  = &object_info[dest];
   m_queue   = obj_info->ucos_msg_queue;
   obj_entry = obj_info->obj_entry;

   if (!m_queue->empty)
   {
      ucos_msg_fifo_entry_t * this_entry;
      
      this_entry = m_queue->next_out;
      for(;;)
      {
         if (
               (this_entry->obj_entry == obj_entry)        &&
               (this_entry->msg       == SIG_UCOS_TIMEOUT) &&
               (this_entry->param     == id)
            )
         {
            if (kill)
            {
               this_entry->obj_entry = msg_drain;
            }
            rc = TRUE;
            break;   /* Can only be one timer */
         }
         this_entry = this_entry->next;
         if (this_entry == m_queue->next_in)
         {
            break;
         }
      }
   }
   return rc;
}

         /********************/
         /*                  */
         /*  ERROR HANDLERS  */
         /*                  */
         /********************/


/**************************************************************************/
/* noObjectEntry Function Description:                                    */
/* ==================================                                     */
/* A signal sent to an unregistered object will land here.                */
/*                                                                        */
/*                                                                        */
/* Input parameters:                                                      */
/* ================                                                       */
/* Signal, data                                                           */
/*                                                                        */
/*                                                                        */
/*                                                                        */
/* Otput:                                                                 */
/* ======                                                                 */
/* Error always called.                                                   */
/*                                                                        */
/*                                                                        */
/* Returns:                                                               */
/* =======                                                                */
/* void                                                                   */
/*                                                                        */
/*                                                                        */
/* Restrictions/Special marks:                                            */
/* ==========================                                             */
/* N/A                                                                    */
/**************************************************************************/
static void msg_drain(ucos_msg_id_t        msg,
                      ucos_msg_param_t   param)
{
   UNUSED(msg);
   UNUSED(param);
}

static void invalid_msg_destination(ucos_msg_id_t        msg,
                                    ucos_msg_param_t   param)
{
   UNUSED(msg);
   UNUSED(param);
   SCB_error(OBJID_MYSELF, UCOS_RC_INVALID_DESTINATION);
}
