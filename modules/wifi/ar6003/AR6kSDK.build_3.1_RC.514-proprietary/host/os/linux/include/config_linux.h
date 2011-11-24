//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
//
// 
// The software source and binaries included in this development package are
// licensed, not sold. You, or your company, received the package under one
// or more license agreements. The rights granted to you are specifically
// listed in these license agreement(s). All other rights remain with Atheros
// Communications, Inc., its subsidiaries, or the respective owner including
// those listed on the included copyright notices.  Distribution of any
// portion of this package must be in strict compliance with the license
// agreement(s) terms.
// </copyright>
// 
// <summary>
// 	Wifi driver for AR6002
// </summary>
//
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------

#ifndef _CONFIG_LINUX_H_
#define _CONFIG_LINUX_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <linux/version.h>

/*
 * Host-side GPIO support is optional.
 * If run-time access to GPIO pins is not required, then
 * this should be changed to #undef.
 */
#define CONFIG_HOST_GPIO_SUPPORT

/*
 * Host side Test Command support
 * Note: when HCI SDIO is enabled, a low stack IRQ or statck overflow is
 *       hit on FC10. So with HCI SDIO, minimize the stack allocation by 
 *       mutually exclude TCMD_SUPPORT, which allocates large buffers 
 *       in AR_TCMD_RESP in AR_SOFTC_T
 *
 */
#ifndef HCI_TRANSPORT_SDIO
#define CONFIG_HOST_TCMD_SUPPORT
#endif

/* Host-side support for Target-side profiling */
#undef CONFIG_TARGET_PROFILE_SUPPORT
/*DIX OFFLOAD SUPPORT*/
/*#define DIX_RX_OFFLOAD*/
/*#define DIX_TX_OFFLOAD*/

/* IP/TCP checksum offload */
/* Checksum offload is currently not supported for 64 bit platforms */
#ifndef __LP64__
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
#define CONFIG_CHECKSUM_OFFLOAD
#endif
#endif /* __LP64__ */

#ifdef __cplusplus
}
#endif

#endif
