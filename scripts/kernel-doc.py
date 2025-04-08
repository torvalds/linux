#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2025: Mauro Carvalho Chehab <mchehab@kernel.org>.
#
# pylint: disable=R0902,R0903,R0904,R0911,R0912,R0913,R0914,R0915,R0917,R1702
# pylint: disable=C0302,C0103,C0301
# pylint: disable=C0116,C0115,W0511,W0613
#
# Converted from the kernel-doc script originally written in Perl
# under GPLv2, copyrighted since 1998 by the following authors:
#
#    Aditya Srivastava <yashsri421@gmail.com>
#    Akira Yokosawa <akiyks@gmail.com>
#    Alexander A. Klimov <grandmaster@al2klimov.de>
#    Alexander Lobakin <aleksander.lobakin@intel.com>
#    André Almeida <andrealmeid@igalia.com>
#    Andy Shevchenko <andriy.shevchenko@linux.intel.com>
#    Anna-Maria Behnsen <anna-maria@linutronix.de>
#    Armin Kuster <akuster@mvista.com>
#    Bart Van Assche <bart.vanassche@sandisk.com>
#    Ben Hutchings <ben@decadent.org.uk>
#    Borislav Petkov <bbpetkov@yahoo.de>
#    Chen-Yu Tsai <wenst@chromium.org>
#    Coco Li <lixiaoyan@google.com>
#    Conchúr Navid <conchur@web.de>
#    Daniel Santos <daniel.santos@pobox.com>
#    Danilo Cesar Lemes de Paula <danilo.cesar@collabora.co.uk>
#    Dan Luedtke <mail@danrl.de>
#    Donald Hunter <donald.hunter@gmail.com>
#    Gabriel Krisman Bertazi <krisman@collabora.co.uk>
#    Greg Kroah-Hartman <gregkh@linuxfoundation.org>
#    Harvey Harrison <harvey.harrison@gmail.com>
#    Horia Geanta <horia.geanta@freescale.com>
#    Ilya Dryomov <idryomov@gmail.com>
#    Jakub Kicinski <kuba@kernel.org>
#    Jani Nikula <jani.nikula@intel.com>
#    Jason Baron <jbaron@redhat.com>
#    Jason Gunthorpe <jgg@nvidia.com>
#    Jérémy Bobbio <lunar@debian.org>
#    Johannes Berg <johannes.berg@intel.com>
#    Johannes Weiner <hannes@cmpxchg.org>
#    Jonathan Cameron <Jonathan.Cameron@huawei.com>
#    Jonathan Corbet <corbet@lwn.net>
#    Jonathan Neuschäfer <j.neuschaefer@gmx.net>
#    Kamil Rytarowski <n54@gmx.com>
#    Kees Cook <kees@kernel.org>
#    Laurent Pinchart <laurent.pinchart@ideasonboard.com>
#    Levin, Alexander (Sasha Levin) <alexander.levin@verizon.com>
#    Linus Torvalds <torvalds@linux-foundation.org>
#    Lucas De Marchi <lucas.demarchi@profusion.mobi>
#    Mark Rutland <mark.rutland@arm.com>
#    Markus Heiser <markus.heiser@darmarit.de>
#    Martin Waitz <tali@admingilde.org>
#    Masahiro Yamada <masahiroy@kernel.org>
#    Matthew Wilcox <willy@infradead.org>
#    Mauro Carvalho Chehab <mchehab+huawei@kernel.org>
#    Michal Wajdeczko <michal.wajdeczko@intel.com>
#    Michael Zucchi
#    Mike Rapoport <rppt@linux.ibm.com>
#    Niklas Söderlund <niklas.soderlund@corigine.com>
#    Nishanth Menon <nm@ti.com>
#    Paolo Bonzini <pbonzini@redhat.com>
#    Pavan Kumar Linga <pavan.kumar.linga@intel.com>
#    Pavel Pisa <pisa@cmp.felk.cvut.cz>
#    Peter Maydell <peter.maydell@linaro.org>
#    Pierre-Louis Bossart <pierre-louis.bossart@linux.intel.com>
#    Randy Dunlap <rdunlap@infradead.org>
#    Richard Kennedy <richard@rsk.demon.co.uk>
#    Rich Walker <rw@shadow.org.uk>
#    Rolf Eike Beer <eike-kernel@sf-tec.de>
#    Sakari Ailus <sakari.ailus@linux.intel.com>
#    Silvio Fricke <silvio.fricke@gmail.com>
#    Simon Huggins
#    Tim Waugh <twaugh@redhat.com>
#    Tomasz Warniełło <tomasz.warniello@gmail.com>
#    Utkarsh Tripathi <utripathi2002@gmail.com>
#    valdis.kletnieks@vt.edu <valdis.kletnieks@vt.edu>
#    Vegard Nossum <vegard.nossum@oracle.com>
#    Will Deacon <will.deacon@arm.com>
#    Yacine Belkadi <yacine.belkadi.1@gmail.com>
#    Yujie Liu <yujie.liu@intel.com>

# TODO: implement warning filtering

"""
kernel_doc
==========

Print formatted kernel documentation to stdout

Read C language source or header FILEs, extract embedded
documentation comments, and print formatted documentation
to standard output.

The documentation comments are identified by the "/**"
opening comment mark.

See Documentation/doc-guide/kernel-doc.rst for the
documentation comment syntax.
"""

import argparse
import logging
import os
import re
import sys

from datetime import datetime
from pprint import pformat

from dateutil import tz

# Import Python modules

LIB_DIR = "lib/kdoc"
SRC_DIR = os.path.dirname(os.path.realpath(__file__))

sys.path.insert(0, os.path.join(SRC_DIR, LIB_DIR))

from kdoc_parser import KernelDoc, type_param
from kdoc_re import Re

function_pointer = Re(r"([^\(]*\(\*)\s*\)\s*\(([^\)]*)\)", cache=False)

# match expressions used to find embedded type information
type_constant = Re(r"\b``([^\`]+)``\b", cache=False)
type_constant2 = Re(r"\%([-_*\w]+)", cache=False)
type_func = Re(r"(\w+)\(\)", cache=False)
type_param_ref = Re(r"([\!~\*]?)\@(\w*((\.\w+)|(->\w+))*(\.\.\.)?)", cache=False)

# Special RST handling for func ptr params
type_fp_param = Re(r"\@(\w+)\(\)", cache=False)

# Special RST handling for structs with func ptr params
type_fp_param2 = Re(r"\@(\w+->\S+)\(\)", cache=False)

type_env = Re(r"(\$\w+)", cache=False)
type_enum = Re(r"\&(enum\s*([_\w]+))", cache=False)
type_struct = Re(r"\&(struct\s*([_\w]+))", cache=False)
type_typedef = Re(r"\&(typedef\s*([_\w]+))", cache=False)
type_union = Re(r"\&(union\s*([_\w]+))", cache=False)
type_member = Re(r"\&([_\w]+)(\.|->)([_\w]+)", cache=False)
type_fallback = Re(r"\&([_\w]+)", cache=False)
type_member_func = type_member + Re(r"\(\)", cache=False)

class GlobSourceFiles:
    """
    Parse C source code file names and directories via an Interactor.

    """

    def __init__(self, srctree=None, valid_extensions=None):
        """
        Initialize valid extensions with a tuple.

        If not defined, assume default C extensions (.c and .h)

        It would be possible to use python's glob function, but it is
        very slow, and it is not interactive. So, it would wait to read all
        directories before actually do something.

        So, let's use our own implementation.
        """

        if not valid_extensions:
            self.extensions = (".c", ".h")
        else:
            self.extensions = valid_extensions

        self.srctree = srctree

    def _parse_dir(self, dirname):
        """Internal function to parse files recursively"""

        with os.scandir(dirname) as obj:
            for entry in obj:
                name = os.path.join(dirname, entry.name)

                if entry.is_dir():
                    yield from self._parse_dir(name)

                if not entry.is_file():
                    continue

                basename = os.path.basename(name)

                if not basename.endswith(self.extensions):
                    continue

                yield name

    def parse_files(self, file_list, file_not_found_cb):
        for fname in file_list:
            if self.srctree:
                f = os.path.join(self.srctree, fname)
            else:
                f = fname

            if os.path.isdir(f):
                yield from self._parse_dir(f)
            elif os.path.isfile(f):
                yield f
            elif file_not_found_cb:
                file_not_found_cb(fname)


class KernelFiles():

    def parse_file(self, fname):

        doc = KernelDoc(self.config, fname)
        doc.run()

        return doc

    def process_export_file(self, fname):
        try:
            with open(fname, "r", encoding="utf8",
                      errors="backslashreplace") as fp:
                for line in fp:
                    KernelDoc.process_export(self.config.function_table, line)

        except IOError:
            print(f"Error: Cannot open fname {fname}", fname=sys.stderr)
            self.config.errors += 1

    def file_not_found_cb(self, fname):
        self.config.log.error("Cannot find file %s", fname)
        self.config.errors += 1

    def __init__(self, files=None, verbose=False, out_style=None,
                 werror=False, wreturn=False, wshort_desc=False,
                 wcontents_before_sections=False,
                 logger=None, modulename=None, export_file=None):
        """Initialize startup variables and parse all files"""


        if not verbose:
            verbose = bool(os.environ.get("KBUILD_VERBOSE", 0))

        if not modulename:
            modulename = "Kernel API"

        dt = datetime.now()
        if os.environ.get("KBUILD_BUILD_TIMESTAMP", None):
            # use UTC TZ
            to_zone = tz.gettz('UTC')
            dt = dt.astimezone(to_zone)

        if not werror:
            kcflags = os.environ.get("KCFLAGS", None)
            if kcflags:
                match = re.search(r"(\s|^)-Werror(\s|$)/", kcflags)
                if match:
                    werror = True

            # reading this variable is for backwards compat just in case
            # someone was calling it with the variable from outside the
            # kernel's build system
            kdoc_werror = os.environ.get("KDOC_WERROR", None)
            if kdoc_werror:
                werror = kdoc_werror

        # Set global config data used on all files
        self.config = argparse.Namespace

        self.config.verbose = verbose
        self.config.werror = werror
        self.config.wreturn = wreturn
        self.config.wshort_desc = wshort_desc
        self.config.wcontents_before_sections = wcontents_before_sections
        self.config.modulename = modulename

        self.config.function_table = set()
        self.config.source_map = {}

        if not logger:
            self.config.log = logging.getLogger("kernel-doc")
        else:
            self.config.log = logger

        self.config.kernel_version = os.environ.get("KERNELVERSION",
                                                    "unknown kernel version'")
        self.config.src_tree = os.environ.get("SRCTREE", None)

        self.out_style = out_style
        self.export_file = export_file

        # Initialize internal variables

        self.config.errors = 0
        self.results = []

        self.file_list = files
        self.files = set()

    def parse(self):
        """
        Parse all files
        """

        glob = GlobSourceFiles(srctree=self.config.src_tree)

        # Let's use a set here to avoid duplicating files

        for fname in glob.parse_files(self.file_list, self.file_not_found_cb):
            if fname in self.files:
                continue

            self.files.add(fname)

            res = self.parse_file(fname)
            self.results.append((res.fname, res.entries))

        if not self.files:
            sys.exit(1)

        # If a list of export files was provided, parse EXPORT_SYMBOL*
        # from the ones not already parsed

        if self.export_file:
            files = self.files

            glob = GlobSourceFiles(srctree=self.config.src_tree)

            for fname in glob.parse_files(self.export_file,
                                          self.file_not_found_cb):
                if fname not in files:
                    files.add(fname)

                    self.process_export_file(fname)

    def out_msg(self, fname, name, arg):
        # TODO: filter out unwanted parts

        return self.out_style.msg(fname, name, arg)

    def msg(self, enable_lineno=False, export=False, internal=False,
            symbol=None, nosymbol=None):

        function_table = self.config.function_table

        if symbol:
            for s in symbol:
                function_table.add(s)

        # Output none mode: only warnings will be shown
        if not self.out_style:
            return

        self.out_style.set_config(self.config)

        self.out_style.set_filter(export, internal, symbol, nosymbol,
                                  function_table, enable_lineno)

        for fname, arg_tuple in self.results:
            for name, arg in arg_tuple:
                if self.out_msg(fname, name, arg):
                    ln = arg.get("ln", 0)
                    dtype = arg.get('type', "")

                    self.config.log.warning("%s:%d Can't handle %s",
                                            fname, ln, dtype)


class OutputFormat:
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
        self.function_table = set()
        self.config = None

    def set_config(self, config):
        self.config = config

    def set_filter(self, export, internal, symbol, nosymbol, function_table,
                   enable_lineno):
        """
        Initialize filter variables according with the requested mode.

        Only one choice is valid between export, internal and symbol.

        The nosymbol filter can be used on all modes.
        """

        self.enable_lineno = enable_lineno

        if symbol:
            self.out_mode = self.OUTPUT_INCLUDE
            function_table = symbol
        elif export:
            self.out_mode = self.OUTPUT_EXPORTED
        elif internal:
            self.out_mode = self.OUTPUT_INTERNAL
        else:
            self.out_mode = self.OUTPUT_ALL

        if nosymbol:
            self.nosymbol = set(nosymbol)

        if function_table:
            self.function_table = function_table

    def highlight_block(self, block):
        """
        Apply the RST highlights to a sub-block of text.
        """

        for r, sub in self.highlights:
            block = r.sub(sub, block)

        return block

    def check_doc(self, name):
        """Check if DOC should be output"""

        if self.out_mode == self.OUTPUT_ALL:
            return True

        if self.out_mode == self.OUTPUT_INCLUDE:
            if name in self.nosymbol:
                return False

            if name in self.function_table:
                return True

        return False

    def check_declaration(self, dtype, name):
        if name in self.nosymbol:
            return False

        if self.out_mode == self.OUTPUT_ALL:
            return True

        if self.out_mode in [ self.OUTPUT_INCLUDE, self.OUTPUT_EXPORTED ]:
            if name in self.function_table:
                return True

        if self.out_mode == self.OUTPUT_INTERNAL:
            if dtype != "function":
                return True

            if name not in self.function_table:
                return True

        return False

    def check_function(self, fname, name, args):
        return True

    def check_enum(self, fname, name, args):
        return True

    def check_typedef(self, fname, name, args):
        return True

    def msg(self, fname, name, args):

        dtype = args.get('type', "")

        if dtype == "doc":
            self.out_doc(fname, name, args)
            return False

        if not self.check_declaration(dtype, name):
            return False

        if dtype == "function":
            self.out_function(fname, name, args)
            return False

        if dtype == "enum":
            self.out_enum(fname, name, args)
            return False

        if dtype == "typedef":
            self.out_typedef(fname, name, args)
            return False

        if dtype in ["struct", "union"]:
            self.out_struct(fname, name, args)
            return False

        # Warn if some type requires an output logic
        self.config.log.warning("doesn't now how to output '%s' block",
                                dtype)

        return True

    # Virtual methods to be overridden by inherited classes
    def out_doc(self, fname, name, args):
        pass

    def out_function(self, fname, name, args):
        pass

    def out_enum(self, fname, name, args):
        pass

    def out_typedef(self, fname, name, args):
        pass

    def out_struct(self, fname, name, args):
        pass


class RestFormat(OutputFormat):
    # """Consts and functions used by ReST output"""

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

    sphinx_literal = Re(r'^[^.].*::$', cache=False)
    sphinx_cblock = Re(r'^\.\.\ +code-block::', cache=False)

    def __init__(self):
        """
        Creates class variables.

        Not really mandatory, but it is a good coding style and makes
        pylint happy.
        """

        super().__init__()
        self.lineprefix = ""

    def print_lineno (self, ln):
        """Outputs a line number"""

        if self.enable_lineno and ln:
            print(f".. LINENO {ln}")

    def output_highlight(self, args):
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
                        r = Re(r'^(\s*)')
                        if r.match(line):
                            litprefix = '^' + r.group(1)
                        else:
                            litprefix = ""

                        output += line + "\n"
                    elif not Re(litprefix).match(line):
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
            print(self.lineprefix + line)

    def out_section(self, args, out_reference=False):
        """
        Outputs a block section.

        This could use some work; it's used to output the DOC: sections, and
        starts by putting out the name of the doc section itself, but that
        tends to duplicate a header already in the template file.
        """

        sectionlist = args.get('sectionlist', [])
        sections = args.get('sections', {})
        section_start_lines = args.get('section_start_lines', {})

        for section in sectionlist:
            # Skip sections that are in the nosymbol_table
            if section in self.nosymbol:
                continue

            if not self.out_mode == self.OUTPUT_INCLUDE:
                if out_reference:
                    print(f".. _{section}:\n")

                if not self.symbol:
                    print(f'{self.lineprefix}**{section}**\n')

            self.print_lineno(section_start_lines.get(section, 0))
            self.output_highlight(sections[section])
            print()
        print()

    def out_doc(self, fname, name, args):
        if not self.check_doc(name):
            return

        self.out_section(args, out_reference=True)

    def out_function(self, fname, name, args):

        oldprefix = self.lineprefix
        signature = ""

        func_macro = args.get('func_macro', False)
        if func_macro:
            signature = args['function']
        else:
            if args.get('functiontype'):
                signature = args['functiontype'] + " "
            signature += args['function'] + " ("

        parameterlist = args.get('parameterlist', [])
        parameterdescs = args.get('parameterdescs', {})
        parameterdesc_start_lines = args.get('parameterdesc_start_lines', {})

        ln = args.get('ln', 0)

        count = 0
        for parameter in parameterlist:
            if count != 0:
                signature += ", "
            count += 1
            dtype = args['parametertypes'].get(parameter, "")

            if function_pointer.search(dtype):
                signature += function_pointer.group(1) + parameter + function_pointer.group(3)
            else:
                signature += dtype

        if not func_macro:
            signature += ")"

        if args.get('typedef') or not args.get('functiontype'):
            print(f".. c:macro:: {args['function']}\n")

            if args.get('typedef'):
                self.print_lineno(ln)
                print("   **Typedef**: ", end="")
                self.lineprefix = ""
                self.output_highlight(args.get('purpose', ""))
                print("\n\n**Syntax**\n")
                print(f"  ``{signature}``\n")
            else:
                print(f"``{signature}``\n")
        else:
            print(f".. c:function:: {signature}\n")

        if not args.get('typedef'):
            self.print_lineno(ln)
            self.lineprefix = "   "
            self.output_highlight(args.get('purpose', ""))
            print()

        # Put descriptive text into a container (HTML <div>) to help set
        # function prototypes apart
        self.lineprefix = "  "

        if parameterlist:
            print(".. container:: kernelindent\n")
            print(f"{self.lineprefix}**Parameters**\n")

        for parameter in parameterlist:
            parameter_name = Re(r'\[.*').sub('', parameter)
            dtype = args['parametertypes'].get(parameter, "")

            if dtype:
                print(f"{self.lineprefix}``{dtype}``")
            else:
                print(f"{self.lineprefix}``{parameter}``")

            self.print_lineno(parameterdesc_start_lines.get(parameter_name, 0))

            self.lineprefix = "    "
            if parameter_name in parameterdescs and \
               parameterdescs[parameter_name] != KernelDoc.undescribed:

                self.output_highlight(parameterdescs[parameter_name])
                print()
            else:
                print(f"{self.lineprefix}*undescribed*\n")
            self.lineprefix = "  "

        self.out_section(args)
        self.lineprefix = oldprefix

    def out_enum(self, fname, name, args):

        oldprefix = self.lineprefix
        name = args.get('enum', '')
        parameterlist = args.get('parameterlist', [])
        parameterdescs = args.get('parameterdescs', {})
        ln = args.get('ln', 0)

        print(f"\n\n.. c:enum:: {name}\n")

        self.print_lineno(ln)
        self.lineprefix = "  "
        self.output_highlight(args.get('purpose', ''))
        print()

        print(".. container:: kernelindent\n")
        outer = self.lineprefix + "  "
        self.lineprefix = outer + "  "
        print(f"{outer}**Constants**\n")

        for parameter in parameterlist:
            print(f"{outer}``{parameter}``")

            if parameterdescs.get(parameter, '') != KernelDoc.undescribed:
                self.output_highlight(parameterdescs[parameter])
            else:
                print(f"{self.lineprefix}*undescribed*\n")
            print()

        self.lineprefix = oldprefix
        self.out_section(args)

    def out_typedef(self, fname, name, args):

        oldprefix = self.lineprefix
        name = args.get('typedef', '')
        ln = args.get('ln', 0)

        print(f"\n\n.. c:type:: {name}\n")

        self.print_lineno(ln)
        self.lineprefix = "   "

        self.output_highlight(args.get('purpose', ''))

        print()

        self.lineprefix = oldprefix
        self.out_section(args)

    def out_struct(self, fname, name, args):

        name = args.get('struct', "")
        purpose = args.get('purpose', "")
        declaration = args.get('definition', "")
        dtype = args.get('type', "struct")
        ln = args.get('ln', 0)

        parameterlist = args.get('parameterlist', [])
        parameterdescs = args.get('parameterdescs', {})
        parameterdesc_start_lines = args.get('parameterdesc_start_lines', {})

        print(f"\n\n.. c:{dtype}:: {name}\n")

        self.print_lineno(ln)

        oldprefix = self.lineprefix
        self.lineprefix += "  "

        self.output_highlight(purpose)
        print()

        print(".. container:: kernelindent\n")
        print(f"{self.lineprefix}**Definition**::\n")

        self.lineprefix = self.lineprefix + "  "

        declaration = declaration.replace("\t", self.lineprefix)

        print(f"{self.lineprefix}{dtype} {name}" + ' {')
        print(f"{declaration}{self.lineprefix}" + "};\n")

        self.lineprefix = "  "
        print(f"{self.lineprefix}**Members**\n")
        for parameter in parameterlist:
            if not parameter or parameter.startswith("#"):
                continue

            parameter_name = parameter.split("[", maxsplit=1)[0]

            if parameterdescs.get(parameter_name) == KernelDoc.undescribed:
                continue

            self.print_lineno(parameterdesc_start_lines.get(parameter_name, 0))

            print(f"{self.lineprefix}``{parameter}``")

            self.lineprefix = "    "
            self.output_highlight(parameterdescs[parameter_name])
            self.lineprefix = "  "

            print()

        print()

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

    def __init__(self):
        """
        Creates class variables.

        Not really mandatory, but it is a good coding style and makes
        pylint happy.
        """

        super().__init__()

        dt = datetime.now()
        if os.environ.get("KBUILD_BUILD_TIMESTAMP", None):
            # use UTC TZ
            to_zone = tz.gettz('UTC')
            dt = dt.astimezone(to_zone)

        self.man_date = dt.strftime("%B %Y")

    def output_highlight(self, block):

        contents = self.highlight_block(block)

        if isinstance(contents, list):
            contents = "\n".join(contents)

        for line in contents.strip("\n").split("\n"):
            line = Re(r"^\s*").sub("", line)

            if line and line[0] == ".":
                print("\\&" + line)
            else:
                print(line)

    def out_doc(self, fname, name, args):
        module = args.get('module')
        sectionlist = args.get('sectionlist', [])
        sections = args.get('sections', {})

        print(f'.TH "{module}" 9 "{module}" "{self.man_date}" "API Manual" LINUX')

        for section in sectionlist:
            print(f'.SH "{section}"')
            self.output_highlight(sections.get(section))

    def out_function(self, fname, name, args):
        """output function in man"""

        parameterlist = args.get('parameterlist', [])
        parameterdescs = args.get('parameterdescs', {})
        sectionlist = args.get('sectionlist', [])
        sections = args.get('sections', {})

        print(f'.TH "{args['function']}" 9 "{args['function']}" "{self.man_date}" "Kernel Hacker\'s Manual" LINUX')

        print(".SH NAME")
        print(f"{args['function']} \\- {args['purpose']}")

        print(".SH SYNOPSIS")
        if args.get('functiontype', ''):
            print(f'.B "{args['functiontype']}" {args['function']}')
        else:
            print(f'.B "{args['function']}')

        count = 0
        parenth = "("
        post = ","

        for parameter in parameterlist:
            if count == len(parameterlist) - 1:
                post = ");"

            dtype = args['parametertypes'].get(parameter, "")
            if function_pointer.match(dtype):
                # Pointer-to-function
                print(f'".BI "{parenth}{function_pointer.group(1)}" " ") ({function_pointer.group(2)}){post}"')
            else:
                dtype = Re(r'([^\*])$').sub(r'\1 ', dtype)

                print(f'.BI "{parenth}{dtype}"  "{post}"')
            count += 1
            parenth = ""

        if parameterlist:
            print(".SH ARGUMENTS")

        for parameter in parameterlist:
            parameter_name = re.sub(r'\[.*', '', parameter)

            print(f'.IP "{parameter}" 12')
            self.output_highlight(parameterdescs.get(parameter_name, ""))

        for section in sectionlist:
            print(f'.SH "{section.upper()}"')
            self.output_highlight(sections[section])

    def out_enum(self, fname, name, args):

        name = args.get('enum', '')
        parameterlist = args.get('parameterlist', [])
        sectionlist = args.get('sectionlist', [])
        sections = args.get('sections', {})

        print(f'.TH "{args['module']}" 9 "enum {args['enum']}" "{self.man_date}" "API Manual" LINUX')

        print(".SH NAME")
        print(f"enum {args['enum']} \\- {args['purpose']}")

        print(".SH SYNOPSIS")
        print(f"enum {args['enum']}" + " {")

        count = 0
        for parameter in parameterlist:
            print(f'.br\n.BI "    {parameter}"')
            if count == len(parameterlist) - 1:
                print("\n};")
            else:
                print(", \n.br")

            count += 1

        print(".SH Constants")

        for parameter in parameterlist:
            parameter_name = Re(r'\[.*').sub('', parameter)
            print(f'.IP "{parameter}" 12')
            self.output_highlight(args['parameterdescs'].get(parameter_name, ""))

        for section in sectionlist:
            print(f'.SH "{section}"')
            self.output_highlight(sections[section])

    def out_typedef(self, fname, name, args):
        module = args.get('module')
        typedef = args.get('typedef')
        purpose = args.get('purpose')
        sectionlist = args.get('sectionlist', [])
        sections = args.get('sections', {})

        print(f'.TH "{module}" 9 "{typedef}" "{self.man_date}" "API Manual" LINUX')

        print(".SH NAME")
        print(f"typedef {typedef} \\- {purpose}")

        for section in sectionlist:
            print(f'.SH "{section}"')
            self.output_highlight(sections.get(section))

    def out_struct(self, fname, name, args):
        module = args.get('module')
        struct_type = args.get('type')
        struct_name = args.get('struct')
        purpose = args.get('purpose')
        definition = args.get('definition')
        sectionlist = args.get('sectionlist', [])
        parameterlist = args.get('parameterlist', [])
        sections = args.get('sections', {})
        parameterdescs = args.get('parameterdescs', {})

        print(f'.TH "{module}" 9 "{struct_type} {struct_name}" "{self.man_date}" "API Manual" LINUX')

        print(".SH NAME")
        print(f"{struct_type} {struct_name} \\- {purpose}")

        # Replace tabs with two spaces and handle newlines
        declaration = definition.replace("\t", "  ")
        declaration = Re(r"\n").sub('"\n.br\n.BI "', declaration)

        print(".SH SYNOPSIS")
        print(f"{struct_type} {struct_name} " + "{" +"\n.br")
        print(f'.BI "{declaration}\n' + "};\n.br\n")

        print(".SH Members")
        for parameter in parameterlist:
            if parameter.startswith("#"):
                continue

            parameter_name = re.sub(r"\[.*", "", parameter)

            if parameterdescs.get(parameter_name) == KernelDoc.undescribed:
                continue

            print(f'.IP "{parameter}" 12')
            self.output_highlight(parameterdescs.get(parameter_name))

        for section in sectionlist:
            print(f'.SH "{section}"')
            self.output_highlight(sections.get(section))


# Command line interface


DESC = """
Read C language source or header FILEs, extract embedded documentation comments,
and print formatted documentation to standard output.

The documentation comments are identified by the "/**" opening comment mark.

See Documentation/doc-guide/kernel-doc.rst for the documentation comment syntax.
"""

EXPORT_FILE_DESC = """
Specify an additional FILE in which to look for EXPORT_SYMBOL information.

May be used multiple times.
"""

EXPORT_DESC = """
Only output documentation for the symbols that have been
exported using EXPORT_SYMBOL() and related macros in any input
FILE or -export-file FILE.
"""

INTERNAL_DESC = """
Only output documentation for the symbols that have NOT been
exported using EXPORT_SYMBOL() and related macros in any input
FILE or -export-file FILE.
"""

FUNCTION_DESC = """
Only output documentation for the given function or DOC: section
title. All other functions and DOC: sections are ignored.

May be used multiple times.
"""

NOSYMBOL_DESC = """
Exclude the specified symbol from the output documentation.

May be used multiple times.
"""

FILES_DESC = """
Header and C source files to be parsed.
"""

WARN_CONTENTS_BEFORE_SECTIONS_DESC = """
Warns if there are contents before sections (deprecated).

This option is kept just for backward-compatibility, but it does nothing,
neither here nor at the original Perl script.
"""


class MsgFormatter(logging.Formatter):
    def format(self, record):
        record.levelname = record.levelname.capitalize()
        return logging.Formatter.format(self, record)

def main():
    """Main program"""

    parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter,
                                     description=DESC)

    # Normal arguments

    parser.add_argument("-v", "-verbose", "--verbose", action="store_true",
                        help="Verbose output, more warnings and other information.")

    parser.add_argument("-d", "-debug", "--debug", action="store_true",
                        help="Enable debug messages")

    parser.add_argument("-M", "-modulename", "--modulename",
                        help="Allow setting a module name at the output.")

    parser.add_argument("-l", "-enable-lineno", "--enable_lineno",
                        action="store_true",
                        help="Enable line number output (only in ReST mode)")

    # Arguments to control the warning behavior

    parser.add_argument("-Wreturn", "--wreturn", action="store_true",
                        help="Warns about the lack of a return markup on functions.")

    parser.add_argument("-Wshort-desc", "-Wshort-description", "--wshort-desc",
                        action="store_true",
                        help="Warns if initial short description is missing")

    parser.add_argument("-Wcontents-before-sections",
                        "--wcontents-before-sections", action="store_true",
                        help=WARN_CONTENTS_BEFORE_SECTIONS_DESC)

    parser.add_argument("-Wall", "--wall", action="store_true",
                        help="Enable all types of warnings")

    parser.add_argument("-Werror", "--werror", action="store_true",
                        help="Treat warnings as errors.")

    parser.add_argument("-export-file", "--export-file", action='append',
                        help=EXPORT_FILE_DESC)

    # Output format mutually-exclusive group

    out_group = parser.add_argument_group("Output format selection (mutually exclusive)")

    out_fmt = out_group.add_mutually_exclusive_group()

    out_fmt.add_argument("-m", "-man", "--man", action="store_true",
                         help="Output troff manual page format.")
    out_fmt.add_argument("-r", "-rst", "--rst", action="store_true",
                         help="Output reStructuredText format (default).")
    out_fmt.add_argument("-N", "-none", "--none", action="store_true",
                         help="Do not output documentation, only warnings.")

    # Output selection mutually-exclusive group

    sel_group = parser.add_argument_group("Output selection (mutually exclusive)")
    sel_mut = sel_group.add_mutually_exclusive_group()

    sel_mut.add_argument("-e", "-export", "--export", action='store_true',
                         help=EXPORT_DESC)

    sel_mut.add_argument("-i", "-internal", "--internal", action='store_true',
                         help=INTERNAL_DESC)

    sel_mut.add_argument("-s", "-function", "--symbol", action='append',
                         help=FUNCTION_DESC)

    # This one is valid for all 3 types of filter
    parser.add_argument("-n", "-nosymbol", "--nosymbol", action='append',
                         help=NOSYMBOL_DESC)

    parser.add_argument("files", metavar="FILE",
                        nargs="+", help=FILES_DESC)

    args = parser.parse_args()

    if args.wall:
        args.wreturn = True
        args.wshort_desc = True
        args.wcontents_before_sections = True

    logger = logging.getLogger()

    if not args.debug:
        logger.setLevel(logging.INFO)
    else:
        logger.setLevel(logging.DEBUG)

    formatter = MsgFormatter('%(levelname)s: %(message)s')

    handler = logging.StreamHandler()
    handler.setFormatter(formatter)

    logger.addHandler(handler)

    if args.man:
        out_style = ManFormat()
    elif args.none:
        out_style = None
    else:
        out_style = RestFormat()

    kfiles = KernelFiles(files=args.files, verbose=args.verbose,
                         out_style=out_style, werror=args.werror,
                         wreturn=args.wreturn, wshort_desc=args.wshort_desc,
                         wcontents_before_sections=args.wcontents_before_sections,
                         modulename=args.modulename,
                         export_file=args.export_file)

    kfiles.parse()

    kfiles.msg(enable_lineno=args.enable_lineno, export=args.export,
               internal=args.internal, symbol=args.symbol,
               nosymbol=args.nosymbol)


# Call main method
if __name__ == "__main__":
    main()
