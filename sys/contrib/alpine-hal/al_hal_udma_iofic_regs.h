/*-
*******************************************************************************
Copyright (C) 2015 Annapurna Labs Ltd.

This file may be licensed under the terms of the Annapurna Labs Commercial
License Agreement.

Alternatively, this file can be distributed under the terms of the GNU General
Public License V2 as published by the Free Software Foundation and can be
found at http://www.gnu.org/licenses/gpl-2.0.html

Alternatively, redistribution and use in source and binary forms, with or
without modification, are permitted provided that the following conditions are
met:

    *     Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

    *     Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in
the documentation and/or other materials provided with the
distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/


#ifndef __AL_HAL_UDMA_IOFIC_REG_H
#define __AL_HAL_UDMA_IOFIC_REG_H

#include "al_hal_iofic_regs.h"
#ifdef __cplusplus
extern "C" {
#endif

/** This structure covers all interrupt registers of a given UDMA, which is
 * built of an al_iofic_regs, which is the common I/O Fabric Interrupt
 * controller (IOFIC), and additional two interrupts groups dedicated for the
 * application-specific engine attached to the UDMA, the interrupt summary
 * of those two groups routed to gourp D of the main controller.
 */
struct udma_iofic_regs {
	struct al_iofic_regs	main_iofic;
	uint32_t rsrvd1[(0x1c00) >> 2];
	struct al_iofic_grp_ctrl secondary_iofic_ctrl[2];
};

#ifdef __cplusplus
}
#endif

#endif /* __AL_HAL_UDMA_IOFIC_REG_H */




