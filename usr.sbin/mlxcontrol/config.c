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

#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>

#include <dev/mlx/mlxio.h>
#include <dev/mlx/mlxreg.h>

#include "mlxcontrol.h"

static void	print_span(struct mlx_sys_drv_span *span, int arms);
static void	print_sys_drive(struct conf_config *conf, int drvno);
static void	print_phys_drive(struct conf_config *conf, int chn, int targ);

/********************************************************************************
 * Get the configuration from the selected controller.
 *
 * config <controller>
 *		Print the configuration for <controller>
 *
 * XXX update to support adding/deleting drives.
 */

int
cmd_config(int argc, char *argv[]) 
{
    struct conf_config	conf;
    int			unit = 0;	/* XXX */
    int			i, j;

    bzero(&conf.cc_cfg, sizeof(conf.cc_cfg));
    if (mlx_read_configuration(unit, &conf.cc_cfg)) {
	printf("mlx%d: error submitting READ CONFIGURATION\n", unit);
    } else {

	printf("# Controller <INSERT DETAILS HERE>\n");
	printf("#\n# Physical devices connected:\n");
	for (i = 0; i < 5; i++)
	    for (j = 0; j < 16; j++)
	    print_phys_drive(&conf, i, j);
	printf("#\n# System Drives defined:\n");

	for (i = 0; i < conf.cc_cfg.cc_num_sys_drives; i++)
	    print_sys_drive(&conf, i);
    }
    return(0);
}


/********************************************************************************
 * Print details for the system drive (drvno) in a format that we will be 
 * able to parse later.
 *
 * drive?? <raidlevel> <writemode>
 *   span? 0x????????-0x???????? ????MB on <disk> [...]
 *   ...
 */
static void
print_span(struct mlx_sys_drv_span *span, int arms)
{
    int		i;
    
    printf("0x%08x-0x%08x %uMB on", span->sp_start_lba, span->sp_start_lba + span->sp_nblks, span->sp_nblks / 2048);
    for (i = 0; i < arms; i++)
	printf(" disk%02d%02d", span->sp_arm[i] >> 4, span->sp_arm[i] & 0x0f);
    printf("\n");
}

static void
print_sys_drive(struct conf_config *conf, int drvno)
{
    struct mlx_sys_drv	*drv = &conf->cc_cfg.cc_sys_drives[drvno];
    int			i;

    printf("drive%02d ", drvno);
    switch(drv->sd_raidlevel & 0xf) {
    case MLX_SYS_DRV_RAID0:
	printf("RAID0");
	break;
    case MLX_SYS_DRV_RAID1:
	printf("RAID1");
	break;
    case MLX_SYS_DRV_RAID3:
	printf("RAID3");
	break;
    case MLX_SYS_DRV_RAID5:
	printf("RAID5");
	break;
    case MLX_SYS_DRV_RAID6:
	printf("RAID6");
	break;
    case MLX_SYS_DRV_JBOD:
	printf("JBOD");
	break;
    default:
	printf("RAID?");
    }
    printf(" write%s\n", drv->sd_raidlevel & MLX_SYS_DRV_WRITEBACK ? "back" : "through");
    
    for (i = 0; i < drv->sd_valid_spans; i++) {
	printf("  span%d ", i);
	print_span(&drv->sd_span[i], drv->sd_valid_arms);
    }
}

/********************************************************************************
 * Print details for the physical drive at chn/targ in a format suitable for
 * human consumption.
 *
 * <type>CCTT (<state>) "<vendor>/<model>" 
 *                       ????MB <features>
 *
 */
static void
print_phys_drive(struct conf_config *conf, int chn, int targ)
{
    struct mlx_phys_drv		*drv = &conf->cc_cfg.cc_phys_drives[chn * 16 + targ];
 
    /* if the drive isn't present, don't print it */
    if (!(drv->pd_flags1 & MLX_PHYS_DRV_PRESENT))
	return;

    mlx_print_phys_drv(drv, chn, targ, "# ", 1);
}


