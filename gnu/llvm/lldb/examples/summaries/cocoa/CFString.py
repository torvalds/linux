"""
LLDB AppKit formatters

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
See https://llvm.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""
# example synthetic children and summary provider for CFString (and related NSString class)
# the real code is part of the LLDB core
import lldb
import lldb.runtime.objc.objc_runtime
import lldb.formatters.Logger

try:
    unichr
except NameError:
    unichr = chr


def CFString_SummaryProvider(valobj, dict):
    logger = lldb.formatters.Logger.Logger()
    provider = CFStringSynthProvider(valobj, dict)
    if not provider.invalid:
        try:
            summary = provider.get_child_at_index(provider.get_child_index("content"))
            if isinstance(summary, lldb.SBValue):
                summary = summary.GetSummary()
            else:
                summary = '"' + summary + '"'
        except:
            summary = None
        if summary is None:
            summary = "<variable is not NSString>"
        return "@" + summary
    return ""


def CFAttributedString_SummaryProvider(valobj, dict):
    logger = lldb.formatters.Logger.Logger()
    offset = valobj.GetTarget().GetProcess().GetAddressByteSize()
    pointee = valobj.GetValueAsUnsigned(0)
    summary = "<variable is not NSAttributedString>"
    if pointee is not None and pointee != 0:
        pointee = pointee + offset
        child_ptr = valobj.CreateValueFromAddress(
            "string_ptr", pointee, valobj.GetType()
        )
        child = child_ptr.CreateValueFromAddress(
            "string_data", child_ptr.GetValueAsUnsigned(), valobj.GetType()
        ).AddressOf()
        provider = CFStringSynthProvider(child, dict)
        if not provider.invalid:
            try:
                summary = provider.get_child_at_index(
                    provider.get_child_index("content")
                ).GetSummary()
            except:
                summary = "<variable is not NSAttributedString>"
    if summary is None:
        summary = "<variable is not NSAttributedString>"
    return "@" + summary


def __lldb_init_module(debugger, dict):
    debugger.HandleCommand(
        "type summary add -F CFString.CFString_SummaryProvider NSString CFStringRef CFMutableStringRef"
    )
    debugger.HandleCommand(
        "type summary add -F CFString.CFAttributedString_SummaryProvider NSAttributedString"
    )


class CFStringSynthProvider:
    def __init__(self, valobj, dict):
        logger = lldb.formatters.Logger.Logger()
        self.valobj = valobj
        self.update()

    # children other than "content" are for debugging only and must not be
    # used in production code
    def num_children(self):
        logger = lldb.formatters.Logger.Logger()
        if self.invalid:
            return 0
        return 6

    def read_unicode(self, pointer, max_len=2048):
        logger = lldb.formatters.Logger.Logger()
        process = self.valobj.GetTarget().GetProcess()
        error = lldb.SBError()
        pystr = ""
        # cannot do the read at once because the length value has
        # a weird encoding. better play it safe here
        while max_len > 0:
            content = process.ReadMemory(pointer, 2, error)
            new_bytes = bytearray(content)
            b0 = new_bytes[0]
            b1 = new_bytes[1]
            pointer = pointer + 2
            if b0 == 0 and b1 == 0:
                break
            # rearrange bytes depending on endianness
            # (do we really need this or is Cocoa going to
            #  use Windows-compatible little-endian even
            #  if the target is big endian?)
            if self.is_little:
                value = b1 * 256 + b0
            else:
                value = b0 * 256 + b1
            pystr = pystr + unichr(value)
            # read max_len unicode values, not max_len bytes
            max_len = max_len - 1
        return pystr

    # handle the special case strings
    # only use the custom code for the tested LP64 case
    def handle_special(self):
        logger = lldb.formatters.Logger.Logger()
        if not self.is_64_bit:
            # for 32bit targets, use safe ObjC code
            return self.handle_unicode_string_safe()
        offset = 12
        pointer = self.valobj.GetValueAsUnsigned(0) + offset
        pystr = self.read_unicode(pointer)
        return self.valobj.CreateValueFromExpression(
            "content", '(char*)"' + pystr.encode("utf-8") + '"'
        )

    # last resort call, use ObjC code to read; the final aim is to
    # be able to strip this call away entirely and only do the read
    # ourselves
    def handle_unicode_string_safe(self):
        return self.valobj.CreateValueFromExpression(
            "content", '(char*)"' + self.valobj.GetObjectDescription() + '"'
        )

    def handle_unicode_string(self):
        logger = lldb.formatters.Logger.Logger()
        # step 1: find offset
        if self.inline:
            pointer = self.valobj.GetValueAsUnsigned(0) + self.size_of_cfruntime_base()
            if not self.explicit:
                # untested, use the safe code path
                return self.handle_unicode_string_safe()
            else:
                # a full pointer is skipped here before getting to the live
                # data
                pointer = pointer + self.pointer_size
        else:
            pointer = self.valobj.GetValueAsUnsigned(0) + self.size_of_cfruntime_base()
            # read 8 bytes here and make an address out of them
            try:
                char_type = (
                    self.valobj.GetType()
                    .GetBasicType(lldb.eBasicTypeChar)
                    .GetPointerType()
                )
                vopointer = self.valobj.CreateValueFromAddress(
                    "dummy", pointer, char_type
                )
                pointer = vopointer.GetValueAsUnsigned(0)
            except:
                return self.valobj.CreateValueFromExpression(
                    "content", '(char*)"@"invalid NSString""'
                )
        # step 2: read Unicode data at pointer
        pystr = self.read_unicode(pointer)
        # step 3: return it
        return pystr.encode("utf-8")

    def handle_inline_explicit(self):
        logger = lldb.formatters.Logger.Logger()
        offset = 3 * self.pointer_size
        offset = offset + self.valobj.GetValueAsUnsigned(0)
        return self.valobj.CreateValueFromExpression(
            "content", "(char*)(" + str(offset) + ")"
        )

    def handle_mutable_string(self):
        logger = lldb.formatters.Logger.Logger()
        offset = 2 * self.pointer_size
        data = self.valobj.CreateChildAtOffset(
            "content",
            offset,
            self.valobj.GetType().GetBasicType(lldb.eBasicTypeChar).GetPointerType(),
        )
        data_value = data.GetValueAsUnsigned(0)
        if self.explicit and self.unicode:
            return self.read_unicode(data_value).encode("utf-8")
        else:
            data_value = data_value + 1
            return self.valobj.CreateValueFromExpression(
                "content", "(char*)(" + str(data_value) + ")"
            )

    def handle_UTF8_inline(self):
        logger = lldb.formatters.Logger.Logger()
        offset = self.valobj.GetValueAsUnsigned(0) + self.size_of_cfruntime_base()
        if not self.explicit:
            offset = offset + 1
        return self.valobj.CreateValueFromAddress(
            "content", offset, self.valobj.GetType().GetBasicType(lldb.eBasicTypeChar)
        ).AddressOf()

    def handle_UTF8_not_inline(self):
        logger = lldb.formatters.Logger.Logger()
        offset = self.size_of_cfruntime_base()
        return self.valobj.CreateChildAtOffset(
            "content",
            offset,
            self.valobj.GetType().GetBasicType(lldb.eBasicTypeChar).GetPointerType(),
        )

    def get_child_at_index(self, index):
        logger = lldb.formatters.Logger.Logger()
        logger >> "Querying for child [" + str(index) + "]"
        if index == 0:
            return self.valobj.CreateValueFromExpression(
                "mutable", str(int(self.mutable))
            )
        if index == 1:
            return self.valobj.CreateValueFromExpression(
                "inline", str(int(self.inline))
            )
        if index == 2:
            return self.valobj.CreateValueFromExpression(
                "explicit", str(int(self.explicit))
            )
        if index == 3:
            return self.valobj.CreateValueFromExpression(
                "unicode", str(int(self.unicode))
            )
        if index == 4:
            return self.valobj.CreateValueFromExpression(
                "special", str(int(self.special))
            )
        if index == 5:
            # we are handling the several possible combinations of flags.
            # for each known combination we have a function that knows how to
            # go fetch the data from memory instead of running code. if a string is not
            # correctly displayed, one should start by finding a combination of flags that
            # makes it different from these known cases, and provide a new reader function
            # if this is not possible, a new flag might have to be made up (like the "special" flag
            # below, which is not a real flag in CFString), or alternatively one might need to use
            # the ObjC runtime helper to detect the new class and deal with it accordingly
            # print 'mutable = ' + str(self.mutable)
            # print 'inline = ' + str(self.inline)
            # print 'explicit = ' + str(self.explicit)
            # print 'unicode = ' + str(self.unicode)
            # print 'special = ' + str(self.special)
            if self.mutable:
                return self.handle_mutable_string()
            elif (
                self.inline
                and self.explicit
                and not self.unicode
                and not self.special
                and not self.mutable
            ):
                return self.handle_inline_explicit()
            elif self.unicode:
                return self.handle_unicode_string()
            elif self.special:
                return self.handle_special()
            elif self.inline:
                return self.handle_UTF8_inline()
            else:
                return self.handle_UTF8_not_inline()

    def get_child_index(self, name):
        logger = lldb.formatters.Logger.Logger()
        logger >> "Querying for child ['" + str(name) + "']"
        if name == "content":
            return self.num_children() - 1
        if name == "mutable":
            return 0
        if name == "inline":
            return 1
        if name == "explicit":
            return 2
        if name == "unicode":
            return 3
        if name == "special":
            return 4

    # CFRuntimeBase is defined as having an additional
    # 4 bytes (padding?) on LP64 architectures
    # to get its size we add up sizeof(pointer)+4
    # and then add 4 more bytes if we are on a 64bit system
    def size_of_cfruntime_base(self):
        logger = lldb.formatters.Logger.Logger()
        return self.pointer_size + 4 + (4 if self.is_64_bit else 0)

    # the info bits are part of the CFRuntimeBase structure
    # to get at them we have to skip a uintptr_t and then get
    # at the least-significant byte of a 4 byte array. If we are
    # on big-endian this means going to byte 3, if we are on
    # little endian (OSX & iOS), this means reading byte 0
    def offset_of_info_bits(self):
        logger = lldb.formatters.Logger.Logger()
        offset = self.pointer_size
        if not self.is_little:
            offset = offset + 3
        return offset

    def read_info_bits(self):
        logger = lldb.formatters.Logger.Logger()
        cfinfo = self.valobj.CreateChildAtOffset(
            "cfinfo",
            self.offset_of_info_bits(),
            self.valobj.GetType().GetBasicType(lldb.eBasicTypeChar),
        )
        cfinfo.SetFormat(11)
        info = cfinfo.GetValue()
        if info is not None:
            self.invalid = False
            return int(info, 0)
        else:
            self.invalid = True
            return None

    # calculating internal flag bits of the CFString object
    # this stuff is defined and discussed in CFString.c
    def is_mutable(self):
        logger = lldb.formatters.Logger.Logger()
        return (self.info_bits & 1) == 1

    def is_inline(self):
        logger = lldb.formatters.Logger.Logger()
        return (self.info_bits & 0x60) == 0

    # this flag's name is ambiguous, it turns out
    # we must skip a length byte to get at the data
    # when this flag is False
    def has_explicit_length(self):
        logger = lldb.formatters.Logger.Logger()
        return (self.info_bits & (1 | 4)) != 4

    # probably a subclass of NSString. obtained this from [str pathExtension]
    # here info_bits = 0 and Unicode data at the start of the padding word
    # in the long run using the isa value might be safer as a way to identify this
    # instead of reading the info_bits
    def is_special_case(self):
        logger = lldb.formatters.Logger.Logger()
        return self.info_bits == 0

    def is_unicode(self):
        logger = lldb.formatters.Logger.Logger()
        return (self.info_bits & 0x10) == 0x10

    # preparing ourselves to read into memory
    # by adjusting architecture-specific info
    def adjust_for_architecture(self):
        logger = lldb.formatters.Logger.Logger()
        self.pointer_size = self.valobj.GetTarget().GetProcess().GetAddressByteSize()
        self.is_64_bit = self.pointer_size == 8
        self.is_little = (
            self.valobj.GetTarget().GetProcess().GetByteOrder() == lldb.eByteOrderLittle
        )

    # reading info bits out of the CFString and computing
    # useful values to get at the real data
    def compute_flags(self):
        logger = lldb.formatters.Logger.Logger()
        self.info_bits = self.read_info_bits()
        if self.info_bits is None:
            return
        self.mutable = self.is_mutable()
        self.inline = self.is_inline()
        self.explicit = self.has_explicit_length()
        self.unicode = self.is_unicode()
        self.special = self.is_special_case()

    def update(self):
        logger = lldb.formatters.Logger.Logger()
        self.adjust_for_architecture()
        self.compute_flags()
