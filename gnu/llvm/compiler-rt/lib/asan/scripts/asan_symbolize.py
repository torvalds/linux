#!/usr/bin/env python
# ===- lib/asan/scripts/asan_symbolize.py -----------------------------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===#
"""
Example of use:
  asan_symbolize.py -c "$HOME/opt/cross/bin/arm-linux-gnueabi-" -s "$HOME/SymbolFiles" < asan.log

PLUGINS

This script provides a way for external plug-ins to hook into the behaviour of
various parts of this script (see `--plugins`). This is useful for situations
where it is necessary to handle site-specific quirks (e.g. binaries with debug
symbols only accessible via a remote service) without having to modify the
script itself.

"""
import argparse
import bisect
import errno
import getopt
import logging
import os
import re
import shutil
import subprocess
import sys

symbolizers = {}
demangle = False
binutils_prefix = None
fix_filename_patterns = None
logfile = sys.stdin
allow_system_symbolizer = True
force_system_symbolizer = False

# FIXME: merge the code that calls fix_filename().
def fix_filename(file_name):
    if fix_filename_patterns:
        for path_to_cut in fix_filename_patterns:
            file_name = re.sub(".*" + path_to_cut, "", file_name)
    file_name = re.sub(".*asan_[a-z_]*.(cc|cpp):[0-9]*", "_asan_rtl_", file_name)
    file_name = re.sub(".*crtstuff.c:0", "???:0", file_name)
    return file_name


def is_valid_arch(s):
    return s in [
        "i386",
        "x86_64",
        "x86_64h",
        "arm",
        "armv6",
        "armv7",
        "armv7s",
        "armv7k",
        "arm64",
        "powerpc64",
        "powerpc64le",
        "s390x",
        "s390",
        "riscv64",
        "loongarch64",
    ]


def guess_arch(addr):
    # Guess which arch we're running. 10 = len('0x') + 8 hex digits.
    if len(addr) > 10:
        return "x86_64"
    else:
        return "i386"


class Symbolizer(object):
    def __init__(self):
        pass

    def symbolize(self, addr, binary, offset):
        """Symbolize the given address (pair of binary and offset).

        Overriden in subclasses.
        Args:
            addr: virtual address of an instruction.
            binary: path to executable/shared object containing this instruction.
            offset: instruction offset in the @binary.
        Returns:
            list of strings (one string for each inlined frame) describing
            the code locations for this instruction (that is, function name, file
            name, line and column numbers).
        """
        return None


class LLVMSymbolizer(Symbolizer):
    def __init__(self, symbolizer_path, default_arch, system, dsym_hints=[]):
        super(LLVMSymbolizer, self).__init__()
        self.symbolizer_path = symbolizer_path
        self.default_arch = default_arch
        self.system = system
        self.dsym_hints = dsym_hints
        self.pipe = self.open_llvm_symbolizer()

    def open_llvm_symbolizer(self):
        cmd = [
            self.symbolizer_path,
            ("--demangle" if demangle else "--no-demangle"),
            "--functions=linkage",
            "--inlines",
            "--default-arch=%s" % self.default_arch,
        ]
        if self.system == "Darwin":
            for hint in self.dsym_hints:
                cmd.append("--dsym-hint=%s" % hint)
        logging.debug(" ".join(cmd))
        try:
            result = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                bufsize=0,
                universal_newlines=True,
            )
        except OSError:
            result = None
        return result

    def symbolize(self, addr, binary, offset):
        """Overrides Symbolizer.symbolize."""
        if not self.pipe:
            return None
        result = []
        try:
            symbolizer_input = '"%s" %s' % (binary, offset)
            logging.debug(symbolizer_input)
            self.pipe.stdin.write("%s\n" % symbolizer_input)
            while True:
                function_name = self.pipe.stdout.readline().rstrip()
                if not function_name:
                    break
                file_name = self.pipe.stdout.readline().rstrip()
                file_name = fix_filename(file_name)
                if not function_name.startswith("??") or not file_name.startswith("??"):
                    # Append only non-trivial frames.
                    result.append("%s in %s %s" % (addr, function_name, file_name))
        except Exception:
            result = []
        if not result:
            result = None
        return result


def LLVMSymbolizerFactory(system, default_arch, dsym_hints=[]):
    symbolizer_path = os.getenv("LLVM_SYMBOLIZER_PATH")
    if not symbolizer_path:
        symbolizer_path = os.getenv("ASAN_SYMBOLIZER_PATH")
        if not symbolizer_path:
            # Assume llvm-symbolizer is in PATH.
            symbolizer_path = "llvm-symbolizer"
    return LLVMSymbolizer(symbolizer_path, default_arch, system, dsym_hints)


class Addr2LineSymbolizer(Symbolizer):
    def __init__(self, binary):
        super(Addr2LineSymbolizer, self).__init__()
        self.binary = binary
        self.pipe = self.open_addr2line()
        self.output_terminator = -1

    def open_addr2line(self):
        addr2line_tool = "addr2line"
        if binutils_prefix:
            addr2line_tool = binutils_prefix + addr2line_tool
        logging.debug("addr2line binary is %s" % shutil.which(addr2line_tool))
        cmd = [addr2line_tool, "-fi"]
        if demangle:
            cmd += ["--demangle"]
        cmd += ["-e", self.binary]
        logging.debug(" ".join(cmd))
        return subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            bufsize=0,
            universal_newlines=True,
        )

    def symbolize(self, addr, binary, offset):
        """Overrides Symbolizer.symbolize."""
        if self.binary != binary:
            return None
        lines = []
        try:
            self.pipe.stdin.write("%s\n" % offset)
            self.pipe.stdin.write("%s\n" % self.output_terminator)
            is_first_frame = True
            while True:
                function_name = self.pipe.stdout.readline().rstrip()
                logging.debug("read function_name='%s' from addr2line" % function_name)
                # If llvm-symbolizer is installed as addr2line, older versions of
                # llvm-symbolizer will print -1 when presented with -1 and not print
                # a second line. In that case we will block for ever trying to read the
                # file name. This also happens for non-existent files, in which case GNU
                # addr2line exits immediate, but llvm-symbolizer does not (see
                # https://llvm.org/PR42754).
                if function_name == "-1":
                    logging.debug("got function '-1' -> no more input")
                    break
                file_name = self.pipe.stdout.readline().rstrip()
                logging.debug("read file_name='%s' from addr2line" % file_name)
                if is_first_frame:
                    is_first_frame = False
                elif function_name == "??":
                    assert file_name == "??:0", file_name
                    logging.debug("got function '??' -> no more input")
                    break
                elif not function_name:
                    assert not file_name, file_name
                    logging.debug("got empty function name -> no more input")
                    break
                if not function_name and not file_name:
                    logging.debug(
                        "got empty function and file name -> unknown function"
                    )
                    function_name = "??"
                    file_name = "??:0"
                lines.append((function_name, file_name))
        except IOError as e:
            # EPIPE happens if addr2line exits early (which some implementations do
            # if an invalid file is passed).
            if e.errno == errno.EPIPE:
                logging.debug(
                    f"addr2line exited early (broken pipe) returncode={self.pipe.poll()}"
                )
            else:
                logging.debug(
                    "unexpected I/O exception communicating with addr2line", exc_info=e
                )
            lines.append(("??", "??:0"))
        except Exception as e:
            logging.debug(
                "got unknown exception communicating with addr2line", exc_info=e
            )
            lines.append(("??", "??:0"))
        return [
            "%s in %s %s" % (addr, function, fix_filename(file))
            for (function, file) in lines
        ]


class UnbufferedLineConverter(object):
    """
    Wrap a child process that responds to each line of input with one line of
    output.  Uses pty to trick the child into providing unbuffered output.
    """

    def __init__(self, args, close_stderr=False):
        # Local imports so that the script can start on Windows.
        import pty
        import termios

        pid, fd = pty.fork()
        if pid == 0:
            # We're the child. Transfer control to command.
            if close_stderr:
                dev_null = os.open("/dev/null", 0)
                os.dup2(dev_null, 2)
            os.execvp(args[0], args)
        else:
            # Disable echoing.
            attr = termios.tcgetattr(fd)
            attr[3] = attr[3] & ~termios.ECHO
            termios.tcsetattr(fd, termios.TCSANOW, attr)
            # Set up a file()-like interface to the child process
            self.r = os.fdopen(fd, "r", 1)
            self.w = os.fdopen(os.dup(fd), "w", 1)

    def convert(self, line):
        self.w.write(line + "\n")
        return self.readline()

    def readline(self):
        return self.r.readline().rstrip()


class DarwinSymbolizer(Symbolizer):
    def __init__(self, addr, binary, arch):
        super(DarwinSymbolizer, self).__init__()
        self.binary = binary
        self.arch = arch
        self.open_atos()

    def open_atos(self):
        logging.debug("atos -o %s -arch %s", self.binary, self.arch)
        cmdline = ["atos", "-o", self.binary, "-arch", self.arch]
        self.atos = UnbufferedLineConverter(cmdline, close_stderr=True)

    def symbolize(self, addr, binary, offset):
        """Overrides Symbolizer.symbolize."""
        if self.binary != binary:
            return None
        if not os.path.exists(binary):
            # If the binary doesn't exist atos will exit which will lead to IOError
            # exceptions being raised later on so just don't try to symbolize.
            return ["{} ({}:{}+{})".format(addr, binary, self.arch, offset)]
        atos_line = self.atos.convert("0x%x" % int(offset, 16))
        while "got symbolicator for" in atos_line:
            atos_line = self.atos.readline()
        # A well-formed atos response looks like this:
        #   foo(type1, type2) (in object.name) (filename.cc:80)
        # NOTE:
        #   * For C functions atos omits parentheses and argument types.
        #   * For C++ functions the function name (i.e., `foo` above) may contain
        #     templates which may contain parentheses.
        match = re.match("^(.*) \(in (.*)\) \((.*:\d*)\)$", atos_line)
        logging.debug("atos_line: %s", atos_line)
        if match:
            function_name = match.group(1)
            file_name = fix_filename(match.group(3))
            return ["%s in %s %s" % (addr, function_name, file_name)]
        else:
            return ["%s in %s" % (addr, atos_line)]


# Chain several symbolizers so that if one symbolizer fails, we fall back
# to the next symbolizer in chain.
class ChainSymbolizer(Symbolizer):
    def __init__(self, symbolizer_list):
        super(ChainSymbolizer, self).__init__()
        self.symbolizer_list = symbolizer_list

    def symbolize(self, addr, binary, offset):
        """Overrides Symbolizer.symbolize."""
        for symbolizer in self.symbolizer_list:
            if symbolizer:
                result = symbolizer.symbolize(addr, binary, offset)
                if result:
                    return result
        return None

    def append_symbolizer(self, symbolizer):
        self.symbolizer_list.append(symbolizer)


def BreakpadSymbolizerFactory(binary):
    suffix = os.getenv("BREAKPAD_SUFFIX")
    if suffix:
        filename = binary + suffix
        if os.access(filename, os.F_OK):
            return BreakpadSymbolizer(filename)
    return None


def SystemSymbolizerFactory(system, addr, binary, arch):
    if system == "Darwin":
        return DarwinSymbolizer(addr, binary, arch)
    elif system in ["Linux", "FreeBSD", "NetBSD", "SunOS"]:
        return Addr2LineSymbolizer(binary)


class BreakpadSymbolizer(Symbolizer):
    def __init__(self, filename):
        super(BreakpadSymbolizer, self).__init__()
        self.filename = filename
        lines = file(filename).readlines()
        self.files = []
        self.symbols = {}
        self.address_list = []
        self.addresses = {}
        # MODULE mac x86_64 A7001116478B33F18FF9BEDE9F615F190 t
        fragments = lines[0].rstrip().split()
        self.arch = fragments[2]
        self.debug_id = fragments[3]
        self.binary = " ".join(fragments[4:])
        self.parse_lines(lines[1:])

    def parse_lines(self, lines):
        cur_function_addr = ""
        for line in lines:
            fragments = line.split()
            if fragments[0] == "FILE":
                assert int(fragments[1]) == len(self.files)
                self.files.append(" ".join(fragments[2:]))
            elif fragments[0] == "PUBLIC":
                self.symbols[int(fragments[1], 16)] = " ".join(fragments[3:])
            elif fragments[0] in ["CFI", "STACK"]:
                pass
            elif fragments[0] == "FUNC":
                cur_function_addr = int(fragments[1], 16)
                if not cur_function_addr in self.symbols.keys():
                    self.symbols[cur_function_addr] = " ".join(fragments[4:])
            else:
                # Line starting with an address.
                addr = int(fragments[0], 16)
                self.address_list.append(addr)
                # Tuple of symbol address, size, line, file number.
                self.addresses[addr] = (
                    cur_function_addr,
                    int(fragments[1], 16),
                    int(fragments[2]),
                    int(fragments[3]),
                )
        self.address_list.sort()

    def get_sym_file_line(self, addr):
        key = None
        if addr in self.addresses.keys():
            key = addr
        else:
            index = bisect.bisect_left(self.address_list, addr)
            if index == 0:
                return None
            else:
                key = self.address_list[index - 1]
        sym_id, size, line_no, file_no = self.addresses[key]
        symbol = self.symbols[sym_id]
        filename = self.files[file_no]
        if addr < key + size:
            return symbol, filename, line_no
        else:
            return None

    def symbolize(self, addr, binary, offset):
        if self.binary != binary:
            return None
        res = self.get_sym_file_line(int(offset, 16))
        if res:
            function_name, file_name, line_no = res
            result = ["%s in %s %s:%d" % (addr, function_name, file_name, line_no)]
            print(result)
            return result
        else:
            return None


class SymbolizationLoop(object):
    def __init__(self, plugin_proxy=None, dsym_hint_producer=None):
        self.plugin_proxy = plugin_proxy
        if sys.platform == "win32":
            # ASan on Windows uses dbghelp.dll to symbolize in-process, which works
            # even in sandboxed processes.  Nothing needs to be done here.
            self.process_line = self.process_line_echo
        else:
            # Used by clients who may want to supply a different binary name.
            # E.g. in Chrome several binaries may share a single .dSYM.
            self.dsym_hint_producer = dsym_hint_producer
            self.system = os.uname()[0]
            if self.system not in ["Linux", "Darwin", "FreeBSD", "NetBSD", "SunOS"]:
                raise Exception("Unknown system")
            self.llvm_symbolizers = {}
            self.last_llvm_symbolizer = None
            self.dsym_hints = set([])
            self.frame_no = 0
            self.process_line = self.process_line_posix
            self.using_module_map = plugin_proxy.has_plugin(ModuleMapPlugIn.get_name())

    def symbolize_address(self, addr, binary, offset, arch):
        # On non-Darwin (i.e. on platforms without .dSYM debug info) always use
        # a single symbolizer binary.
        # On Darwin, if the dsym hint producer is present:
        #  1. check whether we've seen this binary already; if so,
        #     use |llvm_symbolizers[binary]|, which has already loaded the debug
        #     info for this binary (might not be the case for
        #     |last_llvm_symbolizer|);
        #  2. otherwise check if we've seen all the hints for this binary already;
        #     if so, reuse |last_llvm_symbolizer| which has the full set of hints;
        #  3. otherwise create a new symbolizer and pass all currently known
        #     .dSYM hints to it.
        result = None
        if not force_system_symbolizer:
            if not binary in self.llvm_symbolizers:
                use_new_symbolizer = True
                if self.system == "Darwin" and self.dsym_hint_producer:
                    dsym_hints_for_binary = set(self.dsym_hint_producer(binary))
                    use_new_symbolizer = bool(dsym_hints_for_binary - self.dsym_hints)
                    self.dsym_hints |= dsym_hints_for_binary
                if self.last_llvm_symbolizer and not use_new_symbolizer:
                    self.llvm_symbolizers[binary] = self.last_llvm_symbolizer
                else:
                    self.last_llvm_symbolizer = LLVMSymbolizerFactory(
                        self.system, arch, self.dsym_hints
                    )
                    self.llvm_symbolizers[binary] = self.last_llvm_symbolizer
            # Use the chain of symbolizers:
            # Breakpad symbolizer -> LLVM symbolizer -> addr2line/atos
            # (fall back to next symbolizer if the previous one fails).
            if not binary in symbolizers:
                symbolizers[binary] = ChainSymbolizer(
                    [BreakpadSymbolizerFactory(binary), self.llvm_symbolizers[binary]]
                )
            result = symbolizers[binary].symbolize(addr, binary, offset)
        else:
            symbolizers[binary] = ChainSymbolizer([])
        if result is None:
            if not allow_system_symbolizer:
                raise Exception("Failed to launch or use llvm-symbolizer.")
            # Initialize system symbolizer only if other symbolizers failed.
            symbolizers[binary].append_symbolizer(
                SystemSymbolizerFactory(self.system, addr, binary, arch)
            )
            result = symbolizers[binary].symbolize(addr, binary, offset)
        # The system symbolizer must produce some result.
        assert result
        return result

    def get_symbolized_lines(self, symbolized_lines, inc_frame_counter=True):
        if not symbolized_lines:
            if inc_frame_counter:
                self.frame_no += 1
            return [self.current_line]
        else:
            assert inc_frame_counter
            result = []
            for symbolized_frame in symbolized_lines:
                result.append(
                    "    #%s %s" % (str(self.frame_no), symbolized_frame.rstrip())
                )
                self.frame_no += 1
            return result

    def process_logfile(self):
        self.frame_no = 0
        for line in logfile:
            processed = self.process_line(line)
            print("\n".join(processed))

    def process_line_echo(self, line):
        return [line.rstrip()]

    def process_line_posix(self, line):
        self.current_line = line.rstrip()
        # Unsymbolicated:
        # #0 0x7f6e35cf2e45  (/blah/foo.so+0x11fe45)
        # Partially symbolicated:
        # #0 0x7f6e35cf2e45 in foo (foo.so+0x11fe45)
        # NOTE: We have to very liberal with symbol
        # names in the regex because it could be an
        # Objective-C or C++ demangled name.
        stack_trace_line_format = (
            "^( *#([0-9]+) *)(0x[0-9a-f]+) *(?:in *.+)? *\((.*)\+(0x[0-9a-f]+)\)"
        )
        match = re.match(stack_trace_line_format, line)
        if not match:
            logging.debug('Line "{}" does not match regex'.format(line))
            # Not a frame line so don't increment the frame counter.
            return self.get_symbolized_lines(None, inc_frame_counter=False)
        logging.debug(line)
        _, frameno_str, addr, binary, offset = match.groups()

        if not self.using_module_map and not os.path.isabs(binary):
            # Do not try to symbolicate if the binary is just the module file name
            # and a module map is unavailable.
            # FIXME(dliew): This is currently necessary for reports on Darwin that are
            # partially symbolicated by `atos`.
            return self.get_symbolized_lines(None)
        arch = ""
        # Arch can be embedded in the filename, e.g.: "libabc.dylib:x86_64h"
        colon_pos = binary.rfind(":")
        if colon_pos != -1:
            maybe_arch = binary[colon_pos + 1 :]
            if is_valid_arch(maybe_arch):
                arch = maybe_arch
                binary = binary[0:colon_pos]
        if arch == "":
            arch = guess_arch(addr)
        if frameno_str == "0":
            # Assume that frame #0 is the first frame of new stack trace.
            self.frame_no = 0
        original_binary = binary
        binary = self.plugin_proxy.filter_binary_path(binary)
        if binary is None:
            # The binary filter has told us this binary can't be symbolized.
            logging.debug('Skipping symbolication of binary "%s"', original_binary)
            return self.get_symbolized_lines(None)
        symbolized_line = self.symbolize_address(addr, binary, offset, arch)
        if not symbolized_line:
            if original_binary != binary:
                symbolized_line = self.symbolize_address(
                    addr, original_binary, offset, arch
                )
        return self.get_symbolized_lines(symbolized_line)


class AsanSymbolizerPlugInProxy(object):
    """
    Serves several purposes:
    - Manages the lifetime of plugins (must be used a `with` statement).
    - Provides interface for calling into plugins from within this script.
    """

    def __init__(self):
        self._plugins = []
        self._plugin_names = set()

    def _load_plugin_from_file_impl_py_gt_2(self, file_path, globals_space):
        with open(file_path, "r") as f:
            exec(f.read(), globals_space, None)

    def load_plugin_from_file(self, file_path):
        logging.info('Loading plugins from "{}"'.format(file_path))
        globals_space = dict(globals())
        # Provide function to register plugins
        def register_plugin(plugin):
            logging.info("Registering plugin %s", plugin.get_name())
            self.add_plugin(plugin)

        globals_space["register_plugin"] = register_plugin
        if sys.version_info.major < 3:
            execfile(file_path, globals_space, None)
        else:
            # Indirection here is to avoid a bug in older Python 2 versions:
            # `SyntaxError: unqualified exec is not allowed in function ...`
            self._load_plugin_from_file_impl_py_gt_2(file_path, globals_space)

    def add_plugin(self, plugin):
        assert isinstance(plugin, AsanSymbolizerPlugIn)
        self._plugins.append(plugin)
        self._plugin_names.add(plugin.get_name())
        plugin._receive_proxy(self)

    def remove_plugin(self, plugin):
        assert isinstance(plugin, AsanSymbolizerPlugIn)
        self._plugins.remove(plugin)
        self._plugin_names.remove(plugin.get_name())
        logging.debug("Removing plugin %s", plugin.get_name())
        plugin.destroy()

    def has_plugin(self, name):
        """
        Returns true iff the plugin name is currently
        being managed by AsanSymbolizerPlugInProxy.
        """
        return name in self._plugin_names

    def register_cmdline_args(self, parser):
        plugins = list(self._plugins)
        for plugin in plugins:
            plugin.register_cmdline_args(parser)

    def process_cmdline_args(self, pargs):
        # Use copy so we can remove items as we iterate.
        plugins = list(self._plugins)
        for plugin in plugins:
            keep = plugin.process_cmdline_args(pargs)
            assert isinstance(keep, bool)
            if not keep:
                self.remove_plugin(plugin)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        for plugin in self._plugins:
            plugin.destroy()
        # Don't suppress raised exceptions
        return False

    def _filter_single_value(self, function_name, input_value):
        """
        Helper for filter style plugin functions.
        """
        new_value = input_value
        for plugin in self._plugins:
            result = getattr(plugin, function_name)(new_value)
            if result is None:
                return None
            new_value = result
        return new_value

    def filter_binary_path(self, binary_path):
        """
        Consult available plugins to filter the path to a binary
        to make it suitable for symbolication.

        Returns `None` if symbolication should not be attempted for this
        binary.
        """
        return self._filter_single_value("filter_binary_path", binary_path)

    def filter_module_desc(self, module_desc):
        """
        Consult available plugins to determine the module
        description suitable for symbolication.

        Returns `None` if symbolication should not be attempted for this module.
        """
        assert isinstance(module_desc, ModuleDesc)
        return self._filter_single_value("filter_module_desc", module_desc)


class AsanSymbolizerPlugIn(object):
    """
    This is the interface the `asan_symbolize.py` code uses to talk
    to plugins.
    """

    @classmethod
    def get_name(cls):
        """
        Returns the name of the plugin.
        """
        return cls.__name__

    def _receive_proxy(self, proxy):
        assert isinstance(proxy, AsanSymbolizerPlugInProxy)
        self.proxy = proxy

    def register_cmdline_args(self, parser):
        """
        Hook for registering command line arguments to be
        consumed in `process_cmdline_args()`.

        `parser` - Instance of `argparse.ArgumentParser`.
        """
        pass

    def process_cmdline_args(self, pargs):
        """
        Hook for handling parsed arguments. Implementations
        should not modify `pargs`.

        `pargs` - Instance of `argparse.Namespace` containing
        parsed command line arguments.

        Return `True` if plug-in should be used, otherwise
        return `False`.
        """
        return True

    def destroy(self):
        """
        Hook called when a plugin is about to be destroyed.
        Implementations should free any allocated resources here.
        """
        pass

    # Symbolization hooks
    def filter_binary_path(self, binary_path):
        """
        Given a binary path return a binary path suitable for symbolication.

        Implementations should return `None` if symbolication of this binary
        should be skipped.
        """
        return binary_path

    def filter_module_desc(self, module_desc):
        """
        Given a ModuleDesc object (`module_desc`) return
        a ModuleDesc suitable for symbolication.

        Implementations should return `None` if symbolication of this binary
        should be skipped.
        """
        return module_desc


class ModuleDesc(object):
    def __init__(self, name, arch, start_addr, end_addr, module_path, uuid):
        self.name = name
        self.arch = arch
        self.start_addr = start_addr
        self.end_addr = end_addr
        # Module path from an ASan report.
        self.module_path = module_path
        # Module for performing symbolization, by default same as above.
        self.module_path_for_symbolization = module_path
        self.uuid = uuid
        assert self.is_valid()

    def __str__(self):
        assert self.is_valid()
        return "{name} {arch} {start_addr:#016x}-{end_addr:#016x} {module_path} {uuid}".format(
            name=self.name,
            arch=self.arch,
            start_addr=self.start_addr,
            end_addr=self.end_addr,
            module_path=self.module_path
            if self.module_path == self.module_path_for_symbolization
            else "{} ({})".format(self.module_path_for_symbolization, self.module_path),
            uuid=self.uuid,
        )

    def is_valid(self):
        if not isinstance(self.name, str):
            return False
        if not isinstance(self.arch, str):
            return False
        if not isinstance(self.start_addr, int):
            return False
        if self.start_addr < 0:
            return False
        if not isinstance(self.end_addr, int):
            return False
        if self.end_addr <= self.start_addr:
            return False
        if not isinstance(self.module_path, str):
            return False
        if not os.path.isabs(self.module_path):
            return False
        if not isinstance(self.module_path_for_symbolization, str):
            return False
        if not os.path.isabs(self.module_path_for_symbolization):
            return False
        if not isinstance(self.uuid, str):
            return False
        return True


class GetUUIDFromBinaryException(Exception):
    def __init__(self, msg):
        super(GetUUIDFromBinaryException, self).__init__(msg)


_get_uuid_from_binary_cache = dict()


def get_uuid_from_binary(path_to_binary, arch=None):
    cache_key = (path_to_binary, arch)
    cached_value = _get_uuid_from_binary_cache.get(cache_key)
    if cached_value:
        return cached_value
    if not os.path.exists(path_to_binary):
        raise GetUUIDFromBinaryException(
            'Binary "{}" does not exist'.format(path_to_binary)
        )
    cmd = ["/usr/bin/otool", "-l"]
    if arch:
        cmd.extend(["-arch", arch])
    cmd.append(path_to_binary)
    output = subprocess.check_output(cmd, stderr=subprocess.STDOUT)
    # Look for this output:
    # cmd LC_UUID
    # cmdsize 24
    # uuid 4CA778FE-5BF9-3C45-AE59-7DF01B2BE83F
    if isinstance(output, str):
        output_str = output
    else:
        assert isinstance(output, bytes)
        output_str = output.decode()
    assert isinstance(output_str, str)
    lines = output_str.split("\n")
    uuid = None
    for index, line in enumerate(lines):
        stripped_line = line.strip()
        if not stripped_line.startswith("cmd LC_UUID"):
            continue
        uuid_line = lines[index + 2].strip()
        if not uuid_line.startswith("uuid"):
            raise GetUUIDFromBinaryException('Malformed output: "{}"'.format(uuid_line))
        split_uuid_line = uuid_line.split()
        uuid = split_uuid_line[1]
        break
    if uuid is None:
        logging.error("Failed to retrieve UUID from binary {}".format(path_to_binary))
        logging.error("otool output was:\n{}".format(output_str))
        raise GetUUIDFromBinaryException(
            'Failed to retrieve UUID from binary "{}"'.format(path_to_binary)
        )
    else:
        # Update cache
        _get_uuid_from_binary_cache[cache_key] = uuid
    return uuid


class ModuleMap(object):
    def __init__(self):
        self._module_name_to_description_map = dict()

    def add_module(self, desc):
        assert isinstance(desc, ModuleDesc)
        assert desc.name not in self._module_name_to_description_map
        self._module_name_to_description_map[desc.name] = desc

    def find_module_by_name(self, name):
        return self._module_name_to_description_map.get(name, None)

    def __str__(self):
        s = "{} modules:\n".format(self.num_modules)
        for module_desc in sorted(
            self._module_name_to_description_map.values(), key=lambda v: v.start_addr
        ):
            s += str(module_desc) + "\n"
        return s

    @property
    def num_modules(self):
        return len(self._module_name_to_description_map)

    @property
    def modules(self):
        return set(self._module_name_to_description_map.values())

    def get_module_path_for_symbolication(self, module_name, proxy, validate_uuid):
        module_desc = self.find_module_by_name(module_name)
        if module_desc is None:
            return None
        # Allow a plug-in to change the module description to make it
        # suitable for symbolication or avoid symbolication altogether.
        module_desc = proxy.filter_module_desc(module_desc)
        if module_desc is None:
            return None
        if validate_uuid:
            logging.debug(
                "Validating UUID of {}".format(
                    module_desc.module_path_for_symbolization
                )
            )
            try:
                uuid = get_uuid_from_binary(
                    module_desc.module_path_for_symbolization, arch=module_desc.arch
                )
                if uuid != module_desc.uuid:
                    logging.warning(
                        "Detected UUID mismatch {} != {}".format(uuid, module_desc.uuid)
                    )
                    # UUIDs don't match. Tell client to not symbolize this.
                    return None
            except GetUUIDFromBinaryException as e:
                logging.error("Failed to get binary from UUID: %s", str(e))
                return None
        else:
            logging.warning(
                "Skipping validation of UUID of {}".format(
                    module_desc.module_path_for_symbolization
                )
            )
        return module_desc.module_path_for_symbolization

    @staticmethod
    def parse_from_file(module_map_path):
        if not os.path.exists(module_map_path):
            raise Exception('module map "{}" does not exist'.format(module_map_path))
        with open(module_map_path, "r") as f:
            mm = None
            # E.g.
            # 0x2db4000-0x102ddc000 /path/to (arm64) <0D6BBDE0-FF90-3680-899D-8E6F9528E04C>
            hex_regex = lambda name: r"0x(?P<" + name + r">[0-9a-f]+)"
            module_path_regex = r"(?P<path>.+)"
            arch_regex = r"\((?P<arch>.+)\)"
            uuid_regex = r"<(?P<uuid>[0-9A-Z-]+)>"
            line_regex = r"^{}-{}\s+{}\s+{}\s+{}".format(
                hex_regex("start_addr"),
                hex_regex("end_addr"),
                module_path_regex,
                arch_regex,
                uuid_regex,
            )
            matcher = re.compile(line_regex)
            line_num = 0
            line = "dummy"
            while line != "":
                line = f.readline()
                line_num += 1
                if mm is None:
                    if line.startswith("Process module map:"):
                        mm = ModuleMap()
                    continue
                if line.startswith("End of module map"):
                    break
                m_obj = matcher.match(line)
                if not m_obj:
                    raise Exception(
                        'Failed to parse line {} "{}"'.format(line_num, line)
                    )
                arch = m_obj.group("arch")
                start_addr = int(m_obj.group("start_addr"), base=16)
                end_addr = int(m_obj.group("end_addr"), base=16)
                module_path = m_obj.group("path")
                uuid = m_obj.group("uuid")
                module_desc = ModuleDesc(
                    name=os.path.basename(module_path),
                    arch=arch,
                    start_addr=start_addr,
                    end_addr=end_addr,
                    module_path=module_path,
                    uuid=uuid,
                )
                mm.add_module(module_desc)
            if mm is not None:
                logging.debug(
                    'Loaded Module map from "{}":\n{}'.format(f.name, str(mm))
                )
            return mm


class SysRootFilterPlugIn(AsanSymbolizerPlugIn):
    """
    Simple plug-in to add sys root prefix to all binary paths
    used for symbolication.
    """

    def __init__(self):
        self.sysroot_path = ""

    def register_cmdline_args(self, parser):
        parser.add_argument(
            "-s",
            dest="sys_root",
            metavar="SYSROOT",
            help="set path to sysroot for sanitized binaries",
        )

    def process_cmdline_args(self, pargs):
        if pargs.sys_root is None:
            # Not being used so remove ourselves.
            return False
        self.sysroot_path = pargs.sys_root
        return True

    def filter_binary_path(self, path):
        return self.sysroot_path + path


class ModuleMapPlugIn(AsanSymbolizerPlugIn):
    def __init__(self):
        self._module_map = None
        self._uuid_validation = True

    def register_cmdline_args(self, parser):
        parser.add_argument(
            "--module-map",
            help="Path to text file containing module map"
            "output. See print_module_map ASan option.",
        )
        parser.add_argument(
            "--skip-uuid-validation",
            default=False,
            action="store_true",
            help="Skips validating UUID of modules using otool.",
        )

    def process_cmdline_args(self, pargs):
        if not pargs.module_map:
            return False
        self._module_map = ModuleMap.parse_from_file(args.module_map)
        if self._module_map is None:
            msg = "Failed to find module map"
            logging.error(msg)
            raise Exception(msg)
        self._uuid_validation = not pargs.skip_uuid_validation
        return True

    def filter_binary_path(self, binary_path):
        if os.path.isabs(binary_path):
            # This is a binary path so transform into
            # a module name
            module_name = os.path.basename(binary_path)
        else:
            module_name = binary_path
        return self._module_map.get_module_path_for_symbolication(
            module_name, self.proxy, self._uuid_validation
        )


def add_logging_args(parser):
    parser.add_argument(
        "--log-dest",
        default=None,
        help="Destination path for script logging (default stderr).",
    )
    parser.add_argument(
        "--log-level",
        choices=["debug", "info", "warning", "error", "critical"],
        default="info",
        help="Log level for script (default: %(default)s).",
    )


def setup_logging():
    # Set up a parser just for parsing the logging arguments.
    # This is necessary because logging should be configured before we
    # perform the main argument parsing.
    parser = argparse.ArgumentParser(add_help=False)
    add_logging_args(parser)
    pargs, unparsed_args = parser.parse_known_args()

    log_level = getattr(logging, pargs.log_level.upper())
    if log_level == logging.DEBUG:
        log_format = (
            "%(levelname)s: [%(funcName)s() %(filename)s:%(lineno)d] %(message)s"
        )
    else:
        log_format = "%(levelname)s: %(message)s"
    basic_config = {"level": log_level, "format": log_format}
    log_dest = pargs.log_dest
    if log_dest:
        basic_config["filename"] = log_dest
    logging.basicConfig(**basic_config)
    logging.debug(
        'Logging level set to "{}" and directing output to "{}"'.format(
            pargs.log_level, "stderr" if log_dest is None else log_dest
        )
    )
    return unparsed_args


def add_load_plugin_args(parser):
    parser.add_argument("-p", "--plugins", help="Load plug-in", nargs="+", default=[])


def setup_plugins(plugin_proxy, args):
    parser = argparse.ArgumentParser(add_help=False)
    add_load_plugin_args(parser)
    pargs, unparsed_args = parser.parse_known_args()
    for plugin_path in pargs.plugins:
        plugin_proxy.load_plugin_from_file(plugin_path)
    # Add built-in plugins.
    plugin_proxy.add_plugin(ModuleMapPlugIn())
    plugin_proxy.add_plugin(SysRootFilterPlugIn())
    return unparsed_args


if __name__ == "__main__":
    remaining_args = setup_logging()
    with AsanSymbolizerPlugInProxy() as plugin_proxy:
        remaining_args = setup_plugins(plugin_proxy, remaining_args)
        parser = argparse.ArgumentParser(
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description="ASan symbolization script",
            epilog=__doc__,
        )
        parser.add_argument(
            "path_to_cut",
            nargs="*",
            help="pattern to be cut from the result file path ",
        )
        parser.add_argument(
            "-d", "--demangle", action="store_true", help="demangle function names"
        )
        parser.add_argument(
            "-c", metavar="CROSS_COMPILE", help="set prefix for binutils"
        )
        parser.add_argument(
            "-l",
            "--logfile",
            default=sys.stdin,
            type=argparse.FileType("r"),
            help="set log file name to parse, default is stdin",
        )
        parser.add_argument(
            "--force-system-symbolizer",
            action="store_true",
            help="don't use llvm-symbolizer",
        )
        # Add logging arguments so that `--help` shows them.
        add_logging_args(parser)
        # Add load plugin arguments so that `--help` shows them.
        add_load_plugin_args(parser)
        plugin_proxy.register_cmdline_args(parser)
        args = parser.parse_args(remaining_args)
        plugin_proxy.process_cmdline_args(args)
        if args.path_to_cut:
            fix_filename_patterns = args.path_to_cut
        if args.demangle:
            demangle = True
        if args.c:
            binutils_prefix = args.c
        if args.logfile:
            logfile = args.logfile
        else:
            logfile = sys.stdin
        if args.force_system_symbolizer:
            force_system_symbolizer = True
        if force_system_symbolizer:
            assert allow_system_symbolizer
        loop = SymbolizationLoop(plugin_proxy)
        loop.process_logfile()
