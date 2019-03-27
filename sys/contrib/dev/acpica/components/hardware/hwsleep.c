/******************************************************************************
 *
 * Name: hwsleep.c - ACPI Hardware Sleep/Wake Support functions for the
 *                   original/legacy sleep/PM registers.
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

#define _COMPONENT          ACPI_HARDWARE
        ACPI_MODULE_NAME    ("hwsleep")


#if (!ACPI_REDUCED_HARDWARE) /* Entire module */
/*******************************************************************************
 *
 * FUNCTION:    AcpiHwLegacySleep
 *
 * PARAMETERS:  SleepState          - Which sleep state to enter
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enter a system sleep state via the legacy FADT PM registers
 *              THIS FUNCTION MUST BE CALLED WITH INTERRUPTS DISABLED
 *
 ******************************************************************************/

ACPI_STATUS
AcpiHwLegacySleep (
    UINT8                   SleepState)
{
    ACPI_BIT_REGISTER_INFO  *SleepTypeRegInfo;
    ACPI_BIT_REGISTER_INFO  *SleepEnableRegInfo;
    UINT32                  Pm1aControl;
    UINT32                  Pm1bControl;
    UINT32                  InValue;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (HwLegacySleep);


    SleepTypeRegInfo = AcpiHwGetBitRegisterInfo (ACPI_BITREG_SLEEP_TYPE);
    SleepEnableRegInfo = AcpiHwGetBitRegisterInfo (ACPI_BITREG_SLEEP_ENABLE);

    /* Clear wake status */

    Status = AcpiWriteBitRegister (ACPI_BITREG_WAKE_STATUS,
        ACPI_CLEAR_STATUS);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Disable all GPEs */

    Status = AcpiHwDisableAllGpes ();
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }
    Status = AcpiHwClearAcpiStatus();
    if (ACPI_FAILURE(Status))
    {
        return_ACPI_STATUS(Status);
    }
    AcpiGbl_SystemAwakeAndRunning = FALSE;

    /* Enable all wakeup GPEs */

    Status = AcpiHwEnableAllWakeupGpes ();
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Get current value of PM1A control */

    Status = AcpiHwRegisterRead (ACPI_REGISTER_PM1_CONTROL,
        &Pm1aControl);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }
    ACPI_DEBUG_PRINT ((ACPI_DB_INIT,
        "Entering sleep state [S%u]\n", SleepState));

    /* Clear the SLP_EN and SLP_TYP fields */

    Pm1aControl &= ~(SleepTypeRegInfo->AccessBitMask |
         SleepEnableRegInfo->AccessBitMask);
    Pm1bControl = Pm1aControl;

    /* Insert the SLP_TYP bits */

    Pm1aControl |= (AcpiGbl_SleepTypeA << SleepTypeRegInfo->BitPosition);
    Pm1bControl |= (AcpiGbl_SleepTypeB << SleepTypeRegInfo->BitPosition);

    /*
     * We split the writes of SLP_TYP and SLP_EN to workaround
     * poorly implemented hardware.
     */

    /* Write #1: write the SLP_TYP data to the PM1 Control registers */

    Status = AcpiHwWritePm1Control (Pm1aControl, Pm1bControl);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Insert the sleep enable (SLP_EN) bit */

    Pm1aControl |= SleepEnableRegInfo->AccessBitMask;
    Pm1bControl |= SleepEnableRegInfo->AccessBitMask;

    /* Flush caches, as per ACPI specification */

    ACPI_FLUSH_CPU_CACHE ();

    Status = AcpiOsEnterSleep (SleepState, Pm1aControl, Pm1bControl);
    if (Status == AE_CTRL_TERMINATE)
    {
        return_ACPI_STATUS (AE_OK);
    }
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /* Write #2: Write both SLP_TYP + SLP_EN */

    Status = AcpiHwWritePm1Control (Pm1aControl, Pm1bControl);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    if (SleepState > ACPI_STATE_S3)
    {
        /*
         * We wanted to sleep > S3, but it didn't happen (by virtue of the
         * fact that we are still executing!)
         *
         * Wait ten seconds, then try again. This is to get S4/S5 to work on
         * all machines.
         *
         * We wait so long to allow chipsets that poll this reg very slowly
         * to still read the right value. Ideally, this block would go
         * away entirely.
         */
        AcpiOsStall (10 * ACPI_USEC_PER_SEC);

        Status = AcpiHwRegisterWrite (ACPI_REGISTER_PM1_CONTROL,
            SleepEnableRegInfo->AccessBitMask);
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }
    }

    /* Wait for transition back to Working State */

    do
    {
        Status = AcpiReadBitRegister (ACPI_BITREG_WAKE_STATUS, &InValue);
        if (ACPI_FAILURE (Status))
        {
            return_ACPI_STATUS (Status);
        }

    } while (!InValue);

    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiHwLegacyWakePrep
 *
 * PARAMETERS:  SleepState          - Which sleep state we just exited
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform the first state of OS-independent ACPI cleanup after a
 *              sleep.
 *              Called with interrupts ENABLED.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiHwLegacyWakePrep (
    UINT8                   SleepState)
{
    ACPI_STATUS             Status;
    ACPI_BIT_REGISTER_INFO  *SleepTypeRegInfo;
    ACPI_BIT_REGISTER_INFO  *SleepEnableRegInfo;
    UINT32                  Pm1aControl;
    UINT32                  Pm1bControl;


    ACPI_FUNCTION_TRACE (HwLegacyWakePrep);

    /*
     * Set SLP_TYPE and SLP_EN to state S0.
     * This is unclear from the ACPI Spec, but it is required
     * by some machines.
     */
    Status = AcpiGetSleepTypeData (ACPI_STATE_S0,
        &AcpiGbl_SleepTypeA, &AcpiGbl_SleepTypeB);
    if (ACPI_SUCCESS (Status))
    {
        SleepTypeRegInfo =
            AcpiHwGetBitRegisterInfo (ACPI_BITREG_SLEEP_TYPE);
        SleepEnableRegInfo =
            AcpiHwGetBitRegisterInfo (ACPI_BITREG_SLEEP_ENABLE);

        /* Get current value of PM1A control */

        Status = AcpiHwRegisterRead (ACPI_REGISTER_PM1_CONTROL,
            &Pm1aControl);
        if (ACPI_SUCCESS (Status))
        {
            /* Clear the SLP_EN and SLP_TYP fields */

            Pm1aControl &= ~(SleepTypeRegInfo->AccessBitMask |
                SleepEnableRegInfo->AccessBitMask);
            Pm1bControl = Pm1aControl;

            /* Insert the SLP_TYP bits */

            Pm1aControl |= (AcpiGbl_SleepTypeA <<
                SleepTypeRegInfo->BitPosition);
            Pm1bControl |= (AcpiGbl_SleepTypeB <<
                SleepTypeRegInfo->BitPosition);

            /* Write the control registers and ignore any errors */

            (void) AcpiHwWritePm1Control (Pm1aControl, Pm1bControl);
        }
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiHwLegacyWake
 *
 * PARAMETERS:  SleepState          - Which sleep state we just exited
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform OS-independent ACPI cleanup after a sleep
 *              Called with interrupts ENABLED.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiHwLegacyWake (
    UINT8                   SleepState)
{
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (HwLegacyWake);


    /* Ensure EnterSleepStatePrep -> EnterSleepState ordering */

    AcpiGbl_SleepTypeA = ACPI_SLEEP_TYPE_INVALID;
    AcpiHwExecuteSleepMethod (METHOD_PATHNAME__SST, ACPI_SST_WAKING);

    /*
     * GPEs must be enabled before _WAK is called as GPEs
     * might get fired there
     *
     * Restore the GPEs:
     * 1) Disable all GPEs
     * 2) Enable all runtime GPEs
     */
    Status = AcpiHwDisableAllGpes ();
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    Status = AcpiHwEnableAllRuntimeGpes ();
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    /*
     * Now we can execute _WAK, etc. Some machines require that the GPEs
     * are enabled before the wake methods are executed.
     */
    AcpiHwExecuteSleepMethod (METHOD_PATHNAME__WAK, SleepState);

    /*
     * Some BIOS code assumes that WAK_STS will be cleared on resume
     * and use it to determine whether the system is rebooting or
     * resuming. Clear WAK_STS for compatibility.
     */
    (void) AcpiWriteBitRegister (ACPI_BITREG_WAKE_STATUS,
        ACPI_CLEAR_STATUS);
    AcpiGbl_SystemAwakeAndRunning = TRUE;

    /* Enable power button */

    (void) AcpiWriteBitRegister(
            AcpiGbl_FixedEventInfo[ACPI_EVENT_POWER_BUTTON].EnableRegisterId,
            ACPI_ENABLE_EVENT);

    (void) AcpiWriteBitRegister(
            AcpiGbl_FixedEventInfo[ACPI_EVENT_POWER_BUTTON].StatusRegisterId,
            ACPI_CLEAR_STATUS);

    AcpiHwExecuteSleepMethod (METHOD_PATHNAME__SST, ACPI_SST_WORKING);
    return_ACPI_STATUS (Status);
}

#endif /* !ACPI_REDUCED_HARDWARE */
