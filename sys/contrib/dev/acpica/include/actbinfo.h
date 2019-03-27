/******************************************************************************
 *
 * Module Name: actbinfo - Table disassembly info for non-AML tables
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

/*
 * Macros used to generate offsets to specific table fields
 */
#define ACPI_FACS_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_FACS,f)
#define ACPI_GAS_OFFSET(f)              (UINT16) ACPI_OFFSET (ACPI_GENERIC_ADDRESS,f)
#define ACPI_HDR_OFFSET(f)              (UINT16) ACPI_OFFSET (ACPI_TABLE_HEADER,f)
#define ACPI_RSDP_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_RSDP,f)
#define ACPI_BERT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_BERT,f)
#define ACPI_BGRT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_BGRT,f)
#define ACPI_BOOT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_BOOT,f)
#define ACPI_CPEP_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_CPEP,f)
#define ACPI_DBG2_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_DBG2,f)
#define ACPI_DBGP_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_DBGP,f)
#define ACPI_DMAR_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_DMAR,f)
#define ACPI_DRTM_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_DRTM,f)
#define ACPI_ECDT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_ECDT,f)
#define ACPI_EINJ_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_EINJ,f)
#define ACPI_ERST_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_ERST,f)
#define ACPI_GTDT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_GTDT,f)
#define ACPI_HEST_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_HEST,f)
#define ACPI_HPET_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_HPET,f)
#define ACPI_HMAT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_HMAT,f)
#define ACPI_IORT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_IORT,f)
#define ACPI_IVRS_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_IVRS,f)
#define ACPI_MADT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_MADT,f)
#define ACPI_MCFG_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_MCFG,f)
#define ACPI_MCHI_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_MCHI,f)
#define ACPI_MPST_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_MPST,f)
#define ACPI_MSCT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_MSCT,f)
#define ACPI_NFIT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_NFIT,f)
#define ACPI_PCCT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_PCCT,f)
#define ACPI_PDTT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_PDTT,f)
#define ACPI_PMTT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_PMTT,f)
#define ACPI_RASF_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_RASF,f)
#define ACPI_S3PT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_S3PT,f)
#define ACPI_SBST_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_SBST,f)
#define ACPI_SDEI_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_SDEI,f)
#define ACPI_SDEV_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_SDEV,f)
#define ACPI_SLIT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_SLIT,f)
#define ACPI_SPCR_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_SPCR,f)
#define ACPI_SPMI_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_SPMI,f)
#define ACPI_SRAT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_SRAT,f)
#define ACPI_STAO_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_STAO,f)
#define ACPI_TCPA_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_TCPA_HDR,f)
#define ACPI_TPM2_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_TPM2,f)
#define ACPI_TPM23_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_TABLE_TPM23,f)
#define ACPI_UEFI_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_UEFI,f)
#define ACPI_WAET_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_WAET,f)
#define ACPI_WDAT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_WDAT,f)
#define ACPI_WDDT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_WDDT,f)
#define ACPI_WDRT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_WDRT,f)
#define ACPI_WPBT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_WPBT,f)
#define ACPI_WSMT_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_WSMT,f)
#define ACPI_XENV_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_XENV,f)

/* Subtables */

#define ACPI_ASF0_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_ASF_INFO,f)
#define ACPI_ASF1_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_ASF_ALERT,f)
#define ACPI_ASF1a_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_ASF_ALERT_DATA,f)
#define ACPI_ASF2_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_ASF_REMOTE,f)
#define ACPI_ASF2a_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_ASF_CONTROL_DATA,f)
#define ACPI_ASF3_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_ASF_RMCP,f)
#define ACPI_ASF4_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_ASF_ADDRESS,f)
#define ACPI_CPEP0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_CPEP_POLLING,f)
#define ACPI_CSRT0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_CSRT_GROUP,f)
#define ACPI_CSRT1_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_CSRT_SHARED_INFO,f)
#define ACPI_CSRT2_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_CSRT_DESCRIPTOR,f)
#define ACPI_DBG20_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_DBG2_DEVICE,f)
#define ACPI_DMARS_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_DMAR_DEVICE_SCOPE,f)
#define ACPI_DMAR0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_DMAR_HARDWARE_UNIT,f)
#define ACPI_DMAR1_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_DMAR_RESERVED_MEMORY,f)
#define ACPI_DMAR2_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_DMAR_ATSR,f)
#define ACPI_DMAR3_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_DMAR_RHSA,f)
#define ACPI_DMAR4_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_DMAR_ANDD,f)
#define ACPI_DRTM0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_DRTM_VTABLE_LIST,f)
#define ACPI_DRTM1_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_DRTM_RESOURCE_LIST,f)
#define ACPI_DRTM1a_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_DRTM_RESOURCE,f)
#define ACPI_DRTM2_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_DRTM_DPS_ID,f)
#define ACPI_EINJ0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_WHEA_HEADER,f)
#define ACPI_ERST0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_WHEA_HEADER,f)
#define ACPI_FPDTH_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_FPDT_HEADER,f)
#define ACPI_FPDT0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_FPDT_BOOT_POINTER,f)
#define ACPI_FPDT1_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_FPDT_S3PT_POINTER,f)
#define ACPI_GTDT0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_GTDT_TIMER_BLOCK,f)
#define ACPI_GTDT0a_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_GTDT_TIMER_ENTRY,f)
#define ACPI_GTDT1_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_GTDT_WATCHDOG,f)
#define ACPI_GTDTH_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_GTDT_HEADER,f)
#define ACPI_GTDT_EL2_OFFSET(f)         (UINT16) ACPI_OFFSET (ACPI_GTDT_EL2,f)
#define ACPI_HEST0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_HEST_IA_MACHINE_CHECK,f)
#define ACPI_HEST1_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_HEST_IA_CORRECTED,f)
#define ACPI_HEST2_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_HEST_IA_NMI,f)
#define ACPI_HEST6_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_HEST_AER_ROOT,f)
#define ACPI_HEST7_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_HEST_AER,f)
#define ACPI_HEST8_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_HEST_AER_BRIDGE,f)
#define ACPI_HEST9_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_HEST_GENERIC,f)
#define ACPI_HEST10_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_HEST_GENERIC_V2,f)
#define ACPI_HEST11_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_HEST_IA_DEFERRED_CHECK,f)
#define ACPI_HESTN_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_HEST_NOTIFY,f)
#define ACPI_HESTB_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_HEST_IA_ERROR_BANK,f)
#define ACPI_HMAT0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_HMAT_PROXIMITY_DOMAIN,f)
#define ACPI_HMAT1_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_HMAT_LOCALITY,f)
#define ACPI_HMAT2_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_HMAT_CACHE,f)
#define ACPI_HMATH_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_HMAT_STRUCTURE,f)
#define ACPI_IORT0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_IORT_ITS_GROUP,f)
#define ACPI_IORT1_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_IORT_NAMED_COMPONENT,f)
#define ACPI_IORT2_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_IORT_ROOT_COMPLEX,f)
#define ACPI_IORT3_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_IORT_SMMU,f)
#define ACPI_IORT3A_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_IORT_SMMU_GSI,f)
#define ACPI_IORT4_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_IORT_SMMU_V3,f)
#define ACPI_IORT5_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_IORT_PMCG,f)
#define ACPI_IORTA_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_IORT_MEMORY_ACCESS,f)
#define ACPI_IORTH_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_IORT_NODE,f)
#define ACPI_IORTM_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_IORT_ID_MAPPING,f)
#define ACPI_IVRSH_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_IVRS_HEADER,f)
#define ACPI_IVRS0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_IVRS_HARDWARE,f)
#define ACPI_IVRS1_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_IVRS_MEMORY,f)
#define ACPI_IVRSD_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_IVRS_DE_HEADER,f)
#define ACPI_IVRS8A_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_IVRS_DEVICE8A,f)
#define ACPI_IVRS8B_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_IVRS_DEVICE8B,f)
#define ACPI_IVRS8C_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_IVRS_DEVICE8C,f)
#define ACPI_LPITH_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_LPIT_HEADER,f)
#define ACPI_LPIT0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_LPIT_NATIVE,f)
#define ACPI_MADT0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_MADT_LOCAL_APIC,f)
#define ACPI_MADT1_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_MADT_IO_APIC,f)
#define ACPI_MADT2_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_MADT_INTERRUPT_OVERRIDE,f)
#define ACPI_MADT3_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_MADT_NMI_SOURCE,f)
#define ACPI_MADT4_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_MADT_LOCAL_APIC_NMI,f)
#define ACPI_MADT5_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_MADT_LOCAL_APIC_OVERRIDE,f)
#define ACPI_MADT6_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_MADT_IO_SAPIC,f)
#define ACPI_MADT7_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_MADT_LOCAL_SAPIC,f)
#define ACPI_MADT8_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_MADT_INTERRUPT_SOURCE,f)
#define ACPI_MADT9_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_MADT_LOCAL_X2APIC,f)
#define ACPI_MADT10_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_MADT_LOCAL_X2APIC_NMI,f)
#define ACPI_MADT11_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_MADT_GENERIC_INTERRUPT,f)
#define ACPI_MADT12_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_MADT_GENERIC_DISTRIBUTOR,f)
#define ACPI_MADT13_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_MADT_GENERIC_MSI_FRAME,f)
#define ACPI_MADT14_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_MADT_GENERIC_REDISTRIBUTOR,f)
#define ACPI_MADT15_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_MADT_GENERIC_TRANSLATOR,f)
#define ACPI_MADTH_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_SUBTABLE_HEADER,f)
#define ACPI_MCFG0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_MCFG_ALLOCATION,f)
#define ACPI_MPST0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_MPST_POWER_NODE,f)
#define ACPI_MPST0A_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_MPST_POWER_STATE,f)
#define ACPI_MPST0B_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_MPST_COMPONENT,f)
#define ACPI_MPST1_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_MPST_DATA_HDR,f)
#define ACPI_MPST2_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_MPST_POWER_DATA,f)
#define ACPI_MSCT0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_MSCT_PROXIMITY,f)
#define ACPI_MTMR0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_MTMR_ENTRY,f)
#define ACPI_NFITH_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_NFIT_HEADER,f)
#define ACPI_NFIT0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_NFIT_SYSTEM_ADDRESS,f)
#define ACPI_NFIT1_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_NFIT_MEMORY_MAP,f)
#define ACPI_NFIT2_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_NFIT_INTERLEAVE,f)
#define ACPI_NFIT3_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_NFIT_SMBIOS,f)
#define ACPI_NFIT4_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_NFIT_CONTROL_REGION,f)
#define ACPI_NFIT5_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_NFIT_DATA_REGION,f)
#define ACPI_NFIT6_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_NFIT_FLUSH_ADDRESS,f)
#define ACPI_NFIT7_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_NFIT_CAPABILITIES,f)
#define ACPI_PCCT0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_PCCT_SUBSPACE,f)
#define ACPI_PCCT1_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_PCCT_HW_REDUCED,f)
#define ACPI_PCCT2_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_PCCT_HW_REDUCED_TYPE2,f)
#define ACPI_PCCT3_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_PCCT_EXT_PCC_MASTER,f)
#define ACPI_PCCT4_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_PCCT_EXT_PCC_SLAVE,f)
#define ACPI_PDTT0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_PDTT_CHANNEL,f)
#define ACPI_PMTT0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_PMTT_SOCKET,f)
#define ACPI_PMTT1_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_PMTT_CONTROLLER,f)
#define ACPI_PMTT1A_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_PMTT_DOMAIN,f)
#define ACPI_PMTT2_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_PMTT_PHYSICAL_COMPONENT,f)
#define ACPI_PMTTH_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_PMTT_HEADER,f)
#define ACPI_PPTTH_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_SUBTABLE_HEADER,f)
#define ACPI_PPTT0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_PPTT_PROCESSOR,f)
#define ACPI_PPTT1_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_PPTT_CACHE,f)
#define ACPI_PPTT2_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_PPTT_ID,f)
#define ACPI_S3PTH_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_FPDT_HEADER,f)
#define ACPI_S3PT0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_S3PT_RESUME,f)
#define ACPI_S3PT1_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_S3PT_SUSPEND,f)
#define ACPI_SDEVH_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_SDEV_HEADER,f)
#define ACPI_SDEV0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_SDEV_NAMESPACE,f)
#define ACPI_SDEV1_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_SDEV_PCIE,f)
#define ACPI_SDEV1A_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_SDEV_PCIE_PATH,f)
#define ACPI_SLIC_OFFSET(f)             (UINT16) ACPI_OFFSET (ACPI_TABLE_SLIC,f)
#define ACPI_SRATH_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_SUBTABLE_HEADER,f)
#define ACPI_SRAT0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_SRAT_CPU_AFFINITY,f)
#define ACPI_SRAT1_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_SRAT_MEM_AFFINITY,f)
#define ACPI_SRAT2_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_SRAT_X2APIC_CPU_AFFINITY,f)
#define ACPI_SRAT3_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_SRAT_GICC_AFFINITY,f)
#define ACPI_SRAT4_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_SRAT_GIC_ITS_AFFINITY,f)
#define ACPI_SRAT5_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_SRAT_GENERIC_AFFINITY,f)
#define ACPI_TCPA_CLIENT_OFFSET(f)      (UINT16) ACPI_OFFSET (ACPI_TABLE_TCPA_CLIENT,f)
#define ACPI_TCPA_SERVER_OFFSET(f)      (UINT16) ACPI_OFFSET (ACPI_TABLE_TCPA_SERVER,f)
#define ACPI_TPM2A_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_TPM2_TRAILER,f)
#define ACPI_TPM211_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_TPM2_ARM_SMC,f)
#define ACPI_TPM23A_OFFSET(f)           (UINT16) ACPI_OFFSET (ACPI_TPM23_TRAILER,f)
#define ACPI_VRTC0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_VRTC_ENTRY,f)
#define ACPI_WDAT0_OFFSET(f)            (UINT16) ACPI_OFFSET (ACPI_WDAT_ENTRY,f)

/*
 * Simplify access to flag fields by breaking them up into bytes
 */
#define ACPI_FLAG_OFFSET(d,f,o)         (UINT16) (ACPI_OFFSET (d,f) + o)

/* Flags */

#define ACPI_BGRT_FLAG_OFFSET(f,o)      ACPI_FLAG_OFFSET (ACPI_TABLE_BGRT,f,o)
#define ACPI_DRTM_FLAG_OFFSET(f,o)      ACPI_FLAG_OFFSET (ACPI_TABLE_DRTM,f,o)
#define ACPI_DRTM1a_FLAG_OFFSET(f,o)    ACPI_FLAG_OFFSET (ACPI_DRTM_RESOURCE,f,o)
#define ACPI_FADT_FLAG_OFFSET(f,o)      ACPI_FLAG_OFFSET (ACPI_TABLE_FADT,f,o)
#define ACPI_FACS_FLAG_OFFSET(f,o)      ACPI_FLAG_OFFSET (ACPI_TABLE_FACS,f,o)
#define ACPI_HPET_FLAG_OFFSET(f,o)      ACPI_FLAG_OFFSET (ACPI_TABLE_HPET,f,o)
#define ACPI_PPTT0_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_PPTT_PROCESSOR,f,o)
#define ACPI_PPTT1_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_PPTT_CACHE,f,o)
#define ACPI_SRAT0_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_SRAT_CPU_AFFINITY,f,o)
#define ACPI_SRAT1_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_SRAT_MEM_AFFINITY,f,o)
#define ACPI_SRAT2_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_SRAT_X2APIC_CPU_AFFINITY,f,o)
#define ACPI_SRAT3_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_SRAT_GICC_AFFINITY,f,o)
#define ACPI_SRAT5_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_SRAT_GENERIC_AFFINITY,f,o)
#define ACPI_GTDT_FLAG_OFFSET(f,o)      ACPI_FLAG_OFFSET (ACPI_TABLE_GTDT,f,o)
#define ACPI_GTDT0a_FLAG_OFFSET(f,o)    ACPI_FLAG_OFFSET (ACPI_GTDT_TIMER_ENTRY,f,o)
#define ACPI_GTDT1_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_GTDT_WATCHDOG,f,o)
#define ACPI_HMAT0_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_HMAT_PROXIMITY_DOMAIN,f,o)
#define ACPI_HMAT1_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_HMAT_LOCALITY,f,o)
#define ACPI_HMAT2_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_HMAT_CACHE,f,o)
#define ACPI_IORT3_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_IORT_SMMU,f,o)
#define ACPI_IORT3a_FLAG_OFFSET(f,o)    ACPI_FLAG_OFFSET (ACPI_IORT_SMMU_GSI,f,o)
#define ACPI_IORT4_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_IORT_SMMU_V3,f,o)
#define ACPI_IORTA_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_IORT_MEMORY_ACCESS,f,o)
#define ACPI_IORTM_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_IORT_ID_MAPPING,f,o)
#define ACPI_LPITH_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_LPIT_HEADER,f,o)
#define ACPI_MADT_FLAG_OFFSET(f,o)      ACPI_FLAG_OFFSET (ACPI_TABLE_MADT,f,o)
#define ACPI_MADT0_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_MADT_LOCAL_APIC,f,o)
#define ACPI_MADT2_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_MADT_INTERRUPT_OVERRIDE,f,o)
#define ACPI_MADT3_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_MADT_NMI_SOURCE,f,o)
#define ACPI_MADT4_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_MADT_LOCAL_APIC_NMI,f,o)
#define ACPI_MADT7_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_MADT_LOCAL_SAPIC,f,o)
#define ACPI_MADT8_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_MADT_INTERRUPT_SOURCE,f,o)
#define ACPI_MADT9_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_MADT_LOCAL_X2APIC,f,o)
#define ACPI_MADT10_FLAG_OFFSET(f,o)    ACPI_FLAG_OFFSET (ACPI_MADT_LOCAL_X2APIC_NMI,f,o)
#define ACPI_MADT11_FLAG_OFFSET(f,o)    ACPI_FLAG_OFFSET (ACPI_MADT_GENERIC_INTERRUPT,f,o)
#define ACPI_MADT13_FLAG_OFFSET(f,o)    ACPI_FLAG_OFFSET (ACPI_MADT_GENERIC_MSI_FRAME,f,o)
#define ACPI_MPST0_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_MPST_POWER_NODE,f,o)
#define ACPI_MPST2_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_MPST_POWER_DATA,f,o)
#define ACPI_NFIT0_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_NFIT_SYSTEM_ADDRESS,f,o)
#define ACPI_NFIT1_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_NFIT_MEMORY_MAP,f,o)
#define ACPI_NFIT4_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_NFIT_CONTROL_REGION,f,o)
#define ACPI_NFIT7_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_NFIT_CAPABILITIES,f,o)
#define ACPI_PCCT_FLAG_OFFSET(f,o)      ACPI_FLAG_OFFSET (ACPI_TABLE_PCCT,f,o)
#define ACPI_PCCT1_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_PCCT_HW_REDUCED,f,o)
#define ACPI_PCCT2_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_PCCT_HW_REDUCED_TYPE2,f,o)
#define ACPI_PCCT3_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_PCCT_EXT_PCC_MASTER,f,o)
#define ACPI_PCCT4_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_PCCT_EXT_PCC_SLAVE,f,o)
#define ACPI_PDTT0_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_PDTT_CHANNEL,f,o)
#define ACPI_PMTTH_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_PMTT_HEADER,f,o)
#define ACPI_SDEVH_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_SDEV_HEADER,f,o)
#define ACPI_WDDT_FLAG_OFFSET(f,o)      ACPI_FLAG_OFFSET (ACPI_TABLE_WDDT,f,o)
#define ACPI_WSMT_FLAG_OFFSET(f,o)      ACPI_FLAG_OFFSET (ACPI_TABLE_WSMT,f,o)
#define ACPI_EINJ0_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_WHEA_HEADER,f,o)
#define ACPI_ERST0_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_WHEA_HEADER,f,o)
#define ACPI_HEST0_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_HEST_IA_MACHINE_CHECK,f,o)
#define ACPI_HEST1_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_HEST_IA_CORRECTED,f,o)
#define ACPI_HEST6_FLAG_OFFSET(f,o)     ACPI_FLAG_OFFSET (ACPI_HEST_AER_ROOT,f,o)
#define ACPI_HEST11_FLAG_OFFSET(f,o)    ACPI_FLAG_OFFSET (ACPI_HEST_IA_DEFERRED_CHECK,f,o)

/*
 * Required terminator for all tables below
 */
#define ACPI_DMT_TERMINATOR             {ACPI_DMT_EXIT, 0, NULL, 0}
#define ACPI_DMT_NEW_LINE               {ACPI_DMT_EXTRA_TEXT, 0, "\n", 0}
