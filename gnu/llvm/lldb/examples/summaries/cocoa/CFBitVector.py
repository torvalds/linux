"""
LLDB AppKit formatters

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""

# summary provider for CF(Mutable)BitVector
import lldb
import ctypes
import lldb.runtime.objc.objc_runtime
import lldb.formatters.metrics
import lldb.formatters.Logger

# first define some utility functions


def byte_index(abs_pos):
    logger = lldb.formatters.Logger.Logger()
    return abs_pos / 8


def bit_index(abs_pos):
    logger = lldb.formatters.Logger.Logger()
    return abs_pos & 7


def get_bit(byte, index):
    logger = lldb.formatters.Logger.Logger()
    if index < 0 or index > 7:
        return None
    return (byte >> (7 - index)) & 1


def grab_array_item_data(pointer, index):
    logger = lldb.formatters.Logger.Logger()
    return pointer.GetPointeeData(index, 1)


statistics = lldb.formatters.metrics.Metrics()
statistics.add_metric("invalid_isa")
statistics.add_metric("invalid_pointer")
statistics.add_metric("unknown_class")
statistics.add_metric("code_notrun")

# despite the similary to synthetic children providers, these classes are not
# trying to provide anything but a summary for a CF*BitVector, so they need not
# obey the interface specification for synthetic children providers


class CFBitVectorKnown_SummaryProvider:
    def adjust_for_architecture(self):
        logger = lldb.formatters.Logger.Logger()
        self.uiint_size = self.sys_params.types_cache.NSUInteger.GetByteSize()
        pass

    def __init__(self, valobj, params):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.sys_params = params
        if not (self.sys_params.types_cache.NSUInteger):
            if self.sys_params.is_64_bit:
                self.sys_params.types_cache.NSUInteger = (
                    self.valobj.GetType().GetBasicType(lldb.eBasicTypeUnsignedLong)
                )
            else:
                self.sys_params.types_cache.NSUInteger = (
                    self.valobj.GetType().GetBasicType(lldb.eBasicTypeUnsignedInt)
                )
        if not (self.sys_params.types_cache.charptr):
            self.sys_params.types_cache.charptr = (
                self.valobj.GetType().GetBasicType(lldb.eBasicTypeChar).GetPointerType()
            )
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    # we skip the CFRuntimeBase
    # then the next CFIndex is the count
    # then we skip another CFIndex and then we get at a byte array
    # that wraps the individual bits

    def contents(self):
        logger = lldb.formatters.Logger.Logger()
        count_vo = self.valobj.CreateChildAtOffset(
            "count",
            self.sys_params.cfruntime_size,
            self.sys_params.types_cache.NSUInteger,
        )
        count = count_vo.GetValueAsUnsigned(0)
        if count == 0:
            return "(empty)"

        array_vo = self.valobj.CreateChildAtOffset(
            "data",
            self.sys_params.cfruntime_size + 2 * self.uiint_size,
            self.sys_params.types_cache.charptr,
        )

        data_list = []
        cur_byte_pos = None
        for i in range(0, count):
            if cur_byte_pos is None:
                cur_byte_pos = byte_index(i)
                cur_byte = grab_array_item_data(array_vo, cur_byte_pos)
                cur_byte_val = cur_byte.uint8[0]
            else:
                byte_pos = byte_index(i)
                # do not fetch the pointee data every single time through
                if byte_pos != cur_byte_pos:
                    cur_byte_pos = byte_pos
                    cur_byte = grab_array_item_data(array_vo, cur_byte_pos)
                    cur_byte_val = cur_byte.uint8[0]
            bit = get_bit(cur_byte_val, bit_index(i))
            if (i % 4) == 0:
                data_list.append(" ")
            if bit == 1:
                data_list.append("1")
            else:
                data_list.append("0")
        return "".join(data_list)


class CFBitVectorUnknown_SummaryProvider:
    def adjust_for_architecture(self):
        pass

    def __init__(self, valobj, params):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.sys_params = params
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    def contents(self):
        logger = lldb.formatters.Logger.Logger()
        return "<unable to summarize this CFBitVector>"


def GetSummary_Impl(valobj):
    logger = lldb.formatters.Logger.Logger()
    global statistics
    (
        class_data,
        wrapper,
    ) = lldb.runtime.objc.objc_runtime.Utilities.prepare_class_detection(
        valobj, statistics
    )
    if wrapper:
        return wrapper

    name_string = class_data.class_name()
    actual_name = name_string

    logger >> "name string got was " + str(name_string) + " but actual name is " + str(
        actual_name
    )

    if class_data.is_cftype():
        # CFBitVectorRef does not expose an actual NSWrapper type, so we have to check that this is
        # an NSCFType and then check we are a pointer-to CFBitVectorRef
        valobj_type = valobj.GetType()
        if valobj_type.IsValid() and valobj_type.IsPointerType():
            valobj_type = valobj_type.GetPointeeType()
            if valobj_type.IsValid():
                actual_name = valobj_type.GetName()
        if actual_name == "__CFBitVector" or actual_name == "__CFMutableBitVector":
            wrapper = CFBitVectorKnown_SummaryProvider(valobj, class_data.sys_params)
            statistics.metric_hit("code_notrun", valobj)
        else:
            wrapper = CFBitVectorUnknown_SummaryProvider(valobj, class_data.sys_params)
            print(actual_name)
    else:
        wrapper = CFBitVectorUnknown_SummaryProvider(valobj, class_data.sys_params)
        print(name_string)
        statistics.metric_hit(
            "unknown_class", valobj.GetName() + " seen as " + name_string
        )
    return wrapper


def CFBitVector_SummaryProvider(valobj, dict):
    logger = lldb.formatters.Logger.Logger()
    provider = GetSummary_Impl(valobj)
    if provider is not None:
        if isinstance(
            provider, lldb.runtime.objc.objc_runtime.SpecialSituation_Description
        ):
            return provider.message()
        try:
            summary = provider.contents()
        except:
            summary = None
        logger >> "summary got from provider: " + str(summary)
        if summary is None or summary == "":
            summary = "<variable is not CFBitVector>"
        return summary
    return "Summary Unavailable"


def __lldb_init_module(debugger, dict):
    debugger.HandleCommand(
        "type summary add -F CFBitVector.CFBitVector_SummaryProvider CFBitVectorRef CFMutableBitVectorRef"
    )
