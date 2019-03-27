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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <cam/scsi/scsi_all.h>

#include <dev/mlx/mlxio.h>
#include <dev/mlx/mlxreg.h>

#include "mlxcontrol.h"

/********************************************************************************
 * Iterate over all mlx devices, call (func) with each ones' path and (arg)
 */
void
mlx_foreach(void (*func)(int unit, void *arg), void *arg)
{
    int		i, fd;
    
    /* limit total count for sanity */
    for (i = 0; i < 64; i++) {
	/* verify we can open it */
	if ((fd = open(ctrlrpath(i), 0)) >= 0)
	    close(fd);
	/* if we can, do */
	if (fd >= 0) {
	    func(i, arg);
	}
    }
}

/********************************************************************************
 * Open the controller (unit) and give the fd to (func) along with (arg)
 */
void
mlx_perform(int unit, void (*func)(int fd, void *arg), void *arg)
{    
    int		fd;
    
    if ((fd = open(ctrlrpath(unit), 0)) >= 0) {
	func(fd, arg);
	close(fd);
    }
}

/********************************************************************************
 * Iterate over all mlxd devices, call (func) with each ones' path and (arg)
 */
void
mlxd_foreach_ctrlr(int unit, void *arg)
{
    struct mlxd_foreach_action	*ma = (struct mlxd_foreach_action *)arg;
    int				i, fd, ctrlfd;
    
    /* Get the device */
    if ((ctrlfd = open(ctrlrpath(unit), 0)) < 0)
	return;
    
    for (i = -1; ;) {
	/* Get the unit number of the next child device */
	if (ioctl(ctrlfd, MLX_NEXT_CHILD, &i) < 0) {
	    close(ctrlfd);
	    return;
	}
	
	/* check that we can open this unit */
	if ((fd = open(drivepath(i), 0)) >= 0)
	    close(fd);
	/* if we can, do */
	if (fd >= 0) {
	    ma->func(i, ma->arg);
	}
    }
}

void
mlxd_foreach(void (*func)(int unit, void *arg), void *arg)
{
    struct mlxd_foreach_action ma;
    
    ma.func = func;
    ma.arg = arg;
    mlx_foreach(mlxd_foreach_ctrlr, &ma);
}

/********************************************************************************
 * Find the controller that manages the drive (unit), return controller number
 * and system drive number on that controller.
 */
static struct 
{
    int		unit;
    int		ctrlr;
    int		sysdrive;
} mlxd_find_ctrlr_param;

static void
mlxd_find_ctrlr_search(int unit, void *arg)
{
    int				i, fd;
    
    /* Get the device */
    if ((fd = open(ctrlrpath(unit), 0)) >= 0) {
	for (i = -1; ;) {
	    /* Get the unit number of the next child device */
	    if (ioctl(fd, MLX_NEXT_CHILD, &i) < 0)
		break;

	    /* is this child the unit we want? */
	    if (i == mlxd_find_ctrlr_param.unit) {
		mlxd_find_ctrlr_param.ctrlr = unit;
		if (ioctl(fd, MLX_GET_SYSDRIVE, &i) == 0)
		    mlxd_find_ctrlr_param.sysdrive = i;
	    }
	}
	close(fd);
    }
}

int
mlxd_find_ctrlr(int unit, int *ctrlr, int *sysdrive)
{
    mlxd_find_ctrlr_param.unit = unit;
    mlxd_find_ctrlr_param.ctrlr = -1;
    mlxd_find_ctrlr_param.sysdrive = -1;

    mlx_foreach(mlxd_find_ctrlr_search, NULL);
    if ((mlxd_find_ctrlr_param.ctrlr != -1) && (mlxd_find_ctrlr_param.sysdrive != -1)) {
	*ctrlr = mlxd_find_ctrlr_param.ctrlr;
	*sysdrive = mlxd_find_ctrlr_param.sysdrive;
	return(0);
    }
    return(1);
}


/********************************************************************************
 * Send a command to the controller on (fd)
 */

void
mlx_command(int fd, void *arg)
{
    struct mlx_usercommand	*cmd = (struct mlx_usercommand *)arg;
    int				error;
    
    error = ioctl(fd, MLX_COMMAND, cmd);
    if (error != 0)
	cmd->mu_error = error;
}

/********************************************************************************
 * Perform an ENQUIRY2 command and return information related to the controller
 * (unit)
 */
int
mlx_enquiry(int unit, struct mlx_enquiry2 *enq)
{
    struct mlx_usercommand	cmd;

    /* build the command */
    cmd.mu_datasize = sizeof(*enq);
    cmd.mu_buf = enq;
    cmd.mu_bufptr = 8;
    cmd.mu_command[0] = MLX_CMD_ENQUIRY2;

    /* hand it off for processing */
    mlx_perform(unit, mlx_command, (void *)&cmd);

    return(cmd.mu_status != 0);
}


/********************************************************************************
 * Perform a READ CONFIGURATION command and return information related to the controller
 * (unit)
 */
int
mlx_read_configuration(int unit, struct mlx_core_cfg *cfg)
{
    struct mlx_usercommand	cmd;

    /* build the command */
    cmd.mu_datasize = sizeof(*cfg);
    cmd.mu_buf = cfg;
    cmd.mu_bufptr = 8;
    cmd.mu_command[0] = MLX_CMD_READ_CONFIG;

    /* hand it off for processing */
    mlx_perform(unit, mlx_command, (void *)&cmd);

    return(cmd.mu_status != 0);
}

/********************************************************************************
 * Perform a SCSI INQUIRY command and return pointers to the relevant data.
 */
int
mlx_scsi_inquiry(int unit, int channel, int target, char **vendor, char **device, char **revision)
{
    struct mlx_usercommand	cmd;
    static struct {
	    struct mlx_dcdb		dcdb;
	    union {
		struct scsi_inquiry_data	inq;
		u_int8_t			pad[SHORT_INQUIRY_LENGTH];
	    } d;
    } __attribute__ ((packed))		dcdb_cmd;
    struct scsi_inquiry		*inq_cmd = (struct scsi_inquiry *)&dcdb_cmd.dcdb.dcdb_cdb[0];
    
    /* build the command */
    cmd.mu_datasize = sizeof(dcdb_cmd);
    cmd.mu_buf = &dcdb_cmd;
    cmd.mu_command[0] = MLX_CMD_DIRECT_CDB;
    
    /* build the DCDB */
    bzero(&dcdb_cmd, sizeof(dcdb_cmd));
    dcdb_cmd.dcdb.dcdb_channel = channel;
    dcdb_cmd.dcdb.dcdb_target = target;
    dcdb_cmd.dcdb.dcdb_flags = MLX_DCDB_DATA_IN | MLX_DCDB_TIMEOUT_10S;
    dcdb_cmd.dcdb.dcdb_datasize = SHORT_INQUIRY_LENGTH;
    dcdb_cmd.dcdb.dcdb_cdb_length = 6;
    dcdb_cmd.dcdb.dcdb_sense_length = SSD_FULL_SIZE;

    /* build the cdb */
    inq_cmd->opcode = INQUIRY;
    scsi_ulto2b(SHORT_INQUIRY_LENGTH, inq_cmd->length);
    
    /* hand it off for processing */
    mlx_perform(unit, mlx_command, &cmd);

    if (cmd.mu_status == 0) {
	*vendor = &dcdb_cmd.d.inq.vendor[0];
	*device = &dcdb_cmd.d.inq.product[0];
	*revision = &dcdb_cmd.d.inq.revision[0];
    }
    return(cmd.mu_status);
}

/********************************************************************************
 * Perform a GET DEVICE STATE command and return pointers to the relevant data.
 */
int
mlx_get_device_state(int unit, int channel, int target, struct mlx_phys_drv *drv)
{
    struct mlx_usercommand	cmd;

    /* build the command */
    cmd.mu_datasize = sizeof(*drv);
    cmd.mu_buf = drv;
    cmd.mu_bufptr = 8;
    cmd.mu_command[0] = MLX_CMD_DEVICE_STATE;
    cmd.mu_command[2] = channel;
    cmd.mu_command[3] = target;

    /* hand it off for processing */
    mlx_perform(unit, mlx_command, (void *)&cmd);

    return(cmd.mu_status != 0);
}
