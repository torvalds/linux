#ifndef __M68K_FPU_H
#define __M68K_FPU_H

#include <linux/config.h>

/*
 * MAX floating point unit state size (FSAVE/FRESTORE)
 */

#if defined(CONFIG_M68020) || defined(CONFIG_M68030)
#define FPSTATESIZE (216/sizeof(unsigned char))
#elif defined(CONFIG_M68040)
#define FPSTATESIZE (96/sizeof(unsigned char))
#elif defined(CONFIG_M68KFPU_EMU)
#define FPSTATESIZE (28/sizeof(unsigned char))
#elif defined(CONFIG_M68060)
#define FPSTATESIZE (12/sizeof(unsigned char))
#else
#define FPSTATESIZE error no_cpu_type_configured
#endif

#endif /* __M68K_FPU_H */
