/******************************************************************************
 *
 * Module Name: adfile - Application-level disassembler file support routines
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
#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/acapps.h>

#include <stdio.h>


#define _COMPONENT          ACPI_TOOLS
        ACPI_MODULE_NAME    ("adfile")

/* Local prototypes */

static INT32
AdWriteBuffer (
    char                    *Filename,
    char                    *Buffer,
    UINT32                  Length);

static char                 FilenameBuf[20];


/******************************************************************************
 *
 * FUNCTION:    AfGenerateFilename
 *
 * PARAMETERS:  Prefix              - prefix string
 *              TableId             - The table ID
 *
 * RETURN:      Pointer to the completed string
 *
 * DESCRIPTION: Build an output filename from an ACPI table ID string
 *
 ******************************************************************************/

char *
AdGenerateFilename (
    char                    *Prefix,
    char                    *TableId)
{
    UINT32                  i;
    UINT32                  j;


    for (i = 0; Prefix[i]; i++)
    {
        FilenameBuf[i] = Prefix[i];
    }

    FilenameBuf[i] = '_';
    i++;

    for (j = 0; j < 8 && (TableId[j] != ' ') && (TableId[j] != 0); i++, j++)
    {
        FilenameBuf[i] = TableId[j];
    }

    FilenameBuf[i] = 0;
    strcat (FilenameBuf, FILE_SUFFIX_BINARY_TABLE);
    return (FilenameBuf);
}


/******************************************************************************
 *
 * FUNCTION:    AfWriteBuffer
 *
 * PARAMETERS:  Filename            - name of file
 *              Buffer              - data to write
 *              Length              - length of data
 *
 * RETURN:      Actual number of bytes written
 *
 * DESCRIPTION: Open a file and write out a single buffer
 *
 ******************************************************************************/

static INT32
AdWriteBuffer (
    char                    *Filename,
    char                    *Buffer,
    UINT32                  Length)
{
    FILE                    *File;
    ACPI_SIZE               Actual;


    File = fopen (Filename, "wb");
    if (!File)
    {
        printf ("Could not open file %s\n", Filename);
        return (-1);
    }

    Actual = fwrite (Buffer, 1, (size_t) Length, File);
    if (Actual != Length)
    {
        printf ("Could not write to file %s\n", Filename);
    }

    fclose (File);
    return ((INT32) Actual);
}


/******************************************************************************
 *
 * FUNCTION:    AfWriteTable
 *
 * PARAMETERS:  Table               - pointer to the ACPI table
 *              Length              - length of the table
 *              TableName           - the table signature
 *              OemTableID          - from the table header
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the loaded tables to a file (or files)
 *
 ******************************************************************************/

void
AdWriteTable (
    ACPI_TABLE_HEADER       *Table,
    UINT32                  Length,
    char                    *TableName,
    char                    *OemTableId)
{
    char                    *Filename;


    Filename = AdGenerateFilename (TableName, OemTableId);
    AdWriteBuffer (Filename, (char *) Table, Length);

    AcpiOsPrintf ("Table [%s] written to \"%s\"\n", TableName, Filename);
}


/*******************************************************************************
 *
 * FUNCTION:    FlGenerateFilename
 *
 * PARAMETERS:  InputFilename       - Original ASL source filename
 *              Suffix              - New extension.
 *
 * RETURN:      New filename containing the original base + the new suffix
 *
 * DESCRIPTION: Generate a new filename from the ASL source filename and a new
 *              extension. Used to create the *.LST, *.TXT, etc. files.
 *
 ******************************************************************************/

char *
FlGenerateFilename (
    char                    *InputFilename,
    char                    *Suffix)
{
    char                    *Position;
    char                    *NewFilename;
    char                    *DirectoryPosition;


    /*
     * Copy the original filename to a new buffer. Leave room for the worst
     * case where we append the suffix, an added dot and the null terminator.
     */
    NewFilename = UtLocalCacheCalloc ((ACPI_SIZE)
        strlen (InputFilename) + strlen (Suffix) + 2);
    strcpy (NewFilename, InputFilename);

    /* Try to find the last dot in the filename */

    DirectoryPosition = strrchr (NewFilename, '/');
    Position = strrchr (NewFilename, '.');

    if (Position && (Position > DirectoryPosition))
    {
        /* Tack on the new suffix */

        Position++;
        *Position = 0;
        strcat (Position, Suffix);
    }
    else
    {
        /* No dot, add one and then the suffix */

        strcat (NewFilename, ".");
        strcat (NewFilename, Suffix);
    }

    return (NewFilename);
}


/*******************************************************************************
 *
 * FUNCTION:    FlStrdup
 *
 * DESCRIPTION: Local strdup function
 *
 ******************************************************************************/

static char *
FlStrdup (
    char                *String)
{
    char                *NewString;


    NewString = UtLocalCacheCalloc ((ACPI_SIZE) strlen (String) + 1);
    strcpy (NewString, String);
    return (NewString);
}


/*******************************************************************************
 *
 * FUNCTION:    FlSplitInputPathname
 *
 * PARAMETERS:  InputFilename       - The user-specified ASL source file to be
 *                                    compiled
 *              OutDirectoryPath    - Where the directory path prefix is
 *                                    returned
 *              OutFilename         - Where the filename part is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Split the input path into a directory and filename part
 *              1) Directory part used to open include files
 *              2) Filename part used to generate output filenames
 *
 ******************************************************************************/

ACPI_STATUS
FlSplitInputPathname (
    char                    *InputPath,
    char                    **OutDirectoryPath,
    char                    **OutFilename)
{
    char                    *Substring;
    char                    *DirectoryPath;
    char                    *Filename;


    if (OutDirectoryPath)
    {
        *OutDirectoryPath = NULL;
    }

    if (!InputPath)
    {
        return (AE_OK);
    }

    /* Get the path to the input filename's directory */

    DirectoryPath = FlStrdup (InputPath);
    if (!DirectoryPath)
    {
        return (AE_NO_MEMORY);
    }

    /* Convert backslashes to slashes in the entire path */

    UtConvertBackslashes (DirectoryPath);

    /* Backup to last slash or colon */

    Substring = strrchr (DirectoryPath, '/');
    if (!Substring)
    {
        Substring = strrchr (DirectoryPath, ':');
    }

    /* Extract the simple filename */

    if (!Substring)
    {
        Filename = FlStrdup (DirectoryPath);
        DirectoryPath[0] = 0;
    }
    else
    {
        Filename = FlStrdup (Substring + 1);
        *(Substring+1) = 0;
    }

    if (!Filename)
    {
        return (AE_NO_MEMORY);
    }

    if (OutDirectoryPath)
    {
        *OutDirectoryPath = DirectoryPath;
    }

    if (OutFilename)
    {
        *OutFilename = Filename;
        return (AE_OK);
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    FlGetFileBasename
 *
 * PARAMETERS:  FilePathname            - File path to be split
 *
 * RETURN:      The extracted base name of the file, in upper case
 *
 * DESCRIPTION: Extract the file base name (the file name with no extension)
 *              from the input pathname.
 *
 *              Note: Any backslashes in the pathname should be previously
 *              converted to forward slashes before calling this function.
 *
 ******************************************************************************/

char *
FlGetFileBasename (
    char                    *FilePathname)
{
    char                    *FileBasename;
    char                    *Substring;


    /* Backup to last slash or colon */

    Substring = strrchr (FilePathname, '/');
    if (!Substring)
    {
        Substring = strrchr (FilePathname, ':');
    }

    /* Extract the full filename (base + extension) */

    if (Substring)
    {
        FileBasename = FlStrdup (Substring + 1);
    }
    else
    {
        FileBasename = FlStrdup (FilePathname);
    }

    /* Remove the filename extension if present */

    Substring = strchr (FileBasename, '.');
    if (Substring)
    {
        *Substring = 0;
    }

    AcpiUtStrupr (FileBasename);
    return (FileBasename);
}
