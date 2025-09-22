"""
LLDB AppKit formatters

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""
# summary provider for class NSException
import lldb.runtime.objc.objc_runtime
import lldb.formatters.metrics
import CFString
import lldb
import lldb.formatters.Logger

statistics = lldb.formatters.metrics.Metrics()
statistics.add_metric("invalid_isa")
statistics.add_metric("invalid_pointer")
statistics.add_metric("unknown_class")
statistics.add_metric("code_notrun")


class NSKnownException_SummaryProvider:
    def adjust_for_architecture(self):
        pass

    def __init__(self, valobj, params):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.sys_params = params
        if not (self.sys_params.types_cache.id):
            self.sys_params.types_cache.id = self.valobj.GetType().GetBasicType(
                lldb.eBasicTypeObjCID
            )
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    def offset_name(self):
        logger = lldb.formatters.Logger.Logger()
        return self.sys_params.pointer_size

    def offset_reason(self):
        logger = lldb.formatters.Logger.Logger()
        return 2 * self.sys_params.pointer_size

    def description(self):
        logger = lldb.formatters.Logger.Logger()
        name_ptr = self.valobj.CreateChildAtOffset(
            "name", self.offset_name(), self.sys_params.types_cache.id
        )
        reason_ptr = self.valobj.CreateChildAtOffset(
            "reason", self.offset_reason(), self.sys_params.types_cache.id
        )
        return (
            "name:"
            + CFString.CFString_SummaryProvider(name_ptr, None)
            + " reason:"
            + CFString.CFString_SummaryProvider(reason_ptr, None)
        )


class NSUnknownException_SummaryProvider:
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

    def description(self):
        logger = lldb.formatters.Logger.Logger()
        stream = lldb.SBStream()
        self.valobj.GetExpressionPath(stream)
        name_vo = self.valobj.CreateValueFromExpression(
            "name", "(NSString*)[" + stream.GetData() + " name]"
        )
        reason_vo = self.valobj.CreateValueFromExpression(
            "reason", "(NSString*)[" + stream.GetData() + " reason]"
        )
        if name_vo.IsValid() and reason_vo.IsValid():
            return (
                CFString.CFString_SummaryProvider(name_vo, None)
                + " "
                + CFString.CFString_SummaryProvider(reason_vo, None)
            )
        return "<variable is not NSException>"


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

    if name_string == "NSException":
        wrapper = NSKnownException_SummaryProvider(valobj, class_data.sys_params)
        statistics.metric_hit("code_notrun", valobj)
    else:
        wrapper = NSUnknownException_SummaryProvider(valobj, class_data.sys_params)
        statistics.metric_hit(
            "unknown_class", valobj.GetName() + " seen as " + name_string
        )
    return wrapper


def NSException_SummaryProvider(valobj, dict):
    logger = lldb.formatters.Logger.Logger()
    provider = GetSummary_Impl(valobj)
    if provider is not None:
        if isinstance(
            provider, lldb.runtime.objc.objc_runtime.SpecialSituation_Description
        ):
            return provider.message()
        try:
            summary = provider.description()
        except:
            summary = None
        logger >> "got summary " + str(summary)
        if summary is None:
            summary = "<variable is not NSException>"
        return str(summary)
    return "Summary Unavailable"


def __lldb_init_module(debugger, dict):
    debugger.HandleCommand(
        "type summary add -F NSException.NSException_SummaryProvider NSException"
    )
