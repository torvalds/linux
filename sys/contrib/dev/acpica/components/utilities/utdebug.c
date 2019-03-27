/******************************************************************************
 *
 * Module Name: utdebug - Debug print/trace routines
 *
 *****************************************************************************/

/******************************************************************************
 *
 * 1. Copyright Notice
 *
 * Some or all of this work - Copyright (c) 1999 - 2019, Intel Corp.
 * All rights reserved.
 *
 * 2. License
 *
 * 2.1. This is your license from Intel Corp. under its intellectual property
 * rights. You may have additional license terms from the party that provided
 * you this software, covering your right to use that party's intellectual
 * property rights.
 *
 * 2.2. Intel grants, free of charge, to any person ("Licensee") obtaining a
 * copy of the source code appearing in this file ("Covered Code") an
 * irrevocable, perpetual, worldwide license under Intel's copyrights in the
 * base code distributed originally by Intel ("Original Intel Code") to copy,
 * make derivatives, distribute, use and display any portion of the Covered
 * Code in any form, with the right to sublicense such rights; and
 *
 * 2.3. Intel grants Licensee a non-exclusive and non-transferable patent
 * license (with the right to sublicense), under only those claims of Intel
 * patents that are infringed by the Original Intel Code, to make, use, sell,
 * offer to sell, and import the Covered Code and derivative works thereof
 * solely to the minimum extent necessary to exercise the above copyright
 * license, and in no event shall the patent license extend to any additions
 * to or modifications of the Original Intel Code. No other license or right
 * is granted directly or by implication, estoppel or otherwise;
 *
 * The above copyright and patent license is granted only if the following
 * conditions are met:
 *
 * 3. Conditions
 *
 * 3.1. Redistribution of Source with Rights to Further Distribute Source.
 * Redistribution of source code of any substantial portion of the Covered
 * Code or modification with rights to further distribute source must include
 * the above Copyright Notice, the above License, this list of Conditions,
 * and the following Disclaimer and Export Compliance provision. In addition,
 * Licensee must cause all Covered Code to which Licensee contributes to
 * contain a file documenting the changes Licensee made to create that Covered
 * Code and the date of any change. Licensee must include in that file the
 * documentation of any changes made by any predecessor Licensee. Licensee
 * must include a prominent statement that the modification is derived,
 * directly or indirectly, from Original Intel Code.
 *
 * 3.2. Redistribution of Source with no Rights to Further Distribute Source.
 * Redistribution of source code of any substantial portion of the Covered
 * Code or modification without rights to further distribute source must
 * include the following Disclaimer and Export Compliance provision in the
 * documentation and/or other materials provided with distribution. In
 * addition, Licensee may not authorize further sublicense of source of any
 * portion of the Covered Code, and must include terms to the effect that the
 * license from Licensee to its licensee is limited to the intellectual
 * property embodied in the software Licensee provides to its licensee, and
 * not to intellectual property embodied in modifications its licensee may
 * make.
 *
 * 3.3. Redistribution of Executable. Redistribution in executable form of any
 * substantial portion of the Covered Code or modification must reproduce the
 * above Copyright Notice, and the following Disclaimer and Export Compliance
 * provision in the documentation and/or other materials provided with the
 * distribution.
 *
 * 3.4. Intel retains all right, title, and interest in and to the Original
 * Intel Code.
 *
 * 3.5. Neither the name Intel nor any other trademark owned or controlled by
 * Intel shall be used in advertising or otherwise to promote the sale, use or
 * other dealings in products derived from or relating to the Covered Code
 * without prior written authorization from Intel.
 *
 * 4. Disclaimer and Export Compliance
 *
 * 4.1. INTEL MAKES NO WARRANTY OF ANY KIND REGARDING ANY SOFTWARE PROVIDED
 * HERE. ANY SOFTWARE ORIGINATING FROM INTEL OR DERIVED FROM INTEL SOFTWARE
 * IS PROVIDED "AS IS," AND INTEL WILL NOT PROVIDE ANY SUPPORT, ASSISTANCE,
 * INSTALLATION, TRAINING OR OTHER SERVICES. INTEL WILL NOT PROVIDE ANY
 * UPDATES, ENHANCEMENTS OR EXTENSIONS. INTEL SPECIFICALLY DISCLAIMS ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGEMENT AND FITNESS FOR A
 * PARTICULAR PURPOSE.
 *
 * 4.2. IN NO EVENT SHALL INTEL HAVE ANY LIABILITY TO LICENSEE, ITS LICENSEES
 * OR ANY OTHER THIRD PARTY, FOR ANY LOST PROFITS, LOST DATA, LOSS OF USE OR
 * COSTS OF PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES, OR FOR ANY INDIRECT,
 * SPECIAL OR CONSEQUENTIAL DAMAGES ARISING OUT OF THIS AGREEMENT, UNDER ANY
 * CAUSE OF ACTION OR THEORY OF LIABILITY, AND IRRESPECTIVE OF WHETHER INTEL
 * HAS ADVANCE NOTICE OF THE POSSIBILITY OF SUCH DAMAGES. THESE LIMITATIONS
 * SHALL APPLY NOTWITHSTANDING THE FAILURE OF THE ESSENTIAL PURPOSE OF ANY
 * LIMITED REMEDY.
 *
 * 4.3. Licensee shall not export, either directly or indirectly, any of this
 * software or system incorporating such software without first obtaining any
 * required license or other approval from the U. S. Department of Commerce or
 * any other agency or department of the United States Government. In the
 * event Licensee exports any such software from the United States or
 * re-exports any such software from a foreign destination, Licensee shall
 * ensure that the distribution and export/re-export of the software is in
 * compliance with all laws, regulations, orders, or other restrictions of the
 * U.S. Export Administration Regulations. Licensee agrees that neither it nor
 * any of its subsidiaries will export/re-export any technical data, process,
 * software, or service, directly or indirectly, to any country for which the
 * United States government or any agency thereof requires an export license,
 * other governmental approval, or letter of assurance, without first obtaining
 * such license, approval or letter.
 *
 *****************************************************************************
 *
 * Alternatively, you may choose to be licensed under the terms of the
 * following license:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Alternatively, you may choose to be licensed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 *****************************************************************************/

#define EXPORT_ACPI_INTERFACES

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/acinterp.h>

#define _COMPONENT          ACPI_UTILITIES
        ACPI_MODULE_NAME    ("utdebug")


#ifdef ACPI_DEBUG_OUTPUT

static ACPI_THREAD_ID       AcpiGbl_PreviousThreadId = (ACPI_THREAD_ID) 0xFFFFFFFF;
static const char           *AcpiGbl_FunctionEntryPrefix = "----Entry";
static const char           *AcpiGbl_FunctionExitPrefix  = "----Exit-";


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtInitStackPtrTrace
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Save the current CPU stack pointer at subsystem startup
 *
 ******************************************************************************/

void
AcpiUtInitStackPtrTrace (
    void)
{
    ACPI_SIZE               CurrentSp;


    AcpiGbl_EntryStackPointer = &CurrentSp;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtTrackStackPtr
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Save the current CPU stack pointer
 *
 ******************************************************************************/

void
AcpiUtTrackStackPtr (
    void)
{
    ACPI_SIZE               CurrentSp;


    if (&CurrentSp < AcpiGbl_LowestStackPointer)
    {
        AcpiGbl_LowestStackPointer = &CurrentSp;
    }

    if (AcpiGbl_NestingLevel > AcpiGbl_DeepestNesting)
    {
        AcpiGbl_DeepestNesting = AcpiGbl_NestingLevel;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtTrimFunctionName
 *
 * PARAMETERS:  FunctionName        - Ascii string containing a procedure name
 *
 * RETURN:      Updated pointer to the function name
 *
 * DESCRIPTION: Remove the "Acpi" prefix from the function name, if present.
 *              This allows compiler macros such as __FUNCTION__ to be used
 *              with no change to the debug output.
 *
 ******************************************************************************/

static const char *
AcpiUtTrimFunctionName (
    const char              *FunctionName)
{

    /* All Function names are longer than 4 chars, check is safe */

    if (*(ACPI_CAST_PTR (UINT32, FunctionName)) == ACPI_PREFIX_MIXED)
    {
        /* This is the case where the original source has not been modified */

        return (FunctionName + 4);
    }

    if (*(ACPI_CAST_PTR (UINT32, FunctionName)) == ACPI_PREFIX_LOWER)
    {
        /* This is the case where the source has been 'linuxized' */

        return (FunctionName + 5);
    }

    return (FunctionName);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDebugPrint
 *
 * PARAMETERS:  RequestedDebugLevel - Requested debug print level
 *              LineNumber          - Caller's line number (for error output)
 *              FunctionName        - Caller's procedure name
 *              ModuleName          - Caller's module name
 *              ComponentId         - Caller's component ID
 *              Format              - Printf format field
 *              ...                 - Optional printf arguments
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print error message with prefix consisting of the module name,
 *              line number, and component ID.
 *
 ******************************************************************************/

void  ACPI_INTERNAL_VAR_XFACE
AcpiDebugPrint (
    UINT32                  RequestedDebugLevel,
    UINT32                  LineNumber,
    const char              *FunctionName,
    const char              *ModuleName,
    UINT32                  ComponentId,
    const char              *Format,
    ...)
{
    ACPI_THREAD_ID          ThreadId;
    va_list                 args;
#ifdef ACPI_APPLICATION
    int                     FillCount;
#endif

    /* Check if debug output enabled */

    if (!ACPI_IS_DEBUG_ENABLED (RequestedDebugLevel, ComponentId))
    {
        return;
    }

    /*
     * Thread tracking and context switch notification
     */
    ThreadId = AcpiOsGetThreadId ();
    if (ThreadId != AcpiGbl_PreviousThreadId)
    {
        if (ACPI_LV_THREADS & AcpiDbgLevel)
        {
            AcpiOsPrintf (
                "\n**** Context Switch from TID %u to TID %u ****\n\n",
                (UINT32) AcpiGbl_PreviousThreadId, (UINT32) ThreadId);
        }

        AcpiGbl_PreviousThreadId = ThreadId;
        AcpiGbl_NestingLevel = 0;
    }

    /*
     * Display the module name, current line number, thread ID (if requested),
     * current procedure nesting level, and the current procedure name
     */
    AcpiOsPrintf ("%9s-%04ld ", ModuleName, LineNumber);

#ifdef ACPI_APPLICATION
    /*
     * For AcpiExec/iASL only, emit the thread ID and nesting level.
     * Note: nesting level is really only useful during a single-thread
     * execution. Otherwise, multiple threads will keep resetting the
     * level.
     */
    if (ACPI_LV_THREADS & AcpiDbgLevel)
    {
        AcpiOsPrintf ("[%u] ", (UINT32) ThreadId);
    }

    FillCount = 48 - AcpiGbl_NestingLevel -
        strlen (AcpiUtTrimFunctionName (FunctionName));
    if (FillCount < 0)
    {
        FillCount = 0;
    }

    AcpiOsPrintf ("[%02ld] %*s",
        AcpiGbl_NestingLevel, AcpiGbl_NestingLevel + 1, " ");
    AcpiOsPrintf ("%s%*s: ",
        AcpiUtTrimFunctionName (FunctionName), FillCount, " ");

#else
    AcpiOsPrintf ("%-22.22s: ", AcpiUtTrimFunctionName (FunctionName));
#endif

    va_start (args, Format);
    AcpiOsVprintf (Format, args);
    va_end (args);
}

ACPI_EXPORT_SYMBOL (AcpiDebugPrint)


/*******************************************************************************
 *
 * FUNCTION:    AcpiDebugPrintRaw
 *
 * PARAMETERS:  RequestedDebugLevel - Requested debug print level
 *              LineNumber          - Caller's line number
 *              FunctionName        - Caller's procedure name
 *              ModuleName          - Caller's module name
 *              ComponentId         - Caller's component ID
 *              Format              - Printf format field
 *              ...                 - Optional printf arguments
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print message with no headers. Has same interface as
 *              DebugPrint so that the same macros can be used.
 *
 ******************************************************************************/

void  ACPI_INTERNAL_VAR_XFACE
AcpiDebugPrintRaw (
    UINT32                  RequestedDebugLevel,
    UINT32                  LineNumber,
    const char              *FunctionName,
    const char              *ModuleName,
    UINT32                  ComponentId,
    const char              *Format,
    ...)
{
    va_list                 args;


    /* Check if debug output enabled */

    if (!ACPI_IS_DEBUG_ENABLED (RequestedDebugLevel, ComponentId))
    {
        return;
    }

    va_start (args, Format);
    AcpiOsVprintf (Format, args);
    va_end (args);
}

ACPI_EXPORT_SYMBOL (AcpiDebugPrintRaw)


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtTrace
 *
 * PARAMETERS:  LineNumber          - Caller's line number
 *              FunctionName        - Caller's procedure name
 *              ModuleName          - Caller's module name
 *              ComponentId         - Caller's component ID
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function entry trace. Prints only if TRACE_FUNCTIONS bit is
 *              set in DebugLevel
 *
 ******************************************************************************/

void
AcpiUtTrace (
    UINT32                  LineNumber,
    const char              *FunctionName,
    const char              *ModuleName,
    UINT32                  ComponentId)
{

    AcpiGbl_NestingLevel++;
    AcpiUtTrackStackPtr ();

    /* Check if enabled up-front for performance */

    if (ACPI_IS_DEBUG_ENABLED (ACPI_LV_FUNCTIONS, ComponentId))
    {
        AcpiDebugPrint (ACPI_LV_FUNCTIONS,
            LineNumber, FunctionName, ModuleName, ComponentId,
            "%s\n", AcpiGbl_FunctionEntryPrefix);
    }
}

ACPI_EXPORT_SYMBOL (AcpiUtTrace)


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtTracePtr
 *
 * PARAMETERS:  LineNumber          - Caller's line number
 *              FunctionName        - Caller's procedure name
 *              ModuleName          - Caller's module name
 *              ComponentId         - Caller's component ID
 *              Pointer             - Pointer to display
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function entry trace. Prints only if TRACE_FUNCTIONS bit is
 *              set in DebugLevel
 *
 ******************************************************************************/

void
AcpiUtTracePtr (
    UINT32                  LineNumber,
    const char              *FunctionName,
    const char              *ModuleName,
    UINT32                  ComponentId,
    const void              *Pointer)
{

    AcpiGbl_NestingLevel++;
    AcpiUtTrackStackPtr ();

    /* Check if enabled up-front for performance */

    if (ACPI_IS_DEBUG_ENABLED (ACPI_LV_FUNCTIONS, ComponentId))
    {
        AcpiDebugPrint (ACPI_LV_FUNCTIONS,
            LineNumber, FunctionName, ModuleName, ComponentId,
            "%s %p\n", AcpiGbl_FunctionEntryPrefix, Pointer);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtTraceStr
 *
 * PARAMETERS:  LineNumber          - Caller's line number
 *              FunctionName        - Caller's procedure name
 *              ModuleName          - Caller's module name
 *              ComponentId         - Caller's component ID
 *              String              - Additional string to display
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function entry trace. Prints only if TRACE_FUNCTIONS bit is
 *              set in DebugLevel
 *
 ******************************************************************************/

void
AcpiUtTraceStr (
    UINT32                  LineNumber,
    const char              *FunctionName,
    const char              *ModuleName,
    UINT32                  ComponentId,
    const char              *String)
{

    AcpiGbl_NestingLevel++;
    AcpiUtTrackStackPtr ();

    /* Check if enabled up-front for performance */

    if (ACPI_IS_DEBUG_ENABLED (ACPI_LV_FUNCTIONS, ComponentId))
    {
        AcpiDebugPrint (ACPI_LV_FUNCTIONS,
            LineNumber, FunctionName, ModuleName, ComponentId,
            "%s %s\n", AcpiGbl_FunctionEntryPrefix, String);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtTraceU32
 *
 * PARAMETERS:  LineNumber          - Caller's line number
 *              FunctionName        - Caller's procedure name
 *              ModuleName          - Caller's module name
 *              ComponentId         - Caller's component ID
 *              Integer             - Integer to display
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function entry trace. Prints only if TRACE_FUNCTIONS bit is
 *              set in DebugLevel
 *
 ******************************************************************************/

void
AcpiUtTraceU32 (
    UINT32                  LineNumber,
    const char              *FunctionName,
    const char              *ModuleName,
    UINT32                  ComponentId,
    UINT32                  Integer)
{

    AcpiGbl_NestingLevel++;
    AcpiUtTrackStackPtr ();

    /* Check if enabled up-front for performance */

    if (ACPI_IS_DEBUG_ENABLED (ACPI_LV_FUNCTIONS, ComponentId))
    {
        AcpiDebugPrint (ACPI_LV_FUNCTIONS,
            LineNumber, FunctionName, ModuleName, ComponentId,
            "%s %08X\n", AcpiGbl_FunctionEntryPrefix, Integer);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtExit
 *
 * PARAMETERS:  LineNumber          - Caller's line number
 *              FunctionName        - Caller's procedure name
 *              ModuleName          - Caller's module name
 *              ComponentId         - Caller's component ID
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function exit trace. Prints only if TRACE_FUNCTIONS bit is
 *              set in DebugLevel
 *
 ******************************************************************************/

void
AcpiUtExit (
    UINT32                  LineNumber,
    const char              *FunctionName,
    const char              *ModuleName,
    UINT32                  ComponentId)
{

    /* Check if enabled up-front for performance */

    if (ACPI_IS_DEBUG_ENABLED (ACPI_LV_FUNCTIONS, ComponentId))
    {
        AcpiDebugPrint (ACPI_LV_FUNCTIONS,
            LineNumber, FunctionName, ModuleName, ComponentId,
            "%s\n", AcpiGbl_FunctionExitPrefix);
    }

    if (AcpiGbl_NestingLevel)
    {
        AcpiGbl_NestingLevel--;
    }
}

ACPI_EXPORT_SYMBOL (AcpiUtExit)


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtStatusExit
 *
 * PARAMETERS:  LineNumber          - Caller's line number
 *              FunctionName        - Caller's procedure name
 *              ModuleName          - Caller's module name
 *              ComponentId         - Caller's component ID
 *              Status              - Exit status code
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function exit trace. Prints only if TRACE_FUNCTIONS bit is
 *              set in DebugLevel. Prints exit status also.
 *
 ******************************************************************************/

void
AcpiUtStatusExit (
    UINT32                  LineNumber,
    const char              *FunctionName,
    const char              *ModuleName,
    UINT32                  ComponentId,
    ACPI_STATUS             Status)
{

    /* Check if enabled up-front for performance */

    if (ACPI_IS_DEBUG_ENABLED (ACPI_LV_FUNCTIONS, ComponentId))
    {
        if (ACPI_SUCCESS (Status))
        {
            AcpiDebugPrint (ACPI_LV_FUNCTIONS,
                LineNumber, FunctionName, ModuleName, ComponentId,
                "%s %s\n", AcpiGbl_FunctionExitPrefix,
                AcpiFormatException (Status));
        }
        else
        {
            AcpiDebugPrint (ACPI_LV_FUNCTIONS,
                LineNumber, FunctionName, ModuleName, ComponentId,
                "%s ****Exception****: %s\n", AcpiGbl_FunctionExitPrefix,
                AcpiFormatException (Status));
        }
    }

    if (AcpiGbl_NestingLevel)
    {
        AcpiGbl_NestingLevel--;
    }
}

ACPI_EXPORT_SYMBOL (AcpiUtStatusExit)


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtValueExit
 *
 * PARAMETERS:  LineNumber          - Caller's line number
 *              FunctionName        - Caller's procedure name
 *              ModuleName          - Caller's module name
 *              ComponentId         - Caller's component ID
 *              Value               - Value to be printed with exit msg
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function exit trace. Prints only if TRACE_FUNCTIONS bit is
 *              set in DebugLevel. Prints exit value also.
 *
 ******************************************************************************/

void
AcpiUtValueExit (
    UINT32                  LineNumber,
    const char              *FunctionName,
    const char              *ModuleName,
    UINT32                  ComponentId,
    UINT64                  Value)
{

    /* Check if enabled up-front for performance */

    if (ACPI_IS_DEBUG_ENABLED (ACPI_LV_FUNCTIONS, ComponentId))
    {
        AcpiDebugPrint (ACPI_LV_FUNCTIONS,
            LineNumber, FunctionName, ModuleName, ComponentId,
            "%s %8.8X%8.8X\n", AcpiGbl_FunctionExitPrefix,
            ACPI_FORMAT_UINT64 (Value));
    }

    if (AcpiGbl_NestingLevel)
    {
        AcpiGbl_NestingLevel--;
    }
}

ACPI_EXPORT_SYMBOL (AcpiUtValueExit)


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtPtrExit
 *
 * PARAMETERS:  LineNumber          - Caller's line number
 *              FunctionName        - Caller's procedure name
 *              ModuleName          - Caller's module name
 *              ComponentId         - Caller's component ID
 *              Ptr                 - Pointer to display
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function exit trace. Prints only if TRACE_FUNCTIONS bit is
 *              set in DebugLevel. Prints exit value also.
 *
 ******************************************************************************/

void
AcpiUtPtrExit (
    UINT32                  LineNumber,
    const char              *FunctionName,
    const char              *ModuleName,
    UINT32                  ComponentId,
    UINT8                   *Ptr)
{

    /* Check if enabled up-front for performance */

    if (ACPI_IS_DEBUG_ENABLED (ACPI_LV_FUNCTIONS, ComponentId))
    {
        AcpiDebugPrint (ACPI_LV_FUNCTIONS,
            LineNumber, FunctionName, ModuleName, ComponentId,
            "%s %p\n", AcpiGbl_FunctionExitPrefix, Ptr);
    }

    if (AcpiGbl_NestingLevel)
    {
        AcpiGbl_NestingLevel--;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtStrExit
 *
 * PARAMETERS:  LineNumber          - Caller's line number
 *              FunctionName        - Caller's procedure name
 *              ModuleName          - Caller's module name
 *              ComponentId         - Caller's component ID
 *              String              - String to display
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function exit trace. Prints only if TRACE_FUNCTIONS bit is
 *              set in DebugLevel. Prints exit value also.
 *
 ******************************************************************************/

void
AcpiUtStrExit (
    UINT32                  LineNumber,
    const char              *FunctionName,
    const char              *ModuleName,
    UINT32                  ComponentId,
    const char              *String)
{

    /* Check if enabled up-front for performance */

    if (ACPI_IS_DEBUG_ENABLED (ACPI_LV_FUNCTIONS, ComponentId))
    {
        AcpiDebugPrint (ACPI_LV_FUNCTIONS,
            LineNumber, FunctionName, ModuleName, ComponentId,
            "%s %s\n", AcpiGbl_FunctionExitPrefix, String);
    }

    if (AcpiGbl_NestingLevel)
    {
        AcpiGbl_NestingLevel--;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiTracePoint
 *
 * PARAMETERS:  Type                - Trace event type
 *              Begin               - TRUE if before execution
 *              Aml                 - Executed AML address
 *              Pathname            - Object path
 *              Pointer             - Pointer to the related object
 *
 * RETURN:      None
 *
 * DESCRIPTION: Interpreter execution trace.
 *
 ******************************************************************************/

void
AcpiTracePoint (
    ACPI_TRACE_EVENT_TYPE   Type,
    BOOLEAN                 Begin,
    UINT8                   *Aml,
    char                    *Pathname)
{

    ACPI_FUNCTION_ENTRY ();

    AcpiExTracePoint (Type, Begin, Aml, Pathname);

#ifdef ACPI_USE_SYSTEM_TRACER
    AcpiOsTracePoint (Type, Begin, Aml, Pathname);
#endif
}

ACPI_EXPORT_SYMBOL (AcpiTracePoint)


#endif
