# Usage:
#   art/test/run-test --host --gdb [--64] [--interpreter] 004-JniTest
#   'b Java_Main_shortMethod'
#   'r'
#   'command script import host_art_bt.py'
#   'host_art_bt'

import sys
import re

import lldb


def host_art_bt(debugger, command, result, internal_dict):
    prettified_frames = []
    lldb_frame_index = 0
    art_frame_index = 0
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    while lldb_frame_index < thread.GetNumFrames():
        frame = thread.GetFrameAtIndex(lldb_frame_index)
        if frame.GetModule() and re.match(
            r"JIT\(.*?\)", frame.GetModule().GetFileSpec().GetFilename()
        ):
            # Compiled Java frame

            # Get function/filename/lineno from symbol context
            symbol = frame.GetSymbol()
            if not symbol:
                print("No symbol info for compiled Java frame: ", frame)
                sys.exit(1)
            line_entry = frame.GetLineEntry()
            prettified_frames.append(
                {
                    "function": symbol.GetName(),
                    "file": str(line_entry.GetFileSpec()) if line_entry else None,
                    "line": line_entry.GetLine() if line_entry else -1,
                }
            )

            # Skip art frames
            while True:
                art_stack_visitor = frame.EvaluateExpression(
                    """struct GetStackVisitor : public StackVisitor { GetStackVisitor(int depth_) : StackVisitor(Thread::Current(), NULL), depth(depth_) {} bool VisitFrame() { if (cur_depth_ == depth) { return false; } else { return true; } } int depth; }; GetStackVisitor visitor("""
                    + str(art_frame_index)
                    + """); visitor.WalkStack(true); visitor"""
                )
                art_method = frame.EvaluateExpression(
                    art_stack_visitor.GetName() + """.GetMethod()"""
                )
                if art_method.GetValueAsUnsigned() != 0:
                    art_method_name = frame.EvaluateExpression(
                        """art::PrettyMethod(""" + art_method.GetName() + """, true)"""
                    )
                    art_method_name_data = frame.EvaluateExpression(
                        art_method_name.GetName() + """.c_str()"""
                    ).GetValueAsUnsigned()
                    art_method_name_size = frame.EvaluateExpression(
                        art_method_name.GetName() + """.length()"""
                    ).GetValueAsUnsigned()
                    error = lldb.SBError()
                    art_method_name = process.ReadCStringFromMemory(
                        art_method_name_data, art_method_name_size + 1, error
                    )
                    if not error.Success:
                        print("Failed to read method name")
                        sys.exit(1)
                    if art_method_name != symbol.GetName():
                        print(
                            "Function names in native symbol and art runtime stack do not match: ",
                            symbol.GetName(),
                            " != ",
                            art_method_name,
                        )
                    art_frame_index = art_frame_index + 1
                    break
                art_frame_index = art_frame_index + 1

            # Skip native frames
            lldb_frame_index = lldb_frame_index + 1
            if lldb_frame_index < thread.GetNumFrames():
                frame = thread.GetFrameAtIndex(lldb_frame_index)
                if frame.GetModule() and re.match(
                    r"JIT\(.*?\)", frame.GetModule().GetFileSpec().GetFilename()
                ):
                    # Another compile Java frame
                    # Don't skip; leave it to the next iteration
                    continue
                elif frame.GetSymbol() and (
                    frame.GetSymbol().GetName() == "art_quick_invoke_stub"
                    or frame.GetSymbol().GetName() == "art_quick_invoke_static_stub"
                ):
                    # art_quick_invoke_stub / art_quick_invoke_static_stub
                    # Skip until we get past the next ArtMethod::Invoke()
                    while True:
                        lldb_frame_index = lldb_frame_index + 1
                        if lldb_frame_index >= thread.GetNumFrames():
                            print(
                                "ArtMethod::Invoke not found below art_quick_invoke_stub/art_quick_invoke_static_stub"
                            )
                            sys.exit(1)
                        frame = thread.GetFrameAtIndex(lldb_frame_index)
                        if (
                            frame.GetSymbol()
                            and frame.GetSymbol().GetName()
                            == "art::mirror::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char const*)"
                        ):
                            lldb_frame_index = lldb_frame_index + 1
                            break
                else:
                    print("Invalid frame below compiled Java frame: ", frame)
        elif (
            frame.GetSymbol()
            and frame.GetSymbol().GetName() == "art_quick_generic_jni_trampoline"
        ):
            # Interpreted JNI frame for x86_64

            # Skip art frames
            while True:
                art_stack_visitor = frame.EvaluateExpression(
                    """struct GetStackVisitor : public StackVisitor { GetStackVisitor(int depth_) : StackVisitor(Thread::Current(), NULL), depth(depth_) {} bool VisitFrame() { if (cur_depth_ == depth) { return false; } else { return true; } } int depth; }; GetStackVisitor visitor("""
                    + str(art_frame_index)
                    + """); visitor.WalkStack(true); visitor"""
                )
                art_method = frame.EvaluateExpression(
                    art_stack_visitor.GetName() + """.GetMethod()"""
                )
                if art_method.GetValueAsUnsigned() != 0:
                    # Get function/filename/lineno from ART runtime
                    art_method_name = frame.EvaluateExpression(
                        """art::PrettyMethod(""" + art_method.GetName() + """, true)"""
                    )
                    art_method_name_data = frame.EvaluateExpression(
                        art_method_name.GetName() + """.c_str()"""
                    ).GetValueAsUnsigned()
                    art_method_name_size = frame.EvaluateExpression(
                        art_method_name.GetName() + """.length()"""
                    ).GetValueAsUnsigned()
                    error = lldb.SBError()
                    function = process.ReadCStringFromMemory(
                        art_method_name_data, art_method_name_size + 1, error
                    )

                    prettified_frames.append(
                        {"function": function, "file": None, "line": -1}
                    )

                    art_frame_index = art_frame_index + 1
                    break
                art_frame_index = art_frame_index + 1

            # Skip native frames
            lldb_frame_index = lldb_frame_index + 1
            if lldb_frame_index < thread.GetNumFrames():
                frame = thread.GetFrameAtIndex(lldb_frame_index)
                if frame.GetSymbol() and (
                    frame.GetSymbol().GetName() == "art_quick_invoke_stub"
                    or frame.GetSymbol().GetName() == "art_quick_invoke_static_stub"
                ):
                    # art_quick_invoke_stub / art_quick_invoke_static_stub
                    # Skip until we get past the next ArtMethod::Invoke()
                    while True:
                        lldb_frame_index = lldb_frame_index + 1
                        if lldb_frame_index >= thread.GetNumFrames():
                            print(
                                "ArtMethod::Invoke not found below art_quick_invoke_stub/art_quick_invoke_static_stub"
                            )
                            sys.exit(1)
                        frame = thread.GetFrameAtIndex(lldb_frame_index)
                        if (
                            frame.GetSymbol()
                            and frame.GetSymbol().GetName()
                            == "art::mirror::ArtMethod::Invoke(art::Thread*, unsigned int*, unsigned int, art::JValue*, char const*)"
                        ):
                            lldb_frame_index = lldb_frame_index + 1
                            break
                else:
                    print("Invalid frame below compiled Java frame: ", frame)
        elif frame.GetSymbol() and re.search(
            r"art::interpreter::", frame.GetSymbol().GetName()
        ):
            # Interpreted Java frame

            while True:
                lldb_frame_index = lldb_frame_index + 1
                if lldb_frame_index >= thread.GetNumFrames():
                    print("art::interpreter::Execute not found in interpreter frame")
                    sys.exit(1)
                frame = thread.GetFrameAtIndex(lldb_frame_index)
                if (
                    frame.GetSymbol()
                    and frame.GetSymbol().GetName()
                    == "art::interpreter::Execute(art::Thread*, art::MethodHelper&, art::DexFile::CodeItem const*, art::ShadowFrame&, art::JValue)"
                ):
                    break

            # Skip art frames
            while True:
                art_stack_visitor = frame.EvaluateExpression(
                    """struct GetStackVisitor : public StackVisitor { GetStackVisitor(int depth_) : StackVisitor(Thread::Current(), NULL), depth(depth_) {} bool VisitFrame() { if (cur_depth_ == depth) { return false; } else { return true; } } int depth; }; GetStackVisitor visitor("""
                    + str(art_frame_index)
                    + """); visitor.WalkStack(true); visitor"""
                )
                art_method = frame.EvaluateExpression(
                    art_stack_visitor.GetName() + """.GetMethod()"""
                )
                if art_method.GetValueAsUnsigned() != 0:
                    # Get function/filename/lineno from ART runtime
                    art_method_name = frame.EvaluateExpression(
                        """art::PrettyMethod(""" + art_method.GetName() + """, true)"""
                    )
                    art_method_name_data = frame.EvaluateExpression(
                        art_method_name.GetName() + """.c_str()"""
                    ).GetValueAsUnsigned()
                    art_method_name_size = frame.EvaluateExpression(
                        art_method_name.GetName() + """.length()"""
                    ).GetValueAsUnsigned()
                    error = lldb.SBError()
                    function = process.ReadCStringFromMemory(
                        art_method_name_data, art_method_name_size + 1, error
                    )

                    line = frame.EvaluateExpression(
                        art_stack_visitor.GetName()
                        + """.GetMethod()->GetLineNumFromDexPC("""
                        + art_stack_visitor.GetName()
                        + """.GetDexPc(true))"""
                    ).GetValueAsUnsigned()

                    file_name = frame.EvaluateExpression(
                        art_method.GetName() + """->GetDeclaringClassSourceFile()"""
                    )
                    file_name_data = file_name.GetValueAsUnsigned()
                    file_name_size = frame.EvaluateExpression(
                        """(size_t)strlen(""" + file_name.GetName() + """)"""
                    ).GetValueAsUnsigned()
                    error = lldb.SBError()
                    file_name = process.ReadCStringFromMemory(
                        file_name_data, file_name_size + 1, error
                    )
                    if not error.Success():
                        print("Failed to read source file name")
                        sys.exit(1)

                    prettified_frames.append(
                        {"function": function, "file": file_name, "line": line}
                    )

                    art_frame_index = art_frame_index + 1
                    break
                art_frame_index = art_frame_index + 1

            # Skip native frames
            while True:
                lldb_frame_index = lldb_frame_index + 1
                if lldb_frame_index >= thread.GetNumFrames():
                    print("Can not get past interpreter native frames")
                    sys.exit(1)
                frame = thread.GetFrameAtIndex(lldb_frame_index)
                if frame.GetSymbol() and not re.search(
                    r"art::interpreter::", frame.GetSymbol().GetName()
                ):
                    break
        else:
            # Other frames. Add them as-is.
            frame = thread.GetFrameAtIndex(lldb_frame_index)
            lldb_frame_index = lldb_frame_index + 1
            if frame.GetModule():
                module_name = frame.GetModule().GetFileSpec().GetFilename()
                if not module_name in [
                    "libartd.so",
                    "dalvikvm32",
                    "dalvikvm64",
                    "libc.so.6",
                ]:
                    prettified_frames.append(
                        {
                            "function": frame.GetSymbol().GetName()
                            if frame.GetSymbol()
                            else None,
                            "file": str(frame.GetLineEntry().GetFileSpec())
                            if frame.GetLineEntry()
                            else None,
                            "line": frame.GetLineEntry().GetLine()
                            if frame.GetLineEntry()
                            else -1,
                        }
                    )

    for prettified_frame in prettified_frames:
        print(
            prettified_frame["function"],
            prettified_frame["file"],
            prettified_frame["line"],
        )


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand("command script add -f host_art_bt.host_art_bt host_art_bt")
