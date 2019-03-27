/******************************************************************************
 *
 * Module Name: aslhex - ASCII hex output file generation (C, ASM, and ASL)
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
        ACPI_MODULE_NAME    ("ashex")

/*
 * This module emits ASCII hex output files in either C, ASM, or ASL format
 */

/* Local prototypes */

static void
HxDoHexOutputC (
    void);

static void
HxDoHexOutputAsl (
    void);

static void
HxDoHexOutputAsm (
    void);

static UINT32
HxReadAmlOutputFile (
    UINT8                   *Buffer);


/*******************************************************************************
 *
 * FUNCTION:    HxDoHexOutput
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Create the hex output file. Note: data is obtained by reading
 *              the entire AML output file that was previously generated.
 *
 ******************************************************************************/

void
HxDoHexOutput (
    void)
{

    switch (AslGbl_HexOutputFlag)
    {
    case HEX_OUTPUT_C:

        HxDoHexOutputC ();
        break;

    case HEX_OUTPUT_ASM:

        HxDoHexOutputAsm ();
        break;

    case HEX_OUTPUT_ASL:

        HxDoHexOutputAsl ();
        break;

    default:

        /* No other output types supported */

        break;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    HxReadAmlOutputFile
 *
 * PARAMETERS:  Buffer              - Where to return data
 *
 * RETURN:      None
 *
 * DESCRIPTION: Read a line of the AML output prior to formatting the data
 *
 ******************************************************************************/

static UINT32
HxReadAmlOutputFile (
    UINT8                   *Buffer)
{
    UINT32                  Actual;


    Actual = fread (Buffer, 1, HEX_TABLE_LINE_SIZE,
        AslGbl_Files[ASL_FILE_AML_OUTPUT].Handle);

    if (ferror (AslGbl_Files[ASL_FILE_AML_OUTPUT].Handle))
    {
        FlFileError (ASL_FILE_AML_OUTPUT, ASL_MSG_READ);
        AslAbort ();
    }

    return (Actual);
}


/*******************************************************************************
 *
 * FUNCTION:    HxDoHexOutputC
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Create the hex output file. This is the same data as the AML
 *              output file, but formatted into hex/ascii bytes suitable for
 *              inclusion into a C source file.
 *
 *              Note: the base name of the hex output file is prepended to
 *              all symbols as they are output to the file.
 *
 ******************************************************************************/

static void
HxDoHexOutputC (
    void)
{
    UINT8                   FileData[HEX_TABLE_LINE_SIZE];
    UINT32                  LineLength;
    UINT32                  Offset = 0;
    UINT32                  AmlFileSize;
    UINT32                  i;
    char                    *FileBasename;


    /* Obtain the file basename (filename with no extension) */

    FileBasename = FlGetFileBasename (AslGbl_Files [ASL_FILE_HEX_OUTPUT].Filename);

    /* Get AML size, seek back to start */

    AmlFileSize = FlGetFileSize (ASL_FILE_AML_OUTPUT);
    FlSeekFile (ASL_FILE_AML_OUTPUT, 0);

    /* Finish the file header and emit the non-data symbols */

    FlPrintFile (ASL_FILE_HEX_OUTPUT, " * C source code output\n");
    FlPrintFile (ASL_FILE_HEX_OUTPUT, " * AML code block contains 0x%X bytes\n *\n */\n",
        AmlFileSize);

    FlPrintFile (ASL_FILE_HEX_OUTPUT, "#ifndef __%s_HEX__\n", FileBasename);
    FlPrintFile (ASL_FILE_HEX_OUTPUT, "#define __%s_HEX__\n\n", FileBasename);

    AcpiUtStrlwr (FileBasename);
    FlPrintFile (ASL_FILE_HEX_OUTPUT, "unsigned char %s_aml_code[] =\n{\n", FileBasename);

    while (Offset < AmlFileSize)
    {
        /* Read enough bytes needed for one output line */

        LineLength = HxReadAmlOutputFile (FileData);
        if (!LineLength)
        {
            break;
        }

        FlPrintFile (ASL_FILE_HEX_OUTPUT, "    ");

        for (i = 0; i < LineLength; i++)
        {
            /*
             * Output each hex byte in the form: "0xnn,"
             * Add a comma until the very last byte of the AML file
             * (Some C compilers complain about a trailing comma)
             */
            FlPrintFile (ASL_FILE_HEX_OUTPUT, "0x%2.2X", FileData[i]);
            if ((Offset + i + 1) < AmlFileSize)
            {
                FlPrintFile (ASL_FILE_HEX_OUTPUT, ",");
            }
            else
            {
                FlPrintFile (ASL_FILE_HEX_OUTPUT, " ");
            }
        }

        /* Add fill spaces if needed for last line */

        if (LineLength < HEX_TABLE_LINE_SIZE)
        {
            FlPrintFile (ASL_FILE_HEX_OUTPUT, "%*s",
                5 * (HEX_TABLE_LINE_SIZE - LineLength), " ");
        }

        /* Emit the offset and ascii dump for the entire line */

        FlPrintFile (ASL_FILE_HEX_OUTPUT, "  /* %8.8X", Offset);
        LsDumpAsciiInComment (ASL_FILE_HEX_OUTPUT, LineLength, FileData);

        FlPrintFile (ASL_FILE_HEX_OUTPUT, "%*s*/\n",
            HEX_TABLE_LINE_SIZE - LineLength + 1, " ");

        Offset += LineLength;
    }

    FlPrintFile (ASL_FILE_HEX_OUTPUT, "};\n\n");
    FlPrintFile (ASL_FILE_HEX_OUTPUT, "#endif\n");
}


/*******************************************************************************
 *
 * FUNCTION:    HxDoHexOutputAsl
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Create the hex output file. This is the same data as the AML
 *              output file, but formatted into hex/ascii bytes suitable for
 *              inclusion into a C source file.
 *
 ******************************************************************************/

static void
HxDoHexOutputAsl (
    void)
{
    UINT8                   FileData[HEX_TABLE_LINE_SIZE];
    UINT32                  LineLength;
    UINT32                  Offset = 0;
    UINT32                  AmlFileSize;
    UINT32                  i;


    /* Get AML size, seek back to start */

    AmlFileSize = FlGetFileSize (ASL_FILE_AML_OUTPUT);
    FlSeekFile (ASL_FILE_AML_OUTPUT, 0);

    FlPrintFile (ASL_FILE_HEX_OUTPUT, " * ASL source code output\n");
    FlPrintFile (ASL_FILE_HEX_OUTPUT, " * AML code block contains 0x%X bytes\n *\n */\n",
        AmlFileSize);
    FlPrintFile (ASL_FILE_HEX_OUTPUT, "    Name (BUF1, Buffer()\n    {\n");

    while (Offset < AmlFileSize)
    {
        /* Read enough bytes needed for one output line */

        LineLength = HxReadAmlOutputFile (FileData);
        if (!LineLength)
        {
            break;
        }

        FlPrintFile (ASL_FILE_HEX_OUTPUT, "        ");

        for (i = 0; i < LineLength; i++)
        {
            /*
             * Print each hex byte.
             * Add a comma until the very last byte of the AML file
             * (Some C compilers complain about a trailing comma)
             */
            FlPrintFile (ASL_FILE_HEX_OUTPUT, "0x%2.2X", FileData[i]);
            if ((Offset + i + 1) < AmlFileSize)
            {
                FlPrintFile (ASL_FILE_HEX_OUTPUT, ",");
            }
            else
            {
                FlPrintFile (ASL_FILE_HEX_OUTPUT, " ");
            }
        }

        /* Add fill spaces if needed for last line */

        if (LineLength < HEX_TABLE_LINE_SIZE)
        {
            FlPrintFile (ASL_FILE_HEX_OUTPUT, "%*s",
                5 * (HEX_TABLE_LINE_SIZE - LineLength), " ");
        }

        /* Emit the offset and ascii dump for the entire line */

        FlPrintFile (ASL_FILE_HEX_OUTPUT, "  /* %8.8X", Offset);
        LsDumpAsciiInComment (ASL_FILE_HEX_OUTPUT, LineLength, FileData);

        FlPrintFile (ASL_FILE_HEX_OUTPUT, "%*s*/\n",
            HEX_TABLE_LINE_SIZE - LineLength + 1, " ");

        Offset += LineLength;
    }

    FlPrintFile (ASL_FILE_HEX_OUTPUT, "    })\n");
}


/*******************************************************************************
 *
 * FUNCTION:    HxDoHexOutputAsm
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Create the hex output file. This is the same data as the AML
 *              output file, but formatted into hex/ascii bytes suitable for
 *              inclusion into a ASM source file.
 *
 ******************************************************************************/

static void
HxDoHexOutputAsm (
    void)
{
    UINT8                   FileData[HEX_TABLE_LINE_SIZE];
    UINT32                  LineLength;
    UINT32                  Offset = 0;
    UINT32                  AmlFileSize;
    UINT32                  i;


    /* Get AML size, seek back to start */

    AmlFileSize = FlGetFileSize (ASL_FILE_AML_OUTPUT);
    FlSeekFile (ASL_FILE_AML_OUTPUT, 0);

    FlPrintFile (ASL_FILE_HEX_OUTPUT, "; Assembly code source output\n");
    FlPrintFile (ASL_FILE_HEX_OUTPUT, "; AML code block contains 0x%X bytes\n;\n",
        AmlFileSize);

    while (Offset < AmlFileSize)
    {
        /* Read enough bytes needed for one output line */

        LineLength = HxReadAmlOutputFile (FileData);
        if (!LineLength)
        {
            break;
        }

        FlPrintFile (ASL_FILE_HEX_OUTPUT, "  db  ");

        for (i = 0; i < LineLength; i++)
        {
            /*
             * Print each hex byte.
             * Add a comma until the last byte of the line
             */
            FlPrintFile (ASL_FILE_HEX_OUTPUT, "0%2.2Xh", FileData[i]);
            if ((i + 1) < LineLength)
            {
                FlPrintFile (ASL_FILE_HEX_OUTPUT, ",");
            }
        }

        FlPrintFile (ASL_FILE_HEX_OUTPUT, " ");

        /* Add fill spaces if needed for last line */

        if (LineLength < HEX_TABLE_LINE_SIZE)
        {
            FlPrintFile (ASL_FILE_HEX_OUTPUT, "%*s",
                5 * (HEX_TABLE_LINE_SIZE - LineLength), " ");
        }

        /* Emit the offset and ascii dump for the entire line */

        FlPrintFile (ASL_FILE_HEX_OUTPUT, "  ; %8.8X", Offset);
        LsDumpAsciiInComment (ASL_FILE_HEX_OUTPUT, LineLength, FileData);

        FlPrintFile (ASL_FILE_HEX_OUTPUT, "\n");

        Offset += LineLength;
    }

    FlPrintFile (ASL_FILE_HEX_OUTPUT, "\n");
}
