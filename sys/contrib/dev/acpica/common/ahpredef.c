/******************************************************************************
 *
 * Module Name: ahpredef - Table of all known ACPI predefined names
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

#define _COMPONENT          ACPI_UTILITIES
        ACPI_MODULE_NAME    ("ahpredef")

/*
 * iASL only needs a partial table (short descriptions only).
 * AcpiHelp needs the full table.
 */
#ifdef ACPI_ASL_COMPILER
#define AH_PREDEF(Name, ShortDesc, LongDesc) {Name, ShortDesc}
#else
#define AH_PREDEF(Name, ShortDesc, LongDesc) {Name, ShortDesc, LongDesc}
#endif

/*
 * Predefined ACPI names, with short description and return value.
 * This table was extracted directly from the ACPI specification.
 */
const AH_PREDEFINED_NAME    AslPredefinedInfo[] =
{
    AH_PREDEF ("_ACx",    "Active Cooling, x=0-9", "Returns the active cooling policy threshold values"),
    AH_PREDEF ("_ADR",    "Address", "Returns address of a device on parent bus, and resource field"),
    AH_PREDEF ("_AEI",    "ACPI Event Interrupts", "Returns a list of GPIO events to be used as ACPI events"),
    AH_PREDEF ("_ALC",    "Ambient Light Chromaticity", "Returns the ambient light color chromaticity"),
    AH_PREDEF ("_ALI",    "Ambient Light Illuminance", "Returns the ambient light brightness"),
    AH_PREDEF ("_ALN",    "Alignment", "Base alignment, Resource Descriptor field"),
    AH_PREDEF ("_ALP",    "Ambient Light Polling", "Returns the ambient light sensor polling frequency"),
    AH_PREDEF ("_ALR",    "Ambient Light Response", "Returns the ambient light brightness to display brightness mappings"),
    AH_PREDEF ("_ALT",    "Ambient Light Temperature", "Returns the ambient light color temperature"),
    AH_PREDEF ("_ALx",    "Active List, x=0-9", "Returns a list of active cooling device objects"),
    AH_PREDEF ("_ART",    "Active Cooling Relationship Table", "Returns thermal relationship information between platform devices and fan devices"),
    AH_PREDEF ("_ASI",    "Address Space Id", "Resource Descriptor field"),
    AH_PREDEF ("_ASZ",    "Access Size", "Resource Descriptor field"),
    AH_PREDEF ("_ATT",    "Type-Specific Attribute", "Resource Descriptor field"),
    AH_PREDEF ("_BAS",    "Base Address", "Range base address, Resource Descriptor field"),
    AH_PREDEF ("_BBN",    "BIOS Bus Number", "Returns the PCI bus number returned by the BIOS"),
    AH_PREDEF ("_BCL",    "Brightness Control Levels", "Returns a list of supported brightness control levels"),
    AH_PREDEF ("_BCM",    "Brightness Control Method", "Sets the brightness level of the display device"),
    AH_PREDEF ("_BCT",    "Battery Charge Time", "Returns time remaining to complete charging battery"),
    AH_PREDEF ("_BDN",    "BIOS Dock Name", "Returns the Dock ID returned by the BIOS"),
    AH_PREDEF ("_BFS",    "Back From Sleep", "Inform AML of a wake event"),
    AH_PREDEF ("_BIF",    "Battery Information", "Returns a Control Method Battery information block"),
    AH_PREDEF ("_BIX",    "Battery Information Extended", "Returns a Control Method Battery extended information block"),
    AH_PREDEF ("_BLT",    "Battery Level Threshold", "Set battery level threshold preferences"),
    AH_PREDEF ("_BM_",    "Bus Master", "Resource Descriptor field"),
    AH_PREDEF ("_BMA",    "Battery Measurement Averaging Interval", "Sets battery measurement averaging interval"),
    AH_PREDEF ("_BMC",    "Battery Maintenance Control", "Sets battery maintenance and control features"),
    AH_PREDEF ("_BMD",    "Battery Maintenance Data", "Returns battery maintenance, control, and state data"),
    AH_PREDEF ("_BMS",    "Battery Measurement Sampling Time", "Sets the battery measurement sampling time"),
    AH_PREDEF ("_BQC",    "Brightness Query Current", "Returns the current display brightness level"),
    AH_PREDEF ("_BST",    "Battery Status", "Returns a Control Method Battery status block"),
    AH_PREDEF ("_BTH",    "Battery Throttle Limit", "Thermal limit for charging and discharging"),
    AH_PREDEF ("_BTM",    "Battery Time", "Returns the battery runtime"),
    AH_PREDEF ("_BTP",    "Battery Trip Point", "Sets a Control Method Battery trip point"),
    AH_PREDEF ("_CBA",    "Configuration Base Address", "Sets the base address for a PCI Express host bridge"),
    AH_PREDEF ("_CCA",    "Cache Coherency Attribute", "Returns a device's support level for cache coherency"),
    AH_PREDEF ("_CDM",    "Clock Domain", "Returns a logical processor's clock domain identifier"),
    AH_PREDEF ("_CID",    "Compatible ID", "Returns a device's Plug and Play Compatible ID list"),
    AH_PREDEF ("_CLS",    "Class Code", "Returns PCI class code and subclass"),
    AH_PREDEF ("_CPC",    "Continuous Performance Control", "Returns a list of performance control interfaces"),
    AH_PREDEF ("_CR3",    "Warm/Standby Temperature", "Temperature for a fast low power state"),
    AH_PREDEF ("_CRS",    "Current Resource Settings", "Returns the current resource settings for a device"),
    AH_PREDEF ("_CRT",    "Critical Temperature", "Returns the shutdown critical temperature"),
    AH_PREDEF ("_CSD",    "C-State Dependencies", "Returns a list of C-state dependencies"),
    AH_PREDEF ("_CST",    "C-States", "Returns a list of supported C-states"),
    AH_PREDEF ("_CWS",    "Clear Wake Alarm Status", "Clear the status of wake alarms"),
    AH_PREDEF ("_DBT",    "Debounce Timeout", "Timeout value, Resource Descriptor field"),
    AH_PREDEF ("_DCK",    "Dock Present", "Sets docking isolation. Presence indicates device is a docking station"),
    AH_PREDEF ("_DCS",    "Display Current Status", "Returns status of the display output device"),
    AH_PREDEF ("_DDC",    "Display Data Current", "Returns the EDID for the display output device"),
    AH_PREDEF ("_DDN",    "DOS Device Name", "Returns a device logical name"),
    AH_PREDEF ("_DEC",    "Decode", "Device decoding type, Resource Descriptor field"),
    AH_PREDEF ("_DEP",    "Dependencies", "Returns a list of operation region dependencies"),
    AH_PREDEF ("_DGS",    "Display Graphics State", "Return the current state of the output device"),
    AH_PREDEF ("_DIS",    "Disable Device", "Disables a device"),
    AH_PREDEF ("_DLM",    "Device Lock Mutex", "Defines mutex for OS/AML sharing"),
    AH_PREDEF ("_DMA",    "Direct Memory Access", "Returns device current resources for DMA transactions, and resource field"),
    AH_PREDEF ("_DOD",    "Display Output Devices", "Enumerate all devices attached to the display adapter"),
    AH_PREDEF ("_DOS",    "Disable Output Switching", "Sets the display output switching mode"),
    AH_PREDEF ("_DPL",    "Device Selection Polarity", "Polarity of Device Selection signal, Resource Descriptor field"),
    AH_PREDEF ("_DRS",    "Drive Strength", "Drive Strength setting for GPIO connection, Resource Descriptor field"),
    AH_PREDEF ("_DSD",    "Device-Specific Data", "Returns a list of device property information"),
    AH_PREDEF ("_DSM",    "Device-Specific Method", "Executes device-specific functions"),
    AH_PREDEF ("_DSS",    "Device Set State", "Sets the display device state"),
    AH_PREDEF ("_DSW",    "Device Sleep Wake", "Sets the sleep and wake transition states for a device"),
    AH_PREDEF ("_DTI",    "Device Temperature Indication", "Conveys native device temperature to the platform"),
    AH_PREDEF ("_Exx",    "Edge-Triggered GPE, xx=0x00-0xFF", "Method executed as a result of a general-purpose event"),
    AH_PREDEF ("_EC_",    "Embedded Controller", "returns EC offset and query information"),
    AH_PREDEF ("_EDL",    "Eject Device List", "Returns a list of devices that are dependent on a device (docking)"),
    AH_PREDEF ("_EJD",    "Ejection Dependent Device", "Returns the name of dependent (parent) device (docking)"),
    AH_PREDEF ("_EJx",    "Eject Device, x=0-9", "Begin or cancel a device ejection request (docking)"),
    AH_PREDEF ("_END",    "Endianness", "Endian orientation, Resource Descriptor field"),
    AH_PREDEF ("_EVT",    "Event", "Event method for GPIO events"),
    AH_PREDEF ("_FDE",    "Floppy Disk Enumerate", "Returns floppy disk configuration information"),
    AH_PREDEF ("_FDI",    "Floppy Drive Information", "Returns a floppy drive information block"),
    AH_PREDEF ("_FDM",    "Floppy Drive Mode", "Sets a floppy drive speed"),
    AH_PREDEF ("_FIF",    "Fan Information", "Returns fan device information"),
    AH_PREDEF ("_FIT",    "Firmware Interface Table", "Returns a list of NFIT structures"),
    AH_PREDEF ("_FIX",    "Fixed Register Resource Provider", "Returns a list of devices that implement FADT register blocks"),
    AH_PREDEF ("_FLC",    "Flow Control", "Flow control, Resource Descriptor field"),
    AH_PREDEF ("_FPS",    "Fan Performance States", "Returns a list of supported fan performance states"),
    AH_PREDEF ("_FSL",    "Fan Set Level", "Control method that sets the fan device's speed level (performance state)"),
    AH_PREDEF ("_FST",    "Fan Status", "Returns current status information for a fan device"),
    AH_PREDEF ("_FUN",    "Function Number", "Resource descriptor field"),
    AH_PREDEF ("_GAI",    "Get Averaging Interval", "Returns the power meter averaging interval"),
    AH_PREDEF ("_GCP",    "Get Capabilities", "Get device time capabilities"),
    AH_PREDEF ("_GHL",    "Get Hardware Limit", "Returns the hardware limit enforced by the power meter"),
    AH_PREDEF ("_GL_",    "Global Lock", "OS-defined Global Lock mutex object"),
    AH_PREDEF ("_GLK",    "Get Global Lock Requirement", "Returns a device's Global Lock requirement for device access"),
    AH_PREDEF ("_GPD",    "Get Post Data", "Returns the value of the VGA device that will be posted at boot"),
    AH_PREDEF ("_GPE",    "General Purpose Events", "Predefined scope (\\_GPE) or SCI number for EC"),
    AH_PREDEF ("_GRA",    "Granularity", "Address space granularity, Resource Descriptor field"),
    AH_PREDEF ("_GRT",    "Get Real Time", "Returns current time-of-day from a time/alarm device"),
    AH_PREDEF ("_GSB",    "Global System Interrupt Base", "Returns the GSB for a I/O APIC device"),
    AH_PREDEF ("_GTF",    "Get Task File", "Returns a list of ATA commands to restore a drive to default state"),
    AH_PREDEF ("_GTM",    "Get Timing Mode", "Returns a list of IDE controller timing information"),
    AH_PREDEF ("_GTS",    "Going To Sleep", "Inform AML of pending sleep"),
    AH_PREDEF ("_GWS",    "Get Wake Status", "Return status of wake alarms"),
    AH_PREDEF ("_HE_",    "High-Edge", "Interrupt triggering, Resource Descriptor field"),
    AH_PREDEF ("_HID",    "Hardware ID", "Returns a device's Plug and Play Hardware ID"),
    AH_PREDEF ("_HMA",    "Heterogeneous Memory Attributes", "Returns a list of HMAT structures."),
    AH_PREDEF ("_HOT",    "Hot Temperature", "Returns the critical temperature for sleep (entry to S4)"),
    AH_PREDEF ("_HPP",    "Hot Plug Parameters", "Returns a list of hot-plug information for a PCI device"),
    AH_PREDEF ("_HPX",    "Hot Plug Parameter Extensions", "Returns a list of hot-plug information for a PCI device. Supersedes _HPP"),
    AH_PREDEF ("_HRV",    "Hardware Revision", "Returns a hardware revision value"),
    AH_PREDEF ("_IFT",    "IPMI Interface Type", "See the Intelligent Platform Management Interface Specification"),
    AH_PREDEF ("_INI",    "Initialize", "Performs device specific initialization"),
    AH_PREDEF ("_INT",    "Interrupts", "Interrupt mask bits, Resource Descriptor field"),
    AH_PREDEF ("_IOR",    "I/O Restriction", "Restriction type, Resource Descriptor field"),
    AH_PREDEF ("_IRC",    "Inrush Current", "Presence indicates that a device has a significant inrush current draw"),
    AH_PREDEF ("_Lxx",    "Level-Triggered GPE, xx=0x00-0xFF", "Control method executed as a result of a general-purpose event"),
    AH_PREDEF ("_LCK",    "Lock Device", "Locks or unlocks a device (docking)"),
    AH_PREDEF ("_LEN",    "Length", "Range length, Resource Descriptor field"),
    AH_PREDEF ("_LID",    "Lid Status", "Returns the open/closed status of the lid on a mobile system"),
    AH_PREDEF ("_LIN",    "Lines In Use", "Handshake lines, Resource Descriptor field"),
    AH_PREDEF ("_LL_",    "Low Level", "Interrupt polarity, Resource Descriptor field"),
    AH_PREDEF ("_LPD",    "Low Power Dependencies", "Returns a list of dependencies for low power idle entry"),
    AH_PREDEF ("_LPI",    "Low Power Idle States", "Returns a list of supported low power idle states"),
    AH_PREDEF ("_LSI",    "Label Storage Information", "Returns information about the Label Storage Area associated with the NVDIMM object."),
    AH_PREDEF ("_LSR",    "Label Storage Read", "Returns label data from the Label Storage Area of the NVDIMM object."),
    AH_PREDEF ("_LSW",    "Label Storage Write", "Writes label data in to the Label Storage Area of the NVDIMM object."),
    AH_PREDEF ("_MAF",    "Maximum Address Fixed", "Resource Descriptor field"),
    AH_PREDEF ("_MAT",    "Multiple APIC Table Entry", "Returns a list of MADT APIC structure entries"),
    AH_PREDEF ("_MAX",    "Maximum Base Address", "Resource Descriptor field"),
    AH_PREDEF ("_MBM",    "Memory Bandwidth Monitoring Data", "Returns bandwidth monitoring data for a memory device"),
    AH_PREDEF ("_MEM",    "Memory Attributes", "Resource Descriptor field"),
    AH_PREDEF ("_MIF",    "Minimum Address Fixed", "Resource Descriptor field"),
    AH_PREDEF ("_MIN",    "Minimum Base Address", "Resource Descriptor field"),
    AH_PREDEF ("_MLS",    "Multiple Language String", "Returns a device description in multiple languages"),
    AH_PREDEF ("_MOD",    "Mode", "Interrupt mode, Resource Descriptor field"),
    AH_PREDEF ("_MSG",    "Message", "Sets the system message waiting status indicator"),
    AH_PREDEF ("_MSM",    "Memory Set Monitoring", "Sets bandwidth monitoring parameters for a memory device"),
    AH_PREDEF ("_MTL",    "Minimum Throttle Limit", "Returns the minimum throttle limit for a thermal zone"),
    AH_PREDEF ("_MTP",    "Memory Type", "Resource Descriptor field"),
    AH_PREDEF ("_NBS",    "NVDIMM Boot Status", "Returns information about NVDIMM device’s status at boot time"),
    AH_PREDEF ("_NCH",    "NVDIMM Current Health Information", "Returns current health information of the NVDIMM device"),
    AH_PREDEF ("_NIC",    "NVDIMM Health Error Injection Capabilities", "Returns health error injection capabilities that are supported by the platform"),
    AH_PREDEF ("_NIG",    "NVDIMM Inject Health Error Status","Returns currently active health errors and their error attributes that are injected by _NIH"),
    AH_PREDEF ("_NIH",    "NVDIMM Inject/Clear Health Errors", "Returns the status of injecting or clearing Health Errors"),
    AH_PREDEF ("_NTT",    "Notification Temperature Threshold", "Returns a threshold for device temperature change that requires platform notification"),
    AH_PREDEF ("_OFF",    "Power Off", "Sets a power resource to the off state"),
    AH_PREDEF ("_ON_",    "Power On", "Sets a power resource to the on state"),
    AH_PREDEF ("_OS_",    "Operating System", "Returns a string that identifies the operating system"),
    AH_PREDEF ("_OSC",    "Operating System Capabilities", "Inform AML of host features and capabilities"),
    AH_PREDEF ("_OSI",    "Operating System Interfaces", "Returns supported interfaces, behaviors, and features"),
    AH_PREDEF ("_OST",    "OSPM Status Indication", "Inform AML of event processing status"),
    AH_PREDEF ("_PAI",    "Power Averaging Interval", "Sets the averaging interval for a power meter"),
    AH_PREDEF ("_PAR",    "Parity", "Parity bits, Resource Descriptor field"),
    AH_PREDEF ("_PCL",    "Power Consumer List", "Returns a list of devices powered by a power source"),
    AH_PREDEF ("_PCT",    "Performance Control", "Returns processor performance control and status registers"),
    AH_PREDEF ("_PDC",    "Processor Driver Capabilities", "Inform AML of processor driver capabilities"),
    AH_PREDEF ("_PDL",    "P-state Depth Limit", "Returns the lowest available performance P-state"),
    AH_PREDEF ("_PHA",    "Clock Phase", "Clock phase, Resource Descriptor field"),
    AH_PREDEF ("_PIC",    "Interrupt Model", "Inform AML of the interrupt model in use"),
    AH_PREDEF ("_PIF",    "Power Source Information", "Returns a Power Source information block"),
    AH_PREDEF ("_PIN",    "Pin List", "Pin list, Resource Descriptor field"),
    AH_PREDEF ("_PLD",    "Physical Location of Device", "Returns a device's physical location information"),
    AH_PREDEF ("_PMC",    "Power Meter Capabilities", "Returns a list of Power Meter capabilities info"),
    AH_PREDEF ("_PMD",    "Power Metered Devices", "Returns a list of devices that are measured by the power meter device"),
    AH_PREDEF ("_PMM",    "Power Meter Measurement", "Returns the current value of the Power Meter"),
    AH_PREDEF ("_POL",    "Polarity", "Interrupt polarity, Resource Descriptor field"),
    AH_PREDEF ("_PPC",    "Performance Present Capabilities", "Returns a list of the performance states currently supported by the platform"),
    AH_PREDEF ("_PPE",    "Polling for Platform Error", "Returns the polling interval to retrieve Corrected Platform Error information"),
    AH_PREDEF ("_PPI",    "Pin Configuration", "Resource Descriptor field"),
    AH_PREDEF ("_PR",     "Processor", "Predefined scope for processor objects"),
    AH_PREDEF ("_PR0",    "Power Resources for D0", "Returns a list of dependent power resources to enter state D0 (fully on)"),
    AH_PREDEF ("_PR1",    "Power Resources for D1", "Returns a list of dependent power resources to enter state D1"),
    AH_PREDEF ("_PR2",    "Power Resources for D2", "Returns a list of dependent power resources to enter state D2"),
    AH_PREDEF ("_PR3",    "Power Resources for D3hot", "Returns a list of dependent power resources to enter state D3hot"),
    AH_PREDEF ("_PRE",    "Power Resources for Enumeration", "Returns a list of dependent power resources to enumerate devices on a bus"),
    AH_PREDEF ("_PRL",    "Power Source Redundancy List", "Returns a list of power source devices in the same redundancy grouping"),
    AH_PREDEF ("_PRR",    "Power Resource for Reset", "Execute a reset on a device"),
    AH_PREDEF ("_PRS",    "Possible Resource Settings", "Returns a list of a device's possible resource settings"),
    AH_PREDEF ("_PRT",    "PCI Routing Table", "Returns a list of PCI interrupt mappings"),
    AH_PREDEF ("_PRW",    "Power Resources for Wake", "Returns a list of dependent power resources for waking"),
    AH_PREDEF ("_PS0",    "Power State 0", "Sets a device's power state to D0 (device fully on)"),
    AH_PREDEF ("_PS1",    "Power State 1", "Sets a device's power state to D1"),
    AH_PREDEF ("_PS2",    "Power State 2", "Sets a device's power state to D2"),
    AH_PREDEF ("_PS3",    "Power State 3", "Sets a device's power state to D3 (device off)"),
    AH_PREDEF ("_PSC",    "Power State Current", "Returns a device's current power state"),
    AH_PREDEF ("_PSD",    "Power State Dependencies", "Returns processor P-State dependencies"),
    AH_PREDEF ("_PSE",    "Power State for Enumeration", "Put a bus into enumeration power mode"),
    AH_PREDEF ("_PSL",    "Passive List", "Returns a list of passive cooling device objects"),
    AH_PREDEF ("_PSR",    "Power Source", "Returns the power source device currently in use"),
    AH_PREDEF ("_PSS",    "Performance Supported States", "Returns a list of supported processor performance states"),
    AH_PREDEF ("_PSV",    "Passive Temperature", "Returns the passive trip point temperature"),
    AH_PREDEF ("_PSW",    "Power State Wake", "Sets a device's wake function"),
    AH_PREDEF ("_PTC",    "Processor Throttling Control", "Returns throttling control and status registers"),
    AH_PREDEF ("_PTP",    "Power Trip Points", "Sets trip points for the Power Meter device"),
    AH_PREDEF ("_PTS",    "Prepare To Sleep", "Inform the platform of an impending sleep transition"),
    AH_PREDEF ("_PUR",    "Processor Utilization Request", "Returns the number of processors that the platform would like to idle"),
    AH_PREDEF ("_PXM",    "Device Proximity", "Returns a device's proximity domain identifier"),
    AH_PREDEF ("_Qxx",    "EC Query, xx=0x00-0xFF", "Embedded Controller query and SMBus Alarm control method"),
    AH_PREDEF ("_RBO",    "Register Bit Offset", "Resource Descriptor field"),
    AH_PREDEF ("_RBW",    "Register Bit Width", "Resource Descriptor field"),
    AH_PREDEF ("_RDI",    "Resource Dependencies for Idle", "Returns a list of dependencies for idle states"),
    AH_PREDEF ("_REG",    "Region Availability", "Inform AML code of an operation region availability change"),
    AH_PREDEF ("_REV",    "Supported Integer Width", "Returns the supported integer width (<= 1: 32 bits only, >=2: both 32 and 64 bits"),
    AH_PREDEF ("_RMV",    "Removal Status", "Returns a device's removal ability status (docking)"),
    AH_PREDEF ("_RNG",    "Range", "Memory range type, Resource Descriptor field"),
    AH_PREDEF ("_RST",    "Device Reset", "Executes a reset on a device"),
    AH_PREDEF ("_ROM",    "Read-Only Memory", "Returns a copy of the ROM data for a display device"),
    AH_PREDEF ("_RT_",    "Resource Type", "Resource Descriptor field"),
    AH_PREDEF ("_RTV",    "Relative Temperature Values", "Returns temperature value information"),
    AH_PREDEF ("_RW_",    "Read-Write Status", "Resource Descriptor field"),
    AH_PREDEF ("_RXL",    "Receive Buffer Size", "Serial channel buffer, Resource Descriptor field"),
    AH_PREDEF ("_S0_",    "S0 System State", "Returns values to enter the system into the S0 state"),
    AH_PREDEF ("_S1_",    "S1 System State", "Returns values to enter the system into the S1 state"),
    AH_PREDEF ("_S2_",    "S2 System State", "Returns values to enter the system into the S2 state"),
    AH_PREDEF ("_S3_",    "S3 System State", "Returns values to enter the system into the S3 state"),
    AH_PREDEF ("_S4_",    "S4 System State", "Returns values to enter the system into the S4 state"),
    AH_PREDEF ("_S5_",    "S5 System State", "Returns values to enter the system into the S5 state"),
    AH_PREDEF ("_S1D",    "S1 Device State", "Returns the highest D-state supported by a device when in the S1 state"),
    AH_PREDEF ("_S2D",    "S2 Device State", "Returns the highest D-state supported by a device when in the S2 state"),
    AH_PREDEF ("_S3D",    "S3 Device State", "Returns the highest D-state supported by a device when in the S3 state"),
    AH_PREDEF ("_S4D",    "S4 Device State", "Returns the highest D-state supported by a device when in the S4 state"),
    AH_PREDEF ("_S0W",    "S0 Device Wake State", "Returns the lowest D-state that the device can wake itself from S0"),
    AH_PREDEF ("_S1W",    "S1 Device Wake State", "Returns the lowest D-state for this device that can wake the system from S1"),
    AH_PREDEF ("_S2W",    "S2 Device Wake State", "Returns the lowest D-state for this device that can wake the system from S2"),
    AH_PREDEF ("_S3W",    "S3 Device Wake State", "Returns the lowest D-state for this device that can wake the system from S3"),
    AH_PREDEF ("_S4W",    "S4 Device Wake State", "Returns the lowest D-state for this device that can wake the system from S4"),
    AH_PREDEF ("_SB_",    "System Bus", "Predefined scope for device and bus objects"),
    AH_PREDEF ("_SBS",    "Smart Battery Subsystem", "Returns the subsystem configuration"),
    AH_PREDEF ("_SCP",    "Set Cooling Policy", "Sets the cooling policy (active or passive)"),
    AH_PREDEF ("_SDD",    "Set Device Data", "Sets data for a SATA device"),
    AH_PREDEF ("_SEG",    "PCI Segment", "Returns a device's PCI Segment Group number"),
    AH_PREDEF ("_SHL",    "Set Hardware Limit", "Sets the hardware limit enforced by the Power Meter"),
    AH_PREDEF ("_SHR",    "Shareable", "Interrupt share status, Resource Descriptor field"),
    AH_PREDEF ("_SI_",    "System Indicators", "Predefined scope"),
    AH_PREDEF ("_SIZ",    "Size", "DMA transfer size, Resource Descriptor field"),
    AH_PREDEF ("_SLI",    "System Locality Information", "Returns a list of NUMA system localities"),
    AH_PREDEF ("_SLV",    "Slave Mode", "Mode setting, Resource Descriptor field"),
    AH_PREDEF ("_SPD",    "Set Post Device", "Sets which video device will be posted at boot"),
    AH_PREDEF ("_SPE",    "Speed", "Connection speed, Resource Descriptor field"),
    AH_PREDEF ("_SRS",    "Set Resource Settings", "Sets a device's resource allocation"),
    AH_PREDEF ("_SRT",    "Set Real Time", "Sets the current time for a time/alarm device"),
    AH_PREDEF ("_SRV",    "IPMI Spec Revision", "See the Intelligent Platform Management Interface Specification"),
    AH_PREDEF ("_SST",    "System Status", "Sets the system status indicator"),
    AH_PREDEF ("_STA",    "Status", "Returns the current status of a Device or Power Resource"),
    AH_PREDEF ("_STB",    "Stop Bits", "Serial channel stop bits, Resource Descriptor field"),
    AH_PREDEF ("_STM",    "Set Timing Mode", "Sets an IDE controller transfer timings"),
    AH_PREDEF ("_STP",    "Set Expired Timer Wake Policy", "Sets expired timer policies of the wake alarm device"),
    AH_PREDEF ("_STR",    "Description String", "Returns a device's description string"),
    AH_PREDEF ("_STV",    "Set Timer Value", "Set timer values of the wake alarm device"),
    AH_PREDEF ("_SUB",    "Subsystem ID", "Returns the subsystem ID for a device"),
    AH_PREDEF ("_SUN",    "Slot User Number", "Returns the slot unique ID number"),
    AH_PREDEF ("_SWS",    "System Wake Source", "Returns the source event that caused the system to wake"),
    AH_PREDEF ("_T_x",    "Emitted by ASL Compiler, x=0-9, A-Z", "Reserved for use by ASL compilers"),
    AH_PREDEF ("_TC1",    "Thermal Constant 1", "Returns TC1 for the passive cooling formula"),
    AH_PREDEF ("_TC2",    "Thermal Constant 2", "Returns TC2 for the passive cooling formula"),
    AH_PREDEF ("_TDL",    "T-State Depth Limit", "Returns the _TSS entry number of the lowest power throttling state"),
    AH_PREDEF ("_TFP",    "Thermal Fast Sampling Period", "Returns the sampling period for passive cooling"),
    AH_PREDEF ("_TIP",    "Expired Timer Wake Policy", "Returns timer policies of the wake alarm device"),
    AH_PREDEF ("_TIV",    "Timer Values", "Returns remaining time of the wake alarm device"),
    AH_PREDEF ("_TMP",    "Temperature", "Returns a thermal zone's current temperature"),
    AH_PREDEF ("_TPC",    "Throttling Present Capabilities", "Returns the current number of supported throttling states"),
    AH_PREDEF ("_TPT",    "Trip Point Temperature", "Inform AML that a device's embedded temperature sensor has crossed a temperature trip point"),
    AH_PREDEF ("_TRA",    "Translation", "Address translation offset, Resource Descriptor field"),
    AH_PREDEF ("_TRS",    "Translation Sparse", "Sparse/dense flag, Resource Descriptor field"),
    AH_PREDEF ("_TRT",    "Thermal Relationship Table", "Returns thermal relationships between platform devices"),
    AH_PREDEF ("_TSD",    "Throttling State Dependencies", "Returns a list of T-state dependencies"),
    AH_PREDEF ("_TSF",    "Type-Specific Flags", "Resource Descriptor field"),
    AH_PREDEF ("_TSN",    "Thermal Sensor Device", "Returns a reference to a thermal sensor"),
    AH_PREDEF ("_TSP",    "Thermal Sampling Period", "Returns the thermal sampling period for passive cooling"),
    AH_PREDEF ("_TSS",    "Throttling Supported States", "Returns supported throttling state information"),
    AH_PREDEF ("_TST",    "Temperature Sensor Threshold", "Returns the minimum separation for a device's temperature trip points"),
    AH_PREDEF ("_TTP",    "Translation Type", "Translation/static flag, Resource Descriptor field"),
    AH_PREDEF ("_TTS",    "Transition To State", "Inform AML of an S-state transition"),
    AH_PREDEF ("_TXL",    "Transmit Buffer Size", "Serial Channel buffer, Resource Descriptor field"),
    AH_PREDEF ("_TYP",    "Type", "DMA channel type (speed), Resource Descriptor field"),
    AH_PREDEF ("_TZ_",    "Thermal Zone", "Predefined scope: ACPI 1.0"),
    AH_PREDEF ("_TZD",    "Thermal Zone Devices", "Returns a list of device names associated with a Thermal Zone"),
    AH_PREDEF ("_TZM",    "Thermal Zone Member", "Returns a reference to the thermal zone of which a device is a member"),
    AH_PREDEF ("_TZP",    "Thermal Zone Polling", "Returns a Thermal zone's polling frequency"),
    AH_PREDEF ("_UID",    "Unique ID", "Return a device's unique persistent ID"),
    AH_PREDEF ("_UPC",    "USB Port Capabilities", "Returns a list of USB port capabilities"),
    AH_PREDEF ("_UPD",    "User Presence Detect", "Returns user detection information"),
    AH_PREDEF ("_UPP",    "User Presence Polling", "Returns the recommended user presence polling interval"),
    AH_PREDEF ("_VAL",    "Pin Configuration Value", "Resource Descriptor field"),
    AH_PREDEF ("_VEN",    "Vendor Data", "Resource Descriptor field"),
    AH_PREDEF ("_VPO",    "Video Post Options", "Returns the implemented video post options"),
    AH_PREDEF ("_Wxx",    "Wake Event, xx=0x00-0xFF", "Method executed as a result of a wake event"),
    AH_PREDEF ("_WAK",    "Wake", "Inform AML that the system has just awakened"),
    AH_PREDEF ("_WPC",    "Wireless Power Calibration", "Calibrate power and notify wireless device"),
    AH_PREDEF ("_WPP",    "Wireless Power Polling", "Get recommended polling interval"),
    AH_PREDEF (NULL,      NULL, NULL)
};


/*******************************************************************************
 *
 * FUNCTION:    AcpiAhMatchPredefinedName
 *
 * PARAMETERS:  Nameseg                 - Predefined name string
 *
 * RETURN:      ID info struct. NULL if Nameseg not found
 *
 * DESCRIPTION: Lookup a predefined name.
 *
 ******************************************************************************/

const AH_PREDEFINED_NAME *
AcpiAhMatchPredefinedName (
    char                        *Nameseg)
{
    const AH_PREDEFINED_NAME    *Info;


    /* Nameseg must start with an underscore */

    if (*Nameseg != '_')
    {
        return (NULL);
    }

    /* Search for a match in the predefined name table */

    for (Info = AslPredefinedInfo; Info->Name; Info++)
    {
        if (ACPI_COMPARE_NAME (Nameseg, Info->Name))
        {
            return (Info);
        }
    }

    return (NULL);
}
