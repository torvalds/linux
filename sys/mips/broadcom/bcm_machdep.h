/*-
 * Copyright (c) 2016 Landon Fuller <landonf@FreeBSD.org>
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Landon Fuller
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 * 
 * $FreeBSD$
 */

#ifndef	_MIPS_BROADCOM_BCM_MACHDEP_H_
#define	_MIPS_BROADCOM_BCM_MACHDEP_H_

#include <machine/cpufunc.h>
#include <machine/cpuregs.h>

#include <dev/bhnd/bhnd.h>
#include <dev/bhnd/bhnd_eromvar.h>

#include <dev/bhnd/cores/pmu/bhnd_pmuvar.h>

#include "bcm_nvram_cfevar.h"

extern const struct bhnd_pmu_io	bcm_pmu_soc_io;

struct bcm_platform {
	struct bhnd_chipid		 cid;		/**< chip id */
	struct bhnd_core_info		 cc_id;		/**< chipc core info */
	uintptr_t			 cc_addr;	/**< chipc core phys address */
	uint32_t			 cc_caps;	/**< chipc capabilities */
	uint32_t			 cc_caps_ext;	/**< chipc extended capabilies */

	struct bhnd_core_info		 cpu_id;	/**< cpu core info */
	uintptr_t			 cpu_addr;	/**< cpu core phys address */

	/* On non-AOB devices, the PMU register block is mapped to chipc;
	 * the pmu_id and pmu_addr values will be copied from cc_id
	 * and cc_addr. */
	struct bhnd_core_info		 pmu_id;	/**< PMU core info */
	uintptr_t			 pmu_addr;	/**< PMU core phys address, or
							     0x0 if no PMU */

	struct bhnd_pmu_query		 pmu;		/**< PMU query instance */

	bhnd_erom_class_t		*erom_impl;	/**< erom parser class */
	struct kobj_ops			 erom_ops;	/**< compiled kobj opcache */
	struct bhnd_erom_iobus		 erom_io;	/**< erom I/O callbacks */
	union {
		bhnd_erom_static_t	 data;
		bhnd_erom_t		 obj;
	} erom;

	struct bhnd_nvram_io		*nvram_io;	/**< NVRAM I/O context, or NULL if unavailable */
	bhnd_nvram_data_class		*nvram_cls;	/**< NVRAM data class, or NULL if unavailable */

	struct bhnd_service_registry	 services;	/**< platform service providers */

#ifdef CFE
	int				 cfe_console;	/**< Console handle, or -1 */
#endif
};

struct bcm_platform	*bcm_get_platform(void);

uint64_t		 bcm_get_cpufreq(struct bcm_platform *bp);
uint64_t		 bcm_get_sifreq(struct bcm_platform *bp);
uint64_t		 bcm_get_alpfreq(struct bcm_platform *bp);
uint64_t		 bcm_get_ilpfreq(struct bcm_platform *bp);

u_int			 bcm_get_uart_rclk(struct bcm_platform *bp);

int			 bcm_get_nvram(struct bcm_platform *bp,
			     const char *name, void *outp, size_t *olen,
			     bhnd_nvram_type type);

#define	BCM_ERR(fmt, ...)	\
	printf("%s: " fmt, __FUNCTION__, ##__VA_ARGS__)

#define	BCM_SOC_BSH(_addr, _offset)			\
	((bus_space_handle_t)BCM_SOC_ADDR((_addr), (_offset)))

#define	BCM_SOC_ADDR(_addr, _offset)			\
	MIPS_PHYS_TO_KSEG1((_addr) + (_offset))

#define	BCM_SOC_READ_4(_addr, _offset)			\
	readl(BCM_SOC_ADDR((_addr), (_offset)))
#define	BCM_SOC_WRITE_4(_addr, _reg, _val)		\
	writel(BCM_SOC_ADDR((_addr), (_offset)), (_val))

#define	BCM_CORE_ADDR(_bp, _name, _reg)			\
	BCM_SOC_ADDR(_bp->_name, (_reg))

#define	BCM_CORE_READ_4(_bp, _name, _reg)		\
	readl(BCM_CORE_ADDR(_bp, _name, (_reg)))
#define	BCM_CORE_WRITE_4(_bp, _name, _reg, _val)	\
	writel(BCM_CORE_ADDR(_bp, _name, (_reg)), (_val))

#define	BCM_CHIPC_READ_4(_bp, _reg)			\
	BCM_CORE_READ_4(_bp, cc_addr, (_reg))
#define	BCM_CHIPC_WRITE_4(_bp, _reg, _val)		\
	BCM_CORE_WRITE_4(_bp, cc_addr, (_reg), (_val))

#define	BCM_CPU_READ_4(_bp, _reg)			\
	BCM_CORE_READ_4(_bp, cpu_addr, (_reg))
#define	BCM_CPU_WRITE_4(_bp, _reg, _val)		\
	BCM_CORE_WRITE_4(_bp, cpu_addr, (_reg), (_val))

#define	BCM_PMU_READ_4(_bp, _reg)			\
	BCM_CORE_READ_4(_bp, pmu_addr, (_reg))
#define	BCM_PMU_WRITE_4(_bp, _reg, _val)		\
	BCM_CORE_WRITE_4(_bp, pmu_addr, (_reg), (_val))

#endif /* _MIPS_BROADCOM_BCM_MACHDEP_H_ */
