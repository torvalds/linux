import argparse
import os
import sys

from subprocess import call, check_call, CalledProcessError
from time import sleep
from typing import List, Tuple


def main():
    settings, rest = parse_arguments()
    cmake_opts = ["-D" + cmd for cmd in settings.D]
    if settings.wait:
        wait()
    if settings.build_llvm or settings.build_llvm_only:
        build_llvm(cmake_opts)
    if settings.build_llvm_only:
        return
    sys.exit(test(rest))


def wait():
    # It is an easy on CPU way of keeping the docker container running
    # while the user has a terminal session in that container.
    while True:
        sleep(3600)


def parse_arguments() -> Tuple[argparse.Namespace, List[str]]:
    parser = argparse.ArgumentParser()
    parser.add_argument("--wait", action="store_true")
    parser.add_argument("--build-llvm", action="store_true")
    parser.add_argument("--build-llvm-only", action="store_true")
    parser.add_argument("-D", action="append", default=[])
    return parser.parse_known_args()


def build_llvm(cmake_options):
    os.chdir("/build")
    try:
        if is_cmake_needed():
            cmake(cmake_options)
        ninja()
    except CalledProcessError:
        print("Build failed!")
        sys.exit(1)


def is_cmake_needed():
    return "build.ninja" not in os.listdir()


CMAKE_COMMAND = (
    "cmake -G Ninja -DCMAKE_BUILD_TYPE=Release "
    "-DCMAKE_INSTALL_PREFIX=/analyzer -DLLVM_TARGETS_TO_BUILD=X86 "
    '-DLLVM_ENABLE_PROJECTS="clang;openmp" -DLLVM_BUILD_RUNTIME=OFF '
    "-DCLANG_ENABLE_ARCMT=OFF "
    "-DCLANG_ENABLE_STATIC_ANALYZER=ON"
)


def cmake(cmake_options):
    check_call(
        CMAKE_COMMAND + " ".join(cmake_options) + " /llvm-project/llvm", shell=True
    )


def ninja():
    check_call("ninja install", shell=True)


def test(args: List[str]) -> int:
    os.chdir("/projects")
    return call("/scripts/SATest.py " + " ".join(args), shell=True)


if __name__ == "__main__":
    main()
