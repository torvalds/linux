/*****************************************************************************

            Copyright (c) 2008 by Nanoradio AB

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

This file contains default configuration. The file is common for all platforms.
If you want to enable or disable a function for your platform you should edit
"env_config.h".


*****************************************************************************/

#ifndef ENV_CONFIG_ALL_H
#define ENV_CONFIG_ALL_H

/* Define configuration flags */
#define CFG_OFF                  0x0000
#define CFG_ON                   0x0001
#define CFG_NOT_INCLUDED         0x0000
#define CFG_INCLUDED             0x0001
#define CFG_SIMPLE               0
#define CFG_ADVANCED             1
#define CFG_USE_DYNAMIC_BUF      2
#define CFG_LOG_CMD52            2
#define CFG_AGGR_ALL             3
#define CFG_AGGR_SCAN_IND        4
#define CFG_AGGR_ALL_BUT_DATA    5
#define CFG_AGGR_ONLY_DATA_CFM   6
#define CFG_AGGR_LAST_MARK       7
#define CFG_MULTI_THREADED       8
#define CFG_SINGLE_THREADED      9
#define CFG_MEMORY               0x0001
#define CFG_FILE                 0x0002
#define CFG_SERIAL               0x0004
#define CFG_MIB                  0x0008
#define CFG_REGISTRY             0x0010
#define CFG_DEBUG_CONF           0x0020
#define CFG_NVMEM                0x0040
#define CFG_BINARY               0x0080
#define CFG_TEXT                 0x0100
#define CFG_STDOUT               0x0200
#define CFG_DYNAMIC_BUFFER       0x0400
#define CFG_NETWORK_BSS_STA      0x0001
#define CFG_NETWORK_BSS_AP       0x0002
#define CFG_NETWORK_IBSS         0x0004
#define CFG_NETWORK_AMP          0x0008



//Include the platform-specific config-file.
#include "env_config.h"

/*
* xx.yy DE_BUILTIN_SUPPLICANT (must not be left empty)
*
* Configures if the supplicant should be included in Nanoradio-driver or not
*
* Syntax:
*     DE_BUILTIN_SUPPLICANT  ( CFG_NOT_INCLUDED | CFG_INCLUDED )
*/
#ifndef DE_BUILTIN_SUPPLICANT
#define DE_BUILTIN_SUPPLICANT            CFG_NOT_INCLUDED
#endif

/*
* xx.yy DE_BUILTIN_WTE (must not be left empty)
*
* Configures if the WFA Test Engine API should be included in Nanoradio-driver or not
*
* Syntax:
*     DE_BUILTIN_SUPPLICANT  ( CFG_NOT_INCLUDED | CFG_INCLUDED )
*/
#ifndef DE_BUILTIN_WTE
#define DE_BUILTIN_WTE                   CFG_NOT_INCLUDED
#endif

/*
* xx.yy DE_WPA_ENT_SUPPORT (must not be left empty)
*
* Configures if enterprise modes should be included or not
* Pre-shared-key is always included
*
* Syntax:
*     DE_WPA_ENT_SUPPORT  ( CFG_NOT_INCLUDED | CFG_INCLUDED )
*/
#ifndef DE_WPA_ENT_SUPPORT
#define DE_WPA_ENT_SUPPORT            CFG_NOT_INCLUDED
#endif

/*
* xx.yy DE_BUILTIN_WAPI (must not be left empty)
*
* Configures if WiFi-Engine should include builtin support for WAPI.
* This option has no effect if DE_BUILTIN_SUPPLICANT is set to CFG_NOT_INCLUDED
* Activating this function should not have any other effect than slightly bigger
* code size.
*
* Syntax:
*     DE_BUILTIN_WAPI  ( CFG_NOT_INCLUDED | CFG_INCLUDED )
*/
#ifndef DE_BUILTIN_WAPI
#define DE_BUILTIN_WAPI            CFG_NOT_INCLUDED
#endif

/*
* xx.yy DE_TRACE_MODE (must not be left empty)
*
* Configures how driver traces are done
*
* Syntax:
*     DE_TRACE_MODE  ( CFG_OFF | CFG_MEMORY | CFG_FILE | CFG_SERIAL | CFG_STDOUT | CFG_DYNAMIC_BUFFER )
*/
#ifndef DE_TRACE_MODE
#define DE_TRACE_MODE                        CFG_OFF
#endif

/*
* xx.yy DE_COREDUMP_SUPPORT support (must not be left empty)
*
* Configures support for core dumps
*
* Syntax:
*     DE_COREDUMP_SUPPORT  ( CFG_INCLUDED | CFG_NOT_INCLUDED | CFG_USE_DYNAMIC_BUF )
*/
#ifndef DE_COREDUMP_SUPPORT
#define DE_COREDUMP_SUPPORT                  CFG_NOT_INCLUDED
#endif

/*
* xx.yy DE_COREDUMP_CACHE_SIZE
*
* Configures size of cache used for core dumps
*
* Syntax:
*     DE_COREDUMP_CACHE_SIZE  ( <integer> )
*/
#ifndef DE_COREDUMP_CACHE_SIZE
#define DE_COREDUMP_CACHE_SIZE               10000
#endif

/*
* xx.yy DE_COREDUMP_MAX_COUNT
*
* Configures maximum number of core dumps
*
* Syntax:
*     DE_COREDUMP_MAX_COUNT ( <integer> )
*/
#ifndef DE_COREDUMP_MAX_COUNT
#define DE_COREDUMP_MAX_COUNT                2
#endif


/*
* xx.yy DE_ENABLE_FILE_ACCESS support (must not be left empty)
*
* Configures support for file access for initial MIB settings,
* initiation of driver REGISTRY and for DEBUG_CONF support.
*
* Syntax:
*     DE_ENABLE_FILE_ACCESS  ( CFG_OFF | CFG_MIB, CFG_REGISTRY, CFG_DEBUG_CONF )
*/
#ifndef DE_ENABLE_FILE_ACCESS
#define DE_ENABLE_FILE_ACCESS                CFG_OFF
#endif


/*
* xx.yy DE_ENABLE_PRODUCTION_TEST_MODE support (must not be left empty)
*
* Configures support for core dumps
*
* Syntax:
*     DE_ENABLE_PRODUCTION_TEST_MODE  ( CFG_OFF | CFG_ON )
*/
#ifndef DE_ENABLE_PRODUCTION_TEST_MODE
#define DE_ENABLE_PRODUCTION_TEST_MODE       CFG_OFF
#endif


/*
* xx.yy DE_FIRMWARE_STORAGE
*
* Configures where fw is stored
*
* Syntax:
*     DE_FIRMWARE_STORAGE  ( CFG_NVMEM | CFG_FILE )
*/
#ifndef DE_FIRMWARE_STORAGE
#define DE_FIRMWARE_STORAGE                  CFG_NVMEM
#endif

/*
* xx.yy DE_FIRMWARE_SIZE
*
* Configures how much static memory is available for storing firmware
*
* Syntax:
*     DE_FIRMWARE_SIZE  ( <integer> )
*/
#ifndef DE_FIRMWARE_SIZE
#define DE_FIRMWARE_SIZE                     160000
#endif

/*
* xx.yy DE_BUILTIN_FIRMWARE (must not be left empty)
*
* Configures firmware storage options
*
* Syntax:
*     DE_BUILTIN_FIRMWARE  ( CFG_OFF | CFG_ON )
*/
#ifndef DE_BUILTIN_FIRMWARE
#define DE_BUILTIN_FIRMWARE                  CFG_OFF
#endif


/*
* xx.yy DE_REGISTRY_TYPE (must not be left empty)
*
* Configures registry storage
*
* Syntax:
*     DE_REGISTRY_TYPE  ( CFG_BINARY | CFG_TEXT )
*/
#ifndef DE_REGISTRY_TYPE
#define DE_REGISTRY_TYPE                     CFG_BINARY
#endif


/*
* xx.yy DE_MSG_MIN_SIZE
*
* Configures the minimum size of packets that target may send to WFE
* If a message is shorter padding will be added
*
* Syntax:
*     DE_MSG_MIN_SIZE  must be a multiple of DE_PACKET_ALIGN
*/
#ifndef DE_MSG_MIN_SIZE
#define DE_MSG_MIN_SIZE                      DE_PACKET_ALIGN*0
#endif


/*
* xx.yy DE_PACKET_ALIGN
*
* Configures how the first byte of a data packet should be aligned
* Needed by some OS like Windows CE/XP who will ignore unailgned packets
*
* This parameter must be selected from the set {0,2,4,8,16,32,64,128}
*
* Syntax:
*     DE_PACKET_ALIGN  0,2,4,8,16,32,64,128
*/
#ifndef DE_PACKET_ALIGN
#define DE_PACKET_ALIGN                      4
#endif


/*
* xx.yy DE_FIRMWARE_SYSTEM (must not be left empty)
*
* configure type of FW download
* CFG_SIMPLE - application optimized download
*
* Syntax:
*     DE_FIRMWARE_STORAGE  ( CFG_SIMPLE | CFG_ADVANCED )
*/
#ifndef DE_FIRMWARE_SYSTEM
#define DE_FIRMWARE_SYSTEM                   CFG_ADVANCED
#endif


/*
* xx.yy DE_HI_MAX_SIZE
*
* Host interface max size
*
* Configures the maximum size of data that target are allowed to send to WFE.
* The size must be atleast the size of largest possible 802.11 packet + the size
* of our HIC-header.
* There is no purpose making this value bigger unless aggregation is enabled.
*
* Syntax:
*     DE_HI_MAX_SIZE  1600,1601,1602...?
*/
#ifndef DE_HI_MAX_SIZE
#define DE_HI_MAX_SIZE                       1600
#endif


/*
* xx.yy DE_AGGREGATE_HI_DATA
*
* Aggregate host interface data
*
* This feature configures whether or not target should merge many messages into
* one message when communicating with the host.
* Enabling this feature require that the functions handling data from target
* support aggregation.
*
* Possible settings:
*     CFG_OFF                 Do not aggregate anything
*     CFG_AGGR_ALL            Aggregate all types of messages
*     CFG_AGGR_SCAN_IND       Aggregate only scan-indications
*     CFG_AGGR_ALL_BUT_DATA   Aggregate all but data-frames
*     CFG_AGGR_ONLY_DATA_CFM  Aggregate only data-send-confirms
*     CFG_AGGR_LAST_MARK      ????
*
* Syntax:
*     DE_AGGREGATE_HI_DATA ( CFG_OFF | CFG_AGGR_xxxx )
*/
#ifndef DE_AGGREGATE_HI_DATA
#define DE_AGGREGATE_HI_DATA                 CFG_OFF
#endif

/*
* xx.yy DE_NETWORK_SUPPORT
*
* Configures if ibss should be supported
*
* Possible settings:
*     CFG_NETWORK_BSS_STA       Support BSS networks where device is STA
*     CFG_NETWORK_BSS_AP         Support BSS networks where device is AP
*     CFG_NETWORK_IBSS             Support IBSS networks
*     CFG_NETWORK_AMP              Support AMP networks
*
* Syntax:
*     DE_NETWORK_SUPPORT (CFG_NETWORK_BSS_STA | CFG_NETWORK_AMP)
*/
#ifndef DE_NETWORK_SUPPORT
#define DE_NETWORK_SUPPORT (CFG_NETWORK_BSS_STA | CFG_NETWORK_IBSS)
#endif



/*
* xx.yy DE_IRQ_TYPE
*
* Configures how target should send IRQ:s to the host (which pin and type)
*
* Syntax:
*     DE_IRQ_TYPE
*/
#ifndef DE_IRQ_TYPE
#define DE_IRQ_TYPE              (HIC_CTRL_ALIGN_HATTN_VAL_POLICY_NATIVE_SDIO)
#endif


/*
* xx.yy Log size (must not be left empty)
*
* Configuration of size allocated for trace to memory
*
* Syntax:
*     DE_LOG_SIZE  ( <Number of 32-bit words > )
*/
#ifndef DE_LOG_SIZE
#define DE_LOG_SIZE              ( 0x30000 )
#endif

/*
* xx.yy Maximum number of nets in the WFE internal scan lists
*
* If the numeber of nets is exeded, the net with the lowest RSSI will be removed
* Should be used on devices with limited memory.
* Setting this value to 0 means unlimited number of nets
* If you are unsure what to use, use 0
*
* Syntax:
*     DE_MAX_SCAN_LIST_SIZE  0 | 1,2...
*/
#ifndef DE_MAX_SCAN_LIST_SIZE
#define DE_MAX_SCAN_LIST_SIZE  0
#endif

/*
* xx.yy Include driver support for HT rates
*
* Syntax:
*     DE_ENABLE_HT_RATES  ( CFG_OFF | CFG_ON )
*/
#ifndef DE_ENABLE_HT_RATES
#define DE_ENABLE_HT_RATES  CFG_ON
#endif

/*
* xx.yy Include CM Scan module
*
* Syntax:
*     DE_ENABLE_CM_SCAN  ( CFG_OFF | CFG_ON )
*/
#ifndef DE_ENABLE_CM_SCAN
#define DE_ENABLE_CM_SCAN  CFG_ON
#endif

/*
* xx.yy Include driver support for PCAP logs
*
* Syntax:
*     DE_ENABLE_PCAPLOG  ( CFG_OFF | CFG_ON | CFG_LOG_CMD52 )
*/
#ifndef DE_ENABLE_PCAPLOG
#define DE_ENABLE_PCAPLOG  CFG_OFF
#endif

/*
* xx.yy Size of a fragment sent as one SDIO CMD53
*
* Syntax:
*     DE_SDIO_FRAGMENT_SIZE  ( 32 | 64 | 128 | 256 | 512 for NRX700 )
*/
#ifndef DE_SDIO_FRAGMENT_SIZE
#define DE_SDIO_FRAGMENT_SIZE  512
#endif

/*
* xx.yy DE_DEBUG_MODE
*
* Configures if ibss should be supported
*
* Possible settings:
*     CFG_MEMORY        Debug memory allocations
*
* Syntax:
*     DE_DEBUG_MODE (CFG_OFF, CFG_MEMORY)
*/
#ifndef DE_DEBUG_MODE
#define DE_DEBUG_MODE (CFG_OFF)
#endif


/*
* xx.yy DE_RUNTIME_INTEGRATION
*
* Configures how the driver is intgrated with the host system.
*
* Possible settings:
*     CFG_MULTI_THREADED   The driver will be run from more than one thread, all locks must be implemented
*     CFG_SINGLE_THREADED  The driver is run on only one thread, no locks needs to be implemented
*
* Syntax:
*     DE_RUNTIME_INTEGRATION (CFG_MULTI_THREADED, CFG_SINGLE_THREADED)
*/
#ifndef DE_RUNTIME_INTEGRATION
#define DE_RUNTIME_INTEGRATION (CFG_MULTI_THREADED)
#endif


/*
* xx.yy PROTECT_FROM_DUP_SCAN_INDS
*
* will protect from excessive indications with new scan inds
* (needed on some platforms). If enabled the driver will grow
* by some 2k.
*
*/
#ifndef DE_PROTECT_FROM_DUP_SCAN_INDS
#define DE_PROTECT_FROM_DUP_SCAN_INDS CFG_ON
#endif


/*
* xx.yy DE_PMKID_CACHE_SUPPORT
*
* PMKID is generally used for fast re-auth and fast roaming
* in Enterprice mode with a radius server.
*
* If enabled the driver will grow by some 1.2k.
*
*/
#ifndef DE_PMKID_CACHE_SUPPORT
#define DE_PMKID_CACHE_SUPPORT CFG_ON
#endif


/*
 * xx.yy DE_MIB_TABLE_SUPPORT
 *
 * Does the firmware require a mib_table?
 * Some versions of NRX700 firmware require it
 * NRX600 does not require it.
 *
 */
#ifndef DE_MIB_TABLE_SUPPORT
#define DE_MIB_TABLE_SUPPORT CFG_ON
#endif

/*
* xx.yy Enter shutdown state when network interface goes down
*
* Only for the linux driver.
* If this is enabled, the command "ifconfig <iface> down" will cause
* the device to enter the shutdown mode. If this is disabled,
* the device will enter the shutdown mode only via NRXIOCSHUTDOWN.
*
* Syntax:
*     DE_ENABLE_PCAPLOG  ( CFG_OFF | CFG_ON )
*/
#ifndef DE_SHUTDOWN_ON_IFDOWN
#define DE_SHUTDOWN_ON_IFDOWN  CFG_OFF
#endif


/*
* xx.yy Configures if device parameters will be loaded from file
*
* Only for the linux driver.
* Configures if the device parameters will be loaded from file
* /nanoradio/settings.bin before the firmware is downloaded.
*
* Syntax:
*     DE_LOAD_DEVICE_PARAMS  ( CFG_NOT_INCLUDED | CFG_INCLUDED )
*/
#ifndef DE_LOAD_DEVICE_PARAMS
#define DE_LOAD_DEVICE_PARAMS  CFG_INCLUDED
#endif

/*
* xx.yy Configures if certain hash functions are provided by the
* driver environment.
*
* Syntax:
*     DE_ENABLE_HASH_FUNCTIONS  ( CFG_ON | CFG_OFF )
*/
#ifndef DE_ENABLE_HASH_FUNCTIONS
#define DE_ENABLE_HASH_FUNCTIONS CFG_OFF
#endif



#endif   /* ENV_CONFIG_ALL_H */
