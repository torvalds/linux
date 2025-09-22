"""
LLDB AppKit formatters

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""
# example summary provider for NSDictionary
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
# trying to provide anything but the count for an NSDictionary, so they need not
# obey the interface specification for synthetic children providers


class NSCFDictionary_SummaryProvider:
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

    # empirically determined on both 32 and 64bit desktop Mac OS X
    # probably boils down to 2 pointers and 4 bytes of data, but
    # the description of __CFDictionary is not readily available so most
    # of this is guesswork, plain and simple
    def offset(self):
        logger = lldb.formatters.Logger.Logger()
        if self.sys_params.is_64_bit:
            return 20
        else:
            return 12

    def num_children(self):
        logger = lldb.formatters.Logger.Logger()
        num_children_vo = self.valobj.CreateChildAtOffset(
            "count", self.offset(), self.sys_params.types_cache.NSUInteger
        )
        return num_children_vo.GetValueAsUnsigned(0)


class NSDictionaryI_SummaryProvider:
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

    # we just need to skip the ISA and the count immediately follows
    def offset(self):
        logger = lldb.formatters.Logger.Logger()
        return self.sys_params.pointer_size

    def num_children(self):
        logger = lldb.formatters.Logger.Logger()
        num_children_vo = self.valobj.CreateChildAtOffset(
            "count", self.offset(), self.sys_params.types_cache.NSUInteger
        )
        value = num_children_vo.GetValueAsUnsigned(0)
        if value is not None:
            # the MS6bits on immutable dictionaries seem to be taken by the LSB of capacity
            # not sure if it is a bug or some weird sort of feature, but masking that out
            # gets the count right
            if self.sys_params.is_64_bit:
                value = value & ~0xFC00000000000000
            else:
                value = value & ~0xFC000000
        return value


class NSDictionaryM_SummaryProvider:
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

    # we just need to skip the ISA and the count immediately follows
    def offset(self):
        return self.sys_params.pointer_size

    def num_children(self):
        logger = lldb.formatters.Logger.Logger()
        num_children_vo = self.valobj.CreateChildAtOffset(
            "count", self.offset(), self.sys_params.types_cache.NSUInteger
        )
        value = num_children_vo.GetValueAsUnsigned(0)
        if value is not None:
            # the MS6bits on immutable dictionaries seem to be taken by the LSB of capacity
            # not sure if it is a bug or some weird sort of feature, but masking that out
            # gets the count right
            if self.sys_params.is_64_bit:
                value = value & ~0xFC00000000000000
            else:
                value = value & ~0xFC000000
        return value


class NSDictionaryUnknown_SummaryProvider:
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

    def num_children(self):
        logger = lldb.formatters.Logger.Logger()
        stream = lldb.SBStream()
        self.valobj.GetExpressionPath(stream)
        num_children_vo = self.valobj.CreateValueFromExpression(
            "count", "(int)[" + stream.GetData() + " count]"
        )
        if num_children_vo.IsValid():
            return num_children_vo.GetValueAsUnsigned(0)
        return "<variable is not NSDictionary>"


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

    logger >> "class name is: " + str(name_string)

    if name_string == "__NSCFDictionary":
        wrapper = NSCFDictionary_SummaryProvider(valobj, class_data.sys_params)
        statistics.metric_hit("code_notrun", valobj)
    elif name_string == "__NSDictionaryI":
        wrapper = NSDictionaryI_SummaryProvider(valobj, class_data.sys_params)
        statistics.metric_hit("code_notrun", valobj)
    elif name_string == "__NSDictionaryM":
        wrapper = NSDictionaryM_SummaryProvider(valobj, class_data.sys_params)
        statistics.metric_hit("code_notrun", valobj)
    else:
        wrapper = NSDictionaryUnknown_SummaryProvider(valobj, class_data.sys_params)
        statistics.metric_hit(
            "unknown_class", valobj.GetName() + " seen as " + name_string
        )
    return wrapper


def CFDictionary_SummaryProvider(valobj, dict):
    logger = lldb.formatters.Logger.Logger()
    provider = GetSummary_Impl(valobj)
    if provider is not None:
        if isinstance(
            provider, lldb.runtime.objc.objc_runtime.SpecialSituation_Description
        ):
            return provider.message()
        try:
            summary = provider.num_children()
        except:
            summary = None
        logger >> "got summary " + str(summary)
        if summary is None:
            return "<variable is not NSDictionary>"
        if isinstance(summary, str):
            return summary
        return str(summary) + (
            " key/value pairs" if summary != 1 else " key/value pair"
        )
    return "Summary Unavailable"


def CFDictionary_SummaryProvider2(valobj, dict):
    logger = lldb.formatters.Logger.Logger()
    provider = GetSummary_Impl(valobj)
    if provider is not None:
        if isinstance(
            provider, lldb.runtime.objc.objc_runtime.SpecialSituation_Description
        ):
            return provider.message()
        try:
            summary = provider.num_children()
        except:
            summary = None
        logger >> "got summary " + str(summary)
        if summary is None:
            summary = "<variable is not CFDictionary>"
        if isinstance(summary, str):
            return summary
        else:
            # needed on OSX Mountain Lion
            if provider.sys_params.is_64_bit:
                summary = summary & ~0x0F1F000000000000
            summary = '@"' + str(summary) + (' entries"' if summary != 1 else ' entry"')
        return summary
    return "Summary Unavailable"


def __lldb_init_module(debugger, dict):
    debugger.HandleCommand(
        "type summary add -F CFDictionary.CFDictionary_SummaryProvider NSDictionary"
    )
    debugger.HandleCommand(
        "type summary add -F CFDictionary.CFDictionary_SummaryProvider2 CFDictionaryRef CFMutableDictionaryRef"
    )
