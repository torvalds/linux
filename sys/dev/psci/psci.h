/*-
 * Copyright (c) 2013, 2014 Robin Randhawa
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

#ifndef	_MACHINE_PSCI_H_
#define	_MACHINE_PSCI_H_

#include <sys/types.h>

typedef int (*psci_initfn_t)(device_t dev, int default_version);
typedef int (*psci_callfn_t)(register_t, register_t, register_t, register_t);

extern int psci_present;

int	psci_cpu_on(unsigned long, unsigned long, unsigned long);
void	psci_reset(void);
int32_t	psci_features(uint32_t);
int	psci_get_version(void);

/* Handler to let us call into the PSCI/SMCCC firmware */
extern psci_callfn_t psci_callfn;
static inline int
psci_call(register_t a, register_t b, register_t c, register_t d)
{

	return (psci_callfn(a, b, c, d));
}
/* One of these handlers will be selected during the boot */
int	psci_hvc_despatch(register_t, register_t, register_t, register_t);
int	psci_smc_despatch(register_t, register_t, register_t, register_t);


/*
 * PSCI return codes.
 */
#define	PSCI_RETVAL_SUCCESS		0
#define	PSCI_RETVAL_NOT_SUPPORTED	-1
#define	PSCI_RETVAL_INVALID_PARAMS	-2
#define	PSCI_RETVAL_DENIED		-3
#define	PSCI_RETVAL_ALREADY_ON		-4
#define	PSCI_RETVAL_ON_PENDING		-5
#define	PSCI_RETVAL_INTERNAL_FAILURE	-6
#define	PSCI_RETVAL_NOT_PRESENT		-7
#define	PSCI_RETVAL_DISABLED		-8
/*
 * Used to signal PSCI is not available, e.g. to start a CPU.
 */
#define	PSCI_MISSING			1

/*
 * PSCI function codes (as per PSCI v0.2).
 */
#ifdef __aarch64__
#define	PSCI_FNID_VERSION		0x84000000
#define	PSCI_FNID_CPU_SUSPEND		0xc4000001
#define	PSCI_FNID_CPU_OFF		0x84000002
#define	PSCI_FNID_CPU_ON		0xc4000003
#define	PSCI_FNID_AFFINITY_INFO		0xc4000004
#define	PSCI_FNID_MIGRATE		0xc4000005
#define	PSCI_FNID_MIGRATE_INFO_TYPE	0x84000006
#define	PSCI_FNID_MIGRATE_INFO_UP_CPU	0xc4000007
#define	PSCI_FNID_SYSTEM_OFF		0x84000008
#define	PSCI_FNID_SYSTEM_RESET		0x84000009
#define	PSCI_FNID_FEATURES		0x8400000a
#else
#define	PSCI_FNID_VERSION		0x84000000
#define	PSCI_FNID_CPU_SUSPEND		0x84000001
#define	PSCI_FNID_CPU_OFF		0x84000002
#define	PSCI_FNID_CPU_ON		0x84000003
#define	PSCI_FNID_AFFINITY_INFO		0x84000004
#define	PSCI_FNID_MIGRATE		0x84000005
#define	PSCI_FNID_MIGRATE_INFO_TYPE	0x84000006
#define	PSCI_FNID_MIGRATE_INFO_UP_CPU	0x84000007
#define	PSCI_FNID_SYSTEM_OFF		0x84000008
#define	PSCI_FNID_SYSTEM_RESET		0x84000009
#define	PSCI_FNID_FEATURES		0x8400000a
#endif

#define	PSCI_VER_MAJOR(v)		(((v) >> 16) & 0xFF)
#define	PSCI_VER_MINOR(v)		((v) & 0xFF)

enum psci_fn {
	PSCI_FN_VERSION,
	PSCI_FN_CPU_SUSPEND,
	PSCI_FN_CPU_OFF,
	PSCI_FN_CPU_ON,
	PSCI_FN_AFFINITY_INFO,
	PSCI_FN_MIGRATE,
	PSCI_FN_MIGRATE_INFO_TYPE,
	PSCI_FN_MIGRATE_INFO_UP_CPU,
	PSCI_FN_SYSTEM_OFF,
	PSCI_FN_SYSTEM_RESET,
	PSCI_FN_MAX
};

#endif /* _MACHINE_PSCI_H_ */
