/*******************************************************************************
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

/**
 * @defgroup group_eth_alu_api API
 * Ethernet Controller generic ALU API
 * @ingroup group_eth
 * @{
 * @file   al_hal_eth_alu.h
 *
 * @brief Header file for control parameters for the generic ALU unit in the Ethernet Datapath for Advanced Ethernet port.
 *
 */

#ifndef __AL_HAL_ETH_ALU_H__
#define __AL_HAL_ETH_ALU_H__

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

enum AL_ETH_ALU_OPCODE
{
	AL_ALU_FWD_A                            = 0,
	AL_ALU_ARITHMETIC_ADD                   = 1,
	AL_ALU_ARITHMETIC_SUBTRACT              = 2,
	AL_ALU_BITWISE_AND                      = 3,
	AL_ALU_BITWISE_OR                       = 4,
	AL_ALU_SHIFT_RIGHT_A_BY_B               = 5,
	AL_ALU_SHIFT_LEFT_A_BY_B                = 6,
	AL_ALU_BITWISE_XOR                      = 7,
	AL_ALU_FWD_INV_A                        = 16,
	AL_ALU_ARITHMETIC_ADD_INV_A_AND_B       = 17,
	AL_ALU_ARITHMETIC_SUBTRACT_INV_A_AND_B  = 18,
	AL_ALU_BITWISE_AND_INV_A_AND_B          = 19,
	AL_ALU_BITWISE_OR_INV_A_AND_B           = 20,
	AL_ALU_SHIFT_RIGHT_INV_A_BY_B           = 21,
	AL_ALU_SHIFT_LEFT_INV_A_BY_B            = 22,
	AL_ALU_BITWISE_XOR_INV_A_AND_B          = 23,
	AL_ALU_ARITHMETIC_ADD_A_AND_INV_B       = 33,
	AL_ALU_ARITHMETIC_SUBTRACT_A_AND_INV_B  = 34,
	AL_ALU_BITWISE_AND_A_AND_INV_B          = 35,
	AL_ALU_BITWISE_OR_A_AND_INV_B           = 36,
	AL_ALU_SHIFT_RIGHT_A_BY_INV_B           = 37,
	AL_ALU_SHIFT_LEFT_A_BY_INV_B            = 38,
	AL_ALU_BITWISE_XOR_A_AND_INV_B          = 39,
	AL_ALU_ARITHMETIC_ADD_INV_A_AND_INV_B   = 49,
	AL_ALU_ARITHMETIC_SUBTRACT_INV_A_AND    = 50,
	AL_ALU_BITWISE_AND_INV_A_AND_INV_B      = 51,
	AL_ALU_BITWISE_OR_INV_A_AND_INV_B       = 52,
	AL_ALU_SHIFT_RIGHT_INV_A_BY_INV_B       = 53,
	AL_ALU_SHIFT_LEFT_INV_A_BY_INV_B        = 54,
	AL_ALU_BITWISE_XOR_INV_A_AND_INV_B      = 55
};

#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */
#endif /* __AL_HAL_ETH_ALU_H__ */
/** @} end of Ethernet group */
