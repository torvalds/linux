/******************************************************************************
 *
 * Module Name: aslmapoutput - Output/emit the resource descriptor/device maps
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
#include <contrib/dev/acpica/include/acapps.h>
#include <contrib/dev/acpica/compiler/aslcompiler.h>
#include "aslcompiler.y.h"
#include <contrib/dev/acpica/include/acinterp.h>
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/amlcode.h>

/* This module used for application-level code only */

#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslmapoutput")

/* Local prototypes */

static void
MpEmitGpioInfo (
    void);

static void
MpEmitSerialInfo (
    void);

static void
MpEmitDeviceTree (
    void);

static ACPI_STATUS
MpEmitOneDevice (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue);

static void
MpXrefDevices (
    ACPI_GPIO_INFO          *Info);

static ACPI_STATUS
MpNamespaceXrefBegin (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context);


/* Strings used to decode flag bits */

const char                  *DirectionDecode[] =
{
    "Both I/O   ",
    "InputOnly  ",
    "OutputOnly ",
    "Preserve   "
};

const char                  *PolarityDecode[] =
{
    "ActiveHigh",
    "ActiveLow ",
    "ActiveBoth",
    "Reserved  "
};


/*******************************************************************************
 *
 * FUNCTION:    MpEmitMappingInfo
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: External interface.
 *              Map file has already been opened. Emit all of the collected
 *              hardware mapping information. Includes: GPIO information,
 *              Serial information, and a dump of the entire ACPI device tree.
 *
 ******************************************************************************/

void
MpEmitMappingInfo (
    void)
{

    /* Mapfile option enabled? */

    if (!AslGbl_MapfileFlag)
    {
        return;
    }

    if (!AslGbl_GpioList)
    {
        FlPrintFile (ASL_FILE_MAP_OUTPUT,
            "\nNo GPIO devices found\n");
    }

    if (!AslGbl_SerialList)
    {
        FlPrintFile (ASL_FILE_MAP_OUTPUT,
            "\nNo Serial devices found (I2C/SPI/UART)\n");
    }

    if (!AslGbl_GpioList && !AslGbl_SerialList)
    {
        return;
    }

    /* Headers */

    FlPrintFile (ASL_FILE_MAP_OUTPUT, "\nResource Descriptor Connectivity Map\n");
    FlPrintFile (ASL_FILE_MAP_OUTPUT,   "------------------------------------\n");

    /* Emit GPIO and Serial descriptors, then entire ACPI device tree */

    MpEmitGpioInfo ();
    MpEmitSerialInfo ();
    MpEmitDeviceTree ();

    /* Clear the lists - no need to free memory here */

    AslGbl_SerialList = NULL;
    AslGbl_GpioList = NULL;
}


/*******************************************************************************
 *
 * FUNCTION:    MpEmitGpioInfo
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Emit the info about all GPIO devices found during the
 *              compile or disassembly.
 *
 ******************************************************************************/

static void
MpEmitGpioInfo (
    void)
{
    ACPI_GPIO_INFO          *Info;
    char                    *Type;
    char                    *PrevDeviceName = NULL;
    const char              *Direction;
    const char              *Polarity;
    char                    *ParentPathname;
    const char              *Description;
    char                    *HidString;
    const AH_DEVICE_ID      *HidInfo;


    /* Walk the GPIO descriptor list */

    Info = AslGbl_GpioList;
    while (Info)
    {
        HidString = MpGetHidViaNamestring (Info->DeviceName);

        /* Print header info for the controller itself */

        if (!PrevDeviceName ||
            strcmp (PrevDeviceName, Info->DeviceName))
        {
            FlPrintFile (ASL_FILE_MAP_OUTPUT,
                "\n\nGPIO Controller:  %-8s  %-28s",
                HidString, Info->DeviceName);

            HidInfo = AcpiAhMatchHardwareId (HidString);
            if (HidInfo)
            {
                FlPrintFile (ASL_FILE_MAP_OUTPUT, "  // %s",
                    HidInfo->Description);
            }

            FlPrintFile (ASL_FILE_MAP_OUTPUT,
                "\n\nPin   Type     Direction    Polarity"
                "    Dest _HID  Destination\n");
        }

        PrevDeviceName = Info->DeviceName;

        /* Setup various strings based upon the type (GpioInt or GpioIo) */

        switch (Info->Type)
        {
        case AML_RESOURCE_GPIO_TYPE_INT:

            Type = "GpioInt";
            Direction = "-Interrupt-";
            Polarity = PolarityDecode[Info->Polarity];
            break;

        case AML_RESOURCE_GPIO_TYPE_IO:

            Type = "GpioIo ";
            Direction = DirectionDecode[Info->Direction];
            Polarity = "          ";
            break;

        default:
            continue;
        }

        /* Emit the GPIO info */

        FlPrintFile (ASL_FILE_MAP_OUTPUT, "%4.4X  %s  %s  %s  ",
            Info->PinNumber, Type, Direction, Polarity);

        ParentPathname = NULL;
        HidString = MpGetConnectionInfo (Info->Op, Info->PinIndex,
            &Info->TargetNode, &ParentPathname);
        if (HidString)
        {
            /*
             * This is a Connection() field
             * Attempt to find all references to the field.
             */
            FlPrintFile (ASL_FILE_MAP_OUTPUT, "%8s   %-28s",
                HidString, ParentPathname);

            MpXrefDevices (Info);
        }
        else
        {
            /*
             * For Devices, attempt to get the _HID description string.
             * Failing that (many _HIDs are not recognized), attempt to
             * get the _DDN description string.
             */
            HidString = MpGetParentDeviceHid (Info->Op, &Info->TargetNode,
                &ParentPathname);

            FlPrintFile (ASL_FILE_MAP_OUTPUT, "%8s   %-28s",
                HidString, ParentPathname);

            /* Get the _HID description or _DDN string */

            HidInfo = AcpiAhMatchHardwareId (HidString);
            if (HidInfo)
            {
                FlPrintFile (ASL_FILE_MAP_OUTPUT, "  // %s",
                    HidInfo->Description);
            }
            else if ((Description = MpGetDdnValue (ParentPathname)))
            {
                FlPrintFile (ASL_FILE_MAP_OUTPUT, "  // %s (_DDN)",
                    Description);
            }
        }

        FlPrintFile (ASL_FILE_MAP_OUTPUT, "\n");
        ACPI_FREE (ParentPathname);
        Info = Info->Next;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    MpEmitSerialInfo
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Emit the info about all Serial devices found during the
 *              compile or disassembly.
 *
 ******************************************************************************/

static void
MpEmitSerialInfo (
    void)
{
    ACPI_SERIAL_INFO        *Info;
    char                    *Type;
    char                    *ParentPathname;
    char                    *PrevDeviceName = NULL;
    char                    *HidString;
    const AH_DEVICE_ID      *HidInfo;
    const char              *Description;
    AML_RESOURCE            *Resource;


    /* Walk the constructed serial descriptor list */

    Info = AslGbl_SerialList;
    while (Info)
    {
        Resource = Info->Resource;
        switch (Resource->CommonSerialBus.Type)
        {
        case AML_RESOURCE_I2C_SERIALBUSTYPE:
            Type = "I2C ";
            break;

        case AML_RESOURCE_SPI_SERIALBUSTYPE:
            Type = "SPI ";
            break;

        case AML_RESOURCE_UART_SERIALBUSTYPE:
            Type = "UART";
            break;

        default:
            Type = "UNKN";
            break;
        }

        HidString = MpGetHidViaNamestring (Info->DeviceName);

        /* Print header info for the controller itself */

        if (!PrevDeviceName ||
            strcmp (PrevDeviceName, Info->DeviceName))
        {
            FlPrintFile (ASL_FILE_MAP_OUTPUT, "\n\n%s Controller:  ",
                Type);
            FlPrintFile (ASL_FILE_MAP_OUTPUT, "%-8s  %-28s",
                HidString, Info->DeviceName);

            HidInfo = AcpiAhMatchHardwareId (HidString);
            if (HidInfo)
            {
                FlPrintFile (ASL_FILE_MAP_OUTPUT, "  // %s",
                    HidInfo->Description);
            }

            FlPrintFile (ASL_FILE_MAP_OUTPUT, "\n\n");
            FlPrintFile (ASL_FILE_MAP_OUTPUT,
                "Type  Address   Speed      Dest _HID  Destination\n");
        }

        PrevDeviceName = Info->DeviceName;

        FlPrintFile (ASL_FILE_MAP_OUTPUT, "%s   %4.4X    %8.8X    ",
            Type, Info->Address, Info->Speed);

        ParentPathname = NULL;
        HidString = MpGetConnectionInfo (Info->Op, 0, &Info->TargetNode,
            &ParentPathname);
        if (HidString)
        {
            /*
             * This is a Connection() field
             * Attempt to find all references to the field.
             */
            FlPrintFile (ASL_FILE_MAP_OUTPUT, "%8s   %-28s",
                HidString, ParentPathname);
        }
        else
        {
            /* Normal resource template */

            HidString = MpGetParentDeviceHid (Info->Op, &Info->TargetNode,
                &ParentPathname);
            FlPrintFile (ASL_FILE_MAP_OUTPUT, "%8s   %-28s",
                HidString, ParentPathname);

            /* Get the _HID description or _DDN string */

            HidInfo = AcpiAhMatchHardwareId (HidString);
            if (HidInfo)
            {
                FlPrintFile (ASL_FILE_MAP_OUTPUT, "  // %s",
                    HidInfo->Description);
            }
            else if ((Description = MpGetDdnValue (ParentPathname)))
            {
                FlPrintFile (ASL_FILE_MAP_OUTPUT, "  // %s (_DDN)",
                    Description);
            }
        }

        FlPrintFile (ASL_FILE_MAP_OUTPUT, "\n");
        ACPI_FREE (ParentPathname);
        Info = Info->Next;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    MpEmitDeviceTree
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Emit information about all devices within the ACPI namespace.
 *
 ******************************************************************************/

static void
MpEmitDeviceTree (
    void)
{

    FlPrintFile (ASL_FILE_MAP_OUTPUT, "\n\nACPI Device Tree\n");
    FlPrintFile (ASL_FILE_MAP_OUTPUT,     "----------------\n\n");

    FlPrintFile (ASL_FILE_MAP_OUTPUT, "Device Pathname                   "
        "_HID      Description\n\n");

    /* Walk the namespace from the root */

    (void) AcpiNsWalkNamespace (ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
        ACPI_UINT32_MAX, FALSE, MpEmitOneDevice, NULL, NULL, NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    MpEmitOneDevice
 *
 * PARAMETERS:  ACPI_NAMESPACE_WALK callback
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Emit information about one ACPI device in the namespace. Used
 *              during dump of all device objects within the namespace.
 *
 ******************************************************************************/

static ACPI_STATUS
MpEmitOneDevice (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue)
{
    char                    *DevicePathname;
    char                    *DdnString;
    char                    *HidString;
    const AH_DEVICE_ID      *HidInfo;


    /* Device pathname */

    DevicePathname = AcpiNsGetExternalPathname (
        ACPI_CAST_PTR (ACPI_NAMESPACE_NODE, ObjHandle));

    FlPrintFile (ASL_FILE_MAP_OUTPUT, "%-32s", DevicePathname);

    /* _HID or _DDN */

    HidString = MpGetHidValue (
        ACPI_CAST_PTR (ACPI_NAMESPACE_NODE, ObjHandle));
    FlPrintFile (ASL_FILE_MAP_OUTPUT, "%8s", HidString);

    HidInfo = AcpiAhMatchHardwareId (HidString);
    if (HidInfo)
    {
        FlPrintFile (ASL_FILE_MAP_OUTPUT, "    // %s",
            HidInfo->Description);
    }
    else if ((DdnString = MpGetDdnValue (DevicePathname)))
    {
        FlPrintFile (ASL_FILE_MAP_OUTPUT, "    // %s (_DDN)", DdnString);
    }

    FlPrintFile (ASL_FILE_MAP_OUTPUT, "\n");
    ACPI_FREE (DevicePathname);
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    MpXrefDevices
 *
 * PARAMETERS:  Info                    - A GPIO Info block
 *
 * RETURN:      None
 *
 * DESCRIPTION: Cross-reference the parse tree and find all references to the
 *              specified GPIO device.
 *
 ******************************************************************************/

static void
MpXrefDevices (
    ACPI_GPIO_INFO          *Info)
{

    /* Walk the entire parse tree */

    TrWalkParseTree (AslGbl_ParseTreeRoot, ASL_WALK_VISIT_DOWNWARD,
        MpNamespaceXrefBegin, NULL, Info);

    if (!Info->References)
    {
        FlPrintFile (ASL_FILE_MAP_OUTPUT, "  // **** No references in table");
    }
}


/*******************************************************************************
 *
 * FUNCTION:    MpNamespaceXrefBegin
 *
 * PARAMETERS:  WALK_PARSE_TREE callback
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk parse tree callback used to cross-reference GPIO pins.
 *
 ******************************************************************************/

static ACPI_STATUS
MpNamespaceXrefBegin (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{
    ACPI_GPIO_INFO          *Info = ACPI_CAST_PTR (ACPI_GPIO_INFO, Context);
    const ACPI_OPCODE_INFO  *OpInfo;
    char                    *DevicePathname;
    ACPI_PARSE_OBJECT       *ParentOp;
    char                    *HidString;


    ACPI_FUNCTION_TRACE_PTR (MpNamespaceXrefBegin, Op);

    /*
     * If this node is the actual declaration of a name
     * [such as the XXXX name in "Method (XXXX)"],
     * we are not interested in it here. We only care about names that
     * are references to other objects within the namespace and the
     * parent objects of name declarations
     */
    if (Op->Asl.CompileFlags & OP_IS_NAME_DECLARATION)
    {
        return (AE_OK);
    }

    /* We are only interested in opcodes that have an associated name */

    OpInfo = AcpiPsGetOpcodeInfo (Op->Asl.AmlOpcode);

    if ((OpInfo->Flags & AML_NAMED) ||
        (OpInfo->Flags & AML_CREATE))
    {
        return (AE_OK);
    }

    if ((Op->Asl.ParseOpcode != PARSEOP_NAMESTRING) &&
        (Op->Asl.ParseOpcode != PARSEOP_NAMESEG)    &&
        (Op->Asl.ParseOpcode != PARSEOP_METHODCALL))
    {
        return (AE_OK);
    }

    if (!Op->Asl.Node)
    {
        return (AE_OK);
    }

    ParentOp = Op->Asl.Parent;
    if (ParentOp->Asl.ParseOpcode == PARSEOP_FIELD)
    {
        return (AE_OK);
    }

    if (Op->Asl.Node == Info->TargetNode)
    {
        while (ParentOp && (!ParentOp->Asl.Node))
        {
            ParentOp = ParentOp->Asl.Parent;
        }

        if (ParentOp)
        {
            DevicePathname = AcpiNsGetExternalPathname (
                ParentOp->Asl.Node);

            if (!Info->References)
            {
                FlPrintFile (ASL_FILE_MAP_OUTPUT, "  // References:");
            }

            HidString = MpGetHidViaNamestring (DevicePathname);

            FlPrintFile (ASL_FILE_MAP_OUTPUT, " %s [%s]",
                DevicePathname, HidString);

            Info->References++;

            ACPI_FREE (DevicePathname);
        }
    }

    return (AE_OK);
}
