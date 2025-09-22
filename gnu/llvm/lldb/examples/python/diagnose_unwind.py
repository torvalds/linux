# This implements the "diagnose-unwind" command, usually installed
# in the debug session like
#   command script import lldb.diagnose
# it is used when lldb's backtrace fails -- it collects and prints
# information about the stack frames, and tries an alternate unwind
# algorithm, that will help to understand why lldb's unwind algorithm
# did not succeed.

import optparse
import lldb
import re
import shlex

# Print the frame number, pc, frame pointer, module UUID and function name
# Returns the SBModule that contains the PC, if it could be found


def backtrace_print_frame(target, frame_num, addr, fp):
    process = target.GetProcess()
    addr_for_printing = addr
    addr_width = process.GetAddressByteSize() * 2
    if frame_num > 0:
        addr = addr - 1

    sbaddr = lldb.SBAddress()
    try:
        sbaddr.SetLoadAddress(addr, target)
        module_description = ""
        if sbaddr.GetModule():
            module_filename = ""
            module_uuid_str = sbaddr.GetModule().GetUUIDString()
            if module_uuid_str is None:
                module_uuid_str = ""
            if sbaddr.GetModule().GetFileSpec():
                module_filename = sbaddr.GetModule().GetFileSpec().GetFilename()
                if module_filename is None:
                    module_filename = ""
            if module_uuid_str != "" or module_filename != "":
                module_description = "%s %s" % (module_filename, module_uuid_str)
    except Exception:
        print(
            "%2d: pc==0x%-*x fp==0x%-*x"
            % (frame_num, addr_width, addr_for_printing, addr_width, fp)
        )
        return

    sym_ctx = target.ResolveSymbolContextForAddress(
        sbaddr, lldb.eSymbolContextEverything
    )
    if sym_ctx.IsValid() and sym_ctx.GetSymbol().IsValid():
        function_start = sym_ctx.GetSymbol().GetStartAddress().GetLoadAddress(target)
        offset = addr - function_start
        print(
            "%2d: pc==0x%-*x fp==0x%-*x %s %s + %d"
            % (
                frame_num,
                addr_width,
                addr_for_printing,
                addr_width,
                fp,
                module_description,
                sym_ctx.GetSymbol().GetName(),
                offset,
            )
        )
    else:
        print(
            "%2d: pc==0x%-*x fp==0x%-*x %s"
            % (
                frame_num,
                addr_width,
                addr_for_printing,
                addr_width,
                fp,
                module_description,
            )
        )
    return sbaddr.GetModule()


# A simple stack walk algorithm that follows the frame chain.
# Returns a two-element list; the first element is a list of modules
# seen and the second element is a list of addresses seen during the backtrace.


def simple_backtrace(debugger):
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    cur_thread = process.GetSelectedThread()

    initial_fp = cur_thread.GetFrameAtIndex(0).GetFP()

    # If the pseudoreg "fp" isn't recognized, on arm hardcode to r7 which is
    # correct for Darwin programs.
    if initial_fp == lldb.LLDB_INVALID_ADDRESS and target.triple[0:3] == "arm":
        for reggroup in cur_thread.GetFrameAtIndex(1).registers:
            if reggroup.GetName() == "General Purpose Registers":
                for reg in reggroup:
                    if reg.GetName() == "r7":
                        initial_fp = int(reg.GetValue(), 16)

    module_list = []
    address_list = [cur_thread.GetFrameAtIndex(0).GetPC()]
    this_module = backtrace_print_frame(
        target, 0, cur_thread.GetFrameAtIndex(0).GetPC(), initial_fp
    )
    print_stack_frame(process, initial_fp)
    print("")
    if this_module is not None:
        module_list.append(this_module)
    if cur_thread.GetNumFrames() < 2:
        return [module_list, address_list]

    cur_fp = process.ReadPointerFromMemory(initial_fp, lldb.SBError())
    cur_pc = process.ReadPointerFromMemory(
        initial_fp + process.GetAddressByteSize(), lldb.SBError()
    )

    frame_num = 1

    while (
        cur_pc != 0
        and cur_fp != 0
        and cur_pc != lldb.LLDB_INVALID_ADDRESS
        and cur_fp != lldb.LLDB_INVALID_ADDRESS
    ):
        address_list.append(cur_pc)
        this_module = backtrace_print_frame(target, frame_num, cur_pc, cur_fp)
        print_stack_frame(process, cur_fp)
        print("")
        if this_module is not None:
            module_list.append(this_module)
        frame_num = frame_num + 1
        next_pc = 0
        next_fp = 0
        if (
            target.triple[0:6] == "x86_64"
            or target.triple[0:4] == "i386"
            or target.triple[0:3] == "arm"
        ):
            error = lldb.SBError()
            next_pc = process.ReadPointerFromMemory(
                cur_fp + process.GetAddressByteSize(), error
            )
            if not error.Success():
                next_pc = 0
            next_fp = process.ReadPointerFromMemory(cur_fp, error)
            if not error.Success():
                next_fp = 0
        # Clear the 0th bit for arm frames - this indicates it is a thumb frame
        if target.triple[0:3] == "arm" and (next_pc & 1) == 1:
            next_pc = next_pc & ~1
        cur_pc = next_pc
        cur_fp = next_fp
    this_module = backtrace_print_frame(target, frame_num, cur_pc, cur_fp)
    print_stack_frame(process, cur_fp)
    print("")
    if this_module is not None:
        module_list.append(this_module)
    return [module_list, address_list]


def print_stack_frame(process, fp):
    if fp == 0 or fp == lldb.LLDB_INVALID_ADDRESS or fp == 1:
        return
    addr_size = process.GetAddressByteSize()
    addr = fp - (2 * addr_size)
    i = 0
    outline = "Stack frame from $fp-%d: " % (2 * addr_size)
    error = lldb.SBError()
    try:
        while i < 5 and error.Success():
            address = process.ReadPointerFromMemory(addr + (i * addr_size), error)
            outline += " 0x%x" % address
            i += 1
        print(outline)
    except Exception:
        return


def diagnose_unwind(debugger, command, result, dict):
    """
    Gather diagnostic information to help debug incorrect unwind (backtrace)
    behavior in lldb.  When there is a backtrace that doesn't look
    correct, run this command with the correct thread selected and a
    large amount of diagnostic information will be printed, it is likely
    to be helpful when reporting the problem.
    """

    command_args = shlex.split(command)
    parser = create_diagnose_unwind_options()
    try:
        (options, args) = parser.parse_args(command_args)
    except:
        return
    target = debugger.GetSelectedTarget()
    if target:
        process = target.GetProcess()
        if process:
            thread = process.GetSelectedThread()
            if thread:
                lldb_versions_match = re.search(
                    r"[lL][lL][dD][bB]-(\d+)([.](\d+))?([.](\d+))?",
                    debugger.GetVersionString(),
                )
                lldb_version = 0
                lldb_minor = 0
                if (
                    len(lldb_versions_match.groups()) >= 1
                    and lldb_versions_match.groups()[0]
                ):
                    lldb_major = int(lldb_versions_match.groups()[0])
                if (
                    len(lldb_versions_match.groups()) >= 5
                    and lldb_versions_match.groups()[4]
                ):
                    lldb_minor = int(lldb_versions_match.groups()[4])

                modules_seen = []
                addresses_seen = []

                print("LLDB version %s" % debugger.GetVersionString())
                print("Unwind diagnostics for thread %d" % thread.GetIndexID())
                print("")
                print(
                    "============================================================================================="
                )
                print("")
                print("OS plugin setting:")
                debugger.HandleCommand(
                    "settings show target.process.python-os-plugin-path"
                )
                print("")
                print("Live register context:")
                thread.SetSelectedFrame(0)
                debugger.HandleCommand("register read")
                print("")
                print(
                    "============================================================================================="
                )
                print("")
                print("lldb's unwind algorithm:")
                print("")
                frame_num = 0
                for frame in thread.frames:
                    if not frame.IsInlined():
                        this_module = backtrace_print_frame(
                            target, frame_num, frame.GetPC(), frame.GetFP()
                        )
                        print_stack_frame(process, frame.GetFP())
                        print("")
                        if this_module is not None:
                            modules_seen.append(this_module)
                        addresses_seen.append(frame.GetPC())
                        frame_num = frame_num + 1
                print("")
                print(
                    "============================================================================================="
                )
                print("")
                print("Simple stack walk algorithm:")
                print("")
                (module_list, address_list) = simple_backtrace(debugger)
                if module_list and module_list is not None:
                    modules_seen += module_list
                if address_list and address_list is not None:
                    addresses_seen = set(addresses_seen)
                    addresses_seen.update(set(address_list))

                print("")
                print(
                    "============================================================================================="
                )
                print("")
                print("Modules seen in stack walks:")
                print("")
                modules_already_seen = set()
                for module in modules_seen:
                    if (
                        module is not None
                        and module.GetFileSpec().GetFilename() is not None
                    ):
                        if (
                            not module.GetFileSpec().GetFilename()
                            in modules_already_seen
                        ):
                            debugger.HandleCommand(
                                "image list %s" % module.GetFileSpec().GetFilename()
                            )
                            modules_already_seen.add(module.GetFileSpec().GetFilename())

                print("")
                print(
                    "============================================================================================="
                )
                print("")
                print("Disassembly ofaddresses seen in stack walks:")
                print("")
                additional_addresses_to_disassemble = addresses_seen
                for frame in thread.frames:
                    if not frame.IsInlined():
                        print(
                            "--------------------------------------------------------------------------------------"
                        )
                        print("")
                        print(
                            "Disassembly of %s, frame %d, address 0x%x"
                            % (
                                frame.GetFunctionName(),
                                frame.GetFrameID(),
                                frame.GetPC(),
                            )
                        )
                        print("")
                        if (
                            target.triple[0:6] == "x86_64"
                            or target.triple[0:4] == "i386"
                        ):
                            debugger.HandleCommand(
                                "disassemble -F att -a 0x%x" % frame.GetPC()
                            )
                        else:
                            debugger.HandleCommand(
                                "disassemble -a 0x%x" % frame.GetPC()
                            )
                        if frame.GetPC() in additional_addresses_to_disassemble:
                            additional_addresses_to_disassemble.remove(frame.GetPC())

                for address in list(additional_addresses_to_disassemble):
                    print(
                        "--------------------------------------------------------------------------------------"
                    )
                    print("")
                    print("Disassembly of 0x%x" % address)
                    print("")
                    if target.triple[0:6] == "x86_64" or target.triple[0:4] == "i386":
                        debugger.HandleCommand("disassemble -F att -a 0x%x" % address)
                    else:
                        debugger.HandleCommand("disassemble -a 0x%x" % address)

                print("")
                print(
                    "============================================================================================="
                )
                print("")
                additional_addresses_to_show_unwind = addresses_seen
                for frame in thread.frames:
                    if not frame.IsInlined():
                        print(
                            "--------------------------------------------------------------------------------------"
                        )
                        print("")
                        print(
                            "Unwind instructions for %s, frame %d"
                            % (frame.GetFunctionName(), frame.GetFrameID())
                        )
                        print("")
                        debugger.HandleCommand(
                            'image show-unwind -a "0x%x"' % frame.GetPC()
                        )
                        if frame.GetPC() in additional_addresses_to_show_unwind:
                            additional_addresses_to_show_unwind.remove(frame.GetPC())

                for address in list(additional_addresses_to_show_unwind):
                    print(
                        "--------------------------------------------------------------------------------------"
                    )
                    print("")
                    print("Unwind instructions for 0x%x" % address)
                    print("")
                    debugger.HandleCommand('image show-unwind -a "0x%x"' % address)


def create_diagnose_unwind_options():
    usage = "usage: %prog"
    description = """Print diagnostic information about a thread backtrace which will help to debug unwind problems"""
    parser = optparse.OptionParser(
        description=description, prog="diagnose_unwind", usage=usage
    )
    return parser


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand(
        "command script add -o -f %s.diagnose_unwind diagnose-unwind" % __name__
    )
    print(
        'The "diagnose-unwind" command has been installed, type "help diagnose-unwind" for detailed help.'
    )
