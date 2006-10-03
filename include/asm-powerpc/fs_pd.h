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
#include <asm/cpm2.h>
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

#define cpm2_map(member)						\
({									\
	u32 offset = offsetof(cpm2_map_t, member);			\
	void *addr = ioremap (CPM_MAP_ADDR + offset,			\
			      sizeof( ((cpm2_map_t*)0)->member));	\
	addr;								\
})

#define cpm2_map_size(member, size)					\
({									\
	u32 offset = offsetof(cpm2_map_t, member);			\
	void *addr = ioremap (CPM_MAP_ADDR + offset, size);		\
	addr;								\
})

#define cpm2_unmap(addr)	iounmap(addr)

#endif
