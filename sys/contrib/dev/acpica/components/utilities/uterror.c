/*******************************************************************************
 *
 * Module Name: uterror - Various internal error/warning output functions
 *
 ******************************************************************************/

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

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/acnamesp.h>


#define _COMPONENT          ACPI_UTILITIES
        ACPI_MODULE_NAME    ("uterror")


/*
 * This module contains internal error functions that may
 * be configured out.
 */
#if !defined (ACPI_NO_ERROR_MESSAGES)

/*******************************************************************************
 *
 * FUNCTION:    AcpiUtPredefinedWarning
 *
 * PARAMETERS:  ModuleName      - Caller's module name (for error output)
 *              LineNumber      - Caller's line number (for error output)
 *              Pathname        - Full pathname to the node
 *              NodeFlags       - From Namespace node for the method/object
 *              Format          - Printf format string + additional args
 *
 * RETURN:      None
 *
 * DESCRIPTION: Warnings for the predefined validation module. Messages are
 *              only emitted the first time a problem with a particular
 *              method/object is detected. This prevents a flood of error
 *              messages for methods that are repeatedly evaluated.
 *
 ******************************************************************************/

void ACPI_INTERNAL_VAR_XFACE
AcpiUtPredefinedWarning (
    const char              *ModuleName,
    UINT32                  LineNumber,
    char                    *Pathname,
    UINT8                   NodeFlags,
    const char              *Format,
    ...)
{
    va_list                 ArgList;


    /*
     * Warning messages for this method/object will be disabled after the
     * first time a validation fails or an object is successfully repaired.
     */
    if (NodeFlags & ANOBJ_EVALUATED)
    {
        return;
    }

    AcpiOsPrintf (ACPI_MSG_WARNING "%s: ", Pathname);

    va_start (ArgList, Format);
    AcpiOsVprintf (Format, ArgList);
    ACPI_MSG_SUFFIX;
    va_end (ArgList);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtPredefinedInfo
 *
 * PARAMETERS:  ModuleName      - Caller's module name (for error output)
 *              LineNumber      - Caller's line number (for error output)
 *              Pathname        - Full pathname to the node
 *              NodeFlags       - From Namespace node for the method/object
 *              Format          - Printf format string + additional args
 *
 * RETURN:      None
 *
 * DESCRIPTION: Info messages for the predefined validation module. Messages
 *              are only emitted the first time a problem with a particular
 *              method/object is detected. This prevents a flood of
 *              messages for methods that are repeatedly evaluated.
 *
 ******************************************************************************/

void ACPI_INTERNAL_VAR_XFACE
AcpiUtPredefinedInfo (
    const char              *ModuleName,
    UINT32                  LineNumber,
    char                    *Pathname,
    UINT8                   NodeFlags,
    const char              *Format,
    ...)
{
    va_list                 ArgList;


    /*
     * Warning messages for this method/object will be disabled after the
     * first time a validation fails or an object is successfully repaired.
     */
    if (NodeFlags & ANOBJ_EVALUATED)
    {
        return;
    }

    AcpiOsPrintf (ACPI_MSG_INFO "%s: ", Pathname);

    va_start (ArgList, Format);
    AcpiOsVprintf (Format, ArgList);
    ACPI_MSG_SUFFIX;
    va_end (ArgList);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtPredefinedBiosError
 *
 * PARAMETERS:  ModuleName      - Caller's module name (for error output)
 *              LineNumber      - Caller's line number (for error output)
 *              Pathname        - Full pathname to the node
 *              NodeFlags       - From Namespace node for the method/object
 *              Format          - Printf format string + additional args
 *
 * RETURN:      None
 *
 * DESCRIPTION: BIOS error message for predefined names. Messages
 *              are only emitted the first time a problem with a particular
 *              method/object is detected. This prevents a flood of
 *              messages for methods that are repeatedly evaluated.
 *
 ******************************************************************************/

void ACPI_INTERNAL_VAR_XFACE
AcpiUtPredefinedBiosError (
    const char              *ModuleName,
    UINT32                  LineNumber,
    char                    *Pathname,
    UINT8                   NodeFlags,
    const char              *Format,
    ...)
{
    va_list                 ArgList;


    /*
     * Warning messages for this method/object will be disabled after the
     * first time a validation fails or an object is successfully repaired.
     */
    if (NodeFlags & ANOBJ_EVALUATED)
    {
        return;
    }

    AcpiOsPrintf (ACPI_MSG_BIOS_ERROR "%s: ", Pathname);

    va_start (ArgList, Format);
    AcpiOsVprintf (Format, ArgList);
    ACPI_MSG_SUFFIX;
    va_end (ArgList);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtPrefixedNamespaceError
 *
 * PARAMETERS:  ModuleName          - Caller's module name (for error output)
 *              LineNumber          - Caller's line number (for error output)
 *              PrefixScope         - Scope/Path that prefixes the internal path
 *              InternalPath        - Name or path of the namespace node
 *              LookupStatus        - Exception code from NS lookup
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print error message with the full pathname constructed this way:
 *
 *                  PrefixScopeNodeFullPath.ExternalizedInternalPath
 *
 * NOTE:        10/2017: Treat the major NsLookup errors as firmware errors
 *
 ******************************************************************************/

void
AcpiUtPrefixedNamespaceError (
    const char              *ModuleName,
    UINT32                  LineNumber,
    ACPI_GENERIC_STATE      *PrefixScope,
    const char              *InternalPath,
    ACPI_STATUS             LookupStatus)
{
    char                    *FullPath;
    const char              *Message;


    /*
     * Main cases:
     * 1) Object creation, object must not already exist
     * 2) Object lookup, object must exist
     */
    switch (LookupStatus)
    {
    case AE_ALREADY_EXISTS:

        AcpiOsPrintf (ACPI_MSG_BIOS_ERROR);
        Message = "Failure creating named object";
        break;

    case AE_NOT_FOUND:

        AcpiOsPrintf (ACPI_MSG_BIOS_ERROR);
        Message = "Could not resolve symbol";
        break;

    default:

        AcpiOsPrintf (ACPI_MSG_ERROR);
        Message = "Failure resolving symbol";
        break;
    }

    /* Concatenate the prefix path and the internal path */

    FullPath = AcpiNsBuildPrefixedPathname (PrefixScope, InternalPath);

    AcpiOsPrintf ("%s [%s], %s", Message,
        FullPath ? FullPath : "Could not get pathname",
        AcpiFormatException (LookupStatus));

    if (FullPath)
    {
        ACPI_FREE (FullPath);
    }

    ACPI_MSG_SUFFIX;
}


#ifdef __OBSOLETE_FUNCTION
/*******************************************************************************
 *
 * FUNCTION:    AcpiUtNamespaceError
 *
 * PARAMETERS:  ModuleName          - Caller's module name (for error output)
 *              LineNumber          - Caller's line number (for error output)
 *              InternalName        - Name or path of the namespace node
 *              LookupStatus        - Exception code from NS lookup
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print error message with the full pathname for the NS node.
 *
 ******************************************************************************/

void
AcpiUtNamespaceError (
    const char              *ModuleName,
    UINT32                  LineNumber,
    const char              *InternalName,
    ACPI_STATUS             LookupStatus)
{
    ACPI_STATUS             Status;
    UINT32                  BadName;
    char                    *Name = NULL;


    ACPI_MSG_REDIRECT_BEGIN;
    AcpiOsPrintf (ACPI_MSG_ERROR);

    if (LookupStatus == AE_BAD_CHARACTER)
    {
        /* There is a non-ascii character in the name */

        ACPI_MOVE_32_TO_32 (&BadName, ACPI_CAST_PTR (UINT32, InternalName));
        AcpiOsPrintf ("[0x%.8X] (NON-ASCII)", BadName);
    }
    else
    {
        /* Convert path to external format */

        Status = AcpiNsExternalizeName (
            ACPI_UINT32_MAX, InternalName, NULL, &Name);

        /* Print target name */

        if (ACPI_SUCCESS (Status))
        {
            AcpiOsPrintf ("[%s]", Name);
        }
        else
        {
            AcpiOsPrintf ("[COULD NOT EXTERNALIZE NAME]");
        }

        if (Name)
        {
            ACPI_FREE (Name);
        }
    }

    AcpiOsPrintf (" Namespace lookup failure, %s",
        AcpiFormatException (LookupStatus));

    ACPI_MSG_SUFFIX;
    ACPI_MSG_REDIRECT_END;
}
#endif

/*******************************************************************************
 *
 * FUNCTION:    AcpiUtMethodError
 *
 * PARAMETERS:  ModuleName          - Caller's module name (for error output)
 *              LineNumber          - Caller's line number (for error output)
 *              Message             - Error message to use on failure
 *              PrefixNode          - Prefix relative to the path
 *              Path                - Path to the node (optional)
 *              MethodStatus        - Execution status
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print error message with the full pathname for the method.
 *
 ******************************************************************************/

void
AcpiUtMethodError (
    const char              *ModuleName,
    UINT32                  LineNumber,
    const char              *Message,
    ACPI_NAMESPACE_NODE     *PrefixNode,
    const char              *Path,
    ACPI_STATUS             MethodStatus)
{
    ACPI_STATUS             Status;
    ACPI_NAMESPACE_NODE     *Node = PrefixNode;


    ACPI_MSG_REDIRECT_BEGIN;
    AcpiOsPrintf (ACPI_MSG_ERROR);

    if (Path)
    {
        Status = AcpiNsGetNode (PrefixNode, Path,
            ACPI_NS_NO_UPSEARCH, &Node);
        if (ACPI_FAILURE (Status))
        {
            AcpiOsPrintf ("[Could not get node by pathname]");
        }
    }

    AcpiNsPrintNodePathname (Node, Message);
    AcpiOsPrintf (" due to previous error (%s)",
        AcpiFormatException (MethodStatus));

    ACPI_MSG_SUFFIX;
    ACPI_MSG_REDIRECT_END;
}

#endif /* ACPI_NO_ERROR_MESSAGES */
