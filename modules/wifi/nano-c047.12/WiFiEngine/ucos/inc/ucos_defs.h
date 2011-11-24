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
#ifndef UCOS_DEFS_H
#define UCOS_DEFS_H

/* M A C R O S  **************************************************************/

/****************************************/
/* Dynamic buffer controlling constants */
/****************************************/
#ifndef UCOS_SMALL_XBUFFER_SIZE
   #define UCOS_SMALL_XBUFFER_SIZE 30
#endif

#ifndef UCOS_LARGE_XBUFFER_SIZE
   #define UCOS_LARGE_XBUFFER_SIZE 290
#endif

/*#define UCOS_NUM_SMALL_XBUFFERS 20*/
/*#define UCOS_NUM_LARGE_XBUFFERS 60*/

#ifndef UCOS_NUM_SMALL_XBUFFERS
   #define UCOS_NUM_SMALL_XBUFFERS 10
#endif

#ifndef UCOS_NUM_LARGE_XBUFFERS
   #define UCOS_NUM_LARGE_XBUFFERS 10
#endif


/***************************************************/
/* Constants to be used along with timer functions */
/***************************************************/
#define UCOS_SECOND          1000000
#define UCOS_MILLISECOND     1000
#define UCOS_MICROSECOND     1

/* E X P O R T E D  D A T A T Y P E S ****************************************/

typedef enum
{
   UCOS_RC_OK,
   UCOS_RC_MSG_KILL_ERROR,
   UCOS_RC_OBJECT_REGISTRATION_ERROR,
   UCOS_RC_OUT_OF_BUFFERS,
   UCOS_RC_REQUESTED_BUFFER_TOO_LARGE,
   UCOS_RC_INVALID_BUFFER,
   UCOS_RC_MESSAGE_QUEUE_EXHAUSTED,
   UCOS_RC_INVALID_DESTINATION,
   UCOS_RC_MAX_NO_OF_OBJECTS_EXCEEDED,
   UCOS_RC_BAD_BUFFER_STATUS,
   UCOS_RC_OUT_OF_TIMERS,
   UCOS_RC_DUPLICATE_TIMER,
   UCOS_RC_INVALID_TIMEOUT,
   UCOS_RC_OUT_OF_CORE,
   UCOS_RC_TOO_MANY_XBUFSTATE_SUBSCRIBERS,
   UCOS_RC_END_OF_BUFFER_ERROR
}ucos_rc_t;

typedef uint32_t   ucos_msg_id_t;
typedef uintptr_t ucos_msg_param_t;
typedef void      (*ucos_object_entry_t)(ucos_msg_id_t      msg,
                                         ucos_msg_param_t param);

typedef IDATA_CHAR * UCOS_BufType;

typedef char * UCOS_XBufType;

typedef uint32_t         ucos_timeout_t;
typedef ucos_msg_param_t ucos_timer_id_t;

typedef enum
{
   UCOS_MODE_FOREVER,
   UCOS_MODE_ONE_SHOT,
   UCOS_MODE_UNTIL_EMPTY
}ucos_mode_t;





/* G L O B A L  V A R I A B L E S ********************************************/

/* I N T E R F A C E  F U N C T I O N S **************************************/

#endif    /* UCOS_DEFS_H */
/* END OF FILE ***************************************************************/
