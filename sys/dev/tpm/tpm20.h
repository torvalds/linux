/*-
 * Copyright (c) 2018 Stormshield.
 * Copyright (c) 2018 Semihalf.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _TPM20_H_
#define _TPM20_H_

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/endian.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/conf.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/sx.h>
#include <sys/uio.h>

#include <machine/bus.h>
#include <machine/md_var.h>
#include <machine/resource.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <dev/acpica/acpivar.h>
#include "opt_acpi.h"

#include "opt_tpm.h"

#define	BIT(x) (1 << (x))

/* Timeouts in us */
#define	TPM_TIMEOUT_A		750000
#define	TPM_TIMEOUT_B		2000000
#define	TPM_TIMEOUT_C		200000
#define	TPM_TIMEOUT_D		30000

/*
 * Generating RSA key pair takes ~(10-20s), which is significantly longer than
 * any timeout defined in spec. Because of that we need a new one.
 */
#define	TPM_TIMEOUT_LONG	40000000

/* List of commands that require TPM_TIMEOUT_LONG time to complete */
#define	TPM_CC_CreatePrimary	0x00000131
#define	TPM_CC_Create			0x00000153
#define	TPM_CC_CreateLoaded		0x00000191

/* List of commands that require only TPM_TIMEOUT_C time to complete */
#define TPM_CC_SequenceComplete			0x0000013e
#define TPM_CC_Startup					0x00000144
#define TPM_CC_SequenceUpdate			0x0000015c
#define TPM_CC_GetCapability			0x0000017a
#define TPM_CC_PCR_Extend				0x00000182
#define TPM_CC_EventSequenceComplete	0x00000185
#define TPM_CC_HashSequenceStart		0x00000186

/* Timeout before data in read buffer is discarded */
#define	TPM_READ_TIMEOUT	500000

#define	TPM_BUFSIZE		0x1000

#define	TPM_HEADER_SIZE		10

#define	TPM_CDEV_NAME		"tpm0"
#define	TPM_CDEV_PERM_FLAG	0600


#define TPM2_START_METHOD_ACPI 2
#define TPM2_START_METHOD_TIS 6
#define TPM2_START_METHOD_CRB 7
#define TPM2_START_METHOD_CRB_ACPI 8

struct tpm_sc {
	device_t	dev;

	struct resource	*mem_res;
	struct resource	*irq_res;
	int		mem_rid;
	int		irq_rid;

	struct cdev	*sc_cdev;

	struct sx 	dev_lock;
	struct cv	buf_cv;

	void 		*intr_cookie;
	int 		intr_type;	/* Current event type */
	bool 		interrupts;

	uint8_t 	*buf;
	size_t		pending_data_length;

	struct callout 	discard_buffer_callout;
#ifdef TPM_HARVEST
	struct callout 	harvest_callout;
	int		harvest_ticks;
#endif

	int		(*transmit)(struct tpm_sc *, size_t);
};

int tpm20_suspend(device_t dev);
int tpm20_shutdown(device_t dev);
int32_t tpm20_get_timeout(uint32_t command);
int tpm20_init(struct tpm_sc *sc);
void tpm20_release(struct tpm_sc *sc);

/* Small helper routines for io ops */
static inline uint8_t
RD1(struct tpm_sc *sc, bus_size_t off)
{

	return (bus_read_1(sc->mem_res, off));
}
static inline uint32_t
RD4(struct tpm_sc *sc, bus_size_t off)
{

	return (bus_read_4(sc->mem_res, off));
}
#ifdef __amd64__
static inline uint64_t
RD8(struct tpm_sc *sc, bus_size_t off)
{

	return (bus_read_8(sc->mem_res, off));
}
#endif
static inline void
WR1(struct tpm_sc *sc, bus_size_t off, uint8_t val)
{

	bus_write_1(sc->mem_res, off, val);
}
static inline void
WR4(struct tpm_sc *sc, bus_size_t off, uint32_t val)
{

	bus_write_4(sc->mem_res, off, val);
}
static inline void
AND4(struct tpm_sc *sc, bus_size_t off, uint32_t val)
{

	WR4(sc, off, RD4(sc, off) & val);
}
static inline void
OR1(struct tpm_sc *sc, bus_size_t off, uint8_t val)
{

	WR1(sc, off, RD1(sc, off) | val);
}
static inline void
OR4(struct tpm_sc *sc, bus_size_t off, uint32_t val)
{

	WR4(sc, off, RD4(sc, off) | val);
}
#endif	/* _TPM20_H_ */
