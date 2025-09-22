#!/usr/bin/env python3

import os
import sys
import argparse
import sysconfig


def relpath_nodots(path, base):
    rel = os.path.normpath(os.path.relpath(path, base))
    assert not os.path.isabs(rel)
    parts = rel.split(os.path.sep)
    if parts and parts[0] == "..":
        raise ValueError(f"{path} is not under {base}")
    return rel


def main():
    parser = argparse.ArgumentParser(description="extract cmake variables from python")
    parser.add_argument("variable_name")
    args = parser.parse_args()
    if args.variable_name == "LLDB_PYTHON_RELATIVE_PATH":
        # LLDB_PYTHON_RELATIVE_PATH is the relative path from lldb's prefix
        # to where lldb's python libraries will be installed.
        #
        # The way we're going to compute this is to take the relative path from
        # PYTHON'S prefix to where python libraries are supposed to be
        # installed.
        #
        # The result is if LLDB and python are give the same prefix, then
        # lldb's python lib will be put in the correct place for python to find it.
        # If not, you'll have to use lldb -P or lldb -print-script-interpreter-info
        # to figure out where it is.
        try:
            print(relpath_nodots(sysconfig.get_path("platlib"), sys.prefix))
        except ValueError:
            # Try to fall back to something reasonable if sysconfig's platlib
            # is outside of sys.prefix
            if os.name == "posix":
                print("lib/python%d.%d/site-packages" % sys.version_info[:2])
            elif os.name == "nt":
                print("Lib\\site-packages")
            else:
                raise
    elif args.variable_name == "LLDB_PYTHON_EXE_RELATIVE_PATH":
        tried = list()
        exe = sys.executable
        prefix = os.path.realpath(sys.prefix)
        while True:
            try:
                print(relpath_nodots(exe, prefix))
                break
            except ValueError:
                tried.append(exe)
                # Retry if the executable is symlinked or similar.
                # This is roughly equal to os.path.islink, except it also works for junctions on Windows.
                if os.path.realpath(exe) != exe:
                    exe = os.path.realpath(exe)
                    continue
                else:
                    print(
                        "Could not find a relative path to sys.executable under sys.prefix",
                        file=sys.stderr,
                    )
                    for e in tried:
                        print("tried:", e, file=sys.stderr)
                    print("realpath(sys.prefix):", prefix, file=sys.stderr)
                    print("sys.prefix:", sys.prefix, file=sys.stderr)
                    sys.exit(1)
    elif args.variable_name == "LLDB_PYTHON_EXT_SUFFIX":
        print(sysconfig.get_config_var("EXT_SUFFIX"))
    else:
        parser.error(f"unknown variable {args.variable_name}")


if __name__ == "__main__":
    main()
