/*****************************************************************************

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
164 40 Kista                            http://www.nanoradio.se
SWEDEN

Module Description :
==================
This module is part of the t_btit unit. It performs bla, bla and bla.

*****************************************************************************/
#include "sysdef.h" /* Must be first include */
#include "ucos.h"
#include "application.h"
#include "hmg.h"
#include "hmg_traffic.h"
#include "hmg_ps.h"
#if (DE_NETWORK_SUPPORT & CFG_NETWORK_AMP)
   #include "hmg_pal.h"
#endif


/*****************************************************************************
T E M P O R A R Y   T E S T V A R I A B L E S
*****************************************************************************/

/*****************************************************************************
C O N S T A N T S / M A C R O S
*****************************************************************************/
#define OBJID_MYSELF    SYSDEF_OBJID_APP
#define INTSIG_START    0xF0


/*****************************************************************************
L O C A L   D A T A T Y P E S
*****************************************************************************/

/*****************************************************************************
L O C A L   F U N C T I O N   P R O T O T Y P E S
*****************************************************************************/
static void APP_entry(ucos_msg_id_t signal, ucos_msg_param_t inData);
static void APP_init(void);
static void APP_startUp(void);

/*****************************************************************************
 M O D U L E   V A R I A B L E S
*****************************************************************************/

/*********************/
/* Error class table */
/*********************/

/*****************************************************************************
G L O B A L   C O N S T A N T S / V A R I A B L E S
*****************************************************************************/

/* 
   There is one external dependency for UCOS to resolve in runtime,
   and that is the number of objects (that is tasks) defined in the 
   custom application.
 */
uint16_t NR_UCOS_OBJECTS = SYSDEF_MAX_NO_OBJECTS;

/*****************************************************************************
G L O B A L   F U N C T I O N S
*****************************************************************************/

/*-----------------------------------------------------------------------------
A little more thorough description than was provided for global
functions, as this is the only place to put descriptions for local functions.

Returns: Describe return value here.
-----------------------------------------------------------------------------*/
int app_init()
{
      ucos_init();
      ucos_startup();
               
      /* Register all objects */
      ucos_register_object(SYSDEF_OBJID_APP,
                          SYSCFG_UCOS_OBJECT_PRIO_LOW,
                          (ucos_object_entry_t)APP_entry,
                          "APP");
   
      /*******************************************************/
      /* Initialize all objects. NOTE: the order is of great */
      /* importance and must not be changed                  */
      /*******************************************************/
         
      APP_init();
      HMG_init_ps();
      HMG_init_traffic();
#if (DE_NETWORK_SUPPORT & CFG_NETWORK_AMP)
      HMG_init_pal();
#endif
   
      /*******************************************************/
      /* Now startup all objects.NOTE: the order is of great */
      /* importance and must not be changed                  */
      /*******************************************************/
   
      APP_startUp();
      HMG_startUp_ps();
      HMG_startUp_traffic();
#if (DE_NETWORK_SUPPORT & CFG_NETWORK_AMP)
      HMG_startUp_pal();
#endif
   
      return 0;
}

/*****************************************************************************
L O C A L    F U N C T I O N S
*****************************************************************************/
static void APP_init(void)
{
}

static void APP_startUp(void)
{
   ucos_send_msg(INTSIG_START, OBJID_MYSELF, DUMMY);
}

static void APP_entry(ucos_msg_id_t msg, ucos_msg_param_t param)
{
   switch(msg)
   {
      case INTSIG_START:
      {
      }
      break;

      case SIG_UCOS_TIMEOUT:
      break;

      default:
         break;
   }
   // S_BUF_free_all((S_BUF_BufType)inData);
}

/******************************* END OF FILE ********************************/
