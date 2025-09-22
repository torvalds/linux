/*	$OpenBSD: shpcicvar.h,v 1.7 2024/05/22 14:25:47 jsg Exp $	*/
/*	$NetBSD: shpcicvar.h,v 1.6 2005/12/11 12:18:58 christos Exp $	*/

/*-
 * Copyright (c) 2005 NONAKA Kimihiro
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef SH_DEV_PCICVAR_H
#define SH_DEV_PCICVAR_H

#include <machine/bus.h>

bus_space_tag_t shpcic_get_bus_io_tag(void);
bus_space_tag_t shpcic_get_bus_mem_tag(void);
bus_dma_tag_t shpcic_get_bus_dma_tag(void);

int shpcic_bus_maxdevs(void *v, int busno);
pcitag_t shpcic_make_tag(void *v, int bus, int device, int function);
void shpcic_decompose_tag(void *v, pcitag_t tag, int *bp, int *dp, int *fp);
int shpcic_conf_size(void *, pcitag_t);
pcireg_t shpcic_conf_read(void *v, pcitag_t tag, int reg);
void shpcic_conf_write(void *v, pcitag_t tag, int reg, pcireg_t data);

struct config_bus_space {
        u_int32_t bus_base;
        u_int32_t bus_size;
        int bus_io;
};

struct shpcic_softc {
        struct device s_dev;

	pci_chipset_tag_t sc_pci_chipset;

        /* Structures to do bus fixup */
        int nbogus;
        struct extent *extent_mem;
        struct extent *extent_port;
        struct config_bus_space sc_membus_space;
        struct config_bus_space sc_iobus_space;
};

/*
 * shpcic io/mem bus space
 */
int shpcic_iomem_map(void *v, bus_addr_t bpa, bus_size_t size, int flags,
    bus_space_handle_t *bshp);
void shpcic_iomem_unmap(void *v, bus_space_handle_t bsh, bus_size_t size);
int shpcic_iomem_subregion(void *v, bus_space_handle_t bsh, bus_size_t offset,
    bus_size_t size, bus_space_handle_t *nbshp);
int shpcic_iomem_alloc(void *v, bus_addr_t rstart, bus_addr_t rend,
    bus_size_t size, bus_size_t alignment, bus_size_t boundary, int flags,
    bus_addr_t *bpap, bus_space_handle_t *bshp);
void shpcic_iomem_free(void *v, bus_space_handle_t bsh, bus_size_t size);
void *shpcic_iomem_vaddr(void *v, bus_space_handle_t bsh);

/* read single */
uint8_t shpcic_io_read_1(void *v, bus_space_handle_t bsh, bus_size_t offset);
uint16_t shpcic_io_read_2(void *v, bus_space_handle_t bsh, bus_size_t offset);
uint32_t shpcic_io_read_4(void *v, bus_space_handle_t bsh, bus_size_t offset);
uint8_t shpcic_mem_read_1(void *v, bus_space_handle_t bsh, bus_size_t offset);
uint16_t shpcic_mem_read_2(void *v, bus_space_handle_t bsh, bus_size_t offset);
uint32_t shpcic_mem_read_4(void *v, bus_space_handle_t bsh, bus_size_t offset);

/* read multi */
void shpcic_io_read_multi_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count);
void shpcic_io_read_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t *addr, bus_size_t count);
void shpcic_io_read_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t *addr, bus_size_t count);
void shpcic_mem_read_multi_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count);
void shpcic_mem_read_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t *addr, bus_size_t count);
void shpcic_mem_read_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t *addr, bus_size_t count);

/* read raw multi */
void shpcic_io_read_raw_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count);
void shpcic_io_read_raw_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count);
void shpcic_mem_read_raw_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count);
void shpcic_mem_read_raw_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count);

/* read region */
void shpcic_io_read_region_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count);
void shpcic_io_read_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t *addr, bus_size_t count);
void shpcic_io_read_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t *addr, bus_size_t count);
void shpcic_mem_read_region_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count);
void shpcic_mem_read_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t *addr, bus_size_t count);
void shpcic_mem_read_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t *addr, bus_size_t count);

/* read raw region */
void shpcic_io_read_raw_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count);
void shpcic_io_read_raw_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count);
void shpcic_mem_read_raw_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count);
void shpcic_mem_read_raw_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t *addr, bus_size_t count);

/* write single */
void shpcic_io_write_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t data);
void shpcic_io_write_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t data);
void shpcic_io_write_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t data);
void shpcic_mem_write_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t data);
void shpcic_mem_write_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t data);
void shpcic_mem_write_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t data);

/* write multi */
void shpcic_io_write_multi_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count);
void shpcic_io_write_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint16_t *addr, bus_size_t count);
void shpcic_io_write_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint32_t *addr, bus_size_t count);
void shpcic_mem_write_multi_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count);
void shpcic_mem_write_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint16_t *addr, bus_size_t count);
void shpcic_mem_write_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint32_t *addr, bus_size_t count);

/* write raw multi */
void shpcic_io_write_raw_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count);
void shpcic_io_write_raw_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count);
void shpcic_mem_write_raw_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count);
void shpcic_mem_write_raw_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count);

/* write region */
void shpcic_io_write_region_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count);
void shpcic_io_write_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint16_t *addr, bus_size_t count);
void shpcic_io_write_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint32_t *addr, bus_size_t count);
void shpcic_mem_write_region_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count);
void shpcic_mem_write_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint16_t *addr, bus_size_t count);
void shpcic_mem_write_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint32_t *addr, bus_size_t count);

/* write raw region */
void shpcic_io_write_raw_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count);
void shpcic_io_write_raw_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count);
void shpcic_mem_write_raw_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count);
void shpcic_mem_write_raw_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, const uint8_t *addr, bus_size_t count);

/* set multi */
void shpcic_io_set_multi_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t val, bus_size_t count);
void shpcic_io_set_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t val, bus_size_t count);
void shpcic_io_set_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t val, bus_size_t count);
void shpcic_mem_set_multi_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t val, bus_size_t count);
void shpcic_mem_set_multi_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t val, bus_size_t count);
void shpcic_mem_set_multi_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t val, bus_size_t count);

/* set region */
void shpcic_io_set_region_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t val, bus_size_t count);
void shpcic_io_set_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t val, bus_size_t count);
void shpcic_io_set_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t val, bus_size_t count);
void shpcic_mem_set_region_1(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint8_t val, bus_size_t count);
void shpcic_mem_set_region_2(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint16_t val, bus_size_t count);
void shpcic_mem_set_region_4(void *v, bus_space_handle_t bsh,
    bus_size_t offset, uint32_t val, bus_size_t count);

/* copy region */
void shpcic_io_copy_1(void *v, bus_space_handle_t bsh1,
    bus_size_t off1, bus_space_handle_t bsh2, bus_size_t off2,
    bus_size_t count);
void shpcic_io_copy_2(void *v, bus_space_handle_t bsh1,
    bus_size_t off1, bus_space_handle_t bsh2, bus_size_t off2,
    bus_size_t count);
void shpcic_io_copy_4(void *v, bus_space_handle_t bsh1,
    bus_size_t off1, bus_space_handle_t bsh2, bus_size_t off2,
    bus_size_t count);
void shpcic_mem_copy_1(void *v, bus_space_handle_t bsh1,
    bus_size_t off1, bus_space_handle_t bsh2, bus_size_t off2,
    bus_size_t count);
void shpcic_mem_copy_2(void *v, bus_space_handle_t bsh1,
    bus_size_t off1, bus_space_handle_t bsh2, bus_size_t off2,
    bus_size_t count);
void shpcic_mem_copy_4(void *v, bus_space_handle_t bsh1,
    bus_size_t off1, bus_space_handle_t bsh2, bus_size_t off2,
    bus_size_t count);
#endif /* SH_DEV_PCICVAR_H */
