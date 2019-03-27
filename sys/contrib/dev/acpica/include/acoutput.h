/******************************************************************************
 *
 * Name: acoutput.h -- debug output
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

#ifndef __ACOUTPUT_H__
#define __ACOUTPUT_H__

/*
 * Debug levels and component IDs. These are used to control the
 * granularity of the output of the ACPI_DEBUG_PRINT macro -- on a
 * per-component basis and a per-exception-type basis.
 */

/* Component IDs are used in the global "DebugLayer" */

#define ACPI_UTILITIES              0x00000001
#define ACPI_HARDWARE               0x00000002
#define ACPI_EVENTS                 0x00000004
#define ACPI_TABLES                 0x00000008
#define ACPI_NAMESPACE              0x00000010
#define ACPI_PARSER                 0x00000020
#define ACPI_DISPATCHER             0x00000040
#define ACPI_EXECUTER               0x00000080
#define ACPI_RESOURCES              0x00000100
#define ACPI_CA_DEBUGGER            0x00000200
#define ACPI_OS_SERVICES            0x00000400
#define ACPI_CA_DISASSEMBLER        0x00000800

/* Component IDs for ACPI tools and utilities */

#define ACPI_COMPILER               0x00001000
#define ACPI_TOOLS                  0x00002000
#define ACPI_EXAMPLE                0x00004000
#define ACPI_DRIVER                 0x00008000
#define DT_COMPILER                 0x00010000
#define ASL_PREPROCESSOR            0x00020000

#define ACPI_ALL_COMPONENTS         0x0001FFFF
#define ACPI_COMPONENT_DEFAULT      (ACPI_ALL_COMPONENTS)

/* Component IDs reserved for ACPI drivers */

#define ACPI_ALL_DRIVERS            0xFFFF0000


/*
 * Raw debug output levels, do not use these in the ACPI_DEBUG_PRINT macros
 */
#define ACPI_LV_INIT                0x00000001
#define ACPI_LV_DEBUG_OBJECT        0x00000002
#define ACPI_LV_INFO                0x00000004
#define ACPI_LV_REPAIR              0x00000008
#define ACPI_LV_TRACE_POINT         0x00000010
#define ACPI_LV_ALL_EXCEPTIONS      0x0000001F

/* Trace verbosity level 1 [Standard Trace Level] */

#define ACPI_LV_INIT_NAMES          0x00000020
#define ACPI_LV_PARSE               0x00000040
#define ACPI_LV_LOAD                0x00000080
#define ACPI_LV_DISPATCH            0x00000100
#define ACPI_LV_EXEC                0x00000200
#define ACPI_LV_NAMES               0x00000400
#define ACPI_LV_OPREGION            0x00000800
#define ACPI_LV_BFIELD              0x00001000
#define ACPI_LV_TABLES              0x00002000
#define ACPI_LV_VALUES              0x00004000
#define ACPI_LV_OBJECTS             0x00008000
#define ACPI_LV_RESOURCES           0x00010000
#define ACPI_LV_USER_REQUESTS       0x00020000
#define ACPI_LV_PACKAGE             0x00040000
#define ACPI_LV_EVALUATION          0x00080000
#define ACPI_LV_VERBOSITY1          0x000FFF40 | ACPI_LV_ALL_EXCEPTIONS

/* Trace verbosity level 2 [Function tracing and memory allocation] */

#define ACPI_LV_ALLOCATIONS         0x00100000
#define ACPI_LV_FUNCTIONS           0x00200000
#define ACPI_LV_OPTIMIZATIONS       0x00400000
#define ACPI_LV_PARSE_TREES         0x00800000
#define ACPI_LV_VERBOSITY2          0x00F00000 | ACPI_LV_VERBOSITY1
#define ACPI_LV_ALL                 ACPI_LV_VERBOSITY2

/* Trace verbosity level 3 [Threading, I/O, and Interrupts] */

#define ACPI_LV_MUTEX               0x01000000
#define ACPI_LV_THREADS             0x02000000
#define ACPI_LV_IO                  0x04000000
#define ACPI_LV_INTERRUPTS          0x08000000
#define ACPI_LV_VERBOSITY3          0x0F000000 | ACPI_LV_VERBOSITY2

/* Exceptionally verbose output -- also used in the global "DebugLevel"  */

#define ACPI_LV_AML_DISASSEMBLE     0x10000000
#define ACPI_LV_VERBOSE_INFO        0x20000000
#define ACPI_LV_FULL_TABLES         0x40000000
#define ACPI_LV_EVENTS              0x80000000
#define ACPI_LV_VERBOSE             0xF0000000


/*
 * Debug level macros that are used in the DEBUG_PRINT macros
 */
#define ACPI_DEBUG_LEVEL(dl)        (UINT32) dl,ACPI_DEBUG_PARAMETERS

/*
 * Exception level -- used in the global "DebugLevel"
 *
 * Note: For errors, use the ACPI_ERROR or ACPI_EXCEPTION interfaces.
 * For warnings, use ACPI_WARNING.
 */
#define ACPI_DB_INIT                ACPI_DEBUG_LEVEL (ACPI_LV_INIT)
#define ACPI_DB_DEBUG_OBJECT        ACPI_DEBUG_LEVEL (ACPI_LV_DEBUG_OBJECT)
#define ACPI_DB_INFO                ACPI_DEBUG_LEVEL (ACPI_LV_INFO)
#define ACPI_DB_REPAIR              ACPI_DEBUG_LEVEL (ACPI_LV_REPAIR)
#define ACPI_DB_TRACE_POINT         ACPI_DEBUG_LEVEL (ACPI_LV_TRACE_POINT)
#define ACPI_DB_ALL_EXCEPTIONS      ACPI_DEBUG_LEVEL (ACPI_LV_ALL_EXCEPTIONS)

/* Trace level -- also used in the global "DebugLevel" */

#define ACPI_DB_INIT_NAMES          ACPI_DEBUG_LEVEL (ACPI_LV_INIT_NAMES)
#define ACPI_DB_THREADS             ACPI_DEBUG_LEVEL (ACPI_LV_THREADS)
#define ACPI_DB_PARSE               ACPI_DEBUG_LEVEL (ACPI_LV_PARSE)
#define ACPI_DB_DISPATCH            ACPI_DEBUG_LEVEL (ACPI_LV_DISPATCH)
#define ACPI_DB_LOAD                ACPI_DEBUG_LEVEL (ACPI_LV_LOAD)
#define ACPI_DB_EXEC                ACPI_DEBUG_LEVEL (ACPI_LV_EXEC)
#define ACPI_DB_NAMES               ACPI_DEBUG_LEVEL (ACPI_LV_NAMES)
#define ACPI_DB_OPREGION            ACPI_DEBUG_LEVEL (ACPI_LV_OPREGION)
#define ACPI_DB_BFIELD              ACPI_DEBUG_LEVEL (ACPI_LV_BFIELD)
#define ACPI_DB_TABLES              ACPI_DEBUG_LEVEL (ACPI_LV_TABLES)
#define ACPI_DB_FUNCTIONS           ACPI_DEBUG_LEVEL (ACPI_LV_FUNCTIONS)
#define ACPI_DB_OPTIMIZATIONS       ACPI_DEBUG_LEVEL (ACPI_LV_OPTIMIZATIONS)
#define ACPI_DB_PARSE_TREES         ACPI_DEBUG_LEVEL (ACPI_LV_PARSE_TREES)
#define ACPI_DB_VALUES              ACPI_DEBUG_LEVEL (ACPI_LV_VALUES)
#define ACPI_DB_OBJECTS             ACPI_DEBUG_LEVEL (ACPI_LV_OBJECTS)
#define ACPI_DB_ALLOCATIONS         ACPI_DEBUG_LEVEL (ACPI_LV_ALLOCATIONS)
#define ACPI_DB_RESOURCES           ACPI_DEBUG_LEVEL (ACPI_LV_RESOURCES)
#define ACPI_DB_IO                  ACPI_DEBUG_LEVEL (ACPI_LV_IO)
#define ACPI_DB_INTERRUPTS          ACPI_DEBUG_LEVEL (ACPI_LV_INTERRUPTS)
#define ACPI_DB_USER_REQUESTS       ACPI_DEBUG_LEVEL (ACPI_LV_USER_REQUESTS)
#define ACPI_DB_PACKAGE             ACPI_DEBUG_LEVEL (ACPI_LV_PACKAGE)
#define ACPI_DB_EVALUATION          ACPI_DEBUG_LEVEL (ACPI_LV_EVALUATION)
#define ACPI_DB_MUTEX               ACPI_DEBUG_LEVEL (ACPI_LV_MUTEX)
#define ACPI_DB_EVENTS              ACPI_DEBUG_LEVEL (ACPI_LV_EVENTS)

#define ACPI_DB_ALL                 ACPI_DEBUG_LEVEL (ACPI_LV_ALL)

/* Defaults for DebugLevel, debug and normal */

#define ACPI_DEBUG_DEFAULT          (ACPI_LV_INIT | ACPI_LV_DEBUG_OBJECT | ACPI_LV_EVALUATION | ACPI_LV_REPAIR)
#define ACPI_NORMAL_DEFAULT         (ACPI_LV_INIT | ACPI_LV_DEBUG_OBJECT | ACPI_LV_REPAIR)
#define ACPI_DEBUG_ALL              (ACPI_LV_AML_DISASSEMBLE | ACPI_LV_ALL_EXCEPTIONS | ACPI_LV_ALL)


/*
 * Global trace flags
 */
#define ACPI_TRACE_ENABLED          ((UINT32) 4)
#define ACPI_TRACE_ONESHOT          ((UINT32) 2)
#define ACPI_TRACE_OPCODE           ((UINT32) 1)

/* Defaults for trace debugging level/layer */

#define ACPI_TRACE_LEVEL_ALL        ACPI_LV_ALL
#define ACPI_TRACE_LAYER_ALL        0x000001FF
#define ACPI_TRACE_LEVEL_DEFAULT    ACPI_LV_TRACE_POINT
#define ACPI_TRACE_LAYER_DEFAULT    ACPI_EXECUTER


#if defined (ACPI_DEBUG_OUTPUT) || !defined (ACPI_NO_ERROR_MESSAGES)
/*
 * The module name is used primarily for error and debug messages.
 * The __FILE__ macro is not very useful for this, because it
 * usually includes the entire pathname to the module making the
 * debug output difficult to read.
 */
#define ACPI_MODULE_NAME(Name)          static const char ACPI_UNUSED_VAR _AcpiModuleName[] = Name;
#else
/*
 * For the no-debug and no-error-msg cases, we must at least define
 * a null module name.
 */
#define ACPI_MODULE_NAME(Name)
#define _AcpiModuleName ""
#endif

/*
 * Ascii error messages can be configured out
 */
#ifndef ACPI_NO_ERROR_MESSAGES
#define AE_INFO                         _AcpiModuleName, __LINE__

/*
 * Error reporting. Callers module and line number are inserted by AE_INFO,
 * the plist contains a set of parens to allow variable-length lists.
 * These macros are used for both the debug and non-debug versions of the code.
 */
#define ACPI_INFO(plist)                AcpiInfo plist
#define ACPI_WARNING(plist)             AcpiWarning plist
#define ACPI_EXCEPTION(plist)           AcpiException plist
#define ACPI_ERROR(plist)               AcpiError plist
#define ACPI_BIOS_WARNING(plist)        AcpiBiosWarning plist
#define ACPI_BIOS_EXCEPTION(plist)      AcpiBiosException plist
#define ACPI_BIOS_ERROR(plist)          AcpiBiosError plist
#define ACPI_DEBUG_OBJECT(obj,l,i)      AcpiExDoDebugObject(obj,l,i)

#else

/* No error messages */

#define ACPI_INFO(plist)
#define ACPI_WARNING(plist)
#define ACPI_EXCEPTION(plist)
#define ACPI_ERROR(plist)
#define ACPI_BIOS_WARNING(plist)
#define ACPI_BIOS_EXCEPTION(plist)
#define ACPI_BIOS_ERROR(plist)
#define ACPI_DEBUG_OBJECT(obj,l,i)

#endif /* ACPI_NO_ERROR_MESSAGES */


/*
 * Debug macros that are conditionally compiled
 */
#ifdef ACPI_DEBUG_OUTPUT

/*
 * If ACPI_GET_FUNCTION_NAME was not defined in the compiler-dependent header,
 * define it now. This is the case where there the compiler does not support
 * a __FUNCTION__ macro or equivalent.
 */
#ifndef ACPI_GET_FUNCTION_NAME
#define ACPI_GET_FUNCTION_NAME          _AcpiFunctionName

/*
 * The Name parameter should be the procedure name as a non-quoted string.
 * The function name is also used by the function exit macros below.
 * Note: (const char) is used to be compatible with the debug interfaces
 * and macros such as __FUNCTION__.
 */
#define ACPI_FUNCTION_NAME(Name)        static const char _AcpiFunctionName[] = #Name;

#else
/* Compiler supports __FUNCTION__ (or equivalent) -- Ignore this macro */

#define ACPI_FUNCTION_NAME(Name)
#endif /* ACPI_GET_FUNCTION_NAME */

/*
 * Common parameters used for debug output functions:
 * line number, function name, module(file) name, component ID
 */
#define ACPI_DEBUG_PARAMETERS \
    __LINE__, ACPI_GET_FUNCTION_NAME, _AcpiModuleName, _COMPONENT

/* Check if debug output is currently dynamically enabled */

#define ACPI_IS_DEBUG_ENABLED(Level, Component) \
    ((Level & AcpiDbgLevel) && (Component & AcpiDbgLayer))

/*
 * Master debug print macros
 * Print message if and only if:
 *    1) Debug print for the current component is enabled
 *    2) Debug error level or trace level for the print statement is enabled
 *
 * November 2012: Moved the runtime check for whether to actually emit the
 * debug message outside of the print function itself. This improves overall
 * performance at a relatively small code cost. Implementation involves the
 * use of variadic macros supported by C99.
 *
 * Note: the ACPI_DO_WHILE0 macro is used to prevent some compilers from
 * complaining about these constructs. On other compilers the do...while
 * adds some extra code, so this feature is optional.
 */
#ifdef ACPI_USE_DO_WHILE_0
#define ACPI_DO_WHILE0(a)               do a while(0)
#else
#define ACPI_DO_WHILE0(a)               a
#endif

/* DEBUG_PRINT functions */

#ifndef COMPILER_VA_MACRO

#define ACPI_DEBUG_PRINT(plist)         AcpiDebugPrint plist
#define ACPI_DEBUG_PRINT_RAW(plist)     AcpiDebugPrintRaw plist

#else

/* Helper macros for DEBUG_PRINT */

#define ACPI_DO_DEBUG_PRINT(Function, Level, Line, Filename, Modulename, Component, ...) \
    ACPI_DO_WHILE0 ({ \
        if (ACPI_IS_DEBUG_ENABLED (Level, Component)) \
        { \
            Function (Level, Line, Filename, Modulename, Component, __VA_ARGS__); \
        } \
    })

#define ACPI_ACTUAL_DEBUG(Level, Line, Filename, Modulename, Component, ...) \
    ACPI_DO_DEBUG_PRINT (AcpiDebugPrint, Level, Line, \
        Filename, Modulename, Component, __VA_ARGS__)

#define ACPI_ACTUAL_DEBUG_RAW(Level, Line, Filename, Modulename, Component, ...) \
    ACPI_DO_DEBUG_PRINT (AcpiDebugPrintRaw, Level, Line, \
        Filename, Modulename, Component, __VA_ARGS__)

#define ACPI_DEBUG_PRINT(plist)         ACPI_ACTUAL_DEBUG plist
#define ACPI_DEBUG_PRINT_RAW(plist)     ACPI_ACTUAL_DEBUG_RAW plist

#endif


/*
 * Function entry tracing
 *
 * The name of the function is emitted as a local variable that is
 * intended to be used by both the entry trace and the exit trace.
 */

/* Helper macro */

#define ACPI_TRACE_ENTRY(Name, Function, Type, Param) \
    ACPI_FUNCTION_NAME (Name) \
    Function (ACPI_DEBUG_PARAMETERS, (Type) (Param))

/* The actual entry trace macros */

#define ACPI_FUNCTION_TRACE(Name) \
    ACPI_FUNCTION_NAME(Name) \
    AcpiUtTrace (ACPI_DEBUG_PARAMETERS)

#define ACPI_FUNCTION_TRACE_PTR(Name, Pointer) \
    ACPI_TRACE_ENTRY (Name, AcpiUtTracePtr, void *, Pointer)

#define ACPI_FUNCTION_TRACE_U32(Name, Value) \
    ACPI_TRACE_ENTRY (Name, AcpiUtTraceU32, UINT32, Value)

#define ACPI_FUNCTION_TRACE_STR(Name, String) \
    ACPI_TRACE_ENTRY (Name, AcpiUtTraceStr, const char *, String)

#define ACPI_FUNCTION_ENTRY() \
    AcpiUtTrackStackPtr()


/*
 * Function exit tracing
 *
 * These macros include a return statement. This is usually considered
 * bad form, but having a separate exit macro before the actual return
 * is very ugly and difficult to maintain.
 *
 * One of the FUNCTION_TRACE macros above must be used in conjunction
 * with these macros so that "_AcpiFunctionName" is defined.
 *
 * There are two versions of most of the return macros. The default version is
 * safer, since it avoids side-effects by guaranteeing that the argument will
 * not be evaluated twice.
 *
 * A less-safe version of the macros is provided for optional use if the
 * compiler uses excessive CPU stack (for example, this may happen in the
 * debug case if code optimzation is disabled.)
 */

/* Exit trace helper macro */

#ifndef ACPI_SIMPLE_RETURN_MACROS

#define ACPI_TRACE_EXIT(Function, Type, Param) \
    ACPI_DO_WHILE0 ({ \
        register Type _Param = (Type) (Param); \
        Function (ACPI_DEBUG_PARAMETERS, _Param); \
        return (_Param); \
    })

#else /* Use original less-safe macros */

#define ACPI_TRACE_EXIT(Function, Type, Param) \
    ACPI_DO_WHILE0 ({ \
        Function (ACPI_DEBUG_PARAMETERS, (Type) (Param)); \
        return (Param); \
    })

#endif /* ACPI_SIMPLE_RETURN_MACROS */

/* The actual exit macros */

#define return_VOID \
    ACPI_DO_WHILE0 ({ \
        AcpiUtExit (ACPI_DEBUG_PARAMETERS); \
        return; \
    })

#define return_ACPI_STATUS(Status) \
    ACPI_TRACE_EXIT (AcpiUtStatusExit, ACPI_STATUS, Status)

#define return_PTR(Pointer) \
    ACPI_TRACE_EXIT (AcpiUtPtrExit, void *, Pointer)

#define return_STR(String) \
    ACPI_TRACE_EXIT (AcpiUtStrExit, const char *, String)

#define return_VALUE(Value) \
    ACPI_TRACE_EXIT (AcpiUtValueExit, UINT64, Value)

#define return_UINT32(Value) \
    ACPI_TRACE_EXIT (AcpiUtValueExit, UINT32, Value)

#define return_UINT8(Value) \
    ACPI_TRACE_EXIT (AcpiUtValueExit, UINT8, Value)

/* Conditional execution */

#define ACPI_DEBUG_EXEC(a)              a
#define ACPI_DEBUG_ONLY_MEMBERS(a)      a;
#define _VERBOSE_STRUCTURES


/* Various object display routines for debug */

#define ACPI_DUMP_STACK_ENTRY(a)        AcpiExDumpOperand((a), 0)
#define ACPI_DUMP_OPERANDS(a, b ,c)     AcpiExDumpOperands(a, b, c)
#define ACPI_DUMP_ENTRY(a, b)           AcpiNsDumpEntry (a, b)
#define ACPI_DUMP_PATHNAME(a, b, c, d)  AcpiNsDumpPathname(a, b, c, d)
#define ACPI_DUMP_BUFFER(a, b)          AcpiUtDebugDumpBuffer((UINT8 *) a, b, DB_BYTE_DISPLAY, _COMPONENT)

#define ACPI_TRACE_POINT(a, b, c, d)    AcpiTracePoint (a, b, c, d)

#else /* ACPI_DEBUG_OUTPUT */
/*
 * This is the non-debug case -- make everything go away,
 * leaving no executable debug code!
 */
#define ACPI_DEBUG_PRINT(pl)
#define ACPI_DEBUG_PRINT_RAW(pl)
#define ACPI_DEBUG_EXEC(a)
#define ACPI_DEBUG_ONLY_MEMBERS(a)
#define ACPI_FUNCTION_NAME(a)
#define ACPI_FUNCTION_TRACE(a)
#define ACPI_FUNCTION_TRACE_PTR(a, b)
#define ACPI_FUNCTION_TRACE_U32(a, b)
#define ACPI_FUNCTION_TRACE_STR(a, b)
#define ACPI_FUNCTION_ENTRY()
#define ACPI_DUMP_STACK_ENTRY(a)
#define ACPI_DUMP_OPERANDS(a, b, c)
#define ACPI_DUMP_ENTRY(a, b)
#define ACPI_DUMP_PATHNAME(a, b, c, d)
#define ACPI_DUMP_BUFFER(a, b)
#define ACPI_IS_DEBUG_ENABLED(Level, Component) 0
#define ACPI_TRACE_POINT(a, b, c, d)

/* Return macros must have a return statement at the minimum */

#define return_VOID                     return
#define return_ACPI_STATUS(s)           return(s)
#define return_PTR(s)                   return(s)
#define return_STR(s)                   return(s)
#define return_VALUE(s)                 return(s)
#define return_UINT8(s)                 return(s)
#define return_UINT32(s)                return(s)

#endif /* ACPI_DEBUG_OUTPUT */


#endif /* __ACOUTPUT_H__ */
