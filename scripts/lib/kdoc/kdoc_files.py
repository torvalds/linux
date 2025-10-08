#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2025: Mauro Carvalho Chehab <mchehab@kernel.org>.
#
# pylint: disable=R0903,R0913,R0914,R0917

"""
Parse lernel-doc tags on multiple kernel source files.
"""

import argparse
import logging
import os
import re

from kdoc_parser import KernelDoc
from kdoc_output import OutputFormat


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
        """
        Define an interator to parse all source files from file_list,
        handling directories if any
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


class KernelFiles():
    """
    Parse kernel-doc tags on multiple kernel source files.

    There are two type of parsers defined here:
        - self.parse_file(): parses both kernel-doc markups and
          EXPORT_SYMBOL* macros;
        - self.process_export_file(): parses only EXPORT_SYMBOL* macros.
    """

    def warning(self, msg):
        """Ancillary routine to output a warning and increment error count"""

        self.config.log.warning(msg)
        self.errors += 1

    def error(self, msg):
        """Ancillary routine to output an error and increment error count"""

        self.config.log.error(msg)
        self.errors += 1

    def parse_file(self, fname):
        """
        Parse a single Kernel source.
        """

        # Prevent parsing the same file twice if results are cached
        if fname in self.files:
            return

        doc = KernelDoc(self.config, fname)
        export_table, entries = doc.parse_kdoc()

        self.export_table[fname] = export_table

        self.files.add(fname)
        self.export_files.add(fname)      # parse_kdoc() already check exports

        self.results[fname] = entries

    def process_export_file(self, fname):
        """
        Parses EXPORT_SYMBOL* macros from a single Kernel source file.
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

    def __init__(self, verbose=False, out_style=None,
                 werror=False, wreturn=False, wshort_desc=False,
                 wcontents_before_sections=False,
                 logger=None):
        """
        Initialize startup variables and parse all files
        """

        if not verbose:
            verbose = bool(os.environ.get("KBUILD_VERBOSE", 0))

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

        # Some variables are global to the parser logic as a whole as they are
        # used to send control configuration to KernelDoc class. As such,
        # those variables are read-only inside the KernelDoc.
        self.config = argparse.Namespace

        self.config.verbose = verbose
        self.config.werror = werror
        self.config.wreturn = wreturn
        self.config.wshort_desc = wshort_desc
        self.config.wcontents_before_sections = wcontents_before_sections

        if not logger:
            self.config.log = logging.getLogger("kernel-doc")
        else:
            self.config.log = logger

        self.config.warning = self.warning

        self.config.src_tree = os.environ.get("SRCTREE", None)

        # Initialize variables that are internal to KernelFiles

        self.out_style = out_style

        self.errors = 0
        self.results = {}

        self.files = set()
        self.export_files = set()
        self.export_table = {}

    def parse(self, file_list, export_file=None):
        """
        Parse all files
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

        If output type was not handled by the syler, return None.
        """

        # NOTE: we can add rules here to filter out unwanted parts,
        # although OutputFormat.msg already does that.

        return self.out_style.msg(fname, name, arg)

    def msg(self, enable_lineno=False, export=False, internal=False,
            symbol=None, nosymbol=None, no_doc_sections=False,
            filenames=None, export_file=None):
        """
        Interacts over the kernel-doc results and output messages,
        returning kernel-doc markups on each interaction
        """

        self.out_style.set_config(self.config)

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

            self.out_style.set_filter(export, internal, symbol, nosymbol,
                                      function_table, enable_lineno,
                                      no_doc_sections)

            msg = ""
            if fname not in self.results:
                self.config.log.warning("No kernel-doc for file %s", fname)
                continue

            for arg in self.results[fname]:
                m = self.out_msg(fname, arg.name, arg)

                if m is None:
                    ln = arg.get("ln", 0)
                    dtype = arg.get('type', "")

                    self.config.log.warning("%s:%d Can't handle %s",
                                            fname, ln, dtype)
                else:
                    msg += m

            if msg:
                yield fname, msg
