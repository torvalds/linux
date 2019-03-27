/******************************************************************************
 *
 * Module Name: utdecode - Utility decoding routines (value-to-string)
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

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/amlcode.h>

#define _COMPONENT          ACPI_UTILITIES
        ACPI_MODULE_NAME    ("utdecode")


/*
 * Properties of the ACPI Object Types, both internal and external.
 * The table is indexed by values of ACPI_OBJECT_TYPE
 */
const UINT8                     AcpiGbl_NsProperties[ACPI_NUM_NS_TYPES] =
{
    ACPI_NS_NORMAL,                     /* 00 Any              */
    ACPI_NS_NORMAL,                     /* 01 Number           */
    ACPI_NS_NORMAL,                     /* 02 String           */
    ACPI_NS_NORMAL,                     /* 03 Buffer           */
    ACPI_NS_NORMAL,                     /* 04 Package          */
    ACPI_NS_NORMAL,                     /* 05 FieldUnit        */
    ACPI_NS_NEWSCOPE,                   /* 06 Device           */
    ACPI_NS_NORMAL,                     /* 07 Event            */
    ACPI_NS_NEWSCOPE,                   /* 08 Method           */
    ACPI_NS_NORMAL,                     /* 09 Mutex            */
    ACPI_NS_NORMAL,                     /* 10 Region           */
    ACPI_NS_NEWSCOPE,                   /* 11 Power            */
    ACPI_NS_NEWSCOPE,                   /* 12 Processor        */
    ACPI_NS_NEWSCOPE,                   /* 13 Thermal          */
    ACPI_NS_NORMAL,                     /* 14 BufferField      */
    ACPI_NS_NORMAL,                     /* 15 DdbHandle        */
    ACPI_NS_NORMAL,                     /* 16 Debug Object     */
    ACPI_NS_NORMAL,                     /* 17 DefField         */
    ACPI_NS_NORMAL,                     /* 18 BankField        */
    ACPI_NS_NORMAL,                     /* 19 IndexField       */
    ACPI_NS_NORMAL,                     /* 20 Reference        */
    ACPI_NS_NORMAL,                     /* 21 Alias            */
    ACPI_NS_NORMAL,                     /* 22 MethodAlias      */
    ACPI_NS_NORMAL,                     /* 23 Notify           */
    ACPI_NS_NORMAL,                     /* 24 Address Handler  */
    ACPI_NS_NEWSCOPE | ACPI_NS_LOCAL,   /* 25 Resource Desc    */
    ACPI_NS_NEWSCOPE | ACPI_NS_LOCAL,   /* 26 Resource Field   */
    ACPI_NS_NEWSCOPE,                   /* 27 Scope            */
    ACPI_NS_NORMAL,                     /* 28 Extra            */
    ACPI_NS_NORMAL,                     /* 29 Data             */
    ACPI_NS_NORMAL                      /* 30 Invalid          */
};


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetRegionName
 *
 * PARAMETERS:  Space ID            - ID for the region
 *
 * RETURN:      Decoded region SpaceId name
 *
 * DESCRIPTION: Translate a Space ID into a name string (Debug only)
 *
 ******************************************************************************/

/* Region type decoding */

const char        *AcpiGbl_RegionTypes[ACPI_NUM_PREDEFINED_REGIONS] =
{
    "SystemMemory",     /* 0x00 */
    "SystemIO",         /* 0x01 */
    "PCI_Config",       /* 0x02 */
    "EmbeddedControl",  /* 0x03 */
    "SMBus",            /* 0x04 */
    "SystemCMOS",       /* 0x05 */
    "PCIBARTarget",     /* 0x06 */
    "IPMI",             /* 0x07 */
    "GeneralPurposeIo", /* 0x08 */
    "GenericSerialBus", /* 0x09 */
    "PCC"               /* 0x0A */
};


const char *
AcpiUtGetRegionName (
    UINT8                   SpaceId)
{

    if (SpaceId >= ACPI_USER_REGION_BEGIN)
    {
        return ("UserDefinedRegion");
    }
    else if (SpaceId == ACPI_ADR_SPACE_DATA_TABLE)
    {
        return ("DataTable");
    }
    else if (SpaceId == ACPI_ADR_SPACE_FIXED_HARDWARE)
    {
        return ("FunctionalFixedHW");
    }
    else if (SpaceId >= ACPI_NUM_PREDEFINED_REGIONS)
    {
        return ("InvalidSpaceId");
    }

    return (AcpiGbl_RegionTypes[SpaceId]);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetEventName
 *
 * PARAMETERS:  EventId             - Fixed event ID
 *
 * RETURN:      Decoded event ID name
 *
 * DESCRIPTION: Translate a Event ID into a name string (Debug only)
 *
 ******************************************************************************/

/* Event type decoding */

static const char        *AcpiGbl_EventTypes[ACPI_NUM_FIXED_EVENTS] =
{
    "PM_Timer",
    "GlobalLock",
    "PowerButton",
    "SleepButton",
    "RealTimeClock",
};


const char *
AcpiUtGetEventName (
    UINT32                  EventId)
{

    if (EventId > ACPI_EVENT_MAX)
    {
        return ("InvalidEventID");
    }

    return (AcpiGbl_EventTypes[EventId]);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetTypeName
 *
 * PARAMETERS:  Type                - An ACPI object type
 *
 * RETURN:      Decoded ACPI object type name
 *
 * DESCRIPTION: Translate a Type ID into a name string (Debug only)
 *
 ******************************************************************************/

/*
 * Elements of AcpiGbl_NsTypeNames below must match
 * one-to-one with values of ACPI_OBJECT_TYPE
 *
 * The type ACPI_TYPE_ANY (Untyped) is used as a "don't care" when searching;
 * when stored in a table it really means that we have thus far seen no
 * evidence to indicate what type is actually going to be stored for this
 & entry.
 */
static const char           AcpiGbl_BadType[] = "UNDEFINED";

/* Printable names of the ACPI object types */

static const char           *AcpiGbl_NsTypeNames[] =
{
    /* 00 */ "Untyped",
    /* 01 */ "Integer",
    /* 02 */ "String",
    /* 03 */ "Buffer",
    /* 04 */ "Package",
    /* 05 */ "FieldUnit",
    /* 06 */ "Device",
    /* 07 */ "Event",
    /* 08 */ "Method",
    /* 09 */ "Mutex",
    /* 10 */ "Region",
    /* 11 */ "Power",
    /* 12 */ "Processor",
    /* 13 */ "Thermal",
    /* 14 */ "BufferField",
    /* 15 */ "DdbHandle",
    /* 16 */ "DebugObject",
    /* 17 */ "RegionField",
    /* 18 */ "BankField",
    /* 19 */ "IndexField",
    /* 20 */ "Reference",
    /* 21 */ "Alias",
    /* 22 */ "MethodAlias",
    /* 23 */ "Notify",
    /* 24 */ "AddrHandler",
    /* 25 */ "ResourceDesc",
    /* 26 */ "ResourceFld",
    /* 27 */ "Scope",
    /* 28 */ "Extra",
    /* 29 */ "Data",
    /* 30 */ "Invalid"
};


const char *
AcpiUtGetTypeName (
    ACPI_OBJECT_TYPE        Type)
{

    if (Type > ACPI_TYPE_INVALID)
    {
        return (AcpiGbl_BadType);
    }

    return (AcpiGbl_NsTypeNames[Type]);
}


const char *
AcpiUtGetObjectTypeName (
    ACPI_OPERAND_OBJECT     *ObjDesc)
{
    ACPI_FUNCTION_TRACE (UtGetObjectTypeName);


    if (!ObjDesc)
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Null Object Descriptor\n"));
        return_STR ("[NULL Object Descriptor]");
    }

    /* These descriptor types share a common area */

    if ((ACPI_GET_DESCRIPTOR_TYPE (ObjDesc) != ACPI_DESC_TYPE_OPERAND) &&
        (ACPI_GET_DESCRIPTOR_TYPE (ObjDesc) != ACPI_DESC_TYPE_NAMED))
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
            "Invalid object descriptor type: 0x%2.2X [%s] (%p)\n",
            ACPI_GET_DESCRIPTOR_TYPE (ObjDesc),
            AcpiUtGetDescriptorName (ObjDesc), ObjDesc));

        return_STR ("Invalid object");
    }

    return_STR (AcpiUtGetTypeName (ObjDesc->Common.Type));
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetNodeName
 *
 * PARAMETERS:  Object               - A namespace node
 *
 * RETURN:      ASCII name of the node
 *
 * DESCRIPTION: Validate the node and return the node's ACPI name.
 *
 ******************************************************************************/

const char *
AcpiUtGetNodeName (
    void                    *Object)
{
    ACPI_NAMESPACE_NODE     *Node = (ACPI_NAMESPACE_NODE *) Object;


    /* Must return a string of exactly 4 characters == ACPI_NAME_SIZE */

    if (!Object)
    {
        return ("NULL");
    }

    /* Check for Root node */

    if ((Object == ACPI_ROOT_OBJECT) ||
        (Object == AcpiGbl_RootNode))
    {
        return ("\"\\\" ");
    }

    /* Descriptor must be a namespace node */

    if (ACPI_GET_DESCRIPTOR_TYPE (Node) != ACPI_DESC_TYPE_NAMED)
    {
        return ("####");
    }

    /*
     * Ensure name is valid. The name was validated/repaired when the node
     * was created, but make sure it has not been corrupted.
     */
    AcpiUtRepairName (Node->Name.Ascii);

    /* Return the name */

    return (Node->Name.Ascii);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetDescriptorName
 *
 * PARAMETERS:  Object               - An ACPI object
 *
 * RETURN:      Decoded name of the descriptor type
 *
 * DESCRIPTION: Validate object and return the descriptor type
 *
 ******************************************************************************/

/* Printable names of object descriptor types */

static const char           *AcpiGbl_DescTypeNames[] =
{
    /* 00 */ "Not a Descriptor",
    /* 01 */ "Cached Object",
    /* 02 */ "State-Generic",
    /* 03 */ "State-Update",
    /* 04 */ "State-Package",
    /* 05 */ "State-Control",
    /* 06 */ "State-RootParseScope",
    /* 07 */ "State-ParseScope",
    /* 08 */ "State-WalkScope",
    /* 09 */ "State-Result",
    /* 10 */ "State-Notify",
    /* 11 */ "State-Thread",
    /* 12 */ "Tree Walk State",
    /* 13 */ "Parse Tree Op",
    /* 14 */ "Operand Object",
    /* 15 */ "Namespace Node"
};


const char *
AcpiUtGetDescriptorName (
    void                    *Object)
{

    if (!Object)
    {
        return ("NULL OBJECT");
    }

    if (ACPI_GET_DESCRIPTOR_TYPE (Object) > ACPI_DESC_TYPE_MAX)
    {
        return ("Not a Descriptor");
    }

    return (AcpiGbl_DescTypeNames[ACPI_GET_DESCRIPTOR_TYPE (Object)]);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetReferenceName
 *
 * PARAMETERS:  Object               - An ACPI reference object
 *
 * RETURN:      Decoded name of the type of reference
 *
 * DESCRIPTION: Decode a reference object sub-type to a string.
 *
 ******************************************************************************/

/* Printable names of reference object sub-types */

static const char           *AcpiGbl_RefClassNames[] =
{
    /* 00 */ "Local",
    /* 01 */ "Argument",
    /* 02 */ "RefOf",
    /* 03 */ "Index",
    /* 04 */ "DdbHandle",
    /* 05 */ "Named Object",
    /* 06 */ "Debug"
};

const char *
AcpiUtGetReferenceName (
    ACPI_OPERAND_OBJECT     *Object)
{

    if (!Object)
    {
        return ("NULL Object");
    }

    if (ACPI_GET_DESCRIPTOR_TYPE (Object) != ACPI_DESC_TYPE_OPERAND)
    {
        return ("Not an Operand object");
    }

    if (Object->Common.Type != ACPI_TYPE_LOCAL_REFERENCE)
    {
        return ("Not a Reference object");
    }

    if (Object->Reference.Class > ACPI_REFCLASS_MAX)
    {
        return ("Unknown Reference class");
    }

    return (AcpiGbl_RefClassNames[Object->Reference.Class]);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetMutexName
 *
 * PARAMETERS:  MutexId         - The predefined ID for this mutex.
 *
 * RETURN:      Decoded name of the internal mutex
 *
 * DESCRIPTION: Translate a mutex ID into a name string (Debug only)
 *
 ******************************************************************************/

/* Names for internal mutex objects, used for debug output */

static const char           *AcpiGbl_MutexNames[ACPI_NUM_MUTEX] =
{
    "ACPI_MTX_Interpreter",
    "ACPI_MTX_Namespace",
    "ACPI_MTX_Tables",
    "ACPI_MTX_Events",
    "ACPI_MTX_Caches",
    "ACPI_MTX_Memory",
};

const char *
AcpiUtGetMutexName (
    UINT32                  MutexId)
{

    if (MutexId > ACPI_MAX_MUTEX)
    {
        return ("Invalid Mutex ID");
    }

    return (AcpiGbl_MutexNames[MutexId]);
}


#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DEBUGGER)

/*
 * Strings and procedures used for debug only
 */

/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetNotifyName
 *
 * PARAMETERS:  NotifyValue     - Value from the Notify() request
 *
 * RETURN:      Decoded name for the notify value
 *
 * DESCRIPTION: Translate a Notify Value to a notify namestring.
 *
 ******************************************************************************/

/* Names for Notify() values, used for debug output */

static const char           *AcpiGbl_GenericNotify[ACPI_GENERIC_NOTIFY_MAX + 1] =
{
    /* 00 */ "Bus Check",
    /* 01 */ "Device Check",
    /* 02 */ "Device Wake",
    /* 03 */ "Eject Request",
    /* 04 */ "Device Check Light",
    /* 05 */ "Frequency Mismatch",
    /* 06 */ "Bus Mode Mismatch",
    /* 07 */ "Power Fault",
    /* 08 */ "Capabilities Check",
    /* 09 */ "Device PLD Check",
    /* 0A */ "Reserved",
    /* 0B */ "System Locality Update",
    /* 0C */ "Reserved (was previously Shutdown Request)",  /* Reserved in ACPI 6.0 */
    /* 0D */ "System Resource Affinity Update",
    /* 0E */ "Heterogeneous Memory Attributes Update",      /* ACPI 6.2 */
    /* 0F */ "Error Disconnect Recover"                     /* ACPI 6.3 */
};

static const char           *AcpiGbl_DeviceNotify[5] =
{
    /* 80 */ "Status Change",
    /* 81 */ "Information Change",
    /* 82 */ "Device-Specific Change",
    /* 83 */ "Device-Specific Change",
    /* 84 */ "Reserved"
};

static const char           *AcpiGbl_ProcessorNotify[5] =
{
    /* 80 */ "Performance Capability Change",
    /* 81 */ "C-State Change",
    /* 82 */ "Throttling Capability Change",
    /* 83 */ "Guaranteed Change",
    /* 84 */ "Minimum Excursion"
};

static const char           *AcpiGbl_ThermalNotify[5] =
{
    /* 80 */ "Thermal Status Change",
    /* 81 */ "Thermal Trip Point Change",
    /* 82 */ "Thermal Device List Change",
    /* 83 */ "Thermal Relationship Change",
    /* 84 */ "Reserved"
};


const char *
AcpiUtGetNotifyName (
    UINT32                  NotifyValue,
    ACPI_OBJECT_TYPE        Type)
{

    /* 00 - 0F are "common to all object types" (from ACPI Spec) */

    if (NotifyValue <= ACPI_GENERIC_NOTIFY_MAX)
    {
        return (AcpiGbl_GenericNotify[NotifyValue]);
    }

    /* 10 - 7F are reserved */

    if (NotifyValue <= ACPI_MAX_SYS_NOTIFY)
    {
        return ("Reserved");
    }

    /* 80 - 84 are per-object-type */

    if (NotifyValue <= ACPI_SPECIFIC_NOTIFY_MAX)
    {
        switch (Type)
        {
        case ACPI_TYPE_ANY:
        case ACPI_TYPE_DEVICE:
            return (AcpiGbl_DeviceNotify [NotifyValue - 0x80]);

        case ACPI_TYPE_PROCESSOR:
            return (AcpiGbl_ProcessorNotify [NotifyValue - 0x80]);

        case ACPI_TYPE_THERMAL:
            return (AcpiGbl_ThermalNotify [NotifyValue - 0x80]);

        default:
            return ("Target object type does not support notifies");
        }
    }

    /* 84 - BF are device-specific */

    if (NotifyValue <= ACPI_MAX_DEVICE_SPECIFIC_NOTIFY)
    {
        return ("Device-Specific");
    }

    /* C0 and above are hardware-specific */

    return ("Hardware-Specific");
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtGetArgumentTypeName
 *
 * PARAMETERS:  ArgType             - an ARGP_* parser argument type
 *
 * RETURN:      Decoded ARGP_* type
 *
 * DESCRIPTION: Decode an ARGP_* parser type, as defined in the amlcode.h file,
 *              and used in the acopcode.h file. For example, ARGP_TERMARG.
 *              Used for debug only.
 *
 ******************************************************************************/

static const char           *AcpiGbl_ArgumentType[20] =
{
    /* 00 */ "Unknown ARGP",
    /* 01 */ "ByteData",
    /* 02 */ "ByteList",
    /* 03 */ "CharList",
    /* 04 */ "DataObject",
    /* 05 */ "DataObjectList",
    /* 06 */ "DWordData",
    /* 07 */ "FieldList",
    /* 08 */ "Name",
    /* 09 */ "NameString",
    /* 0A */ "ObjectList",
    /* 0B */ "PackageLength",
    /* 0C */ "SuperName",
    /* 0D */ "Target",
    /* 0E */ "TermArg",
    /* 0F */ "TermList",
    /* 10 */ "WordData",
    /* 11 */ "QWordData",
    /* 12 */ "SimpleName",
    /* 13 */ "NameOrRef"
};

const char *
AcpiUtGetArgumentTypeName (
    UINT32                  ArgType)
{

    if (ArgType > ARGP_MAX)
    {
        return ("Unknown ARGP");
    }

    return (AcpiGbl_ArgumentType[ArgType]);
}

#endif


/*******************************************************************************
 *
 * FUNCTION:    AcpiUtValidObjectType
 *
 * PARAMETERS:  Type            - Object type to be validated
 *
 * RETURN:      TRUE if valid object type, FALSE otherwise
 *
 * DESCRIPTION: Validate an object type
 *
 ******************************************************************************/

BOOLEAN
AcpiUtValidObjectType (
    ACPI_OBJECT_TYPE        Type)
{

    if (Type > ACPI_TYPE_LOCAL_MAX)
    {
        /* Note: Assumes all TYPEs are contiguous (external/local) */

        return (FALSE);
    }

    return (TRUE);
}
