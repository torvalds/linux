/*_
********************************************************************************
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


#ifndef __AL_HAL_IOFIC_REG_H
#define __AL_HAL_IOFIC_REG_H

#ifdef __cplusplus
extern "C" {
#endif
/*
* Unit Registers
*/

struct al_iofic_grp_ctrl {
	uint32_t int_cause_grp;         /* Interrupt Cause RegisterSet by hardware */
	uint32_t rsrvd1;
	uint32_t int_cause_set_grp;     /* Interrupt Cause Set RegisterWriting 1 to a bit in t ... */
	uint32_t rsrvd2;
	uint32_t int_mask_grp;          /* Interrupt Mask RegisterIf Auto-mask control bit =TR ... */
	uint32_t rsrvd3;
	uint32_t int_mask_clear_grp;    /* Interrupt Mask Clear RegisterUsed when auto-mask co ... */
	uint32_t rsrvd4;
	uint32_t int_status_grp;        /* Interrupt status RegisterThis register latch the st ... */
	uint32_t rsrvd5;
	uint32_t int_control_grp;       /* Interrupt Control Register */
	uint32_t rsrvd6;
	uint32_t int_abort_msk_grp;     /* Interrupt Mask RegisterEach bit in this register ma ... */
	uint32_t rsrvd7;
	uint32_t int_log_msk_grp;       /* Interrupt Log RegisterEach bit in this register mas ... */
	uint32_t rsrvd8;
};

struct al_iofic_grp_mod {
	uint32_t grp_int_mod_reg;      /* Interrupt moderation registerDedicated moderation in ... */
	uint32_t grp_int_tgtid_reg;
};

struct al_iofic_regs {
	struct al_iofic_grp_ctrl ctrl[0];
	uint32_t rsrvd1[0x400 >> 2];
	struct al_iofic_grp_mod grp_int_mod[0][32];
};


/*
* Registers Fields
*/


/**** int_control_grp register ****/
/* When Clear_on_Read =1, All bits of  Cause register  ... */
#define INT_CONTROL_GRP_CLEAR_ON_READ (1 << 0)
/* (must be set only when MSIX is enabled)When Auto-Ma ... */
#define INT_CONTROL_GRP_AUTO_MASK (1 << 1)
/* Auto_Clear (RW)When Auto-Clear =1, the bits in the  ... */
#define INT_CONTROL_GRP_AUTO_CLEAR (1 << 2)
/* When Set_on_Posedge =1, the bits in the interrupt c ... */
#define INT_CONTROL_GRP_SET_ON_POSEDGE (1 << 3)
/* When Moderation_Reset =1, all Moderation timers ass ... */
#define INT_CONTROL_GRP_MOD_RST (1 << 4)
/* When mask_msi_x =1, No MSI-X from this group is sen ... */
#define INT_CONTROL_GRP_MASK_MSI_X (1 << 5)
/* MSI-X AWID value, same ID for all cause bits */
#define INT_CONTROL_GRP_AWID_MASK 0x00000F00
#define INT_CONTROL_GRP_AWID_SHIFT 8
/* This value determines the interval between interrup ... */
#define INT_CONTROL_GRP_MOD_INTV_MASK 0x00FF0000
#define INT_CONTROL_GRP_MOD_INTV_SHIFT 16
/* This value determines the Moderation_Timer_Clock sp ... */
#define INT_CONTROL_GRP_MOD_RES_MASK 0x0F000000
#define INT_CONTROL_GRP_MOD_RES_SHIFT 24

/**** grp_int_mod_reg register ****/
/* Interrupt Moderation Interval registerDedicated reg ... */
#define INT_MOD_INTV_MASK 0x000000FF
#define INT_MOD_INTV_SHIFT 0

/**** grp_int_tgtid_reg register ****/
/* Interrupt tgtid value registerDedicated reg ... */
#define INT_MSIX_TGTID_MASK 0x0000FFFF
#define INT_MSIX_TGTID_SHIFT 0
/* Interrupt tgtid_en value registerDedicated reg ... */
#define INT_MSIX_TGTID_EN_SHIFT 31

#ifdef __cplusplus
}
#endif

#endif /* __AL_HAL_IOFIC_REG_H */




