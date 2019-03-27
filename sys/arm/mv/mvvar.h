/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002, 2003 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
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
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * from: FreeBSD: //depot/projects/arm/src/sys/arm/xscale/pxa2x0/pxa2x0var.h, rev 1
 *
 * $FreeBSD$
 */

#ifndef _MVVAR_H_
#define _MVVAR_H_

#include <sys/rman.h>
#include <machine/bus.h>
#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/ofw/openfirm.h>

#define	MV_TYPE_PCI		0
#define	MV_TYPE_PCIE		1

#define MV_MODE_ENDPOINT	0
#define MV_MODE_ROOT		1

enum soc_family{
	MV_SOC_ARMADA_38X 	= 0x00,
	MV_SOC_ARMADA_XP	= 0x01,
	MV_SOC_ARMV5		= 0x02,
	MV_SOC_UNSUPPORTED	= 0xff,
};

struct gpio_config {
	int		gc_gpio;	/* GPIO number */
	uint32_t	gc_flags;	/* GPIO flags */
	int		gc_output;	/* GPIO output value */
};

struct decode_win {
	int		target;		/* Mbus unit ID */
	int		attr;		/* Attributes of the target interface */
	vm_paddr_t	base;		/* Physical base addr */
	uint32_t	size;
	vm_paddr_t	remap;
};

extern const struct gpio_config mv_gpio_config[];
extern const struct decode_win *cpu_wins;
extern const struct decode_win *idma_wins;
extern const struct decode_win *xor_wins;
extern int idma_wins_no;
extern int xor_wins_no;

int soc_decode_win(void);
void soc_id(uint32_t *dev, uint32_t *rev);
void soc_dump_decode_win(void);
uint32_t soc_power_ctrl_get(uint32_t mask);
void soc_power_ctrl_set(uint32_t mask);

int decode_win_cpu_set(int target, int attr, vm_paddr_t base, uint32_t size,
    vm_paddr_t remap);
int decode_win_overlap(int, int, const struct decode_win *);
int win_cpu_can_remap(int);
void decode_win_pcie_setup(u_long);

void ddr_disable(int i);
int ddr_is_active(int i);
uint32_t ddr_base(int i);
uint32_t ddr_size(int i);
uint32_t ddr_attr(int i);
uint32_t ddr_target(int i);

uint32_t cpu_extra_feat(void);
uint32_t get_tclk(void);
uint32_t get_cpu_freq(void);
uint32_t get_l2clk(void);
uint32_t read_cpu_ctrl(uint32_t);
void write_cpu_ctrl(uint32_t, uint32_t);

uint32_t read_cpu_mp_clocks(uint32_t reg);
void write_cpu_mp_clocks(uint32_t reg, uint32_t val);
uint32_t read_cpu_misc(uint32_t reg);
void write_cpu_misc(uint32_t reg, uint32_t val);

int mv_pcib_bar_win_set(device_t dev, uint32_t base, uint32_t size,
    uint32_t remap, int winno, int busno);
int mv_pcib_cpu_win_remap(device_t dev, uint32_t remap, uint32_t size);

void mv_mask_endpoint_irq(uintptr_t nb, int unit);
void mv_unmask_endpoint_irq(uintptr_t nb, int unit);

int	mv_drbl_get_next_irq(int dir, int unit);
void	mv_drbl_mask_all(int unit);
void	mv_drbl_mask_irq(uint32_t irq, int dir, int unit);
void	mv_drbl_unmask_irq(uint32_t irq, int dir, int unit);
void	mv_drbl_set_mask(uint32_t val, int dir, int unit);
uint32_t mv_drbl_get_mask(int dir, int unit);
void	mv_drbl_set_cause(uint32_t val, int dir, int unit);
uint32_t mv_drbl_get_cause(int dir, int unit);
void	mv_drbl_set_msg(uint32_t val, int mnr, int dir, int unit);
uint32_t mv_drbl_get_msg(int mnr, int dir, int unit);

int	mv_msi_data(int irq, uint64_t *addr, uint32_t *data);

struct devmap_entry;

int mv_pci_devmap(phandle_t, struct devmap_entry *, vm_offset_t,
    vm_offset_t);
int fdt_localbus_devmap(phandle_t, struct devmap_entry *, int, int *);
enum soc_family mv_check_soc_family(void);

int mv_fdt_is_type(phandle_t, const char *);
int mv_fdt_pm(phandle_t);

uint32_t get_tclk_armadaxp(void);
uint32_t get_tclk_armada38x(void);
uint32_t get_cpu_freq_armadaxp(void);
uint32_t get_cpu_freq_armada38x(void);
#endif /* _MVVAR_H_ */
