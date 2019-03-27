/******************************************************************************
 *
 * Module Name: aslbtypes - Support for bitfield types
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
#include "aslcompiler.y.h"
#include <contrib/dev/acpica/include/amlcode.h>


#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslbtypes")

/* Local prototypes */

static UINT32
AnMapEtypeToBtype (
    UINT32                  Etype);


/*******************************************************************************
 *
 * FUNCTION:    AnMapArgTypeToBtype
 *
 * PARAMETERS:  ArgType             - The ARGI required type(s) for this
 *                                    argument, from the opcode info table
 *
 * RETURN:      The corresponding Bit-encoded types
 *
 * DESCRIPTION: Convert an encoded ARGI required argument type code into a
 *              bitfield type code. Implements the implicit source conversion
 *              rules.
 *
 ******************************************************************************/

UINT32
AnMapArgTypeToBtype (
    UINT32                  ArgType)
{

    switch (ArgType)
    {
    /* Simple types */

    case ARGI_ANYTYPE:

        return (ACPI_BTYPE_OBJECTS_AND_REFS);

    case ARGI_PACKAGE:

        return (ACPI_BTYPE_PACKAGE);

    case ARGI_EVENT:

        return (ACPI_BTYPE_EVENT);

    case ARGI_MUTEX:

        return (ACPI_BTYPE_MUTEX);

    case ARGI_DDBHANDLE:
        /*
         * DDBHandleObject := SuperName
         * ACPI_BTYPE_REFERENCE_OBJECT:
         *      Index reference as parameter of Load/Unload
         */
        return (ACPI_BTYPE_DDB_HANDLE | ACPI_BTYPE_REFERENCE_OBJECT);

    /* Interchangeable types */
    /*
     * Source conversion rules:
     * Integer, String, and Buffer are all interchangeable
     */
    case ARGI_INTEGER:
    case ARGI_STRING:
    case ARGI_BUFFER:
    case ARGI_BUFFER_OR_STRING:
    case ARGI_COMPUTEDATA:

        return (ACPI_BTYPE_COMPUTE_DATA);

    /* References */

    case ARGI_INTEGER_REF:

        return (ACPI_BTYPE_INTEGER);

    case ARGI_OBJECT_REF:

        return (ACPI_BTYPE_ALL_OBJECTS);

    case ARGI_DEVICE_REF:

        return (ACPI_BTYPE_DEVICE_OBJECTS);

    case ARGI_REFERENCE:

        return (ACPI_BTYPE_NAMED_REFERENCE); /* Name or Namestring */

    case ARGI_TARGETREF:

        /*
         * Target operand for most math and logic operators.
         * Package objects not allowed as target.
         */
        return (ACPI_BTYPE_COMPUTE_DATA | ACPI_BTYPE_DEBUG_OBJECT |
            ACPI_BTYPE_REFERENCE_OBJECT);

    case ARGI_STORE_TARGET:

        /* Special target for Store(), includes packages */

        return (ACPI_BTYPE_DATA | ACPI_BTYPE_DEBUG_OBJECT |
            ACPI_BTYPE_REFERENCE_OBJECT);

    case ARGI_FIXED_TARGET:
    case ARGI_SIMPLE_TARGET:

        return (ACPI_BTYPE_OBJECTS_AND_REFS);

    /* Complex types */

    case ARGI_DATAOBJECT:
        /*
         * Buffer, string, package or reference to a Op -
         * Used only by SizeOf operator
         */
        return (ACPI_BTYPE_STRING | ACPI_BTYPE_BUFFER |
            ACPI_BTYPE_PACKAGE | ACPI_BTYPE_REFERENCE_OBJECT);

    case ARGI_COMPLEXOBJ:

        /* Buffer, String, or package */

        return (ACPI_BTYPE_STRING | ACPI_BTYPE_BUFFER |
            ACPI_BTYPE_PACKAGE);

    case ARGI_REF_OR_STRING:

        /* Used by DeRefOf operator only */

        return (ACPI_BTYPE_STRING | ACPI_BTYPE_REFERENCE_OBJECT);

    case ARGI_REGION_OR_BUFFER:

        /* Used by Load() only. Allow buffers in addition to regions/fields */

        return (ACPI_BTYPE_REGION | ACPI_BTYPE_BUFFER |
            ACPI_BTYPE_FIELD_UNIT);

    case ARGI_DATAREFOBJ:

        /* Used by Store() only, as the source operand */

        return (ACPI_BTYPE_DATA_REFERENCE | ACPI_BTYPE_REFERENCE_OBJECT);

    default:

        break;
    }

    return (ACPI_BTYPE_OBJECTS_AND_REFS);
}


/*******************************************************************************
 *
 * FUNCTION:    AnMapEtypeToBtype
 *
 * PARAMETERS:  Etype               - Encoded ACPI Type
 *
 * RETURN:      Btype corresponding to the Etype
 *
 * DESCRIPTION: Convert an encoded ACPI type to a bitfield type applying the
 *              operand conversion rules. In other words, returns the type(s)
 *              this Etype is implicitly converted to during interpretation.
 *
 ******************************************************************************/

static UINT32
AnMapEtypeToBtype (
    UINT32                  Etype)
{

    if (Etype == ACPI_TYPE_ANY)
    {
        return (ACPI_BTYPE_OBJECTS_AND_REFS);
    }

    /* Try the standard ACPI data types */

    if (Etype <= ACPI_TYPE_EXTERNAL_MAX)
    {
        /*
         * This switch statement implements the allowed operand conversion
         * rules as per the "ASL Data Types" section of the ACPI
         * specification.
         */
        switch (Etype)
        {
        case ACPI_TYPE_INTEGER:

            return (ACPI_BTYPE_COMPUTE_DATA | ACPI_BTYPE_DDB_HANDLE);

        case ACPI_TYPE_STRING:
        case ACPI_TYPE_BUFFER:

            return (ACPI_BTYPE_COMPUTE_DATA);

        case ACPI_TYPE_PACKAGE:

            return (ACPI_BTYPE_PACKAGE);

        case ACPI_TYPE_FIELD_UNIT:

            return (ACPI_BTYPE_COMPUTE_DATA | ACPI_BTYPE_FIELD_UNIT);

        case ACPI_TYPE_BUFFER_FIELD:

            return (ACPI_BTYPE_COMPUTE_DATA | ACPI_BTYPE_BUFFER_FIELD);

        case ACPI_TYPE_DDB_HANDLE:

            return (ACPI_BTYPE_INTEGER | ACPI_BTYPE_DDB_HANDLE);

        case ACPI_TYPE_DEBUG_OBJECT:

            /* Cannot be used as a source operand */

            return (0);

        default:

            return (1 << (Etype - 1));
        }
    }

    /* Try the internal data types */

    switch (Etype)
    {
    case ACPI_TYPE_LOCAL_REGION_FIELD:
    case ACPI_TYPE_LOCAL_BANK_FIELD:
    case ACPI_TYPE_LOCAL_INDEX_FIELD:

        /* Named fields can be either Integer/Buffer/String */

        return (ACPI_BTYPE_COMPUTE_DATA | ACPI_BTYPE_FIELD_UNIT);

    case ACPI_TYPE_LOCAL_ALIAS:

        return (ACPI_BTYPE_INTEGER);


    case ACPI_TYPE_LOCAL_RESOURCE:
    case ACPI_TYPE_LOCAL_RESOURCE_FIELD:

        return (ACPI_BTYPE_REFERENCE_OBJECT);

    default:

        printf ("Unhandled encoded type: %X\n", Etype);
        return (0);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AnFormatBtype
 *
 * PARAMETERS:  Btype               - Bitfield of ACPI types
 *              Buffer              - Where to put the ascii string
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Convert a Btype to a string of ACPI types
 *
 ******************************************************************************/

void
AnFormatBtype (
    char                    *Buffer,
    UINT32                  Btype)
{
    UINT32                  Type;
    BOOLEAN                 First = TRUE;


    *Buffer = 0;
    if (Btype == 0)
    {
        strcat (Buffer, "NoReturnValue");
        return;
    }

    for (Type = 1; Type <= ACPI_TYPE_EXTERNAL_MAX; Type++)
    {
        if (Btype & 0x00000001)
        {
            if (!First)
            {
                strcat (Buffer, "|");
            }

            First = FALSE;
            strcat (Buffer, AcpiUtGetTypeName (Type));
        }
        Btype >>= 1;
    }

    if (Btype & 0x00000001)
    {
        if (!First)
        {
            strcat (Buffer, "|");
        }

        First = FALSE;
        strcat (Buffer, "Reference");
    }

    Btype >>= 1;
    if (Btype & 0x00000001)
    {
        if (!First)
        {
            strcat (Buffer, "|");
        }

        First = FALSE;
        strcat (Buffer, "Resource");
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AnGetBtype
 *
 * PARAMETERS:  Op                  - Parse node whose type will be returned.
 *
 * RETURN:      The Btype associated with the Op.
 *
 * DESCRIPTION: Get the (bitfield) ACPI type associated with the parse node.
 *              Handles the case where the node is a name or method call and
 *              the actual type must be obtained from the namespace node.
 *
 ******************************************************************************/

UINT32
AnGetBtype (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_NAMESPACE_NODE     *Node;
    ACPI_PARSE_OBJECT       *ReferencedNode;
    UINT32                  ThisNodeBtype = 0;


    if (!Op)
    {
        AcpiOsPrintf ("Null Op in AnGetBtype\n");
        return (ACPI_UINT32_MAX);
    }

    if ((Op->Asl.ParseOpcode == PARSEOP_NAMESEG)     ||
        (Op->Asl.ParseOpcode == PARSEOP_NAMESTRING)  ||
        (Op->Asl.ParseOpcode == PARSEOP_METHODCALL))
    {
        Node = Op->Asl.Node;
        if (!Node)
        {
            /* These are not expected to have a node at this time */

            if ((Op->Asl.Parent->Asl.ParseOpcode == PARSEOP_CREATEWORDFIELD) ||
                (Op->Asl.Parent->Asl.ParseOpcode == PARSEOP_CREATEDWORDFIELD) ||
                (Op->Asl.Parent->Asl.ParseOpcode == PARSEOP_CREATEQWORDFIELD) ||
                (Op->Asl.Parent->Asl.ParseOpcode == PARSEOP_CREATEBYTEFIELD) ||
                (Op->Asl.Parent->Asl.ParseOpcode == PARSEOP_CREATEBITFIELD) ||
                (Op->Asl.Parent->Asl.ParseOpcode == PARSEOP_CREATEFIELD)    ||
                (Op->Asl.Parent->Asl.ParseOpcode == PARSEOP_CONDREFOF))
            {
                return (ACPI_UINT32_MAX - 1);
            }

            DbgPrint (ASL_DEBUG_OUTPUT,
                "No attached Nsnode: [%s] at line %u name [%s], "
                "ignoring typecheck. Parent [%s]\n",
                Op->Asl.ParseOpName, Op->Asl.LineNumber,
                Op->Asl.ExternalName, Op->Asl.Parent->Asl.ParseOpName);
            return (ACPI_UINT32_MAX - 1);
        }

        ThisNodeBtype = AnMapEtypeToBtype (Node->Type);
        if (!ThisNodeBtype)
        {
            AslError (ASL_ERROR, ASL_MSG_COMPILER_INTERNAL, Op,
                "could not map type");
        }

        if (Op->Asl.ParseOpcode == PARSEOP_METHODCALL)
        {
            ReferencedNode = Node->Op;
            if (!ReferencedNode)
            {
                /* Check for an internal method */

                if (AnIsInternalMethod (Op))
                {
                    return (AnGetInternalMethodReturnType (Op));
                }

                AslError (ASL_ERROR, ASL_MSG_COMPILER_INTERNAL, Op,
                    "null Op pointer");
                return (ACPI_UINT32_MAX);
            }

            if (ReferencedNode->Asl.CompileFlags & OP_METHOD_TYPED)
            {
                ThisNodeBtype = ReferencedNode->Asl.AcpiBtype;
            }
            else
            {
                return (ACPI_UINT32_MAX -1);
            }
        }
    }
    else
    {
        ThisNodeBtype = Op->Asl.AcpiBtype;
    }

    return (ThisNodeBtype);
}


/*******************************************************************************
 *
 * FUNCTION:    AnMapObjTypeToBtype
 *
 * PARAMETERS:  Op                  - A parse node
 *
 * RETURN:      A Btype
 *
 * DESCRIPTION: Map object to the associated "Btype"
 *
 ******************************************************************************/

UINT32
AnMapObjTypeToBtype (
    ACPI_PARSE_OBJECT       *Op)
{

    switch (Op->Asl.ParseOpcode)
    {
    case PARSEOP_OBJECTTYPE_BFF:        /* "BuffFieldObj" */

        return (ACPI_BTYPE_BUFFER_FIELD);

    case PARSEOP_OBJECTTYPE_BUF:        /* "BuffObj" */

        return (ACPI_BTYPE_BUFFER);

    case PARSEOP_OBJECTTYPE_DDB:        /* "DDBHandleObj" */

        return (ACPI_BTYPE_DDB_HANDLE);

    case PARSEOP_OBJECTTYPE_DEV:        /* "DeviceObj" */

        return (ACPI_BTYPE_DEVICE);

    case PARSEOP_OBJECTTYPE_EVT:        /* "EventObj" */

        return (ACPI_BTYPE_EVENT);

    case PARSEOP_OBJECTTYPE_FLD:        /* "FieldUnitObj" */

        return (ACPI_BTYPE_FIELD_UNIT);

    case PARSEOP_OBJECTTYPE_INT:        /* "IntObj" */

        return (ACPI_BTYPE_INTEGER);

    case PARSEOP_OBJECTTYPE_MTH:        /* "MethodObj" */

        return (ACPI_BTYPE_METHOD);

    case PARSEOP_OBJECTTYPE_MTX:        /* "MutexObj" */

        return (ACPI_BTYPE_MUTEX);

    case PARSEOP_OBJECTTYPE_OPR:        /* "OpRegionObj" */

        return (ACPI_BTYPE_REGION);

    case PARSEOP_OBJECTTYPE_PKG:        /* "PkgObj" */

        return (ACPI_BTYPE_PACKAGE);

    case PARSEOP_OBJECTTYPE_POW:        /* "PowerResObj" */

        return (ACPI_BTYPE_POWER);

    case PARSEOP_OBJECTTYPE_STR:        /* "StrObj" */

        return (ACPI_BTYPE_STRING);

    case PARSEOP_OBJECTTYPE_THZ:        /* "ThermalZoneObj" */

        return (ACPI_BTYPE_THERMAL);

    case PARSEOP_OBJECTTYPE_UNK:        /* "UnknownObj" */

        return (ACPI_BTYPE_OBJECTS_AND_REFS);

    default:

        return (0);
    }
}


#ifdef ACPI_OBSOLETE_FUNCTIONS
/*******************************************************************************
 *
 * FUNCTION:    AnMapBtypeToEtype
 *
 * PARAMETERS:  Btype               - Bitfield of ACPI types
 *
 * RETURN:      The Etype corresponding the the Btype
 *
 * DESCRIPTION: Convert a bitfield type to an encoded type
 *
 ******************************************************************************/

UINT32
AnMapBtypeToEtype (
    UINT32              Btype)
{
    UINT32              i;
    UINT32              Etype;


    if (Btype == 0)
    {
        return (0);
    }

    Etype = 1;
    for (i = 1; i < Btype; i *= 2)
    {
        Etype++;
    }

    return (Etype);
}
#endif
