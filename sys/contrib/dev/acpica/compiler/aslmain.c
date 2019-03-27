/******************************************************************************
 *
 * Module Name: aslmain - compiler main and utilities
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

#define _DECLARE_GLOBALS

#include <contrib/dev/acpica/compiler/aslcompiler.h>
#include <contrib/dev/acpica/include/acapps.h>
#include <contrib/dev/acpica/include/acdisasm.h>
#include <signal.h>

#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslmain")

/*
 * Main routine for the iASL compiler.
 *
 * Portability note: The compiler depends upon the host for command-line
 * wildcard support - it is not implemented locally. For example:
 *
 * Linux/Unix systems: Shell expands wildcards automatically.
 *
 * Windows: The setargv.obj module must be linked in to automatically
 * expand wildcards.
 */

/* Local prototypes */

static void ACPI_SYSTEM_XFACE
AslSignalHandler (
    int                     Sig);

static void
AslInitialize (
    void);


/*******************************************************************************
 *
 * FUNCTION:    main
 *
 * PARAMETERS:  Standard argc/argv
 *
 * RETURN:      Program termination code
 *
 * DESCRIPTION: C main routine for the iASL Compiler/Disassembler. Process
 *  command line options and begin the compile/disassembly for each file on
 *  the command line (wildcards supported).
 *
 ******************************************************************************/

int ACPI_SYSTEM_XFACE
main (
    int                     argc,
    char                    **argv)
{
    ACPI_STATUS             Status;
    int                     Index1;
    int                     Index2;
    int                     ReturnStatus = 0;


    signal (SIGINT, AslSignalHandler);

    /*
     * Big-endian machines are not currently supported. ACPI tables must
     * be little-endian, and support for big-endian machines needs to
     * be implemented.
     */
    if (UtIsBigEndianMachine ())
    {
        fprintf (stderr,
            "iASL is not currently supported on big-endian machines.\n");
        return (-1);
    }

    AcpiOsInitialize ();
    ACPI_DEBUG_INITIALIZE (); /* For debug version only */

    /* Initialize preprocessor and compiler before command line processing */

    AcpiGbl_ExternalFileList = NULL;
    AcpiDbgLevel = 0;
    PrInitializePreprocessor ();
    AslInitialize ();

    Index1 = Index2 =
        AslCommandLine (argc, argv);

    /* Allocate the line buffer(s), must be after command line */

    AslGbl_LineBufferSize /= 2;
    UtExpandLineBuffers ();

    /* Perform global actions first/only */

    if (AslGbl_DisassembleAll)
    {
        while (argv[Index1])
        {
            Status = AcpiDmAddToExternalFileList (argv[Index1]);
            if (ACPI_FAILURE (Status))
            {
                return (-1);
            }

            Index1++;
        }
    }


    /* Process each pathname/filename in the list, with possible wildcards */

    while (argv[Index2])
    {
        /*
         * If -p not specified, we will use the input filename as the
         * output filename prefix
         */
        if (AslGbl_UseDefaultAmlFilename)
        {
            AslGbl_OutputFilenamePrefix = argv[Index2];
            UtConvertBackslashes (AslGbl_OutputFilenamePrefix);
        }

        Status = AslDoOneFile (argv[Index2]);
        if (ACPI_FAILURE (Status))
        {
            ReturnStatus = -1;
            goto CleanupAndExit;
        }

        Index2++;
    }


CleanupAndExit:

    UtFreeLineBuffers ();
    AslParserCleanup ();

    if (AcpiGbl_ExternalFileList)
    {
        AcpiDmClearExternalFileList();
    }

    return (ReturnStatus);
}


/******************************************************************************
 *
 * FUNCTION:    AslSignalHandler
 *
 * PARAMETERS:  Sig                 - Signal that invoked this handler
 *
 * RETURN:      None
 *
 * DESCRIPTION: Signal interrupt handler. Delete any intermediate files and
 *              any output files that may be left in an indeterminate state.
 *              Currently handles SIGINT (control-c).
 *
 *****************************************************************************/

static void ACPI_SYSTEM_XFACE
AslSignalHandler (
    int                     Sig)
{
    UINT32                  i;


    signal (Sig, SIG_IGN);
    fflush (stdout);
    fflush (stderr);

    switch (Sig)
    {
    case SIGINT:

        printf ("\n" ASL_PREFIX "<Control-C>\n");
        break;

    default:

        printf (ASL_PREFIX "Unknown interrupt signal (%d)\n", Sig);
        break;
    }

    /*
     * Close all open files
     * Note: the .pre file is the same as the input source file
     */
    AslGbl_Files[ASL_FILE_PREPROCESSOR].Handle = NULL;

    for (i = ASL_FILE_INPUT; i < ASL_MAX_FILE_TYPE; i++)
    {
        FlCloseFile (i);
    }

    /* Delete any output files */

    for (i = ASL_FILE_AML_OUTPUT; i < ASL_MAX_FILE_TYPE; i++)
    {
        FlDeleteFile (i);
    }

    printf (ASL_PREFIX "Terminating\n");
    exit (0);
}


/*******************************************************************************
 *
 * FUNCTION:    AslInitialize
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize compiler globals
 *
 ******************************************************************************/

static void
AslInitialize (
    void)
{
    UINT32                  i;


    AcpiGbl_DmOpt_Verbose = FALSE;

    /* Default integer width is 32 bits */

    AcpiGbl_IntegerBitWidth = 32;
    AcpiGbl_IntegerNybbleWidth = 8;
    AcpiGbl_IntegerByteWidth = 4;

    for (i = 0; i < ASL_NUM_FILES; i++)
    {
        AslGbl_Files[i].Handle = NULL;
        AslGbl_Files[i].Filename = NULL;
    }

    AslGbl_Files[ASL_FILE_STDOUT].Handle   = stdout;
    AslGbl_Files[ASL_FILE_STDOUT].Filename = "STDOUT";

    AslGbl_Files[ASL_FILE_STDERR].Handle   = stderr;
    AslGbl_Files[ASL_FILE_STDERR].Filename = "STDERR";
}
