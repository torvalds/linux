/*-
 * Copyright (c) 2010 Aleksandr Rybalko.
 * Copyright (c) 2016 Michael Zhilin.
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


#ifndef NVRAM2ENV_NVRAM2ENV_H_
#define	NVRAM2ENV_NVRAM2ENV_H_

#define	nvram2env_read_1(sc, reg)				\
	bus_space_read_1((sc)->sc_bt, (sc)->sc_bh,(reg))

#define	nvram2env_read_2(sc, reg)				\
	bus_space_read_2((sc)->sc_bt, (sc)->sc_bh,(reg))

#define	nvram2env_read_4(sc, reg)				\
	bus_space_read_4((sc)->sc_bt, (sc)->sc_bh,(reg))

#define	nvram2env_write_1(sc, reg, val)			\
	bus_space_write_1((sc)->sc_bt, (sc)->sc_bh,	\
			 (reg), (val))

#define	nvram2env_write_2(sc, reg, val)			\
	bus_space_write_2((sc)->sc_bt, (sc)->sc_bh,	\
			 (reg), (val))

#define	nvram2env_write_4(sc, reg, val)			\
	bus_space_write_4((sc)->sc_bt, (sc)->sc_bh,	\
			 (reg), (val))

struct nvram2env_softc {
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	bus_addr_t addr;
	int need_swap;
	uint32_t sig;
	uint32_t flags;
#define	NVRAM_FLAGS_NOCHECK	0x0001	/* Do not check(CRC or somthing else)*/
#define	NVRAM_FLAGS_GENERIC	0x0002	/* Format Generic, skip 4b and read */
#define	NVRAM_FLAGS_BROADCOM	0x0004	/* Format Broadcom, use struct nvram */
#define	NVRAM_FLAGS_UBOOT	0x0008	/* Format Generic, skip 4b of CRC and read */
	uint32_t maxsize;
	uint32_t crc;
};

#define	NVRAM_MAX_SIZE	0x10000
#define	CFE_NVRAM_SIGNATURE 0x48534c46

struct nvram {
	u_int32_t sig;
	u_int32_t size;
	u_int32_t unknown1;
	u_int32_t unknown2;
	u_int32_t unknown3;
	char data[];
};

int		nvram2env_attach(device_t);
int		nvram2env_probe(device_t);

extern devclass_t	nvram2env_devclass;
extern driver_t		nvram2env_driver;

#endif /* SYS_DEV_NVRAM2ENV_NVRAM2ENV_H_ */
