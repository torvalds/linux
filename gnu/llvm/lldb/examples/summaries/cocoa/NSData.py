"""
LLDB AppKit formatters

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""
# example summary provider for NSData
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
# trying to provide anything but the length for an NSData, so they need not
# obey the interface specification for synthetic children providers


class NSConcreteData_SummaryProvider:
    def adjust_for_architecture(self):
        pass

    def __init__(self, valobj, params):
        logger = lldb.formatters.Logger.Logger()
        logger >> "NSConcreteData_SummaryProvider __init__"
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
        self.adjust_for_architecture()

    # one pointer is the ISA
    # then there are 32 bit worth of flags and other data
    # however, on 64bit systems these are padded to be a full
    # machine word long, which means we actually have two pointers
    # worth of data to skip
    def offset(self):
        return 2 * self.sys_params.pointer_size

    def length(self):
        logger = lldb.formatters.Logger.Logger()
        logger >> "NSConcreteData_SummaryProvider length"
        size = self.valobj.CreateChildAtOffset(
            "count", self.offset(), self.sys_params.types_cache.NSUInteger
        )
        logger >> str(size)
        logger >> str(size.GetValueAsUnsigned(0))
        return size.GetValueAsUnsigned(0)


class NSDataUnknown_SummaryProvider:
    def adjust_for_architecture(self):
        pass

    def __init__(self, valobj, params):
        logger = lldb.formatters.Logger.Logger()
        logger >> "NSDataUnknown_SummaryProvider __init__"
        self.valobj = valobj
        self.sys_params = params
        self.update()

    def update(self):
        self.adjust_for_architecture()

    def length(self):
        logger = lldb.formatters.Logger.Logger()
        logger >> "NSDataUnknown_SummaryProvider length"
        stream = lldb.SBStream()
        self.valobj.GetExpressionPath(stream)
        logger >> stream.GetData()
        num_children_vo = self.valobj.CreateValueFromExpression(
            "count", "(int)[" + stream.GetData() + " length]"
        )
        logger >> "still in after expression: " + str(num_children_vo)
        if num_children_vo.IsValid():
            logger >> "wow - expr output is valid: " + str(
                num_children_vo.GetValueAsUnsigned()
            )
            return num_children_vo.GetValueAsUnsigned(0)
        logger >> "invalid expr output - too bad"
        return "<variable is not NSData>"


def GetSummary_Impl(valobj):
    global statistics
    logger = lldb.formatters.Logger.Logger()
    logger >> "NSData GetSummary_Impl"
    (
        class_data,
        wrapper,
    ) = lldb.runtime.objc.objc_runtime.Utilities.prepare_class_detection(
        valobj, statistics
    )
    if wrapper:
        logger >> "got a wrapper summary - using it"
        return wrapper

    name_string = class_data.class_name()
    logger >> "class name: " + name_string
    if (
        name_string == "NSConcreteData"
        or name_string == "NSConcreteMutableData"
        or name_string == "__NSCFData"
    ):
        wrapper = NSConcreteData_SummaryProvider(valobj, class_data.sys_params)
        statistics.metric_hit("code_notrun", valobj)
    else:
        wrapper = NSDataUnknown_SummaryProvider(valobj, class_data.sys_params)
        statistics.metric_hit(
            "unknown_class", valobj.GetName() + " seen as " + name_string
        )
    return wrapper


def NSData_SummaryProvider(valobj, dict):
    logger = lldb.formatters.Logger.Logger()
    logger >> "NSData_SummaryProvider"
    provider = GetSummary_Impl(valobj)
    logger >> "found a summary provider, it is: " + str(provider)
    if provider is not None:
        try:
            summary = provider.length()
        except:
            summary = None
        logger >> "got a summary: it is " + str(summary)
        if summary is None:
            summary = "<variable is not NSData>"
        elif isinstance(summary, str):
            pass
        else:
            if summary == 1:
                summary = "1 byte"
            else:
                summary = str(summary) + " bytes"
        return summary
    return "Summary Unavailable"


def NSData_SummaryProvider2(valobj, dict):
    logger = lldb.formatters.Logger.Logger()
    logger >> "NSData_SummaryProvider2"
    provider = GetSummary_Impl(valobj)
    logger >> "found a summary provider, it is: " + str(provider)
    if provider is not None:
        if isinstance(
            provider, lldb.runtime.objc.objc_runtime.SpecialSituation_Description
        ):
            return provider.message()
        try:
            summary = provider.length()
        except:
            summary = None
        logger >> "got a summary: it is " + str(summary)
        if summary is None:
            summary = "<variable is not CFData>"
        elif isinstance(summary, str):
            pass
        else:
            if summary == 1:
                summary = '@"1 byte"'
            else:
                summary = '@"' + str(summary) + ' bytes"'
        return summary
    return "Summary Unavailable"


def __lldb_init_module(debugger, dict):
    debugger.HandleCommand("type summary add -F NSData.NSData_SummaryProvider NSData")
    debugger.HandleCommand(
        "type summary add -F NSData.NSData_SummaryProvider2 CFDataRef CFMutableDataRef"
    )
