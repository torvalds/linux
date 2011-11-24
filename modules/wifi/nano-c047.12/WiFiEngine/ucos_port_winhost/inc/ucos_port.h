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
#ifndef UCOS_PORT_H
#define UCOS_PORT_H
#include "driverenv.h"

/* M A C R O S  **************************************************************/
#define SYSCFG_TIMER_TICK 10

#define UCOS_PORT_SETUP_TIMER                      /* Nothing to do in host environment. */
#define UCOS_PORT_START_TIMER(_time, _trans_id)    
#define UCOS_PORT_STOP_TIMER                       
#define UCOS_PORT_GET_CURRENT_TIME
#define UCOS_PORT_OS_IDLE

#define UCOS_PORT_MALLOC(_size)                    DriverEnvironment_Malloc(_size)
#define UCOS_PORT_FREE(_ptr)                       DriverEnvironment_Free(_ptr)
        

/* E X P O R T E D  D A T A T Y P E S ****************************************/


/* G L O B A L  V A R I A B L E S ********************************************/

/* I N T E R F A C E  F U N C T I O N S **************************************/

#endif    /* UCOS_PORT_H */
/* END OF FILE ***************************************************************/
