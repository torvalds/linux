#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2025: Mauro Carvalho Chehab <mchehab@kernel.org>.
#
# pylint: disable=R0903,R0913,R0914,R0917

# TODO: implement warning filtering

"""
Parse lernel-doc tags on multiple kernel source files.
"""

import argparse
import logging
import os
import re
import sys
from datetime import datetime

from dateutil import tz

from kdoc_parser import KernelDoc


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
    Parse lernel-doc tags on multiple kernel source files.
    """

    def parse_file(self, fname):
        """
        Parse a single Kernel source.
        """

        doc = KernelDoc(self.config, fname)
        doc.run()

        return doc

    def process_export_file(self, fname):
        """
        Parses EXPORT_SYMBOL* macros from a single Kernel source file.
        """
        try:
            with open(fname, "r", encoding="utf8",
                      errors="backslashreplace") as fp:
                for line in fp:
                    KernelDoc.process_export(self.config.function_table, line)

        except IOError:
            print(f"Error: Cannot open fname {fname}", fname=sys.stderr)
            self.config.errors += 1

    def file_not_found_cb(self, fname):
        """
        Callback to warn if a file was not found.
        """

        self.config.log.error("Cannot find file %s", fname)
        self.config.errors += 1

    def __init__(self, files=None, verbose=False, out_style=None,
                 werror=False, wreturn=False, wshort_desc=False,
                 wcontents_before_sections=False,
                 logger=None, modulename=None, export_file=None):
        """
        Initialize startup variables and parse all files
        """

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
        """
        Return output messages from a file name using the output style
        filtering.

        If output type was not handled by the syler, return None.
        """

        # NOTE: we can add rules here to filter out unwanted parts,
        # although OutputFormat.msg already does that.

        return self.out_style.msg(fname, name, arg)

    def msg(self, enable_lineno=False, export=False, internal=False,
            symbol=None, nosymbol=None):
        """
        Interacts over the kernel-doc results and output messages,
        returning kernel-doc markups on each interaction
        """

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
            msg = ""
            for name, arg in arg_tuple:
                msg += self.out_msg(fname, name, arg)

                if msg is None:
                    ln = arg.get("ln", 0)
                    dtype = arg.get('type', "")

                    self.config.log.warning("%s:%d Can't handle %s",
                                            fname, ln, dtype)
            if msg:
                yield fname, msg
