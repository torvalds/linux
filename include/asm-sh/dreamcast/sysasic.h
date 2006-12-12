/* include/asm-sh/dreamcast/sysasic.h
 *
 * Definitions for the Dreamcast System ASIC and related peripherals.
 *
 * Copyright (c) 2001 M. R. Brown <mrbrown@linuxdc.org>
 * Copyright (C) 2003 Paul Mundt <lethal@linux-sh.org>
 *
 * This file is part of the LinuxDC project (www.linuxdc.org)
 *
 * Released under the terms of the GNU GPL v2.0.
 *
 */
#ifndef __ASM_SH_DREAMCAST_SYSASIC_H
#define __ASM_SH_DREAMCAST_SYSASIC_H

#include <asm/irq.h>

/* Hardware events -

   Each of these events correspond to a bit within the Event Mask Registers/
   Event Status Registers.  Because of the virtual IRQ numbering scheme, a
   base offset must be used when calculating the virtual IRQ that each event
   takes.
*/

#define HW_EVENT_IRQ_BASE  OFFCHIP_IRQ_BASE /* 48 */

/* IRQ 13 */
#define HW_EVENT_VSYNC     (HW_EVENT_IRQ_BASE +  5) /* VSync */
#define HW_EVENT_MAPLE_DMA (HW_EVENT_IRQ_BASE + 12) /* Maple DMA complete */
#define HW_EVENT_GDROM_DMA (HW_EVENT_IRQ_BASE + 14) /* GD-ROM DMA complete */
#define HW_EVENT_G2_DMA    (HW_EVENT_IRQ_BASE + 15) /* G2 DMA complete */
#define HW_EVENT_PVR2_DMA  (HW_EVENT_IRQ_BASE + 19) /* PVR2 DMA complete */

/* IRQ 11 */
#define HW_EVENT_GDROM_CMD (HW_EVENT_IRQ_BASE + 32) /* GD-ROM cmd. complete */
#define HW_EVENT_AICA_SYS  (HW_EVENT_IRQ_BASE + 33) /* AICA-related */
#define HW_EVENT_EXTERNAL  (HW_EVENT_IRQ_BASE + 35) /* Ext. (expansion) */

#define HW_EVENT_IRQ_MAX (HW_EVENT_IRQ_BASE + 95)

#endif /* __ASM_SH_DREAMCAST_SYSASIC_H */

