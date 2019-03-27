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
 @File          debug_ext.h

 @Description   Debug mode definitions.
*//***************************************************************************/

#ifndef __DEBUG_EXT_H
#define __DEBUG_EXT_H

#include "std_ext.h"
#include "xx_ext.h"
#include "memcpy_ext.h"
#if (DEBUG_ERRORS > 0)
#include "sprint_ext.h"
#include "string_ext.h"
#endif /* DEBUG_ERRORS > 0 */


#if (DEBUG_ERRORS > 0)

/* Internally used macros */

#define DUMP_Print          XX_Print
#define DUMP_MAX_LEVELS     6
#define DUMP_IDX_LEN        6
#define DUMP_MAX_STR        64


#define _CREATE_DUMP_SUBSTR(phrase) \
    dumpTmpLevel = 0; dumpSubStr[0] = '\0'; \
    snprintf(dumpTmpStr, DUMP_MAX_STR, "%s", #phrase); \
    p_DumpToken = strtok(dumpTmpStr, (dumpIsArr[0] ? "[" : ".")); \
    while ((p_DumpToken != NULL) && (dumpTmpLevel < DUMP_MAX_LEVELS)) \
    { \
        strlcat(dumpSubStr, p_DumpToken, DUMP_MAX_STR); \
        if (dumpIsArr[dumpTmpLevel]) \
        { \
            strlcat(dumpSubStr, dumpIdxStr[dumpTmpLevel], DUMP_MAX_STR); \
            p_DumpToken = strtok(NULL, "."); \
        } \
        if ((p_DumpToken != NULL) && \
            ((p_DumpToken = strtok(NULL, (dumpIsArr[++dumpTmpLevel] ? "[" : "."))) != NULL)) \
            strlcat(dumpSubStr, ".", DUMP_MAX_STR); \
    }


/**************************************************************************//**
 @Group         gen_id  General Drivers Utilities

 @Description   External routines.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         dump_id  Memory and Registers Dump Mechanism

 @Description   Macros for dumping memory mapped structures.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Description   Declaration of dump mechanism variables.

                This macro must be declared at the beginning of each routine
                which uses the dump mechanism macros, before the routine's code
                starts.
*//***************************************************************************/
#define DECLARE_DUMP \
    char    dumpIdxStr[DUMP_MAX_LEVELS + 1][DUMP_IDX_LEN] = { "", }; \
    char    dumpSubStr[DUMP_MAX_STR] = ""; \
    char    dumpTmpStr[DUMP_MAX_STR] = ""; \
    char    *p_DumpToken = NULL; \
    int     dumpArrIdx = 0, dumpArrSize = 0, dumpLevel = 0, dumpTmpLevel = 0; \
    uint8_t dumpIsArr[DUMP_MAX_LEVELS + 1] = { 0 }; \
    /* Prevent warnings if not all used */ \
    UNUSED(dumpIdxStr[0][0]); \
    UNUSED(dumpSubStr[0]); \
    UNUSED(dumpTmpStr[0]); \
    UNUSED(p_DumpToken); \
    UNUSED(dumpArrIdx); \
    UNUSED(dumpArrSize); \
    UNUSED(dumpLevel); \
    UNUSED(dumpTmpLevel); \
    UNUSED(dumpIsArr[0]);


/**************************************************************************//**
 @Description   Prints a title for a subsequent dumped structure or memory.

                The inputs for this macro are the structure/memory title and
                its base addresses.
*//***************************************************************************/
#define DUMP_TITLE(addr, msg)           \
    DUMP_Print("\r\n"); DUMP_Print msg; \
    if (addr)                           \
        DUMP_Print(" (%p)", (addr));    \
    DUMP_Print("\r\n---------------------------------------------------------\r\n");

/**************************************************************************//**
 @Description   Prints a subtitle for a subsequent dumped sub-structure (optional).

                The inputs for this macro are the sub-structure subtitle.
                A separating line with this subtitle will be printed.
*//***************************************************************************/
#define DUMP_SUBTITLE(subtitle)  \
    DUMP_Print("----------- "); DUMP_Print subtitle; DUMP_Print("\r\n")


/**************************************************************************//**
 @Description   Dumps a memory region in 4-bytes aligned format.

                The inputs for this macro are the base addresses and size
                (in bytes) of the memory region.
*//***************************************************************************/
#define DUMP_MEMORY(addr, size)  \
    MemDisp((uint8_t *)(addr), (int)(size))


/**************************************************************************//**
 @Description   Declares a dump loop, for dumping a sub-structure array.

                The inputs for this macro are:
                - idx: an index variable, for indexing the sub-structure items
                       inside the loop. This variable must be declared separately
                       in the beginning of the routine.
                - cnt: the number of times to repeat the loop. This number should
                       equal the number of items in the sub-structures array.

                Note, that the body of the loop must be written inside brackets.
*//***************************************************************************/
#define DUMP_SUBSTRUCT_ARRAY(idx, cnt) \
    for (idx=0, dumpIsArr[dumpLevel++] = 1; \
         (idx < cnt) && (dumpLevel > 0) && snprintf(dumpIdxStr[dumpLevel-1], DUMP_IDX_LEN, "[%d]", idx); \
         idx++, ((idx < cnt) || (dumpIsArr[--dumpLevel] = 0)))


/**************************************************************************//**
 @Description   Dumps a structure's member variable.

                The input for this macro is the full reference for the member
                variable, where the structure is referenced using a pointer.

                Note, that a members array must be dumped using DUMP_ARR macro,
                rather than using this macro.

                If the member variable is part of a sub-structure hierarchy,
                the full hierarchy (including array indexing) must be specified.

                Examples:   p_Struct->member
                            p_Struct->sub.member
                            p_Struct->sub[i].member
*//***************************************************************************/
#define DUMP_VAR(st, phrase) \
    do { \
        void            *addr = (void *)&((st)->phrase); \
        physAddress_t   physAddr = XX_VirtToPhys(addr); \
        _CREATE_DUMP_SUBSTR(phrase); \
        DUMP_Print("0x%010llX: 0x%08x%8s\t%s\r\n", \
                   physAddr, GET_UINT32(*(uint32_t*)addr), "", dumpSubStr); \
    } while (0)


/**************************************************************************//**
 @Description   Dumps a structure's members array.

                The input for this macro is the full reference for the members
                array, where the structure is referenced using a pointer.

                If the members array is part of a sub-structure hierarchy,
                the full hierarchy (including array indexing) must be specified.

                Examples:   p_Struct->array
                            p_Struct->sub.array
                            p_Struct->sub[i].array
*//***************************************************************************/
#define DUMP_ARR(st, phrase) \
    do { \
        physAddress_t physAddr; \
        _CREATE_DUMP_SUBSTR(phrase); \
        dumpArrSize = ARRAY_SIZE((st)->phrase); \
        for (dumpArrIdx=0; dumpArrIdx < dumpArrSize; dumpArrIdx++) { \
            physAddr = XX_VirtToPhys((void *)&((st)->phrase[dumpArrIdx])); \
            DUMP_Print("0x%010llX: 0x%08x%8s\t%s[%d]\r\n", \
                       physAddr, GET_UINT32((st)->phrase[dumpArrIdx]), "", dumpSubStr, dumpArrIdx); \
        } \
    } while (0)



#endif /* DEBUG_ERRORS > 0 */


/** @} */ /* end of dump_id group */
/** @} */ /* end of gen_id group */


#endif /* __DEBUG_EXT_H */

