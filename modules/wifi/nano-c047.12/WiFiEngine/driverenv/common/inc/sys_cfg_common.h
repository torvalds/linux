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

*****************************************************************************/
#ifndef __sys_cfg_common_h__
#define __sys_cfg_common_h__ 1

#define SMALL_UCOS

/****************************************/
/* Object identities in CPU             */
/****************************************/
#define SYSDEF_OBJID_NOOBJECT                   0
#define SYSDEF_OBJID_UCOS                       1
#define SYSDEF_OBJID_APP                        2
#define SYSDEF_OBJID_HOST_MANAGER_PS            3
#define SYSDEF_OBJID_HOST_MANAGER_TRAFFIC       4
#define SYSDEF_OBJID_HOST_MANAGER_PAL           5

#define SYSDEF_OBJID_LAST                       5
#define SYSDEF_NUM_OBJECTS (SYSDEF_OBJID_LAST+1)
#define SYSDEF_MAX_NO_OBJECTS SYSDEF_NUM_OBJECTS

#define SIG_UCOS_TIMEOUT                 0x01


/***************************************/
/*  Common compiler flags and switches */
/***************************************/

/*Additional defines*/
typedef enum
{
   SYSCFG_UCOS_OBJECT_PRIO_HIGH = 0,
   SYSCFG_UCOS_OBJECT_PRIO_LOW,
   SYSCFG_UCOS_OBJECT_PRIO_NUM_LEVELS
}syscfg_ucos_object_prio_t;

#define SYSCFG_UCOS_MSGFIFO_SIZE 16
#define SYSCFG_UCOS_NUM_TIMERS   8
#define UCOS_SMALL_XBUFFER_SIZE 1
#define UCOS_NUM_SMALL_XBUFFERS 1
#define UCOS_LARGE_XBUFFER_SIZE 1
#define UCOS_NUM_LARGE_XBUFFERS 1

#endif /* __sys_cfg_common_h__ */

