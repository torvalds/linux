/******************************************************************************
 *
 * Module Name: aslhelp - iASL help screens
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

#include <contrib/dev/acpica/compiler/aslcompiler.h>
#include <contrib/dev/acpica/include/acapps.h>

#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslhelp")


/*******************************************************************************
 *
 * FUNCTION:    Usage
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display option help message.
 *              Optional items in square brackets.
 *
 ******************************************************************************/

void
Usage (
    void)
{
    printf (ACPI_COMMON_SIGNON (ASL_COMPILER_NAME));
    printf ("%s\n\n", ASL_COMPLIANCE);
    ACPI_USAGE_HEADER ("iasl [Options] [Files]");

    printf ("\nGeneral:\n");
    ACPI_OPTION ("-@  <file>",      "Specify command file");
    ACPI_OPTION ("-I  <dir>",       "Specify additional include directory");
    ACPI_OPTION ("-p  <prefix>",    "Specify path/filename prefix for all output files");
    ACPI_OPTION ("-v",              "Display compiler version");
    ACPI_OPTION ("-vo",             "Enable optimization comments");
    ACPI_OPTION ("-vs",             "Disable signon");

    printf ("\nHelp:\n");
    ACPI_OPTION ("-h",              "This message");
    ACPI_OPTION ("-hc",             "Display operators allowed in constant expressions");
    ACPI_OPTION ("-hd",             "Info for obtaining and disassembling binary ACPI tables");
    ACPI_OPTION ("-hf",             "Display help for output filename generation");
    ACPI_OPTION ("-hr",             "Display ACPI reserved method names");
    ACPI_OPTION ("-ht",             "Display currently supported ACPI table names");

    printf ("\nPreprocessor:\n");
    ACPI_OPTION ("-D <symbol>",     "Define symbol for preprocessor use");
    ACPI_OPTION ("-li",             "Create preprocessed output file (*.i)");
    ACPI_OPTION ("-P",              "Preprocess only and create preprocessor output file (*.i)");
    ACPI_OPTION ("-Pn",             "Disable preprocessor");

    printf ("\nErrors, Warnings, and Remarks:\n");
    ACPI_OPTION ("-va",             "Disable all errors/warnings/remarks");
    ACPI_OPTION ("-ve",             "Report only errors (ignore warnings and remarks)");
    ACPI_OPTION ("-vi",             "Less verbose errors and warnings for use with IDEs");
    ACPI_OPTION ("-vr",             "Disable remarks");
    ACPI_OPTION ("-vw <messageid>", "Ignore specific error, warning or remark");
    ACPI_OPTION ("-vx <messageid>", "Expect a specific warning, remark, or error");
    ACPI_OPTION ("-w <1|2|3>",      "Set warning reporting level");
    ACPI_OPTION ("-we",             "Report warnings as errors");
    ACPI_OPTION ("-ww <messageid>", "Report specific warning or remark as error");

    printf ("\nAML Bytecode Generation (*.aml):\n");
    ACPI_OPTION ("-oa",             "Disable all optimizations (compatibility mode)");
    ACPI_OPTION ("-of",             "Disable constant folding");
    ACPI_OPTION ("-oi",             "Disable integer optimization to Zero/One/Ones");
    ACPI_OPTION ("-on",             "Disable named reference string optimization");
    ACPI_OPTION ("-ot",             "Disable typechecking");
    ACPI_OPTION ("-cr",             "Disable Resource Descriptor error checking");
    ACPI_OPTION ("-in",             "Ignore NoOp operators");
    ACPI_OPTION ("-r <revision>",   "Override table header Revision (1-255)");

    printf ("\nListings:\n");
    ACPI_OPTION ("-l",              "Create mixed listing file (ASL source and AML) (*.lst)");
    ACPI_OPTION ("-lm",             "Create hardware summary map file (*.map)");
    ACPI_OPTION ("-ln",             "Create namespace file (*.nsp)");
    ACPI_OPTION ("-ls",             "Create combined source file (expanded includes) (*.src)");
    ACPI_OPTION ("-lx",             "Create cross-reference file (*.xrf)");

    printf ("\nFirmware Support - C Text Output:\n");
    ACPI_OPTION ("-tc",             "Create hex AML table in C (*.hex)");
    ACPI_OPTION ("-sc",             "Create named hex AML arrays in C (*.c)");
    ACPI_OPTION ("-ic",             "Create include file in C for -sc symbols (*.h)");
    ACPI_OPTION ("-so",             "Create namespace AML offset table in C (*.offset.h)");

    printf ("\nFirmware Support - Assembler Text Output:\n");
    ACPI_OPTION ("-ta",             "Create hex AML table in assembler (*.hex)");
    ACPI_OPTION ("-sa",             "Create named hex AML arrays in assembler (*.asm)");
    ACPI_OPTION ("-ia",             "Create include file in assembler for -sa symbols (*.inc)");

    printf ("\nFirmware Support - ASL Text Output:\n");
    ACPI_OPTION ("-ts",             "Create hex AML table in ASL (Buffer object) (*.hex)");

    printf ("\nLegacy-ASL to ASL+ Converter:\n");
    ACPI_OPTION ("-ca <file>",      "Convert legacy-ASL source file to new ASL+ file");
    ACPI_OPTION ("",                "  (Original comments are passed through to ASL+ file)");

    printf ("\nData Table Compiler:\n");
    ACPI_OPTION ("-G",              "Compile custom table that contains generic operators");
    ACPI_OPTION ("-T <sig list>|ALL",   "Create ACPI table template/example files");
    ACPI_OPTION ("-T <count>",      "Emit DSDT and <count> SSDTs to same file");
    ACPI_OPTION ("-vt",             "Create verbose template files (full disassembly)");

    printf ("\nAML Disassembler:\n");
    ACPI_OPTION ("-d  <f1 f2 ...>", "Disassemble or decode binary ACPI tables to file (*.dsl)");
    ACPI_OPTION ("",                "  (Optional, file type is automatically detected)");
    ACPI_OPTION ("-da <f1 f2 ...>", "Disassemble multiple tables from single namespace");
    ACPI_OPTION ("-db",             "Do not translate Buffers to Resource Templates");
    ACPI_OPTION ("-dc <f1 f2 ...>", "Disassemble AML and immediately compile it");
    ACPI_OPTION ("",                "  (Obtain DSDT from current system if no input file)");
    ACPI_OPTION ("-df",             "Force disassembler to assume table contains valid AML");
    ACPI_OPTION ("-dl",             "Emit legacy ASL code only (no C-style operators)");
    ACPI_OPTION ("-e  <f1 f2 ...>", "Include ACPI table(s) for external symbol resolution");
    ACPI_OPTION ("-fe <file>",      "Specify external symbol declaration file");
    ACPI_OPTION ("-in",             "Ignore NoOp opcodes");
    ACPI_OPTION ("-l",              "Disassemble to mixed ASL and AML code");
    ACPI_OPTION ("-vt",             "Dump binary table data in hex format within output file");

    printf ("\nDebug Options:\n");
    ACPI_OPTION ("-bc",             "Create converter debug file (*.cdb)");
    ACPI_OPTION ("-bf",             "Create debug file (full output) (*.txt)");
    ACPI_OPTION ("-bs",             "Create debug file (parse tree only) (*.txt)");
    ACPI_OPTION ("-bp <depth>",     "Prune ASL parse tree");
    ACPI_OPTION ("-bt <type>",      "Object type to be pruned from the parse tree");
    ACPI_OPTION ("-f",              "Ignore errors, force creation of AML output file(s)");
    ACPI_OPTION ("-m <size>",       "Set internal line buffer size (in Kbytes)");
    ACPI_OPTION ("-n",              "Parse only, no output generation");
    ACPI_OPTION ("-oc",             "Display compile times and statistics");
    ACPI_OPTION ("-x <level>",      "Set debug level for trace output");
    ACPI_OPTION ("-z",              "Do not insert new compiler ID for DataTables");
}


/*******************************************************************************
 *
 * FUNCTION:    FilenameHelp
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display help message for output filename generation
 *
 ******************************************************************************/

void
AslFilenameHelp (
    void)
{

    printf ("\nAML output filename generation:\n");
    printf ("  Output filenames are generated by appending an extension to a common\n");
    printf ("  filename prefix. The filename prefix is obtained via one of the\n");
    printf ("  following methods (in priority order):\n");
    printf ("    1) The -p option specifies the prefix\n");
    printf ("    2) The prefix of the AMLFileName in the ASL Definition Block\n");
    printf ("    3) The prefix of the input filename\n");
    printf ("\n");
}

/*******************************************************************************
 *
 * FUNCTION:    AslDisassemblyHelp
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display help message for obtaining and disassembling AML/ASL
 *              files.
 *
 ******************************************************************************/

void
AslDisassemblyHelp (
    void)
{

    printf ("\nObtaining binary ACPI tables and disassembling to ASL source code.\n\n");
    printf ("Use the following ACPICA toolchain:\n");
    printf ("  AcpiDump: Dump all ACPI tables to a hex ascii file\n");
    printf ("  AcpiXtract: Extract one or more binary ACPI tables from AcpiDump output\n");
    printf ("  iASL -d <file>: Disassemble a binary ACPI table to ASL source code\n");
    printf ("\n");
}
