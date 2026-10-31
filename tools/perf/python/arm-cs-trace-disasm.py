#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
arm-cs-trace-disasm.py: ARM CoreSight Trace Dump With Disassembler

Author: Tor Jeremiassen <tor@ti.com>
        Mathieu Poirier <mathieu.poirier@linaro.org>
        Leo Yan <leo.yan@linaro.org>
        Al Grant <Al.Grant@arm.com>

Below are some example commands for using this script.
Note a --kcore recording is required for accurate decode
due to the alternatives patching mechanism. In addition to this,
source line info comes from Perf, and when using kcore there is
no debug info. The following lists the supported features in each mode:

+-----------+-----------------+------------------+------------------+
| Recording | Accurate decode | Source line dump | Disassembly dump |
+-----------+-----------------+------------------+------------------+
| --kcore   | yes             | no               | yes              |
| normal    | no              | yes (inaccurate) | yes (inaccurate) |
+-----------+-----------------+------------------+------------------+

Output disassembly with objdump and auto detect vmlinux
(when running on same machine):
 perf script arm-cs-trace-disasm -- -d

Output disassembly with llvm-objdump:
 perf script arm-cs-trace-disasm -- -d llvm-objdump-11 -k path/to/vmlinux

Output accurate disassembly by passing kcore to script:
 perf script arm-cs-trace-disasm -- -d -k perf.data/kcore_dir/kcore

Output only source line and symbols:
 perf script arm-cs-trace-disasm
"""
from __future__ import annotations

import os
from os import path
import re
from subprocess import CalledProcessError, check_output
import argparse
import platform
import sys
from typing import Dict, List, Optional

import perf

# Initialize global dicts and regular expression
DISASM_CACHE: Dict[str, List[str]] = {}
DISASM_RE = re.compile(r"^\s*([0-9a-fA-F]+):")
DISASM_FUNC_RE = re.compile(r"^\s*([0-9a-fA-F]+)\s.*:")
CACHE_SIZE = 1024
class _State:
    sample_idx: int = -1
    source_file_name: Optional[str] = None
    line_number: Optional[int] = None
    dso: Optional[str] = None

_STATE = _State()

KVER = platform.release()
VMLINUX_PATHS = [
    f"/usr/lib/debug/boot/vmlinux-{KVER}.debug",
    f"/usr/lib/debug/lib/modules/{KVER}/vmlinux",
    f"/lib/modules/{KVER}/build/vmlinux",
    f"/usr/lib/debug/boot/vmlinux-{KVER}",
    f"/boot/vmlinux-{KVER}",
    "/boot/vmlinux",
    "vmlinux",
]

def default_objdump() -> str:
    """Return the default objdump path from perf config or 'objdump'."""
    try:
        config = perf.config_get("annotate.objdump")
        return str(config) if config else "objdump"
    except (AttributeError, TypeError):
        return "objdump"

def find_vmlinux() -> Optional[str]:
    """Find the vmlinux file in standard paths."""
    if hasattr(find_vmlinux, "path"):
        return getattr(find_vmlinux, "path")

    for v in VMLINUX_PATHS:
        if os.access(v, os.R_OK):
            setattr(find_vmlinux, "path", v)
            return v
    setattr(find_vmlinux, "path", None)
    return None

def get_dso_file_path(dso_name: str, dso_build_id: str, vmlinux: Optional[str]) -> str:
    """Return the path to the DSO file."""
    # Locate DSO binaries in the perf build-id cache (~/.debug or PERF_BUILDID_DIR).
    # Perf stores cached binaries under both <buildid_dir>/<dso_long_name>/<build_id>/elf
    # and the canonical symlink/file layout <buildid_dir>/.build-id/<bid[:2]>/<bid[2:]>/elf.
    buildid_dir = os.environ.get('PERF_BUILDID_DIR')
    if not buildid_dir:
        buildid_dir = os.path.join(os.environ.get('HOME', ''), '.debug')

    if (dso_name in ("[kernel.kallsyms]", "vmlinux", "kcore")
            or dso_name.endswith("/vmlinux") or dso_name.endswith("/kcore")):
        if vmlinux:
            return vmlinux
        if dso_build_id and dso_build_id != "[unknown]" and len(dso_build_id) > 2:
            bid_candidate = os.path.join(
                buildid_dir, ".build-id", dso_build_id[:2], dso_build_id[2:], "elf"
            )
            if os.access(bid_candidate, os.R_OK):
                return bid_candidate
            for kname in (dso_name, "vmlinux", "[kernel.kallsyms]"):
                candidate = os.path.join(buildid_dir, kname.lstrip("/"), dso_build_id, "elf")
                if os.access(candidate, os.R_OK):
                    return candidate
        return find_vmlinux() or dso_name

    if dso_name == "[vdso]":
        append = "/vdso"
    else:
        append = "/elf"

    if dso_build_id and dso_build_id != "[unknown]" and len(dso_build_id) > 2:
        bid_path = (buildid_dir + "/.build-id/" + dso_build_id[:2] + "/"
                    + dso_build_id[2:] + append).replace('//', '/')
        if os.access(bid_path, os.R_OK):
            return bid_path

    dso_path = buildid_dir + "/" + dso_name + "/" + dso_build_id + append
    # Replace duplicate slash chars to single slash char
    dso_path = dso_path.replace('//', '/')
    return dso_path

def read_disam(dso_fname: str, dso_start: int, start_addr: int,
               stop_addr: int, objdump: str) -> List[str]:
    """Read disassembly from a DSO file using objdump."""
    if stop_addr <= start_addr or (stop_addr - start_addr) > 0x100000:
        return []
    addr_range = f"{start_addr}:{stop_addr}:{dso_start}:{dso_fname}"

    # Don't let the cache get too big, clear it when it hits max size
    if len(DISASM_CACHE) > CACHE_SIZE:
        DISASM_CACHE.clear()

    if addr_range in DISASM_CACHE:
        disasm_output = DISASM_CACHE[addr_range]
    else:
        start_addr = start_addr - dso_start
        stop_addr = stop_addr - dso_start
        disasm = [objdump, "-d", "-z",
                  f"--start-address={start_addr:#x}",
                  f"--stop-address={stop_addr:#x}"]
        disasm += [dso_fname]
        try:
            disasm_output = check_output(disasm).decode('utf-8', errors='replace').split('\n')
        except (CalledProcessError, OSError):
            return []
        if len(disasm_output) <= 512:
            DISASM_CACHE[addr_range] = disasm_output

    return disasm_output

def print_disam(dso_fname: str, dso_start: int, start_addr: int,
                stop_addr: int, objdump: str) -> None:
    """Print disassembly for a given address range."""
    for line in read_disam(dso_fname, dso_start, start_addr, stop_addr, objdump):
        m = DISASM_FUNC_RE.search(line)
        if m is None:
            m = DISASM_RE.search(line)
            if m is None:
                continue
        print(f"\t{line}")

def print_sample(sample: perf.sample_event) -> None:
    """Print sample details."""
    print(f"Sample = {{ cpu: {sample.sample_cpu:04d} addr: {sample.sample_addr:016x} "
          f"phys_addr: {sample.sample_phys_addr:016x} ip: {sample.sample_ip:016x} "
          f"pid: {sample.sample_pid} tid: {sample.sample_tid} period: {sample.sample_period} "
          f"time: {sample.sample_time} index: {_STATE.sample_idx}}}")

def common_start_str(comm: str, sample: perf.sample_event) -> str:
    """Return common start string for sample output."""
    sec = int(sample.sample_time / 1000000000)
    ns = sample.sample_time % 1000000000
    cpu = sample.sample_cpu
    pid = sample.sample_pid
    tid = sample.sample_tid
    return f"{comm:>16s} {pid:5d}/{tid:<5d} [{cpu:04d}] {sec:9d}.{ns:09d}  "

def print_srccode(comm: str, sample: perf.sample_event, symbol: str, dso: str) -> None:
    """Print source code and symbols for a sample."""
    ip = sample.sample_ip
    if symbol == "[unknown]":
        start_str = common_start_str(comm, sample) + f"{ip:x}".rjust(16).ljust(40)
    else:
        symoff = 0
        symoff = getattr(sample, 'sym_offset', 0) or 0
        offs = f"+{symoff:#x}" if symoff != 0 else ""
        start_str = common_start_str(comm, sample) + (symbol + offs).ljust(40)

    source_file_name, line_number, source_line = sample.srccode() or (None, 0, None)
    if source_file_name:
        if _STATE.line_number == line_number and _STATE.source_file_name == source_file_name:
            src_str = ""
        else:
            if len(source_file_name) > 40:
                src_file = f"...{source_file_name[-37:]} "
            else:
                src_file = source_file_name.ljust(41)

            if source_line is None:
                src_str = f"{src_file}{line_number:>4d} <source not found>"
            else:
                src_str = f"{src_file}{line_number:>4d} {source_line}"
        _STATE.dso = None
    elif dso == _STATE.dso:
        src_str = ""
    else:
        src_str = dso
        _STATE.dso = dso

    _STATE.line_number = line_number
    _STATE.source_file_name = source_file_name

    print(start_str, src_str)

class TraceDisasm:
    """Class to handle trace disassembly."""
    def __init__(self, cli_options: argparse.Namespace):
        self.options = cli_options
        self.sample_idx = -1
        self.cpu_data: Dict[str, int] = {}
        self.session: Optional[perf.session] = None

    def process_event(self, sample: perf.sample_event) -> None:
        """Process a single perf event."""
        self.sample_idx += 1
        _STATE.sample_idx = self.sample_idx

        if self.options.start_time is not None and sample.sample_time < self.options.start_time:
            return
        if self.options.stop_time is not None and sample.sample_time > self.options.stop_time:
            sys.exit(0)
        if self.options.start_sample is not None and self.sample_idx < self.options.start_sample:
            return
        if self.options.stop_sample is not None and self.sample_idx > self.options.stop_sample:
            sys.exit(0)

        ev_name = str(sample.evsel)
        if self.options.verbose:
            print(f"Event type: {ev_name}")
            print_sample(sample)

        # Initialize CPU data if it's empty, and directly return back
        # if this is the first tracing event for this CPU. This must happen
        # before the dso == '[unknown]' check because the initial
        # CS_ETM_TRACE_ON sample has ip == 0 (dso == '[unknown]') and
        # carries the starting target address in sample.sample_addr.
        if self.cpu_data.get(f"{sample.sample_cpu}addr") is None:
            self.cpu_data[f"{sample.sample_cpu}addr"] = sample.sample_addr
            return

        dso = getattr(sample, 'dso_long_name', None) or sample.dso or '[unknown]'
        symbol = sample.symbol or '[unknown]'
        dso_bid = sample.dso_bid or '[unknown]'
        dso_start = sample.map_start
        dso_end = sample.map_end
        map_pgoff = sample.map_pgoff or 0

        comm = "[unknown]"
        try:
            if self.session:
                thread_info = self.session.find_thread(sample.sample_pid, sample.sample_tid)
                if thread_info:
                    comm = thread_info.comm() or "[unknown]"
        except (TypeError, AttributeError):
            pass

        if dso == '[unknown]':
            return

        if dso_start is None or dso_end is None:
            print(f"Failed to find valid dso map for dso {dso}")
            return

        if "instructions" in ev_name:
            print_srccode(comm, sample, symbol, dso)
            return

        if "branches" not in ev_name:
            return

        self._process_branch(sample, comm, symbol, dso, dso_bid, dso_start, dso_end, map_pgoff)

    def _process_branch(self, sample: perf.sample_event, comm: str, symbol: str, dso: str,
                        dso_bid: str, dso_start: int, dso_end: int, map_pgoff: int) -> None:
        """Helper to process branch events."""
        cpu = sample.sample_cpu
        ip = sample.sample_ip
        addr = sample.sample_addr

        # The format for packet is:
        #
        #                 +------------+------------+------------+
        #  sample_prev:   |    addr    |     ip     |    cpu     |
        #                 +------------+------------+------------+
        #  sample_next:   |    addr    |     ip     |    cpu     |
        #                 +------------+------------+------------+
        #
        # Combine the two continuous packets to get the instruction range for
        # sample_prev::cpu:
        #
        #     [ sample_prev::addr .. sample_next::ip ]
        #
        # sample_prev::addr is stored into cpu_data and read back as
        # 'start_addr' when the new packet arrives, and sample_next::ip + 4 is
        # used for 'stop_addr' so objdump includes the branch instruction at
        # sample_next::ip.
        start_addr = self.cpu_data[str(cpu) + 'addr']
        stop_addr  = ip + 4

        # Record for previous sample packet
        self.cpu_data[str(cpu) + 'addr'] = addr

        # Filter out zero start_address. Optionally identify CS_ETM_TRACE_ON packet
        if start_addr == 0:
            if stop_addr == 4 and self.options.verbose:
                print(f"CPU{cpu}: CS_ETM_TRACE_ON packet is inserted")
            return

        if start_addr < dso_start or start_addr > dso_end:
            print(f"Start address {start_addr:#x} is out of range [ {dso_start:#x} .. "
                  f"{dso_end:#x} ] for dso {dso}")
            return

        if stop_addr < dso_start or stop_addr > dso_end:
            print(f"Stop address {stop_addr:#x} is out of range [ {dso_start:#x} .. "
                  f"{dso_end:#x} ] for dso {dso}")
            return

        if self.options.objdump is not None:
            # Kernel images (vmlinux / kcore) and non-PIE executables linked at
            # the default 0x400000 base already have their virtual load addresses
            # baked into the ELF symbol/section headers read by objdump, so
            # dso_vm_start and map_pgoff_local must be 0 to avoid subtracting
            # map_start from virtual sample addresses.
            if (dso in ("[kernel.kallsyms]", "vmlinux", "kcore")
                    or dso.endswith("/vmlinux") or dso.endswith("/kcore")):
                dso_vm_start = 0
                map_pgoff_local = 0
            elif dso_start == 0x400000:
                dso_vm_start = 0
                map_pgoff_local = 0
            else:
                dso_vm_start = dso_start
                map_pgoff_local = map_pgoff

            dso_fname = get_dso_file_path(dso, dso_bid, self.options.vmlinux)
            if path.exists(dso_fname):
                print_disam(dso_fname, dso_vm_start, start_addr + map_pgoff_local,
                            stop_addr + map_pgoff_local, self.options.objdump)
            else:
                print(f"Failed to find dso {dso} for address range [ "
                      f"{start_addr + map_pgoff_local:#x} .. {stop_addr + map_pgoff_local:#x} ]")

        print_srccode(comm, sample, symbol, dso)

    def run(self) -> None:
        """Run the trace disassembly session."""
        input_file = self.options.input or "perf.data"
        if input_file != "-" and not os.path.exists(input_file):
            print(f"Error: {input_file} not found.", file=sys.stderr)
            sys.exit(1)

        print('ARM CoreSight Trace Data Assembler Dump')
        try:
            session_kwargs = {
                "sample": self.process_event,
                "itrace": self.options.itrace,
            }
            if self.options.vmlinux:
                session_kwargs["vmlinux"] = self.options.vmlinux
            try:
                self.session = perf.session(perf.data(input_file), **session_kwargs)
            except TypeError:
                session_kwargs.pop("vmlinux", None)
                self.session = perf.session(perf.data(input_file), **session_kwargs)
        except (OSError, ValueError, KeyError, RuntimeError, TypeError, AttributeError) as e:
            print(f"Error opening session: {e}", file=sys.stderr)
            sys.exit(1)

        try:
            self.session.process_events()
        except (KeyboardInterrupt, SystemExit):
            pass
        finally:
            # Break the reference cycle between self.session and the bound
            # self.process_event callback so the C perf.session object is freed.
            self.session = None
        print('End')

if __name__ == "__main__":
    def int_arg(v: str) -> int:
        """Helper for integer command line arguments."""
        val = int(v)
        if val < 0:
            raise argparse.ArgumentTypeError("Argument must be a positive integer")
        return val

    arg_parser = argparse.ArgumentParser(description="ARM CoreSight Trace Dump With Disassembler")
    arg_parser.add_argument("-i", "--input", help="input perf.data file")
    arg_parser.add_argument("-k", "--vmlinux", default=os.environ.get("PERF_SYMBOL_VMLINUX"),
                            help="Set path to vmlinux file. Omit to autodetect")
    arg_parser.add_argument("-d", "--objdump", nargs="?", const=default_objdump(),
                            help="Show disassembly. Can also be used to change the objdump path")
    arg_parser.add_argument("-v", "--verbose", action="store_true", help="Enable debugging log")
    arg_parser.add_argument("--start-time", type=int_arg,
                            help="Monotonic clock time of sample to start from.")
    arg_parser.add_argument("--stop-time", type=int_arg,
                            help="Monotonic clock time of sample to stop at.")
    arg_parser.add_argument("--itrace", default=os.environ.get("PERF_ITRACE") or "b",
                            help="Instruction tracing options.")
    arg_parser.add_argument("--start-sample", type=int_arg,
                            help="Index of sample to start from.")
    arg_parser.add_argument("--stop-sample", type=int_arg,
                            help="Index of sample to stop at.")

    parsed_options = arg_parser.parse_args()
    if (parsed_options.start_time is not None and parsed_options.stop_time is not None and
            parsed_options.start_time >= parsed_options.stop_time):
        print("--start-time must less than --stop-time")
        sys.exit(2)
    if (parsed_options.start_sample is not None and parsed_options.stop_sample is not None and
            parsed_options.start_sample >= parsed_options.stop_sample):
        print("--start-sample must less than --stop-sample")
        sys.exit(2)

    td = TraceDisasm(parsed_options)
    td.run()
