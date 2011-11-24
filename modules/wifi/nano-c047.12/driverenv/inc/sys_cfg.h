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
#ifndef SYS_CFG_H
#define SYS_CFG_H

#include "sys_cfg_common.h"


#define SUPPORT_STRUCTURE_COPY

// #define WIFI_DEBUG_ON
#define WIFI_ENGINE

#define WE_DLM_DYNAMIC

/* Enable WAPI support */
#define WAPI_SUPPORT

#define CONNECT_ON_PROP_SET

/* #define PRINT_PKT_HDR */
/* #define PRINT_TRANS_ID */
#undef WITH_LOST_SCAN_WORKAROUND

/* #define WITH_HW_PROFILING */
/* WiFiEngine build config */
/* #define WITH_PACKET_HISTORY */
#define PKT_LOG_LENGTH 64
#define PKT_LOG_PKT_SIZE 64

#define USE_PMKID
#define WITH_8021X_PORT
#define BUILD_RSN_IE
/* #define ENABLE_STATE_TRACE  */
/* #define ENABLE_TRACE_ALWAYS */

#ifndef LITTLEENDIAN
#define LITTLEENDIAN
#endif

/* #define USE_DCONFIRM_COMPLETION */
#define USE_NEW_AGE

#define SCANREQ_NONBLOCK  1 /* don't wait for active scan requests */
#define SCANREQ_NOWAIT    1 /* don't wait for scan to complete */

#endif   /* SYS_CFG_H */

