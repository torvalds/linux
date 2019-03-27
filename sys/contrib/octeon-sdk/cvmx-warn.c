/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/







/**
 * @file
 *
 * Functions for warning users about errors and such.
 *
 * <hr>$Revision: 70030 $<hr>
 *
 */
#include "cvmx.h"
#include "cvmx-warn.h"

void cvmx_warn(const char *format, ...)
{
#ifdef CVMX_BUILD_FOR_UBOOT
    DECLARE_GLOBAL_DATA_PTR;
    if (!gd->have_console)
    {
        /* If the serial port is not set up yet,
        ** save pointer to error message (most likely a constant in flash)
        ** to print out once we can. */
#ifdef U_BOOT_OLD
	gd->err_msg = (void *)format;
#else
	gd->ogd.err_msg = (void *)format;
#endif
        return;
    }
#endif
    va_list args;
    va_start(args, format);
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
    printk("WARNING:");
    vprintk(format, args);
#else
#ifdef CVMX_BUILD_FOR_FREEBSD_KERNEL
    printf("WARNING: ");
#else
    printf("WARNING:\n");
#endif
    vprintf(format, args);
#endif
    va_end(args);
}

