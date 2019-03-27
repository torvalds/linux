/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2003 Paul Saab
 * Copyright (c) 2003 Vinod Kashyap
 * Copyright (c) 2000 BSDi
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
 * $FreeBSD$
 */
/*
 * Portability and compatibility interfaces.
 */

#ifdef __FreeBSD__
/******************************************************************************
 * FreeBSD
 */
#define TWE_SUPPORTED_PLATFORM

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/sx.h>

#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/stat.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <geom/geom_disk.h>

#define TWE_DRIVER_NAME		twe
#define TWED_DRIVER_NAME	twed
#define TWE_MALLOC_CLASS	M_TWE

/* 
 * Wrappers for bus-space actions
 */
#define TWE_CONTROL(sc, val)		bus_write_4((sc)->twe_io, 0x0, (u_int32_t)val)
#define TWE_STATUS(sc)			(u_int32_t)bus_read_4((sc)->twe_io, 0x4)
#define TWE_COMMAND_QUEUE(sc, val)	bus_write_4((sc)->twe_io, 0x8, (u_int32_t)val)
#define TWE_RESPONSE_QUEUE(sc)		(TWE_Response_Queue)bus_read_4((sc)->twe_io, 0xc)

/*
 * FreeBSD-specific softc elements
 */
#define TWE_PLATFORM_SOFTC								\
    bus_dmamap_t		twe_cmdmap;	/* DMA map for command */				\
    u_int32_t			twe_cmdphys;	/* address of command in controller space */		\
    device_t			twe_dev;		/* bus device */		\
    struct cdev *twe_dev_t;		/* control device */		\
    struct resource		*twe_io;		/* register interface window */	\
    bus_dma_tag_t		twe_parent_dmat;	/* parent DMA tag */		\
    bus_dma_tag_t		twe_buffer_dmat;	/* data buffer DMA tag */	\
    bus_dma_tag_t		twe_cmd_dmat;		/* command buffer DMA tag */	\
    bus_dma_tag_t		twe_immediate_dmat;	/* command buffer DMA tag */	\
    struct resource		*twe_irq;		/* interrupt */			\
    void			*twe_intr;		/* interrupt handle */		\
    struct intr_config_hook	twe_ich;		/* delayed-startup hook */	\
    void			*twe_cmd;		/* command structures */	\
    void			*twe_immediate;		/* immediate commands */	\
    bus_dmamap_t		twe_immediate_map;					\
    struct mtx			twe_io_lock;						\
    struct sx			twe_config_lock;

/*
 * FreeBSD-specific request elements
 */
#define TWE_PLATFORM_REQUEST										\
    bus_dmamap_t		tr_dmamap;	/* DMA map for data */					\
    u_int32_t			tr_dataphys;	/* data buffer base address in controller space */

/*
 * Output identifying the controller/disk
 */
#define twe_printf(sc, fmt, args...)	device_printf(sc->twe_dev, fmt , ##args)
#define twed_printf(twed, fmt, args...)	device_printf(twed->twed_dev, fmt , ##args)

#define	TWE_IO_LOCK(sc)			mtx_lock(&(sc)->twe_io_lock)
#define	TWE_IO_UNLOCK(sc)		mtx_unlock(&(sc)->twe_io_lock)
#define	TWE_IO_ASSERT_LOCKED(sc)	mtx_assert(&(sc)->twe_io_lock, MA_OWNED)
#define	TWE_CONFIG_LOCK(sc)		sx_xlock(&(sc)->twe_config_lock)
#define	TWE_CONFIG_UNLOCK(sc)		sx_xunlock(&(sc)->twe_config_lock)
#define	TWE_CONFIG_ASSERT_LOCKED(sc)	sx_assert(&(sc)->twe_config_lock, SA_XLOCKED)

#endif /* FreeBSD */

#ifndef TWE_SUPPORTED_PLATFORM
#error platform not supported
#endif
