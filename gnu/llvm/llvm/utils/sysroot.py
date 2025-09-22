#!/usr/bin/env python3

"""Helps manage sysroots."""

import argparse
import os
import subprocess
import sys


def make_fake_sysroot(out_dir):
    def cmdout(cmd):
        return subprocess.check_output(cmd).decode(sys.stdout.encoding).strip()

    if sys.platform == "win32":

        def mkjunction(dst, src):
            subprocess.check_call(["mklink", "/j", dst, src], shell=True)

        os.mkdir(out_dir)
        p = os.getenv("ProgramFiles(x86)", "C:\\Program Files (x86)")

        winsdk = os.getenv("WindowsSdkDir")
        if not winsdk:
            winsdk = os.path.join(p, "Windows Kits", "10")
            print("%WindowsSdkDir% not set. You might want to run this from")
            print("a Visual Studio cmd prompt. Defaulting to", winsdk)
        os.mkdir(os.path.join(out_dir, "Windows Kits"))
        mkjunction(os.path.join(out_dir, "Windows Kits", "10"), winsdk)

        vswhere = os.path.join(p, "Microsoft Visual Studio", "Installer", "vswhere")
        vcid = "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
        vsinstalldir = cmdout(
            [
                vswhere,
                "-latest",
                "-products",
                "*",
                "-requires",
                vcid,
                "-property",
                "installationPath",
            ]
        )

        mkjunction(os.path.join(out_dir, "VC"), os.path.join(vsinstalldir, "VC"))
        # Not all MSVC versions ship the DIA SDK, so the junction destination
        # might not exist. That's fine.
        mkjunction(
            os.path.join(out_dir, "DIA SDK"), os.path.join(vsinstalldir, "DIA SDK")
        )
    elif sys.platform == "darwin":
        # The SDKs used by default in compiler-rt/cmake/base-config-ix.cmake.
        # COMPILER_RT_ENABLE_IOS defaults to on.
        # COMPILER_RT_ENABLE_WATCHOS and COMPILER_RT_ENABLE_TV default to off.
        # compiler-rt/cmake/config-ix.cmake sets DARWIN_EMBEDDED_PLATFORMS
        # depending on these.
        sdks = ["macosx", "iphoneos", "iphonesimulator"]
        os.mkdir(out_dir)
        for sdk in sdks:
            sdkpath = cmdout(["xcrun", "-sdk", sdk, "-show-sdk-path"])
            # sdkpath is something like /.../SDKs/MacOSX11.1.sdk, which is a
            # symlink to MacOSX.sdk in the same directory. Resolve the symlink,
            # to make the symlink in out_dir less likely to break when the SDK
            # is updated (which will bump the number on xcrun's output, but not
            # on the symlink destination).
            sdkpath = os.path.realpath(sdkpath)
            os.symlink(sdkpath, os.path.join(out_dir, os.path.basename(sdkpath)))
    else:
        os.symlink("/", out_dir)

    print("Done. Pass these flags to cmake:")
    abs_out_dir = os.path.abspath(out_dir)
    if sys.platform == "win32":
        # CMake doesn't like backslashes in commandline args.
        abs_out_dir = abs_out_dir.replace(os.path.sep, "/")
        print("  -DLLVM_WINSYSROOT=" + abs_out_dir)
    elif sys.platform == "darwin":
        flags = [
            "-DCMAKE_OSX_SYSROOT=" + os.path.join(abs_out_dir, "MacOSX.sdk"),
            # For find_darwin_sdk_dir() in
            # compiler-rt/cmake/Modules/CompilerRTDarwinUtils.cmake
            "-DDARWIN_macosx_CACHED_SYSROOT=" + os.path.join(abs_out_dir, "MacOSX.sdk"),
            "-DDARWIN_iphoneos_CACHED_SYSROOT="
            + os.path.join(abs_out_dir, "iPhoneOS.sdk"),
            "-DDARWIN_iphonesimulator_CACHED_SYSROOT="
            + os.path.join(abs_out_dir, "iPhoneSimulator.sdk"),
        ]
        print("  " + " ".join(flags))
    else:
        print("  -DCMAKE_SYSROOT=" + abs_out_dir + " to cmake.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)

    subparsers = parser.add_subparsers(dest="command", required=True)

    makefake = subparsers.add_parser(
        "make-fake", help="Create a sysroot that symlinks to local directories."
    )
    makefake.add_argument("--out-dir", required=True)

    args = parser.parse_args()

    assert args.command == "make-fake"
    make_fake_sysroot(args.out_dir)


if __name__ == "__main__":
    main()
