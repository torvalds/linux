#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2025: Mauro Carvalho Chehab <mchehab@kernel.org>.
#
# pylint: disable=R0903,R0913,R0914,R0917

"""
Classes for navigating through the files that kernel-doc needs to handle
to generate documentation.
"""

import logging
import os
import re

from kdoc.kdoc_parser import KernelDoc
from kdoc.xforms_lists import CTransforms
from kdoc.kdoc_output import OutputFormat
from kdoc.kdoc_yaml_file import KDocTestFile


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
        """Internal function to parse files recursively."""

        with os.scandir(dirname) as obj:
            for entry in obj:
                name = os.path.join(dirname, entry.name)

                if entry.is_dir(follow_symlinks=False):
                    yield from self._parse_dir(name)

                if not entry.is_file():
                    continue

                basename = os.path.basename(name)

                if not basename.endswith(self.extensions):
                    continue

                yield name

    def parse_files(self, file_list, file_not_found_cb):
        """
        Define an iterator to parse all source files from file_list,
        handling directories if any.
        """

        if not file_list:
            return

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


class KdocConfig():
    """
    Stores all configuration attributes that kdoc_parser and kdoc_output
    needs.
    """
    def __init__(self, verbose=False, werror=False, wreturn=False,
                 wshort_desc=False, wcontents_before_sections=False,
                 logger=None):

        self.verbose = verbose
        self.werror = werror
        self.wreturn = wreturn
        self.wshort_desc =  wshort_desc
        self.wcontents_before_sections = wcontents_before_sections

        if logger:
            self.log = logger
        else:
            self.log = logging.getLogger(__file__)

        self.warning = self.log.warning

class KernelFiles():
    """
    Parse kernel-doc tags on multiple kernel source files.

    This is the main entry point to run kernel-doc. This class is initialized
    using a series of optional arguments:

    ``verbose``
        If True, enables kernel-doc verbosity. Default: False.

    ``out_style``
        Class to be used to format output. If None (default),
        only report errors.

    ``xforms``
        Transforms to be applied to C prototypes and data structs.
        If not specified, defaults to xforms = CFunction()

    ``werror``
        If True, treat warnings as errors, retuning an error code on warnings.

        Default: False.

    ``wreturn``
        If True, warns about the lack of a return markup on functions.

        Default: False.
    ``wshort_desc``
        If True, warns if initial short description is missing.

        Default: False.

    ``wcontents_before_sections``
        If True, warn if there are contents before sections (deprecated).
        This option is kept just for backward-compatibility, but it does
        nothing, neither here nor at the original Perl script.

        Default: False.

    ``logger``
        Optional logger class instance.

        If not specified, defaults to use: ``logging.getLogger("kernel-doc")``

    ``yaml_file``
        If defined, stores the output inside a YAML file.

    ``yaml_content``
        Defines what will be inside the YAML file.

    Note:
        There are two type of parsers defined here:

        - self.parse_file(): parses both kernel-doc markups and
          ``EXPORT_SYMBOL*`` macros;
        - self.process_export_file(): parses only ``EXPORT_SYMBOL*`` macros.
    """

    def warning(self, msg):
        """Ancillary routine to output a warning and increment error count."""

        self.config.log.warning(msg)
        self.errors += 1

    def error(self, msg):
        """Ancillary routine to output an error and increment error count."""

        self.config.log.error(msg)
        self.errors += 1

    def parse_file(self, fname):
        """
        Parse a single Kernel source.
        """

        # Prevent parsing the same file twice if results are cached
        if fname in self.files:
            return

        if self.test_file:
            store_src = True
        else:
            store_src = False

        doc = KernelDoc(self.config, fname, self.xforms, store_src=store_src)
        export_table, entries = doc.parse_kdoc()

        self.export_table[fname] = export_table

        self.files.add(fname)
        self.export_files.add(fname)      # parse_kdoc() already check exports

        self.results[fname] = entries

    def process_export_file(self, fname):
        """
        Parses ``EXPORT_SYMBOL*`` macros from a single Kernel source file.
        """

        # Prevent parsing the same file twice if results are cached
        if fname in self.export_files:
            return

        doc = KernelDoc(self.config, fname)
        export_table = doc.parse_export()

        if not export_table:
            self.error(f"Error: Cannot check EXPORT_SYMBOL* on {fname}")
            export_table = set()

        self.export_table[fname] = export_table
        self.export_files.add(fname)

    def file_not_found_cb(self, fname):
        """
        Callback to warn if a file was not found.
        """

        self.error(f"Cannot find file {fname}")

    def __init__(self, verbose=False, out_style=None, xforms=None,
                 werror=False, wreturn=False, wshort_desc=False,
                 wcontents_before_sections=False,
                 yaml_file=None, yaml_content=None, logger=None):
        """
        Initialize startup variables and parse all files.
        """

        if not verbose:
            try:
                verbose = bool(int(os.environ.get("KBUILD_VERBOSE", 0)))
            except ValueError:
                # Handles an eventual case where verbosity is not a number
                # like KBUILD_VERBOSE=""
                verbose = False

        if out_style is None:
            out_style = OutputFormat()

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

        if not logger:
           logger = logging.getLogger("kernel-doc")
        else:
            logger = logger

        # Some variables are global to the parser logic as a whole as they are
        # used to send control configuration to KernelDoc class. As such,
        # those variables are read-only inside the KernelDoc.
        self.config = KdocConfig(verbose, werror, wreturn, wshort_desc,
                                 wcontents_before_sections, logger)

        # Override log warning, as we want to count errors
        self.config.warning = self.warning

        if yaml_file:
            self.test_file = KDocTestFile(self.config, yaml_file, yaml_content)
        else:
            self.test_file = None

        if xforms:
            self.xforms = xforms
        else:
            self.xforms = CTransforms()

        self.config.src_tree = os.environ.get("SRCTREE", None)

        # Initialize variables that are internal to KernelFiles

        self.out_style = out_style
        self.out_style.set_config(self.config)

        self.errors = 0
        self.results = {}

        self.files = set()
        self.export_files = set()
        self.export_table = {}

    def parse(self, file_list, export_file=None):
        """
        Parse all files.
        """

        glob = GlobSourceFiles(srctree=self.config.src_tree)

        for fname in glob.parse_files(file_list, self.file_not_found_cb):
            self.parse_file(fname)

        for fname in glob.parse_files(export_file, self.file_not_found_cb):
            self.process_export_file(fname)

    def out_msg(self, fname, name, arg):
        """
        Return output messages from a file name using the output style
        filtering.

        If output type was not handled by the styler, return None.
        """

        # NOTE: we can add rules here to filter out unwanted parts,
        # although OutputFormat.msg already does that.

        return self.out_style.msg(fname, name, arg)

    def msg(self, enable_lineno=False, export=False, internal=False,
            symbol=None, nosymbol=None, no_doc_sections=False,
            filenames=None, export_file=None):
        """
        Interacts over the kernel-doc results and output messages,
        returning kernel-doc markups on each interaction.
        """

        if not filenames:
            filenames = sorted(self.results.keys())

        glob = GlobSourceFiles(srctree=self.config.src_tree)

        for fname in filenames:
            function_table = set()

            if internal or export:
                if not export_file:
                    export_file = [fname]

                for f in glob.parse_files(export_file, self.file_not_found_cb):
                    function_table |= self.export_table[f]

            if symbol:
                for s in symbol:
                    function_table.add(s)

            if fname not in self.results:
                self.config.log.warning("No kernel-doc for file %s", fname)
                continue

            symbols = self.results[fname]

            if self.test_file:
                self.test_file.set_filter(export, internal, symbol, nosymbol,
                                          function_table, enable_lineno,
                                          no_doc_sections)

                self.test_file.output_symbols(fname, symbols)

                continue

            self.out_style.set_filter(export, internal, symbol, nosymbol,
                                      function_table, enable_lineno,
                                      no_doc_sections)

            msg = self.out_style.output_symbols(fname, symbols)
            if msg:
                yield fname, msg

        if self.test_file:
            self.test_file.write()
