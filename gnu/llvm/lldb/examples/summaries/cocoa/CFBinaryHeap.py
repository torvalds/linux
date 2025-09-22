"""
LLDB AppKit formatters

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""
# example summary provider for CFBinaryHeap
# the real summary is now C++ code built into LLDB
import lldb
import ctypes
import lldb.runtime.objc.objc_runtime
import lldb.formatters.metrics
import lldb.formatters.Logger

statistics = lldb.formatters.metrics.Metrics()
statistics.add_metric("invalid_isa")
statistics.add_metric("invalid_pointer")
statistics.add_metric("unknown_class")
statistics.add_metric("code_notrun")

# despite the similary to synthetic children providers, these classes are not
# trying to provide anything but the length for an CFBinaryHeap, so they need not
# obey the interface specification for synthetic children providers


class CFBinaryHeapRef_SummaryProvider:
    def adjust_for_architecture(self):
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
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    # 8 bytes on i386
    # 16 bytes on x64
    # most probably 2 pointers
    def offset(self):
        logger = lldb.formatters.Logger.Logger()
        return 2 * self.sys_params.pointer_size

    def length(self):
        logger = lldb.formatters.Logger.Logger()
        size = self.valobj.CreateChildAtOffset(
            "count", self.offset(), self.sys_params.types_cache.NSUInteger
        )
        return size.GetValueAsUnsigned(0)


class CFBinaryHeapUnknown_SummaryProvider:
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

    def length(self):
        logger = lldb.formatters.Logger.Logger()
        stream = lldb.SBStream()
        self.valobj.GetExpressionPath(stream)
        num_children_vo = self.valobj.CreateValueFromExpression(
            "count", "(int)CFBinaryHeapGetCount(" + stream.GetData() + " )"
        )
        if num_children_vo.IsValid():
            return num_children_vo.GetValueAsUnsigned(0)
        return "<variable is not CFBinaryHeap>"


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
    actual_name = class_data.class_name()

    logger >> "name string got was " + str(name_string) + " but actual name is " + str(
        actual_name
    )

    if class_data.is_cftype():
        # CFBinaryHeap does not expose an actual NSWrapper type, so we have to check that this is
        # an NSCFType and then check we are a pointer-to CFBinaryHeap
        valobj_type = valobj.GetType()
        if valobj_type.IsValid() and valobj_type.IsPointerType():
            valobj_type = valobj_type.GetPointeeType()
            if valobj_type.IsValid():
                actual_name = valobj_type.GetName()
        if actual_name == "__CFBinaryHeap":
            wrapper = CFBinaryHeapRef_SummaryProvider(valobj, class_data.sys_params)
            statistics.metric_hit("code_notrun", valobj)
            return wrapper
    wrapper = CFBinaryHeapUnknown_SummaryProvider(valobj, class_data.sys_params)
    statistics.metric_hit("unknown_class", valobj.GetName() + " seen as " + name_string)
    return wrapper


def CFBinaryHeap_SummaryProvider(valobj, dict):
    logger = lldb.formatters.Logger.Logger()
    provider = GetSummary_Impl(valobj)
    if provider is not None:
        if isinstance(
            provider, lldb.runtime.objc.objc_runtime.SpecialSituation_Description
        ):
            return provider.message()
        try:
            summary = provider.length()
        except:
            summary = None
        logger >> "summary got from provider: " + str(summary)
        # for some reason, one needs to clear some bits for the count
        # to be correct when using CF(Mutable)BagRef on x64
        # the bit mask was derived through experimentation
        # (if counts start looking weird, then most probably
        #  the mask needs to be changed)
        if summary is None:
            summary = "<variable is not CFBinaryHeap>"
        elif isinstance(summary, str):
            pass
        else:
            if provider.sys_params.is_64_bit:
                summary = summary & ~0x1FFF000000000000
            if summary == 1:
                return '@"1 item"'
            else:
                summary = '@"' + str(summary) + ' items"'
        return summary
    return "Summary Unavailable"


def __lldb_init_module(debugger, dict):
    debugger.HandleCommand(
        "type summary add -F CFBinaryHeap.CFBinaryHeap_SummaryProvider CFBinaryHeapRef"
    )
