/*
 *  linux/include/asm-arm/hardware/ioc.h
 *
 *  Copyright (C) Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Use these macros to read/write the IOC.  All it does is perform the actual
 *  read/write.
 */
#ifndef __ASMARM_HARDWARE_IOC_H
#define __ASMARM_HARDWARE_IOC_H

#ifndef __ASSEMBLY__

/*
 * We use __raw_base variants here so that we give the compiler the
 * chance to keep IOC_BASE in a register.
 */
#define ioc_readb(off)		__raw_readb(IOC_BASE + (off))
#define ioc_writeb(val,off)	__raw_writeb(val, IOC_BASE + (off))

#endif

#define IOC_CONTROL	(0x00)
#define IOC_KARTTX	(0x04)
#define IOC_KARTRX	(0x04)

#define IOC_IRQSTATA	(0x10)
#define IOC_IRQREQA	(0x14)
#define IOC_IRQCLRA	(0x14)
#define IOC_IRQMASKA	(0x18)

#define IOC_IRQSTATB	(0x20)
#define IOC_IRQREQB	(0x24)
#define IOC_IRQMASKB	(0x28)

#define IOC_FIQSTAT	(0x30)
#define IOC_FIQREQ	(0x34)
#define IOC_FIQMASK	(0x38)

#define IOC_T0CNTL	(0x40)
#define IOC_T0LTCHL	(0x40)
#define IOC_T0CNTH	(0x44)
#define IOC_T0LTCHH	(0x44)
#define IOC_T0GO	(0x48)
#define IOC_T0LATCH	(0x4c)

#define IOC_T1CNTL	(0x50)
#define IOC_T1LTCHL	(0x50)
#define IOC_T1CNTH	(0x54)
#define IOC_T1LTCHH	(0x54)
#define IOC_T1GO	(0x58)
#define IOC_T1LATCH	(0x5c)

#define IOC_T2CNTL	(0x60)
#define IOC_T2LTCHL	(0x60)
#define IOC_T2CNTH	(0x64)
#define IOC_T2LTCHH	(0x64)
#define IOC_T2GO	(0x68)
#define IOC_T2LATCH	(0x6c)

#define IOC_T3CNTL	(0x70)
#define IOC_T3LTCHL	(0x70)
#define IOC_T3CNTH	(0x74)
#define IOC_T3LTCHH	(0x74)
#define IOC_T3GO	(0x78)
#define IOC_T3LATCH	(0x7c)

#endif
