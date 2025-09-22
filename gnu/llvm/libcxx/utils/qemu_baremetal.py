#!/usr/bin/env python3
# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##

"""qemu_baremetal.py is a utility for running a program with QEMU's system mode.

It is able to pass command line arguments to the program and forward input and
output (if the underlying baremetal enviroment supports QEMU semihosting).
"""

import argparse
import os
import sys
import shutil


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", type=str, required=True)
    parser.add_argument("--cpu", type=str, required=False)
    parser.add_argument("--machine", type=str, default="virt")
    parser.add_argument(
        "--qemu-arg", dest="qemu_args", type=str, action="append", default=[]
    )
    parser.add_argument("--semihosting", action="store_true", default=True)
    parser.add_argument("--no-semihosting", dest="semihosting", action="store_false")
    parser.add_argument("--execdir", type=str, required=True)
    parser.add_argument("test_binary")
    parser.add_argument("test_args", nargs=argparse.ZERO_OR_MORE, default=[])
    args = parser.parse_args()

    if not shutil.which(args.qemu):
        sys.exit(f"Failed to find QEMU binary from --qemu value: '{args.qemu}'")

    if not os.path.exists(args.test_binary):
        sys.exit(f"Expected argument to be a test executable: '{args.test_binary}'")

    qemu_commandline = [
        args.qemu,
        "-chardev",
        "stdio,mux=on,id=stdio0",
        "-monitor",
        "none",
        "-serial",
        "none",
        "-machine",
        f"{args.machine},accel=tcg",
        "-device",
        f"loader,file={args.test_binary},cpu-num=0",
        "-nographic",
        *args.qemu_args,
    ]
    if args.cpu:
        qemu_commandline += ["-cpu", args.cpu]

    if args.semihosting:
        # Use QEMU's semihosting support to pass argv (supported by picolibc)
        semihosting_config = f"enable=on,chardev=stdio0,arg={args.test_binary}"
        for arg in args.test_args:
            semihosting_config += f",arg={arg}"
        qemu_commandline += ["-semihosting-config", semihosting_config]
    elif args.test_args:
        sys.exit(
            "Got non-empty test arguments but do no know how to pass them to "
            "QEMU without semihosting support"
        )
    os.execvp(qemu_commandline[0], qemu_commandline)


if __name__ == "__main__":
    exit(main())
