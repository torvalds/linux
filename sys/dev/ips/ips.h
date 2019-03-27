/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Adaptec Inc.
 * All rights reserved.
 *
 * Written by: David Jeffery
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
 * $FreeBSD$
 */
#ifndef _IPS_H
#define _IPS_H

#ifdef _KERNEL

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/bio.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sema.h>
#include <sys/time.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

MALLOC_DECLARE(M_IPSBUF);

/*
 *  IPS MACROS
 */

#define ips_read_1(sc,offset)		bus_read_1(sc->iores, offset)
#define ips_read_2(sc,offset) 		bus_read_2(sc->iores, offset)
#define ips_read_4(sc,offset)		bus_read_4(sc->iores, offset)

#define ips_write_1(sc,offset,value)	bus_write_1(sc->iores, offset, value)
#define ips_write_2(sc,offset,value) 	bus_write_2(sc->iores, offset, value)
#define ips_write_4(sc,offset,value)	bus_write_4(sc->iores, offset, value)

/* this is ugly.  It zeros the end elements in an ips_command_t struct starting with the status element */
#define clear_ips_command(command)	bzero(&((command)->status), (unsigned long)(&(command)[1])-(unsigned long)&((command)->status))

#define ips_read_request(iobuf)		((iobuf)->bio_cmd == BIO_READ)

#define COMMAND_ERROR(command)		(((command)->status.fields.basic_status & IPS_GSC_STATUS_MASK) >= IPS_MIN_ERROR)

#define ips_set_error(command, error)	do {				\
	(command)->status.fields.basic_status = IPS_DRV_ERROR;		\
	(command)->status.fields.reserved = ((error) & 0x0f);		\
} while (0)

#ifndef IPS_DEBUG
#define DEVICE_PRINTF(x...)
#define PRINTF(x...)
#else
#define DEVICE_PRINTF(level,x...)	if(IPS_DEBUG >= level)device_printf(x)
#define PRINTF(level,x...)		if(IPS_DEBUG >= level)printf(x)
#endif

struct ips_softc;

typedef struct {
	u_int32_t 	status[IPS_MAX_CMD_NUM];
	u_int32_t 	base_phys_addr;
	int 		nextstatus;
	bus_dma_tag_t	dmatag;
	bus_dmamap_t	dmamap;
} ips_copper_queue_t;

/* used to keep track of current commands to the card */
typedef struct ips_command{
	u_int8_t		command_number;
	u_int8_t 		id;
	u_int8_t		timeout;
	struct ips_softc *	sc;
	bus_dma_tag_t		data_dmatag;
	bus_dmamap_t		data_dmamap;
	bus_dmamap_t		command_dmamap;
	void *			command_buffer;
	u_int32_t		command_phys_addr;/*WARNING! must be changed if 64bit addressing ever used*/	
	ips_cmd_status_t	status;
	SLIST_ENTRY(ips_command)	next;
	void *			data_buffer;
	void *			arg;
	void			(* callback)(struct ips_command *command);
}ips_command_t;

typedef struct ips_softc{
        struct resource *       iores;
        struct resource *       irqres;
        struct intr_config_hook ips_ich;
        int                     configured;
        int                     state;
        int                     iotype;
        int                     rid;
        int                     irqrid;
        void *                  irqcookie;
	bus_dma_tag_t	        adapter_dmatag;
	bus_dma_tag_t		command_dmatag;
	bus_dma_tag_t		sg_dmatag;
        device_t                dev;
        struct cdev *device_file;
	struct callout		timer;
	u_int16_t		adapter_type;
	ips_adapter_info_t	adapter_info;
	device_t		diskdev[IPS_MAX_NUM_DRIVES];
	ips_drive_t		drives[IPS_MAX_NUM_DRIVES];
	u_int8_t		drivecount;
	u_int16_t		ffdc_resetcount;
	struct timeval		ffdc_resettime;
	u_int8_t		next_drive;
	u_int8_t		max_cmds;
	volatile u_int8_t	used_commands;
	ips_command_t		*commandarray;
	ips_command_t		*staticcmd;
	SLIST_HEAD(command_list, ips_command) free_cmd_list;
	int			(* ips_adapter_reinit)(struct ips_softc *sc, 
						       int force);
        void                    (* ips_adapter_intr)(void *sc);
	void			(* ips_issue_cmd)(ips_command_t *command);
	void			(* ips_poll_cmd)(ips_command_t *command);
	ips_copper_queue_t *	copper_queue;
	struct mtx		queue_mtx;
	struct bio_queue_head	queue;
	struct sema		cmd_sema;

}ips_softc_t;

/* function defines from ips_ioctl.c */
extern int ips_ioctl_request(ips_softc_t *sc, u_long ioctl_cmd, caddr_t addr, 
				int32_t flags);
/* function defines from ips_disk.c */
extern void ipsd_finish(struct bio *iobuf);

/* function defines from ips_commands.c */
extern int ips_flush_cache(ips_softc_t *sc);
extern void ips_start_io_request(ips_softc_t *sc);
extern int ips_get_drive_info(ips_softc_t *sc);
extern int ips_get_adapter_info(ips_softc_t *sc);
extern int ips_ffdc_reset(ips_softc_t *sc);
extern int ips_update_nvram(ips_softc_t *sc); 
extern int ips_clear_adapter(ips_softc_t *sc);

/* function defines from ips.c */
extern int ips_get_free_cmd(ips_softc_t *sc, ips_command_t **command, unsigned long flags);
extern void ips_insert_free_cmd(ips_softc_t *sc, ips_command_t *command);
extern int ips_adapter_init(ips_softc_t *sc);
extern int ips_morpheus_reinit(ips_softc_t *sc, int force);
extern int ips_adapter_free(ips_softc_t *sc);
extern void ips_morpheus_intr(void *sc);
extern void ips_issue_morpheus_cmd(ips_command_t *command);
extern void ips_morpheus_poll(ips_command_t *command);
extern int ips_copperhead_reinit(ips_softc_t *sc, int force);
extern void ips_copperhead_intr(void *sc);
extern void ips_issue_copperhead_cmd(ips_command_t *command);
extern void ips_copperhead_poll(ips_command_t *command);

#endif
#endif
