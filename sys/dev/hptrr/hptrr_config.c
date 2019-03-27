/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) HighPoint Technologies, Inc.
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
#include <dev/hptrr/hptrr_config.h>
/****************************************************************************
 * config.c - auto-generated file
 ****************************************************************************/
#include <dev/hptrr/os_bsd.h>

extern int init_module_him_rr2310pm(void);
extern int init_module_him_rr174x_rr2210pm(void);
extern int init_module_him_rr2522pm(void);
extern int init_module_him_rr2340(void);
extern int init_module_him_rr222x_rr2240(void);
extern int init_module_him_rr1720(void);
extern int init_module_him_rr232x(void);
extern int init_module_vdev_raw(void);
extern int init_module_partition(void);
extern int init_module_raid0(void);
extern int init_module_raid1(void);
extern int init_module_raid5(void);
extern int init_module_jbod(void);

int init_config(void)
{
	init_module_him_rr2310pm();
	init_module_him_rr174x_rr2210pm();
	init_module_him_rr2522pm();
	init_module_him_rr2340();
	init_module_him_rr222x_rr2240();
	init_module_him_rr1720();
	init_module_him_rr232x();
	init_module_vdev_raw();
	init_module_partition();
	init_module_raid0();
	init_module_raid1();
	init_module_raid5();
	init_module_jbod();
	return 0;
}

char driver_name[] = "hptrr";
char driver_name_long[] = "RocketRAID 17xx/2xxx SATA controller driver";
char driver_ver[] = "v1.2";
int  osm_max_targets = 0xff;


int os_max_cache_size = 0x1000000;
