/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Michael Smith
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD$
 */

#include <sys/types.h>
#include <stdio.h>
#include <paths.h>
#include <string.h>

#include <dev/mlx/mlxio.h>
#include <dev/mlx/mlxreg.h>

#include "mlxcontrol.h"

/********************************************************************************
 * Various name-producing and -parsing functions
 */

/* return path of controller (unit) */
char *
ctrlrpath(int unit)
{
    static char	buf[32];
    
    sprintf(buf, "%s%s", _PATH_DEV, ctrlrname(unit));
    return(buf);
}

/* return name of controller (unit) */
char *
ctrlrname(int unit)
{
    static char	buf[32];
    
    sprintf(buf, "mlx%d", unit);
    return(buf);
}

/* return path of drive (unit) */
char *
drivepath(int unit)
{
    static char	buf[32];
    
    sprintf(buf, "%s%s", _PATH_DEV, drivename(unit));
    return(buf);
}

/* return name of drive (unit) */
char *
drivename(int unit)
{
    static char	buf[32];
    
    sprintf(buf, "mlxd%d", unit);
    return(buf);
}

/* get controller unit number from name in (str) */
int
ctrlrunit(char *str)
{
    int		unit;
    
    if (sscanf(str, "mlx%d", &unit) == 1)
	return(unit);
    return(-1);
}

/* get drive unit number from name in (str) */
int
driveunit(char *str)
{
    int		unit;
    
    if (sscanf(str, "mlxd%d", &unit) == 1)
	return(unit);
    return(-1);
}

/********************************************************************************
 * Standardised output of various data structures.
 */

void
mlx_print_phys_drv(struct mlx_phys_drv *drv, int chn, int targ, char *prefix, int verbose)
{
    char	*type, *device, *vendor, *revision;

    switch(drv->pd_flags2 & 0x03) {
    case MLX_PHYS_DRV_DISK:
	type = "disk";
	break;
    case MLX_PHYS_DRV_SEQUENTIAL:
	type = "tape";
	break;
    case MLX_PHYS_DRV_CDROM:
	type= "cdrom";
	break;
    case MLX_PHYS_DRV_OTHER:
    default:
	type = "unknown";
	break;
    }
    printf("%s%s%02d%02d ", prefix, type, chn, targ);
    switch(drv->pd_status) {
    case MLX_PHYS_DRV_DEAD:
	printf(" (dead)       ");
	break;
    case MLX_PHYS_DRV_WRONLY:
	printf(" (write-only) ");
	break;
    case MLX_PHYS_DRV_ONLINE:
	printf(" (online)     ");
	break;
    case MLX_PHYS_DRV_STANDBY:
	printf(" (standby)    ");
	break;
    default:
	printf(" (0x%02x)   ", drv->pd_status);
    }
    printf("\n");
    
    if (verbose) {
	
	printf("%s   ", prefix);
	if (!mlx_scsi_inquiry(0, chn, targ, &vendor, &device, &revision)) {
	    printf("'%8.8s' '%16.16s' '%4.4s'", vendor, device, revision);
	} else {
	    printf("<IDENTIFY FAILED>");
	}
    
	printf(" %dMB ", drv->pd_config_size / 2048);
    
	if (drv->pd_flags2 & MLX_PHYS_DRV_FAST20) {
	    printf(" fast20");
	} else if (drv->pd_flags2 & MLX_PHYS_DRV_FAST) {
	    printf(" fast");
	}
	if (drv->pd_flags2 & MLX_PHYS_DRV_WIDE)
	    printf(" wide");
	if (drv->pd_flags2 & MLX_PHYS_DRV_SYNC)
	    printf(" sync");
	if (drv->pd_flags2 & MLX_PHYS_DRV_TAG)
	    printf(" tag-enabled");
	printf("\n");
    }
}
