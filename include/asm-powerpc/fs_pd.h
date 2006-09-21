/*
 * Platform information definitions.
 *
 * 2006 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef FS_PD_H
#define FS_PD_H
#include <sysdev/fsl_soc.h>
#include <asm/time.h>

static inline int uart_baudrate(void)
{
        return get_baudrate();
}

static inline int uart_clock(void)
{
        return ppc_proc_freq;
}

#endif
