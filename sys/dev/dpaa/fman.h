/*-
 * Copyright (c) 2011-2012 Semihalf.
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

#ifndef FMAN_H_
#define FMAN_H_

#include <dev/fdt/simplebus.h>

/**
 * FMan driver instance data.
 */
struct fman_softc {
	struct simplebus_softc sc_base;
	struct resource *mem_res;
	struct resource *irq_res;
	struct resource *err_irq_res;
	struct rman	rman;
	int mem_rid;
	int irq_rid;
	int err_irq_rid;
	int qman_chan_base;
	int qman_chan_count;

	t_Handle fm_handle;
	t_Handle muram_handle;
};


/**
 * @group QMan bus interface.
 * @{
 */
struct resource * fman_alloc_resource(device_t bus, device_t child, int type,
    int *rid, rman_res_t start, rman_res_t end, rman_res_t count, u_int flags);
int fman_activate_resource(device_t bus, device_t child,
    int type, int rid, struct resource *res);
int fman_release_resource(device_t bus, device_t child, int type, int rid,
    struct resource *res);
int	fman_attach(device_t dev);
int	fman_detach(device_t dev);
int	fman_suspend(device_t dev);
int	fman_resume_dev(device_t dev);
int	fman_shutdown(device_t dev);
int	fman_read_ivar(device_t dev, device_t child, int index,
	    uintptr_t *result);
int	fman_qman_channel_id(device_t, int);
/** @} */

uint32_t	fman_get_clock(struct fman_softc *sc);
int	fman_get_handle(device_t dev, t_Handle *fmh);
int	fman_get_muram_handle(device_t dev, t_Handle *muramh);
int	fman_get_bushandle(device_t dev, vm_offset_t *fm_base);

#endif /* FMAN_H_ */
