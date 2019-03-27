/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/**************************************************************************//**

 @File          endian_ext.h

 @Description   Big/little endian swapping routines.
*//***************************************************************************/

#ifndef __ENDIAN_EXT_H
#define __ENDIAN_EXT_H

#include "std_ext.h"


/**************************************************************************//**
 @Group         gen_id  General Drivers Utilities

 @Description   General usage API. This API is intended for usage by both the
                internal modules and the user's application.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         endian_id Big/Little-Endian Conversion

 @Description   Routines and macros for Big/Little-Endian conversion and
                general byte swapping.

                All routines and macros are expecting unsigned values as
                parameters, but will generate the correct result also for
                signed values. Therefore, signed/unsigned casting is allowed.
 @{
*//***************************************************************************/

/**************************************************************************//**
 @Collection    Byte-Swap Macros

                Macros for swapping byte order.

 @Cautions      The parameters of these macros are evaluated multiple times.
                For calculated expressions or expressions that contain function
                calls it is recommended to use the byte-swap routines.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   Swaps the byte order of a given 16-bit value.

 @Param[in]     val - The 16-bit value to swap.

 @Return        The byte-swapped value..

 @Cautions      The given value is evaluated multiple times by this macro.
                For calculated expressions or expressions that contain function
                calls it is recommended to use the SwapUint16() routine.

 @hideinitializer
*//***************************************************************************/
#define SWAP_UINT16(val) \
    ((uint16_t)((((val) & 0x00FF) << 8) | (((val) & 0xFF00) >> 8)))

/**************************************************************************//**
 @Description   Swaps the byte order of a given 32-bit value.

 @Param[in]     val - The 32-bit value to swap.

 @Return        The byte-swapped value..

 @Cautions      The given value is evaluated multiple times by this macro.
                For calculated expressions or expressions that contain function
                calls it is recommended to use the SwapUint32() routine.

 @hideinitializer
*//***************************************************************************/
#define SWAP_UINT32(val) \
    ((uint32_t)((((val) & 0x000000FF) << 24) | \
                (((val) & 0x0000FF00) <<  8) | \
                (((val) & 0x00FF0000) >>  8) | \
                (((val) & 0xFF000000) >> 24)))

/**************************************************************************//**
 @Description   Swaps the byte order of a given 64-bit value.

 @Param[in]     val - The 64-bit value to swap.

 @Return        The byte-swapped value..

 @Cautions      The given value is evaluated multiple times by this macro.
                For calculated expressions or expressions that contain function
                calls it is recommended to use the SwapUint64() routine.

 @hideinitializer
*//***************************************************************************/
#define SWAP_UINT64(val) \
    ((uint64_t)((((val) & 0x00000000000000FFULL) << 56) | \
                (((val) & 0x000000000000FF00ULL) << 40) | \
                (((val) & 0x0000000000FF0000ULL) << 24) | \
                (((val) & 0x00000000FF000000ULL) <<  8) | \
                (((val) & 0x000000FF00000000ULL) >>  8) | \
                (((val) & 0x0000FF0000000000ULL) >> 24) | \
                (((val) & 0x00FF000000000000ULL) >> 40) | \
                (((val) & 0xFF00000000000000ULL) >> 56)))

/* @} */

/**************************************************************************//**
 @Collection    Byte-Swap Routines

                Routines for swapping the byte order of a given parameter and
                returning the swapped value.

                These inline routines are safer than the byte-swap macros,
                because they evaluate the parameter expression only once.
 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      SwapUint16

 @Description   Returns the byte-swapped value of a given 16-bit value.

 @Param[in]     val - The 16-bit value.

 @Return        The byte-swapped value of the parameter.
*//***************************************************************************/
static __inline__ uint16_t SwapUint16(uint16_t val)
{
    return (uint16_t)(((val & 0x00FF) << 8) |
                      ((val & 0xFF00) >> 8));
}

/**************************************************************************//**
 @Function      SwapUint32

 @Description   Returns the byte-swapped value of a given 32-bit value.

 @Param[in]     val - The 32-bit value.

 @Return        The byte-swapped value of the parameter.
*//***************************************************************************/
static __inline__ uint32_t SwapUint32(uint32_t val)
{
    return (uint32_t)(((val & 0x000000FF) << 24) |
                      ((val & 0x0000FF00) <<  8) |
                      ((val & 0x00FF0000) >>  8) |
                      ((val & 0xFF000000) >> 24));
}

/**************************************************************************//**
 @Function      SwapUint64

 @Description   Returns the byte-swapped value of a given 64-bit value.

 @Param[in]     val - The 64-bit value.

 @Return        The byte-swapped value of the parameter.
*//***************************************************************************/
static __inline__ uint64_t SwapUint64(uint64_t val)
{
    return (uint64_t)(((val & 0x00000000000000FFULL) << 56) |
                      ((val & 0x000000000000FF00ULL) << 40) |
                      ((val & 0x0000000000FF0000ULL) << 24) |
                      ((val & 0x00000000FF000000ULL) <<  8) |
                      ((val & 0x000000FF00000000ULL) >>  8) |
                      ((val & 0x0000FF0000000000ULL) >> 24) |
                      ((val & 0x00FF000000000000ULL) >> 40) |
                      ((val & 0xFF00000000000000ULL) >> 56));
}

/* @} */

/**************************************************************************//**
 @Collection    In-place Byte-Swap-And-Set Routines

                Routines for swapping the byte order of a given variable and
                setting the swapped value back to the same variable.
 @{
*//***************************************************************************/

/**************************************************************************//**
 @Function      SwapUint16P

 @Description   Swaps the byte order of a given 16-bit variable.

 @Param[in]     p_Val - Pointer to the 16-bit variable.

 @Return        None.
*//***************************************************************************/
static __inline__ void SwapUint16P(uint16_t *p_Val)
{
    *p_Val = SwapUint16(*p_Val);
}

/**************************************************************************//**
 @Function      SwapUint32P

 @Description   Swaps the byte order of a given 32-bit variable.

 @Param[in]     p_Val - Pointer to the 32-bit variable.

 @Return        None.
*//***************************************************************************/
static __inline__ void SwapUint32P(uint32_t *p_Val)
{
    *p_Val = SwapUint32(*p_Val);
}

/**************************************************************************//**
 @Function      SwapUint64P

 @Description   Swaps the byte order of a given 64-bit variable.

 @Param[in]     p_Val - Pointer to the 64-bit variable.

 @Return        None.
*//***************************************************************************/
static __inline__ void SwapUint64P(uint64_t *p_Val)
{
    *p_Val = SwapUint64(*p_Val);
}

/* @} */


/**************************************************************************//**
 @Collection    Little-Endian Conversion Macros

                These macros convert given parameters to or from Little-Endian
                format. Use these macros when you want to read or write a specific
                Little-Endian value in memory, without a-priori knowing the CPU
                byte order.

                These macros use the byte-swap routines. For conversion of
                constants in initialization structures, you may use the CONST
                versions of these macros (see below), which are using the
                byte-swap macros instead.
 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   Converts a given 16-bit value from CPU byte order to
                Little-Endian byte order.

 @Param[in]     val - The 16-bit value to convert.

 @Return        The converted value.

 @hideinitializer
*//***************************************************************************/
#define CPU_TO_LE16(val)        SwapUint16(val)

/**************************************************************************//**
 @Description   Converts a given 32-bit value from CPU byte order to
                Little-Endian byte order.

 @Param[in]     val - The 32-bit value to convert.

 @Return        The converted value.

 @hideinitializer
*//***************************************************************************/
#define CPU_TO_LE32(val)        SwapUint32(val)

/**************************************************************************//**
 @Description   Converts a given 64-bit value from CPU byte order to
                Little-Endian byte order.

 @Param[in]     val - The 64-bit value to convert.

 @Return        The converted value.

 @hideinitializer
*//***************************************************************************/
#define CPU_TO_LE64(val)        SwapUint64(val)


/**************************************************************************//**
 @Description   Converts a given 16-bit value from Little-Endian byte order to
                CPU byte order.

 @Param[in]     val - The 16-bit value to convert.

 @Return        The converted value.

 @hideinitializer
*//***************************************************************************/
#define LE16_TO_CPU(val)        CPU_TO_LE16(val)

/**************************************************************************//**
 @Description   Converts a given 32-bit value from Little-Endian byte order to
                CPU byte order.

 @Param[in]     val - The 32-bit value to convert.

 @Return        The converted value.

 @hideinitializer
*//***************************************************************************/
#define LE32_TO_CPU(val)        CPU_TO_LE32(val)

/**************************************************************************//**
 @Description   Converts a given 64-bit value from Little-Endian byte order to
                CPU byte order.

 @Param[in]     val - The 64-bit value to convert.

 @Return        The converted value.

 @hideinitializer
*//***************************************************************************/
#define LE64_TO_CPU(val)        CPU_TO_LE64(val)

/* @} */

/**************************************************************************//**
 @Collection    Little-Endian Constant Conversion Macros

                These macros convert given constants to or from Little-Endian
                format. Use these macros when you want to read or write a specific
                Little-Endian constant in memory, without a-priori knowing the
                CPU byte order.

                These macros use the byte-swap macros, therefore can be used for
                conversion of constants in initialization structures.

 @Cautions      The parameters of these macros are evaluated multiple times.
                For non-constant expressions, use the non-CONST macro versions.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   Converts a given 16-bit constant from CPU byte order to
                Little-Endian byte order.

 @Param[in]     val - The 16-bit value to convert.

 @Return        The converted value.

 @hideinitializer
*//***************************************************************************/
#define CONST_CPU_TO_LE16(val)  SWAP_UINT16(val)

/**************************************************************************//**
 @Description   Converts a given 32-bit constant from CPU byte order to
                Little-Endian byte order.

 @Param[in]     val - The 32-bit value to convert.

 @Return        The converted value.

 @hideinitializer
*//***************************************************************************/
#define CONST_CPU_TO_LE32(val)  SWAP_UINT32(val)

/**************************************************************************//**
 @Description   Converts a given 64-bit constant from CPU byte order to
                Little-Endian byte order.

 @Param[in]     val - The 64-bit value to convert.

 @Return        The converted value.

 @hideinitializer
*//***************************************************************************/
#define CONST_CPU_TO_LE64(val)  SWAP_UINT64(val)


/**************************************************************************//**
 @Description   Converts a given 16-bit constant from Little-Endian byte order
                to CPU byte order.

 @Param[in]     val - The 16-bit value to convert.

 @Return        The converted value.

 @hideinitializer
*//***************************************************************************/
#define CONST_LE16_TO_CPU(val)  CONST_CPU_TO_LE16(val)

/**************************************************************************//**
 @Description   Converts a given 32-bit constant from Little-Endian byte order
                to CPU byte order.

 @Param[in]     val - The 32-bit value to convert.

 @Return        The converted value.

 @hideinitializer
*//***************************************************************************/
#define CONST_LE32_TO_CPU(val)  CONST_CPU_TO_LE32(val)

/**************************************************************************//**
 @Description   Converts a given 64-bit constant from Little-Endian byte order
                to CPU byte order.

 @Param[in]     val - The 64-bit value to convert.

 @Return        The converted value.

 @hideinitializer
*//***************************************************************************/
#define CONST_LE64_TO_CPU(val)  CONST_CPU_TO_LE64(val)

/* @} */


/** @} */ /* end of endian_id group */
/** @} */ /* end of gen_id group */


#endif /* __ENDIAN_EXT_H */

