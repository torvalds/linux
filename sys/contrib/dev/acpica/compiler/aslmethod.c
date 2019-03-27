/******************************************************************************
 *
 * Module Name: aslmethod.c - Control method analysis walk
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
#include <contrib/dev/acpica/include/acparser.h>
#include <contrib/dev/acpica/include/amlcode.h>


#define _COMPONENT          ACPI_COMPILER
        ACPI_MODULE_NAME    ("aslmethod")


/* Local prototypes */

static void
MtCheckNamedObjectInMethod (
    ACPI_PARSE_OBJECT       *Op,
    ASL_METHOD_INFO         *MethodInfo);


/*******************************************************************************
 *
 * FUNCTION:    MtMethodAnalysisWalkBegin
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Descending callback for the analysis walk. Check methods for:
 *              1) Initialized local variables
 *              2) Valid arguments
 *              3) Return types
 *
 ******************************************************************************/

ACPI_STATUS
MtMethodAnalysisWalkBegin (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{
    ASL_ANALYSIS_WALK_INFO  *WalkInfo = (ASL_ANALYSIS_WALK_INFO *) Context;
    ASL_METHOD_INFO         *MethodInfo = WalkInfo->MethodStack;
    ACPI_PARSE_OBJECT       *Next;
    UINT32                  RegisterNumber;
    UINT32                  i;
    char                    LocalName[] = "Local0";
    char                    ArgName[] = "Arg0";
    ACPI_PARSE_OBJECT       *ArgNode;
    ACPI_PARSE_OBJECT       *NextType;
    ACPI_PARSE_OBJECT       *NextParamType;
    UINT8                   ActualArgs = 0;


    /* Build cross-reference output file if requested */

    if (AslGbl_CrossReferenceOutput)
    {
        OtXrefWalkPart1 (Op, Level, MethodInfo);
    }

    switch (Op->Asl.ParseOpcode)
    {
    case PARSEOP_METHOD:

        AslGbl_TotalMethods++;

        /* Create and init method info */

        MethodInfo = UtLocalCalloc (sizeof (ASL_METHOD_INFO));
        MethodInfo->Next = WalkInfo->MethodStack;
        MethodInfo->Op = Op;

        WalkInfo->MethodStack = MethodInfo;

        /*
         * Special handling for _PSx methods. Dependency rules (same scope):
         *
         * 1) _PS0 - One of these must exist: _PS1, _PS2, _PS3
         * 2) _PS1/_PS2/_PS3: A _PS0 must exist
         */
        if (ACPI_COMPARE_NAME (METHOD_NAME__PS0, Op->Asl.NameSeg))
        {
            /* For _PS0, one of _PS1/_PS2/_PS3 must exist */

            if ((!ApFindNameInScope (METHOD_NAME__PS1, Op)) &&
                (!ApFindNameInScope (METHOD_NAME__PS2, Op)) &&
                (!ApFindNameInScope (METHOD_NAME__PS3, Op)))
            {
                AslError (ASL_WARNING, ASL_MSG_MISSING_DEPENDENCY, Op,
                    "_PS0 requires one of _PS1/_PS2/_PS3 in same scope");
            }
        }
        else if (
            ACPI_COMPARE_NAME (METHOD_NAME__PS1, Op->Asl.NameSeg) ||
            ACPI_COMPARE_NAME (METHOD_NAME__PS2, Op->Asl.NameSeg) ||
            ACPI_COMPARE_NAME (METHOD_NAME__PS3, Op->Asl.NameSeg))
        {
            /* For _PS1/_PS2/_PS3, a _PS0 must exist */

            if (!ApFindNameInScope (METHOD_NAME__PS0, Op))
            {
                sprintf (AslGbl_MsgBuffer,
                    "%4.4s requires _PS0 in same scope", Op->Asl.NameSeg);

                AslError (ASL_WARNING, ASL_MSG_MISSING_DEPENDENCY, Op,
                    AslGbl_MsgBuffer);
            }
        }

        /* Get the name node */

        Next = Op->Asl.Child;

        /* Get the NumArguments node */

        Next = Next->Asl.Next;
        MethodInfo->NumArguments = (UINT8)
            (((UINT8) Next->Asl.Value.Integer) & 0x07);

        /* Get the SerializeRule and SyncLevel nodes, ignored here */

        Next = Next->Asl.Next;
        MethodInfo->ShouldBeSerialized = (UINT8) Next->Asl.Value.Integer;

        Next = Next->Asl.Next;
        ArgNode = Next;

        /* Get the ReturnType node */

        Next = Next->Asl.Next;

        NextType = Next->Asl.Child;
        while (NextType)
        {
            /* Get and map each of the ReturnTypes */

            MethodInfo->ValidReturnTypes |= AnMapObjTypeToBtype (NextType);
            NextType->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;
            NextType = NextType->Asl.Next;
        }

        /* Get the ParameterType node */

        Next = Next->Asl.Next;

        NextType = Next->Asl.Child;
        while (NextType)
        {
            if (NextType->Asl.ParseOpcode == PARSEOP_DEFAULT_ARG)
            {
                NextParamType = NextType->Asl.Child;
                while (NextParamType)
                {
                    MethodInfo->ValidArgTypes[ActualArgs] |=
                        AnMapObjTypeToBtype (NextParamType);

                    NextParamType->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;
                    NextParamType = NextParamType->Asl.Next;
                }
            }
            else
            {
                MethodInfo->ValidArgTypes[ActualArgs] =
                    AnMapObjTypeToBtype (NextType);

                NextType->Asl.ParseOpcode = PARSEOP_DEFAULT_ARG;
                ActualArgs++;
            }

            NextType = NextType->Asl.Next;
        }

        if ((MethodInfo->NumArguments) &&
            (MethodInfo->NumArguments != ActualArgs))
        {
            /* error: Param list did not match number of args */
        }

        /* Allow numarguments == 0 for Function() */

        if ((!MethodInfo->NumArguments) && (ActualArgs))
        {
            MethodInfo->NumArguments = ActualArgs;
            ArgNode->Asl.Value.Integer |= ActualArgs;
        }

        /*
         * Actual arguments are initialized at method entry.
         * All other ArgX "registers" can be used as locals, so we
         * track their initialization.
         */
        for (i = 0; i < MethodInfo->NumArguments; i++)
        {
            MethodInfo->ArgInitialized[i] = TRUE;
        }
        break;

    case PARSEOP_METHODCALL:

        /* Check for a recursive method call */

        if (MethodInfo &&
           (Op->Asl.Node == MethodInfo->Op->Asl.Node))
        {
            if (MethodInfo->CreatesNamedObjects)
            {
                /*
                 * This is an error, as it will fail at runtime on all ACPI
                 * implementations. Any named object declarations will be
                 * executed twice, causing failure the second time. Note,
                 * this is independent of whether the method is declared
                 * Serialized, because the same thread is attempting to
                 * reenter the method, and this will always succeed.
                 */
                AslDualParseOpError (ASL_ERROR, ASL_MSG_ILLEGAL_RECURSION, Op,
                    Op->Asl.Value.String, ASL_MSG_FOUND_HERE, MethodInfo->Op,
                    MethodInfo->Op->Asl.ExternalName);
            }
            else
            {
                /* Method does not create objects, issue a remark */

                AslError (ASL_REMARK, ASL_MSG_RECURSION, Op, Op->Asl.ExternalName);
            }
        }
        break;

    case PARSEOP_LOCAL0:
    case PARSEOP_LOCAL1:
    case PARSEOP_LOCAL2:
    case PARSEOP_LOCAL3:
    case PARSEOP_LOCAL4:
    case PARSEOP_LOCAL5:
    case PARSEOP_LOCAL6:
    case PARSEOP_LOCAL7:

        if (!MethodInfo)
        {
            /*
             * Local was used outside a control method, or there was an error
             * in the method declaration.
             */
            AslError (ASL_REMARK, ASL_MSG_LOCAL_OUTSIDE_METHOD,
                Op, Op->Asl.ExternalName);
            return (AE_ERROR);
        }

        RegisterNumber = (Op->Asl.AmlOpcode & 0x0007);

        /*
         * If the local is being used as a target, mark the local
         * initialized
         */
        if (Op->Asl.CompileFlags & OP_IS_TARGET)
        {
            MethodInfo->LocalInitialized[RegisterNumber] = TRUE;
        }

        /*
         * Otherwise, this is a reference, check if the local
         * has been previously initialized.
         *
         * The only operator that accepts an uninitialized value is ObjectType()
         */
        else if ((!MethodInfo->LocalInitialized[RegisterNumber]) &&
                 (Op->Asl.Parent->Asl.ParseOpcode != PARSEOP_OBJECTTYPE))
        {
            LocalName[strlen (LocalName) -1] = (char) (RegisterNumber + 0x30);
            AslError (ASL_ERROR, ASL_MSG_LOCAL_INIT, Op, LocalName);
        }
        break;

    case PARSEOP_ARG0:
    case PARSEOP_ARG1:
    case PARSEOP_ARG2:
    case PARSEOP_ARG3:
    case PARSEOP_ARG4:
    case PARSEOP_ARG5:
    case PARSEOP_ARG6:

        if (!MethodInfo)
        {
            /*
             * Arg was used outside a control method, or there was an error
             * in the method declaration.
             */
            AslError (ASL_REMARK, ASL_MSG_LOCAL_OUTSIDE_METHOD,
                Op, Op->Asl.ExternalName);
            return (AE_ERROR);
        }

        RegisterNumber = (Op->Asl.AmlOpcode & 0x000F) - 8;
        ArgName[strlen (ArgName) -1] = (char) (RegisterNumber + 0x30);

        /*
         * If the Arg is being used as a target, mark the local
         * initialized
         */
        if (Op->Asl.CompileFlags & OP_IS_TARGET)
        {
            MethodInfo->ArgInitialized[RegisterNumber] = TRUE;
        }

        /*
         * Otherwise, this is a reference, check if the Arg
         * has been previously initialized.
         *
         * The only operator that accepts an uninitialized value is ObjectType()
         */
        else if ((!MethodInfo->ArgInitialized[RegisterNumber]) &&
            (Op->Asl.Parent->Asl.ParseOpcode != PARSEOP_OBJECTTYPE))
        {
            AslError (ASL_ERROR, ASL_MSG_ARG_INIT, Op, ArgName);
        }

        /* Flag this arg if it is not a "real" argument to the method */

        if (RegisterNumber >= MethodInfo->NumArguments)
        {
            AslError (ASL_REMARK, ASL_MSG_NOT_PARAMETER, Op, ArgName);
        }
        break;

    case PARSEOP_RETURN:

        if (!MethodInfo)
        {
            /*
             * Probably was an error in the method declaration,
             * no additional error here
             */
            ACPI_WARNING ((AE_INFO, "%p, No parent method", Op));
            return (AE_ERROR);
        }

        /*
         * A child indicates a possible return value. A simple Return or
         * Return() is marked with OP_IS_NULL_RETURN by the parser so
         * that it is not counted as a "real" return-with-value, although
         * the AML code that is actually emitted is Return(0). The AML
         * definition of Return has a required parameter, so we are
         * forced to convert a null return to Return(0).
         */
        if ((Op->Asl.Child) &&
            (Op->Asl.Child->Asl.ParseOpcode != PARSEOP_DEFAULT_ARG) &&
            (!(Op->Asl.Child->Asl.CompileFlags & OP_IS_NULL_RETURN)))
        {
            MethodInfo->NumReturnWithValue++;
        }
        else
        {
            MethodInfo->NumReturnNoValue++;
        }
        break;

    case PARSEOP_BREAK:
    case PARSEOP_CONTINUE:

        Next = Op->Asl.Parent;
        while (Next)
        {
            if (Next->Asl.ParseOpcode == PARSEOP_WHILE)
            {
                break;
            }
            Next = Next->Asl.Parent;
        }

        if (!Next)
        {
            AslError (ASL_ERROR, ASL_MSG_NO_WHILE, Op, NULL);
        }
        break;

    case PARSEOP_STALL:

        /* We can range check if the argument is an integer */

        if ((Op->Asl.Child->Asl.ParseOpcode == PARSEOP_INTEGER) &&
            (Op->Asl.Child->Asl.Value.Integer > ACPI_UINT8_MAX))
        {
            AslError (ASL_ERROR, ASL_MSG_INVALID_TIME, Op, NULL);
        }
        break;

    case PARSEOP_DEVICE:

        if (!ApFindNameInDeviceTree (METHOD_NAME__HID, Op) &&
            !ApFindNameInDeviceTree (METHOD_NAME__ADR, Op))
        {
            AslError (ASL_WARNING, ASL_MSG_MISSING_DEPENDENCY, Op,
                "Device object requires a _HID or _ADR in same scope");
        }
        break;

    case PARSEOP_EVENT:
    case PARSEOP_MUTEX:
    case PARSEOP_OPERATIONREGION:
    case PARSEOP_POWERRESOURCE:
    case PARSEOP_PROCESSOR:
    case PARSEOP_THERMALZONE:

        /*
         * The first operand is a name to be created in the namespace.
         * Check against the reserved list.
         */
        i = ApCheckForPredefinedName (Op, Op->Asl.NameSeg);
        if (i < ACPI_VALID_RESERVED_NAME_MAX)
        {
            AslError (ASL_ERROR, ASL_MSG_RESERVED_USE,
                Op, Op->Asl.ExternalName);
        }
        break;

    case PARSEOP_NAME:

        /* Typecheck any predefined names statically defined with Name() */

        ApCheckForPredefinedObject (Op, Op->Asl.NameSeg);

        /* Special typechecking for _HID */

        if (!strcmp (METHOD_NAME__HID, Op->Asl.NameSeg))
        {
            Next = Op->Asl.Child->Asl.Next;
            AnCheckId (Next, ASL_TYPE_HID);
        }

        /* Special typechecking for _CID */

        else if (!strcmp (METHOD_NAME__CID, Op->Asl.NameSeg))
        {
            Next = Op->Asl.Child->Asl.Next;

            if ((Next->Asl.ParseOpcode == PARSEOP_PACKAGE) ||
                (Next->Asl.ParseOpcode == PARSEOP_VAR_PACKAGE))
            {
                Next = Next->Asl.Child;
                while (Next)
                {
                    AnCheckId (Next, ASL_TYPE_CID);
                    Next = Next->Asl.Next;
                }
            }
            else
            {
                AnCheckId (Next, ASL_TYPE_CID);
            }
        }

        break;

    default:

        break;
    }

    /* Check for named object creation within a non-serialized method */

    MtCheckNamedObjectInMethod (Op, MethodInfo);
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    MtCheckNamedObjectInMethod
 *
 * PARAMETERS:  Op                  - Current parser op
 *              MethodInfo          - Info for method being parsed
 *
 * RETURN:      None
 *
 * DESCRIPTION: Detect if a non-serialized method is creating a named object,
 *              which could possibly cause problems if two threads execute
 *              the method concurrently. Emit a remark in this case.
 *
 ******************************************************************************/

static void
MtCheckNamedObjectInMethod (
    ACPI_PARSE_OBJECT       *Op,
    ASL_METHOD_INFO         *MethodInfo)
{
    const ACPI_OPCODE_INFO  *OpInfo;


    /* We don't care about actual method declarations or scopes */

    if ((Op->Asl.AmlOpcode == AML_METHOD_OP) ||
        (Op->Asl.AmlOpcode == AML_SCOPE_OP))
    {
        return;
    }

    /* Determine if we are creating a named object within a method */

    if (!MethodInfo)
    {
        return;
    }

    OpInfo = AcpiPsGetOpcodeInfo (Op->Asl.AmlOpcode);
    if (OpInfo->Class == AML_CLASS_NAMED_OBJECT)
    {
        /*
         * 1) Mark the method as a method that creates named objects.
         *
         * 2) If the method is non-serialized, emit a remark that the method
         * should be serialized.
         *
         * Reason: If a thread blocks within the method for any reason, and
         * another thread enters the method, the method will fail because
         * an attempt will be made to create the same object twice.
         */
        MethodInfo->CreatesNamedObjects = TRUE;
        if (!MethodInfo->ShouldBeSerialized)
        {
            AslError (ASL_REMARK, ASL_MSG_SERIALIZED_REQUIRED, MethodInfo->Op,
                "due to creation of named objects within");

            /* Emit message only ONCE per method */

            MethodInfo->ShouldBeSerialized = TRUE;
        }
    }
}


/*******************************************************************************
 *
 * FUNCTION:    MtMethodAnalysisWalkEnd
 *
 * PARAMETERS:  ASL_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Ascending callback for analysis walk. Complete method
 *              return analysis.
 *
 ******************************************************************************/

ACPI_STATUS
MtMethodAnalysisWalkEnd (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  Level,
    void                    *Context)
{
    ASL_ANALYSIS_WALK_INFO  *WalkInfo = (ASL_ANALYSIS_WALK_INFO *) Context;
    ASL_METHOD_INFO         *MethodInfo = WalkInfo->MethodStack;


    switch (Op->Asl.ParseOpcode)
    {
    case PARSEOP_METHOD:
    case PARSEOP_RETURN:

        if (!MethodInfo)
        {
            printf ("No method info for method! [%s]\n", Op->Asl.Namepath);
            AslError (ASL_ERROR, ASL_MSG_COMPILER_INTERNAL, Op,
                "No method info for this method");

            CmCleanupAndExit ();
            return (AE_AML_INTERNAL);
        }
        break;

    default:

        break;
    }

    switch (Op->Asl.ParseOpcode)
    {
    case PARSEOP_METHOD:

        WalkInfo->MethodStack = MethodInfo->Next;

        /*
         * Check if there is no return statement at the end of the
         * method AND we can actually get there -- i.e., the execution
         * of the method can possibly terminate without a return statement.
         */
        if ((!AnLastStatementIsReturn (Op)) &&
            (!(Op->Asl.CompileFlags & OP_HAS_NO_EXIT)))
        {
            /*
             * No return statement, and execution can possibly exit
             * via this path. This is equivalent to Return ()
             */
            MethodInfo->NumReturnNoValue++;
        }

        /*
         * Check for case where some return statements have a return value
         * and some do not. Exit without a return statement is a return with
         * no value
         */
        if (MethodInfo->NumReturnNoValue &&
            MethodInfo->NumReturnWithValue)
        {
            AslError (ASL_WARNING, ASL_MSG_RETURN_TYPES, Op,
                Op->Asl.ExternalName);
        }

        /*
         * If there are any RETURN() statements with no value, or there is a
         * control path that allows the method to exit without a return value,
         * we mark the method as a method that does not return a value. This
         * knowledge can be used to check method invocations that expect a
         * returned value.
         */
        if (MethodInfo->NumReturnNoValue)
        {
            if (MethodInfo->NumReturnWithValue)
            {
                Op->Asl.CompileFlags |= OP_METHOD_SOME_NO_RETVAL;
            }
            else
            {
                Op->Asl.CompileFlags |= OP_METHOD_NO_RETVAL;
            }
        }

        /*
         * Check predefined method names for correct return behavior
         * and correct number of arguments. Also, some special checks
         * For GPE and _REG methods.
         */
        if (ApCheckForPredefinedMethod (Op, MethodInfo))
        {
            /* Special check for two names like _L01 and _E01 in same scope */

            ApCheckForGpeNameConflict (Op);

            /*
             * Special check for _REG: Must have an operation region definition
             * within the same scope!
             */
            ApCheckRegMethod (Op);
        }

        ACPI_FREE (MethodInfo);
        break;

    case PARSEOP_NAME:

         /* Special check for two names like _L01 and _E01 in same scope */

        ApCheckForGpeNameConflict (Op);
        break;

    case PARSEOP_RETURN:

        /*
         * If the parent is a predefined method name, attempt to typecheck
         * the return value. Only static types can be validated.
         */
        ApCheckPredefinedReturnValue (Op, MethodInfo);

        /*
         * The parent block does not "exit" and continue execution -- the
         * method is terminated here with the Return() statement.
         */
        Op->Asl.Parent->Asl.CompileFlags |= OP_HAS_NO_EXIT;

        /* Used in the "typing" pass later */

        Op->Asl.ParentMethod = MethodInfo->Op;

        /*
         * If there is a peer node after the return statement, then this
         * node is unreachable code -- i.e., it won't be executed because of
         * the preceding Return() statement.
         */
        if (Op->Asl.Next)
        {
            AslError (ASL_WARNING, ASL_MSG_UNREACHABLE_CODE,
                Op->Asl.Next, NULL);
        }
        break;

    case PARSEOP_IF:

        if ((Op->Asl.CompileFlags & OP_HAS_NO_EXIT) &&
            (Op->Asl.Next) &&
            (Op->Asl.Next->Asl.ParseOpcode == PARSEOP_ELSE))
        {
            /*
             * This IF has a corresponding ELSE. The IF block has no exit,
             * (it contains an unconditional Return)
             * mark the ELSE block to remember this fact.
             */
            Op->Asl.Next->Asl.CompileFlags |= OP_IF_HAS_NO_EXIT;
        }
        break;

    case PARSEOP_ELSE:

        if ((Op->Asl.CompileFlags & OP_HAS_NO_EXIT) &&
            (Op->Asl.CompileFlags & OP_IF_HAS_NO_EXIT))
        {
            /*
             * This ELSE block has no exit and the corresponding IF block
             * has no exit either. Therefore, the parent node has no exit.
             */
            Op->Asl.Parent->Asl.CompileFlags |= OP_HAS_NO_EXIT;
        }
        break;


    default:

        if ((Op->Asl.CompileFlags & OP_HAS_NO_EXIT) &&
            (Op->Asl.Parent))
        {
            /* If this node has no exit, then the parent has no exit either */

            Op->Asl.Parent->Asl.CompileFlags |= OP_HAS_NO_EXIT;
        }
        break;
    }

    return (AE_OK);
}
