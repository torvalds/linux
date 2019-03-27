/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com> and was subsequently ported
 * to FreeBSD by Michael Gmelin <freebsd@grem.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _ICHIIC_IG4_VAR_H_
#define _ICHIIC_IG4_VAR_H_

#include "bus_if.h"
#include "device_if.h"
#include "pci_if.h"
#include "iicbus_if.h"

#define IG4_RBUFSIZE	128
#define IG4_RBUFMASK	(IG4_RBUFSIZE - 1)

enum ig4_op { IG4_IDLE, IG4_READ, IG4_WRITE };
enum ig4_vers { IG4_HASWELL, IG4_ATOM, IG4_SKYLAKE, IG4_APL };

struct ig4iic_softc {
	device_t	dev;
	struct		intr_config_hook enum_hook;
	device_t	iicbus;
	struct resource	*regs_res;
	int		regs_rid;
	struct resource	*intr_res;
	int		intr_rid;
	void		*intr_handle;
	int		intr_type;
	enum ig4_vers	version;
	enum ig4_op	op;
	int		cmd;
	int		rnext;
	int		rpos;
	char		rbuf[IG4_RBUFSIZE];
	int		error;
	uint8_t		last_slave;
	int		platform_attached : 1;
	int		use_10bit : 1;
	int		slave_valid : 1;
	int		read_started : 1;
	int		write_started : 1;
	int		access_intr_mask : 1;

	/*
	 * Locking semantics:
	 *
	 * Functions implementing the icbus interface that interact
	 * with the controller acquire an exclusive lock on call_lock
	 * to prevent interleaving of calls to the interface and a lock on
	 * io_lock right afterwards, to synchronize controller I/O activity.
	 *
	 * The interrupt handler can only read data while no iicbus call
	 * is in progress or while io_lock is dropped during mtx_sleep in
	 * wait_status and set_controller. It is safe to drop io_lock in those
	 * places, because the interrupt handler only accesses those registers:
	 *
	 * - IG4_REG_I2C_STA  (I2C Status)
	 * - IG4_REG_DATA_CMD (Data Buffer and Command)
	 * - IG4_REG_CLR_INTR (Clear Interrupt)
	 *
	 * Locking outside of those places is required to make the content
	 * of rpos/rnext predictable (e.g. whenever data_read is called and in
	 * ig4iic_transfer).
	 */
	struct sx	call_lock;
	struct mtx	io_lock;
};

typedef struct ig4iic_softc ig4iic_softc_t;

/* Attach/Detach called from ig4iic_pci_*() */
int ig4iic_attach(ig4iic_softc_t *sc);
int ig4iic_detach(ig4iic_softc_t *sc);

/* iicbus methods */
extern iicbus_transfer_t ig4iic_transfer;
extern iicbus_reset_t   ig4iic_reset;

#endif /* _ICHIIC_IG4_VAR_H_ */
