"""
LLDB AppKit formatters

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""
import lldb


def SEL_Summary(valobj, dict):
    return valobj.Cast(
        valobj.GetType().GetBasicType(lldb.eBasicTypeChar).GetPointerType()
    ).GetSummary()


def SELPointer_Summary(valobj, dict):
    return (
        valobj.CreateValueFromAddress(
            "text",
            valobj.GetValueAsUnsigned(0),
            valobj.GetType().GetBasicType(lldb.eBasicTypeChar),
        )
        .AddressOf()
        .GetSummary()
    )
