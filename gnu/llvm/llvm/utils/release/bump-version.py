#!/usr/bin/env python3

# This script bumps the version of LLVM in *all* the different places where
# it needs to be defined. Which is quite a few.

import sys
import argparse
import packaging.version
from pathlib import Path
import re
from typing import Optional


class Processor:
    def __init__(self, args):
        self.args = args

    def process_line(self, line: str) -> str:
        raise NotImplementedError()

    def process_file(self, fpath: Path, version: packaging.version.Version) -> None:
        self.version = version
        self.major, self.minor, self.patch, self.suffix = (
            version.major,
            version.minor,
            version.micro,
            version.pre,
        )

        if self.args.rc:
            self.suffix = f"-rc{self.args.rc}"

        if self.args.git:
            self.suffix = "git"

        data = fpath.read_text()
        new_data = []

        for line in data.splitlines(True):
            nline = self.process_line(line)

            # Print the failing line just to inform the user.
            if nline != line:
                print(f"{fpath.name}: {line.strip()} -> {nline.strip()}")

            new_data.append(nline)

        fpath.write_text("".join(new_data), newline="\n")

    # Return a string from the version class
    # optionally include the suffix (-rcX)
    def version_str(
        self,
        version: Optional[packaging.version.Version] = None,
        include_suffix: bool = True,
    ) -> str:
        if version is None:
            version = self.version

        ver = f"{version.major}.{version.minor}.{version.micro}"
        if include_suffix and version.pre:
            ver += f"-{version.pre[0]}{version.pre[1]}"
        return ver


# llvm/CMakeLists.txt
class CMakeProcessor(Processor):
    def process_line(self, line: str) -> str:
        nline = line

        # LLVM_VERSION_SUFFIX should be set to -rcX or be blank if we are
        # building a final version.
        if "set(LLVM_VERSION_SUFFIX" in line:
            if self.suffix:
                nline = re.sub(
                    r"set\(LLVM_VERSION_SUFFIX(.*)\)",
                    f"set(LLVM_VERSION_SUFFIX {self.suffix})",
                    line,
                )
            else:
                nline = re.sub(
                    r"set\(LLVM_VERSION_SUFFIX(.*)\)", f"set(LLVM_VERSION_SUFFIX)", line
                )

        # Check the rest of the LLVM_VERSION_ lines.
        elif "set(LLVM_VERSION_" in line:
            for c, cver in (
                ("MAJOR", self.major),
                ("MINOR", self.minor),
                ("PATCH", self.patch),
            ):
                nline = re.sub(
                    rf"set\(LLVM_VERSION_{c} (\d+)",
                    rf"set(LLVM_VERSION_{c} {cver}",
                    line,
                )
                if nline != line:
                    break

        return nline


# GN build system
class GNIProcessor(Processor):
    def process_line(self, line: str) -> str:
        if "llvm_version_" in line:
            for c, cver in (
                ("major", self.major),
                ("minor", self.minor),
                ("patch", self.patch),
            ):
                nline = re.sub(
                    rf"llvm_version_{c} = \d+", f"llvm_version_{c} = {cver}", line
                )
                if nline != line:
                    return nline

        return line


# LIT python file, a simple tuple
class LitProcessor(Processor):
    def process_line(self, line: str) -> str:
        if "__versioninfo__" in line:
            nline = re.sub(
                rf"__versioninfo__(.*)\((\d+), (\d+), (\d+)\)",
                f"__versioninfo__\\1({self.major}, {self.minor}, {self.patch})",
                line,
            )
            return nline
        return line


# Handle libc++ config header
class LibCXXProcessor(Processor):
    def process_line(self, line: str) -> str:
        # match #define _LIBCPP_VERSION 160000 in a relaxed way
        match = re.match(r".*\s_LIBCPP_VERSION\s+(\d{6})$", line)
        if match:
            verstr = f"{str(self.major).zfill(2)}{str(self.minor).zfill(2)}{str(self.patch).zfill(2)}"

            nline = re.sub(
                rf"_LIBCPP_VERSION (\d+)",
                f"_LIBCPP_VERSION {verstr}",
                line,
            )
            return nline
        return line


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        usage="Call this script with a version and it will bump the version for you"
    )
    parser.add_argument("version", help="Version to bump to, e.g. 15.0.1", default=None)
    parser.add_argument("--rc", default=None, type=int, help="RC version")
    parser.add_argument("--git", action="store_true", help="Git version")
    parser.add_argument(
        "-s",
        "--source-root",
        default=None,
        help="LLVM source root (/path/llvm-project). Defaults to the llvm-project the script is located in.",
    )

    args = parser.parse_args()

    if args.rc and args.git:
        raise RuntimeError("Can't specify --git and --rc at the same time!")

    verstr = args.version

    # parse the version string with distutils.
    # note that -rc will end up as version.pre here
    # since it's a prerelease
    version = packaging.version.parse(verstr)

    # Find llvm-project root
    source_root = Path(__file__).resolve().parents[3]

    if args.source_root:
        source_root = Path(args.source_root).resolve()

    files_to_update = (
        # Main CMakeLists.
        (source_root / "cmake" / "Modules" / "LLVMVersion.cmake", CMakeProcessor(args)),
        # Lit configuration
        (
            "llvm/utils/lit/lit/__init__.py",
            LitProcessor(args),
        ),
        # mlgo-utils configuration
        (
            "llvm/utils/mlgo-utils/mlgo/__init__.py",
            LitProcessor(args),
        ),
        # GN build system
        (
            "llvm/utils/gn/secondary/llvm/version.gni",
            GNIProcessor(args),
        ),
        (
            "libcxx/include/__config",
            LibCXXProcessor(args),
        ),
    )

    for f, processor in files_to_update:
        processor.process_file(source_root / Path(f), version)
