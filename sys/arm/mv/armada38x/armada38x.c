/*-
 * Copyright (c) 2015 Semihalf.
 * Copyright (c) 2015 Stormshield.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <machine/fdt.h>

#include <arm/mv/mvwin.h>
#include <arm/mv/mvreg.h>
#include <arm/mv/mvvar.h>

int armada38x_open_bootrom_win(void);
int armada38x_scu_enable(void);
int armada38x_win_set_iosync_barrier(void);
int armada38x_mbus_optimization(void);
static uint64_t get_sar_value_armada38x(void);

static int hw_clockrate;
SYSCTL_INT(_hw, OID_AUTO, clockrate, CTLFLAG_RD,
    &hw_clockrate, 0, "CPU instruction clock rate");

static uint64_t
get_sar_value_armada38x(void)
{
	uint32_t sar_low, sar_high;

	sar_high = 0;
	sar_low = bus_space_read_4(fdtbus_bs_tag, MV_MISC_BASE,
	    SAMPLE_AT_RESET_ARMADA38X);
	return (((uint64_t)sar_high << 32) | sar_low);
}

uint32_t
get_tclk_armada38x(void)
{
	uint32_t sar;

	/*
	 * On Armada38x TCLK can be configured to 250 MHz or 200 MHz.
	 * Current setting is read from Sample At Reset register.
	 */
	sar = (uint32_t)get_sar_value_armada38x();
	sar = (sar & TCLK_MASK_ARMADA38X) >> TCLK_SHIFT_ARMADA38X;
	if (sar == 0)
		return (TCLK_250MHZ);
	else
		return (TCLK_200MHZ);
}

uint32_t
get_cpu_freq_armada38x(void)
{
	uint32_t sar;

	static const uint32_t cpu_frequencies[] = {
		0, 0, 0, 0,
		1066, 0, 0, 0,
		1332, 0, 0, 0,
		1600, 0, 0, 0,
		1866, 0, 0, 2000
	};

	sar = (uint32_t)get_sar_value_armada38x();
	sar = (sar & A38X_CPU_DDR_CLK_MASK) >> A38X_CPU_DDR_CLK_SHIFT;
	if (sar >= nitems(cpu_frequencies))
		return (0);

	hw_clockrate = cpu_frequencies[sar];

	return (hw_clockrate * 1000 * 1000);
}

int
armada38x_win_set_iosync_barrier(void)
{
	bus_space_handle_t vaddr_iowind;
	int rv;

	rv = bus_space_map(fdtbus_bs_tag, (bus_addr_t)MV_MBUS_BRIDGE_BASE,
	    MV_CPU_SUBSYS_REGS_LEN, 0, &vaddr_iowind);
	if (rv != 0)
		return (rv);

	/* Set Sync Barrier flags for all Mbus internal units */
	bus_space_write_4(fdtbus_bs_tag, vaddr_iowind, MV_SYNC_BARRIER_CTRL,
	    MV_SYNC_BARRIER_CTRL_ALL);

	bus_space_barrier(fdtbus_bs_tag, vaddr_iowind, 0,
	    MV_CPU_SUBSYS_REGS_LEN, BUS_SPACE_BARRIER_WRITE);
	bus_space_unmap(fdtbus_bs_tag, vaddr_iowind, MV_CPU_SUBSYS_REGS_LEN);

	return (rv);
}

int
armada38x_open_bootrom_win(void)
{
	bus_space_handle_t vaddr_iowind;
	uint32_t val;
	int rv;

	rv = bus_space_map(fdtbus_bs_tag, (bus_addr_t)MV_MBUS_BRIDGE_BASE,
	    MV_CPU_SUBSYS_REGS_LEN, 0, &vaddr_iowind);
	if (rv != 0)
		return (rv);

	val = (MV_BOOTROM_WIN_SIZE & IO_WIN_SIZE_MASK) << IO_WIN_SIZE_SHIFT;
	val |= (MBUS_BOOTROM_ATTR & IO_WIN_ATTR_MASK) << IO_WIN_ATTR_SHIFT;
	val |= (MBUS_BOOTROM_TGT_ID & IO_WIN_TGT_MASK) << IO_WIN_TGT_SHIFT;
	/* Enable window and Sync Barrier */
	val |= (0x1 & IO_WIN_SYNC_MASK) << IO_WIN_SYNC_SHIFT;
	val |= (0x1 & IO_WIN_ENA_MASK) << IO_WIN_ENA_SHIFT;

	/* Configure IO Window Control Register */
	bus_space_write_4(fdtbus_bs_tag, vaddr_iowind, IO_WIN_9_CTRL_OFFSET,
	    val);
	/* Configure IO Window Base Register */
	bus_space_write_4(fdtbus_bs_tag, vaddr_iowind, IO_WIN_9_BASE_OFFSET,
	    MV_BOOTROM_MEM_ADDR);

	bus_space_barrier(fdtbus_bs_tag, vaddr_iowind, 0, MV_CPU_SUBSYS_REGS_LEN,
	    BUS_SPACE_BARRIER_WRITE);
	bus_space_unmap(fdtbus_bs_tag, vaddr_iowind, MV_CPU_SUBSYS_REGS_LEN);

	return (rv);
}

int
armada38x_mbus_optimization(void)
{
	bus_space_handle_t vaddr_iowind;
	int rv;

	rv = bus_space_map(fdtbus_bs_tag, (bus_addr_t)MV_MBUS_CTRL_BASE,
	    MV_MBUS_CTRL_REGS_LEN, 0, &vaddr_iowind);
	if (rv != 0)
		return (rv);

	/*
	 * MBUS Units Priority Control Register - Prioritize XOR,
	 * PCIe and GbEs (ID=4,6,3,7,8) DRAM access
	 * GbE is High and others are Medium.
	 */
	bus_space_write_4(fdtbus_bs_tag, vaddr_iowind, 0, 0x19180);

	/*
	 * Fabric Units Priority Control Register -
	 * Prioritize CPUs requests.
	 */
	bus_space_write_4(fdtbus_bs_tag, vaddr_iowind, 0x4, 0x3000A);

	/*
	 * MBUS Units Prefetch Control Register -
	 * Pre-fetch enable for all IO masters.
	 */
	bus_space_write_4(fdtbus_bs_tag, vaddr_iowind, 0x8, 0xFFFF);

	/*
	 * Fabric Units Prefetch Control Register -
	 * Enable the CPUs Instruction and Data prefetch.
	 */
	bus_space_write_4(fdtbus_bs_tag, vaddr_iowind, 0xc, 0x303);

	bus_space_barrier(fdtbus_bs_tag, vaddr_iowind, 0, MV_MBUS_CTRL_REGS_LEN,
	    BUS_SPACE_BARRIER_WRITE);

	bus_space_unmap(fdtbus_bs_tag, vaddr_iowind, MV_MBUS_CTRL_REGS_LEN);

	return (rv);
}

int
armada38x_scu_enable(void)
{
	bus_space_handle_t vaddr_scu;
	int rv;
	uint32_t val;

	rv = bus_space_map(fdtbus_bs_tag, (bus_addr_t)MV_SCU_BASE,
	    MV_SCU_REGS_LEN, 0, &vaddr_scu);
	if (rv != 0)
		return (rv);

	/* Enable SCU */
	val = bus_space_read_4(fdtbus_bs_tag, vaddr_scu, MV_SCU_REG_CTRL);
	if (!(val & MV_SCU_ENABLE)) {
		/* Enable SCU Speculative linefills to L2 */
		val |= MV_SCU_SL_L2_ENABLE;

		bus_space_write_4(fdtbus_bs_tag, vaddr_scu, 0,
		    val | MV_SCU_ENABLE);
	}

	bus_space_unmap(fdtbus_bs_tag, vaddr_scu, MV_SCU_REGS_LEN);
	return (0);
}
