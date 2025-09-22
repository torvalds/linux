"""
LLDB AppKit formatters

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""
import lldb
import lldb.runtime.objc.objc_runtime
import lldb.formatters.Logger


def Class_Summary(valobj, dict):
    logger = lldb.formatters.Logger.Logger()
    runtime = lldb.runtime.objc.objc_runtime.ObjCRuntime.runtime_from_isa(valobj)
    if runtime is None or not runtime.is_valid():
        return "<error: unknown Class>"
    class_data = runtime.read_class_data()
    if class_data is None or not class_data.is_valid():
        return "<error: unknown Class>"
    return class_data.class_name()
