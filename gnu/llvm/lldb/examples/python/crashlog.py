#!/usr/bin/env python3

# ----------------------------------------------------------------------
# Be sure to add the python path that points to the LLDB shared library.
#
# To use this in the embedded python interpreter using "lldb":
#
#   cd /path/containing/crashlog.py
#   lldb
#   (lldb) script import crashlog
#   "crashlog" command installed, type "crashlog --help" for detailed help
#   (lldb) crashlog ~/Library/Logs/DiagnosticReports/a.crash
#
# The benefit of running the crashlog command inside lldb in the
# embedded python interpreter is when the command completes, there
# will be a target with all of the files loaded at the locations
# described in the crash log. Only the files that have stack frames
# in the backtrace will be loaded unless the "--load-all" option
# has been specified. This allows users to explore the program in the
# state it was in right at crash time.
#
# On MacOSX csh, tcsh:
#   ( setenv PYTHONPATH /path/to/LLDB.framework/Resources/Python ; ./crashlog.py ~/Library/Logs/DiagnosticReports/a.crash )
#
# On MacOSX sh, bash:
#   PYTHONPATH=/path/to/LLDB.framework/Resources/Python ./crashlog.py ~/Library/Logs/DiagnosticReports/a.crash
# ----------------------------------------------------------------------

import abc
import argparse
import concurrent.futures
import contextlib
import datetime
import json
import os
import platform
import plistlib
import re
import shlex
import string
import subprocess
import sys
import tempfile
import threading
import time
import uuid


print_lock = threading.RLock()

try:
    # First try for LLDB in case PYTHONPATH is already correctly setup.
    import lldb
except ImportError:
    # Ask the command line driver for the path to the lldb module. Copy over
    # the environment so that SDKROOT is propagated to xcrun.
    command = (
        ["xcrun", "lldb", "-P"] if platform.system() == "Darwin" else ["lldb", "-P"]
    )
    # Extend the PYTHONPATH if the path exists and isn't already there.
    lldb_python_path = subprocess.check_output(command).decode("utf-8").strip()
    if os.path.exists(lldb_python_path) and not sys.path.__contains__(lldb_python_path):
        sys.path.append(lldb_python_path)
    # Try importing LLDB again.
    try:
        import lldb
    except ImportError:
        print(
            "error: couldn't locate the 'lldb' module, please set PYTHONPATH correctly"
        )
        sys.exit(1)

from lldb.utils import symbolication
from lldb.plugins.scripted_process import INTEL64_GPR, ARM64_GPR


def read_plist(s):
    if sys.version_info.major == 3:
        return plistlib.loads(s)
    else:
        return plistlib.readPlistFromString(s)


class CrashLog(symbolication.Symbolicator):
    class Thread:
        """Class that represents a thread in a darwin crash log"""

        def __init__(self, index, app_specific_backtrace, arch):
            self.index = index
            self.id = index
            self.images = list()
            self.frames = list()
            self.idents = list()
            self.registers = dict()
            self.reason = None
            self.name = None
            self.queue = None
            self.crashed = False
            self.app_specific_backtrace = app_specific_backtrace
            self.arch = arch

        def dump_registers(self, prefix=""):
            registers_info = None
            sorted_registers = {}

            def sort_dict(d):
                sorted_keys = list(d.keys())
                sorted_keys.sort()
                return {k: d[k] for k in sorted_keys}

            if self.arch:
                if "x86_64" == self.arch:
                    registers_info = INTEL64_GPR
                elif "arm64" in self.arch:
                    registers_info = ARM64_GPR
                else:
                    print("unknown target architecture: %s" % self.arch)
                    return

                # Add registers available in the register information dictionary.
                for reg_info in registers_info:
                    reg_name = None
                    if reg_info["name"] in self.registers:
                        reg_name = reg_info["name"]
                    elif (
                        "generic" in reg_info and reg_info["generic"] in self.registers
                    ):
                        reg_name = reg_info["generic"]
                    else:
                        # Skip register that are present in the register information dictionary but not present in the report.
                        continue

                    reg_val = self.registers[reg_name]
                    sorted_registers[reg_name] = reg_val

                unknown_parsed_registers = {}
                for reg_name in self.registers:
                    if reg_name not in sorted_registers:
                        unknown_parsed_registers[reg_name] = self.registers[reg_name]

                sorted_registers.update(sort_dict(unknown_parsed_registers))

            else:
                sorted_registers = sort_dict(self.registers)

            for reg_name, reg_val in sorted_registers.items():
                print("%s    %-8s = %#16.16x" % (prefix, reg_name, reg_val))

        def dump(self, prefix=""):
            if self.app_specific_backtrace:
                print(
                    "%Application Specific Backtrace[%u] %s"
                    % (prefix, self.index, self.reason)
                )
            else:
                print("%sThread[%u] %s" % (prefix, self.index, self.reason))
            if self.frames:
                print("%s  Frames:" % (prefix))
                for frame in self.frames:
                    frame.dump(prefix + "    ")
            if self.registers:
                print("%s  Registers:" % (prefix))
                self.dump_registers(prefix)

        def dump_symbolicated(self, crash_log, options):
            this_thread_crashed = self.app_specific_backtrace
            if not this_thread_crashed:
                this_thread_crashed = self.did_crash()
                if options.crashed_only and not this_thread_crashed:
                    return

            print("%s" % self)
            display_frame_idx = -1
            for frame_idx, frame in enumerate(self.frames):
                disassemble = (
                    this_thread_crashed or options.disassemble_all_threads
                ) and frame_idx < options.disassemble_depth

                # Except for the zeroth frame, we should subtract 1 from every
                # frame pc to get the previous line entry.
                pc = frame.pc & crash_log.addr_mask
                pc = pc if frame_idx == 0 or pc == 0 else pc - 1
                symbolicated_frame_addresses = crash_log.symbolicate(
                    pc, options.verbose
                )

                if symbolicated_frame_addresses:
                    symbolicated_frame_address_idx = 0
                    for symbolicated_frame_address in symbolicated_frame_addresses:
                        display_frame_idx += 1
                        print("[%3u] %s" % (frame_idx, symbolicated_frame_address))
                        if (
                            (options.source_all or self.did_crash())
                            and display_frame_idx < options.source_frames
                            and options.source_context
                        ):
                            source_context = options.source_context
                            line_entry = (
                                symbolicated_frame_address.get_symbol_context().line_entry
                            )
                            if line_entry.IsValid():
                                strm = lldb.SBStream()
                                if line_entry:
                                    crash_log.debugger.GetSourceManager().DisplaySourceLinesWithLineNumbers(
                                        line_entry.file,
                                        line_entry.line,
                                        source_context,
                                        source_context,
                                        "->",
                                        strm,
                                    )
                                source_text = strm.GetData()
                                if source_text:
                                    # Indent the source a bit
                                    indent_str = "    "
                                    join_str = "\n" + indent_str
                                    print(
                                        "%s%s"
                                        % (
                                            indent_str,
                                            join_str.join(source_text.split("\n")),
                                        )
                                    )
                        if symbolicated_frame_address_idx == 0:
                            if disassemble:
                                instructions = (
                                    symbolicated_frame_address.get_instructions()
                                )
                                if instructions:
                                    print()
                                    symbolication.disassemble_instructions(
                                        crash_log.get_target(),
                                        instructions,
                                        frame.pc,
                                        options.disassemble_before,
                                        options.disassemble_after,
                                        frame.index > 0,
                                    )
                                    print()
                        symbolicated_frame_address_idx += 1
                else:
                    print(frame)
            if self.registers:
                print()
                self.dump_registers()
            elif self.crashed:
                print()
                print("No thread state (register information) available")

        def add_ident(self, ident):
            if ident not in self.idents:
                self.idents.append(ident)

        def did_crash(self):
            return self.crashed

        def __str__(self):
            if self.app_specific_backtrace:
                s = "Application Specific Backtrace[%u]" % self.index
            else:
                s = "Thread[%u]" % self.index
            if self.reason:
                s += " %s" % self.reason
            return s

    class Frame:
        """Class that represents a stack frame in a thread in a darwin crash log"""

        def __init__(self, index, pc, description):
            self.pc = pc
            self.description = description
            self.index = index

        def __str__(self):
            if self.description:
                return "[%3u] 0x%16.16x %s" % (self.index, self.pc, self.description)
            else:
                return "[%3u] 0x%16.16x" % (self.index, self.pc)

        def dump(self, prefix):
            print("%s%s" % (prefix, str(self)))

    class DarwinImage(symbolication.Image):
        """Class that represents a binary images in a darwin crash log"""

        dsymForUUIDBinary = "/usr/local/bin/dsymForUUID"
        if "LLDB_APPLE_DSYMFORUUID_EXECUTABLE" in os.environ:
            dsymForUUIDBinary = os.environ["LLDB_APPLE_DSYMFORUUID_EXECUTABLE"]
        elif not os.path.exists(dsymForUUIDBinary):
            try:
                dsymForUUIDBinary = (
                    subprocess.check_output("which dsymForUUID", shell=True)
                    .decode("utf-8")
                    .rstrip("\n")
                )
            except:
                dsymForUUIDBinary = ""

        dwarfdump_uuid_regex = re.compile("UUID: ([-0-9a-fA-F]+) \(([^\(]+)\) .*")

        def __init__(
            self, text_addr_lo, text_addr_hi, identifier, version, uuid, path, verbose
        ):
            symbolication.Image.__init__(self, path, uuid)
            self.add_section(
                symbolication.Section(text_addr_lo, text_addr_hi, "__TEXT")
            )
            self.identifier = identifier
            self.version = version
            self.verbose = verbose

        def show_symbol_progress(self):
            """
            Hide progress output and errors from system frameworks as they are plentiful.
            """
            if self.verbose:
                return True
            return not (
                self.path.startswith("/System/Library/")
                or self.path.startswith("/usr/lib/")
            )

        def find_matching_slice(self):
            dwarfdump_cmd_output = subprocess.check_output(
                'dwarfdump --uuid "%s"' % self.path, shell=True
            ).decode("utf-8")
            self_uuid = self.get_uuid()
            for line in dwarfdump_cmd_output.splitlines():
                match = self.dwarfdump_uuid_regex.search(line)
                if match:
                    dwarf_uuid_str = match.group(1)
                    dwarf_uuid = uuid.UUID(dwarf_uuid_str)
                    if self_uuid == dwarf_uuid:
                        self.resolved_path = self.path
                        self.arch = match.group(2)
                        return True
            if not self.resolved_path:
                self.unavailable = True
                if self.show_symbol_progress():
                    print(
                        (
                            "error\n    error: unable to locate '%s' with UUID %s"
                            % (self.path, self.get_normalized_uuid_string())
                        )
                    )
                return False

        def locate_module_and_debug_symbols(self):
            # Don't load a module twice...
            if self.resolved:
                return True
            # Mark this as resolved so we don't keep trying
            self.resolved = True
            uuid_str = self.get_normalized_uuid_string()
            if self.show_symbol_progress():
                with print_lock:
                    print("Getting symbols for %s %s..." % (uuid_str, self.path))
            # Keep track of unresolved source paths.
            unavailable_source_paths = set()
            if os.path.exists(self.dsymForUUIDBinary):
                dsym_for_uuid_command = (
                    "{} --copyExecutable --ignoreNegativeCache {}".format(
                        self.dsymForUUIDBinary, uuid_str
                    )
                )
                s = subprocess.check_output(dsym_for_uuid_command, shell=True)
                if s:
                    try:
                        plist_root = read_plist(s)
                    except:
                        with print_lock:
                            print(
                                (
                                    "Got exception: ",
                                    sys.exc_info()[1],
                                    " handling dsymForUUID output: \n",
                                    s,
                                )
                            )
                        raise
                    if plist_root:
                        plist = plist_root[uuid_str]
                        if plist:
                            if "DBGArchitecture" in plist:
                                self.arch = plist["DBGArchitecture"]
                            if "DBGDSYMPath" in plist:
                                self.symfile = os.path.realpath(plist["DBGDSYMPath"])
                            if "DBGSymbolRichExecutable" in plist:
                                self.path = os.path.expanduser(
                                    plist["DBGSymbolRichExecutable"]
                                )
                                self.resolved_path = self.path
                            if "DBGSourcePathRemapping" in plist:
                                path_remapping = plist["DBGSourcePathRemapping"]
                                for _, value in path_remapping.items():
                                    source_path = os.path.expanduser(value)
                                    if not os.path.exists(source_path):
                                        unavailable_source_paths.add(source_path)
            if not self.resolved_path and os.path.exists(self.path):
                if not self.find_matching_slice():
                    return False
            if not self.resolved_path and not os.path.exists(self.path):
                try:
                    mdfind_results = (
                        subprocess.check_output(
                            [
                                "/usr/bin/mdfind",
                                "com_apple_xcode_dsym_uuids == %s" % uuid_str,
                            ]
                        )
                        .decode("utf-8")
                        .splitlines()
                    )
                    found_matching_slice = False
                    for dsym in mdfind_results:
                        dwarf_dir = os.path.join(dsym, "Contents/Resources/DWARF")
                        if not os.path.exists(dwarf_dir):
                            # Not a dSYM bundle, probably an Xcode archive.
                            continue
                        with print_lock:
                            print('falling back to binary inside "%s"' % dsym)
                        self.symfile = dsym
                        # Look for the executable next to the dSYM bundle.
                        parent_dir = os.path.dirname(dsym)
                        executables = []
                        for root, _, files in os.walk(parent_dir):
                            for file in files:
                                abs_path = os.path.join(root, file)
                                if os.path.isfile(abs_path) and os.access(
                                    abs_path, os.X_OK
                                ):
                                    executables.append(abs_path)
                        for binary in executables:
                            basename = os.path.basename(binary)
                            if basename == self.identifier:
                                self.path = binary
                                found_matching_slice = True
                                break
                        if found_matching_slice:
                            break
                except:
                    pass
            if (self.resolved_path and os.path.exists(self.resolved_path)) or (
                self.path and os.path.exists(self.path)
            ):
                with print_lock:
                    print("Resolved symbols for %s %s..." % (uuid_str, self.path))
                    if len(unavailable_source_paths):
                        for source_path in unavailable_source_paths:
                            print(
                                "Could not access remapped source path for %s %s"
                                % (uuid_str, source_path)
                            )
                return True
            else:
                self.unavailable = True
            return False

    def __init__(self, debugger, path, verbose):
        """CrashLog constructor that take a path to a darwin crash log file"""
        symbolication.Symbolicator.__init__(self, debugger)
        self.path = os.path.expanduser(path)
        self.info_lines = list()
        self.system_profile = list()
        self.threads = list()
        self.backtraces = list()  # For application specific backtraces
        self.idents = (
            list()
        )  # A list of the required identifiers for doing all stack backtraces
        self.errors = list()
        self.exception = dict()
        self.crashed_thread_idx = -1
        self.version = -1
        self.target = None
        self.verbose = verbose
        self.process_id = None
        self.process_identifier = None
        self.process_path = None
        self.process_arch = None

    def dump(self):
        print("Crash Log File: %s" % (self.path))
        if self.backtraces:
            print("\nApplication Specific Backtraces:")
            for thread in self.backtraces:
                thread.dump("  ")
        print("\nThreads:")
        for thread in self.threads:
            thread.dump("  ")
        print("\nImages:")
        for image in self.images:
            image.dump("  ")

    def set_main_image(self, identifier):
        for i, image in enumerate(self.images):
            if image.identifier == identifier:
                self.images.insert(0, self.images.pop(i))
                break

    def find_image_with_identifier(self, identifier):
        for image in self.images:
            if image.identifier == identifier:
                return image
        regex_text = "^.*\.%s$" % (re.escape(identifier))
        regex = re.compile(regex_text)
        for image in self.images:
            if regex.match(image.identifier):
                return image
        return None

    def create_target(self):
        if self.target is None:
            self.target = symbolication.Symbolicator.create_target(self)
            if self.target:
                return self.target
            # We weren't able to open the main executable as, but we can still
            # symbolicate
            print("crashlog.create_target()...2")
            if self.idents:
                for ident in self.idents:
                    image = self.find_image_with_identifier(ident)
                    if image:
                        self.target = image.create_target(self.debugger)
                        if self.target:
                            return self.target  # success
            print("crashlog.create_target()...3")
            for image in self.images:
                self.target = image.create_target(self.debugger)
                if self.target:
                    return self.target  # success
            print("crashlog.create_target()...4")
            print("error: Unable to locate any executables from the crash log.")
            print("       Try loading the executable into lldb before running crashlog")
            print(
                "       and/or make sure the .dSYM bundles can be found by Spotlight."
            )
        return self.target

    def get_target(self):
        return self.target

    def load_images(self, options, loaded_images=None):
        if not loaded_images:
            loaded_images = []
        images_to_load = self.images
        if options.load_all_images:
            for image in self.images:
                image.resolve = True
        elif options.crashed_only:
            images_to_load = []
            for thread in self.threads:
                if thread.did_crash() or thread.app_specific_backtrace:
                    for ident in thread.idents:
                        for image in self.find_images_with_identifier(ident):
                            image.resolve = True
                            images_to_load.append(image)

        futures = []
        with tempfile.TemporaryDirectory() as obj_dir:

            def add_module(image, target, obj_dir):
                return image, image.add_module(target, obj_dir)

            max_worker = None
            if options.no_parallel_image_loading:
                max_worker = 1

            with concurrent.futures.ThreadPoolExecutor(max_worker) as executor:
                for image in images_to_load:
                    if image not in loaded_images:
                        if image.uuid == uuid.UUID(int=0):
                            continue
                        futures.append(
                            executor.submit(
                                add_module,
                                image=image,
                                target=self.target,
                                obj_dir=obj_dir,
                            )
                        )

                for future in concurrent.futures.as_completed(futures):
                    image, err = future.result()
                    if err:
                        print(err)
                    else:
                        loaded_images.append(image)


class CrashLogFormatException(Exception):
    pass


class CrashLogParseException(Exception):
    pass


class InteractiveCrashLogException(Exception):
    pass


class CrashLogParser:
    @staticmethod
    def create(debugger, path, options):
        data = JSONCrashLogParser.is_valid_json(path)
        if data:
            parser = JSONCrashLogParser(debugger, path, options)
            parser.data = data
            return parser
        else:
            return TextCrashLogParser(debugger, path, options)

    def __init__(self, debugger, path, options):
        self.path = os.path.expanduser(path)
        self.options = options
        self.crashlog = CrashLog(debugger, self.path, self.options.verbose)

    @abc.abstractmethod
    def parse(self):
        pass


class JSONCrashLogParser(CrashLogParser):
    @staticmethod
    def is_valid_json(path):
        def parse_json(buffer):
            try:
                return json.loads(buffer)
            except:
                # The first line can contain meta data. Try stripping it and
                # try again.
                head, _, tail = buffer.partition("\n")
                return json.loads(tail)

        with open(path, "r", encoding="utf-8") as f:
            buffer = f.read()
        try:
            return parse_json(buffer)
        except:
            return None

    def __init__(self, debugger, path, options):
        super().__init__(debugger, path, options)

    def parse(self):
        try:
            self.parse_process_info(self.data)
            self.parse_images(self.data["usedImages"])
            self.parse_main_image(self.data)
            self.parse_threads(self.data["threads"])
            if "asi" in self.data:
                self.crashlog.asi = self.data["asi"]
            # FIXME: With the current design, we can either show the ASI or Last
            # Exception Backtrace, not both. Is there a situation where we would
            # like to show both ?
            if "asiBacktraces" in self.data:
                self.parse_app_specific_backtraces(self.data["asiBacktraces"])
            if "lastExceptionBacktrace" in self.data:
                self.parse_last_exception_backtraces(
                    self.data["lastExceptionBacktrace"]
                )
            self.parse_errors(self.data)
            thread = self.crashlog.threads[self.crashlog.crashed_thread_idx]
            reason = self.parse_crash_reason(self.data["exception"])
            if thread.reason:
                thread.reason = "{} {}".format(thread.reason, reason)
            else:
                thread.reason = reason
        except (KeyError, ValueError, TypeError) as e:
            raise CrashLogParseException(
                "Failed to parse JSON crashlog: {}: {}".format(type(e).__name__, e)
            )

        return self.crashlog

    def get_used_image(self, idx):
        return self.data["usedImages"][idx]

    def parse_process_info(self, json_data):
        self.crashlog.process_id = json_data["pid"]
        self.crashlog.process_identifier = json_data["procName"]
        if "procPath" in json_data:
            self.crashlog.process_path = json_data["procPath"]

    def parse_crash_reason(self, json_exception):
        self.crashlog.exception = json_exception
        exception_type = json_exception["type"]
        exception_signal = " "
        if "signal" in json_exception:
            exception_signal += "({})".format(json_exception["signal"])

        if "codes" in json_exception:
            exception_extra = " ({})".format(json_exception["codes"])
        elif "subtype" in json_exception:
            exception_extra = " ({})".format(json_exception["subtype"])
        else:
            exception_extra = ""
        return "{}{}{}".format(exception_type, exception_signal, exception_extra)

    def parse_images(self, json_images):
        for json_image in json_images:
            img_uuid = uuid.UUID(json_image["uuid"])
            low = int(json_image["base"])
            high = low + int(json_image["size"]) if "size" in json_image else low
            name = json_image["name"] if "name" in json_image else ""
            path = json_image["path"] if "path" in json_image else ""
            version = ""
            darwin_image = self.crashlog.DarwinImage(
                low, high, name, version, img_uuid, path, self.options.verbose
            )
            if "arch" in json_image:
                darwin_image.arch = json_image["arch"]
                if path == self.crashlog.process_path:
                    self.crashlog.process_arch = darwin_image.arch
            self.crashlog.images.append(darwin_image)

    def parse_main_image(self, json_data):
        if "procName" in json_data:
            proc_name = json_data["procName"]
            self.crashlog.set_main_image(proc_name)

    def parse_frames(self, thread, json_frames):
        idx = 0
        for json_frame in json_frames:
            image_id = int(json_frame["imageIndex"])
            json_image = self.get_used_image(image_id)
            ident = json_image["name"] if "name" in json_image else ""
            thread.add_ident(ident)
            if ident not in self.crashlog.idents:
                self.crashlog.idents.append(ident)

            frame_offset = int(json_frame["imageOffset"])
            image_addr = self.get_used_image(image_id)["base"]
            pc = image_addr + frame_offset

            if "symbol" in json_frame:
                symbol = json_frame["symbol"]
                location = 0
                if "symbolLocation" in json_frame and json_frame["symbolLocation"]:
                    location = int(json_frame["symbolLocation"])
                image = self.crashlog.images[image_id]
                image.symbols[symbol] = {
                    "name": symbol,
                    "type": "code",
                    "address": frame_offset - location,
                }

            thread.frames.append(self.crashlog.Frame(idx, pc, frame_offset))

            # on arm64 systems, if it jump through a null function pointer,
            # we end up at address 0 and the crash reporter unwinder
            # misses the frame that actually faulted.
            # But $lr can tell us where the last BL/BLR instruction used
            # was at, so insert that address as the caller stack frame.
            if idx == 0 and pc == 0 and "lr" in thread.registers:
                pc = thread.registers["lr"]
                for image in self.data["usedImages"]:
                    text_lo = image["base"]
                    text_hi = text_lo + image["size"]
                    if text_lo <= pc < text_hi:
                        idx += 1
                        frame_offset = pc - text_lo
                        thread.frames.append(self.crashlog.Frame(idx, pc, frame_offset))
                        break

            idx += 1

    def parse_threads(self, json_threads):
        idx = 0
        for json_thread in json_threads:
            thread = self.crashlog.Thread(idx, False, self.crashlog.process_arch)
            if "name" in json_thread:
                thread.name = json_thread["name"]
                thread.reason = json_thread["name"]
            if "id" in json_thread:
                thread.id = int(json_thread["id"])
            if json_thread.get("triggered", False):
                self.crashlog.crashed_thread_idx = idx
                thread.crashed = True
                if "threadState" in json_thread:
                    thread.registers = self.parse_thread_registers(
                        json_thread["threadState"]
                    )
            if "queue" in json_thread:
                thread.queue = json_thread.get("queue")
            self.parse_frames(thread, json_thread.get("frames", []))
            self.crashlog.threads.append(thread)
            idx += 1

    def parse_asi_backtrace(self, thread, bt):
        for line in bt.split("\n"):
            frame_match = TextCrashLogParser.frame_regex.search(line)
            if not frame_match:
                print("error: can't parse application specific backtrace.")
                return False

            frame_id = (
                frame_img_name
            ) = (
                frame_addr
            ) = (
                frame_symbol
            ) = frame_offset = frame_file = frame_line = frame_column = None

            if len(frame_match.groups()) == 3:
                # Get the image UUID from the frame image name.
                (frame_id, frame_img_name, frame_addr) = frame_match.groups()
            elif len(frame_match.groups()) == 5:
                (
                    frame_id,
                    frame_img_name,
                    frame_addr,
                    frame_symbol,
                    frame_offset,
                ) = frame_match.groups()
            elif len(frame_match.groups()) == 7:
                (
                    frame_id,
                    frame_img_name,
                    frame_addr,
                    frame_symbol,
                    frame_offset,
                    frame_file,
                    frame_line,
                ) = frame_match.groups()
            elif len(frame_match.groups()) == 8:
                (
                    frame_id,
                    frame_img_name,
                    frame_addr,
                    frame_symbol,
                    frame_offset,
                    frame_file,
                    frame_line,
                    frame_column,
                ) = frame_match.groups()

            thread.add_ident(frame_img_name)
            if frame_img_name not in self.crashlog.idents:
                self.crashlog.idents.append(frame_img_name)

            description = ""
            if frame_img_name and frame_addr and frame_symbol:
                description = frame_symbol
                frame_offset_value = 0
                if frame_offset:
                    description += " + " + frame_offset
                    frame_offset_value = int(frame_offset, 0)
                for image in self.crashlog.images:
                    if image.identifier == frame_img_name:
                        image.symbols[frame_symbol] = {
                            "name": frame_symbol,
                            "type": "code",
                            "address": int(frame_addr, 0) - frame_offset_value,
                        }

            thread.frames.append(
                self.crashlog.Frame(int(frame_id), int(frame_addr, 0), description)
            )

        return True

    def parse_app_specific_backtraces(self, json_app_specific_bts):
        thread = self.crashlog.Thread(
            len(self.crashlog.threads), True, self.crashlog.process_arch
        )
        thread.name = "Application Specific Backtrace"
        if self.parse_asi_backtrace(thread, json_app_specific_bts[0]):
            self.crashlog.threads.append(thread)
        else:
            print("error: Couldn't parse Application Specific Backtrace.")

    def parse_last_exception_backtraces(self, json_last_exc_bts):
        thread = self.crashlog.Thread(
            len(self.crashlog.threads), True, self.crashlog.process_arch
        )
        thread.name = "Last Exception Backtrace"
        self.parse_frames(thread, json_last_exc_bts)
        self.crashlog.threads.append(thread)

    def parse_thread_registers(self, json_thread_state, prefix=None):
        registers = dict()
        for key, state in json_thread_state.items():
            if key == "rosetta":
                registers.update(self.parse_thread_registers(state))
                continue
            if key == "x":
                gpr_dict = {str(idx): reg for idx, reg in enumerate(state)}
                registers.update(self.parse_thread_registers(gpr_dict, key))
                continue
            if key == "flavor":
                if not self.crashlog.process_arch:
                    if state == "ARM_THREAD_STATE64":
                        self.crashlog.process_arch = "arm64"
                    elif state == "X86_THREAD_STATE":
                        self.crashlog.process_arch = "x86_64"
                continue
            try:
                value = int(state["value"])
                registers["{}{}".format(prefix or "", key)] = value
            except (KeyError, ValueError, TypeError):
                pass
        return registers

    def parse_errors(self, json_data):
        if "reportNotes" in json_data:
            self.crashlog.errors = json_data["reportNotes"]


class TextCrashLogParser(CrashLogParser):
    parent_process_regex = re.compile(r"^Parent Process:\s*(.*)\[(\d+)\]")
    thread_state_regex = re.compile(r"^Thread (\d+ crashed with|State)")
    thread_instrs_regex = re.compile(r"^Thread \d+ instruction stream")
    thread_regex = re.compile(r"^Thread (\d+).*")
    app_backtrace_regex = re.compile(r"^Application Specific Backtrace (\d+).*")

    class VersionRegex:
        version = r"\(.+\)|(?:arm|x86_)[0-9a-z]+"

    class FrameRegex(VersionRegex):
        @classmethod
        def get(cls):
            index = r"^(\d+)\s+"
            img_name = r"(.+?)\s+"
            version = r"(?:" + super().version + r"\s+)?"
            address = r"(0x[0-9a-fA-F]{4,})"  # 4 digits or more

            symbol = """
                        (?:
                            [ ]+
                            (?P<symbol>.+)
                            (?:
                                [ ]\+[ ]
                                (?P<symbol_offset>\d+)
                            )
                            (?:
                                [ ]\(
                                (?P<file_name>[^:]+):(?P<line_number>\d+)
                                (?:
                                    :(?P<column_num>\d+)
                                )?
                            )?
                        )?
                       """

            return re.compile(
                index + img_name + version + address + symbol, flags=re.VERBOSE
            )

    frame_regex = FrameRegex.get()
    null_frame_regex = re.compile(r"^\d+\s+\?\?\?\s+0{4,} +")
    image_regex_uuid = re.compile(
        r"(0x[0-9a-fA-F]+)"  # img_lo
        r"\s+-\s+"  #   -
        r"(0x[0-9a-fA-F]+)\s+"  # img_hi
        r"[+]?(.+?)\s+"  # img_name
        r"(?:(" + VersionRegex.version + r")\s+)?"  # img_version
        r"(?:<([-0-9a-fA-F]+)>\s+)?"  # img_uuid
        r"(\?+|/.*)"  # img_path
    )
    exception_type_regex = re.compile(
        r"^Exception Type:\s+(EXC_[A-Z_]+)(?:\s+\((.*)\))?"
    )
    exception_codes_regex = re.compile(
        r"^Exception Codes:\s+(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+)"
    )
    exception_extra_regex = re.compile(r"^Exception\s+.*:\s+(.*)")

    class CrashLogParseMode:
        NORMAL = 0
        THREAD = 1
        IMAGES = 2
        THREGS = 3
        SYSTEM = 4
        INSTRS = 5

    def __init__(self, debugger, path, options):
        super().__init__(debugger, path, options)
        self.thread = None
        self.app_specific_backtrace = False
        self.parse_mode = self.CrashLogParseMode.NORMAL
        self.parsers = {
            self.CrashLogParseMode.NORMAL: self.parse_normal,
            self.CrashLogParseMode.THREAD: self.parse_thread,
            self.CrashLogParseMode.IMAGES: self.parse_images,
            self.CrashLogParseMode.THREGS: self.parse_thread_registers,
            self.CrashLogParseMode.SYSTEM: self.parse_system,
            self.CrashLogParseMode.INSTRS: self.parse_instructions,
        }
        self.symbols = {}

    def parse(self):
        with open(self.path, "r", encoding="utf-8") as f:
            lines = f.read().splitlines()

        idx = 0
        lines_count = len(lines)
        while True:
            if idx >= lines_count:
                break

            line = lines[idx]
            line_len = len(line)

            if line_len == 0:
                if self.thread:
                    if self.parse_mode == self.CrashLogParseMode.THREAD:
                        if self.thread.index == self.crashlog.crashed_thread_idx:
                            self.thread.reason = ""
                            if hasattr(self.crashlog, "thread_exception"):
                                self.thread.reason += self.crashlog.thread_exception
                            if hasattr(self.crashlog, "thread_exception_data"):
                                self.thread.reason += (
                                    " (%s)" % self.crashlog.thread_exception_data
                                )
                            self.thread.crashed = True
                        if self.app_specific_backtrace:
                            self.crashlog.backtraces.append(self.thread)
                        else:
                            self.crashlog.threads.append(self.thread)
                    self.thread = None

                empty_lines = 1
                while (
                    idx + empty_lines < lines_count
                    and len(lines[idx + empty_lines]) == 0
                ):
                    empty_lines = empty_lines + 1

                if (
                    empty_lines == 1
                    and idx + empty_lines < lines_count - 1
                    and self.parse_mode != self.CrashLogParseMode.NORMAL
                ):
                    # check if next line can be parsed with the current parse mode
                    next_line_idx = idx + empty_lines
                    if self.parsers[self.parse_mode](lines[next_line_idx]):
                        # If that suceeded, skip the empty line and the next line.
                        idx = next_line_idx + 1
                        continue
                self.parse_mode = self.CrashLogParseMode.NORMAL

            self.parsers[self.parse_mode](line)

            idx = idx + 1

        return self.crashlog

    def parse_exception(self, line):
        if not line.startswith("Exception"):
            return False
        if line.startswith("Exception Type:"):
            self.crashlog.thread_exception = line[15:].strip()
            exception_type_match = self.exception_type_regex.search(line)
            if exception_type_match:
                exc_type, exc_signal = exception_type_match.groups()
                self.crashlog.exception["type"] = exc_type
                if exc_signal:
                    self.crashlog.exception["signal"] = exc_signal
        elif line.startswith("Exception Subtype:"):
            self.crashlog.thread_exception_subtype = line[18:].strip()
            if "type" in self.crashlog.exception:
                self.crashlog.exception[
                    "subtype"
                ] = self.crashlog.thread_exception_subtype
        elif line.startswith("Exception Codes:"):
            self.crashlog.thread_exception_data = line[16:].strip()
            if "type" not in self.crashlog.exception:
                return False
            exception_codes_match = self.exception_codes_regex.search(line)
            if exception_codes_match:
                self.crashlog.exception["codes"] = self.crashlog.thread_exception_data
                code, subcode = exception_codes_match.groups()
                self.crashlog.exception["rawCodes"] = [
                    int(code, base=16),
                    int(subcode, base=16),
                ]
        else:
            if "type" not in self.crashlog.exception:
                return False
            exception_extra_match = self.exception_extra_regex.search(line)
            if exception_extra_match:
                self.crashlog.exception["message"] = exception_extra_match.group(1)
        return True

    def parse_normal(self, line):
        if line.startswith("Process:"):
            (self.crashlog.process_name, pid_with_brackets) = (
                line[8:].strip().split(" [")
            )
            self.crashlog.process_id = pid_with_brackets.strip("[]")
        elif line.startswith("Path:"):
            self.crashlog.process_path = line[5:].strip()
        elif line.startswith("Identifier:"):
            self.crashlog.process_identifier = line[11:].strip()
        elif line.startswith("Version:"):
            version_string = line[8:].strip()
            matched_pair = re.search("(.+)\((.+)\)", version_string)
            if matched_pair:
                self.crashlog.process_version = matched_pair.group(1)
                self.crashlog.process_compatability_version = matched_pair.group(2)
            else:
                self.crashlog.process = version_string
                self.crashlog.process_compatability_version = version_string
        elif line.startswith("Code Type:"):
            if "ARM-64" in line:
                self.crashlog.process_arch = "arm64"
            elif "X86-64" in line:
                self.crashlog.process_arch = "x86_64"
        elif self.parent_process_regex.search(line):
            parent_process_match = self.parent_process_regex.search(line)
            self.crashlog.parent_process_name = parent_process_match.group(1)
            self.crashlog.parent_process_id = parent_process_match.group(2)
        elif line.startswith("Exception"):
            self.parse_exception(line)
            return
        elif line.startswith("Crashed Thread:"):
            self.crashlog.crashed_thread_idx = int(line[15:].strip().split()[0])
            return
        elif line.startswith("Triggered by Thread:"):  # iOS
            self.crashlog.crashed_thread_idx = int(line[20:].strip().split()[0])
            return
        elif line.startswith("Report Version:"):
            self.crashlog.version = int(line[15:].strip())
            return
        elif line.startswith("System Profile:"):
            self.parse_mode = self.CrashLogParseMode.SYSTEM
            return
        elif (
            line.startswith("Interval Since Last Report:")
            or line.startswith("Crashes Since Last Report:")
            or line.startswith("Per-App Interval Since Last Report:")
            or line.startswith("Per-App Crashes Since Last Report:")
            or line.startswith("Sleep/Wake UUID:")
            or line.startswith("Anonymous UUID:")
        ):
            # ignore these
            return
        elif line.startswith("Thread"):
            thread_state_match = self.thread_state_regex.search(line)
            if thread_state_match:
                self.app_specific_backtrace = False
                thread_state_match = self.thread_regex.search(line)
                if thread_state_match:
                    thread_idx = int(thread_state_match.group(1))
                else:
                    thread_idx = self.crashlog.crashed_thread_idx
                self.parse_mode = self.CrashLogParseMode.THREGS
                self.thread = self.crashlog.threads[thread_idx]
                return
            thread_insts_match = self.thread_instrs_regex.search(line)
            if thread_insts_match:
                self.parse_mode = self.CrashLogParseMode.INSTRS
                return
            thread_match = self.thread_regex.search(line)
            if thread_match:
                self.app_specific_backtrace = False
                self.parse_mode = self.CrashLogParseMode.THREAD
                thread_idx = int(thread_match.group(1))
                self.thread = self.crashlog.Thread(
                    thread_idx, False, self.crashlog.process_arch
                )
                return
            return
        elif line.startswith("Binary Images:"):
            self.parse_mode = self.CrashLogParseMode.IMAGES
            return
        elif line.startswith("Application Specific Backtrace"):
            app_backtrace_match = self.app_backtrace_regex.search(line)
            if app_backtrace_match:
                self.parse_mode = self.CrashLogParseMode.THREAD
                self.app_specific_backtrace = True
                idx = int(app_backtrace_match.group(1))
                self.thread = self.crashlog.Thread(
                    idx, True, self.crashlog.process_arch
                )
                self.thread.name = "Application Specific Backtrace"
        elif line.startswith("Last Exception Backtrace:"):  # iOS
            self.parse_mode = self.CrashLogParseMode.THREAD
            self.app_specific_backtrace = True
            idx = 1
            self.thread = self.crashlog.Thread(idx, True, self.crashlog.process_arch)
            self.thread.name = "Last Exception Backtrace"
        self.crashlog.info_lines.append(line.strip())

    def parse_thread(self, line):
        if line.startswith("Thread"):
            return False
        if self.null_frame_regex.search(line):
            print('warning: thread parser ignored null-frame: "%s"' % line)
            return False
        frame_match = self.frame_regex.search(line)
        if not frame_match:
            print('error: frame regex failed for line: "%s"' % line)
            return False

        frame_id = (
            frame_img_name
        ) = (
            frame_addr
        ) = frame_symbol = frame_offset = frame_file = frame_line = frame_column = None

        if len(frame_match.groups()) == 3:
            # Get the image UUID from the frame image name.
            (frame_id, frame_img_name, frame_addr) = frame_match.groups()
        elif len(frame_match.groups()) == 5:
            (
                frame_id,
                frame_img_name,
                frame_addr,
                frame_symbol,
                frame_offset,
            ) = frame_match.groups()
        elif len(frame_match.groups()) == 7:
            (
                frame_id,
                frame_img_name,
                frame_addr,
                frame_symbol,
                frame_offset,
                frame_file,
                frame_line,
            ) = frame_match.groups()
        elif len(frame_match.groups()) == 8:
            (
                frame_id,
                frame_img_name,
                frame_addr,
                frame_symbol,
                frame_offset,
                frame_file,
                frame_line,
                frame_column,
            ) = frame_match.groups()

        self.thread.add_ident(frame_img_name)
        if frame_img_name not in self.crashlog.idents:
            self.crashlog.idents.append(frame_img_name)

        description = ""
        # Since images are parsed after threads, we need to build a
        # map for every image with a list of all the symbols and addresses
        if frame_img_name and frame_addr and frame_symbol:
            description = frame_symbol
            frame_offset_value = 0
            if frame_offset:
                description += " + " + frame_offset
                frame_offset_value = int(frame_offset, 0)
            if frame_img_name not in self.symbols:
                self.symbols[frame_img_name] = list()
            self.symbols[frame_img_name].append(
                {
                    "name": frame_symbol,
                    "address": int(frame_addr, 0) - frame_offset_value,
                }
            )

        self.thread.frames.append(
            self.crashlog.Frame(int(frame_id), int(frame_addr, 0), description)
        )

        return True

    def parse_images(self, line):
        image_match = self.image_regex_uuid.search(line)
        if image_match:
            (
                img_lo,
                img_hi,
                img_name,
                img_version,
                img_uuid,
                img_path,
            ) = image_match.groups()

            image = self.crashlog.DarwinImage(
                int(img_lo, 0),
                int(img_hi, 0),
                img_name.strip(),
                img_version.strip() if img_version else "",
                uuid.UUID(img_uuid),
                img_path,
                self.options.verbose,
            )
            unqualified_img_name = os.path.basename(img_path)
            if unqualified_img_name in self.symbols:
                for symbol in self.symbols[unqualified_img_name]:
                    image.symbols[symbol["name"]] = {
                        "name": symbol["name"],
                        "type": "code",
                        # NOTE: "address" is actually the symbol image offset
                        "address": symbol["address"] - int(img_lo, 0),
                    }

            self.crashlog.images.append(image)
            return True
        else:
            if self.options.debug:
                print("error: image regex failed for: %s" % line)
            return False

    def parse_thread_registers(self, line):
        # "r12: 0x00007fff6b5939c8  r13: 0x0000000007000006  r14: 0x0000000000002a03  r15: 0x0000000000000c00"
        reg_values = re.findall("([a-z0-9]+): (0x[0-9a-f]+)", line, re.I)
        for reg, value in reg_values:
            self.thread.registers[reg] = int(value, 16)
        return len(reg_values) != 0

    def parse_system(self, line):
        self.crashlog.system_profile.append(line)
        return True

    def parse_instructions(self, line):
        pass


def save_crashlog(debugger, command, exe_ctx, result, dict):
    usage = "save_crashlog [options] <output-path>"
    description = """Export the state of current target into a crashlog file"""
    parser = argparse.ArgumentParser(
        description=description,
        prog="save_crashlog",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "output",
        metavar="output-file",
        type=argparse.FileType("w", encoding="utf-8"),
        nargs=1,
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        dest="verbose",
        help="display verbose debug info",
        default=False,
    )
    try:
        options = parser.parse_args(shlex.split(command))
    except Exception as e:
        result.SetError(str(e))
        return
    target = exe_ctx.target
    if target:
        out_file = options.output[0]
        identifier = target.executable.basename
        process = exe_ctx.process
        if process:
            pid = process.id
            if pid != lldb.LLDB_INVALID_PROCESS_ID:
                out_file.write("Process:         %s [%u]\n" % (identifier, pid))
        out_file.write("Path:            %s\n" % (target.executable.fullpath))
        out_file.write("Identifier:      %s\n" % (identifier))
        out_file.write(
            "\nDate/Time:       %s\n"
            % (datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
        )
        out_file.write(
            "OS Version:      Mac OS X %s (%s)\n"
            % (
                platform.mac_ver()[0],
                subprocess.check_output("sysctl -n kern.osversion", shell=True).decode(
                    "utf-8"
                ),
            )
        )
        out_file.write("Report Version:  9\n")
        for thread_idx in range(process.num_threads):
            thread = process.thread[thread_idx]
            out_file.write("\nThread %u:\n" % (thread_idx))
            for frame_idx, frame in enumerate(thread.frames):
                frame_pc = frame.pc
                frame_offset = 0
                if frame.function:
                    block = frame.GetFrameBlock()
                    block_range = block.range[frame.addr]
                    if block_range:
                        block_start_addr = block_range[0]
                        frame_offset = frame_pc - block_start_addr.GetLoadAddress(
                            target
                        )
                    else:
                        frame_offset = frame_pc - frame.function.addr.GetLoadAddress(
                            target
                        )
                elif frame.symbol:
                    frame_offset = frame_pc - frame.symbol.addr.GetLoadAddress(target)
                out_file.write(
                    "%-3u %-32s 0x%16.16x %s"
                    % (frame_idx, frame.module.file.basename, frame_pc, frame.name)
                )
                if frame_offset > 0:
                    out_file.write(" + %u" % (frame_offset))
                line_entry = frame.line_entry
                if line_entry:
                    if options.verbose:
                        # This will output the fullpath + line + column
                        out_file.write(" %s" % (line_entry))
                    else:
                        out_file.write(
                            " %s:%u" % (line_entry.file.basename, line_entry.line)
                        )
                        column = line_entry.column
                        if column:
                            out_file.write(":%u" % (column))
                out_file.write("\n")

        out_file.write("\nBinary Images:\n")
        for module in target.modules:
            text_segment = module.section["__TEXT"]
            if text_segment:
                text_segment_load_addr = text_segment.GetLoadAddress(target)
                if text_segment_load_addr != lldb.LLDB_INVALID_ADDRESS:
                    text_segment_end_load_addr = (
                        text_segment_load_addr + text_segment.size
                    )
                    identifier = module.file.basename
                    module_version = "???"
                    module_version_array = module.GetVersion()
                    if module_version_array:
                        module_version = ".".join(map(str, module_version_array))
                    out_file.write(
                        "    0x%16.16x - 0x%16.16x  %s (%s - ???) <%s> %s\n"
                        % (
                            text_segment_load_addr,
                            text_segment_end_load_addr,
                            identifier,
                            module_version,
                            module.GetUUIDString(),
                            module.file.fullpath,
                        )
                    )
        out_file.close()
    else:
        result.SetError("invalid target")


class Symbolicate:
    def __init__(self, debugger, internal_dict):
        pass

    def __call__(self, debugger, command, exe_ctx, result):
        SymbolicateCrashLogs(debugger, shlex.split(command), result, True)

    def get_short_help(self):
        return "Symbolicate one or more darwin crash log files."

    def get_long_help(self):
        arg_parser = CrashLogOptionParser()
        return arg_parser.format_help()


def SymbolicateCrashLog(crash_log, options):
    if options.debug:
        crash_log.dump()
    if not crash_log.images:
        print("error: no images in crash log")
        return

    if options.dump_image_list:
        print("Binary Images:")
        for image in crash_log.images:
            if options.verbose:
                print(image.debug_dump())
            else:
                print(image)

    target = crash_log.create_target()
    if not target:
        return

    crash_log.load_images(options)

    if crash_log.backtraces:
        for thread in crash_log.backtraces:
            thread.dump_symbolicated(crash_log, options)
            print()

    for thread in crash_log.threads:
        if options.crashed_only and not (
            thread.crashed or thread.app_specific_backtrace
        ):
            continue
        thread.dump_symbolicated(crash_log, options)
        print()

    if crash_log.errors:
        print("Errors:")
        for error in crash_log.errors:
            print(error)


def load_crashlog_in_scripted_process(debugger, crashlog_path, options, result):
    crashlog = CrashLogParser.create(debugger, crashlog_path, options).parse()

    target = lldb.SBTarget()
    # 1. Try to use the user-provided target
    if options.target_path:
        target = debugger.CreateTarget(options.target_path)
        if not target:
            raise InteractiveCrashLogException(
                "couldn't create target provided by the user (%s)" % options.target_path
            )
        crashlog.target = target

    # 2. If the user didn't provide a target, try to create a target using the symbolicator
    if not target or not target.IsValid():
        target = crashlog.create_target()
    # 3. If that didn't work, create a dummy target
    if target is None or not target.IsValid():
        arch = crashlog.process_arch
        if not arch:
            raise InteractiveCrashLogException(
                "couldn't create find the architecture to create the target"
            )
        target = debugger.CreateTargetWithFileAndArch(None, arch)
    # 4. Fail
    if target is None or not target.IsValid():
        raise InteractiveCrashLogException("couldn't create target")

    ci = debugger.GetCommandInterpreter()
    if not ci:
        raise InteractiveCrashLogException("couldn't get command interpreter")

    ci.HandleCommand("script from lldb.macosx import crashlog_scripted_process", result)
    if not result.Succeeded():
        raise InteractiveCrashLogException(
            "couldn't import crashlog scripted process module"
        )

    structured_data = lldb.SBStructuredData()
    structured_data.SetFromJSON(
        json.dumps(
            {
                "file_path": crashlog_path,
                "load_all_images": options.load_all_images,
                "crashed_only": options.crashed_only,
                "no_parallel_image_loading": options.no_parallel_image_loading,
            }
        )
    )
    launch_info = lldb.SBLaunchInfo(None)
    launch_info.SetProcessPluginName("ScriptedProcess")
    launch_info.SetScriptedProcessClassName(
        "crashlog_scripted_process.CrashLogScriptedProcess"
    )
    launch_info.SetScriptedProcessDictionary(structured_data)
    launch_info.SetLaunchFlags(lldb.eLaunchFlagStopAtEntry)

    error = lldb.SBError()
    process = target.Launch(launch_info, error)

    if not process or error.Fail():
        raise InteractiveCrashLogException("couldn't launch Scripted Process", error)

    process.GetScriptedImplementation().set_crashlog(crashlog)
    process.Continue()

    if not options.skip_status:

        @contextlib.contextmanager
        def synchronous(debugger):
            async_state = debugger.GetAsync()
            debugger.SetAsync(False)
            try:
                yield
            finally:
                debugger.SetAsync(async_state)

        with synchronous(debugger):
            run_options = lldb.SBCommandInterpreterRunOptions()
            run_options.SetStopOnError(True)
            run_options.SetStopOnCrash(True)
            run_options.SetEchoCommands(True)

            commands_stream = lldb.SBStream()
            commands_stream.Print("process status --verbose\n")
            commands_stream.Print("thread backtrace --extended true\n")
            error = debugger.SetInputString(commands_stream.GetData())
            if error.Success():
                debugger.RunCommandInterpreter(True, False, run_options, 0, False, True)


def CreateSymbolicateCrashLogOptions(
    command_name, description, add_interactive_options
):
    usage = "crashlog [options] <FILE> [FILE ...]"
    arg_parser = argparse.ArgumentParser(
        description=description,
        prog="crashlog",
        usage=usage,
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    arg_parser.add_argument(
        "reports",
        metavar="FILE",
        type=str,
        nargs="*",
        help="crash report(s) to symbolicate",
    )

    arg_parser.add_argument(
        "--version",
        "-V",
        dest="version",
        action="store_true",
        help="Show crashlog version",
        default=False,
    )
    arg_parser.add_argument(
        "--verbose",
        "-v",
        action="store_true",
        dest="verbose",
        help="display verbose debug info",
        default=False,
    )
    arg_parser.add_argument(
        "--debug",
        "-g",
        action="store_true",
        dest="debug",
        help="display verbose debug logging",
        default=False,
    )
    arg_parser.add_argument(
        "--load-all",
        "-a",
        action="store_true",
        dest="load_all_images",
        help="load all executable images, not just the images found in the "
        "crashed stack frames, loads stackframes for all the threads in "
        "interactive mode.",
        default=False,
    )
    arg_parser.add_argument(
        "--images",
        action="store_true",
        dest="dump_image_list",
        help="show image list",
        default=False,
    )
    arg_parser.add_argument(
        "--debug-delay",
        type=int,
        dest="debug_delay",
        metavar="NSEC",
        help="pause for NSEC seconds for debugger",
        default=0,
    )
    # NOTE: Requires python 3.9
    # arg_parser.add_argument(
    #     "--crashed-only",
    #     "-c",
    #     action=argparse.BooleanOptionalAction,
    #     dest="crashed_only",
    #     help="only symbolicate the crashed thread",
    #     default=True,
    # )
    arg_parser.add_argument(
        "--crashed-only",
        "-c",
        action="store_true",
        dest="crashed_only",
        help="only symbolicate the crashed thread",
        default=True,
    )
    arg_parser.add_argument(
        "--no-crashed-only",
        action="store_false",
        dest="crashed_only",
        help="in batch mode, symbolicate all threads, not only the crashed one",
        default=False,
    )
    arg_parser.add_argument(
        "--disasm-depth",
        "-d",
        type=int,
        dest="disassemble_depth",
        help="set the depth in stack frames that should be disassembled",
        default=1,
    )
    arg_parser.add_argument(
        "--disasm-all",
        "-D",
        action="store_true",
        dest="disassemble_all_threads",
        help="enabled disassembly of frames on all threads (not just the crashed thread)",
        default=False,
    )
    arg_parser.add_argument(
        "--disasm-before",
        "-B",
        type=int,
        dest="disassemble_before",
        help="the number of instructions to disassemble before the frame PC",
        default=4,
    )
    arg_parser.add_argument(
        "--disasm-after",
        "-A",
        type=int,
        dest="disassemble_after",
        help="the number of instructions to disassemble after the frame PC",
        default=4,
    )
    arg_parser.add_argument(
        "--source-context",
        "-C",
        type=int,
        metavar="NLINES",
        dest="source_context",
        help="show NLINES source lines of source context",
        default=4,
    )
    arg_parser.add_argument(
        "--source-frames",
        type=int,
        metavar="NFRAMES",
        dest="source_frames",
        help="show source for NFRAMES",
        default=4,
    )
    arg_parser.add_argument(
        "--source-all",
        action="store_true",
        dest="source_all",
        help="show source for all threads, not just the crashed thread",
        default=False,
    )
    arg_parser.add_argument(
        "--no-parallel-image-loading",
        dest="no_parallel_image_loading",
        action="store_true",
        help=argparse.SUPPRESS,
        default=False,
    )
    if add_interactive_options:
        arg_parser.add_argument(
            "-i",
            "--interactive",
            action="store_true",
            help="parse a crash log and load it in a ScriptedProcess",
            default=False,
        )
        arg_parser.add_argument(
            "-b",
            "--batch",
            action="store_true",
            help="dump symbolicated stackframes without creating a debug session",
            default=True,
        )
        arg_parser.add_argument(
            "--target",
            "-t",
            dest="target_path",
            help="the target binary path that should be used for interactive crashlog (optional)",
            default=None,
        )
        arg_parser.add_argument(
            "--skip-status",
            "-s",
            dest="skip_status",
            action="store_true",
            help="prevent the interactive crashlog to dump the process status and thread backtrace at launch",
            default=False,
        )
    return arg_parser


def CrashLogOptionParser():
    description = """Symbolicate one or more darwin crash log files to provide source file and line information,
inlined stack frames back to the concrete functions, and disassemble the location of the crash
for the first frame of the crashed thread.
If this script is imported into the LLDB command interpreter, a "crashlog" command will be added to the interpreter
for use at the LLDB command line. After a crash log has been parsed and symbolicated, a target will have been
created that has all of the shared libraries loaded at the load addresses found in the crash log file. This allows
you to explore the program as if it were stopped at the locations described in the crash log and functions can
be disassembled and lookups can be performed using the addresses found in the crash log."""
    return CreateSymbolicateCrashLogOptions("crashlog", description, True)


def SymbolicateCrashLogs(debugger, command_args, result, is_command):
    arg_parser = CrashLogOptionParser()

    if not len(command_args):
        arg_parser.print_help()
        return

    try:
        options = arg_parser.parse_args(command_args)
    except Exception as e:
        result.SetError(str(e))
        return

    # Interactive mode requires running the crashlog command from inside lldb.
    if options.interactive and not is_command:
        lldb_exec = (
            subprocess.check_output(["/usr/bin/xcrun", "-f", "lldb"])
            .decode("utf-8")
            .strip()
        )
        sys.exit(
            os.execv(
                lldb_exec,
                [
                    lldb_exec,
                    "-o",
                    "command script import lldb.macosx",
                    "-o",
                    "crashlog {}".format(shlex.join(command_args)),
                ],
            )
        )

    if "NO_PARALLEL_IMG_LOADING" in os.environ:
        options.no_parallel_image_loading = True

    if options.version:
        print(debugger.GetVersionString())
        return

    if options.debug:
        print("command_args = %s" % command_args)
        print("options", options)
        print("args", options.reports)

    if options.debug_delay > 0:
        print("Waiting %u seconds for debugger to attach..." % options.debug_delay)
        time.sleep(options.debug_delay)
    error = lldb.SBError()

    def should_run_in_interactive_mode(options, ci):
        if options.interactive:
            return True
        elif options.batch:
            return False
        # elif ci and ci.IsInteractive():
        #     return True
        else:
            return False

    ci = debugger.GetCommandInterpreter()

    if options.reports:
        for crashlog_file in options.reports:
            crashlog_path = os.path.expanduser(crashlog_file)
            if not os.path.exists(crashlog_path):
                raise FileNotFoundError(
                    "crashlog file %s does not exist" % crashlog_path
                )
            if should_run_in_interactive_mode(options, ci):
                try:
                    load_crashlog_in_scripted_process(
                        debugger, crashlog_path, options, result
                    )
                except InteractiveCrashLogException as e:
                    result.SetError(str(e))
            else:
                crash_log = CrashLogParser.create(
                    debugger, crashlog_path, options
                ).parse()
                SymbolicateCrashLog(crash_log, options)


if __name__ == "__main__":
    # Create a new debugger instance
    debugger = lldb.SBDebugger.Create()
    result = lldb.SBCommandReturnObject()
    SymbolicateCrashLogs(debugger, sys.argv[1:], result, False)
    lldb.SBDebugger.Destroy(debugger)


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand(
        "command script add -o -c lldb.macosx.crashlog.Symbolicate -C disk-file crashlog"
    )
    debugger.HandleCommand(
        "command script add -o -f lldb.macosx.crashlog.save_crashlog -C disk-file save_crashlog"
    )
    print(
        '"crashlog" and "save_crashlog" commands have been installed, use '
        'the "--help" options on these commands for detailed help.'
    )
