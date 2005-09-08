/*
 * linux/include/asm/arch-omap/pm.h
 *
 * Header file for OMAP Power Management Routines
 *
 * Author: MontaVista Software, Inc.
 *	   support@mvista.com
 *
 * Copyright 2002 MontaVista Software Inc.
 *
 * Cleanup 2004 for Linux 2.6 by Dirk Behme <dirk.behme@de.bosch.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ASM_ARCH_OMAP_PM_H
#define __ASM_ARCH_OMAP_PM_H

/*
 * ----------------------------------------------------------------------------
 * Register and offset definitions to be used in PM assembler code
 * ----------------------------------------------------------------------------
 */
#define CLKGEN_REG_ASM_BASE		io_p2v(0xfffece00)
#define ARM_IDLECT1_ASM_OFFSET		0x04
#define ARM_IDLECT2_ASM_OFFSET		0x08

#define TCMIF_ASM_BASE			io_p2v(0xfffecc00)
#define EMIFS_CONFIG_ASM_OFFSET		0x0c
#define EMIFF_SDRAM_CONFIG_ASM_OFFSET	0x20

/*
 * ----------------------------------------------------------------------------
 * Powermanagement bitmasks
 * ----------------------------------------------------------------------------
 */
#define IDLE_WAIT_CYCLES		0x00000fff
#define PERIPHERAL_ENABLE		0x2

#define SELF_REFRESH_MODE		0x0c000001
#define IDLE_EMIFS_REQUEST		0xc
#define MODEM_32K_EN			0x1
#define PER_EN				0x1

#define CPU_SUSPEND_SIZE		200
#define ULPD_LOW_PWR_EN			0x0001
#define ULPD_DEEP_SLEEP_TRANSITION_EN	0x0010
#define ULPD_SETUP_ANALOG_CELL_3_VAL	0
#define ULPD_POWER_CTRL_REG_VAL		0x0219

#define DSP_IDLE_DELAY			10
#define DSP_IDLE			0x0040
#define DSP_RST				0x0004
#define DSP_ENABLE			0x0002
#define SUFFICIENT_DSP_RESET_TIME	1000
#define DEFAULT_MPUI_CONFIG		0x05cf
#define ENABLE_XORCLK			0x2
#define DSP_CLOCK_ENABLE		0x2000
#define DSP_IDLE_MODE			0x2
#define TC_IDLE_REQUEST			(0x0000000c)

#define IRQ_LEVEL2			(1<<0)
#define IRQ_KEYBOARD			(1<<1)
#define IRQ_UART2			(1<<15)

#define PDE_BIT				0x08
#define PWD_EN_BIT			0x04
#define EN_PERCK_BIT			0x04

#define OMAP1510_DEEP_SLEEP_REQUEST	0x0ec7
#define OMAP1510_BIG_SLEEP_REQUEST	0x0cc5
#define OMAP1510_IDLE_LOOP_REQUEST	0x0c00
#define OMAP1510_IDLE_CLOCK_DOMAINS	0x2

/* Both big sleep and deep sleep use same values. Difference is in ULPD. */
#define OMAP1610_IDLECT1_SLEEP_VAL	0x13c7
#define OMAP1610_IDLECT2_SLEEP_VAL	0x09c7
#define OMAP1610_IDLECT3_VAL		0x3f
#define OMAP1610_IDLECT3_SLEEP_ORMASK	0x2c
#define OMAP1610_IDLECT3		0xfffece24
#define OMAP1610_IDLE_LOOP_REQUEST	0x0400

#if     !defined(CONFIG_ARCH_OMAP1510) && \
	!defined(CONFIG_ARCH_OMAP16XX) && \
	!defined(CONFIG_ARCH_OMAP24XX)
#error "Power management for this processor not implemented yet"
#endif

#ifndef __ASSEMBLER__
extern void omap_pm_idle(void);
extern void omap_pm_suspend(void);
extern void omap1510_cpu_suspend(unsigned short, unsigned short);
extern void omap1610_cpu_suspend(unsigned short, unsigned short);
extern void omap1510_idle_loop_suspend(void);
extern void omap1610_idle_loop_suspend(void);

#ifdef CONFIG_OMAP_SERIAL_WAKE
extern void omap_serial_wake_trigger(int enable);
#else
#define omap_serial_wake_trigger(x)	{}
#endif	/* CONFIG_OMAP_SERIAL_WAKE */

extern unsigned int omap1510_cpu_suspend_sz;
extern unsigned int omap1510_idle_loop_suspend_sz;
extern unsigned int omap1610_cpu_suspend_sz;
extern unsigned int omap1610_idle_loop_suspend_sz;

#define ARM_SAVE(x) arm_sleep_save[ARM_SLEEP_SAVE_##x] = omap_readl(x)
#define ARM_RESTORE(x) omap_writel((arm_sleep_save[ARM_SLEEP_SAVE_##x]), (x))
#define ARM_SHOW(x) arm_sleep_save[ARM_SLEEP_SAVE_##x]

#define ULPD_SAVE(x) ulpd_sleep_save[ULPD_SLEEP_SAVE_##x] = omap_readw(x)
#define ULPD_RESTORE(x) omap_writew((ulpd_sleep_save[ULPD_SLEEP_SAVE_##x]), (x))
#define ULPD_SHOW(x) ulpd_sleep_save[ULPD_SLEEP_SAVE_##x]

#define MPUI1510_SAVE(x) mpui1510_sleep_save[MPUI1510_SLEEP_SAVE_##x] = omap_readl(x)
#define MPUI1510_RESTORE(x) omap_writel((mpui1510_sleep_save[MPUI1510_SLEEP_SAVE_##x]), (x))
#define MPUI1510_SHOW(x) mpui1510_sleep_save[MPUI1510_SLEEP_SAVE_##x]

#define MPUI1610_SAVE(x) mpui1610_sleep_save[MPUI1610_SLEEP_SAVE_##x] = omap_readl(x)
#define MPUI1610_RESTORE(x) omap_writel((mpui1610_sleep_save[MPUI1610_SLEEP_SAVE_##x]), (x))
#define MPUI1610_SHOW(x) mpui1610_sleep_save[MPUI1610_SLEEP_SAVE_##x]

/*
 * List of global OMAP registers to preserve.
 * More ones like CP and general purpose register values are preserved
 * with the stack pointer in sleep.S.
 */

enum arm_save_state {
	ARM_SLEEP_SAVE_START = 0,
	/*
	 * MPU control registers 32 bits
	 */
	ARM_SLEEP_SAVE_ARM_CKCTL,
	ARM_SLEEP_SAVE_ARM_IDLECT1,
	ARM_SLEEP_SAVE_ARM_IDLECT2,
	ARM_SLEEP_SAVE_ARM_IDLECT3,
	ARM_SLEEP_SAVE_ARM_EWUPCT,
	ARM_SLEEP_SAVE_ARM_RSTCT1,
	ARM_SLEEP_SAVE_ARM_RSTCT2,
	ARM_SLEEP_SAVE_ARM_SYSST,
	ARM_SLEEP_SAVE_SIZE
};

enum ulpd_save_state {
	ULPD_SLEEP_SAVE_START = 0,
	/*
	 * ULPD registers 16 bits
	 */
	ULPD_SLEEP_SAVE_ULPD_IT_STATUS,
	ULPD_SLEEP_SAVE_ULPD_CLOCK_CTRL,
	ULPD_SLEEP_SAVE_ULPD_SOFT_REQ,
	ULPD_SLEEP_SAVE_ULPD_STATUS_REQ,
	ULPD_SLEEP_SAVE_ULPD_DPLL_CTRL,
	ULPD_SLEEP_SAVE_ULPD_POWER_CTRL,
	ULPD_SLEEP_SAVE_SIZE
};

enum mpui1510_save_state {
	MPUI1510_SLEEP_SAVE_START = 0,
	/*
	 * MPUI registers 32 bits
	 */
	MPUI1510_SLEEP_SAVE_MPUI_CTRL,
	MPUI1510_SLEEP_SAVE_MPUI_DSP_BOOT_CONFIG,
	MPUI1510_SLEEP_SAVE_MPUI_DSP_API_CONFIG,
	MPUI1510_SLEEP_SAVE_MPUI_DSP_STATUS,
	MPUI1510_SLEEP_SAVE_EMIFF_SDRAM_CONFIG,
	MPUI1510_SLEEP_SAVE_EMIFS_CONFIG,
	MPUI1510_SLEEP_SAVE_OMAP_IH1_MIR,
	MPUI1510_SLEEP_SAVE_OMAP_IH2_MIR,
#if defined(CONFIG_ARCH_OMAP1510)
	MPUI1510_SLEEP_SAVE_SIZE
#else
	MPUI1510_SLEEP_SAVE_SIZE = 0
#endif
};

enum mpui1610_save_state {
	MPUI1610_SLEEP_SAVE_START = 0,
	/*
	 * MPUI registers 32 bits
	 */
	MPUI1610_SLEEP_SAVE_MPUI_CTRL,
	MPUI1610_SLEEP_SAVE_MPUI_DSP_BOOT_CONFIG,
	MPUI1610_SLEEP_SAVE_MPUI_DSP_API_CONFIG,
	MPUI1610_SLEEP_SAVE_MPUI_DSP_STATUS,
	MPUI1610_SLEEP_SAVE_EMIFF_SDRAM_CONFIG,
	MPUI1610_SLEEP_SAVE_EMIFS_CONFIG,
	MPUI1610_SLEEP_SAVE_OMAP_IH1_MIR,
	MPUI1610_SLEEP_SAVE_OMAP_IH2_0_MIR,
	MPUI1610_SLEEP_SAVE_OMAP_IH2_1_MIR,
	MPUI1610_SLEEP_SAVE_OMAP_IH2_2_MIR,
	MPUI1610_SLEEP_SAVE_OMAP_IH2_3_MIR,
#if defined(CONFIG_ARCH_OMAP16XX)
	MPUI1610_SLEEP_SAVE_SIZE
#else
	MPUI1610_SLEEP_SAVE_SIZE = 0
#endif
};

#endif /* ASSEMBLER */
#endif /* __ASM_ARCH_OMAP_PM_H */
