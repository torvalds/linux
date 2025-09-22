# -*- coding: utf-8 -*-
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
""" This module compiles the intercept library. """

import sys
import os
import os.path
import re
import tempfile
import shutil
import contextlib
import logging

__all__ = ["build_libear"]


def build_libear(compiler, dst_dir):
    """Returns the full path to the 'libear' library."""

    try:
        src_dir = os.path.dirname(os.path.realpath(__file__))
        toolset = make_toolset(src_dir)
        toolset.set_compiler(compiler)
        toolset.set_language_standard("c99")
        toolset.add_definitions(["-D_GNU_SOURCE"])

        configure = do_configure(toolset)
        configure.check_function_exists("execve", "HAVE_EXECVE")
        configure.check_function_exists("execv", "HAVE_EXECV")
        configure.check_function_exists("execvpe", "HAVE_EXECVPE")
        configure.check_function_exists("execvp", "HAVE_EXECVP")
        configure.check_function_exists("execvP", "HAVE_EXECVP2")
        configure.check_function_exists("exect", "HAVE_EXECT")
        configure.check_function_exists("execl", "HAVE_EXECL")
        configure.check_function_exists("execlp", "HAVE_EXECLP")
        configure.check_function_exists("execle", "HAVE_EXECLE")
        configure.check_function_exists("posix_spawn", "HAVE_POSIX_SPAWN")
        configure.check_function_exists("posix_spawnp", "HAVE_POSIX_SPAWNP")
        configure.check_symbol_exists(
            "_NSGetEnviron", "crt_externs.h", "HAVE_NSGETENVIRON"
        )
        configure.write_by_template(
            os.path.join(src_dir, "config.h.in"), os.path.join(dst_dir, "config.h")
        )

        target = create_shared_library("ear", toolset)
        target.add_include(dst_dir)
        target.add_sources("ear.c")
        target.link_against(toolset.dl_libraries())
        target.link_against(["pthread"])
        target.build_release(dst_dir)

        return os.path.join(dst_dir, target.name)

    except Exception:
        logging.info("Could not build interception library.", exc_info=True)
        return None


def execute(cmd, *args, **kwargs):
    """Make subprocess execution silent."""

    import subprocess

    kwargs.update({"stdout": subprocess.PIPE, "stderr": subprocess.STDOUT})
    return subprocess.check_call(cmd, *args, **kwargs)


@contextlib.contextmanager
def TemporaryDirectory(**kwargs):
    name = tempfile.mkdtemp(**kwargs)
    try:
        yield name
    finally:
        shutil.rmtree(name)


class Toolset(object):
    """Abstract class to represent different toolset."""

    def __init__(self, src_dir):
        self.src_dir = src_dir
        self.compiler = None
        self.c_flags = []

    def set_compiler(self, compiler):
        """part of public interface"""
        self.compiler = compiler

    def set_language_standard(self, standard):
        """part of public interface"""
        self.c_flags.append("-std=" + standard)

    def add_definitions(self, defines):
        """part of public interface"""
        self.c_flags.extend(defines)

    def dl_libraries(self):
        raise NotImplementedError()

    def shared_library_name(self, name):
        raise NotImplementedError()

    def shared_library_c_flags(self, release):
        extra = ["-DNDEBUG", "-O3"] if release else []
        return extra + ["-fPIC"] + self.c_flags

    def shared_library_ld_flags(self, release, name):
        raise NotImplementedError()


class DarwinToolset(Toolset):
    def __init__(self, src_dir):
        Toolset.__init__(self, src_dir)

    def dl_libraries(self):
        return []

    def shared_library_name(self, name):
        return "lib" + name + ".dylib"

    def shared_library_ld_flags(self, release, name):
        extra = ["-dead_strip"] if release else []
        return extra + ["-dynamiclib", "-install_name", "@rpath/" + name]


class UnixToolset(Toolset):
    def __init__(self, src_dir):
        Toolset.__init__(self, src_dir)

    def dl_libraries(self):
        return []

    def shared_library_name(self, name):
        return "lib" + name + ".so"

    def shared_library_ld_flags(self, release, name):
        extra = [] if release else []
        return extra + ["-shared", "-Wl,-soname," + name]


class LinuxToolset(UnixToolset):
    def __init__(self, src_dir):
        UnixToolset.__init__(self, src_dir)

    def dl_libraries(self):
        return ["dl"]


def make_toolset(src_dir):
    platform = sys.platform
    if platform in {"win32", "cygwin"}:
        raise RuntimeError("not implemented on this platform")
    elif platform == "darwin":
        return DarwinToolset(src_dir)
    elif platform in {"linux", "linux2"}:
        return LinuxToolset(src_dir)
    else:
        return UnixToolset(src_dir)


class Configure(object):
    def __init__(self, toolset):
        self.ctx = toolset
        self.results = {"APPLE": sys.platform == "darwin"}

    def _try_to_compile_and_link(self, source):
        try:
            with TemporaryDirectory() as work_dir:
                src_file = "check.c"
                with open(os.path.join(work_dir, src_file), "w") as handle:
                    handle.write(source)

                execute([self.ctx.compiler, src_file] + self.ctx.c_flags, cwd=work_dir)
                return True
        except Exception:
            return False

    def check_function_exists(self, function, name):
        template = "int FUNCTION(); int main() { return FUNCTION(); }"
        source = template.replace("FUNCTION", function)

        logging.debug("Checking function %s", function)
        found = self._try_to_compile_and_link(source)
        logging.debug(
            "Checking function %s -- %s", function, "found" if found else "not found"
        )
        self.results.update({name: found})

    def check_symbol_exists(self, symbol, include, name):
        template = """#include <INCLUDE>
                      int main() { return ((int*)(&SYMBOL))[0]; }"""
        source = template.replace("INCLUDE", include).replace("SYMBOL", symbol)

        logging.debug("Checking symbol %s", symbol)
        found = self._try_to_compile_and_link(source)
        logging.debug(
            "Checking symbol %s -- %s", symbol, "found" if found else "not found"
        )
        self.results.update({name: found})

    def write_by_template(self, template, output):
        def transform(line, definitions):

            pattern = re.compile(r"^#cmakedefine\s+(\S+)")
            m = pattern.match(line)
            if m:
                key = m.group(1)
                if key not in definitions or not definitions[key]:
                    return "/* #undef {0} */{1}".format(key, os.linesep)
                else:
                    return "#define {0}{1}".format(key, os.linesep)
            return line

        with open(template, "r") as src_handle:
            logging.debug("Writing config to %s", output)
            with open(output, "w") as dst_handle:
                for line in src_handle:
                    dst_handle.write(transform(line, self.results))


def do_configure(toolset):
    return Configure(toolset)


class SharedLibrary(object):
    def __init__(self, name, toolset):
        self.name = toolset.shared_library_name(name)
        self.ctx = toolset
        self.inc = []
        self.src = []
        self.lib = []

    def add_include(self, directory):
        self.inc.extend(["-I", directory])

    def add_sources(self, source):
        self.src.append(source)

    def link_against(self, libraries):
        self.lib.extend(["-l" + lib for lib in libraries])

    def build_release(self, directory):
        for src in self.src:
            logging.debug("Compiling %s", src)
            execute(
                [
                    self.ctx.compiler,
                    "-c",
                    os.path.join(self.ctx.src_dir, src),
                    "-o",
                    src + ".o",
                ]
                + self.inc
                + self.ctx.shared_library_c_flags(True),
                cwd=directory,
            )
        logging.debug("Linking %s", self.name)
        execute(
            [self.ctx.compiler]
            + [src + ".o" for src in self.src]
            + ["-o", self.name]
            + self.lib
            + self.ctx.shared_library_ld_flags(True, self.name),
            cwd=directory,
        )


def create_shared_library(name, toolset):
    return SharedLibrary(name, toolset)
