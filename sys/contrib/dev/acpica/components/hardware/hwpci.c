/*******************************************************************************
 *
 * Module Name: hwpci - Obtain PCI bus, device, and function numbers
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


#define _COMPONENT          ACPI_NAMESPACE
        ACPI_MODULE_NAME    ("hwpci")


/* PCI configuration space values */

#define PCI_CFG_HEADER_TYPE_REG             0x0E
#define PCI_CFG_PRIMARY_BUS_NUMBER_REG      0x18
#define PCI_CFG_SECONDARY_BUS_NUMBER_REG    0x19

/* PCI header values */

#define PCI_HEADER_TYPE_MASK                0x7F
#define PCI_TYPE_BRIDGE                     0x01
#define PCI_TYPE_CARDBUS_BRIDGE             0x02

typedef struct acpi_pci_device
{
    ACPI_HANDLE             Device;
    struct acpi_pci_device  *Next;

} ACPI_PCI_DEVICE;


/* Local prototypes */

static ACPI_STATUS
AcpiHwBuildPciList (
    ACPI_HANDLE             RootPciDevice,
    ACPI_HANDLE             PciRegion,
    ACPI_PCI_DEVICE         **ReturnListHead);

static ACPI_STATUS
AcpiHwProcessPciList (
    ACPI_PCI_ID             *PciId,
    ACPI_PCI_DEVICE         *ListHead);

static void
AcpiHwDeletePciList (
    ACPI_PCI_DEVICE         *ListHead);

static ACPI_STATUS
AcpiHwGetPciDeviceInfo (
    ACPI_PCI_ID             *PciId,
    ACPI_HANDLE             PciDevice,
    UINT16                  *BusNumber,
    BOOLEAN                 *IsBridge);


/*******************************************************************************
 *
 * FUNCTION:    AcpiHwDerivePciId
 *
 * PARAMETERS:  PciId               - Initial values for the PCI ID. May be
 *                                    modified by this function.
 *              RootPciDevice       - A handle to a PCI device object. This
 *                                    object must be a PCI Root Bridge having a
 *                                    _HID value of either PNP0A03 or PNP0A08
 *              PciRegion           - A handle to a PCI configuration space
 *                                    Operation Region being initialized
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function derives a full PCI ID for a PCI device,
 *              consisting of a Segment number, Bus number, Device number,
 *              and function code.
 *
 *              The PCI hardware dynamically configures PCI bus numbers
 *              depending on the bus topology discovered during system
 *              initialization. This function is invoked during configuration
 *              of a PCI_Config Operation Region in order to (possibly) update
 *              the Bus/Device/Function numbers in the PciId with the actual
 *              values as determined by the hardware and operating system
 *              configuration.
 *
 *              The PciId parameter is initially populated during the Operation
 *              Region initialization. This function is then called, and is
 *              will make any necessary modifications to the Bus, Device, or
 *              Function number PCI ID subfields as appropriate for the
 *              current hardware and OS configuration.
 *
 * NOTE:        Created 08/2010. Replaces the previous OSL AcpiOsDerivePciId
 *              interface since this feature is OS-independent. This module
 *              specifically avoids any use of recursion by building a local
 *              temporary device list.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiHwDerivePciId (
    ACPI_PCI_ID             *PciId,
    ACPI_HANDLE             RootPciDevice,
    ACPI_HANDLE             PciRegion)
{
    ACPI_STATUS             Status;
    ACPI_PCI_DEVICE         *ListHead;


    ACPI_FUNCTION_TRACE (HwDerivePciId);


    if (!PciId)
    {
        return_ACPI_STATUS (AE_BAD_PARAMETER);
    }

    /* Build a list of PCI devices, from PciRegion up to RootPciDevice */

    Status = AcpiHwBuildPciList (RootPciDevice, PciRegion, &ListHead);
    if (ACPI_SUCCESS (Status))
    {
        /* Walk the list, updating the PCI device/function/bus numbers */

        Status = AcpiHwProcessPciList (PciId, ListHead);

        /* Delete the list */

        AcpiHwDeletePciList (ListHead);
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiHwBuildPciList
 *
 * PARAMETERS:  RootPciDevice       - A handle to a PCI device object. This
 *                                    object is guaranteed to be a PCI Root
 *                                    Bridge having a _HID value of either
 *                                    PNP0A03 or PNP0A08
 *              PciRegion           - A handle to the PCI configuration space
 *                                    Operation Region
 *              ReturnListHead      - Where the PCI device list is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Builds a list of devices from the input PCI region up to the
 *              Root PCI device for this namespace subtree.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiHwBuildPciList (
    ACPI_HANDLE             RootPciDevice,
    ACPI_HANDLE             PciRegion,
    ACPI_PCI_DEVICE         **ReturnListHead)
{
    ACPI_HANDLE             CurrentDevice;
    ACPI_HANDLE             ParentDevice;
    ACPI_STATUS             Status;
    ACPI_PCI_DEVICE         *ListElement;


    /*
     * Ascend namespace branch until the RootPciDevice is reached, building
     * a list of device nodes. Loop will exit when either the PCI device is
     * found, or the root of the namespace is reached.
     */
    *ReturnListHead = NULL;
    CurrentDevice = PciRegion;
    while (1)
    {
        Status = AcpiGetParent (CurrentDevice, &ParentDevice);
        if (ACPI_FAILURE (Status))
        {
            /* Must delete the list before exit */

            AcpiHwDeletePciList (*ReturnListHead);
            return (Status);
        }

        /* Finished when we reach the PCI root device (PNP0A03 or PNP0A08) */

        if (ParentDevice == RootPciDevice)
        {
            return (AE_OK);
        }

        ListElement = ACPI_ALLOCATE (sizeof (ACPI_PCI_DEVICE));
        if (!ListElement)
        {
            /* Must delete the list before exit */

            AcpiHwDeletePciList (*ReturnListHead);
            return (AE_NO_MEMORY);
        }

        /* Put new element at the head of the list */

        ListElement->Next = *ReturnListHead;
        ListElement->Device = ParentDevice;
        *ReturnListHead = ListElement;

        CurrentDevice = ParentDevice;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiHwProcessPciList
 *
 * PARAMETERS:  PciId               - Initial values for the PCI ID. May be
 *                                    modified by this function.
 *              ListHead            - Device list created by
 *                                    AcpiHwBuildPciList
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk downward through the PCI device list, getting the device
 *              info for each, via the PCI configuration space and updating
 *              the PCI ID as necessary. Deletes the list during traversal.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiHwProcessPciList (
    ACPI_PCI_ID             *PciId,
    ACPI_PCI_DEVICE         *ListHead)
{
    ACPI_STATUS             Status = AE_OK;
    ACPI_PCI_DEVICE         *Info;
    UINT16                  BusNumber;
    BOOLEAN                 IsBridge = TRUE;


    ACPI_FUNCTION_NAME (HwProcessPciList);


    ACPI_DEBUG_PRINT ((ACPI_DB_OPREGION,
        "Input PciId:  Seg %4.4X Bus %4.4X Dev %4.4X Func %4.4X\n",
        PciId->Segment, PciId->Bus, PciId->Device, PciId->Function));

    BusNumber = PciId->Bus;

    /*
     * Descend down the namespace tree, collecting PCI device, function,
     * and bus numbers. BusNumber is only important for PCI bridges.
     * Algorithm: As we descend the tree, use the last valid PCI device,
     * function, and bus numbers that are discovered, and assign them
     * to the PCI ID for the target device.
     */
    Info = ListHead;
    while (Info)
    {
        Status = AcpiHwGetPciDeviceInfo (PciId, Info->Device,
            &BusNumber, &IsBridge);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        Info = Info->Next;
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_OPREGION,
        "Output PciId: Seg %4.4X Bus %4.4X Dev %4.4X Func %4.4X "
        "Status %X BusNumber %X IsBridge %X\n",
        PciId->Segment, PciId->Bus, PciId->Device, PciId->Function,
        Status, BusNumber, IsBridge));

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiHwDeletePciList
 *
 * PARAMETERS:  ListHead            - Device list created by
 *                                    AcpiHwBuildPciList
 *
 * RETURN:      None
 *
 * DESCRIPTION: Free the entire PCI list.
 *
 ******************************************************************************/

static void
AcpiHwDeletePciList (
    ACPI_PCI_DEVICE         *ListHead)
{
    ACPI_PCI_DEVICE         *Next;
    ACPI_PCI_DEVICE         *Previous;


    Next = ListHead;
    while (Next)
    {
        Previous = Next;
        Next = Previous->Next;
        ACPI_FREE (Previous);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiHwGetPciDeviceInfo
 *
 * PARAMETERS:  PciId               - Initial values for the PCI ID. May be
 *                                    modified by this function.
 *              PciDevice           - Handle for the PCI device object
 *              BusNumber           - Where a PCI bridge bus number is returned
 *              IsBridge            - Return value, indicates if this PCI
 *                                    device is a PCI bridge
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get the device info for a single PCI device object. Get the
 *              _ADR (contains PCI device and function numbers), and for PCI
 *              bridge devices, get the bus number from PCI configuration
 *              space.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiHwGetPciDeviceInfo (
    ACPI_PCI_ID             *PciId,
    ACPI_HANDLE             PciDevice,
    UINT16                  *BusNumber,
    BOOLEAN                 *IsBridge)
{
    ACPI_STATUS             Status;
    ACPI_OBJECT_TYPE        ObjectType;
    UINT64                  ReturnValue;
    UINT64                  PciValue;


    /* We only care about objects of type Device */

    Status = AcpiGetType (PciDevice, &ObjectType);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    if (ObjectType != ACPI_TYPE_DEVICE)
    {
        return (AE_OK);
    }

    /* We need an _ADR. Ignore device if not present */

    Status = AcpiUtEvaluateNumericObject (METHOD_NAME__ADR,
        PciDevice, &ReturnValue);
    if (ACPI_FAILURE (Status))
    {
        return (AE_OK);
    }

    /*
     * From _ADR, get the PCI Device and Function and
     * update the PCI ID.
     */
    PciId->Device = ACPI_HIWORD (ACPI_LODWORD (ReturnValue));
    PciId->Function = ACPI_LOWORD (ACPI_LODWORD (ReturnValue));

    /*
     * If the previous device was a bridge, use the previous
     * device bus number
     */
    if (*IsBridge)
    {
        PciId->Bus = *BusNumber;
    }

    /*
     * Get the bus numbers from PCI Config space:
     *
     * First, get the PCI HeaderType
     */
    *IsBridge = FALSE;
    Status = AcpiOsReadPciConfiguration (PciId,
        PCI_CFG_HEADER_TYPE_REG, &PciValue, 8);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    /* We only care about bridges (1=PciBridge, 2=CardBusBridge) */

    PciValue &= PCI_HEADER_TYPE_MASK;

    if ((PciValue != PCI_TYPE_BRIDGE) &&
        (PciValue != PCI_TYPE_CARDBUS_BRIDGE))
    {
        return (AE_OK);
    }

    /* Bridge: Get the Primary BusNumber */

    Status = AcpiOsReadPciConfiguration (PciId,
        PCI_CFG_PRIMARY_BUS_NUMBER_REG, &PciValue, 8);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    *IsBridge = TRUE;
    PciId->Bus = (UINT16) PciValue;

    /* Bridge: Get the Secondary BusNumber */

    Status = AcpiOsReadPciConfiguration (PciId,
        PCI_CFG_SECONDARY_BUS_NUMBER_REG, &PciValue, 8);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    *BusNumber = (UINT16) PciValue;
    return (AE_OK);
}
