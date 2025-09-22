#!/usr/bin/env python
import lldb
import optparse
import shlex


def stack_frames(debugger, command, result, dict):
    command_args = shlex.split(command)
    usage = "usage: %prog [options] <PATH> [PATH ...]"
    description = """This command will enumerate all stack frames, print the stack size for each, and print an aggregation of which functions have the largest stack frame sizes at the end."""
    parser = optparse.OptionParser(description=description, prog="ls", usage=usage)
    parser.add_option(
        "-v",
        "--verbose",
        action="store_true",
        dest="verbose",
        help="display verbose debug info",
        default=False,
    )
    try:
        (options, args) = parser.parse_args(command_args)
    except:
        return

    target = debugger.GetSelectedTarget()
    process = target.GetProcess()

    frame_info = {}
    for thread in process:
        last_frame = None
        print("thread %u" % (thread.id))
        for frame in thread.frames:
            if last_frame:
                frame_size = 0
                if frame.idx == 1:
                    if frame.fp == last_frame.fp:
                        # No frame one the first frame (might be right at the
                        # entry point)
                        first_frame_size = 0
                        frame_size = frame.fp - frame.sp
                    else:
                        # First frame that has a valid size
                        first_frame_size = last_frame.fp - last_frame.sp
                    print("<%#7x> %s" % (first_frame_size, last_frame))
                    if first_frame_size:
                        name = last_frame.name
                        if name not in frame_info:
                            frame_info[name] = first_frame_size
                        else:
                            frame_info[name] += first_frame_size
                else:
                    # Second or higher frame
                    frame_size = frame.fp - last_frame.fp
                print("<%#7x> %s" % (frame_size, frame))
                if frame_size > 0:
                    name = frame.name
                    if name not in frame_info:
                        frame_info[name] = frame_size
                    else:
                        frame_info[name] += frame_size
            last_frame = frame
    print(frame_info)


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand("command script add -o -f stacks.stack_frames stack_frames")
    print(
        "A new command called 'stack_frames' was added, type 'stack_frames --help' for more information."
    )
