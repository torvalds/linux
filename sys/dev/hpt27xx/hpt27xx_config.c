/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 HighPoint Technologies, Inc.
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

#include <dev/hpt27xx/hpt27xx_config.h>
/****************************************************************************
 * config.c - auto-generated file
 ****************************************************************************/
#include <dev/hpt27xx/os_bsd.h>

extern int init_module_him_rr2720(void);
extern int init_module_him_rr273x(void);
extern int init_module_him_rr276x(void);
extern int init_module_him_rr278x(void);
extern int init_module_vdev_raw(void);
extern int init_module_partition(void);
extern int init_module_raid0(void);
extern int init_module_raid1(void);
extern int init_module_raid5(void);
extern int init_module_jbod(void);

int init_config(void)
{
	init_module_him_rr2720();
	init_module_him_rr273x();
	init_module_him_rr276x();
	init_module_him_rr278x();
	init_module_vdev_raw();
	init_module_partition();
	init_module_raid0();
	init_module_raid1();
	init_module_raid5();
	init_module_jbod();
	return 0;
}

const char driver_name[] = "hpt27xx";
const char driver_name_long[] = "RocketRAID 27xx controller driver";
const char driver_ver[] = "v1.2.8";
int  osm_max_targets = 0xff;


int os_max_cache_size = 0x1000000;
