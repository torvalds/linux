# This implements the "diagnose-nsstring" command, usually installed in the debug session like
#   command script import lldb.diagnose
# it is used when NSString summary formatter fails to replicate the logic that went into LLDB making the
# decisions it did and  providing some useful context information that can
# be used for improving the formatter

import lldb


def read_memory(process, location, size):
    data = ""
    error = lldb.SBError()
    for x in range(0, size - 1):
        byte = process.ReadUnsignedFromMemory(x + location, 1, error)
        if error.fail:
            data = data + "err%s" % "" if x == size - 2 else ":"
        else:
            try:
                data = data + "0x%x" % byte
                if byte == 0:
                    data = data + "(\\0)"
                elif byte == 0xA:
                    data = data + "(\\a)"
                elif byte == 0xB:
                    data = data + "(\\b)"
                elif byte == 0xC:
                    data = data + "(\\c)"
                elif byte == "\n":
                    data = data + "(\\n)"
                else:
                    data = data + "(%s)" % chr(byte)
                if x < size - 2:
                    data = data + ":"
            except Exception as e:
                print(e)
    return data


def diagnose_nsstring_Command_Impl(debugger, command, result, internal_dict):
    """
    A command to diagnose the LLDB NSString data formatter
    invoke as
    (lldb) diagnose-nsstring <expr returning NSString>
    e.g.
    (lldb) diagnose-nsstring @"Hello world"
    """
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    frame = thread.GetSelectedFrame()
    if not target.IsValid() or not process.IsValid():
        return "unable to get target/process - cannot proceed"
    options = lldb.SBExpressionOptions()
    options.SetFetchDynamicValue()
    error = lldb.SBError()
    if frame.IsValid():
        nsstring = frame.EvaluateExpression(command, options)
    else:
        nsstring = target.EvaluateExpression(command, options)
    print(str(nsstring), file=result)
    nsstring_address = nsstring.GetValueAsUnsigned(0)
    if nsstring_address == 0:
        return "unable to obtain the string - cannot proceed"
    expression = "\
struct $__lldb__notInlineMutable {\
    char* buffer;\
    signed long length;\
    signed long capacity;\
    unsigned int hasGap:1;\
    unsigned int isFixedCapacity:1;\
    unsigned int isExternalMutable:1;\
    unsigned int capacityProvidedExternally:1;\n\
#if __LP64__\n\
    unsigned long desiredCapacity:60;\n\
#else\n\
    unsigned long desiredCapacity:28;\n\
#endif\n\
    void* contentsAllocator;\
};\
\
struct $__lldb__CFString {\
    void* _cfisa;\
    uint8_t _cfinfo[4];\
    uint32_t _rc;\
    union {\
        struct __inline1 {\
            signed long length;\
        } inline1;\
        struct __notInlineImmutable1 {\
            char* buffer;\
            signed long length;\
            void* contentsDeallocator;\
        } notInlineImmutable1;\
        struct __notInlineImmutable2 {\
            char* buffer;\
            void* contentsDeallocator;\
        } notInlineImmutable2;\
        struct $__lldb__notInlineMutable notInlineMutable;\
    } variants;\
};\
"

    expression = expression + "*(($__lldb__CFString*) %d)" % nsstring_address
    # print expression
    dumped = target.EvaluateExpression(expression, options)
    print(str(dumped), file=result)

    little_endian = target.byte_order == lldb.eByteOrderLittle
    ptr_size = target.addr_size

    info_bits = (
        dumped.GetChildMemberWithName("_cfinfo")
        .GetChildAtIndex(0 if little_endian else 3)
        .GetValueAsUnsigned(0)
    )
    is_mutable = (info_bits & 1) == 1
    is_inline = (info_bits & 0x60) == 0
    has_explicit_length = (info_bits & (1 | 4)) != 4
    is_unicode = (info_bits & 0x10) == 0x10
    is_special = (
        nsstring.GetDynamicValue(lldb.eDynamicCanRunTarget).GetTypeName()
        == "NSPathStore2"
    )
    has_null = (info_bits & 8) == 8

    print(
        "\nInfo=%d\nMutable=%s\nInline=%s\nExplicit=%s\nUnicode=%s\nSpecial=%s\nNull=%s\n"
        % (
            info_bits,
            "yes" if is_mutable else "no",
            "yes" if is_inline else "no",
            "yes" if has_explicit_length else "no",
            "yes" if is_unicode else "no",
            "yes" if is_special else "no",
            "yes" if has_null else "no",
        ),
        file=result,
    )

    explicit_length_offset = 0
    if not has_null and has_explicit_length and not is_special:
        explicit_length_offset = 2 * ptr_size
        if is_mutable and not is_inline:
            explicit_length_offset = explicit_length_offset + ptr_size
        elif is_inline:
            pass
        elif not is_inline and not is_mutable:
            explicit_length_offset = explicit_length_offset + ptr_size
        else:
            explicit_length_offset = 0

    if explicit_length_offset == 0:
        print("There is no explicit length marker - skipping this step\n", file=result)
    else:
        explicit_length_offset = nsstring_address + explicit_length_offset
        explicit_length = process.ReadUnsignedFromMemory(
            explicit_length_offset, 4, error
        )
        print(
            "Explicit length location is at 0x%x - read value is %d\n"
            % (explicit_length_offset, explicit_length),
            file=result,
        )

    if is_mutable:
        location = 2 * ptr_size + nsstring_address
        location = process.ReadPointerFromMemory(location, error)
    elif (
        is_inline
        and has_explicit_length
        and not is_unicode
        and not is_special
        and not is_mutable
    ):
        location = 3 * ptr_size + nsstring_address
    elif is_unicode:
        location = 2 * ptr_size + nsstring_address
        if is_inline:
            if not has_explicit_length:
                print(
                    "Unicode & Inline & !Explicit is a new combo - no formula for it",
                    file=result,
                )
            else:
                location += ptr_size
        else:
            location = process.ReadPointerFromMemory(location, error)
    elif is_special:
        location = nsstring_address + ptr_size + 4
    elif is_inline:
        location = 2 * ptr_size + nsstring_address
        if not has_explicit_length:
            location += 1
    else:
        location = 2 * ptr_size + nsstring_address
        location = process.ReadPointerFromMemory(location, error)
    print("Expected data location: 0x%x\n" % (location), file=result)
    print(
        "1K of data around location: %s\n" % read_memory(process, location, 1024),
        file=result,
    )
    print(
        "5K of data around string pointer: %s\n"
        % read_memory(process, nsstring_address, 1024 * 5),
        file=result,
    )


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand(
        "command script add -o -f %s.diagnose_nsstring_Command_Impl diagnose-nsstring"
        % __name__
    )
    print(
        'The "diagnose-nsstring" command has been installed, type "help diagnose-nsstring" for detailed help.'
    )


__lldb_init_module(lldb.debugger, None)
__lldb_init_module = None
