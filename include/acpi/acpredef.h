/******************************************************************************
 *
 * Name: acpredef - Information table for ACPI predefined methods and objects
 *              $Revision: 1.1 $
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2008, Intel Corp.
 * All rights reserved.
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
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#ifndef __ACPREDEF_H__
#define __ACPREDEF_H__

/******************************************************************************
 *
 * Return Package types
 *
 * 1) PTYPE1 packages do not contain sub-packages.
 *
 * ACPI_PTYPE1_FIXED: Fixed length, 1 or 2 object types:
 *     object type
 *     count
 *     object type
 *     count
 *
 * ACPI_PTYPE1_VAR: Variable length:
 *    object type (Int/Buf/Ref)
 *
 * ACPI_PTYPE1_OPTION: Package has some required and some optional elements:
 *      Used for _PRW
 *
 *
 * 2) PTYPE2 packages contain a variable number of sub-packages. Each of the
 *    different types describe the contents of each of the sub-packages.
 *
 * ACPI_PTYPE2: Each subpackage contains 1 or 2 object types:
 *     object type
 *     count
 *     object type
 *     count
 *
 * ACPI_PTYPE2_COUNT: Each subpackage has a count as first element:
 *     object type
 *
 * ACPI_PTYPE2_PKG_COUNT: Count of subpackages at start, 1 or 2 object types:
 *     object type
 *     count
 *     object type
 *     count
 *
 * ACPI_PTYPE2_FIXED: Each subpackage is of fixed length:
 *      Used for _PRT
 *
 * ACPI_PTYPE2_MIN: Each subpackage has a variable but minimum length
 *      Used for _HPX
 *
 *****************************************************************************/

enum acpi_return_package_types {
	ACPI_PTYPE1_FIXED = 1,
	ACPI_PTYPE1_VAR = 2,
	ACPI_PTYPE1_OPTION = 3,
	ACPI_PTYPE2 = 4,
	ACPI_PTYPE2_COUNT = 5,
	ACPI_PTYPE2_PKG_COUNT = 6,
	ACPI_PTYPE2_FIXED = 7,
	ACPI_PTYPE2_MIN = 8
};

/*
 * Predefined method/object information table.
 *
 * These are the names that can actually be evaluated via acpi_evaluate_object.
 * Not present in this table are the following:
 *
 *      1) Predefined/Reserved names that are never evaluated via acpi_evaluate_object:
 *          _Lxx and _Exx GPE methods
 *          _Qxx EC methods
 *          _T_x compiler temporary variables
 *
 *      2) Predefined names that never actually exist within the AML code:
 *          Predefined resource descriptor field names
 *
 *      3) Predefined names that are implemented within ACPICA:
 *          _OSI
 *
 *      4) Some predefined names that are not documented within the ACPI spec.
 *          _WDG, _WED
 *
 * The main entries in the table each contain the following items:
 *
 * Name                 - The ACPI reserved name
 * param_count          - Number of arguments to the method
 * expected_btypes      - Allowed type(s) for the return value.
 *                        0 means that no return value is expected.
 *
 * For methods that return packages, the next entry in the table contains
 * information about the expected structure of the package. This information
 * is saved here (rather than in a separate table) in order to minimize the
 * overall size of the stored data.
 */
static const union acpi_predefined_info predefined_names[] = {
	{.info = {"_AC0", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_AC1", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_AC2", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_AC3", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_AC4", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_AC5", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_AC6", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_AC7", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_AC8", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_AC9", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_ADR", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_AL0", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_REFERENCE, 0, 0, 0, 0}},	/* variable (Refs) */
	{.info = {"_AL1", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_REFERENCE, 0, 0, 0, 0}},	/* variable (Refs) */
	{.info = {"_AL2", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_REFERENCE, 0, 0, 0, 0}},	/* variable (Refs) */
	{.info = {"_AL3", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_REFERENCE, 0, 0, 0, 0}},	/* variable (Refs) */
	{.info = {"_AL4", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_REFERENCE, 0, 0, 0, 0}},	/* variable (Refs) */
	{.info = {"_AL5", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_REFERENCE, 0, 0, 0, 0}},	/* variable (Refs) */
	{.info = {"_AL6", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_REFERENCE, 0, 0, 0, 0}},	/* variable (Refs) */
	{.info = {"_AL7", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_REFERENCE, 0, 0, 0, 0}},	/* variable (Refs) */
	{.info = {"_AL8", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_REFERENCE, 0, 0, 0, 0}},	/* variable (Refs) */
	{.info = {"_AL9", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_REFERENCE, 0, 0, 0, 0}},	/* variable (Refs) */
	{.info = {"_ALC", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_ALI", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_ALP", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_ALR", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE2, ACPI_RTYPE_INTEGER, 2, 0, 0, 0}},	/* variable (Pkgs) each 2 (Ints) */
	{.info = {"_ALT", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_BBN", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_BCL", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_INTEGER, 0, 0, 0, 0}},	/* variable (Ints) */
	{.info = {"_BCM", 1, 0}},
	{.info = {"_BDN", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_BFS", 1, 0}},
	{.info = {"_BIF", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_FIXED, ACPI_RTYPE_INTEGER,
					  9,
					  ACPI_RTYPE_STRING | ACPI_RTYPE_BUFFER, 4, 0}},	/* fixed (9 Int),(4 Str) */
	{.info = {"_BLT", 3, 0}},
	{.info = {"_BMC", 1, 0}},
	{.info = {"_BMD", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_FIXED, ACPI_RTYPE_INTEGER, 5, 0, 0, 0}},	/* fixed (5 Int) */
	{.info = {"_BQC", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_BST", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_FIXED, ACPI_RTYPE_INTEGER, 4, 0, 0, 0}},	/* fixed (4 Int) */
	{.info = {"_BTM", 1, ACPI_RTYPE_INTEGER}},
	{.info = {"_BTP", 1, 0}},
	{.info = {"_CBA", 0, ACPI_RTYPE_INTEGER}},	/* see PCI firmware spec 3.0 */
	{.info = {"_CID", 0,
	 ACPI_RTYPE_INTEGER | ACPI_RTYPE_STRING | ACPI_RTYPE_PACKAGE}},
	{.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_INTEGER | ACPI_RTYPE_STRING, 0, 0, 0, 0}},	/* variable (Ints/Strs) */
	{.info = {"_CRS", 0, ACPI_RTYPE_BUFFER}},
	{.info = {"_CRT", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_CSD", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE2_COUNT, ACPI_RTYPE_INTEGER, 0, 0, 0, 0}},	/* variable (1 Int(n), n-1 Int) */
	{.info = {"_CST", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE2_PKG_COUNT,
					  ACPI_RTYPE_BUFFER, 1,
					  ACPI_RTYPE_INTEGER, 3, 0}},	/* variable (1 Int(n), n Pkg (1 Buf/3 Int) */
	{.info = {"_DCK", 1, ACPI_RTYPE_INTEGER}},
	{.info = {"_DCS", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_DDC", 1, ACPI_RTYPE_INTEGER | ACPI_RTYPE_BUFFER}},
	{.info = {"_DDN", 0, ACPI_RTYPE_STRING}},
	{.info = {"_DGS", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_DIS", 0, 0}},
	{.info = {"_DMA", 0, ACPI_RTYPE_BUFFER}},
	{.info = {"_DOD", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_INTEGER, 0, 0, 0, 0}},	/* variable (Ints) */
	{.info = {"_DOS", 1, 0}},
	{.info = {"_DSM", 4, ACPI_RTYPE_ALL}},	/* Must return a type, but it can be of any type */
	{.info = {"_DSS", 1, 0}},
	{.info = {"_DSW", 3, 0}},
	{.info = {"_EC_", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_EDL", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_REFERENCE, 0, 0, 0, 0}},	/* variable (Refs) */
	{.info = {"_EJ0", 1, 0}},
	{.info = {"_EJ1", 1, 0}},
	{.info = {"_EJ2", 1, 0}},
	{.info = {"_EJ3", 1, 0}},
	{.info = {"_EJ4", 1, 0}},
	{.info = {"_EJD", 0, ACPI_RTYPE_STRING}},
	{.info = {"_FDE", 0, ACPI_RTYPE_BUFFER}},
	{.info = {"_FDI", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_FIXED, ACPI_RTYPE_INTEGER, 16, 0, 0, 0}},	/* fixed (16 Int) */
	{.info = {"_FDM", 1, 0}},
	{.info = {"_FIX", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_INTEGER, 0, 0, 0, 0}},	/* variable (Ints) */
	{.info = {"_GLK", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_GPD", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_GPE", 0, ACPI_RTYPE_INTEGER}},	/* _GPE method, not _GPE scope */
	{.info = {"_GSB", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_GTF", 0, ACPI_RTYPE_BUFFER}},
	{.info = {"_GTM", 0, ACPI_RTYPE_BUFFER}},
	{.info = {"_GTS", 1, 0}},
	{.info = {"_HID", 0, ACPI_RTYPE_INTEGER | ACPI_RTYPE_STRING}},
	{.info = {"_HOT", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_HPP", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_FIXED, ACPI_RTYPE_INTEGER, 4, 0, 0, 0}},	/* fixed (4 Int) */

	/*
	 * For _HPX, a single package is returned, containing a variable number of sub-packages.
	 * Each sub-package contains a PCI record setting. There are several different type of
	 * record settings, of different lengths, but all elements of all settings are Integers.
	 */
	{.info = {"_HPX", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE2_MIN, ACPI_RTYPE_INTEGER, 5, 0, 0, 0}},	/* variable (Pkgs) each (var Ints) */
	{.info = {"_IFT", 0, ACPI_RTYPE_INTEGER}},	/* see IPMI spec */
	{.info = {"_INI", 0, 0}},
	{.info = {"_IRC", 0, 0}},
	{.info = {"_LCK", 1, 0}},
	{.info = {"_LID", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_MAT", 0, ACPI_RTYPE_BUFFER}},
	{.info = {"_MLS", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE2, ACPI_RTYPE_STRING, 2, 0, 0, 0}},	/* variable (Pkgs) each (2 Str) */
	{.info = {"_MSG", 1, 0}},
	{.info = {"_OFF", 0, 0}},
	{.info = {"_ON_", 0, 0}},
	{.info = {"_OS_", 0, ACPI_RTYPE_STRING}},
	{.info = {"_OSC", 4, ACPI_RTYPE_BUFFER}},
	{.info = {"_OST", 3, 0}},
	{.info = {"_PCL", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_REFERENCE, 0, 0, 0, 0}},	/* variable (Refs) */
	{.info = {"_PCT", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_FIXED, ACPI_RTYPE_BUFFER, 2, 0, 0, 0}},	/* fixed (2 Buf) */
	{.info = {"_PDC", 1, 0}},
	{.info = {"_PIC", 1, 0}},
	{.info = {"_PLD", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_BUFFER, 0, 0, 0, 0}},	/* variable (Bufs) */
	{.info = {"_PPC", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_PPE", 0, ACPI_RTYPE_INTEGER}},	/* see dig64 spec */
	{.info = {"_PR0", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_REFERENCE, 0, 0, 0, 0}},	/* variable (Refs) */
	{.info = {"_PR1", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_REFERENCE, 0, 0, 0, 0}},	/* variable (Refs) */
	{.info = {"_PR2", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_REFERENCE, 0, 0, 0, 0}},	/* variable (Refs) */
	{.info = {"_PRS", 0, ACPI_RTYPE_BUFFER}},

	/*
	 * For _PRT, many BIOSs reverse the 2nd and 3rd Package elements. This bug is so prevalent that there
	 * is code in the ACPICA Resource Manager to detect this and switch them back. For now, do not allow
	 * and issue a warning. To allow this and eliminate the warning, add the ACPI_RTYPE_REFERENCE
	 * type to the 2nd element (index 1) in the statement below.
	 */
	{.info = {"_PRT", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE2_FIXED, 4,
					  ACPI_RTYPE_INTEGER,
					  ACPI_RTYPE_INTEGER,
					  ACPI_RTYPE_INTEGER | ACPI_RTYPE_REFERENCE, ACPI_RTYPE_INTEGER}},	/* variable (Pkgs) each (4): Int,Int,Int/Ref,Int */

	{.info = {"_PRW", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_OPTION, 2,
					  ACPI_RTYPE_INTEGER |
					  ACPI_RTYPE_PACKAGE,
					  ACPI_RTYPE_INTEGER, ACPI_RTYPE_REFERENCE, 0}},	/* variable (Pkgs) each: Pkg/Int,Int,[variable Refs] (Pkg is Ref/Int) */

	{.info = {"_PS0", 0, 0}},
	{.info = {"_PS1", 0, 0}},
	{.info = {"_PS2", 0, 0}},
	{.info = {"_PS3", 0, 0}},
	{.info = {"_PSC", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_PSD", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE2_COUNT, ACPI_RTYPE_INTEGER, 0, 0, 0, 0}},	/* variable (Pkgs) each (5 Int) with count */
	{.info = {"_PSL", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_REFERENCE, 0, 0, 0, 0}},	/* variable (Refs) */
	{.info = {"_PSR", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_PSS", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE2, ACPI_RTYPE_INTEGER, 6, 0, 0, 0}},	/* variable (Pkgs) each (6 Int) */
	{.info = {"_PSV", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_PSW", 1, 0}},
	{.info = {"_PTC", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_FIXED, ACPI_RTYPE_BUFFER, 2, 0, 0, 0}},	/* fixed (2 Buf) */
	{.info = {"_PTS", 1, 0}},
	{.info = {"_PXM", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_REG", 2, 0}},
	{.info = {"_REV", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_RMV", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_ROM", 2, ACPI_RTYPE_BUFFER}},
	{.info = {"_RTV", 0, ACPI_RTYPE_INTEGER}},

	/*
	 * For _S0_ through _S5_, the ACPI spec defines a return Package containing 1 Integer,
	 * but most DSDTs have it wrong - 2,3, or 4 integers. Allow this by making the objects "variable length",
	 * but all elements must be Integers.
	 */
	{.info = {"_S0_", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_INTEGER, 1, 0, 0, 0}},	/* fixed (1 Int) */
	{.info = {"_S1_", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_INTEGER, 1, 0, 0, 0}},	/* fixed (1 Int) */
	{.info = {"_S2_", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_INTEGER, 1, 0, 0, 0}},	/* fixed (1 Int) */
	{.info = {"_S3_", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_INTEGER, 1, 0, 0, 0}},	/* fixed (1 Int) */
	{.info = {"_S4_", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_INTEGER, 1, 0, 0, 0}},	/* fixed (1 Int) */
	{.info = {"_S5_", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_INTEGER, 1, 0, 0, 0}},	/* fixed (1 Int) */

	{.info = {"_S1D", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_S2D", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_S3D", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_S4D", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_S0W", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_S1W", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_S2W", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_S3W", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_S4W", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_SBS", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_SCP", 0x13, 0}},	/* Acpi 1.0 allowed 1 arg. Acpi 3.0 expanded to 3 args. Allow both. */
	/* Note: the 3-arg definition may be removed for ACPI 4.0 */
	{.info = {"_SDD", 1, 0}},
	{.info = {"_SEG", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_SLI", 0, ACPI_RTYPE_BUFFER}},
	{.info = {"_SPD", 1, ACPI_RTYPE_INTEGER}},
	{.info = {"_SRS", 1, 0}},
	{.info = {"_SRV", 0, ACPI_RTYPE_INTEGER}},	/* see IPMI spec */
	{.info = {"_SST", 1, 0}},
	{.info = {"_STA", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_STM", 3, 0}},
	{.info = {"_STR", 0, ACPI_RTYPE_BUFFER}},
	{.info = {"_SUN", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_SWS", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_TC1", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_TC2", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_TMP", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_TPC", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_TPT", 1, 0}},
	{.info = {"_TRT", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE2, ACPI_RTYPE_REFERENCE, 2,
					  ACPI_RTYPE_INTEGER, 6, 0}},	/* variable (Pkgs) each 2_ref/6_int */
	{.info = {"_TSD", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE2_COUNT, ACPI_RTYPE_INTEGER, 5, 0, 0, 0}},	/* variable (Pkgs) each 5_int with count */
	{.info = {"_TSP", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_TSS", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE2, ACPI_RTYPE_INTEGER, 5, 0, 0, 0}},	/* variable (Pkgs) each 5_int */
	{.info = {"_TST", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_TTS", 1, 0}},
	{.info = {"_TZD", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_VAR, ACPI_RTYPE_REFERENCE, 0, 0, 0, 0}},	/* variable (Refs) */
	{.info = {"_TZM", 0, ACPI_RTYPE_REFERENCE}},
	{.info = {"_TZP", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_UID", 0, ACPI_RTYPE_INTEGER | ACPI_RTYPE_STRING}},
	{.info = {"_UPC", 0, ACPI_RTYPE_PACKAGE}}, {.ret_info = {ACPI_PTYPE1_FIXED, ACPI_RTYPE_INTEGER, 4, 0, 0, 0}},	/* fixed (4 Int) */
	{.info = {"_UPD", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_UPP", 0, ACPI_RTYPE_INTEGER}},
	{.info = {"_VPO", 0, ACPI_RTYPE_INTEGER}},

	/* Acpi 1.0 defined _WAK with no return value. Later, it was changed to return a package */

	{.info = {"_WAK", 1, ACPI_RTYPE_NONE | ACPI_RTYPE_INTEGER | ACPI_RTYPE_PACKAGE}},
	{.ret_info = {ACPI_PTYPE1_FIXED, ACPI_RTYPE_INTEGER, 2, 0, 0, 0}},	/* fixed (2 Int), but is optional */
	{.ret_info = {0, 0, 0, 0, 0, 0}}	/* Table terminator */
};

#if 0
	/* Not implemented */

{
"_WDG", 0, ACPI_RTYPE_BUFFER},	/* MS Extension */

{
"_WED", 1, ACPI_RTYPE_PACKAGE},	/* MS Extension */

    /* This is an internally implemented control method, no need to check */
{
"_OSI", 1, ACPI_RTYPE_INTEGER},

    /* TBD: */
    _PRT - currently ignore reversed entries.attempt to fix here ?
    think about code that attempts to fix package elements like _BIF, etc.
#endif
#endif
