/*******************************************************************************
 *
 * Module Name: dbinput - user front-end to the AML debugger
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
#include <contrib/dev/acpica/include/acdebug.h>

#ifdef ACPI_APPLICATION
#include <contrib/dev/acpica/include/acapps.h>
#endif

#define _COMPONENT          ACPI_CA_DEBUGGER
        ACPI_MODULE_NAME    ("dbinput")


/* Local prototypes */

static UINT32
AcpiDbGetLine (
    char                    *InputBuffer);

static UINT32
AcpiDbMatchCommand (
    char                    *UserCommand);

static void
AcpiDbDisplayCommandInfo (
    const char              *Command,
    BOOLEAN                 DisplayAll);

static void
AcpiDbDisplayHelp (
    char                    *Command);

static BOOLEAN
AcpiDbMatchCommandHelp (
    const char                  *Command,
    const ACPI_DB_COMMAND_HELP  *Help);


/*
 * Top-level debugger commands.
 *
 * This list of commands must match the string table below it
 */
enum AcpiExDebuggerCommands
{
    CMD_NOT_FOUND = 0,
    CMD_NULL,
    CMD_ALLOCATIONS,
    CMD_ARGS,
    CMD_ARGUMENTS,
    CMD_BREAKPOINT,
    CMD_BUSINFO,
    CMD_CALL,
    CMD_DEBUG,
    CMD_DISASSEMBLE,
    CMD_DISASM,
    CMD_DUMP,
    CMD_EVALUATE,
    CMD_EXECUTE,
    CMD_EXIT,
    CMD_FIND,
    CMD_GO,
    CMD_HANDLERS,
    CMD_HELP,
    CMD_HELP2,
    CMD_HISTORY,
    CMD_HISTORY_EXE,
    CMD_HISTORY_LAST,
    CMD_INFORMATION,
    CMD_INTEGRITY,
    CMD_INTO,
    CMD_LEVEL,
    CMD_LIST,
    CMD_LOCALS,
    CMD_LOCKS,
    CMD_METHODS,
    CMD_NAMESPACE,
    CMD_NOTIFY,
    CMD_OBJECTS,
    CMD_OSI,
    CMD_OWNER,
    CMD_PATHS,
    CMD_PREDEFINED,
    CMD_PREFIX,
    CMD_QUIT,
    CMD_REFERENCES,
    CMD_RESOURCES,
    CMD_RESULTS,
    CMD_SET,
    CMD_STATS,
    CMD_STOP,
    CMD_TABLES,
    CMD_TEMPLATE,
    CMD_TRACE,
    CMD_TREE,
    CMD_TYPE,
#ifdef ACPI_APPLICATION
    CMD_ENABLEACPI,
    CMD_EVENT,
    CMD_GPE,
    CMD_GPES,
    CMD_SCI,
    CMD_SLEEP,

    CMD_CLOSE,
    CMD_LOAD,
    CMD_OPEN,
    CMD_UNLOAD,

    CMD_TERMINATE,
    CMD_BACKGROUND,
    CMD_THREADS,

    CMD_TEST,
#endif
};

#define CMD_FIRST_VALID     2


/* Second parameter is the required argument count */

static const ACPI_DB_COMMAND_INFO   AcpiGbl_DbCommands[] =
{
    {"<NOT FOUND>",  0},
    {"<NULL>",       0},
    {"ALLOCATIONS",  0},
    {"ARGS",         0},
    {"ARGUMENTS",    0},
    {"BREAKPOINT",   1},
    {"BUSINFO",      0},
    {"CALL",         0},
    {"DEBUG",        1},
    {"DISASSEMBLE",  1},
    {"DISASM",       1},
    {"DUMP",         1},
    {"EVALUATE",     1},
    {"EXECUTE",      1},
    {"EXIT",         0},
    {"FIND",         1},
    {"GO",           0},
    {"HANDLERS",     0},
    {"HELP",         0},
    {"?",            0},
    {"HISTORY",      0},
    {"!",            1},
    {"!!",           0},
    {"INFORMATION",  0},
    {"INTEGRITY",    0},
    {"INTO",         0},
    {"LEVEL",        0},
    {"LIST",         0},
    {"LOCALS",       0},
    {"LOCKS",        0},
    {"METHODS",      0},
    {"NAMESPACE",    0},
    {"NOTIFY",       2},
    {"OBJECTS",      0},
    {"OSI",          0},
    {"OWNER",        1},
    {"PATHS",        0},
    {"PREDEFINED",   0},
    {"PREFIX",       0},
    {"QUIT",         0},
    {"REFERENCES",   1},
    {"RESOURCES",    0},
    {"RESULTS",      0},
    {"SET",          3},
    {"STATS",        1},
    {"STOP",         0},
    {"TABLES",       0},
    {"TEMPLATE",     1},
    {"TRACE",        1},
    {"TREE",         0},
    {"TYPE",         1},
#ifdef ACPI_APPLICATION
    {"ENABLEACPI",   0},
    {"EVENT",        1},
    {"GPE",          1},
    {"GPES",         0},
    {"SCI",          0},
    {"SLEEP",        0},

    {"CLOSE",        0},
    {"LOAD",         1},
    {"OPEN",         1},
    {"UNLOAD",       1},

    {"TERMINATE",    0},
    {"BACKGROUND",   1},
    {"THREADS",      3},

    {"TEST",         1},
#endif
    {NULL,           0}
};

/*
 * Help for all debugger commands. First argument is the number of lines
 * of help to output for the command.
 *
 * Note: Some commands are not supported by the kernel-level version of
 * the debugger.
 */
static const ACPI_DB_COMMAND_HELP   AcpiGbl_DbCommandHelp[] =
{
    {0, "\nNamespace Access:",                  "\n"},
    {1, "  Businfo",                            "Display system bus info\n"},
    {1, "  Disassemble <Method>",               "Disassemble a control method\n"},
    {1, "  Find <AcpiName> (? is wildcard)",    "Find ACPI name(s) with wildcards\n"},
    {1, "  Integrity",                          "Validate namespace integrity\n"},
    {1, "  Methods",                            "Display list of loaded control methods\n"},
    {1, "  Namespace [Object] [Depth]",         "Display loaded namespace tree/subtree\n"},
    {1, "  Notify <Object> <Value>",            "Send a notification on Object\n"},
    {1, "  Objects [ObjectType]",               "Display summary of all objects or just given type\n"},
    {1, "  Owner <OwnerId> [Depth]",            "Display loaded namespace by object owner\n"},
    {1, "  Paths",                              "Display full pathnames of namespace objects\n"},
    {1, "  Predefined",                         "Check all predefined names\n"},
    {1, "  Prefix [<Namepath>]",                "Set or Get current execution prefix\n"},
    {1, "  References <Addr>",                  "Find all references to object at addr\n"},
    {1, "  Resources [DeviceName]",             "Display Device resources (no arg = all devices)\n"},
    {1, "  Set N <NamedObject> <Value>",        "Set value for named integer\n"},
    {1, "  Template <Object>",                  "Format/dump a Buffer/ResourceTemplate\n"},
    {1, "  Type <Object>",                      "Display object type\n"},

    {0, "\nControl Method Execution:",          "\n"},
    {1, "  Evaluate <Namepath> [Arguments]",    "Evaluate object or control method\n"},
    {1, "  Execute <Namepath> [Arguments]",     "Synonym for Evaluate\n"},
#ifdef ACPI_APPLICATION
    {1, "  Background <Namepath> [Arguments]",  "Evaluate object/method in a separate thread\n"},
    {1, "  Thread <Threads><Loops><NamePath>",  "Spawn threads to execute method(s)\n"},
#endif
    {1, "  Debug <Namepath> [Arguments]",       "Single-Step a control method\n"},
    {7, "  [Arguments] formats:",               "Control method argument formats\n"},
    {1, "     Hex Integer",                     "Integer\n"},
    {1, "     \"Ascii String\"",                "String\n"},
    {1, "     (Hex Byte List)",                 "Buffer\n"},
    {1, "         (01 42 7A BF)",               "Buffer example (4 bytes)\n"},
    {1, "     [Package Element List]",          "Package\n"},
    {1, "         [0x01 0x1234 \"string\"]",    "Package example (3 elements)\n"},

    {0, "\nMiscellaneous:",                     "\n"},
    {1, "  Allocations",                        "Display list of current memory allocations\n"},
    {2, "  Dump <Address>|<Namepath>",          "\n"},
    {0, "       [Byte|Word|Dword|Qword]",       "Display ACPI objects or memory\n"},
    {1, "  Handlers",                           "Info about global handlers\n"},
    {1, "  Help [Command]",                     "This help screen or individual command\n"},
    {1, "  History",                            "Display command history buffer\n"},
    {1, "  Level <DebugLevel>] [console]",      "Get/Set debug level for file or console\n"},
    {1, "  Locks",                              "Current status of internal mutexes\n"},
    {1, "  Osi [Install|Remove <name>]",        "Display or modify global _OSI list\n"},
    {1, "  Quit or Exit",                       "Exit this command\n"},
    {8, "  Stats <SubCommand>",                 "Display namespace and memory statistics\n"},
    {1, "     Allocations",                     "Display list of current memory allocations\n"},
    {1, "     Memory",                          "Dump internal memory lists\n"},
    {1, "     Misc",                            "Namespace search and mutex stats\n"},
    {1, "     Objects",                         "Summary of namespace objects\n"},
    {1, "     Sizes",                           "Sizes for each of the internal objects\n"},
    {1, "     Stack",                           "Display CPU stack usage\n"},
    {1, "     Tables",                          "Info about current ACPI table(s)\n"},
    {1, "  Tables",                             "Display info about loaded ACPI tables\n"},
#ifdef ACPI_APPLICATION
    {1, "  Terminate",                          "Delete namespace and all internal objects\n"},
#endif
    {1, "  ! <CommandNumber>",                  "Execute command from history buffer\n"},
    {1, "  !!",                                 "Execute last command again\n"},

    {0, "\nMethod and Namespace Debugging:",    "\n"},
    {5, "  Trace <State> [<Namepath>] [Once]",  "Trace control method execution\n"},
    {1, "     Enable",                          "Enable all messages\n"},
    {1, "     Disable",                         "Disable tracing\n"},
    {1, "     Method",                          "Enable method execution messages\n"},
    {1, "     Opcode",                          "Enable opcode execution messages\n"},
    {3, "  Test <TestName>",                    "Invoke a debug test\n"},
    {1, "     Objects",                         "Read/write/compare all namespace data objects\n"},
    {1, "     Predefined",                      "Validate all ACPI predefined names (_STA, etc.)\n"},
    {1, "  Execute predefined",                 "Execute all predefined (public) methods\n"},

    {0, "\nControl Method Single-Step Execution:","\n"},
    {1, "  Arguments (or Args)",                "Display method arguments\n"},
    {1, "  Breakpoint <AmlOffset>",             "Set an AML execution breakpoint\n"},
    {1, "  Call",                               "Run to next control method invocation\n"},
    {1, "  Go",                                 "Allow method to run to completion\n"},
    {1, "  Information",                        "Display info about the current method\n"},
    {1, "  Into",                               "Step into (not over) a method call\n"},
    {1, "  List [# of Aml Opcodes]",            "Display method ASL statements\n"},
    {1, "  Locals",                             "Display method local variables\n"},
    {1, "  Results",                            "Display method result stack\n"},
    {1, "  Set <A|L> <#> <Value>",              "Set method data (Arguments/Locals)\n"},
    {1, "  Stop",                               "Terminate control method\n"},
    {1, "  Tree",                               "Display control method calling tree\n"},
    {1, "  <Enter>",                            "Single step next AML opcode (over calls)\n"},

#ifdef ACPI_APPLICATION
    {0, "\nFile Operations:",                   "\n"},
    {1, "  Close",                              "Close debug output file\n"},
    {1, "  Load <Input Filename>",              "Load ACPI table from a file\n"},
    {1, "  Open <Output Filename>",             "Open a file for debug output\n"},
    {1, "  Unload <Namepath>",                  "Unload an ACPI table via namespace object\n"},

    {0, "\nHardware Simulation:",               "\n"},
    {1, "  EnableAcpi",                         "Enable ACPI (hardware) mode\n"},
    {1, "  Event <F|G> <Value>",                "Generate AcpiEvent (Fixed/GPE)\n"},
    {1, "  Gpe <GpeNum> [GpeBlockDevice]",      "Simulate a GPE\n"},
    {1, "  Gpes",                               "Display info on all GPE devices\n"},
    {1, "  Sci",                                "Generate an SCI\n"},
    {1, "  Sleep [SleepState]",                 "Simulate sleep/wake sequence(s) (0-5)\n"},
#endif
    {0, NULL, NULL}
};


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbMatchCommandHelp
 *
 * PARAMETERS:  Command             - Command string to match
 *              Help                - Help table entry to attempt match
 *
 * RETURN:      TRUE if command matched, FALSE otherwise
 *
 * DESCRIPTION: Attempt to match a command in the help table in order to
 *              print help information for a single command.
 *
 ******************************************************************************/

static BOOLEAN
AcpiDbMatchCommandHelp (
    const char                  *Command,
    const ACPI_DB_COMMAND_HELP  *Help)
{
    char                    *Invocation = Help->Invocation;
    UINT32                  LineCount;


    /* Valid commands in the help table begin with a couple of spaces */

    if (*Invocation != ' ')
    {
        return (FALSE);
    }

    while (*Invocation == ' ')
    {
        Invocation++;
    }

    /* Match command name (full command or substring) */

    while ((*Command) && (*Invocation) && (*Invocation != ' '))
    {
        if (tolower ((int) *Command) != tolower ((int) *Invocation))
        {
            return (FALSE);
        }

        Invocation++;
        Command++;
    }

    /* Print the appropriate number of help lines */

    LineCount = Help->LineCount;
    while (LineCount)
    {
        AcpiOsPrintf ("%-38s : %s", Help->Invocation, Help->Description);
        Help++;
        LineCount--;
    }

    return (TRUE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDisplayCommandInfo
 *
 * PARAMETERS:  Command             - Command string to match
 *              DisplayAll          - Display all matching commands, or just
 *                                    the first one (substring match)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display help information for a Debugger command.
 *
 ******************************************************************************/

static void
AcpiDbDisplayCommandInfo (
    const char              *Command,
    BOOLEAN                 DisplayAll)
{
    const ACPI_DB_COMMAND_HELP  *Next;
    BOOLEAN                     Matched;


    Next = AcpiGbl_DbCommandHelp;
    while (Next->Invocation)
    {
        Matched = AcpiDbMatchCommandHelp (Command, Next);
        if (!DisplayAll && Matched)
        {
            return;
        }

        Next++;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbDisplayHelp
 *
 * PARAMETERS:  Command             - Optional command string to display help.
 *                                    if not specified, all debugger command
 *                                    help strings are displayed
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display help for a single debugger command, or all of them.
 *
 ******************************************************************************/

static void
AcpiDbDisplayHelp (
    char                    *Command)
{
    const ACPI_DB_COMMAND_HELP  *Next = AcpiGbl_DbCommandHelp;


    if (!Command)
    {
        /* No argument to help, display help for all commands */

        AcpiOsPrintf ("\nSummary of AML Debugger Commands\n\n");

        while (Next->Invocation)
        {
            AcpiOsPrintf ("%-38s%s", Next->Invocation, Next->Description);
            Next++;
        }
        AcpiOsPrintf ("\n");

    }
    else
    {
        /* Display help for all commands that match the subtring */

        AcpiDbDisplayCommandInfo (Command, TRUE);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbGetNextToken
 *
 * PARAMETERS:  String          - Command buffer
 *              Next            - Return value, end of next token
 *
 * RETURN:      Pointer to the start of the next token.
 *
 * DESCRIPTION: Command line parsing. Get the next token on the command line
 *
 ******************************************************************************/

char *
AcpiDbGetNextToken (
    char                    *String,
    char                    **Next,
    ACPI_OBJECT_TYPE        *ReturnType)
{
    char                    *Start;
    UINT32                  Depth;
    ACPI_OBJECT_TYPE        Type = ACPI_TYPE_INTEGER;


    /* At end of buffer? */

    if (!String || !(*String))
    {
        return (NULL);
    }

    /* Remove any spaces at the beginning */

    if (*String == ' ')
    {
        while (*String && (*String == ' '))
        {
            String++;
        }

        if (!(*String))
        {
            return (NULL);
        }
    }

    switch (*String)
    {
    case '"':

        /* This is a quoted string, scan until closing quote */

        String++;
        Start = String;
        Type = ACPI_TYPE_STRING;

        /* Find end of string */

        while (*String && (*String != '"'))
        {
            String++;
        }
        break;

    case '(':

        /* This is the start of a buffer, scan until closing paren */

        String++;
        Start = String;
        Type = ACPI_TYPE_BUFFER;

        /* Find end of buffer */

        while (*String && (*String != ')'))
        {
            String++;
        }
        break;

    case '[':

        /* This is the start of a package, scan until closing bracket */

        String++;
        Depth = 1;
        Start = String;
        Type = ACPI_TYPE_PACKAGE;

        /* Find end of package (closing bracket) */

        while (*String)
        {
            /* Handle String package elements */

            if (*String == '"')
            {
                /* Find end of string */

                String++;
                while (*String && (*String != '"'))
                {
                    String++;
                }
                if (!(*String))
                {
                    break;
                }
            }
            else if (*String == '[')
            {
                Depth++;         /* A nested package declaration */
            }
            else if (*String == ']')
            {
                Depth--;
                if (Depth == 0) /* Found final package closing bracket */
                {
                    break;
                }
            }

            String++;
        }
        break;

    default:

        Start = String;

        /* Find end of token */

        while (*String && (*String != ' '))
        {
            String++;
        }
        break;
    }

    if (!(*String))
    {
        *Next = NULL;
    }
    else
    {
        *String = 0;
        *Next = String + 1;
    }

    *ReturnType = Type;
    return (Start);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbGetLine
 *
 * PARAMETERS:  InputBuffer         - Command line buffer
 *
 * RETURN:      Count of arguments to the command
 *
 * DESCRIPTION: Get the next command line from the user. Gets entire line
 *              up to the next newline
 *
 ******************************************************************************/

static UINT32
AcpiDbGetLine (
    char                    *InputBuffer)
{
    UINT32                  i;
    UINT32                  Count;
    char                    *Next;
    char                    *This;


    if (AcpiUtSafeStrcpy (AcpiGbl_DbParsedBuf, sizeof (AcpiGbl_DbParsedBuf),
        InputBuffer))
    {
        AcpiOsPrintf (
            "Buffer overflow while parsing input line (max %u characters)\n",
            sizeof (AcpiGbl_DbParsedBuf));
        return (0);
    }

    This = AcpiGbl_DbParsedBuf;
    for (i = 0; i < ACPI_DEBUGGER_MAX_ARGS; i++)
    {
        AcpiGbl_DbArgs[i] = AcpiDbGetNextToken (This, &Next,
            &AcpiGbl_DbArgTypes[i]);
        if (!AcpiGbl_DbArgs[i])
        {
            break;
        }

        This = Next;
    }

    /* Uppercase the actual command */

    AcpiUtStrupr (AcpiGbl_DbArgs[0]);

    Count = i;
    if (Count)
    {
        Count--;  /* Number of args only */
    }

    return (Count);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbMatchCommand
 *
 * PARAMETERS:  UserCommand             - User command line
 *
 * RETURN:      Index into command array, -1 if not found
 *
 * DESCRIPTION: Search command array for a command match
 *
 ******************************************************************************/

static UINT32
AcpiDbMatchCommand (
    char                    *UserCommand)
{
    UINT32                  i;


    if (!UserCommand || UserCommand[0] == 0)
    {
        return (CMD_NULL);
    }

    for (i = CMD_FIRST_VALID; AcpiGbl_DbCommands[i].Name; i++)
    {
        if (strstr (
            ACPI_CAST_PTR (char, AcpiGbl_DbCommands[i].Name), UserCommand) ==
            AcpiGbl_DbCommands[i].Name)
        {
            return (i);
        }
    }

    /* Command not recognized */

    return (CMD_NOT_FOUND);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbCommandDispatch
 *
 * PARAMETERS:  InputBuffer         - Command line buffer
 *              WalkState           - Current walk
 *              Op                  - Current (executing) parse op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Command dispatcher.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDbCommandDispatch (
    char                    *InputBuffer,
    ACPI_WALK_STATE         *WalkState,
    ACPI_PARSE_OBJECT       *Op)
{
    UINT32                  Temp;
    UINT32                  CommandIndex;
    UINT32                  ParamCount;
    char                    *CommandLine;
    ACPI_STATUS             Status = AE_CTRL_TRUE;


    /* If AcpiTerminate has been called, terminate this thread */

    if (AcpiGbl_DbTerminateLoop)
    {
        return (AE_CTRL_TERMINATE);
    }

    /* Find command and add to the history buffer */

    ParamCount = AcpiDbGetLine (InputBuffer);
    CommandIndex = AcpiDbMatchCommand (AcpiGbl_DbArgs[0]);
    Temp = 0;

    /*
     * We don't want to add the !! command to the history buffer. It
     * would cause an infinite loop because it would always be the
     * previous command.
     */
    if (CommandIndex != CMD_HISTORY_LAST)
    {
        AcpiDbAddToHistory (InputBuffer);
    }

    /* Verify that we have the minimum number of params */

    if (ParamCount < AcpiGbl_DbCommands[CommandIndex].MinArgs)
    {
        AcpiOsPrintf ("%u parameters entered, [%s] requires %u parameters\n",
            ParamCount, AcpiGbl_DbCommands[CommandIndex].Name,
            AcpiGbl_DbCommands[CommandIndex].MinArgs);

        AcpiDbDisplayCommandInfo (
            AcpiGbl_DbCommands[CommandIndex].Name, FALSE);
        return (AE_CTRL_TRUE);
    }

    /* Decode and dispatch the command */

    switch (CommandIndex)
    {
    case CMD_NULL:

        if (Op)
        {
            return (AE_OK);
        }
        break;

    case CMD_ALLOCATIONS:

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
        AcpiUtDumpAllocations ((UINT32) -1, NULL);
#endif
        break;

    case CMD_ARGS:
    case CMD_ARGUMENTS:

        AcpiDbDisplayArguments ();
        break;

    case CMD_BREAKPOINT:

        AcpiDbSetMethodBreakpoint (AcpiGbl_DbArgs[1], WalkState, Op);
        break;

    case CMD_BUSINFO:

        AcpiDbGetBusInfo ();
        break;

    case CMD_CALL:

        AcpiDbSetMethodCallBreakpoint (Op);
        Status = AE_OK;
        break;

    case CMD_DEBUG:

        AcpiDbExecute (AcpiGbl_DbArgs[1],
            &AcpiGbl_DbArgs[2], &AcpiGbl_DbArgTypes[2], EX_SINGLE_STEP);
        break;

    case CMD_DISASSEMBLE:
    case CMD_DISASM:

#ifdef ACPI_DISASSEMBLER
        (void) AcpiDbDisassembleMethod (AcpiGbl_DbArgs[1]);
#else
        AcpiOsPrintf ("The AML Disassembler is not configured/present\n");
#endif
        break;

    case CMD_DUMP:

        AcpiDbDecodeAndDisplayObject (AcpiGbl_DbArgs[1], AcpiGbl_DbArgs[2]);
        break;

    case CMD_EVALUATE:
    case CMD_EXECUTE:

        AcpiDbExecute (AcpiGbl_DbArgs[1],
            &AcpiGbl_DbArgs[2], &AcpiGbl_DbArgTypes[2], EX_NO_SINGLE_STEP);
        break;

    case CMD_FIND:

        Status = AcpiDbFindNameInNamespace (AcpiGbl_DbArgs[1]);
        break;

    case CMD_GO:

        AcpiGbl_CmSingleStep = FALSE;
        return (AE_OK);

    case CMD_HANDLERS:

        AcpiDbDisplayHandlers ();
        break;

    case CMD_HELP:
    case CMD_HELP2:

        AcpiDbDisplayHelp (AcpiGbl_DbArgs[1]);
        break;

    case CMD_HISTORY:

        AcpiDbDisplayHistory ();
        break;

    case CMD_HISTORY_EXE: /* ! command */

        CommandLine = AcpiDbGetFromHistory (AcpiGbl_DbArgs[1]);
        if (!CommandLine)
        {
            return (AE_CTRL_TRUE);
        }

        Status = AcpiDbCommandDispatch (CommandLine, WalkState, Op);
        return (Status);

    case CMD_HISTORY_LAST: /* !! command */

        CommandLine = AcpiDbGetFromHistory (NULL);
        if (!CommandLine)
        {
            return (AE_CTRL_TRUE);
        }

        Status = AcpiDbCommandDispatch (CommandLine, WalkState, Op);
        return (Status);

    case CMD_INFORMATION:

        AcpiDbDisplayMethodInfo (Op);
        break;

    case CMD_INTEGRITY:

        AcpiDbCheckIntegrity ();
        break;

    case CMD_INTO:

        if (Op)
        {
            AcpiGbl_CmSingleStep = TRUE;
            return (AE_OK);
        }
        break;

    case CMD_LEVEL:

        if (ParamCount == 0)
        {
            AcpiOsPrintf (
                "Current debug level for file output is:    %8.8lX\n",
                AcpiGbl_DbDebugLevel);
            AcpiOsPrintf (
                "Current debug level for console output is: %8.8lX\n",
                AcpiGbl_DbConsoleDebugLevel);
        }
        else if (ParamCount == 2)
        {
            Temp = AcpiGbl_DbConsoleDebugLevel;
            AcpiGbl_DbConsoleDebugLevel =
                strtoul (AcpiGbl_DbArgs[1], NULL, 16);
            AcpiOsPrintf (
                "Debug Level for console output was %8.8lX, now %8.8lX\n",
                Temp, AcpiGbl_DbConsoleDebugLevel);
        }
        else
        {
            Temp = AcpiGbl_DbDebugLevel;
            AcpiGbl_DbDebugLevel = strtoul (AcpiGbl_DbArgs[1], NULL, 16);
            AcpiOsPrintf (
                "Debug Level for file output was %8.8lX, now %8.8lX\n",
                Temp, AcpiGbl_DbDebugLevel);
        }
        break;

    case CMD_LIST:

#ifdef ACPI_DISASSEMBLER
        AcpiDbDisassembleAml (AcpiGbl_DbArgs[1], Op);
#else
        AcpiOsPrintf ("The AML Disassembler is not configured/present\n");
#endif
        break;

    case CMD_LOCKS:

        AcpiDbDisplayLocks ();
        break;

    case CMD_LOCALS:

        AcpiDbDisplayLocals ();
        break;

    case CMD_METHODS:

        Status = AcpiDbDisplayObjects ("METHOD", AcpiGbl_DbArgs[1]);
        break;

    case CMD_NAMESPACE:

        AcpiDbDumpNamespace (AcpiGbl_DbArgs[1], AcpiGbl_DbArgs[2]);
        break;

    case CMD_NOTIFY:

        Temp = strtoul (AcpiGbl_DbArgs[2], NULL, 0);
        AcpiDbSendNotify (AcpiGbl_DbArgs[1], Temp);
        break;

    case CMD_OBJECTS:

        AcpiUtStrupr (AcpiGbl_DbArgs[1]);
        Status = AcpiDbDisplayObjects (AcpiGbl_DbArgs[1], AcpiGbl_DbArgs[2]);
        break;

    case CMD_OSI:

        AcpiDbDisplayInterfaces (AcpiGbl_DbArgs[1], AcpiGbl_DbArgs[2]);
        break;

    case CMD_OWNER:

        AcpiDbDumpNamespaceByOwner (AcpiGbl_DbArgs[1], AcpiGbl_DbArgs[2]);
        break;

    case CMD_PATHS:

        AcpiDbDumpNamespacePaths ();
        break;

    case CMD_PREFIX:

        AcpiDbSetScope (AcpiGbl_DbArgs[1]);
        break;

    case CMD_REFERENCES:

        AcpiDbFindReferences (AcpiGbl_DbArgs[1]);
        break;

    case CMD_RESOURCES:

        AcpiDbDisplayResources (AcpiGbl_DbArgs[1]);
        break;

    case CMD_RESULTS:

        AcpiDbDisplayResults ();
        break;

    case CMD_SET:

        AcpiDbSetMethodData (AcpiGbl_DbArgs[1], AcpiGbl_DbArgs[2],
            AcpiGbl_DbArgs[3]);
        break;

    case CMD_STATS:

        Status = AcpiDbDisplayStatistics (AcpiGbl_DbArgs[1]);
        break;

    case CMD_STOP:

        return (AE_NOT_IMPLEMENTED);

    case CMD_TABLES:

        AcpiDbDisplayTableInfo (AcpiGbl_DbArgs[1]);
        break;

    case CMD_TEMPLATE:

        AcpiDbDisplayTemplate (AcpiGbl_DbArgs[1]);
        break;

    case CMD_TRACE:

        AcpiDbTrace (AcpiGbl_DbArgs[1], AcpiGbl_DbArgs[2], AcpiGbl_DbArgs[3]);
        break;

    case CMD_TREE:

        AcpiDbDisplayCallingTree ();
        break;

    case CMD_TYPE:

        AcpiDbDisplayObjectType (AcpiGbl_DbArgs[1]);
        break;

#ifdef ACPI_APPLICATION

    /* Hardware simulation commands. */

    case CMD_ENABLEACPI:
#if (!ACPI_REDUCED_HARDWARE)

        Status = AcpiEnable();
        if (ACPI_FAILURE(Status))
        {
            AcpiOsPrintf("AcpiEnable failed (Status=%X)\n", Status);
            return (Status);
        }
#endif /* !ACPI_REDUCED_HARDWARE */
        break;

    case CMD_EVENT:

        AcpiOsPrintf ("Event command not implemented\n");
        break;

    case CMD_GPE:

        AcpiDbGenerateGpe (AcpiGbl_DbArgs[1], AcpiGbl_DbArgs[2]);
        break;

    case CMD_GPES:

        AcpiDbDisplayGpes ();
        break;

    case CMD_SCI:

        AcpiDbGenerateSci ();
        break;

    case CMD_SLEEP:

        Status = AcpiDbSleep (AcpiGbl_DbArgs[1]);
        break;

    /* File I/O commands. */

    case CMD_CLOSE:

        AcpiDbCloseDebugFile ();
        break;

    case CMD_LOAD:
        {
            ACPI_NEW_TABLE_DESC     *ListHead = NULL;

            Status = AcGetAllTablesFromFile (AcpiGbl_DbArgs[1],
                ACPI_GET_ALL_TABLES, &ListHead);
            if (ACPI_SUCCESS (Status))
            {
                AcpiDbLoadTables (ListHead);
            }
        }
        break;

    case CMD_OPEN:

        AcpiDbOpenDebugFile (AcpiGbl_DbArgs[1]);
        break;

    /* User space commands. */

    case CMD_TERMINATE:

        AcpiDbSetOutputDestination (ACPI_DB_REDIRECTABLE_OUTPUT);
        AcpiUtSubsystemShutdown ();

        /*
         * TBD: [Restructure] Need some way to re-initialize without
         * re-creating the semaphores!
         */

        AcpiGbl_DbTerminateLoop = TRUE;
        /*  AcpiInitialize (NULL);  */
        break;

    case CMD_BACKGROUND:

        AcpiDbCreateExecutionThread (AcpiGbl_DbArgs[1], &AcpiGbl_DbArgs[2],
            &AcpiGbl_DbArgTypes[2]);
        break;

    case CMD_THREADS:

        AcpiDbCreateExecutionThreads (AcpiGbl_DbArgs[1], AcpiGbl_DbArgs[2],
            AcpiGbl_DbArgs[3]);
        break;

    /* Debug test commands. */

    case CMD_PREDEFINED:

        AcpiDbCheckPredefinedNames ();
        break;

    case CMD_TEST:

        AcpiDbExecuteTest (AcpiGbl_DbArgs[1]);
        break;

    case CMD_UNLOAD:

        AcpiDbUnloadAcpiTable (AcpiGbl_DbArgs[1]);
        break;
#endif

    case CMD_EXIT:
    case CMD_QUIT:

        if (Op)
        {
            AcpiOsPrintf ("Method execution terminated\n");
            return (AE_CTRL_TERMINATE);
        }

        if (!AcpiGbl_DbOutputToFile)
        {
            AcpiDbgLevel = ACPI_DEBUG_DEFAULT;
        }

#ifdef ACPI_APPLICATION
        AcpiDbCloseDebugFile ();
#endif
        AcpiGbl_DbTerminateLoop = TRUE;
        return (AE_CTRL_TERMINATE);

    case CMD_NOT_FOUND:
    default:

        AcpiOsPrintf ("%s: unknown command\n", AcpiGbl_DbArgs[0]);
        return (AE_CTRL_TRUE);
    }

    if (ACPI_SUCCESS (Status))
    {
        Status = AE_CTRL_TRUE;
    }

    return (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbExecuteThread
 *
 * PARAMETERS:  Context         - Not used
 *
 * RETURN:      None
 *
 * DESCRIPTION: Debugger execute thread. Waits for a command line, then
 *              simply dispatches it.
 *
 ******************************************************************************/

void ACPI_SYSTEM_XFACE
AcpiDbExecuteThread (
    void                    *Context)
{

    (void) AcpiDbUserCommands ();
    AcpiGbl_DbThreadsTerminated = TRUE;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiDbUserCommands
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Command line execution for the AML debugger. Commands are
 *              matched and dispatched here.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiDbUserCommands (
    void)
{
    ACPI_STATUS             Status = AE_OK;


    AcpiOsPrintf ("\n");

    /* TBD: [Restructure] Need a separate command line buffer for step mode */

    while (!AcpiGbl_DbTerminateLoop)
    {
        /* Wait the readiness of the command */

        Status = AcpiOsWaitCommandReady ();
        if (ACPI_FAILURE (Status))
        {
            break;
        }

        /* Just call to the command line interpreter */

        AcpiGbl_MethodExecuting = FALSE;
        AcpiGbl_StepToNextCall = FALSE;

        (void) AcpiDbCommandDispatch (AcpiGbl_DbLineBuf, NULL, NULL);

        /* Notify the completion of the command */

        Status = AcpiOsNotifyCommandComplete ();
        if (ACPI_FAILURE (Status))
        {
            break;
        }
    }

    if (ACPI_FAILURE (Status) && Status != AE_CTRL_TERMINATE)
    {
        ACPI_EXCEPTION ((AE_INFO, Status, "While parsing command line"));
    }
    return (Status);
}
