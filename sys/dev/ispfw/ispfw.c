/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * ISP Firmware Modules for FreeBSD
 *
 * Copyright (c) 2000, 2001, 2006 by Matthew Jacob
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
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
#include <sys/firmware.h>
#include <sys/kernel.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/systm.h>

#if	defined(ISP_ALL) || !defined(KLD_MODULE) 
#ifdef __sparc64__
#define	ISP_1000	1
#endif
#define	ISP_1040	1
#define	ISP_1040_IT	1
#define	ISP_1080	1
#define	ISP_1080_IT	1
#define	ISP_12160	1
#define	ISP_12160_IT	1
#define	ISP_2100	1
#define	ISP_2200	1
#define	ISP_2300	1
#define	ISP_2322	1
#define	ISP_2400	1
#define	ISP_2500	1
#endif

#ifndef MODULE_NAME
#define	MODULE_NAME	"ispfw"
#endif

#if	defined(ISP_1000)
#ifdef __sparc64__
#include <dev/ispfw/asm_1000.h>
#else
#error "firmware not compatible with this platform"
#endif
#endif
#if	defined(ISP_1040) || defined(ISP_1040_IT)
#include <dev/ispfw/asm_1040.h>
#endif
#if	defined(ISP_1080) || defined(ISP_1080_IT)
#include <dev/ispfw/asm_1080.h>
#endif
#if	defined(ISP_12160) || defined(ISP_12160_IT)
#include <dev/ispfw/asm_12160.h>
#endif
#if	defined(ISP_2100)
#include <dev/ispfw/asm_2100.h>
#endif
#if	defined(ISP_2200)
#include <dev/ispfw/asm_2200.h>
#endif
#if	defined(ISP_2300)
#include <dev/ispfw/asm_2300.h>
#endif
#if	defined(ISP_2322)
#include <dev/ispfw/asm_2322.h>
#endif
#if	defined(ISP_2400)
#include <dev/ispfw/asm_2400.h>
#endif
#if	defined(ISP_2500)
#include <dev/ispfw/asm_2500.h>
#endif

#if	defined(ISP_1000)
static int	isp_1000_loaded;
#endif
#if	defined(ISP_1040)
static int	isp_1040_loaded;
#endif
#if	defined(ISP_1080)
static int	isp_1080_loaded;
#endif
#if	defined(ISP_12160)
static int	isp_12160_loaded;
#endif
#if	defined(ISP_2100)
static int	isp_2100_loaded;
#endif
#if	defined(ISP_2200)
static int	isp_2200_loaded;
#endif
#if	defined(ISP_2300)
static int	isp_2300_loaded;
#endif
#if	defined(ISP_2322)
static int	isp_2322_loaded;
#endif
#if	defined(ISP_2400)
static int	isp_2400_loaded;
#endif
#if	defined(ISP_2500)
static int	isp_2500_loaded;
#endif

#define	ISPFW_VERSION	1

#define	RMACRO(token)	do {						\
	if (token##_loaded)						\
		break;							\
	if (firmware_register(#token, token##_risc_code,		\
	    token##_risc_code[3] * sizeof(token##_risc_code[3]),	\
	    ISPFW_VERSION, NULL) == NULL)				\
		break;							\
	token##_loaded++;						\
} while (0)

#define	UMACRO(token)	do {						\
	if (!token##_loaded)						\
		break;							\
	if (firmware_unregister(#token) != 0) {				\
		error = EBUSY;						\
		break;							\
	}								\
	token##_loaded--;						\
} while (0)

static int
do_load_fw(void)
{

#if	defined(ISP_1000)
	RMACRO(isp_1000);
#endif
#if	defined(ISP_1040)
	RMACRO(isp_1040);
#endif
#if	defined(ISP_1080)
	RMACRO(isp_1080);
#endif
#if	defined(ISP_12160)
	RMACRO(isp_12160);
#endif
#if	defined(ISP_2100)
	RMACRO(isp_2100);
#endif
#if	defined(ISP_2200)
	RMACRO(isp_2200);
#endif
#if	defined(ISP_2300)
	RMACRO(isp_2300);
#endif
#if	defined(ISP_2322)
	RMACRO(isp_2322);
#endif
#if	defined(ISP_2400)
	RMACRO(isp_2400);
#endif
#if	defined(ISP_2500)
	RMACRO(isp_2500);
#endif
	return (0);
}

static int
do_unload_fw(void)
{
	int error = 0;

#if	defined(ISP_1000)
	UMACRO(isp_1000);
#endif
#if	defined(ISP_1040)
	UMACRO(isp_1040);
#endif
#if	defined(ISP_1080)
	UMACRO(isp_1080);
#endif
#if	defined(ISP_12160)
	UMACRO(isp_12160);
#endif
#if	defined(ISP_2100)
	UMACRO(isp_2100);
#endif
#if	defined(ISP_2200)
	UMACRO(isp_2200);
#endif
#if	defined(ISP_2300)
	UMACRO(isp_2300);
#endif
#if	defined(ISP_2322)
	UMACRO(isp_2322);
#endif
#if	defined(ISP_2400)
	UMACRO(isp_2400);
#endif
#if	defined(ISP_2500)
	UMACRO(isp_2500);
#endif
	return (error);
}

static int
module_handler(module_t mod, int what, void *arg)
{

	switch (what) {
	case MOD_LOAD:
		return (do_load_fw());
	case MOD_UNLOAD:
		return (do_unload_fw());
	}
	return (EOPNOTSUPP);
}
static moduledata_t ispfw_mod = {
	MODULE_NAME, module_handler, NULL
};
#if	defined(ISP_ALL) || !defined(KLD_MODULE) 
DECLARE_MODULE(ispfw, ispfw_mod, SI_SUB_DRIVERS, SI_ORDER_THIRD);
#elif	defined(ISP_1000)
DECLARE_MODULE(isp_1000, ispfw_mod, SI_SUB_DRIVERS, SI_ORDER_THIRD);
#elif	defined(ISP_1040)
DECLARE_MODULE(isp_1040, ispfw_mod, SI_SUB_DRIVERS, SI_ORDER_THIRD);
#elif	defined(ISP_1080)
DECLARE_MODULE(isp_1080, ispfw_mod, SI_SUB_DRIVERS, SI_ORDER_THIRD);
#elif	defined(ISP_12160)
DECLARE_MODULE(isp_12160, ispfw_mod, SI_SUB_DRIVERS, SI_ORDER_THIRD);
#elif	defined(ISP_2100)
DECLARE_MODULE(isp_2100, ispfw_mod, SI_SUB_DRIVERS, SI_ORDER_THIRD);
#elif	defined(ISP_2200)
DECLARE_MODULE(isp_2200, ispfw_mod, SI_SUB_DRIVERS, SI_ORDER_THIRD);
#elif	defined(ISP_2300)
DECLARE_MODULE(isp_2300, ispfw_mod, SI_SUB_DRIVERS, SI_ORDER_THIRD);
#elif	defined(ISP_2322)
DECLARE_MODULE(isp_2322, ispfw_mod, SI_SUB_DRIVERS, SI_ORDER_THIRD);
#elif	defined(ISP_2400)
DECLARE_MODULE(isp_2400, ispfw_mod, SI_SUB_DRIVERS, SI_ORDER_THIRD);
#elif	defined(ISP_2500)
DECLARE_MODULE(isp_2500, ispfw_mod, SI_SUB_DRIVERS, SI_ORDER_THIRD);
#else
#error	"firmware not specified"
#endif
