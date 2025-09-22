# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##

from libcxx.header_information import module_headers
from libcxx.header_information import header_restrictions
from dataclasses import dataclass

### SkipDeclarations

# Ignore several declarations found in the includes.
#
# Part of these items are bugs other are not yet implemented features.
SkipDeclarations = dict()

# See comment in the header.
SkipDeclarations["cuchar"] = ["std::mbstate_t", "std::size_t"]

# Not in the synopsis.
SkipDeclarations["cwchar"] = ["std::FILE"]

# The operators are added for private types like __iom_t10.
SkipDeclarations["iomanip"] = ["std::operator<<", "std::operator>>"]

# This header also provides declarations in the namespace that might be
# an error.
SkipDeclarations["filesystem"] = [
    "std::filesystem::operator==",
    "std::filesystem::operator!=",
]

# This is a specialization for a private type
SkipDeclarations["iterator"] = ["std::pointer_traits"]

# TODO MODULES
# This definition is declared in string and defined in istream
# This declaration should be part of string
SkipDeclarations["istream"] = ["std::getline"]

# P1614 (at many places) and LWG3519 too.
SkipDeclarations["random"] = [
    "std::operator!=",
    # LWG3519 makes these hidden friends.
    # Note the older versions had the requirement of these operations but not in
    # the synopsis.
    "std::operator<<",
    "std::operator>>",
    "std::operator==",
]

# TODO MODULES remove zombie names
# https://libcxx.llvm.org/Status/Cxx20.html#note-p0619
SkipDeclarations["memory"] = [
    "std::return_temporary_buffer",
    "std::get_temporary_buffer",
]

# include/__type_traits/is_swappable.h
SkipDeclarations["type_traits"] = [
    "std::swap",
    # TODO MODULES gotten through __functional/unwrap_ref.h
    "std::reference_wrapper",
]

### ExtraDeclarations

# Add declarations in headers.
#
# Some headers have their defines in a different header, which may have
# additional declarations.
ExtraDeclarations = dict()
# This declaration is in the ostream header.
ExtraDeclarations["system_error"] = ["std::operator<<"]

# TODO MODULES avoid this work-around
# This is a work-around for the special math functions. They are declared in
# __math/special_functions.h. Adding this as an ExtraHeader works for the std
# module. However these functions are special; they are not available in the
# global namespace.
ExtraDeclarations["cmath"] = ["std::hermite", "std::hermitef", "std::hermitel"]

### ExtraHeader

# Adds extra headers file to scan
#
# Some C++ headers in libc++ are stored in multiple physical files. There is a
# pattern to find these files. However there are some exceptions these are
# listed here.
ExtraHeader = dict()
# locale has a file and not a subdirectory
ExtraHeader["locale"] = "v1/__locale$"
ExtraHeader["ranges"] = "v1/__fwd/subrange.h$"

# The extra header is needed since two headers are required to provide the
# same definition.
ExtraHeader["functional"] = "v1/__compare/compare_three_way.h$"


# newline needs to be escaped for the module partition output.
nl = "\\\\n"


@dataclass
class module_test_generator:
    tmp_prefix: str
    module_path: str
    clang_tidy: str
    clang_tidy_plugin: str
    compiler: str
    compiler_flags: str
    module: str

    def write_lit_configuration(self):
        print(
            f"""\
// UNSUPPORTED: c++03, c++11, c++14, c++17
// UNSUPPORTED: clang-modules-build

// REQUIRES: has-clang-tidy

// The GCC compiler flags are not always compatible with clang-tidy.
// UNSUPPORTED: gcc

// MODULE_DEPENDENCIES: {self.module}

// RUN: echo -n > {self.tmp_prefix}.all_partitions
"""
        )

    def process_module_partition(self, header, is_c_header):
        # Some headers cannot be included when a libc++ feature is disabled.
        # In that case include the header conditionally. The header __config
        # ensures the libc++ feature macros are available.
        if header in header_restrictions:
            include = (
                f"#include <__config>{nl}"
                f"#if {header_restrictions[header]}{nl}"
                f"#  include <{header}>{nl}"
                f"#endif{nl}"
            )
        else:
            include = f"#include <{header}>{nl}"

        module_files = f'#include \\"{self.module_path}/std/{header}.inc\\"{nl}'
        if is_c_header:
            module_files += (
                f'#include \\"{self.module_path}/std.compat/{header}.inc\\"{nl}'
            )

        # Generate a module partition for the header module includes. This
        # makes it possible to verify that all headers export all their
        # named declarations.
        print(
            '// RUN: echo -e "'
            f"module;{nl}"
            f"{include}{nl}"
            f"{nl}"
            f"// Use __libcpp_module_<HEADER> to ensure that modules{nl}"
            f"// are not named as keywords or reserved names.{nl}"
            f"export module std:__libcpp_module_{header};{nl}"
            f"{module_files}"
            f'" > {self.tmp_prefix}.{header}.cppm'
        )

        # Extract the information of the module partition using lang-tidy
        print(
            f"// RUN: {self.clang_tidy} {self.tmp_prefix}.{header}.cppm "
            "  --checks='-*,libcpp-header-exportable-declarations' "
            "  -config='{CheckOptions: [ "
            "    {"
            "      key: libcpp-header-exportable-declarations.Filename, "
            f"     value: {header}.inc"
            "    }, {"
            "      key: libcpp-header-exportable-declarations.FileType, "
            f"     value: {'CompatModulePartition' if is_c_header else 'ModulePartition'}"
            "    }, "
            "  ]}' "
            f"--load={self.clang_tidy_plugin} "
            f"-- {self.compiler_flags} "
            f"| sort > {self.tmp_prefix}.{header}.module"
        )
        print(
            f"// RUN: cat  {self.tmp_prefix}.{header}.module >> {self.tmp_prefix}.all_partitions"
        )

        return include

    def process_header(self, header, include, is_c_header):
        # Dump the information as found in the module by using the header file(s).
        skip_declarations = " ".join(SkipDeclarations.get(header, []))
        if skip_declarations:
            skip_declarations = (
                "{"
                "  key: libcpp-header-exportable-declarations.SkipDeclarations, "
                f' value: "{skip_declarations}" '
                "}, "
            )

        extra_declarations = " ".join(ExtraDeclarations.get(header, []))
        if extra_declarations:
            extra_declarations = (
                "{"
                "  key: libcpp-header-exportable-declarations.ExtraDeclarations, "
                f' value: "{extra_declarations}" '
                "}, "
            )

        extra_header = ExtraHeader.get(header, "")
        if extra_header:
            extra_header = (
                "{"
                "  key: libcpp-header-exportable-declarations.ExtraHeader, "
                f' value: "{extra_header}" '
                "}, "
            )

        # Clang-tidy needs a file input
        print(f'// RUN: echo -e "' f"{include}" f'" > {self.tmp_prefix}.{header}.cpp')
        print(
            f"// RUN: {self.clang_tidy} {self.tmp_prefix}.{header}.cpp "
            "  --checks='-*,libcpp-header-exportable-declarations' "
            "  -config='{CheckOptions: [ "
            "    {"
            "      key: libcpp-header-exportable-declarations.Filename, "
            f"     value: {header}"
            "    }, {"
            "      key: libcpp-header-exportable-declarations.FileType, "
            f"     value: {'CHeader' if is_c_header else 'Header'}"
            "    }, "
            f"   {skip_declarations} {extra_declarations} {extra_header}, "
            "  ]}' "
            f"--load={self.clang_tidy_plugin} "
            f"-- {self.compiler_flags} "
            f"| sort > {self.tmp_prefix}.{header}.include"
        )
        print(
            f"// RUN: diff -u {self.tmp_prefix}.{header}.module {self.tmp_prefix}.{header}.include"
        )

    def process_module(self, module):
        # Merge the data of the parts
        print(
            f"// RUN: sort -u -o {self.tmp_prefix}.all_partitions {self.tmp_prefix}.all_partitions"
        )

        # Dump the information as found in top-level module.
        print(
            f"// RUN: {self.clang_tidy} {self.module_path}/{module}.cppm "
            "  --checks='-*,libcpp-header-exportable-declarations' "
            "  -config='{CheckOptions: [ "
            "    {"
            "      key: libcpp-header-exportable-declarations.Header, "
            f"     value: {module}.cppm"
            "    }, {"
            "      key: libcpp-header-exportable-declarations.FileType, "
            "      value: Module"
            "    }, "
            "  ]}' "
            f"--load={self.clang_tidy_plugin} "
            f"-- {self.compiler_flags} "
            f"| sort > {self.tmp_prefix}.module"
        )

        # Compare the sum of the parts with the top-level module.
        print(
            f"// RUN: diff -u {self.tmp_prefix}.all_partitions {self.tmp_prefix}.module"
        )

    # Basic smoke test. Import a module and try to compile when using all
    # exported names. This validates the clang-tidy script does not
    # accidentally add named declarations to the list that are not available.
    def test_module(self, module):
        print(
            f"""\
// RUN: echo 'import {module};' > {self.tmp_prefix}.compile.pass.cpp
// RUN: cat {self.tmp_prefix}.all_partitions >> {self.tmp_prefix}.compile.pass.cpp
// RUN: {self.compiler} {self.compiler_flags} -fsyntax-only {self.tmp_prefix}.compile.pass.cpp
"""
        )

    def write_test(self, module, c_headers=[]):
        self.write_lit_configuration()

        # Validate all module parts.
        for header in module_headers:
            is_c_header = header in c_headers
            include = self.process_module_partition(header, is_c_header)
            self.process_header(header, include, is_c_header)

        self.process_module(module)
        self.test_module(module)
