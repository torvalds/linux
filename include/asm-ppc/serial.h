/*
 * include/asm-ppc/serial.h
 */

#ifdef __KERNEL__
#ifndef __ASM_SERIAL_H__
#define __ASM_SERIAL_H__


#if defined(CONFIG_EV64260)
#include <platforms/ev64260.h>
#elif defined(CONFIG_CHESTNUT)
#include <platforms/chestnut.h>
#elif defined(CONFIG_POWERPMC250)
#include <platforms/powerpmc250.h>
#elif defined(CONFIG_LOPEC)
#include <platforms/lopec.h>
#elif defined(CONFIG_MVME5100)
#include <platforms/mvme5100.h>
#elif defined(CONFIG_PAL4)
#include <platforms/pal4_serial.h>
#elif defined(CONFIG_PRPMC750)
#include <platforms/prpmc750.h>
#elif defined(CONFIG_PRPMC800)
#include <platforms/prpmc800.h>
#elif defined(CONFIG_SANDPOINT)
#include <platforms/sandpoint.h>
#elif defined(CONFIG_SPRUCE)
#include <platforms/spruce.h>
#elif defined(CONFIG_4xx)
#include <asm/ibm4xx.h>
#elif defined(CONFIG_83xx)
#include <asm/mpc83xx.h>
#elif defined(CONFIG_85xx)
#include <asm/mpc85xx.h>
#elif defined(CONFIG_RADSTONE_PPC7D)
#include <platforms/radstone_ppc7d.h>
#else

/*
 * XXX Assume it has PC-style ISA serial ports - true for PReP at least.
 */
#include <asm/pc_serial.h>

#endif /* !CONFIG_GEMINI and others */
#endif /* __ASM_SERIAL_H__ */
#endif /* __KERNEL__ */
