#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
Print Intel PT Events including Power Events and PTWRITE.
Ported from tools/perf/scripts/python/intel-pt-events.py
"""
from __future__ import annotations

import argparse
from typing import Dict, List, Tuple
import contextlib
from ctypes import addressof, create_string_buffer
import io
import os
import struct
import sys
from typing import Any, Optional
import typing
import perf

try:
    from libxed import LibXED as _LibXED
    LibXED: Optional[type[_LibXED]] = _LibXED
except ImportError:
    LibXED = None


def sample_flags_to_name(flags: int) -> str:
    """Implement perf's sample_flags_to_name."""
    if not isinstance(flags, int) or flags == 0:
        return "".ljust(21)
    # PERF_IP_FLAG_* bit positions (from tools/perf/util/event.h):
    #   bit 0:  PERF_IP_FLAG_BRANCH
    #   bit 1:  PERF_IP_FLAG_CALL
    #   bit 2:  PERF_IP_FLAG_RETURN
    #   bit 3:  PERF_IP_FLAG_CONDITIONAL
    #   bit 4:  PERF_IP_FLAG_SYSCALLRET
    #   bit 5:  PERF_IP_FLAG_ASYNC
    #   bit 6:  PERF_IP_FLAG_INTERRUPT
    #   bit 7:  PERF_IP_FLAG_TX_ABORT
    #   bit 8:  PERF_IP_FLAG_TRACE_BEGIN
    #   bit 9:  PERF_IP_FLAG_TRACE_END
    #   bit 10: PERF_IP_FLAG_IN_TX
    #   bit 11: PERF_IP_FLAG_VMENTRY
    #   bit 12: PERF_IP_FLAG_VMEXIT
    #   bit 13: PERF_IP_FLAG_INTR_DISABLE
    #   bit 14: PERF_IP_FLAG_INTR_TOGGLE
    #   bit 15: PERF_IP_FLAG_BRANCH_MISS
    #   bit 16: PERF_IP_FLAG_NOT_TAKEN
    sample_flags = [
        ((1 << 0) | (1 << 1), "call"),
        ((1 << 0) | (1 << 2), "return"),
        ((1 << 0) | (1 << 3), "jcc"),
        ((1 << 0), "jmp"),
        ((1 << 0) | (1 << 1) | (1 << 6), "int"),
        ((1 << 0) | (1 << 2) | (1 << 6), "iret"),
        ((1 << 0) | (1 << 1) | (1 << 4), "syscall"),
        ((1 << 0) | (1 << 2) | (1 << 4), "sysret"),
        ((1 << 0) | (1 << 5), "async"),
        ((1 << 0) | (1 << 1) | (1 << 5) | (1 << 6), "hw int"),
        ((1 << 0) | (1 << 7), "tx abrt"),
        ((1 << 0) | (1 << 8), "tr strt"),
        ((1 << 0) | (1 << 9), "tr end"),
        ((1 << 0) | (1 << 1) | (1 << 11), "vmentry"),
        ((1 << 0) | (1 << 1) | (1 << 12), "vmexit"),
    ]
    additional_mask = (1 << 10) | (1 << 13) | (1 << 14)
    branch_event_mask = (1 << 15) | (1 << 16)
    xf = flags & additional_mask
    rem_flags = flags & ~additional_mask

    if rem_flags & (1 << 8):
        prefix = "tr strt "
    elif rem_flags & (1 << 9):
        prefix = "tr end  "
    else:
        prefix = ""

    rem_flags &= ~((1 << 8) | (1 << 9))
    types = rem_flags & ~branch_event_mask
    type_name = ""
    for f_mask, f_name in sample_flags:
        if f_mask == types:
            type_name = f_name
            break

    s = prefix + type_name
    ev_parts = []
    if rem_flags & (1 << 15):
        ev_parts.append("miss")
    if rem_flags & (1 << 16):
        ev_parts.append("not_taken")
    if ev_parts:
        s += "/" + ",".join(ev_parts) + "/"

    if xf:
        xs = "("
        if xf & (1 << 10):
            xs += "x"
        if xf & (1 << 13):
            xs += "D"
        if xf & (1 << 14):
            xs += "t"
        xs += ")"
        if len(s) + len(xs) < 21:
            s = s + xs.rjust(21 - len(s))
        else:
            s = s + " " + xs
    return s.ljust(21)

class IntelPTAnalyzer:
    """Analyzes Intel PT events and prints details."""

    def __init__(self, cfg: argparse.Namespace):
        self.args = cfg
        self.session: Optional[perf.session] = None
        self.insn = False
        self.src = False
        self.source_file_name: Optional[str] = None
        self.line_number: int = 0
        self.dso: Optional[str] = None
        self.stash_dict: Dict[int, List[str]] = {}
        self.output: Any = None
        self.output_pos: int = 0
        self.cpu: int = -1
        self.time: int = 0
        self.switch_str: Dict[int, str] = {}

        if cfg.insn_trace:
            print("Intel PT Instruction Trace")
            self.insn = True
        elif cfg.src_trace:
            print("Intel PT Source Trace")
            self.insn = True
            self.src = True
        else:
            print("Intel PT Branch Trace, Power Events, Event Trace and PTWRITE")

        self.disassembler: Any = None
        self.inst: Any = None
        if self.insn and LibXED is not None:
            try:
                self.disassembler = LibXED()
                self.inst = self.disassembler.instruction()
            except (OSError, ValueError, KeyError, RuntimeError, TypeError, AttributeError,
                    ImportError) as e:
                print(f"Failed to initialize LibXED: {e}")
                self.disassembler = None
                self.inst = None

    def print_ptwrite(self, raw_buf: bytes) -> None:
        """Print PTWRITE data."""
        if not raw_buf:
            return
        try:
            data = struct.unpack_from("<IQ", raw_buf)
            flags = data[0]
            payload = data[1]
        except struct.error:
            return
        exact_ip = flags & 1
        try:
            s = payload.to_bytes(8, "little").decode("ascii").rstrip("\x00")
            if not s.isprintable():
                s = ""
        except (UnicodeDecodeError, ValueError):
            s = ""
        print(f"IP: {exact_ip} payload: {payload:#x} {s}", end=' ')

    def print_cbr(self, raw_buf: bytes) -> None:
        """Print CBR data."""
        if len(raw_buf) < 12:
            return
        try:
            data = struct.unpack_from("<BBBBII", raw_buf)
        except struct.error:
            return
        cbr = data[0]
        f = (data[4] + 500) // 1000
        if data[2] == 0:
            return
        p = ((cbr * 1000 // data[2]) + 5) // 10
        print(f"{cbr:3d}  freq: {f:4d} MHz  ({p:3d}%)", end=' ')

    def print_mwait(self, raw_buf: bytes) -> None:
        """Print MWAIT data."""
        try:
            data = struct.unpack_from("<IQ", raw_buf)
        except struct.error:
            return
        payload = data[1]
        hints = payload & 0xff
        extensions = (payload >> 32) & 0x3
        print(f"hints: {hints:#x} extensions: {extensions:#x}", end=' ')

    def print_pwre(self, raw_buf: bytes) -> None:
        """Print PWRE data."""
        try:
            data = struct.unpack_from("<IQ", raw_buf)
        except struct.error:
            return
        payload = data[1]
        hw = (payload >> 7) & 1
        cstate = (payload >> 12) & 0xf
        subcstate = (payload >> 8) & 0xf
        print(f"hw: {hw} cstate: {cstate} sub-cstate: {subcstate}", end=' ')

    def print_exstop(self, raw_buf: bytes) -> None:
        """Print EXSTOP data."""
        try:
            data = struct.unpack_from("<I", raw_buf)
        except struct.error:
            return
        flags = data[0]
        exact_ip = flags & 1
        print(f"IP: {exact_ip}", end=' ')

    def print_pwrx(self, raw_buf: bytes) -> None:
        """Print PWRX data."""
        try:
            data = struct.unpack_from("<IQ", raw_buf)
        except struct.error:
            return
        payload = data[1]
        deepest_cstate = payload & 0xf
        last_cstate = (payload >> 4) & 0xf
        wake_reason = (payload >> 8) & 0xf
        print(f"deepest cstate: {deepest_cstate} last cstate: {last_cstate} "
              f"wake reason: {wake_reason:#x}", end=' ')

    def print_psb(self, raw_buf: bytes) -> None:
        """Print PSB data."""
        try:
            data = struct.unpack_from("<IQ", raw_buf)
        except struct.error:
            return
        offset = data[1]
        print(f"offset: {offset:#x}", end=' ')

    def print_evt(self, raw_buf: bytes) -> None:
        """Print EVT data."""
        glb_cfe = ["", "INTR", "IRET", "SMI", "RSM", "SIPI", "INIT", "VMENTRY", "VMEXIT",
                   "VMEXIT_INTR", "SHUTDOWN", "", "UINT", "UIRET"] + [""] * 18
        glb_evd = ["", "PFA", "VMXQ", "VMXR"] + [""] * 60

        try:
            data = struct.unpack_from("<BBH", raw_buf)
        except struct.error:
            return
        typ = data[0] & 0x1f
        ip_flag = (data[0] & 0x80) >> 7
        vector = data[1]
        evd_cnt = data[2]
        s = glb_cfe[typ]
        if s:
            print(f" cfe: {s} IP: {ip_flag} vector: {vector}", end=' ')
        else:
            print(f" cfe: {typ} IP: {ip_flag} vector: {vector}", end=' ')
        pos = 4
        for _ in range(evd_cnt):
            if len(raw_buf) < pos + 16:
                break
            try:
                data = struct.unpack_from("<QQ", raw_buf, pos)
            except struct.error:
                return
            et = data[0] & 0x3f
            s = glb_evd[et]
            if s:
                print(f"{s}: {data[1]:#x}", end=' ')
            else:
                print(f"EVD_{et}: {data[1]:#x}", end=' ')
            pos += 16

    def print_iflag(self, raw_buf: bytes) -> None:
        """Print IFLAG data."""
        try:
            data = struct.unpack_from("<IQ", raw_buf)
        except struct.error:
            return
        iflag = data[0] & 1
        old_iflag = iflag ^ 1
        via_branch = data[0] & 2
        s = "via" if via_branch else "non"
        print(f"IFLAG: {old_iflag}->{iflag} {s} branch", end=' ')

    def common_start_str(self, comm: str, sample: perf.sample_event) -> str:
        """Return common start string for display."""
        ts = sample.sample_time
        cpu = sample.sample_cpu
        pid = sample.sample_pid
        tid = sample.sample_tid
        machine_pid = getattr(sample, "machine_pid", 0)
        if machine_pid:
            vcpu = getattr(sample, "vcpu", -1)
            return (f"VM:{machine_pid:5d} VCPU:{vcpu:03d} {comm:>16s} {pid:5d}/{tid:<5d} "
                    f"[{cpu:03d}] {ts // 1000000000:9d}.{ts % 1000000000:09d}  ")
        return (f"{comm:>16s} {pid:5d}/{tid:<5d} [{cpu:03d}] "
                f"{ts // 1000000000:9d}.{ts % 1000000000:09d}  ")

    def print_common_start(self, comm: str, sample: perf.sample_event, name: str) -> None:
        """Print common start info."""
        flags_disp = sample_flags_to_name(getattr(sample, "flags", 0))
        print(self.common_start_str(comm, sample) + f"{name:>8s}  {flags_disp:>21s}", end=' ')

    def print_instructions_start(self, comm: str, sample: perf.sample_event) -> None:
        """Print instructions start info."""
        raw_flags = getattr(sample, "flags", 0)
        if isinstance(raw_flags, int) and (raw_flags & (1 << 10)):
            print(self.common_start_str(comm, sample) + "x", end=' ')
        else:
            print(self.common_start_str(comm, sample), end='  ')

    def disassem(self, insn: bytes, ip: int) -> Tuple[int, str]:
        """Disassemble instruction using LibXED."""
        inst = self.inst if self.inst is not None else self.disassembler.instruction()
        is_64_bit = getattr(self.session, "is_64_bit", True) if self.session else True
        self.disassembler.set_mode(inst, 0 if is_64_bit else 1)
        buf = create_string_buffer(insn)
        return self.disassembler.disassemble_one(inst, addressof(buf), len(insn), ip)

    def print_common_ip(self, sample: perf.sample_event, symbol: str, dso: str) -> None:
        """Print IP and symbol info."""
        ip = sample.sample_ip
        offs = f"+{sample.sym_offset:#x}" if getattr(sample, "sym_offset", None) is not None else ""
        cyc_cnt = getattr(sample, "sample_cyc_count", 0)
        if cyc_cnt:
            insn_cnt = getattr(sample, "sample_insn_count", 0)
            ipc_str = f"  IPC: {insn_cnt / cyc_cnt:#.2f} ({insn_cnt}/{cyc_cnt})"
        else:
            ipc_str = ""

        if self.insn and self.disassembler is not None:
            try:
                insn = sample.insn()
            except AttributeError:
                insn = None
            if insn:
                cnt, text = self.disassem(insn, ip)
                byte_str = (f"{ip:x}").rjust(16)
                for k in range(cnt):
                    byte_str += f" {insn[k]:02x}"
                print(f"{byte_str:<40s}  {text:<30s}", end=' ')
            else:
                print(f"{ip:16x}", end=' ')
            print(f"{symbol}{offs} ({dso})", end=' ')
        else:
            print(f"{ip:16x} {symbol}{offs} ({dso})", end=' ')

        addr = getattr(sample, "sample_addr", getattr(sample, "addr", 0))
        addr_correlates_sym = (
            bool(addr)
            or getattr(sample, "addr_symbol", None) is not None
            or getattr(sample, "addr_dso", None) is not None
        )
        if addr_correlates_sym:
            addr_dso = (sample.addr_dso or '[unknown]')
            addr_symbol = (sample.addr_symbol or '[unknown]')
            addr_offs = (f"+{sample.addr_sym_offset:#x}"
                         if getattr(sample, "addr_sym_offset", None) is not None else "")
            print(f"=> {addr:x} {addr_symbol}{addr_offs} ({addr_dso}){ipc_str}")
        else:
            print(ipc_str)

    def print_srccode(self, comm: str, sample: perf.sample_event,
                      symbol: str, dso: str, with_insn: bool) -> None:
        """Print source code info."""
        ip = sample.sample_ip
        if symbol == "[unknown]":
            start_str = self.common_start_str(comm, sample) + (f"{ip:x}").rjust(16).ljust(40)
        else:
            offs = (f"+{sample.sym_offset:#x}"
                    if getattr(sample, "sym_offset", None) is not None else "")
            start_str = self.common_start_str(comm, sample) + (symbol + offs).ljust(40)

        if with_insn and self.insn and self.disassembler is not None:
            try:
                insn = sample.insn()
            except AttributeError:
                insn = None
            if insn:
                _, text = self.disassem(insn, ip)
                start_str += text.ljust(30)

        source_file_name: typing.Any = None
        line_number: typing.Any = 0
        source_line: typing.Any = None
        try:
            res_srcc = sample.srccode()
            if res_srcc:
                source_file_name, line_number, source_line = res_srcc
        except (AttributeError, ValueError, TypeError):
            pass

        if source_file_name:
            if self.line_number == line_number and self.source_file_name == source_file_name:
                src_str = ""
            else:
                if len(source_file_name) > 40:
                    src_file = ("..." + source_file_name[-37:]) + " "
                else:
                    src_file = source_file_name.ljust(41)
                if source_line is None:
                    src_str = src_file + str(line_number).rjust(4) + " <source not found>"
                else:
                    src_str = src_file + str(line_number).rjust(4) + " " + source_line
            self.dso = None
        elif dso == self.dso:
            src_str = ""
        else:
            src_str = dso
            self.dso = dso

        self.line_number = line_number
        self.source_file_name = source_file_name
        print(start_str, src_str)

    def do_process_event(self, sample: perf.sample_event) -> None:
        """Process event and print info."""
        cpu = getattr(sample, "sample_cpu", getattr(sample, "cpu", 0))
        if cpu in self.switch_str:
            print(self.switch_str[cpu])
            del self.switch_str[cpu]

        comm = "Unknown"
        if hasattr(self, 'session') and self.session:
            try:
                thread = self.session.find_thread(sample.sample_pid, sample.sample_tid)
                if thread:
                    comm = thread.comm() or "Unknown"
            except (OSError, ValueError, KeyError, RuntimeError, TypeError, AttributeError):
                pass
        # Python < 3.9 compatibility
        name = str(sample.evsel)
        if name.startswith("evsel("):
            name = name[6:-1]
        dso = (sample.dso or '[unknown]')
        symbol = (sample.symbol or '[unknown]')

        raw_buf = getattr(sample, 'raw_buf', b'') or b''

        if name.startswith("instructions"):
            if self.src:
                self.print_srccode(comm, sample, symbol, dso, True)
            else:
                self.print_instructions_start(comm, sample)
                self.print_common_ip(sample, symbol, dso)
        elif name.startswith("branches"):
            if self.src:
                self.print_srccode(comm, sample, symbol, dso, False)
            else:
                self.print_common_start(comm, sample, name)
                self.print_common_ip(sample, symbol, dso)
        elif name == "ptwrite":
            self.print_common_start(comm, sample, name)
            self.print_ptwrite(raw_buf)
            self.print_common_ip(sample, symbol, dso)
        elif name == "cbr":
            self.print_common_start(comm, sample, name)
            self.print_cbr(raw_buf)
            self.print_common_ip(sample, symbol, dso)
        elif name == "mwait":
            self.print_common_start(comm, sample, name)
            self.print_mwait(raw_buf)
            self.print_common_ip(sample, symbol, dso)
        elif name == "pwre":
            self.print_common_start(comm, sample, name)
            self.print_pwre(raw_buf)
            self.print_common_ip(sample, symbol, dso)
        elif name == "exstop":
            self.print_common_start(comm, sample, name)
            self.print_exstop(raw_buf)
            self.print_common_ip(sample, symbol, dso)
        elif name == "pwrx":
            self.print_common_start(comm, sample, name)
            self.print_pwrx(raw_buf)
            self.print_common_ip(sample, symbol, dso)
        elif name == "psb":
            self.print_common_start(comm, sample, name)
            self.print_psb(raw_buf)
            self.print_common_ip(sample, symbol, dso)
        elif name == "evt":
            self.print_common_start(comm, sample, name)
            self.print_evt(raw_buf)
            self.print_common_ip(sample, symbol, dso)
        elif name == "iflag":
            self.print_common_start(comm, sample, name)
            self.print_iflag(raw_buf)
            self.print_common_ip(sample, symbol, dso)
        else:
            self.print_common_start(comm, sample, name)
            self.print_common_ip(sample, symbol, dso)

    def interleave_events(self, sample: perf.sample_event) -> None:
        """Interleave output to avoid garbled lines from different CPUs."""
        self.cpu = sample.sample_cpu
        ts = sample.sample_time

        if self.time != ts:
            self.time = ts
            self.flush_stashed_output()

        self.output_pos = 0
        with contextlib.redirect_stdout(io.StringIO()) as self.output:
            self.do_process_event(sample)

        self.stash_output()

    def stash_output(self) -> None:
        """Stash output for later flushing."""
        output_str = self.output.getvalue()[self.output_pos:]
        n = len(output_str)
        if n:
            self.output_pos += n
            if self.cpu not in self.stash_dict:
                self.stash_dict[self.cpu] = []
            self.stash_dict[self.cpu].append(output_str)
            if len(self.stash_dict[self.cpu]) > 1000:
                self.flush_stashed_output()

    def flush_stashed_output(self) -> None:
        """Flush stashed output."""
        while self.stash_dict:
            cpus = list(self.stash_dict.keys())
            for cpu in cpus:
                items = self.stash_dict[cpu]
                countdown = self.args.interleave
                while len(items) and countdown:
                    sys.stdout.write(items[0])
                    del items[0]
                    countdown -= 1
                if not items:
                    del self.stash_dict[cpu]

    def process_context_switch(self, event: perf.switch_event) -> None:
        """Process context switch."""
        cpu = getattr(event, "sample_cpu", getattr(event, "cpu", 0))
        pid = getattr(event, "sample_pid", getattr(event, "pid", 0))
        tid = getattr(event, "sample_tid", getattr(event, "tid", 0))
        ts = getattr(event, "sample_time", getattr(event, "time", 0))
        np_pid = getattr(event, "next_prev_pid", None)
        np_tid = getattr(event, "next_prev_tid", None)
        machine_pid = getattr(event, "machine_pid", -1)
        vcpu = getattr(event, "vcpu", -1)
        misc = getattr(event, "misc", 0)
        out = bool(misc & (1 << 13))
        out_preempt = bool(misc & (1 << 14))

        if self.args.interleave:
            self.flush_stashed_output()

        if out:
            out_str = "Switch out "
        else:
            out_str = "Switch In  "

        preempt_str = "preempt" if out_preempt else ""

        if machine_pid == -1:
            machine_str = ""
        elif vcpu == -1:
            machine_str = f"machine PID {machine_pid}"
        else:
            machine_str = f"machine PID {machine_pid} VCPU {vcpu}"

        np_str = f"{np_pid:5d}/{np_tid:<5d} " if np_pid is not None and np_tid is not None else ""
        c_str = (f"{out_str:>16s} {pid:5d}/{tid:<5d} [{cpu:03d}] "
                 f"{ts // 1000000000:9d}.{ts % 1000000000:09d} "
                 f"{np_str}{machine_str} {preempt_str}")

        if self.args.all_switch_events:
            print(c_str)
        else:
            self.switch_str[cpu] = c_str
    def process_event(self, sample: perf.sample_event) -> None:
        """Wrapper to handle interleaving and exceptions."""
        try:
            if self.args.interleave:
                self.interleave_events(sample)
            else:
                self.do_process_event(sample)
        except BrokenPipeError:
            # Stop python printing broken pipe errors and traceback
            sys.stdout = open(os.devnull, 'w', encoding='utf-8')
            sys.exit(1)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("-i", "--input", default="perf.data", help="Input file name")
    ap.add_argument("--insn-trace", action='store_true')
    ap.add_argument("--src-trace", action='store_true')
    ap.add_argument("--all-switch-events", action='store_true')
    ap.add_argument("--interleave", type=int, nargs='?', const=4, default=0)
    ap.add_argument("--itrace", default=os.environ.get("PERF_ITRACE"), help="itrace options")
    args = ap.parse_args()

    if args.itrace is None:
        if args.insn_trace or args.src_trace:
            args.itrace = "i0nsepwxI"
        else:
            args.itrace = "bepwxI"
    analyzer = IntelPTAnalyzer(args)

    try:
        # Note: Python API currently lacks auxtrace_error callbacks affecting
        # chronological interleaving
        try:
            analyzer.session = perf.session(
                perf.data(args.input),
                sample=analyzer.process_event,
                context_switch=analyzer.process_context_switch,
                itrace=args.itrace
            )
            try:
                analyzer.session.process_events()
            except KeyboardInterrupt:
                pass
        finally:
            # Break the reference cycle between analyzer.session and the bound
            # analyzer.process_event / process_context_switch callbacks so the
            # underlying C perf.session object is freed.
            analyzer.session = None
        if args.interleave:
            analyzer.flush_stashed_output()
        print("End")
    except BrokenPipeError:
        sys.exit(0)
    except (OSError, ValueError, KeyError, RuntimeError, TypeError, AttributeError):
        import traceback
        traceback.print_exc()
        sys.exit(1)
