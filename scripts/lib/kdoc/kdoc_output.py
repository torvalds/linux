#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2025: Mauro Carvalho Chehab <mchehab@kernel.org>.
#
# pylint: disable=C0301,R0902,R0911,R0912,R0913,R0914,R0915,R0917

"""
Implement output filters to print kernel-doc documentation.

The implementation uses a virtual base class (OutputFormat) which
contains a dispatches to virtual methods, and some code to filter
out output messages.

The actual implementation is done on one separate class per each type
of output. Currently, there are output classes for ReST and man/troff.
"""

import os
import re
from datetime import datetime

from kdoc_parser import KernelDoc, type_param
from kdoc_re import KernRe


function_pointer = KernRe(r"([^\(]*\(\*)\s*\)\s*\(([^\)]*)\)", cache=False)

# match expressions used to find embedded type information
type_constant = KernRe(r"\b``([^\`]+)``\b", cache=False)
type_constant2 = KernRe(r"\%([-_*\w]+)", cache=False)
type_func = KernRe(r"(\w+)\(\)", cache=False)
type_param_ref = KernRe(r"([\!~\*]?)\@(\w*((\.\w+)|(->\w+))*(\.\.\.)?)", cache=False)

# Special RST handling for func ptr params
type_fp_param = KernRe(r"\@(\w+)\(\)", cache=False)

# Special RST handling for structs with func ptr params
type_fp_param2 = KernRe(r"\@(\w+->\S+)\(\)", cache=False)

type_env = KernRe(r"(\$\w+)", cache=False)
type_enum = KernRe(r"\&(enum\s*([_\w]+))", cache=False)
type_struct = KernRe(r"\&(struct\s*([_\w]+))", cache=False)
type_typedef = KernRe(r"\&(typedef\s*([_\w]+))", cache=False)
type_union = KernRe(r"\&(union\s*([_\w]+))", cache=False)
type_member = KernRe(r"\&([_\w]+)(\.|->)([_\w]+)", cache=False)
type_fallback = KernRe(r"\&([_\w]+)", cache=False)
type_member_func = type_member + KernRe(r"\(\)", cache=False)


class OutputFormat:
    """
    Base class for OutputFormat. If used as-is, it means that only
    warnings will be displayed.
    """

    # output mode.
    OUTPUT_ALL          = 0 # output all symbols and doc sections
    OUTPUT_INCLUDE      = 1 # output only specified symbols
    OUTPUT_EXPORTED     = 2 # output exported symbols
    OUTPUT_INTERNAL     = 3 # output non-exported symbols

    # Virtual member to be overriden at the  inherited classes
    highlights = []

    def __init__(self):
        """Declare internal vars and set mode to OUTPUT_ALL"""

        self.out_mode = self.OUTPUT_ALL
        self.enable_lineno = None
        self.nosymbol = {}
        self.symbol = None
        self.function_table = None
        self.config = None
        self.no_doc_sections = False

        self.data = ""

    def set_config(self, config):
        """
        Setup global config variables used by both parser and output.
        """

        self.config = config

    def set_filter(self, export, internal, symbol, nosymbol, function_table,
                   enable_lineno, no_doc_sections):
        """
        Initialize filter variables according with the requested mode.

        Only one choice is valid between export, internal and symbol.

        The nosymbol filter can be used on all modes.
        """

        self.enable_lineno = enable_lineno
        self.no_doc_sections = no_doc_sections
        self.function_table = function_table

        if symbol:
            self.out_mode = self.OUTPUT_INCLUDE
        elif export:
            self.out_mode = self.OUTPUT_EXPORTED
        elif internal:
            self.out_mode = self.OUTPUT_INTERNAL
        else:
            self.out_mode = self.OUTPUT_ALL

        if nosymbol:
            self.nosymbol = set(nosymbol)


    def highlight_block(self, block):
        """
        Apply the RST highlights to a sub-block of text.
        """

        for r, sub in self.highlights:
            block = r.sub(sub, block)

        return block

    def out_warnings(self, args):
        """
        Output warnings for identifiers that will be displayed.
        """

        for log_msg in args.warnings:
            self.config.warning(log_msg)

    def check_doc(self, name, args):
        """Check if DOC should be output"""

        if self.no_doc_sections:
            return False

        if name in self.nosymbol:
            return False

        if self.out_mode == self.OUTPUT_ALL:
            self.out_warnings(args)
            return True

        if self.out_mode == self.OUTPUT_INCLUDE:
            if name in self.function_table:
                self.out_warnings(args)
                return True

        return False

    def check_declaration(self, dtype, name, args):
        """
        Checks if a declaration should be output or not based on the
        filtering criteria.
        """

        if name in self.nosymbol:
            return False

        if self.out_mode == self.OUTPUT_ALL:
            self.out_warnings(args)
            return True

        if self.out_mode in [self.OUTPUT_INCLUDE, self.OUTPUT_EXPORTED]:
            if name in self.function_table:
                return True

        if self.out_mode == self.OUTPUT_INTERNAL:
            if dtype != "function":
                self.out_warnings(args)
                return True

            if name not in self.function_table:
                self.out_warnings(args)
                return True

        return False

    def msg(self, fname, name, args):
        """
        Handles a single entry from kernel-doc parser
        """

        self.data = ""

        dtype = args.type

        if dtype == "doc":
            self.out_doc(fname, name, args)
            return self.data

        if not self.check_declaration(dtype, name, args):
            return self.data

        if dtype == "function":
            self.out_function(fname, name, args)
            return self.data

        if dtype == "enum":
            self.out_enum(fname, name, args)
            return self.data

        if dtype == "typedef":
            self.out_typedef(fname, name, args)
            return self.data

        if dtype in ["struct", "union"]:
            self.out_struct(fname, name, args)
            return self.data

        # Warn if some type requires an output logic
        self.config.log.warning("doesn't now how to output '%s' block",
                                dtype)

        return None

    # Virtual methods to be overridden by inherited classes
    # At the base class, those do nothing.
    def out_doc(self, fname, name, args):
        """Outputs a DOC block"""

    def out_function(self, fname, name, args):
        """Outputs a function"""

    def out_enum(self, fname, name, args):
        """Outputs an enum"""

    def out_typedef(self, fname, name, args):
        """Outputs a typedef"""

    def out_struct(self, fname, name, args):
        """Outputs a struct"""


class RestFormat(OutputFormat):
    """Consts and functions used by ReST output"""

    highlights = [
        (type_constant, r"``\1``"),
        (type_constant2, r"``\1``"),

        # Note: need to escape () to avoid func matching later
        (type_member_func, r":c:type:`\1\2\3\\(\\) <\1>`"),
        (type_member, r":c:type:`\1\2\3 <\1>`"),
        (type_fp_param, r"**\1\\(\\)**"),
        (type_fp_param2, r"**\1\\(\\)**"),
        (type_func, r"\1()"),
        (type_enum, r":c:type:`\1 <\2>`"),
        (type_struct, r":c:type:`\1 <\2>`"),
        (type_typedef, r":c:type:`\1 <\2>`"),
        (type_union, r":c:type:`\1 <\2>`"),

        # in rst this can refer to any type
        (type_fallback, r":c:type:`\1`"),
        (type_param_ref, r"**\1\2**")
    ]
    blankline = "\n"

    sphinx_literal = KernRe(r'^[^.].*::$', cache=False)
    sphinx_cblock = KernRe(r'^\.\.\ +code-block::', cache=False)

    def __init__(self):
        """
        Creates class variables.

        Not really mandatory, but it is a good coding style and makes
        pylint happy.
        """

        super().__init__()
        self.lineprefix = ""

    def print_lineno(self, ln):
        """Outputs a line number"""

        if self.enable_lineno and ln is not None:
            ln += 1
            self.data += f".. LINENO {ln}\n"

    def output_highlight(self, args):
        """
        Outputs a C symbol that may require being converted to ReST using
        the self.highlights variable
        """

        input_text = args
        output = ""
        in_literal = False
        litprefix = ""
        block = ""

        for line in input_text.strip("\n").split("\n"):

            # If we're in a literal block, see if we should drop out of it.
            # Otherwise, pass the line straight through unmunged.
            if in_literal:
                if line.strip():  # If the line is not blank
                    # If this is the first non-blank line in a literal block,
                    # figure out the proper indent.
                    if not litprefix:
                        r = KernRe(r'^(\s*)')
                        if r.match(line):
                            litprefix = '^' + r.group(1)
                        else:
                            litprefix = ""

                        output += line + "\n"
                    elif not KernRe(litprefix).match(line):
                        in_literal = False
                    else:
                        output += line + "\n"
                else:
                    output += line + "\n"

            # Not in a literal block (or just dropped out)
            if not in_literal:
                block += line + "\n"
                if self.sphinx_literal.match(line) or self.sphinx_cblock.match(line):
                    in_literal = True
                    litprefix = ""
                    output += self.highlight_block(block)
                    block = ""

        # Handle any remaining block
        if block:
            output += self.highlight_block(block)

        # Print the output with the line prefix
        for line in output.strip("\n").split("\n"):
            self.data += self.lineprefix + line + "\n"

    def out_section(self, args, out_docblock=False):
        """
        Outputs a block section.

        This could use some work; it's used to output the DOC: sections, and
        starts by putting out the name of the doc section itself, but that
        tends to duplicate a header already in the template file.
        """
        for section, text in args.sections.items():
            # Skip sections that are in the nosymbol_table
            if section in self.nosymbol:
                continue

            if out_docblock:
                if not self.out_mode == self.OUTPUT_INCLUDE:
                    self.data += f".. _{section}:\n\n"
                    self.data += f'{self.lineprefix}**{section}**\n\n'
            else:
                self.data += f'{self.lineprefix}**{section}**\n\n'

            self.print_lineno(args.section_start_lines.get(section, 0))
            self.output_highlight(text)
            self.data += "\n"
        self.data += "\n"

    def out_doc(self, fname, name, args):
        if not self.check_doc(name, args):
            return
        self.out_section(args, out_docblock=True)

    def out_function(self, fname, name, args):

        oldprefix = self.lineprefix
        signature = ""

        func_macro = args.get('func_macro', False)
        if func_macro:
            signature = name
        else:
            if args.get('functiontype'):
                signature = args['functiontype'] + " "
            signature += name + " ("

        ln = args.declaration_start_line
        count = 0
        for parameter in args.parameterlist:
            if count != 0:
                signature += ", "
            count += 1
            dtype = args.parametertypes.get(parameter, "")

            if function_pointer.search(dtype):
                signature += function_pointer.group(1) + parameter + function_pointer.group(3)
            else:
                signature += dtype

        if not func_macro:
            signature += ")"

        self.print_lineno(ln)
        if args.get('typedef') or not args.get('functiontype'):
            self.data += f".. c:macro:: {name}\n\n"

            if args.get('typedef'):
                self.data += "   **Typedef**: "
                self.lineprefix = ""
                self.output_highlight(args.get('purpose', ""))
                self.data += "\n\n**Syntax**\n\n"
                self.data += f"  ``{signature}``\n\n"
            else:
                self.data += f"``{signature}``\n\n"
        else:
            self.data += f".. c:function:: {signature}\n\n"

        if not args.get('typedef'):
            self.print_lineno(ln)
            self.lineprefix = "   "
            self.output_highlight(args.get('purpose', ""))
            self.data += "\n"

        # Put descriptive text into a container (HTML <div>) to help set
        # function prototypes apart
        self.lineprefix = "  "

        if args.parameterlist:
            self.data += ".. container:: kernelindent\n\n"
            self.data += f"{self.lineprefix}**Parameters**\n\n"

        for parameter in args.parameterlist:
            parameter_name = KernRe(r'\[.*').sub('', parameter)
            dtype = args.parametertypes.get(parameter, "")

            if dtype:
                self.data += f"{self.lineprefix}``{dtype}``\n"
            else:
                self.data += f"{self.lineprefix}``{parameter}``\n"

            self.print_lineno(args.parameterdesc_start_lines.get(parameter_name, 0))

            self.lineprefix = "    "
            if parameter_name in args.parameterdescs and \
               args.parameterdescs[parameter_name] != KernelDoc.undescribed:

                self.output_highlight(args.parameterdescs[parameter_name])
                self.data += "\n"
            else:
                self.data += f"{self.lineprefix}*undescribed*\n\n"
            self.lineprefix = "  "

        self.out_section(args)
        self.lineprefix = oldprefix

    def out_enum(self, fname, name, args):

        oldprefix = self.lineprefix
        ln = args.declaration_start_line

        self.data += f"\n\n.. c:enum:: {name}\n\n"

        self.print_lineno(ln)
        self.lineprefix = "  "
        self.output_highlight(args.get('purpose', ''))
        self.data += "\n"

        self.data += ".. container:: kernelindent\n\n"
        outer = self.lineprefix + "  "
        self.lineprefix = outer + "  "
        self.data += f"{outer}**Constants**\n\n"

        for parameter in args.parameterlist:
            self.data += f"{outer}``{parameter}``\n"

            if args.parameterdescs.get(parameter, '') != KernelDoc.undescribed:
                self.output_highlight(args.parameterdescs[parameter])
            else:
                self.data += f"{self.lineprefix}*undescribed*\n\n"
            self.data += "\n"

        self.lineprefix = oldprefix
        self.out_section(args)

    def out_typedef(self, fname, name, args):

        oldprefix = self.lineprefix
        ln = args.declaration_start_line

        self.data += f"\n\n.. c:type:: {name}\n\n"

        self.print_lineno(ln)
        self.lineprefix = "   "

        self.output_highlight(args.get('purpose', ''))

        self.data += "\n"

        self.lineprefix = oldprefix
        self.out_section(args)

    def out_struct(self, fname, name, args):

        purpose = args.get('purpose', "")
        declaration = args.get('definition', "")
        dtype = args.type
        ln = args.declaration_start_line

        self.data += f"\n\n.. c:{dtype}:: {name}\n\n"

        self.print_lineno(ln)

        oldprefix = self.lineprefix
        self.lineprefix += "  "

        self.output_highlight(purpose)
        self.data += "\n"

        self.data += ".. container:: kernelindent\n\n"
        self.data += f"{self.lineprefix}**Definition**::\n\n"

        self.lineprefix = self.lineprefix + "  "

        declaration = declaration.replace("\t", self.lineprefix)

        self.data += f"{self.lineprefix}{dtype} {name}" + ' {' + "\n"
        self.data += f"{declaration}{self.lineprefix}" + "};\n\n"

        self.lineprefix = "  "
        self.data += f"{self.lineprefix}**Members**\n\n"
        for parameter in args.parameterlist:
            if not parameter or parameter.startswith("#"):
                continue

            parameter_name = parameter.split("[", maxsplit=1)[0]

            if args.parameterdescs.get(parameter_name) == KernelDoc.undescribed:
                continue

            self.print_lineno(args.parameterdesc_start_lines.get(parameter_name, 0))

            self.data += f"{self.lineprefix}``{parameter}``\n"

            self.lineprefix = "    "
            self.output_highlight(args.parameterdescs[parameter_name])
            self.lineprefix = "  "

            self.data += "\n"

        self.data += "\n"

        self.lineprefix = oldprefix
        self.out_section(args)


class ManFormat(OutputFormat):
    """Consts and functions used by man pages output"""

    highlights = (
        (type_constant, r"\1"),
        (type_constant2, r"\1"),
        (type_func, r"\\fB\1\\fP"),
        (type_enum, r"\\fI\1\\fP"),
        (type_struct, r"\\fI\1\\fP"),
        (type_typedef, r"\\fI\1\\fP"),
        (type_union, r"\\fI\1\\fP"),
        (type_param, r"\\fI\1\\fP"),
        (type_param_ref, r"\\fI\1\2\\fP"),
        (type_member, r"\\fI\1\2\3\\fP"),
        (type_fallback, r"\\fI\1\\fP")
    )
    blankline = ""

    date_formats = [
        "%a %b %d %H:%M:%S %Z %Y",
        "%a %b %d %H:%M:%S %Y",
        "%Y-%m-%d",
        "%b %d %Y",
        "%B %d %Y",
        "%m %d %Y",
    ]

    def __init__(self, modulename):
        """
        Creates class variables.

        Not really mandatory, but it is a good coding style and makes
        pylint happy.
        """

        super().__init__()
        self.modulename = modulename

        dt = None
        tstamp = os.environ.get("KBUILD_BUILD_TIMESTAMP")
        if tstamp:
            for fmt in self.date_formats:
                try:
                    dt = datetime.strptime(tstamp, fmt)
                    break
                except ValueError:
                    pass

        if not dt:
            dt = datetime.now()

        self.man_date = dt.strftime("%B %Y")

    def output_highlight(self, block):
        """
        Outputs a C symbol that may require being highlighted with
        self.highlights variable using troff syntax
        """

        contents = self.highlight_block(block)

        if isinstance(contents, list):
            contents = "\n".join(contents)

        for line in contents.strip("\n").split("\n"):
            line = KernRe(r"^\s*").sub("", line)
            if not line:
                continue

            if line[0] == ".":
                self.data += "\\&" + line + "\n"
            else:
                self.data += line + "\n"

    def out_doc(self, fname, name, args):
        if not self.check_doc(name, args):
            return

        self.data += f'.TH "{self.modulename}" 9 "{self.modulename}" "{self.man_date}" "API Manual" LINUX' + "\n"

        for section, text in args.sections.items():
            self.data += f'.SH "{section}"' + "\n"
            self.output_highlight(text)

    def out_function(self, fname, name, args):
        """output function in man"""

        self.data += f'.TH "{name}" 9 "{name}" "{self.man_date}" "Kernel Hacker\'s Manual" LINUX' + "\n"

        self.data += ".SH NAME\n"
        self.data += f"{name} \\- {args['purpose']}\n"

        self.data += ".SH SYNOPSIS\n"
        if args.get('functiontype', ''):
            self.data += f'.B "{args["functiontype"]}" {name}' + "\n"
        else:
            self.data += f'.B "{name}' + "\n"

        count = 0
        parenth = "("
        post = ","

        for parameter in args.parameterlist:
            if count == len(args.parameterlist) - 1:
                post = ");"

            dtype = args.parametertypes.get(parameter, "")
            if function_pointer.match(dtype):
                # Pointer-to-function
                self.data += f'".BI "{parenth}{function_pointer.group(1)}" " ") ({function_pointer.group(2)}){post}"' + "\n"
            else:
                dtype = KernRe(r'([^\*])$').sub(r'\1 ', dtype)

                self.data += f'.BI "{parenth}{dtype}"  "{post}"' + "\n"
            count += 1
            parenth = ""

        if args.parameterlist:
            self.data += ".SH ARGUMENTS\n"

        for parameter in args.parameterlist:
            parameter_name = re.sub(r'\[.*', '', parameter)

            self.data += f'.IP "{parameter}" 12' + "\n"
            self.output_highlight(args.parameterdescs.get(parameter_name, ""))

        for section, text in args.sections.items():
            self.data += f'.SH "{section.upper()}"' + "\n"
            self.output_highlight(text)

    def out_enum(self, fname, name, args):
        self.data += f'.TH "{self.modulename}" 9 "enum {name}" "{self.man_date}" "API Manual" LINUX' + "\n"

        self.data += ".SH NAME\n"
        self.data += f"enum {name} \\- {args['purpose']}\n"

        self.data += ".SH SYNOPSIS\n"
        self.data += f"enum {name}" + " {\n"

        count = 0
        for parameter in args.parameterlist:
            self.data += f'.br\n.BI "    {parameter}"' + "\n"
            if count == len(args.parameterlist) - 1:
                self.data += "\n};\n"
            else:
                self.data += ", \n.br\n"

            count += 1

        self.data += ".SH Constants\n"

        for parameter in args.parameterlist:
            parameter_name = KernRe(r'\[.*').sub('', parameter)
            self.data += f'.IP "{parameter}" 12' + "\n"
            self.output_highlight(args.parameterdescs.get(parameter_name, ""))

        for section, text in args.sections.items():
            self.data += f'.SH "{section}"' + "\n"
            self.output_highlight(text)

    def out_typedef(self, fname, name, args):
        module = self.modulename
        purpose = args.get('purpose')

        self.data += f'.TH "{module}" 9 "{name}" "{self.man_date}" "API Manual" LINUX' + "\n"

        self.data += ".SH NAME\n"
        self.data += f"typedef {name} \\- {purpose}\n"

        for section, text in args.sections.items():
            self.data += f'.SH "{section}"' + "\n"
            self.output_highlight(text)

    def out_struct(self, fname, name, args):
        module = self.modulename
        purpose = args.get('purpose')
        definition = args.get('definition')

        self.data += f'.TH "{module}" 9 "{args.type} {name}" "{self.man_date}" "API Manual" LINUX' + "\n"

        self.data += ".SH NAME\n"
        self.data += f"{args.type} {name} \\- {purpose}\n"

        # Replace tabs with two spaces and handle newlines
        declaration = definition.replace("\t", "  ")
        declaration = KernRe(r"\n").sub('"\n.br\n.BI "', declaration)

        self.data += ".SH SYNOPSIS\n"
        self.data += f"{args.type} {name} " + "{" + "\n.br\n"
        self.data += f'.BI "{declaration}\n' + "};\n.br\n\n"

        self.data += ".SH Members\n"
        for parameter in args.parameterlist:
            if parameter.startswith("#"):
                continue

            parameter_name = re.sub(r"\[.*", "", parameter)

            if args.parameterdescs.get(parameter_name) == KernelDoc.undescribed:
                continue

            self.data += f'.IP "{parameter}" 12' + "\n"
            self.output_highlight(args.parameterdescs.get(parameter_name))

        for section, text in args.sections.items():
            self.data += f'.SH "{section}"' + "\n"
            self.output_highlight(text)
