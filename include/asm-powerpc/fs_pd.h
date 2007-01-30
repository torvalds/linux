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

#ifdef CONFIG_CPM2
#include <asm/cpm2.h>

#if defined(CONFIG_8260)
#include <asm/mpc8260.h>
#elif defined(CONFIG_85xx)
#include <asm/mpc85xx.h>
#endif

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

#ifdef CONFIG_8xx
#include <asm/8xx_immap.h>
#include <asm/mpc8xx.h>

#define immr_map(member)						\
({									\
	u32 offset = offsetof(immap_t, member);				\
	void *addr = ioremap (IMAP_ADDR + offset,			\
			      sizeof( ((immap_t*)0)->member));		\
	addr;								\
})

#define immr_map_size(member, size)					\
({									\
	u32 offset = offsetof(immap_t, member);				\
	void *addr = ioremap (IMAP_ADDR + offset, size);		\
	addr;								\
})

#define immr_unmap(addr)		iounmap(addr)
#endif

static inline int uart_baudrate(void)
{
        return get_baudrate();
}

static inline int uart_clock(void)
{
        return ppc_proc_freq;
}

#endif
