/******************************************************************************
 *
 * Module Name: utglobal - Global variables for the ACPI subsystem
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
#define DEFINE_ACPI_GLOBALS

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>

#define _COMPONENT          ACPI_UTILITIES
        ACPI_MODULE_NAME    ("utglobal")


/*******************************************************************************
 *
 * Static global variable initialization.
 *
 ******************************************************************************/

/* Various state name strings */

const char                  *AcpiGbl_SleepStateNames[ACPI_S_STATE_COUNT] =
{
    "\\_S0_",
    "\\_S1_",
    "\\_S2_",
    "\\_S3_",
    "\\_S4_",
    "\\_S5_"
};

const char                  *AcpiGbl_LowestDstateNames[ACPI_NUM_SxW_METHODS] =
{
    "_S0W",
    "_S1W",
    "_S2W",
    "_S3W",
    "_S4W"
};

const char                  *AcpiGbl_HighestDstateNames[ACPI_NUM_SxD_METHODS] =
{
    "_S1D",
    "_S2D",
    "_S3D",
    "_S4D"
};


/* Hex-to-ascii */

const char                  AcpiGbl_LowerHexDigits[] = "0123456789abcdef";
const char                  AcpiGbl_UpperHexDigits[] = "0123456789ABCDEF";


/*******************************************************************************
 *
 * Namespace globals
 *
 ******************************************************************************/

/*
 * Predefined ACPI Names (Built-in to the Interpreter)
 *
 * NOTES:
 * 1) _SB_ is defined to be a device to allow \_SB_._INI to be run
 *    during the initialization sequence.
 * 2) _TZ_ is defined to be a thermal zone in order to allow ASL code to
 *    perform a Notify() operation on it. 09/2010: Changed to type Device.
 *    This still allows notifies, but does not confuse host code that
 *    searches for valid ThermalZone objects.
 */
const ACPI_PREDEFINED_NAMES     AcpiGbl_PreDefinedNames[] =
{
    {"_GPE",    ACPI_TYPE_LOCAL_SCOPE,      NULL},
    {"_PR_",    ACPI_TYPE_LOCAL_SCOPE,      NULL},
    {"_SB_",    ACPI_TYPE_DEVICE,           NULL},
    {"_SI_",    ACPI_TYPE_LOCAL_SCOPE,      NULL},
    {"_TZ_",    ACPI_TYPE_DEVICE,           NULL},
    /*
     * March, 2015:
     * The _REV object is in the process of being deprecated, because
     * other ACPI implementations permanently return 2. Thus, it
     * has little or no value. Return 2 for compatibility with
     * other ACPI implementations.
     */
    {"_REV",    ACPI_TYPE_INTEGER,          ACPI_CAST_PTR (char, 2)},
    {"_OS_",    ACPI_TYPE_STRING,           ACPI_OS_NAME},
    {"_GL_",    ACPI_TYPE_MUTEX,            ACPI_CAST_PTR (char, 1)},
    {"_OSI",    ACPI_TYPE_METHOD,           ACPI_CAST_PTR (char, 1)},

    /* Table terminator */

    {NULL,      ACPI_TYPE_ANY,              NULL}
};


#if (!ACPI_REDUCED_HARDWARE)
/******************************************************************************
 *
 * Event and Hardware globals
 *
 ******************************************************************************/

ACPI_BIT_REGISTER_INFO      AcpiGbl_BitRegisterInfo[ACPI_NUM_BITREG] =
{
    /* Name                                     Parent Register             Register Bit Position                   Register Bit Mask       */

    /* ACPI_BITREG_TIMER_STATUS         */   {ACPI_REGISTER_PM1_STATUS,   ACPI_BITPOSITION_TIMER_STATUS,          ACPI_BITMASK_TIMER_STATUS},
    /* ACPI_BITREG_BUS_MASTER_STATUS    */   {ACPI_REGISTER_PM1_STATUS,   ACPI_BITPOSITION_BUS_MASTER_STATUS,     ACPI_BITMASK_BUS_MASTER_STATUS},
    /* ACPI_BITREG_GLOBAL_LOCK_STATUS   */   {ACPI_REGISTER_PM1_STATUS,   ACPI_BITPOSITION_GLOBAL_LOCK_STATUS,    ACPI_BITMASK_GLOBAL_LOCK_STATUS},
    /* ACPI_BITREG_POWER_BUTTON_STATUS  */   {ACPI_REGISTER_PM1_STATUS,   ACPI_BITPOSITION_POWER_BUTTON_STATUS,   ACPI_BITMASK_POWER_BUTTON_STATUS},
    /* ACPI_BITREG_SLEEP_BUTTON_STATUS  */   {ACPI_REGISTER_PM1_STATUS,   ACPI_BITPOSITION_SLEEP_BUTTON_STATUS,   ACPI_BITMASK_SLEEP_BUTTON_STATUS},
    /* ACPI_BITREG_RT_CLOCK_STATUS      */   {ACPI_REGISTER_PM1_STATUS,   ACPI_BITPOSITION_RT_CLOCK_STATUS,       ACPI_BITMASK_RT_CLOCK_STATUS},
    /* ACPI_BITREG_WAKE_STATUS          */   {ACPI_REGISTER_PM1_STATUS,   ACPI_BITPOSITION_WAKE_STATUS,           ACPI_BITMASK_WAKE_STATUS},
    /* ACPI_BITREG_PCIEXP_WAKE_STATUS   */   {ACPI_REGISTER_PM1_STATUS,   ACPI_BITPOSITION_PCIEXP_WAKE_STATUS,    ACPI_BITMASK_PCIEXP_WAKE_STATUS},

    /* ACPI_BITREG_TIMER_ENABLE         */   {ACPI_REGISTER_PM1_ENABLE,   ACPI_BITPOSITION_TIMER_ENABLE,          ACPI_BITMASK_TIMER_ENABLE},
    /* ACPI_BITREG_GLOBAL_LOCK_ENABLE   */   {ACPI_REGISTER_PM1_ENABLE,   ACPI_BITPOSITION_GLOBAL_LOCK_ENABLE,    ACPI_BITMASK_GLOBAL_LOCK_ENABLE},
    /* ACPI_BITREG_POWER_BUTTON_ENABLE  */   {ACPI_REGISTER_PM1_ENABLE,   ACPI_BITPOSITION_POWER_BUTTON_ENABLE,   ACPI_BITMASK_POWER_BUTTON_ENABLE},
    /* ACPI_BITREG_SLEEP_BUTTON_ENABLE  */   {ACPI_REGISTER_PM1_ENABLE,   ACPI_BITPOSITION_SLEEP_BUTTON_ENABLE,   ACPI_BITMASK_SLEEP_BUTTON_ENABLE},
    /* ACPI_BITREG_RT_CLOCK_ENABLE      */   {ACPI_REGISTER_PM1_ENABLE,   ACPI_BITPOSITION_RT_CLOCK_ENABLE,       ACPI_BITMASK_RT_CLOCK_ENABLE},
    /* ACPI_BITREG_PCIEXP_WAKE_DISABLE  */   {ACPI_REGISTER_PM1_ENABLE,   ACPI_BITPOSITION_PCIEXP_WAKE_DISABLE,   ACPI_BITMASK_PCIEXP_WAKE_DISABLE},

    /* ACPI_BITREG_SCI_ENABLE           */   {ACPI_REGISTER_PM1_CONTROL,  ACPI_BITPOSITION_SCI_ENABLE,            ACPI_BITMASK_SCI_ENABLE},
    /* ACPI_BITREG_BUS_MASTER_RLD       */   {ACPI_REGISTER_PM1_CONTROL,  ACPI_BITPOSITION_BUS_MASTER_RLD,        ACPI_BITMASK_BUS_MASTER_RLD},
    /* ACPI_BITREG_GLOBAL_LOCK_RELEASE  */   {ACPI_REGISTER_PM1_CONTROL,  ACPI_BITPOSITION_GLOBAL_LOCK_RELEASE,   ACPI_BITMASK_GLOBAL_LOCK_RELEASE},
    /* ACPI_BITREG_SLEEP_TYPE           */   {ACPI_REGISTER_PM1_CONTROL,  ACPI_BITPOSITION_SLEEP_TYPE,            ACPI_BITMASK_SLEEP_TYPE},
    /* ACPI_BITREG_SLEEP_ENABLE         */   {ACPI_REGISTER_PM1_CONTROL,  ACPI_BITPOSITION_SLEEP_ENABLE,          ACPI_BITMASK_SLEEP_ENABLE},

    /* ACPI_BITREG_ARB_DIS              */   {ACPI_REGISTER_PM2_CONTROL,  ACPI_BITPOSITION_ARB_DISABLE,           ACPI_BITMASK_ARB_DISABLE}
};


ACPI_FIXED_EVENT_INFO       AcpiGbl_FixedEventInfo[ACPI_NUM_FIXED_EVENTS] =
{
    /* ACPI_EVENT_PMTIMER       */  {ACPI_BITREG_TIMER_STATUS,          ACPI_BITREG_TIMER_ENABLE,        ACPI_BITMASK_TIMER_STATUS,          ACPI_BITMASK_TIMER_ENABLE},
    /* ACPI_EVENT_GLOBAL        */  {ACPI_BITREG_GLOBAL_LOCK_STATUS,    ACPI_BITREG_GLOBAL_LOCK_ENABLE,  ACPI_BITMASK_GLOBAL_LOCK_STATUS,    ACPI_BITMASK_GLOBAL_LOCK_ENABLE},
    /* ACPI_EVENT_POWER_BUTTON  */  {ACPI_BITREG_POWER_BUTTON_STATUS,   ACPI_BITREG_POWER_BUTTON_ENABLE, ACPI_BITMASK_POWER_BUTTON_STATUS,   ACPI_BITMASK_POWER_BUTTON_ENABLE},
    /* ACPI_EVENT_SLEEP_BUTTON  */  {ACPI_BITREG_SLEEP_BUTTON_STATUS,   ACPI_BITREG_SLEEP_BUTTON_ENABLE, ACPI_BITMASK_SLEEP_BUTTON_STATUS,   ACPI_BITMASK_SLEEP_BUTTON_ENABLE},
    /* ACPI_EVENT_RTC           */  {ACPI_BITREG_RT_CLOCK_STATUS,       ACPI_BITREG_RT_CLOCK_ENABLE,     ACPI_BITMASK_RT_CLOCK_STATUS,       ACPI_BITMASK_RT_CLOCK_ENABLE},
};
#endif /* !ACPI_REDUCED_HARDWARE */


#if defined (ACPI_DISASSEMBLER) || defined (ACPI_ASL_COMPILER)

/* ToPld macro: compile/disassemble strings */

const char              *AcpiGbl_PldPanelList[] =
{
    "TOP",
    "BOTTOM",
    "LEFT",
    "RIGHT",
    "FRONT",
    "BACK",
    "UNKNOWN",
    NULL
};

const char              *AcpiGbl_PldVerticalPositionList[] =
{
    "UPPER",
    "CENTER",
    "LOWER",
    NULL
};

const char              *AcpiGbl_PldHorizontalPositionList[] =
{
    "LEFT",
    "CENTER",
    "RIGHT",
    NULL
};

const char              *AcpiGbl_PldShapeList[] =
{
    "ROUND",
    "OVAL",
    "SQUARE",
    "VERTICALRECTANGLE",
    "HORIZONTALRECTANGLE",
    "VERTICALTRAPEZOID",
    "HORIZONTALTRAPEZOID",
    "UNKNOWN",
    "CHAMFERED",
    NULL
};
#endif


/* Public globals */

ACPI_EXPORT_SYMBOL (AcpiGbl_FADT)
ACPI_EXPORT_SYMBOL (AcpiDbgLevel)
ACPI_EXPORT_SYMBOL (AcpiDbgLayer)
ACPI_EXPORT_SYMBOL (AcpiGpeCount)
ACPI_EXPORT_SYMBOL (AcpiCurrentGpeCount)
