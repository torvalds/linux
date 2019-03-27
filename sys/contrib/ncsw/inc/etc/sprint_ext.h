/* Copyright (c) 2008-2012 Freescale Semiconductor, Inc
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/**************************************************************************//**
 @File          sprint_ext.h

 @Description   Debug routines (externals).

*//***************************************************************************/

#ifndef __SPRINT_EXT_H
#define __SPRINT_EXT_H


#if defined(NCSW_LINUX) && defined(__KERNEL__)
#include <linux/kernel.h>

#elif defined(NCSW_VXWORKS)
#include "private/stdioP.h"

#elif defined(NCSW_FREEBSD)
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/libkern.h>

#else
#include <stdio.h>
#endif /* defined(NCSW_LINUX) && defined(__KERNEL__) */

#include "std_ext.h"


/**************************************************************************//**
 @Group         etc_id   Utility Library Application Programming Interface

 @Description   External routines.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         sprint_id Sprint

 @Description   Sprint & Sscan module functions,definitions and enums.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      Sprint

 @Description   Format a string and place it in a buffer.

 @Param[in]     buff - The buffer to place the result into.
 @Param[in]     str  - The format string to use.
 @Param[in]     ...  - Arguments for the format string.

 @Return        Number of bytes formatted.
*//***************************************************************************/
int Sprint(char *buff, const char *str, ...);

/**************************************************************************//**
 @Function      Snprint

 @Description   Format a string and place it in a buffer.

 @Param[in]     buf  - The buffer to place the result into.
 @Param[in]     size - The size of the buffer, including the trailing null space.
 @Param[in]     fmt  - The format string to use.
 @Param[in]     ...  - Arguments for the format string.

 @Return        Number of bytes formatted.
*//***************************************************************************/
int Snprint(char * buf, uint32_t size, const char *fmt, ...);

/**************************************************************************//**
 @Function      Sscan

 @Description   Unformat a buffer into a list of arguments.

 @Param[in]     buf  - input buffer.
 @Param[in]     fmt  - formatting of buffer.
 @Param[out]    ...  - resulting arguments.

 @Return        Number of bytes unformatted.
*//***************************************************************************/
int Sscan(const char * buf, const char * fmt, ...);

/** @} */ /* end of sprint_id group */
/** @} */ /* end of etc_id group */


#endif /* __SPRINT_EXT_H */
