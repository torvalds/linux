#!/usr/bin/env python3
# ===- lib/dfsan/scripts/build-libc-list.py ---------------------------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===#
# The purpose of this script is to identify every function symbol in a set of
# libraries (in this case, libc and libgcc) so that they can be marked as
# uninstrumented, thus allowing the instrumentation pass to treat calls to those
# functions correctly.

# Typical usage will list runtime libraries which are not instrumented by dfsan.
# This would include libc, and compiler builtins.
#
# ./build-libc-list.py \
#    --lib-file=/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 \
#    --lib-file=/lib/x86_64-linux-gnu/libanl.so.1 \
#    --lib-file=/lib/x86_64-linux-gnu/libBrokenLocale.so.1 \
#    --lib-file=/lib/x86_64-linux-gnu/libcidn.so.1 \
#    --lib-file=/lib/x86_64-linux-gnu/libcrypt.so.1 \
#    --lib-file=/lib/x86_64-linux-gnu/libc.so.6 \
#    --lib-file=/lib/x86_64-linux-gnu/libdl.so.2 \
#    --lib-file=/lib/x86_64-linux-gnu/libm.so.6 \
#    --lib-file=/lib/x86_64-linux-gnu/libnsl.so.1 \
#    --lib-file=/lib/x86_64-linux-gnu/libpthread.so.0 \
#    --lib-file=/lib/x86_64-linux-gnu/libresolv.so.2 \
#    --lib-file=/lib/x86_64-linux-gnu/librt.so.1 \
#    --lib-file=/lib/x86_64-linux-gnu/libthread_db.so.1 \
#    --lib-file=/lib/x86_64-linux-gnu/libutil.so.1 \
#    --lib-file=/usr/lib/x86_64-linux-gnu/libc_nonshared.a \
#    --lib-file=/usr/lib/x86_64-linux-gnu/libpthread_nonshared.a \
#    --lib-file=/lib/x86_64-linux-gnu/libgcc_s.so.1 \
#    --lib-file=/usr/lib/gcc/x86_64-linux-gnu/4.6/libgcc.a \
#    --error-missing-lib

import os
import subprocess
import sys
from optparse import OptionParser


def defined_function_list(lib):
    """Get non-local function symbols from lib."""
    functions = []
    readelf_proc = subprocess.Popen(
        ["readelf", "-s", "-W", lib], stdout=subprocess.PIPE
    )
    readelf = readelf_proc.communicate()[0].decode().split("\n")
    if readelf_proc.returncode != 0:
        raise subprocess.CalledProcessError(readelf_proc.returncode, "readelf")
    for line in readelf:
        if (
            (line[31:35] == "FUNC" or line[31:36] == "IFUNC")
            and line[39:44] != "LOCAL"
            and line[55:58] != "UND"
        ):
            function_name = line[59:].split("@")[0]
            functions.append(function_name)
    return functions


p = OptionParser()

p.add_option(
    "--lib-file",
    action="append",
    metavar="PATH",
    help="Specific library files to add.",
    default=[],
)

p.add_option(
    "--error-missing-lib",
    action="store_true",
    help="Make this script exit with an error code if any library is missing.",
    dest="error_missing_lib",
    default=False,
)

(options, args) = p.parse_args()

libs = options.lib_file
if not libs:
    print("No libraries provided.", file=sys.stderr)
    exit(1)

missing_lib = False
functions = []
for l in libs:
    if os.path.exists(l):
        functions += defined_function_list(l)
    else:
        missing_lib = True
        print("warning: library %s not found" % l, file=sys.stderr)

if options.error_missing_lib and missing_lib:
    print("Exiting with failure code due to missing library.", file=sys.stderr)
    exit(1)

for f in sorted(set(functions)):
    print("fun:%s=uninstrumented" % f)
