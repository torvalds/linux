# -*- Python -*- vim: set syntax=python tabstop=4 expandtab cc=80:
# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##
"""
extract - A set of function that extract symbol lists from shared libraries.
"""
import os.path
from os import environ
import re
import shutil
import subprocess
import sys

from libcxx.sym_check import util

extract_ignore_names = ["_init", "_fini"]


class NMExtractor(object):
    """
    NMExtractor - Extract symbol lists from libraries using nm.
    """

    @staticmethod
    def find_tool():
        """
        Search for the nm executable and return the path.
        """
        return shutil.which("nm")

    def __init__(self, static_lib):
        """
        Initialize the nm executable and flags that will be used to extract
        symbols from shared libraries.
        """
        self.nm_exe = self.find_tool()
        if self.nm_exe is None:
            # ERROR no NM found
            print("ERROR: Could not find nm")
            sys.exit(1)
        self.static_lib = static_lib
        self.flags = ["-P", "-g"]
        if sys.platform.startswith("aix"):
            # AIX nm demangles symbols by default, so suppress that.
            self.flags.append("-C")

    def extract(self, lib):
        """
        Extract symbols from a library and return the results as a dict of
        parsed symbols.
        """
        cmd = [self.nm_exe] + self.flags + [lib]
        out = subprocess.check_output(cmd).decode()
        fmt_syms = (self._extract_sym(l) for l in out.splitlines() if l.strip())
        # Cast symbol to string.
        final_syms = (repr(s) for s in fmt_syms if self._want_sym(s))
        # Make unique and sort strings.
        tmp_list = list(sorted(set(final_syms)))
        # Cast string back to symbol.
        return util.read_syms_from_list(tmp_list)

    def _extract_sym(self, sym_str):
        bits = sym_str.split()
        # Everything we want has at least two columns.
        if len(bits) < 2:
            return None
        new_sym = {
            "name": bits[0],
            "type": bits[1],
            "is_defined": (bits[1].lower() != "u"),
        }
        new_sym["name"] = new_sym["name"].replace("@@", "@")
        new_sym = self._transform_sym_type(new_sym)
        # NM types which we want to save the size for.
        if new_sym["type"] == "OBJECT" and len(bits) > 3:
            new_sym["size"] = int(bits[3], 16)
        return new_sym

    @staticmethod
    def _want_sym(sym):
        """
        Check that s is a valid symbol that we want to keep.
        """
        if sym is None or len(sym) < 2:
            return False
        if sym["name"] in extract_ignore_names:
            return False
        bad_types = ["t", "b", "r", "d", "w"]
        return sym["type"] not in bad_types and sym["name"] not in [
            "__bss_start",
            "_end",
            "_edata",
        ]

    @staticmethod
    def _transform_sym_type(sym):
        """
        Map the nm single letter output for type to either FUNC or OBJECT.
        If the type is not recognized it is left unchanged.
        """
        func_types = ["T", "W"]
        obj_types = ["B", "D", "R", "V", "S"]
        if sym["type"] in func_types:
            sym["type"] = "FUNC"
        elif sym["type"] in obj_types:
            sym["type"] = "OBJECT"
        return sym


class ReadElfExtractor(object):
    """
    ReadElfExtractor - Extract symbol lists from libraries using readelf.
    """

    @staticmethod
    def find_tool():
        """
        Search for the readelf executable and return the path.
        """
        return shutil.which("readelf")

    def __init__(self, static_lib):
        """
        Initialize the readelf executable and flags that will be used to
        extract symbols from shared libraries.
        """
        self.tool = self.find_tool()
        if self.tool is None:
            # ERROR no NM found
            print("ERROR: Could not find readelf")
            sys.exit(1)
        # TODO: Support readelf for reading symbols from archives
        assert not static_lib and "RealElf does not yet support static libs"
        self.flags = ["--wide", "--symbols"]

    def extract(self, lib):
        """
        Extract symbols from a library and return the results as a dict of
        parsed symbols.
        """
        cmd = [self.tool] + self.flags + [lib]
        out = subprocess.check_output(cmd).decode()
        dyn_syms = self.get_dynsym_table(out)
        return self.process_syms(dyn_syms)

    def process_syms(self, sym_list):
        new_syms = []
        for s in sym_list:
            parts = s.split()
            if not parts:
                continue
            assert len(parts) == 7 or len(parts) == 8 or len(parts) == 9
            if len(parts) == 7:
                continue
            new_sym = {
                "name": parts[7],
                "size": int(parts[2]),
                "type": parts[3],
                "is_defined": (parts[6] != "UND"),
            }
            assert new_sym["type"] in ["OBJECT", "FUNC", "NOTYPE", "TLS"]
            if new_sym["name"] in extract_ignore_names:
                continue
            if new_sym["type"] == "NOTYPE":
                continue
            if new_sym["type"] == "FUNC":
                del new_sym["size"]
            new_syms += [new_sym]
        return new_syms

    def get_dynsym_table(self, out):
        lines = out.splitlines()
        start = -1
        end = -1
        for i in range(len(lines)):
            # Accept both GNU and ELF Tool Chain readelf format.  Some versions
            # of ELF Tool Chain readelf use ( ) around the symbol table name
            # instead of ' ', and omit the blank line before the heading.
            if re.match(r"Symbol table ['(].dynsym[')]", lines[i]):
                start = i + 2
            elif start != -1 and end == -1:
                if not lines[i].strip():
                    end = i + 1
                if lines[i].startswith("Symbol table ("):
                    end = i
        assert start != -1
        if end == -1:
            end = len(lines)
        return lines[start:end]


class AIXDumpExtractor(object):
    """
    AIXDumpExtractor - Extract symbol lists from libraries using AIX dump.
    """

    @staticmethod
    def find_tool():
        """
        Search for the dump executable and return the path.
        """
        return shutil.which("dump")

    def __init__(self, static_lib):
        """
        Initialize the dump executable and flags that will be used to
        extract symbols from shared libraries.
        """
        # TODO: Support dump for reading symbols from static libraries
        assert not static_lib and "static libs not yet supported with dump"
        self.tool = self.find_tool()
        if self.tool is None:
            print("ERROR: Could not find dump")
            sys.exit(1)
        self.flags = ["-n", "-v"]
        object_mode = environ.get("OBJECT_MODE")
        if object_mode == "32":
            self.flags += ["-X32"]
        elif object_mode == "64":
            self.flags += ["-X64"]
        else:
            self.flags += ["-X32_64"]

    def extract(self, lib):
        """
        Extract symbols from a library and return the results as a dict of
        parsed symbols.
        """
        cmd = [self.tool] + self.flags + [lib]
        out = subprocess.check_output(cmd).decode()
        loader_syms = self.get_loader_symbol_table(out)
        return self.process_syms(loader_syms)

    def process_syms(self, sym_list):
        new_syms = []
        for s in sym_list:
            parts = s.split()
            if not parts:
                continue
            assert len(parts) == 8 or len(parts) == 7
            if len(parts) == 7:
                continue
            new_sym = {
                "name": parts[7],
                "type": "FUNC" if parts[4] == "DS" else "OBJECT",
                "is_defined": (parts[5] != "EXTref"),
                "storage_mapping_class": parts[4],
                "import_export": parts[3],
            }
            if new_sym["name"] in extract_ignore_names:
                continue
            new_syms += [new_sym]
        return new_syms

    def get_loader_symbol_table(self, out):
        lines = out.splitlines()
        return filter(lambda n: re.match(r"^\[[0-9]+\]", n), lines)

    @staticmethod
    def is_shared_lib(lib):
        """
        Check for the shared object flag in XCOFF headers of the input file or
        library archive.
        """
        dump = AIXDumpExtractor.find_tool()
        if dump is None:
            print("ERROR: Could not find dump")
            sys.exit(1)
        cmd = [dump, "-X32_64", "-ov", lib]
        out = subprocess.check_output(cmd).decode()
        return out.find("SHROBJ") != -1


def is_static_library(lib_file):
    """
    Determine if a given library is static or shared.
    """
    if sys.platform.startswith("aix"):
        # An AIX library could be both, but for simplicity assume it isn't.
        return not AIXDumpExtractor.is_shared_lib(lib_file)
    else:
        _, ext = os.path.splitext(lib_file)
        return ext == ".a"


def extract_symbols(lib_file, static_lib=None):
    """
    Extract and return a list of symbols extracted from a static or dynamic
    library. The symbols are extracted using dump, nm or readelf. They are
    then filtered and formated. Finally the symbols are made unique.
    """
    if static_lib is None:
        static_lib = is_static_library(lib_file)
    if sys.platform.startswith("aix"):
        extractor = AIXDumpExtractor(static_lib=static_lib)
    elif ReadElfExtractor.find_tool() and not static_lib:
        extractor = ReadElfExtractor(static_lib=static_lib)
    else:
        extractor = NMExtractor(static_lib=static_lib)
    return extractor.extract(lib_file)
