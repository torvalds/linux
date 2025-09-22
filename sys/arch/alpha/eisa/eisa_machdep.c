/* $OpenBSD: eisa_machdep.c,v 1.6 2015/09/02 14:07:43 deraadt Exp $ */
/* $NetBSD: eisa_machdep.c,v 1.1 2000/07/29 23:18:47 thorpej Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/queue.h>

#include <machine/intr.h>
#include <machine/rpb.h>

#include <dev/eisa/eisareg.h>
#include <dev/eisa/eisavar.h>

int	eisa_compute_maxslots(const char *);

#define	EISA_SLOT_HEADER_SIZE	31
#define	EISA_SLOT_INFO_OFFSET	20

#define	EISA_FUNC_INFO_OFFSET	34
#define	EISA_CONFIG_BLOCK_SIZE	320

#define	ECUF_TYPE_STRING	0x01
#define	ECUF_MEM_ENTRY		0x02
#define	ECUF_IRQ_ENTRY		0x04
#define	ECUF_DMA_ENTRY		0x08
#define	ECUF_IO_ENTRY		0x10
#define	ECUF_INIT_ENTRY		0x20
#define	ECUF_DISABLED		0x80

#define	ECUF_SELECTIONS_SIZE	26
#define	ECUF_TYPE_STRING_SIZE	80
#define	ECUF_MEM_ENTRY_SIZE	7
#define	ECUF_IRQ_ENTRY_SIZE	2
#define	ECUF_DMA_ENTRY_SIZE	2
#define	ECUF_IO_ENTRY_SIZE	3
#define	ECUF_INIT_ENTRY_SIZE	60

#define	ECUF_MEM_ENTRY_CNT	9
#define	ECUF_IRQ_ENTRY_CNT	7
#define	ECUF_DMA_ENTRY_CNT	4
#define	ECUF_IO_ENTRY_CNT	20

#define	CBUFSIZE		512

/*
 * EISA configuration space, as set up by the ECU, may be sparse.
 */
bus_size_t eisa_config_stride;
paddr_t eisa_config_addr;		/* defaults to 0 */
paddr_t eisa_config_header_addr;

struct ecu_mem {
	SIMPLEQ_ENTRY(ecu_mem) ecum_list;
	bus_addr_t ecum_addr;
	bus_size_t ecum_size;
	int	ecum_isram;
	int	ecum_decode;
	int	ecum_unitsize;
};

struct ecu_irq {
	SIMPLEQ_ENTRY(ecu_irq) ecui_list;
	int	ecui_irq;
	int	ecui_ist;
	int	ecui_shared;
};

struct ecu_dma {
	SIMPLEQ_ENTRY(ecu_dma) ecud_list;
	int	ecud_drq;
	int	ecud_shared;
	int	ecud_size;
#define	ECUD_SIZE_8BIT		0
#define	ECUD_SIZE_16BIT		1
#define	ECUD_SIZE_32BIT		2
#define	ECUD_SIZE_RESERVED	3
	int	ecud_timing;
#define	ECUD_TIMING_ISA		0
#define	ECUD_TIMING_TYPEA	1
#define	ECUD_TIMING_TYPEB	2
#define	ECUD_TIMING_TYPEC	3
};

struct ecu_io {
	SIMPLEQ_ENTRY(ecu_io) ecuio_list;
	bus_addr_t ecuio_addr;
	bus_size_t ecuio_size;
	int	ecuio_shared;
};

struct ecu_func {
	SIMPLEQ_ENTRY(ecu_func) ecuf_list;
	int ecuf_funcno;
	u_int32_t ecuf_id;
	u_int16_t ecuf_slot_info;
	u_int16_t ecuf_cfg_ext;
	u_int8_t ecuf_selections[ECUF_SELECTIONS_SIZE];
	u_int8_t ecuf_func_info;
	u_int8_t ecuf_type_string[ECUF_TYPE_STRING_SIZE];
	u_int8_t ecuf_init[ECUF_INIT_ENTRY_SIZE];
	SIMPLEQ_HEAD(, ecu_mem) ecuf_mem;
	SIMPLEQ_HEAD(, ecu_irq) ecuf_irq;
	SIMPLEQ_HEAD(, ecu_dma) ecuf_dma;
	SIMPLEQ_HEAD(, ecu_io) ecuf_io;
};

struct ecu_data {
	SIMPLEQ_ENTRY(ecu_data) ecud_list;
	int ecud_slot;
	u_int8_t ecud_eisaid[EISA_IDSTRINGLEN];
	u_int32_t ecud_offset;

	/* General slot info. */
	u_int8_t ecud_slot_info;
	u_int16_t ecud_ecu_major_rev;
	u_int16_t ecud_ecu_minor_rev;
	u_int16_t ecud_cksum;
	u_int16_t ecud_ndevfuncs;
	u_int8_t ecud_funcinfo;
	u_int32_t ecud_comp_id;

	/* The functions */
	SIMPLEQ_HEAD(, ecu_func) ecud_funcs;
};

SIMPLEQ_HEAD(, ecu_data) ecu_data_list =
    SIMPLEQ_HEAD_INITIALIZER(ecu_data_list);

static void
ecuf_init(struct ecu_func *ecuf)
{

	memset(ecuf, 0, sizeof(*ecuf));
	SIMPLEQ_INIT(&ecuf->ecuf_mem);
	SIMPLEQ_INIT(&ecuf->ecuf_irq);
	SIMPLEQ_INIT(&ecuf->ecuf_dma);
	SIMPLEQ_INIT(&ecuf->ecuf_io);
}

static void
eisa_parse_mem(struct ecu_func *ecuf, u_int8_t *dp)
{
	struct ecu_mem *ecum;
	int i;

	for (i = 0; i < ECUF_MEM_ENTRY_CNT; i++) {
		ecum = malloc(sizeof(*ecum), M_DEVBUF, M_ZERO|M_WAITOK);

		ecum->ecum_isram = dp[0] & 0x1;
		ecum->ecum_unitsize = dp[1] & 0x3;
		ecum->ecum_decode = (dp[1] >> 2) & 0x3;
		ecum->ecum_addr = (dp[2] | (dp[3] << 8) | (dp[4] << 16)) << 8;
		ecum->ecum_size = (dp[5] | (dp[6] << 8)) << 10;
		if (ecum->ecum_size == 0)
			ecum->ecum_size = (1 << 26);
		SIMPLEQ_INSERT_TAIL(&ecuf->ecuf_mem, ecum, ecum_list);

#ifdef EISA_DEBUG
		printf("MEM 0x%lx 0x%lx %d %d %d\n",
		    ecum->ecum_addr, ecum->ecum_size,
		    ecum->ecum_isram, ecum->ecum_unitsize,
		    ecum->ecum_decode);
#endif

		if ((dp[0] & 0x80) == 0)
			break;
		dp += ECUF_MEM_ENTRY_SIZE;
	}
}

static void
eisa_parse_irq(struct ecu_func *ecuf, u_int8_t *dp)
{
	struct ecu_irq *ecui;
	int i;

	for (i = 0; i < ECUF_IRQ_ENTRY_CNT; i++) {
		ecui = malloc(sizeof(*ecui), M_DEVBUF, M_ZERO|M_WAITOK);

		ecui->ecui_irq = dp[0] & 0xf;
		ecui->ecui_ist = (dp[0] & 0x20) ? IST_LEVEL : IST_EDGE;
		ecui->ecui_shared = (dp[0] & 0x40) ? 1 : 0;
		SIMPLEQ_INSERT_TAIL(&ecuf->ecuf_irq, ecui, ecui_list);

#ifdef EISA_DEBUG
		printf("IRQ %d %s%s\n", ecui->ecui_irq,
		    ecui->ecui_ist == IST_LEVEL ? "level" : "edge",
		    ecui->ecui_shared ? " shared" : "");
#endif

		if ((dp[0] & 0x80) == 0)
			break;
		dp += ECUF_IRQ_ENTRY_SIZE;
	}
}

static void
eisa_parse_dma(struct ecu_func *ecuf, u_int8_t *dp)
{
	struct ecu_dma *ecud;
	int i;

	for (i = 0; i < ECUF_DMA_ENTRY_CNT; i++) {
		ecud = malloc(sizeof(*ecud), M_DEVBUF, M_ZERO|M_WAITOK);

		ecud->ecud_drq = dp[0] & 0x7;
		ecud->ecud_shared = dp[0] & 0x40;
		ecud->ecud_size = (dp[1] >> 2) & 0x3;
		ecud->ecud_timing = (dp[1] >> 4) & 0x3;
		SIMPLEQ_INSERT_TAIL(&ecuf->ecuf_dma, ecud, ecud_list);

#ifdef EISA_DEBUG
		printf("DRQ %d%s %d %d\n", ecud->ecud_drq,
		    ecud->ecud_shared ? " shared" : "",
		    ecud->ecud_size, ecud->ecud_timing);
#endif

		if ((dp[0] & 0x80) == 0)
			break;
		dp += ECUF_DMA_ENTRY_SIZE;
	}
}

static void
eisa_parse_io(struct ecu_func *ecuf, u_int8_t *dp)
{
	struct ecu_io *ecuio;
	int i;

	for (i = 0; i < ECUF_IO_ENTRY_CNT; i++) {
		ecuio = malloc(sizeof(*ecuio), M_DEVBUF, M_ZERO|M_WAITOK);

		ecuio->ecuio_addr = dp[1] | (dp[2] << 8);
		ecuio->ecuio_size = (dp[0] & 0x1f) + 1;
		ecuio->ecuio_shared = (dp[0] & 0x40) ? 1 : 0;
		SIMPLEQ_INSERT_TAIL(&ecuf->ecuf_io, ecuio, ecuio_list);

#ifdef EISA_DEBUG
		printf("IO 0x%lx 0x%lx%s\n", ecuio->ecuio_addr,
		    ecuio->ecuio_size,
		    ecuio->ecuio_shared ? " shared" : "");
#endif

		if ((dp[0] & 0x80) == 0)
			break;
		dp += ECUF_IO_ENTRY_SIZE;
	}
}

static void
eisa_read_config_bytes(paddr_t addr, void *buf, size_t count)
{
	const u_int8_t *src = (const u_int8_t *)ALPHA_PHYS_TO_K0SEG(addr);
	u_int8_t *dst = buf;

	for (; count != 0; count--) {
		*dst++ = *src;
		src += eisa_config_stride;
	}
}

static void
eisa_read_config_word(paddr_t addr, u_int32_t *valp)
{
	const u_int8_t *src = (const u_int8_t *)ALPHA_PHYS_TO_K0SEG(addr);
	u_int32_t val = 0;
	int i;

	for (i = 0; i < sizeof(val); i++) {
		val |= (u_int32_t)*src << (i * 8);
		src += eisa_config_stride;
	}

	*valp = val;
}

static size_t
eisa_uncompress(void *cbufp, void *ucbufp, size_t count)
{
	const u_int8_t *cbuf = cbufp;
	u_int8_t *ucbuf = ucbufp;
	u_int zeros = 0;

	while (count--) {
		if (zeros) {
			zeros--;
			*ucbuf++ = '\0';
		} else if (*cbuf == '\0') {
			*ucbuf++ = *cbuf++;
			zeros = *cbuf++ - 1;
		} else
			*ucbuf++ = *cbuf++;
	}

	return ((size_t)cbuf - (size_t)cbufp);
}

void
eisa_init(eisa_chipset_tag_t ec)
{
	struct ecu_data *ecud;
	paddr_t cfgaddr;
	u_int32_t offset;
	u_int8_t eisaid[EISA_IDSTRINGLEN];
	u_int8_t *cdata, *data;
	u_int8_t *cdp, *dp;
	struct ecu_func *ecuf;
	int i, func;

	/*
	 * Locate EISA configuration space.
	 */
	if (hwrpb->rpb_condat_off == 0UL ||
	    (hwrpb->rpb_condat_off >> 63) != 0) {
		printf(": WARNING: no EISA configuration space");
		return;
	}

	if (eisa_config_header_addr) {
		printf("\n");
		panic("eisa_init: EISA config space already initialized");
	}

	eisa_config_header_addr = hwrpb->rpb_condat_off;
	if (eisa_config_stride == 0)
		eisa_config_stride = 1;

#ifdef EISA_DEBUG
	printf("\nEISA config header at 0x%lx\n", eisa_config_header_addr);
	printf("EISA config at %p\n", eisa_config_addr);
	printf("EISA config stride: %ld\n", eisa_config_stride);
#endif

	/*
	 * Read SLOT 0 (motherboard) id, and decide how many (logical)
	 * slots there are.
	 */
	eisa_read_config_bytes(eisa_config_header_addr, eisaid, sizeof(eisaid));
	eisaid[EISA_IDSTRINGLEN - 1] = '\0';	/* sanity */
	ec->ec_maxslots = eisa_compute_maxslots((const char *)eisaid);
	printf(": %s, %d slots", (const char *)eisaid, ec->ec_maxslots - 1);

	/*
	 * Read the slot headers, and allocate config structures for
	 * valid slots.
	 */
	for (cfgaddr = eisa_config_header_addr, i = 0;
	    i < eisa_maxslots(ec); i++) {
		eisa_read_config_bytes(cfgaddr, eisaid, sizeof(eisaid));
		eisaid[EISA_IDSTRINGLEN - 1] = '\0';	/* sanity */
		cfgaddr += sizeof(eisaid) * eisa_config_stride;
		eisa_read_config_word(cfgaddr, &offset);
		cfgaddr += sizeof(offset) * eisa_config_stride;

		if (offset != 0 && offset != 0xffffffff) {
#ifdef EISA_DEBUG
			printf("SLOT %d: offset 0x%08x eisaid %s\n",
			    i, offset, eisaid);
#endif
			ecud = malloc(sizeof(*ecud), M_DEVBUF, M_ZERO|M_WAITOK);

			SIMPLEQ_INIT(&ecud->ecud_funcs);

			ecud->ecud_slot = i;
			memcpy(ecud->ecud_eisaid, eisaid, sizeof(eisaid));
			ecud->ecud_offset = offset;
			SIMPLEQ_INSERT_TAIL(&ecu_data_list, ecud, ecud_list);
		}
	}

	/*
	 * Now traverse the valid slots and read the info.
	 */

	cdata = malloc(CBUFSIZE, M_TEMP, M_ZERO|M_WAITOK);
	
	data = malloc(CBUFSIZE, M_TEMP, M_ZERO|M_WAITOK);

	SIMPLEQ_FOREACH(ecud, &ecu_data_list, ecud_list) {
		cfgaddr = eisa_config_addr + ecud->ecud_offset;
#ifdef EISA_DEBUG
		printf("Checking SLOT %d\n", ecud->ecud_slot);
		printf("Reading config bytes at %p to cdata[0]\n", cfgaddr);
#endif
		eisa_read_config_bytes(cfgaddr, &cdata[0], 1);
		cfgaddr += eisa_config_stride;

		for (i = 1; i < CBUFSIZE; cfgaddr += eisa_config_stride, i++) {
#ifdef EISA_DEBUG
			printf("Reading config bytes at %p to cdata[%d]\n",
			    cfgaddr, i);
#endif
			eisa_read_config_bytes(cfgaddr, &cdata[i], 1);
			if (cdata[i - 1] == 0 && cdata[i] == 0)
				break;
		}
		if (i == CBUFSIZE) {
			/* assume this compressed data invalid */
#ifdef EISA_DEBUG
			printf("SLOT %d has invalid config\n", ecud->ecud_slot);
#endif
			continue;
		}

		i++;	/* index -> length */

#ifdef EISA_DEBUG
		printf("SLOT %d compressed data length %d:",
		    ecud->ecud_slot, i);
		{
			int j;

			for (j = 0; j < i; j++) {
				if ((j % 16) == 0)
					printf("\n");
				printf("0x%02x ", cdata[j]);
			}
			printf("\n");
		}
#endif

		cdp = cdata;
		dp = data;

		/* Uncompress the slot header. */
		cdp += eisa_uncompress(cdp, dp, EISA_SLOT_HEADER_SIZE);
#ifdef EISA_DEBUG
		printf("SLOT %d uncompressed header data:",
		    ecud->ecud_slot);
		{
			int j;

			for (j = 0; j < EISA_SLOT_HEADER_SIZE; j++) {
				if ((j % 16) == 0)
					printf("\n");
				printf("0x%02x ", dp[j]);
			}
			printf("\n");
		}
#endif

		dp = &data[EISA_SLOT_INFO_OFFSET];
		ecud->ecud_slot_info = *dp++;
		ecud->ecud_ecu_major_rev = *dp++;
		ecud->ecud_ecu_minor_rev = *dp++;
		memcpy(&ecud->ecud_cksum, dp, sizeof(ecud->ecud_cksum));
		dp += sizeof(ecud->ecud_cksum);
		ecud->ecud_ndevfuncs = *dp++;
		ecud->ecud_funcinfo = *dp++;
		memcpy(&ecud->ecud_comp_id, dp, sizeof(ecud->ecud_comp_id));
		dp += sizeof(ecud->ecud_comp_id);

#ifdef EISA_DEBUG
		printf("SLOT %d: ndevfuncs %d\n", ecud->ecud_slot,
		    ecud->ecud_ndevfuncs);
#endif

		for (func = 0; func < ecud->ecud_ndevfuncs; func++) {
			dp = data;
			cdp += eisa_uncompress(cdp, dp, EISA_CONFIG_BLOCK_SIZE);
#ifdef EISA_DEBUG
			printf("SLOT %d:%d uncompressed data:",
			    ecud->ecud_slot, func);
			{
				int j;

				for (j = 0; i < EISA_CONFIG_BLOCK_SIZE; j++) {
					if ((j % 16) == 0)
						printf("\n");
					printf("0x%02x ", dp[j]);
				}
				printf("\n");
			}
#endif

			/* Skip disabled functions. */
			if (dp[EISA_FUNC_INFO_OFFSET] & ECUF_DISABLED) {
#ifdef EISA_DEBUG
				printf("SLOT %d:%d disabled\n",
				    ecud->ecud_slot, func);
#endif
				continue;
			}
#ifdef EISA_DEBUG
			else
				printf("SLOT %d:%d settings\n",
				    ecud->ecud_slot, func);
#endif

			ecuf = malloc(sizeof(*ecuf), M_DEVBUF, M_WAITOK);
			
			ecuf_init(ecuf);
			ecuf->ecuf_funcno = func;
			SIMPLEQ_INSERT_TAIL(&ecud->ecud_funcs, ecuf,
			    ecuf_list);

			memcpy(&ecuf->ecuf_id, dp, sizeof(ecuf->ecuf_id));
			dp += sizeof(ecuf->ecuf_id);

			memcpy(&ecuf->ecuf_slot_info, dp,
			    sizeof(ecuf->ecuf_slot_info));
			dp += sizeof(ecuf->ecuf_slot_info);

			memcpy(&ecuf->ecuf_cfg_ext, dp,
			    sizeof(ecuf->ecuf_cfg_ext));
			dp += sizeof(ecuf->ecuf_cfg_ext);

			memcpy(&ecuf->ecuf_selections, dp,
			    sizeof(ecuf->ecuf_selections));
			dp += sizeof(ecuf->ecuf_selections);

			memcpy(&ecuf->ecuf_func_info, dp,
			    sizeof(ecuf->ecuf_func_info));
			dp += sizeof(ecuf->ecuf_func_info);

			if (ecuf->ecuf_func_info & ECUF_TYPE_STRING)
				memcpy(ecuf->ecuf_type_string, dp,
				    sizeof(ecuf->ecuf_type_string));
			dp += sizeof(ecuf->ecuf_type_string);

			if (ecuf->ecuf_func_info & ECUF_MEM_ENTRY)
				eisa_parse_mem(ecuf, dp);
			dp += ECUF_MEM_ENTRY_SIZE * ECUF_MEM_ENTRY_CNT;

			if (ecuf->ecuf_func_info & ECUF_IRQ_ENTRY)
				eisa_parse_irq(ecuf, dp);
			dp += ECUF_IRQ_ENTRY_SIZE * ECUF_IRQ_ENTRY_CNT;

			if (ecuf->ecuf_func_info & ECUF_DMA_ENTRY)
				eisa_parse_dma(ecuf, dp);
			dp += ECUF_DMA_ENTRY_SIZE * ECUF_DMA_ENTRY_CNT;

			if (ecuf->ecuf_func_info & ECUF_IO_ENTRY)
				eisa_parse_io(ecuf, dp);
			dp += ECUF_IO_ENTRY_SIZE * ECUF_IO_ENTRY_CNT;

			if (ecuf->ecuf_func_info & ECUF_INIT_ENTRY)
				memcpy(ecuf->ecuf_init, dp,
				    sizeof(ecuf->ecuf_init));
			dp += sizeof(ecuf->ecuf_init);
		}
	}

	free(cdata, M_TEMP, CBUFSIZE);
	free(data, M_TEMP, CBUFSIZE);
}

/*
 * Return the number of logical slots a motherboard supports,
 * from its signature.
 */
int
eisa_compute_maxslots(const char *idstring)
{
	int nslots;

	if (strcmp(idstring, "DEC2400") == 0)		/* Jensen */
		nslots = 1 + 6;
	else if (strcmp(idstring, "DEC2A01") == 0)	/* AS 2000/2100 */
		nslots = 1 + 8;
	else if (strcmp(idstring, "DEC5000") == 0)	/* AS 1000/600A */
		nslots = 1 + 8;
	else if (strcmp(idstring, "DEC5100") == 0)	/* AS 600 */
		nslots = 1 + 4;
	else if (strcmp(idstring, "DEC5301") == 0)	/* AS 800 */
		nslots = 1 + 3;
	else if (strcmp(idstring, "DEC6000") == 0)	/* AS 8200/8400 */
		nslots = 1 + 8;
	else if (strcmp(idstring, "DEC6400") == 0)	/* AS 4x00/1200 */
		nslots = 1 + 3;
	else {
		/*
		 * Unrecognized design. Not likely to happen, since
		 * Digital ECU will not recognize it either.
		 * But just in case the EISA configuration data badly
		 * fooled us, return the largest possible value.
		 */
		nslots = 1 + 8;
	}

	return nslots;
}
