/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2000-2001 by Coleman Kane <cokane@FreeBSD.org>
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Gardner Buchanan.
 * 4. The name of Gardner Buchanan may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *   $FreeBSD$
 */

/* tdfx_pci.h -- Prototypes for tdfx device methods */
/* Copyright (C) 2000-2001 by Coleman Kane <cokane@FreeBSD.org> */
#include <sys/proc.h>
#include <sys/conf.h>

/* Driver functions */
static int tdfx_probe(device_t dev);
static int tdfx_attach(device_t dev);
static int tdfx_setmtrr(device_t dev);
static int tdfx_clrmtrr(device_t dev);
static int tdfx_detach(device_t dev);
static int tdfx_shutdown(device_t dev);

/* CDEV file ops */
static d_open_t tdfx_open;
static d_close_t tdfx_close;
static d_mmap_t tdfx_mmap;
static d_ioctl_t tdfx_ioctl;

/* Card Queries */
static int tdfx_do_query(u_int cmd, struct tdfx_pio_data *piod);
static int tdfx_query_boards(void);
static int tdfx_query_fetch(u_int cmd, struct tdfx_pio_data *piod);
static int tdfx_query_update(u_int cmd, struct tdfx_pio_data *piod);

/* Card PIO funcs */
static int tdfx_do_pio(u_int cmd, struct tdfx_pio_data *piod);
static int tdfx_do_pio_wt(struct tdfx_pio_data *piod);
static int tdfx_do_pio_rd(struct tdfx_pio_data *piod);
