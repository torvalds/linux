/*
 * Copyright (C) 2004 2N Telekomunikace, Ladislav Michl <michl@2n.cz>
 *
 * Hardware definitions for OMAP5910 based VoiceBlue board.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_VOICEBLUE_H
#define __ASM_ARCH_VOICEBLUE_H

#if (EXTERNAL_MAX_NR_PORTS < 4)
#undef EXTERNAL_MAX_NR_PORTS
#define EXTERNAL_MAX_NR_PORTS	4
#endif

extern void voiceblue_wdt_enable(void);
extern void voiceblue_wdt_disable(void);
extern void voiceblue_wdt_ping(void);
extern void voiceblue_reset(void);

#endif /*  __ASM_ARCH_VOICEBLUE_H */

