/*******************************************************************************
 *
 * Module Name: dbnames - Debugger commands for the acpi namespace
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
#include <contrib/dev/acpica/include/acnamesp.h>
#include <contrib/dev/acpica/include/acdebug.h>
#include <contrib/dev/acpica/include/acpredef.h>


#define _COMPONENT          ACPI_CA_DEBUGGER
        ACPI_MODULE_NAME    ("dbnames")


/* Local prototypes */

static ACPI_STATUS
AcpiDbWalkAndMatchName (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue);

static ACPI_STATUS
AcpiDbWalkForPredefinedNames (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue);

static ACPI_STATUS
AcpiDbWalkForSpecificObjects (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue);

static ACPI_STATUS
AcpiDbWalkForObjectCounts (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue);

static ACPI_STATUS
AcpiDbIntegrityWalk (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue);

static ACPI_STATUS
AcpiDbWalkForReferences (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue);

static ACPI_STATUS
AcpiDbBusWalk (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue);

/*
 * Arguments for the Objects command
 * These object types map directly to the ACPI_TYPES
 */
static ACPI_DB_ARGUMENT_INFO    AcpiDbObjectTypes [] =
{
    {"ANY"},
    {"INTEGERS"},
    {"STRINGS"},
    {"BUFFERS"},
    {"PACKAGES"},
    {"FIELDS"},
    {"DEVICES"},
    {"EVENTS"},
    {"METHODS"},
    {"MUTEXES"},
    {"REGIONS"},
    {"POWERRESOURCES"},
    {"PROCESSORS"},
    {"THERMALZONES"},
    {"BUFFERFIELDS"},
    {"DDBHANDLES"},
    {"DEBUG"},
    {"REGIONFIELDS"},
    {"BANKFIELDS"},
    {"INDEXFIELDS"},
    {"REFERENCES"},
    {"ALIASES"},
    {"METHODALIASES"},
    {"NOTIFY"},
    {"ADDRESSHANDLER"},
    {"RESOURCE"},
    {"RESOURCEFIELD"},
    {"SCOPES"},
    {NULL}           /* Must be null terminated */
};


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbSetScope
 *
 * PARAMETERS:  Name                - New scope path
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Set the "current scope" as maintained by this utility.
 *              The scope is used as a prefix to ACPI paths.
 *
 ******************************************************************************/

void
AcpiDbSetScope (
    char                    *Name)
{
    ACPI_STATUS             Status;
    ACPI_NAMESPACE_NODE     *Node;


    if (!Name || Name[0] == 0)
    {
        AcpiOsPrintf ("Current scope: %s\n", AcpiGbl_DbScopeBuf);
        return;
    }

    AcpiDbPrepNamestring (Name);

    if (ACPI_IS_ROOT_PREFIX (Name[0]))
    {
        /* Validate new scope from the root */

        Status = AcpiNsGetNode (AcpiGbl_RootNode, Name,
            ACPI_NS_NO_UPSEARCH, &Node);
        if (ACPI_FAILURE (Status))
        {
            goto ErrorExit;
        }

        AcpiGbl_DbScopeBuf[0] = 0;
    }
    else
    {
        /* Validate new scope relative to old scope */

        Status = AcpiNsGetNode (AcpiGbl_DbScopeNode, Name,
            ACPI_NS_NO_UPSEARCH, &Node);
        if (ACPI_FAILURE (Status))
        {
            goto ErrorExit;
        }
    }

    /* Build the final pathname */

    if (AcpiUtSafeStrcat (AcpiGbl_DbScopeBuf, sizeof (AcpiGbl_DbScopeBuf),
        Name))
    {
        Status = AE_BUFFER_OVERFLOW;
        goto ErrorExit;
    }

    if (AcpiUtSafeStrcat (AcpiGbl_DbScopeBuf, sizeof (AcpiGbl_DbScopeBuf),
            "\\"))
    {
        Status = AE_BUFFER_OVERFLOW;
        goto ErrorExit;
    }

    AcpiGbl_DbScopeNode = Node;
    AcpiOsPrintf ("New scope: %s\n", AcpiGbl_DbScopeBuf);
    return;

ErrorExit:

    AcpiOsPrintf ("Could not attach scope: %s, %s\n",
        Name, AcpiFormatException (Status));
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDumpNamespace
 *
 * PARAMETERS:  StartArg        - Node to begin namespace dump
 *              DepthArg        - Maximum tree depth to be dumped
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump entire namespace or a subtree. Each node is displayed
 *              with type and other information.
 *
 ******************************************************************************/

void
AcpiDbDumpNamespace (
    char                    *StartArg,
    char                    *DepthArg)
{
    ACPI_HANDLE             SubtreeEntry = AcpiGbl_RootNode;
    UINT32                  MaxDepth = ACPI_UINT32_MAX;


    /* No argument given, just start at the root and dump entire namespace */

    if (StartArg)
    {
        SubtreeEntry = AcpiDbConvertToNode (StartArg);
        if (!SubtreeEntry)
        {
            return;
        }

        /* Now we can check for the depth argument */

        if (DepthArg)
        {
            MaxDepth = strtoul (DepthArg, NULL, 0);
        }
    }

    AcpiDbSetOutputDestination (ACPI_DB_DUPLICATE_OUTPUT);

    if (((ACPI_NAMESPACE_NODE *) SubtreeEntry)->Parent)
    {
        AcpiOsPrintf ("ACPI Namespace (from %4.4s (%p) subtree):\n",
            ((ACPI_NAMESPACE_NODE *) SubtreeEntry)->Name.Ascii, SubtreeEntry);
    }
    else
    {
        AcpiOsPrintf ("ACPI Namespace (from %s):\n",
            ACPI_NAMESPACE_ROOT);
    }

    /* Display the subtree */

    AcpiDbSetOutputDestination (ACPI_DB_REDIRECTABLE_OUTPUT);
    AcpiNsDumpObjects (ACPI_TYPE_ANY, ACPI_DISPLAY_SUMMARY, MaxDepth,
        ACPI_OWNER_ID_MAX, SubtreeEntry);
    AcpiDbSetOutputDestination (ACPI_DB_CONSOLE_OUTPUT);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDumpNamespacePaths
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump entire namespace with full object pathnames and object
 *              type information. Alternative to "namespace" command.
 *
 ******************************************************************************/

void
AcpiDbDumpNamespacePaths (
    void)
{

    AcpiDbSetOutputDestination (ACPI_DB_DUPLICATE_OUTPUT);
    AcpiOsPrintf ("ACPI Namespace (from root):\n");

    /* Display the entire namespace */

    AcpiDbSetOutputDestination (ACPI_DB_REDIRECTABLE_OUTPUT);
    AcpiNsDumpObjectPaths (ACPI_TYPE_ANY, ACPI_DISPLAY_SUMMARY,
        ACPI_UINT32_MAX, ACPI_OWNER_ID_MAX, AcpiGbl_RootNode);

    AcpiDbSetOutputDestination (ACPI_DB_CONSOLE_OUTPUT);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDumpNamespaceByOwner
 *
 * PARAMETERS:  OwnerArg        - Owner ID whose nodes will be displayed
 *              DepthArg        - Maximum tree depth to be dumped
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump elements of the namespace that are owned by the OwnerId.
 *
 ******************************************************************************/

void
AcpiDbDumpNamespaceByOwner (
    char                    *OwnerArg,
    char                    *DepthArg)
{
    ACPI_HANDLE             SubtreeEntry = AcpiGbl_RootNode;
    UINT32                  MaxDepth = ACPI_UINT32_MAX;
    ACPI_OWNER_ID           OwnerId;


    OwnerId = (ACPI_OWNER_ID) strtoul (OwnerArg, NULL, 0);

    /* Now we can check for the depth argument */

    if (DepthArg)
    {
        MaxDepth = strtoul (DepthArg, NULL, 0);
    }

    AcpiDbSetOutputDestination (ACPI_DB_DUPLICATE_OUTPUT);
    AcpiOsPrintf ("ACPI Namespace by owner %X:\n", OwnerId);

    /* Display the subtree */

    AcpiDbSetOutputDestination (ACPI_DB_REDIRECTABLE_OUTPUT);
    AcpiNsDumpObjects (ACPI_TYPE_ANY, ACPI_DISPLAY_SUMMARY, MaxDepth,
        OwnerId, SubtreeEntry);
    AcpiDbSetOutputDestination (ACPI_DB_CONSOLE_OUTPUT);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbWalkAndMatchName
 *
 * PARAMETERS:  Callback from WalkNamespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Find a particular name/names within the namespace. Wildcards
 *              are supported -- '?' matches any character.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbWalkAndMatchName (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue)
{
    ACPI_STATUS             Status;
    char                    *RequestedName = (char *) Context;
    UINT32                  i;
    ACPI_BUFFER             Buffer;
    ACPI_WALK_INFO          Info;


    /* Check for a name match */

    for (i = 0; i < 4; i++)
    {
        /* Wildcard support */

        if ((RequestedName[i] != '?') &&
            (RequestedName[i] != ((ACPI_NAMESPACE_NODE *)
                ObjHandle)->Name.Ascii[i]))
        {
            /* No match, just exit */

            return (AE_OK);
        }
    }

    /* Get the full pathname to this object */

    Buffer.Length = ACPI_ALLOCATE_LOCAL_BUFFER;
    Status = AcpiNsHandleToPathname (ObjHandle, &Buffer, TRUE);
    if (ACPI_FAILURE (Status))
    {
        AcpiOsPrintf ("Could Not get pathname for object %p\n",
            ObjHandle);
    }
    else
    {
        Info.Count = 0;
        Info.OwnerId = ACPI_OWNER_ID_MAX;
        Info.DebugLevel = ACPI_UINT32_MAX;
        Info.DisplayType = ACPI_DISPLAY_SUMMARY | ACPI_DISPLAY_SHORT;

        AcpiOsPrintf ("%32s", (char *) Buffer.Pointer);
        (void) AcpiNsDumpOneObject (ObjHandle, NestingLevel, &Info, NULL);
        ACPI_FREE (Buffer.Pointer);
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbFindNameInNamespace
 *
 * PARAMETERS:  NameArg         - The 4-character ACPI name to find.
 *                                wildcards are supported.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Search the namespace for a given name (with wildcards)
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDbFindNameInNamespace (
    char                    *NameArg)
{
    char                    AcpiName[5] = "____";
    char                    *AcpiNamePtr = AcpiName;


    if (strlen (NameArg) > ACPI_NAME_SIZE)
    {
        AcpiOsPrintf ("Name must be no longer than 4 characters\n");
        return (AE_OK);
    }

    /* Pad out name with underscores as necessary to create a 4-char name */

    AcpiUtStrupr (NameArg);
    while (*NameArg)
    {
        *AcpiNamePtr = *NameArg;
        AcpiNamePtr++;
        NameArg++;
    }

    /* Walk the namespace from the root */

    (void) AcpiWalkNamespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
        ACPI_UINT32_MAX, AcpiDbWalkAndMatchName, NULL, AcpiName, NULL);

    AcpiDbSetOutputDestination (ACPI_DB_CONSOLE_OUTPUT);
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbWalkForPredefinedNames
 *
 * PARAMETERS:  Callback from WalkNamespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Detect and display predefined ACPI names (names that start with
 *              an underscore)
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbWalkForPredefinedNames (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue)
{
    ACPI_NAMESPACE_NODE         *Node = (ACPI_NAMESPACE_NODE *) ObjHandle;
    UINT32                      *Count = (UINT32 *) Context;
    const ACPI_PREDEFINED_INFO  *Predefined;
    const ACPI_PREDEFINED_INFO  *Package = NULL;
    char                        *Pathname;
    char                        StringBuffer[48];


    Predefined = AcpiUtMatchPredefinedMethod (Node->Name.Ascii);
    if (!Predefined)
    {
        return (AE_OK);
    }

    Pathname = AcpiNsGetNormalizedPathname (Node, TRUE);
    if (!Pathname)
    {
        return (AE_OK);
    }

    /* If method returns a package, the info is in the next table entry */

    if (Predefined->Info.ExpectedBtypes & ACPI_RTYPE_PACKAGE)
    {
        Package = Predefined + 1;
    }

    AcpiUtGetExpectedReturnTypes (StringBuffer,
        Predefined->Info.ExpectedBtypes);

    AcpiOsPrintf ("%-32s Arguments %X, Return Types: %s", Pathname,
        METHOD_GET_ARG_COUNT (Predefined->Info.ArgumentList),
        StringBuffer);

    if (Package)
    {
        AcpiOsPrintf (" (PkgType %2.2X, ObjType %2.2X, Count %2.2X)",
            Package->RetInfo.Type, Package->RetInfo.ObjectType1,
            Package->RetInfo.Count1);
    }

    AcpiOsPrintf("\n");

    /* Check that the declared argument count matches the ACPI spec */

    AcpiNsCheckAcpiCompliance (Pathname, Node, Predefined);

    ACPI_FREE (Pathname);
    (*Count)++;
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbCheckPredefinedNames
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Validate all predefined names in the namespace
 *
 ******************************************************************************/

void
AcpiDbCheckPredefinedNames (
    void)
{
    UINT32                  Count = 0;


    /* Search all nodes in namespace */

    (void) AcpiWalkNamespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
        ACPI_UINT32_MAX, AcpiDbWalkForPredefinedNames,
        NULL, (void *) &Count, NULL);

    AcpiOsPrintf ("Found %u predefined names in the namespace\n", Count);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbWalkForObjectCounts
 *
 * PARAMETERS:  Callback from WalkNamespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Display short info about objects in the namespace
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbWalkForObjectCounts (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue)
{
    ACPI_OBJECT_INFO        *Info = (ACPI_OBJECT_INFO *) Context;
    ACPI_NAMESPACE_NODE     *Node = (ACPI_NAMESPACE_NODE *) ObjHandle;


    if (Node->Type > ACPI_TYPE_NS_NODE_MAX)
    {
        AcpiOsPrintf ("[%4.4s]: Unknown object type %X\n",
            Node->Name.Ascii, Node->Type);
    }
    else
    {
        Info->Types[Node->Type]++;
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbWalkForSpecificObjects
 *
 * PARAMETERS:  Callback from WalkNamespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Display short info about objects in the namespace
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbWalkForSpecificObjects (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue)
{
    ACPI_WALK_INFO          *Info = (ACPI_WALK_INFO *) Context;
    ACPI_BUFFER             Buffer;
    ACPI_STATUS             Status;


    Info->Count++;

    /* Get and display the full pathname to this object */

    Buffer.Length = ACPI_ALLOCATE_LOCAL_BUFFER;
    Status = AcpiNsHandleToPathname (ObjHandle, &Buffer, TRUE);
    if (ACPI_FAILURE (Status))
    {
        AcpiOsPrintf ("Could Not get pathname for object %p\n", ObjHandle);
        return (AE_OK);
    }

    AcpiOsPrintf ("%32s", (char *) Buffer.Pointer);
    ACPI_FREE (Buffer.Pointer);

    /* Dump short info about the object */

    (void) AcpiNsDumpOneObject (ObjHandle, NestingLevel, Info, NULL);
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDisplayObjects
 *
 * PARAMETERS:  ObjTypeArg          - Type of object to display
 *              DisplayCountArg     - Max depth to display
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display objects in the namespace of the requested type
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDbDisplayObjects (
    char                    *ObjTypeArg,
    char                    *DisplayCountArg)
{
    ACPI_WALK_INFO          Info;
    ACPI_OBJECT_TYPE        Type;
    ACPI_OBJECT_INFO        *ObjectInfo;
    UINT32                  i;
    UINT32                  TotalObjects = 0;


    /* No argument means display summary/count of all object types */

    if (!ObjTypeArg)
    {
        ObjectInfo = ACPI_ALLOCATE_ZEROED (sizeof (ACPI_OBJECT_INFO));

        /* Walk the namespace from the root */

        (void) AcpiWalkNamespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
            ACPI_UINT32_MAX, AcpiDbWalkForObjectCounts, NULL,
            (void *) ObjectInfo, NULL);

        AcpiOsPrintf ("\nSummary of namespace objects:\n\n");

        for (i = 0; i < ACPI_TOTAL_TYPES; i++)
        {
            AcpiOsPrintf ("%8u   %s\n", ObjectInfo->Types[i],
                AcpiUtGetTypeName (i));

            TotalObjects += ObjectInfo->Types[i];
        }

        AcpiOsPrintf ("\n%8u   Total namespace objects\n\n",
            TotalObjects);

        ACPI_FREE (ObjectInfo);
        return (AE_OK);
    }

    /* Get the object type */

    Type = AcpiDbMatchArgument (ObjTypeArg, AcpiDbObjectTypes);
    if (Type == ACPI_TYPE_NOT_FOUND)
    {
        AcpiOsPrintf ("Invalid or unsupported argument\n");
        return (AE_OK);
    }

    AcpiDbSetOutputDestination (ACPI_DB_DUPLICATE_OUTPUT);
    AcpiOsPrintf (
        "Objects of type [%s] defined in the current ACPI Namespace:\n",
        AcpiUtGetTypeName (Type));

    AcpiDbSetOutputDestination (ACPI_DB_REDIRECTABLE_OUTPUT);

    Info.Count = 0;
    Info.OwnerId = ACPI_OWNER_ID_MAX;
    Info.DebugLevel = ACPI_UINT32_MAX;
    Info.DisplayType = ACPI_DISPLAY_SUMMARY | ACPI_DISPLAY_SHORT;

    /* Walk the namespace from the root */

    (void) AcpiWalkNamespace (Type, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX,
        AcpiDbWalkForSpecificObjects, NULL, (void *) &Info, NULL);

    AcpiOsPrintf (
        "\nFound %u objects of type [%s] in the current ACPI Namespace\n",
        Info.Count, AcpiUtGetTypeName (Type));

    AcpiDbSetOutputDestination (ACPI_DB_CONSOLE_OUTPUT);
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbIntegrityWalk
 *
 * PARAMETERS:  Callback from WalkNamespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Examine one NS node for valid values.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbIntegrityWalk (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue)
{
    ACPI_INTEGRITY_INFO     *Info = (ACPI_INTEGRITY_INFO *) Context;
    ACPI_NAMESPACE_NODE     *Node = (ACPI_NAMESPACE_NODE *) ObjHandle;
    ACPI_OPERAND_OBJECT     *Object;
    BOOLEAN                 Alias = TRUE;


    Info->Nodes++;

    /* Verify the NS node, and dereference aliases */

    while (Alias)
    {
        if (ACPI_GET_DESCRIPTOR_TYPE (Node) != ACPI_DESC_TYPE_NAMED)
        {
            AcpiOsPrintf (
                "Invalid Descriptor Type for Node %p [%s] - "
                "is %2.2X should be %2.2X\n",
                Node, AcpiUtGetDescriptorName (Node),
                ACPI_GET_DESCRIPTOR_TYPE (Node), ACPI_DESC_TYPE_NAMED);
            return (AE_OK);
        }

        if ((Node->Type == ACPI_TYPE_LOCAL_ALIAS)  ||
            (Node->Type == ACPI_TYPE_LOCAL_METHOD_ALIAS))
        {
            Node = (ACPI_NAMESPACE_NODE *) Node->Object;
        }
        else
        {
            Alias = FALSE;
        }
    }

    if (Node->Type > ACPI_TYPE_LOCAL_MAX)
    {
        AcpiOsPrintf ("Invalid Object Type for Node %p, Type = %X\n",
            Node, Node->Type);
        return (AE_OK);
    }

    if (!AcpiUtValidNameseg (Node->Name.Ascii))
    {
        AcpiOsPrintf ("Invalid AcpiName for Node %p\n", Node);
        return (AE_OK);
    }

    Object = AcpiNsGetAttachedObject (Node);
    if (Object)
    {
        Info->Objects++;
        if (ACPI_GET_DESCRIPTOR_TYPE (Object) != ACPI_DESC_TYPE_OPERAND)
        {
            AcpiOsPrintf ("Invalid Descriptor Type for Object %p [%s]\n",
                Object, AcpiUtGetDescriptorName (Object));
        }
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbCheckIntegrity
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Check entire namespace for data structure integrity
 *
 ******************************************************************************/

void
AcpiDbCheckIntegrity (
    void)
{
    ACPI_INTEGRITY_INFO     Info = {0,0};

    /* Search all nodes in namespace */

    (void) AcpiWalkNamespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
        ACPI_UINT32_MAX, AcpiDbIntegrityWalk, NULL, (void *) &Info, NULL);

    AcpiOsPrintf ("Verified %u namespace nodes with %u Objects\n",
        Info.Nodes, Info.Objects);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbWalkForReferences
 *
 * PARAMETERS:  Callback from WalkNamespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check if this namespace object refers to the target object
 *              that is passed in as the context value.
 *
 * Note: Currently doesn't check subobjects within the Node's object
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbWalkForReferences (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue)
{
    ACPI_OPERAND_OBJECT     *ObjDesc = (ACPI_OPERAND_OBJECT  *) Context;
    ACPI_NAMESPACE_NODE     *Node = (ACPI_NAMESPACE_NODE *) ObjHandle;


    /* Check for match against the namespace node itself */

    if (Node == (void *) ObjDesc)
    {
        AcpiOsPrintf ("Object is a Node [%4.4s]\n",
            AcpiUtGetNodeName (Node));
    }

    /* Check for match against the object attached to the node */

    if (AcpiNsGetAttachedObject (Node) == ObjDesc)
    {
        AcpiOsPrintf ("Reference at Node->Object %p [%4.4s]\n",
            Node, AcpiUtGetNodeName (Node));
    }

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbFindReferences
 *
 * PARAMETERS:  ObjectArg       - String with hex value of the object
 *
 * RETURN:      None
 *
 * DESCRIPTION: Search namespace for all references to the input object
 *
 ******************************************************************************/

void
AcpiDbFindReferences (
    char                    *ObjectArg)
{
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_SIZE               Address;


    /* Convert string to object pointer */

    Address = strtoul (ObjectArg, NULL, 16);
    ObjDesc = ACPI_TO_POINTER (Address);

    /* Search all nodes in namespace */

    (void) AcpiWalkNamespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
        ACPI_UINT32_MAX, AcpiDbWalkForReferences, NULL,
        (void *) ObjDesc, NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbBusWalk
 *
 * PARAMETERS:  Callback from WalkNamespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Display info about device objects that have a corresponding
 *              _PRT method.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiDbBusWalk (
    ACPI_HANDLE             ObjHandle,
    UINT32                  NestingLevel,
    void                    *Context,
    void                    **ReturnValue)
{
    ACPI_NAMESPACE_NODE     *Node = (ACPI_NAMESPACE_NODE *) ObjHandle;
    ACPI_STATUS             Status;
    ACPI_BUFFER             Buffer;
    ACPI_NAMESPACE_NODE     *TempNode;
    ACPI_DEVICE_INFO        *Info;
    UINT32                  i;


    if ((Node->Type != ACPI_TYPE_DEVICE) &&
        (Node->Type != ACPI_TYPE_PROCESSOR))
    {
        return (AE_OK);
    }

    /* Exit if there is no _PRT under this device */

    Status = AcpiGetHandle (Node, METHOD_NAME__PRT,
        ACPI_CAST_PTR (ACPI_HANDLE, &TempNode));
    if (ACPI_FAILURE (Status))
    {
        return (AE_OK);
    }

    /* Get the full path to this device object */

    Buffer.Length = ACPI_ALLOCATE_LOCAL_BUFFER;
    Status = AcpiNsHandleToPathname (ObjHandle, &Buffer, TRUE);
    if (ACPI_FAILURE (Status))
    {
        AcpiOsPrintf ("Could Not get pathname for object %p\n",
            ObjHandle);
        return (AE_OK);
    }

    Status = AcpiGetObjectInfo (ObjHandle, &Info);
    if (ACPI_FAILURE (Status))
    {
        return (AE_OK);
    }

    /* Display the full path */

    AcpiOsPrintf ("%-32s Type %X", (char *) Buffer.Pointer, Node->Type);
    ACPI_FREE (Buffer.Pointer);

    if (Info->Flags & ACPI_PCI_ROOT_BRIDGE)
    {
        AcpiOsPrintf ("  - Is PCI Root Bridge");
    }
    AcpiOsPrintf ("\n");

    /* _PRT info */

    AcpiOsPrintf ("_PRT: %p\n", TempNode);

    /* Dump _ADR, _HID, _UID, _CID */

    if (Info->Valid & ACPI_VALID_ADR)
    {
        AcpiOsPrintf ("_ADR: %8.8X%8.8X\n",
            ACPI_FORMAT_UINT64 (Info->Address));
    }
    else
    {
        AcpiOsPrintf ("_ADR: <Not Present>\n");
    }

    if (Info->Valid & ACPI_VALID_HID)
    {
        AcpiOsPrintf ("_HID: %s\n", Info->HardwareId.String);
    }
    else
    {
        AcpiOsPrintf ("_HID: <Not Present>\n");
    }

    if (Info->Valid & ACPI_VALID_UID)
    {
        AcpiOsPrintf ("_UID: %s\n", Info->UniqueId.String);
    }
    else
    {
        AcpiOsPrintf ("_UID: <Not Present>\n");
    }

    if (Info->Valid & ACPI_VALID_CID)
    {
        for (i = 0; i < Info->CompatibleIdList.Count; i++)
        {
            AcpiOsPrintf ("_CID: %s\n",
                Info->CompatibleIdList.Ids[i].String);
        }
    }
    else
    {
        AcpiOsPrintf ("_CID: <Not Present>\n");
    }

    ACPI_FREE (Info);
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbGetBusInfo
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display info about system buses.
 *
 ******************************************************************************/

void
AcpiDbGetBusInfo (
    void)
{
    /* Search all nodes in namespace */

    (void) AcpiWalkNamespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
        ACPI_UINT32_MAX, AcpiDbBusWalk, NULL, NULL, NULL);
}
