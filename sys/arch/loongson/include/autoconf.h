/*	$OpenBSD: autoconf.h,v 1.17 2017/05/21 13:00:53 visa Exp $ */

/*
 * Copyright (c) 2001-2003 Opsycon AB  (www.opsycon.se / www.opsycon.com)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * Definitions used by autoconfiguration.
 */

#ifndef _MACHINE_AUTOCONF_H_
#define _MACHINE_AUTOCONF_H_

#include <machine/bus.h>

struct bonito_config;
struct htb_config;
struct mips_isa_chipset;

/*
 * List of legacy I/O ranges.
 */
struct legacy_io_range {
	bus_addr_t	start;
	bus_size_t	end;	/* inclusive */
};

/*
 * Per platform information.
 */
struct platform {
	int				 system_type;
#define	LOONGSON_2E		0x0000	/* Generic Loongson 2E system */
#define	LOONGSON_YEELOONG	0x0001	/* Lemote Yeeloong */
#define	LOONGSON_GDIUM		0x0002	/* EMTEC Gdium Liberty */
#define	LOONGSON_FULOONG	0x0003	/* Lemote Fuloong */
#define	LOONGSON_LYNLOONG	0x0004	/* Lemote Lynloong */
#define	LOONGSON_EBT700		0x0005	/* eBenton EBT700 */
#define	LOONGSON_3A		0x0066	/* Loongson 2Gq or 3A based system */

	char				*vendor;
	char				*product;

	const struct bonito_config	*bonito_config;
	const struct htb_config		*htb_config;
	struct mips_isa_chipset		*isa_chipset;
	const struct legacy_io_range	*legacy_io_ranges;

	void				(*setup)(void);
	void				(*device_register)(struct device *,
					    void *);

	void				(*powerdown)(void);
	void				(*reset)(void);
	int				(*suspend)(void);
	int				(*resume)(void);

#ifdef MULTIPROCESSOR
	void				(*config_secondary_cpus)(
					    struct device *, cfprint_t);
	void				(*boot_secondary_cpu)(
					    struct cpu_info *);
	int				(*ipi_establish)(int (*)(void *),
					    cpuid_t);
	void				(*ipi_set)(cpuid_t);
	void				(*ipi_clear)(cpuid_t);
#endif /* MULTIPROCESSOR */
};

#define LOONGSON_MAXCPUS	16

extern const struct platform *sys_platform;
extern void *loongson_videobios;
extern uint loongson_cpumask;
extern uint loongson_ver;
extern int nnodes;

#ifdef MULTIPROCESSOR
extern uint64_t cpu_spinup_a0;
extern uint64_t cpu_spinup_sp;
#endif

struct mainbus_attach_args {
	const char	*maa_name;
};

extern struct device *bootdv;
extern char bootdev[];
extern enum devclass bootdev_class;

extern bus_space_tag_t early_mem_t;
extern bus_space_tag_t early_io_t;

#define	REGVAL8(x)	*((volatile uint8_t *)PHYS_TO_XKPHYS((x), CCA_NC))
#define	REGVAL32(x)	*((volatile uint32_t *)PHYS_TO_XKPHYS((x), CCA_NC))
#define	REGVAL64(x)	*((volatile uint64_t *)PHYS_TO_XKPHYS((x), CCA_NC))

#define	REGVAL(x)	REGVAL32(x)

#include <mips64/autoconf.h>

#endif /* _MACHINE_AUTOCONF_H_ */
