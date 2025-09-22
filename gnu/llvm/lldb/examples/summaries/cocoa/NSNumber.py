"""
LLDB AppKit formatters

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""
# example summary provider for NSNumber
# the real summary is now C++ code built into LLDB

import lldb
import ctypes
import lldb.runtime.objc.objc_runtime
import lldb.formatters.metrics
import struct
import lldb.formatters.Logger

statistics = lldb.formatters.metrics.Metrics()
statistics.add_metric("invalid_isa")
statistics.add_metric("invalid_pointer")
statistics.add_metric("unknown_class")
statistics.add_metric("code_notrun")

# despite the similary to synthetic children providers, these classes are not
# trying to provide anything but the port number of an NSNumber, so they need not
# obey the interface specification for synthetic children providers


class NSTaggedNumber_SummaryProvider:
    def adjust_for_architecture(self):
        pass

    def __init__(self, valobj, info_bits, data, params):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.sys_params = params
        self.info_bits = info_bits
        self.data = data
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    def value(self):
        logger = lldb.formatters.Logger.Logger()
        # in spite of the plenty of types made available by the public NSNumber API
        # only a bunch of these are actually used in the internal implementation
        # unfortunately, the original type information appears to be lost
        # so we try to at least recover the proper magnitude of the data
        if self.info_bits == 0:
            return "(char)" + str(ord(ctypes.c_char(chr(self.data % 256)).value))
        if self.info_bits == 4:
            return "(short)" + str(ctypes.c_short(self.data % (256 * 256)).value)
        if self.info_bits == 8:
            return "(int)" + str(
                ctypes.c_int(self.data % (256 * 256 * 256 * 256)).value
            )
        if self.info_bits == 12:
            return "(long)" + str(ctypes.c_long(self.data).value)
        else:
            return (
                "unexpected value:(info="
                + str(self.info_bits)
                + ", value = "
                + str(self.data)
                + ")"
            )


class NSUntaggedNumber_SummaryProvider:
    def adjust_for_architecture(self):
        pass

    def __init__(self, valobj, params):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.sys_params = params
        if not (self.sys_params.types_cache.char):
            self.sys_params.types_cache.char = self.valobj.GetType().GetBasicType(
                lldb.eBasicTypeChar
            )
        if not (self.sys_params.types_cache.short):
            self.sys_params.types_cache.short = self.valobj.GetType().GetBasicType(
                lldb.eBasicTypeShort
            )
        if not (self.sys_params.types_cache.ushort):
            self.sys_params.types_cache.ushort = self.valobj.GetType().GetBasicType(
                lldb.eBasicTypeUnsignedShort
            )
        if not (self.sys_params.types_cache.int):
            self.sys_params.types_cache.int = self.valobj.GetType().GetBasicType(
                lldb.eBasicTypeInt
            )
        if not (self.sys_params.types_cache.long):
            self.sys_params.types_cache.long = self.valobj.GetType().GetBasicType(
                lldb.eBasicTypeLong
            )
        if not (self.sys_params.types_cache.ulong):
            self.sys_params.types_cache.ulong = self.valobj.GetType().GetBasicType(
                lldb.eBasicTypeUnsignedLong
            )
        if not (self.sys_params.types_cache.longlong):
            self.sys_params.types_cache.longlong = self.valobj.GetType().GetBasicType(
                lldb.eBasicTypeLongLong
            )
        if not (self.sys_params.types_cache.ulonglong):
            self.sys_params.types_cache.ulonglong = self.valobj.GetType().GetBasicType(
                lldb.eBasicTypeUnsignedLongLong
            )
        if not (self.sys_params.types_cache.float):
            self.sys_params.types_cache.float = self.valobj.GetType().GetBasicType(
                lldb.eBasicTypeFloat
            )
        if not (self.sys_params.types_cache.double):
            self.sys_params.types_cache.double = self.valobj.GetType().GetBasicType(
                lldb.eBasicTypeDouble
            )
        self.update()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()

    def value(self):
        logger = lldb.formatters.Logger.Logger()
        global statistics
        # we need to skip the ISA, then the next byte tells us what to read
        # we then skip one other full pointer worth of data and then fetch the contents
        # if we are fetching an int64 value, one more pointer must be skipped
        # to get at our data
        data_type_vo = self.valobj.CreateChildAtOffset(
            "dt", self.sys_params.pointer_size, self.sys_params.types_cache.char
        )
        data_type = (data_type_vo.GetValueAsUnsigned(0) % 256) & 0x1F
        data_offset = 2 * self.sys_params.pointer_size
        if data_type == 0b00001:
            data_vo = self.valobj.CreateChildAtOffset(
                "data", data_offset, self.sys_params.types_cache.char
            )
            statistics.metric_hit("code_notrun", self.valobj)
            return "(char)" + str(
                ord(ctypes.c_char(chr(data_vo.GetValueAsUnsigned(0))).value)
            )
        elif data_type == 0b0010:
            data_vo = self.valobj.CreateChildAtOffset(
                "data", data_offset, self.sys_params.types_cache.short
            )
            statistics.metric_hit("code_notrun", self.valobj)
            return "(short)" + str(
                ctypes.c_short(data_vo.GetValueAsUnsigned(0) % (256 * 256)).value
            )
        # IF tagged pointers are possible on 32bit+v2 runtime
        # (of which the only existing instance should be iOS)
        # then values of this type might be tagged
        elif data_type == 0b0011:
            data_vo = self.valobj.CreateChildAtOffset(
                "data", data_offset, self.sys_params.types_cache.int
            )
            statistics.metric_hit("code_notrun", self.valobj)
            return "(int)" + str(
                ctypes.c_int(
                    data_vo.GetValueAsUnsigned(0) % (256 * 256 * 256 * 256)
                ).value
            )
        # apparently, on is_64_bit architectures, these are the only values that will ever
        # be represented by a non tagged pointers
        elif data_type == 0b10001:
            data_offset = data_offset + 8  # 8 is needed even if we are on 32bit
            data_vo = self.valobj.CreateChildAtOffset(
                "data", data_offset, self.sys_params.types_cache.longlong
            )
            statistics.metric_hit("code_notrun", self.valobj)
            return "(long)" + str(ctypes.c_long(data_vo.GetValueAsUnsigned(0)).value)
        elif data_type == 0b0100:
            if self.sys_params.is_64_bit:
                data_offset = data_offset + self.sys_params.pointer_size
            data_vo = self.valobj.CreateChildAtOffset(
                "data", data_offset, self.sys_params.types_cache.longlong
            )
            statistics.metric_hit("code_notrun", self.valobj)
            return "(long)" + str(ctypes.c_long(data_vo.GetValueAsUnsigned(0)).value)
        elif data_type == 0b0101:
            data_vo = self.valobj.CreateChildAtOffset(
                "data", data_offset, self.sys_params.types_cache.longlong
            )
            data_plain = int(str(data_vo.GetValueAsUnsigned(0) & 0x00000000FFFFFFFF))
            packed = struct.pack("I", data_plain)
            data_float = struct.unpack("f", packed)[0]
            statistics.metric_hit("code_notrun", self.valobj)
            return "(float)" + str(data_float)
        elif data_type == 0b0110:
            data_vo = self.valobj.CreateChildAtOffset(
                "data", data_offset, self.sys_params.types_cache.longlong
            )
            data_plain = data_vo.GetValueAsUnsigned(0)
            data_double = struct.unpack("d", struct.pack("Q", data_plain))[0]
            statistics.metric_hit("code_notrun", self.valobj)
            return "(double)" + str(data_double)
        statistics.metric_hit(
            "unknown_class",
            str(valobj.GetName()) + " had unknown data_type " + str(data_type),
        )
        return "unexpected: dt = " + str(data_type)


class NSUnknownNumber_SummaryProvider:
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

    def value(self):
        logger = lldb.formatters.Logger.Logger()
        stream = lldb.SBStream()
        self.valobj.GetExpressionPath(stream)
        expr = "(NSString*)[" + stream.GetData() + " stringValue]"
        num_children_vo = self.valobj.CreateValueFromExpression("str", expr)
        if num_children_vo.IsValid():
            return num_children_vo.GetSummary()
        return "<variable is not NSNumber>"


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

    if name_string == "NSNumber" or name_string == "__NSCFNumber":
        if class_data.is_tagged():
            wrapper = NSTaggedNumber_SummaryProvider(
                valobj,
                class_data.info_bits(),
                class_data.value(),
                class_data.sys_params,
            )
            statistics.metric_hit("code_notrun", valobj)
        else:
            # the wrapper might be unable to decipher what is into the NSNumber
            # and then have to run code on it
            wrapper = NSUntaggedNumber_SummaryProvider(valobj, class_data.sys_params)
    else:
        wrapper = NSUnknownNumber_SummaryProvider(valobj, class_data.sys_params)
        statistics.metric_hit(
            "unknown_class", valobj.GetName() + " seen as " + name_string
        )
    return wrapper


def NSNumber_SummaryProvider(valobj, dict):
    logger = lldb.formatters.Logger.Logger()
    provider = GetSummary_Impl(valobj)
    if provider is not None:
        if isinstance(
            provider, lldb.runtime.objc.objc_runtime.SpecialSituation_Description
        ):
            return provider.message()
        try:
            summary = provider.value()
        except Exception as foo:
            print(foo)
            # 		except:
            summary = None
        logger >> "got summary " + str(summary)
        if summary is None:
            summary = "<variable is not NSNumber>"
        return str(summary)
    return "Summary Unavailable"


def __lldb_init_module(debugger, dict):
    debugger.HandleCommand(
        "type summary add -F NSNumber.NSNumber_SummaryProvider NSNumber"
    )
    debugger.HandleCommand(
        "type summary add -F NSNumber.NSNumber_SummaryProvider __NSCFBoolean"
    )
    debugger.HandleCommand(
        "type summary add -F NSNumber.NSNumber_SummaryProvider __NSCFNumber"
    )
