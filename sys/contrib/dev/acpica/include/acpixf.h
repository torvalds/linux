/******************************************************************************
 *
 * Name: acpixf.h - External interfaces to the ACPI subsystem
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

#ifndef __ACXFACE_H__
#define __ACXFACE_H__

/* Current ACPICA subsystem version in YYYYMMDD format */

#define ACPI_CA_VERSION                 0x20190215

#include <contrib/dev/acpica/include/acconfig.h>
#include <contrib/dev/acpica/include/actypes.h>
#include <contrib/dev/acpica/include/actbl.h>
#include <contrib/dev/acpica/include/acbuffer.h>


/*****************************************************************************
 *
 * Macros used for ACPICA globals and configuration
 *
 ****************************************************************************/

/*
 * Ensure that global variables are defined and initialized only once.
 *
 * The use of these macros allows for a single list of globals (here)
 * in order to simplify maintenance of the code.
 */
#ifdef DEFINE_ACPI_GLOBALS
#define ACPI_GLOBAL(type,name) \
    extern type name; \
    type name

#define ACPI_INIT_GLOBAL(type,name,value) \
    type name=value

#else
#ifndef ACPI_GLOBAL
#define ACPI_GLOBAL(type,name) \
    extern type name
#endif

#ifndef ACPI_INIT_GLOBAL
#define ACPI_INIT_GLOBAL(type,name,value) \
    extern type name
#endif
#endif

/*
 * These macros configure the various ACPICA interfaces. They are
 * useful for generating stub inline functions for features that are
 * configured out of the current kernel or ACPICA application.
 */
#ifndef ACPI_EXTERNAL_RETURN_STATUS
#define ACPI_EXTERNAL_RETURN_STATUS(Prototype) \
    Prototype;
#endif

#ifndef ACPI_EXTERNAL_RETURN_OK
#define ACPI_EXTERNAL_RETURN_OK(Prototype) \
    Prototype;
#endif

#ifndef ACPI_EXTERNAL_RETURN_VOID
#define ACPI_EXTERNAL_RETURN_VOID(Prototype) \
    Prototype;
#endif

#ifndef ACPI_EXTERNAL_RETURN_UINT32
#define ACPI_EXTERNAL_RETURN_UINT32(Prototype) \
    Prototype;
#endif

#ifndef ACPI_EXTERNAL_RETURN_PTR
#define ACPI_EXTERNAL_RETURN_PTR(Prototype) \
    Prototype;
#endif


/*****************************************************************************
 *
 * Public globals and runtime configuration options
 *
 ****************************************************************************/

/*
 * Enable "slack mode" of the AML interpreter?  Default is FALSE, and the
 * interpreter strictly follows the ACPI specification. Setting to TRUE
 * allows the interpreter to ignore certain errors and/or bad AML constructs.
 *
 * Currently, these features are enabled by this flag:
 *
 * 1) Allow "implicit return" of last value in a control method
 * 2) Allow access beyond the end of an operation region
 * 3) Allow access to uninitialized locals/args (auto-init to integer 0)
 * 4) Allow ANY object type to be a source operand for the Store() operator
 * 5) Allow unresolved references (invalid target name) in package objects
 * 6) Enable warning messages for behavior that is not ACPI spec compliant
 */
ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_EnableInterpreterSlack, FALSE);

/*
 * Automatically serialize all methods that create named objects? Default
 * is TRUE, meaning that all NonSerialized methods are scanned once at
 * table load time to determine those that create named objects. Methods
 * that create named objects are marked Serialized in order to prevent
 * possible run-time problems if they are entered by more than one thread.
 */
ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_AutoSerializeMethods, TRUE);

/*
 * Create the predefined _OSI method in the namespace? Default is TRUE
 * because ACPICA is fully compatible with other ACPI implementations.
 * Changing this will revert ACPICA (and machine ASL) to pre-OSI behavior.
 */
ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_CreateOsiMethod, TRUE);

/*
 * Optionally use default values for the ACPI register widths. Set this to
 * TRUE to use the defaults, if an FADT contains incorrect widths/lengths.
 */
ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_UseDefaultRegisterWidths, TRUE);

/*
 * Whether or not to validate (map) an entire table to verify
 * checksum/duplication in early stage before install. Set this to TRUE to
 * allow early table validation before install it to the table manager.
 * Note that enabling this option causes errors to happen in some OSPMs
 * during early initialization stages. Default behavior is to allow such
 * validation.
 */
ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_EnableTableValidation, TRUE);

/*
 * Optionally enable output from the AML Debug Object.
 */
ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_EnableAmlDebugObject, FALSE);

/*
 * Optionally copy the entire DSDT to local memory (instead of simply
 * mapping it.) There are some BIOSs that corrupt or replace the original
 * DSDT, creating the need for this option. Default is FALSE, do not copy
 * the DSDT.
 */
ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_CopyDsdtLocally, FALSE);

/*
 * Optionally ignore an XSDT if present and use the RSDT instead.
 * Although the ACPI specification requires that an XSDT be used instead
 * of the RSDT, the XSDT has been found to be corrupt or ill-formed on
 * some machines. Default behavior is to use the XSDT if present.
 */
ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_DoNotUseXsdt, FALSE);

/*
 * Optionally use 32-bit FADT addresses if and when there is a conflict
 * (address mismatch) between the 32-bit and 64-bit versions of the
 * address. Although ACPICA adheres to the ACPI specification which
 * requires the use of the corresponding 64-bit address if it is non-zero,
 * some machines have been found to have a corrupted non-zero 64-bit
 * address. Default is FALSE, do not favor the 32-bit addresses.
 */
ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_Use32BitFadtAddresses, FALSE);

/*
 * Optionally use 32-bit FACS table addresses.
 * It is reported that some platforms fail to resume from system suspending
 * if 64-bit FACS table address is selected:
 * https://bugzilla.kernel.org/show_bug.cgi?id=74021
 * Default is TRUE, favor the 32-bit addresses.
 */
ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_Use32BitFacsAddresses, TRUE);

/*
 * Optionally truncate I/O addresses to 16 bits. Provides compatibility
 * with other ACPI implementations. NOTE: During ACPICA initialization,
 * this value is set to TRUE if any Windows OSI strings have been
 * requested by the BIOS.
 */
ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_TruncateIoAddresses, FALSE);

/*
 * Disable runtime checking and repair of values returned by control methods.
 * Use only if the repair is causing a problem on a particular machine.
 */
ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_DisableAutoRepair, FALSE);

/*
 * Optionally do not install any SSDTs from the RSDT/XSDT during initialization.
 * This can be useful for debugging ACPI problems on some machines.
 */
ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_DisableSsdtTableInstall, FALSE);

/*
 * Optionally enable runtime namespace override.
 */
ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_RuntimeNamespaceOverride, TRUE);

/*
 * We keep track of the latest version of Windows that has been requested by
 * the BIOS. ACPI 5.0.
 */
ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_OsiData, 0);

/*
 * ACPI 5.0 introduces the concept of a "reduced hardware platform", meaning
 * that the ACPI hardware is no longer required. A flag in the FADT indicates
 * a reduced HW machine, and that flag is duplicated here for convenience.
 */
ACPI_INIT_GLOBAL (BOOLEAN,          AcpiGbl_ReducedHardware, FALSE);

/*
 * Maximum timeout for While() loop iterations before forced method abort.
 * This mechanism is intended to prevent infinite loops during interpreter
 * execution within a host kernel.
 */
ACPI_INIT_GLOBAL (UINT32,           AcpiGbl_MaxLoopIterations, ACPI_MAX_LOOP_TIMEOUT);

/*
 * Optionally ignore AE_NOT_FOUND errors from named reference package elements
 * during DSDT/SSDT table loading. This reduces error "noise" in platforms
 * whose firmware is carrying around a bunch of unused package objects that
 * refer to non-existent named objects. However, If the AML actually tries to
 * use such a package, the unresolved element(s) will be replaced with NULL
 * elements.
 */
ACPI_INIT_GLOBAL (BOOLEAN,          AcpiGbl_IgnorePackageResolutionErrors, FALSE);

/*
 * This mechanism is used to trace a specified AML method. The method is
 * traced each time it is executed.
 */
ACPI_INIT_GLOBAL (UINT32,           AcpiGbl_TraceFlags, 0);
ACPI_INIT_GLOBAL (const char *,     AcpiGbl_TraceMethodName, NULL);
ACPI_INIT_GLOBAL (UINT32,           AcpiGbl_TraceDbgLevel, ACPI_TRACE_LEVEL_DEFAULT);
ACPI_INIT_GLOBAL (UINT32,           AcpiGbl_TraceDbgLayer, ACPI_TRACE_LAYER_DEFAULT);

/*
 * Runtime configuration of debug output control masks. We want the debug
 * switches statically initialized so they are already set when the debugger
 * is entered.
 */
#ifdef ACPI_DEBUG_OUTPUT
ACPI_INIT_GLOBAL (UINT32,           AcpiDbgLevel, ACPI_DEBUG_DEFAULT);
#else
ACPI_INIT_GLOBAL (UINT32,           AcpiDbgLevel, ACPI_NORMAL_DEFAULT);
#endif
ACPI_INIT_GLOBAL (UINT32,           AcpiDbgLayer, ACPI_COMPONENT_DEFAULT);

/* Optionally enable timer output with Debug Object output */

ACPI_INIT_GLOBAL (UINT8,            AcpiGbl_DisplayDebugTimer, FALSE);

/*
 * Debugger command handshake globals. Host OSes need to access these
 * variables to implement their own command handshake mechanism.
 */
#ifdef ACPI_DEBUGGER
ACPI_INIT_GLOBAL (BOOLEAN,          AcpiGbl_MethodExecuting, FALSE);
ACPI_GLOBAL (char,                  AcpiGbl_DbLineBuf[ACPI_DB_LINE_BUFFER_SIZE]);
#endif

/*
 * Other miscellaneous globals
 */
ACPI_GLOBAL (ACPI_TABLE_FADT,       AcpiGbl_FADT);
ACPI_GLOBAL (UINT32,                AcpiCurrentGpeCount);
ACPI_GLOBAL (BOOLEAN,               AcpiGbl_SystemAwakeAndRunning);


/*****************************************************************************
 *
 * ACPICA public interface configuration.
 *
 * Interfaces that are configured out of the ACPICA build are replaced
 * by inlined stubs by default.
 *
 ****************************************************************************/

/*
 * Hardware-reduced prototypes (default: Not hardware reduced).
 *
 * All ACPICA hardware-related interfaces that use these macros will be
 * configured out of the ACPICA build if the ACPI_REDUCED_HARDWARE flag
 * is set to TRUE.
 *
 * Note: This static build option for reduced hardware is intended to
 * reduce ACPICA code size if desired or necessary. However, even if this
 * option is not specified, the runtime behavior of ACPICA is dependent
 * on the actual FADT reduced hardware flag (HW_REDUCED_ACPI). If set,
 * the flag will enable similar behavior -- ACPICA will not attempt
 * to access any ACPI-relate hardware (SCI, GPEs, Fixed Events, etc.)
 */
#if (!ACPI_REDUCED_HARDWARE)
#define ACPI_HW_DEPENDENT_RETURN_STATUS(Prototype) \
    ACPI_EXTERNAL_RETURN_STATUS(Prototype)

#define ACPI_HW_DEPENDENT_RETURN_OK(Prototype) \
    ACPI_EXTERNAL_RETURN_OK(Prototype)

#define ACPI_HW_DEPENDENT_RETURN_VOID(Prototype) \
    ACPI_EXTERNAL_RETURN_VOID(Prototype)

#else
#define ACPI_HW_DEPENDENT_RETURN_STATUS(Prototype) \
    static ACPI_INLINE Prototype {return(AE_NOT_CONFIGURED);}

#define ACPI_HW_DEPENDENT_RETURN_OK(Prototype) \
    static ACPI_INLINE Prototype {return(AE_OK);}

#define ACPI_HW_DEPENDENT_RETURN_VOID(Prototype) \
    static ACPI_INLINE Prototype {return;}

#endif /* !ACPI_REDUCED_HARDWARE */


/*
 * Error message prototypes (default: error messages enabled).
 *
 * All interfaces related to error and warning messages
 * will be configured out of the ACPICA build if the
 * ACPI_NO_ERROR_MESSAGE flag is defined.
 */
#ifndef ACPI_NO_ERROR_MESSAGES
#define ACPI_MSG_DEPENDENT_RETURN_VOID(Prototype) \
    Prototype;

#else
#define ACPI_MSG_DEPENDENT_RETURN_VOID(Prototype) \
    static ACPI_INLINE Prototype {return;}

#endif /* ACPI_NO_ERROR_MESSAGES */


/*
 * Debugging output prototypes (default: no debug output).
 *
 * All interfaces related to debug output messages
 * will be configured out of the ACPICA build unless the
 * ACPI_DEBUG_OUTPUT flag is defined.
 */
#ifdef ACPI_DEBUG_OUTPUT
#define ACPI_DBG_DEPENDENT_RETURN_VOID(Prototype) \
    Prototype;

#else
#define ACPI_DBG_DEPENDENT_RETURN_VOID(Prototype) \
    static ACPI_INLINE Prototype {return;}

#endif /* ACPI_DEBUG_OUTPUT */


/*
 * Application prototypes
 *
 * All interfaces used by application will be configured
 * out of the ACPICA build unless the ACPI_APPLICATION
 * flag is defined.
 */
#ifdef ACPI_APPLICATION
#define ACPI_APP_DEPENDENT_RETURN_VOID(Prototype) \
    Prototype;

#else
#define ACPI_APP_DEPENDENT_RETURN_VOID(Prototype) \
    static ACPI_INLINE Prototype {return;}

#endif /* ACPI_APPLICATION */


/*
 * Debugger prototypes
 *
 * All interfaces used by debugger will be configured
 * out of the ACPICA build unless the ACPI_DEBUGGER
 * flag is defined.
 */
#ifdef ACPI_DEBUGGER
#define ACPI_DBR_DEPENDENT_RETURN_OK(Prototype) \
    ACPI_EXTERNAL_RETURN_OK(Prototype)

#define ACPI_DBR_DEPENDENT_RETURN_VOID(Prototype) \
    ACPI_EXTERNAL_RETURN_VOID(Prototype)

#else
#define ACPI_DBR_DEPENDENT_RETURN_OK(Prototype) \
    static ACPI_INLINE Prototype {return(AE_OK);}

#define ACPI_DBR_DEPENDENT_RETURN_VOID(Prototype) \
    static ACPI_INLINE Prototype {return;}

#endif /* ACPI_DEBUGGER */


/*****************************************************************************
 *
 * ACPICA public interface prototypes
 *
 ****************************************************************************/

/*
 * Initialization
 */
ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS ACPI_INIT_FUNCTION
AcpiInitializeTables (
    ACPI_TABLE_DESC         *InitialStorage,
    UINT32                  InitialTableCount,
    BOOLEAN                 AllowResize))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS ACPI_INIT_FUNCTION
AcpiInitializeSubsystem (
    void))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS ACPI_INIT_FUNCTION
AcpiEnableSubsystem (
    UINT32                  Flags))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS ACPI_INIT_FUNCTION
AcpiInitializeObjects (
    UINT32                  Flags))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS ACPI_INIT_FUNCTION
AcpiTerminate (
    void))


/*
 * Miscellaneous global interfaces
 */
ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiEnable (
    void))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiDisable (
    void))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiSubsystemStatus (
    void))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiGetSystemInfo (
    ACPI_BUFFER             *RetBuffer))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiGetStatistics (
    ACPI_STATISTICS         *Stats))

ACPI_EXTERNAL_RETURN_PTR (
const char *
AcpiFormatException (
    ACPI_STATUS             Exception))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiPurgeCachedObjects (
    void))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiInstallInterface (
    ACPI_STRING             InterfaceName))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiRemoveInterface (
    ACPI_STRING             InterfaceName))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiUpdateInterfaces (
    UINT8                   Action))

ACPI_EXTERNAL_RETURN_UINT32 (
UINT32
AcpiCheckAddressRange (
    ACPI_ADR_SPACE_TYPE     SpaceId,
    ACPI_PHYSICAL_ADDRESS   Address,
    ACPI_SIZE               Length,
    BOOLEAN                 Warn))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiDecodePldBuffer (
    UINT8                   *InBuffer,
    ACPI_SIZE               Length,
    ACPI_PLD_INFO           **ReturnBuffer))


/*
 * ACPI table load/unload interfaces
 */
ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS ACPI_INIT_FUNCTION
AcpiInstallTable (
    ACPI_PHYSICAL_ADDRESS   Address,
    BOOLEAN                 Physical))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiLoadTable (
    ACPI_TABLE_HEADER       *Table))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiUnloadParentTable (
    ACPI_HANDLE             Object))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS ACPI_INIT_FUNCTION
AcpiLoadTables (
    void))


/*
 * ACPI table manipulation interfaces
 */
ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS ACPI_INIT_FUNCTION
AcpiReallocateRootTable (
    void))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS ACPI_INIT_FUNCTION
AcpiFindRootPointer (
    ACPI_PHYSICAL_ADDRESS   *RsdpAddress))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiGetTableHeader (
    ACPI_STRING             Signature,
    UINT32                  Instance,
    ACPI_TABLE_HEADER       *OutTableHeader))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiGetTable (
    ACPI_STRING             Signature,
    UINT32                  Instance,
    ACPI_TABLE_HEADER       **OutTable))

ACPI_EXTERNAL_RETURN_VOID (
void
AcpiPutTable (
    ACPI_TABLE_HEADER       *Table))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiGetTableByIndex (
    UINT32                  TableIndex,
    ACPI_TABLE_HEADER       **OutTable))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiInstallTableHandler (
    ACPI_TABLE_HANDLER      Handler,
    void                    *Context))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiRemoveTableHandler (
    ACPI_TABLE_HANDLER      Handler))


/*
 * Namespace and name interfaces
 */
ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiWalkNamespace (
    ACPI_OBJECT_TYPE        Type,
    ACPI_HANDLE             StartObject,
    UINT32                  MaxDepth,
    ACPI_WALK_CALLBACK      DescendingCallback,
    ACPI_WALK_CALLBACK      AscendingCallback,
    void                    *Context,
    void                    **ReturnValue))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiGetDevices (
    char                    *HID,
    ACPI_WALK_CALLBACK      UserFunction,
    void                    *Context,
    void                    **ReturnValue))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiGetName (
    ACPI_HANDLE             Object,
    UINT32                  NameType,
    ACPI_BUFFER             *RetPathPtr))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiGetHandle (
    ACPI_HANDLE             Parent,
    ACPI_STRING             Pathname,
    ACPI_HANDLE             *RetHandle))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiAttachData (
    ACPI_HANDLE             Object,
    ACPI_OBJECT_HANDLER     Handler,
    void                    *Data))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiDetachData (
    ACPI_HANDLE             Object,
    ACPI_OBJECT_HANDLER     Handler))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiGetData (
    ACPI_HANDLE             Object,
    ACPI_OBJECT_HANDLER     Handler,
    void                    **Data))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiDebugTrace (
    const char              *Name,
    UINT32                  DebugLevel,
    UINT32                  DebugLayer,
    UINT32                  Flags))


/*
 * Object manipulation and enumeration
 */
ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiEvaluateObject (
    ACPI_HANDLE             Object,
    ACPI_STRING             Pathname,
    ACPI_OBJECT_LIST        *ParameterObjects,
    ACPI_BUFFER             *ReturnObjectBuffer))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiEvaluateObjectTyped (
    ACPI_HANDLE             Object,
    ACPI_STRING             Pathname,
    ACPI_OBJECT_LIST        *ExternalParams,
    ACPI_BUFFER             *ReturnBuffer,
    ACPI_OBJECT_TYPE        ReturnType))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiGetObjectInfo (
    ACPI_HANDLE             Object,
    ACPI_DEVICE_INFO        **ReturnBuffer))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiInstallMethod (
    UINT8                   *Buffer))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiGetNextObject (
    ACPI_OBJECT_TYPE        Type,
    ACPI_HANDLE             Parent,
    ACPI_HANDLE             Child,
    ACPI_HANDLE             *OutHandle))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiGetType (
    ACPI_HANDLE             Object,
    ACPI_OBJECT_TYPE        *OutType))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiGetParent (
    ACPI_HANDLE             Object,
    ACPI_HANDLE             *OutHandle))


/*
 * Handler interfaces
 */
ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiInstallInitializationHandler (
    ACPI_INIT_HANDLER       Handler,
    UINT32                  Function))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiInstallSciHandler (
    ACPI_SCI_HANDLER        Address,
    void                    *Context))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiRemoveSciHandler (
    ACPI_SCI_HANDLER        Address))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiInstallGlobalEventHandler (
    ACPI_GBL_EVENT_HANDLER  Handler,
    void                    *Context))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiInstallFixedEventHandler (
    UINT32                  AcpiEvent,
    ACPI_EVENT_HANDLER      Handler,
    void                    *Context))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiRemoveFixedEventHandler (
    UINT32                  AcpiEvent,
    ACPI_EVENT_HANDLER      Handler))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiInstallGpeHandler (
    ACPI_HANDLE             GpeDevice,
    UINT32                  GpeNumber,
    UINT32                  Type,
    ACPI_GPE_HANDLER        Address,
    void                    *Context))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiInstallGpeRawHandler (
    ACPI_HANDLE             GpeDevice,
    UINT32                  GpeNumber,
    UINT32                  Type,
    ACPI_GPE_HANDLER        Address,
    void                    *Context))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiRemoveGpeHandler (
    ACPI_HANDLE             GpeDevice,
    UINT32                  GpeNumber,
    ACPI_GPE_HANDLER        Address))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiInstallNotifyHandler (
    ACPI_HANDLE             Device,
    UINT32                  HandlerType,
    ACPI_NOTIFY_HANDLER     Handler,
    void                    *Context))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiRemoveNotifyHandler (
    ACPI_HANDLE             Device,
    UINT32                  HandlerType,
    ACPI_NOTIFY_HANDLER     Handler))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiInstallAddressSpaceHandler (
    ACPI_HANDLE             Device,
    ACPI_ADR_SPACE_TYPE     SpaceId,
    ACPI_ADR_SPACE_HANDLER  Handler,
    ACPI_ADR_SPACE_SETUP    Setup,
    void                    *Context))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiRemoveAddressSpaceHandler (
    ACPI_HANDLE             Device,
    ACPI_ADR_SPACE_TYPE     SpaceId,
    ACPI_ADR_SPACE_HANDLER  Handler))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiInstallExceptionHandler (
    ACPI_EXCEPTION_HANDLER  Handler))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiInstallInterfaceHandler (
    ACPI_INTERFACE_HANDLER  Handler))


/*
 * Global Lock interfaces
 */
ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiAcquireGlobalLock (
    UINT16                  Timeout,
    UINT32                  *Handle))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiReleaseGlobalLock (
    UINT32                  Handle))


/*
 * Interfaces to AML mutex objects
 */
ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiAcquireMutex (
    ACPI_HANDLE             Handle,
    ACPI_STRING             Pathname,
    UINT16                  Timeout))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiReleaseMutex (
    ACPI_HANDLE             Handle,
    ACPI_STRING             Pathname))


/*
 * Fixed Event interfaces
 */
ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiEnableEvent (
    UINT32                  Event,
    UINT32                  Flags))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiDisableEvent (
    UINT32                  Event,
    UINT32                  Flags))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiClearEvent (
    UINT32                  Event))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiGetEventStatus (
    UINT32                  Event,
    ACPI_EVENT_STATUS       *EventStatus))


/*
 * General Purpose Event (GPE) Interfaces
 */
ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiUpdateAllGpes (
    void))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiEnableGpe (
    ACPI_HANDLE             GpeDevice,
    UINT32                  GpeNumber))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiDisableGpe (
    ACPI_HANDLE             GpeDevice,
    UINT32                  GpeNumber))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiClearGpe (
    ACPI_HANDLE             GpeDevice,
    UINT32                  GpeNumber))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiSetGpe (
    ACPI_HANDLE             GpeDevice,
    UINT32                  GpeNumber,
    UINT8                   Action))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiFinishGpe (
    ACPI_HANDLE             GpeDevice,
    UINT32                  GpeNumber))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiMaskGpe (
    ACPI_HANDLE             GpeDevice,
    UINT32                  GpeNumber,
    BOOLEAN                 IsMasked))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiMarkGpeForWake (
    ACPI_HANDLE             GpeDevice,
    UINT32                  GpeNumber))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiSetupGpeForWake (
    ACPI_HANDLE             ParentDevice,
    ACPI_HANDLE             GpeDevice,
    UINT32                  GpeNumber))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiSetGpeWakeMask (
    ACPI_HANDLE             GpeDevice,
    UINT32                  GpeNumber,
    UINT8                   Action))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiGetGpeStatus (
    ACPI_HANDLE             GpeDevice,
    UINT32                  GpeNumber,
    ACPI_EVENT_STATUS       *EventStatus))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiDisableAllGpes (
    void))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiEnableAllRuntimeGpes (
    void))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiEnableAllWakeupGpes (
    void))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiGetGpeDevice (
    UINT32                  GpeIndex,
    ACPI_HANDLE             *GpeDevice))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiInstallGpeBlock (
    ACPI_HANDLE             GpeDevice,
    ACPI_GENERIC_ADDRESS    *GpeBlockAddress,
    UINT32                  RegisterCount,
    UINT32                  InterruptNumber))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiRemoveGpeBlock (
    ACPI_HANDLE             GpeDevice))


/*
 * Resource interfaces
 */
typedef
ACPI_STATUS (*ACPI_WALK_RESOURCE_CALLBACK) (
    ACPI_RESOURCE           *Resource,
    void                    *Context);

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiGetVendorResource (
    ACPI_HANDLE             Device,
    char                    *Name,
    ACPI_VENDOR_UUID        *Uuid,
    ACPI_BUFFER             *RetBuffer))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiGetCurrentResources (
    ACPI_HANDLE             Device,
    ACPI_BUFFER             *RetBuffer))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiGetPossibleResources (
    ACPI_HANDLE             Device,
    ACPI_BUFFER             *RetBuffer))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiGetEventResources (
    ACPI_HANDLE             DeviceHandle,
    ACPI_BUFFER             *RetBuffer))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiWalkResourceBuffer (
    ACPI_BUFFER                 *Buffer,
    ACPI_WALK_RESOURCE_CALLBACK UserFunction,
    void                        *Context))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiWalkResources (
    ACPI_HANDLE                 Device,
    char                        *Name,
    ACPI_WALK_RESOURCE_CALLBACK UserFunction,
    void                        *Context))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiSetCurrentResources (
    ACPI_HANDLE             Device,
    ACPI_BUFFER             *InBuffer))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiGetIrqRoutingTable (
    ACPI_HANDLE             Device,
    ACPI_BUFFER             *RetBuffer))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiResourceToAddress64 (
    ACPI_RESOURCE           *Resource,
    ACPI_RESOURCE_ADDRESS64 *Out))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiBufferToResource (
    UINT8                   *AmlBuffer,
    UINT16                  AmlBufferLength,
    ACPI_RESOURCE           **ResourcePtr))


/*
 * Hardware (ACPI device) interfaces
 */
ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiReset (
    void))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiRead (
    UINT64                  *Value,
    ACPI_GENERIC_ADDRESS    *Reg))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiWrite (
    UINT64                  Value,
    ACPI_GENERIC_ADDRESS    *Reg))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiReadBitRegister (
    UINT32                  RegisterId,
    UINT32                  *ReturnValue))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiWriteBitRegister (
    UINT32                  RegisterId,
    UINT32                  Value))


/*
 * Sleep/Wake interfaces
 */
ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiGetSleepTypeData (
    UINT8                   SleepState,
    UINT8                   *Slp_TypA,
    UINT8                   *Slp_TypB))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiEnterSleepStatePrep (
    UINT8                   SleepState))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiEnterSleepState (
    UINT8                   SleepState))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiEnterSleepStateS4bios (
    void))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiLeaveSleepStatePrep (
    UINT8                   SleepState))

ACPI_EXTERNAL_RETURN_STATUS (
ACPI_STATUS
AcpiLeaveSleepState (
    UINT8                   SleepState))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiSetFirmwareWakingVector (
    ACPI_PHYSICAL_ADDRESS   PhysicalAddress,
    ACPI_PHYSICAL_ADDRESS   PhysicalAddress64))


/*
 * ACPI Timer interfaces
 */
ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiGetTimerResolution (
    UINT32                  *Resolution))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiGetTimer (
    UINT32                  *Ticks))

ACPI_HW_DEPENDENT_RETURN_STATUS (
ACPI_STATUS
AcpiGetTimerDuration (
    UINT32                  StartTicks,
    UINT32                  EndTicks,
    UINT32                  *TimeElapsed))


/*
 * Error/Warning output
 */
ACPI_MSG_DEPENDENT_RETURN_VOID (
ACPI_PRINTF_LIKE(3)
void ACPI_INTERNAL_VAR_XFACE
AcpiError (
    const char              *ModuleName,
    UINT32                  LineNumber,
    const char              *Format,
    ...))

ACPI_MSG_DEPENDENT_RETURN_VOID (
ACPI_PRINTF_LIKE(4)
void  ACPI_INTERNAL_VAR_XFACE
AcpiException (
    const char              *ModuleName,
    UINT32                  LineNumber,
    ACPI_STATUS             Status,
    const char              *Format,
    ...))

ACPI_MSG_DEPENDENT_RETURN_VOID (
ACPI_PRINTF_LIKE(3)
void ACPI_INTERNAL_VAR_XFACE
AcpiWarning (
    const char              *ModuleName,
    UINT32                  LineNumber,
    const char              *Format,
    ...))

ACPI_MSG_DEPENDENT_RETURN_VOID (
ACPI_PRINTF_LIKE(1)
void ACPI_INTERNAL_VAR_XFACE
AcpiInfo (
    const char              *Format,
    ...))

ACPI_MSG_DEPENDENT_RETURN_VOID (
ACPI_PRINTF_LIKE(3)
void ACPI_INTERNAL_VAR_XFACE
AcpiBiosError (
    const char              *ModuleName,
    UINT32                  LineNumber,
    const char              *Format,
    ...))

ACPI_MSG_DEPENDENT_RETURN_VOID (
ACPI_PRINTF_LIKE(4)
void  ACPI_INTERNAL_VAR_XFACE
AcpiBiosException (
    const char              *ModuleName,
    UINT32                  LineNumber,
    ACPI_STATUS             Status,
    const char              *Format,
    ...))

ACPI_MSG_DEPENDENT_RETURN_VOID (
ACPI_PRINTF_LIKE(3)
void ACPI_INTERNAL_VAR_XFACE
AcpiBiosWarning (
    const char              *ModuleName,
    UINT32                  LineNumber,
    const char              *Format,
    ...))


/*
 * Debug output
 */
ACPI_DBG_DEPENDENT_RETURN_VOID (
ACPI_PRINTF_LIKE(6)
void ACPI_INTERNAL_VAR_XFACE
AcpiDebugPrint (
    UINT32                  RequestedDebugLevel,
    UINT32                  LineNumber,
    const char              *FunctionName,
    const char              *ModuleName,
    UINT32                  ComponentId,
    const char              *Format,
    ...))

ACPI_DBG_DEPENDENT_RETURN_VOID (
ACPI_PRINTF_LIKE(6)
void ACPI_INTERNAL_VAR_XFACE
AcpiDebugPrintRaw (
    UINT32                  RequestedDebugLevel,
    UINT32                  LineNumber,
    const char              *FunctionName,
    const char              *ModuleName,
    UINT32                  ComponentId,
    const char              *Format,
    ...))

ACPI_DBG_DEPENDENT_RETURN_VOID (
void
AcpiTracePoint (
    ACPI_TRACE_EVENT_TYPE   Type,
    BOOLEAN                 Begin,
    UINT8                   *Aml,
    char                    *Pathname))

ACPI_STATUS
AcpiInitializeDebugger (
    void);

void
AcpiTerminateDebugger (
    void);

void
AcpiRunDebugger (
    char                    *BatchBuffer);

void
AcpiSetDebuggerThreadId (
    ACPI_THREAD_ID          ThreadId);

#endif /* __ACXFACE_H__ */
