/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Written by: David Jeffery
 * Copyright (c) 2002 Adaptec Inc.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/ips/ipsreg.h>
#include <dev/ips/ips.h>
#include <dev/ips/ips_ioctl.h>

static void ips_ioctl_finish(ips_command_t *command)
{
	ips_ioctl_t *ioctl_cmd = command->arg;
	if(ioctl_cmd->readwrite & IPS_IOCTL_READ){
		bus_dmamap_sync(ioctl_cmd->dmatag, ioctl_cmd->dmamap, 
				BUS_DMASYNC_POSTREAD);
	} else if(ioctl_cmd->readwrite & IPS_IOCTL_WRITE){
		bus_dmamap_sync(ioctl_cmd->dmatag, ioctl_cmd->dmamap, 
				BUS_DMASYNC_POSTWRITE);
	}
	bus_dmamap_sync(command->sc->command_dmatag, command->command_dmamap, 
			BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(ioctl_cmd->dmatag, ioctl_cmd->dmamap);
	ioctl_cmd->status.value = command->status.value;
	ips_insert_free_cmd(command->sc, command);
}

static void ips_ioctl_callback(void *cmdptr, bus_dma_segment_t *segments,int segnum, int error)
{
	ips_command_t *command = cmdptr;
	ips_ioctl_t *ioctl_cmd = command->arg;
	ips_generic_cmd *command_buffer = command->command_buffer;
	if(error){
		ips_set_error(command, error);
		return;
	}
	command_buffer->id = command->id;
	command_buffer->buffaddr = segments[0].ds_addr;
	if(ioctl_cmd->readwrite & IPS_IOCTL_WRITE){
		bus_dmamap_sync(ioctl_cmd->dmatag, ioctl_cmd->dmamap, 
				BUS_DMASYNC_PREWRITE);
	} else if(ioctl_cmd->readwrite & IPS_IOCTL_READ){
		bus_dmamap_sync(ioctl_cmd->dmatag, ioctl_cmd->dmamap, 
				BUS_DMASYNC_PREREAD);
	}
	bus_dmamap_sync(command->sc->command_dmatag, command->command_dmamap, 
			BUS_DMASYNC_PREWRITE);
	command->sc->ips_issue_cmd(command);
}
static int ips_ioctl_start(ips_command_t *command)
{
	ips_ioctl_t *ioctl_cmd = command->arg;
	memcpy(command->command_buffer, ioctl_cmd->command_buffer,
		sizeof(ips_generic_cmd));
	command->callback = ips_ioctl_finish;
	bus_dmamap_load(ioctl_cmd->dmatag, ioctl_cmd->dmamap, 
			ioctl_cmd->data_buffer,ioctl_cmd->datasize, 
			ips_ioctl_callback, command, 0);
	return 0;
}

static int ips_ioctl_cmd(ips_softc_t *sc, ips_ioctl_t *ioctl_cmd, ips_user_request *user_request)
{
	ips_command_t *command;
	int error = EINVAL;

	if (bus_dma_tag_create(	/* parent    */	sc->adapter_dmatag,
			/* alignment */	1,
			/* boundary  */	0,
			/* lowaddr   */	BUS_SPACE_MAXADDR_32BIT,
			/* highaddr  */	BUS_SPACE_MAXADDR,
			/* filter    */	NULL,
			/* filterarg */	NULL,
			/* maxsize   */	ioctl_cmd->datasize,
			/* numsegs   */	1,
			/* maxsegsize*/	ioctl_cmd->datasize,
			/* flags     */	0,
			/* lockfunc  */ NULL,
			/* lockarg   */ NULL,
			&ioctl_cmd->dmatag) != 0) {
		return ENOMEM;
        }
	if(bus_dmamem_alloc(ioctl_cmd->dmatag, &ioctl_cmd->data_buffer, 
	   0, &ioctl_cmd->dmamap)){
		error = ENOMEM;
		goto exit;
	}
	if(copyin(user_request->data_buffer,ioctl_cmd->data_buffer, 
	    ioctl_cmd->datasize))
		goto exit;
	ioctl_cmd->status.value = 0xffffffff;
	mtx_lock(&sc->queue_mtx);
	if((error = ips_get_free_cmd(sc, &command, 0)) > 0){
		error = ENOMEM;
		mtx_unlock(&sc->queue_mtx);
		goto exit;
	}
	command->arg = ioctl_cmd;
	ips_ioctl_start(command);
	while( ioctl_cmd->status.value == 0xffffffff)
		msleep(ioctl_cmd, &sc->queue_mtx, 0, "ips", hz/10);
	if(COMMAND_ERROR(ioctl_cmd))
		error = EIO;
	else
		error = 0;
	mtx_unlock(&sc->queue_mtx);
	if(copyout(ioctl_cmd->data_buffer, user_request->data_buffer,
	    ioctl_cmd->datasize))
		error = EINVAL;
	mtx_lock(&sc->queue_mtx);
	ips_insert_free_cmd(sc, command);
	mtx_unlock(&sc->queue_mtx);

exit:	bus_dmamem_free(ioctl_cmd->dmatag, ioctl_cmd->data_buffer,
			ioctl_cmd->dmamap);
	bus_dma_tag_destroy(ioctl_cmd->dmatag);
	return error;
}


int ips_ioctl_request(ips_softc_t *sc, u_long ioctl_request, caddr_t addr, int32_t flags){
	int error = EINVAL;
	ips_ioctl_t *ioctl_cmd;
	ips_user_request *user_request;
	switch(ioctl_request){
	case IPS_USER_CMD:
		user_request = (ips_user_request *)addr;
		ioctl_cmd = malloc(sizeof(ips_ioctl_t), M_IPSBUF, M_WAITOK);
		ioctl_cmd->command_buffer = malloc(sizeof(ips_generic_cmd),
						M_IPSBUF, M_WAITOK);
		if(copyin(user_request->command_buffer, 
		    ioctl_cmd->command_buffer, sizeof(ips_generic_cmd))){
			free(ioctl_cmd->command_buffer, M_IPSBUF);
			free(ioctl_cmd, M_IPSBUF);
			break;
		}
		ioctl_cmd->readwrite = IPS_IOCTL_READ | IPS_IOCTL_WRITE;
		ioctl_cmd->datasize = IPS_IOCTL_BUFFER_SIZE;
		error = ips_ioctl_cmd(sc, ioctl_cmd, user_request);
		free(ioctl_cmd->command_buffer, M_IPSBUF);
		free(ioctl_cmd, M_IPSBUF);
		break;
	}

	return error;
} 
