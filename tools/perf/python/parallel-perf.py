#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Parallel perf script."""
#
# run a perf script command multiple times in parallel, using perf script
# options --cpu and --time so that each job processes a different chunk
# of the data.
#
# Copyright (c) 2024, Intel Corporation.

import subprocess
import argparse
import pathlib
import shlex
import time
from typing import Any
import copy
import sys
import os
import re

glb_prog_name = "parallel-perf.py"
glb_min_interval = 10.0
glb_min_samples = 64


class Verbosity():

    def __init__(self, quiet: bool = False, verbose: bool = False,
                 debug: bool = False) -> None:
        """__init__."""

        self.normal = True
        self.verbose = verbose
        self.debug = debug
        self.self_test = True
        if self.debug:
            self.verbose = True
        if self.verbose:
            quiet = False
        if quiet:
            self.normal = False

# Manage work (start/wait/kill), as represented by a subprocess.Popen command


class Work():

    def __init__(self, cmd: list[str], pipe_to: str,
                 output_dir: str = ".") -> None:
        """__init__."""

        self.popen: Any = None
        self.consumer: Any = None
        self.cmd = cmd
        self.pipe_to = pipe_to
        self.output_dir = output_dir
        self.cmdout_name = f"{output_dir}/cmd.txt"
        self.stdout_name = f"{output_dir}/out.txt"
        self.stderr_name = f"{output_dir}/err.txt"

    def command(self):
        """command."""

        return " ".join(shlex.quote(x) for x in self.cmd)

    def stdout(self):
        """stdout."""

        return open(self.stdout_name, "w", encoding="utf-8")

    def stderr(self):
        """stderr."""

        return open(self.stderr_name, "w", encoding="utf-8")

    def create_output_dir(self):
        """create_output_dir."""

        pathlib.Path(self.output_dir).mkdir(parents=True, exist_ok=True)

    def start(self):
        """start."""

        if self.popen:
            return
        self.create_output_dir()
        with open(self.cmdout_name, "w", encoding="utf-8") as f:
            f.write(self.command())
            f.write("\n")
        stdout = self.stdout()
        stderr = self.stderr()
        if self.pipe_to:
            self.popen = subprocess.Popen(
                self.cmd, stdout=subprocess.PIPE, stderr=stderr)
            args = shlex.split(self.pipe_to)
            self.consumer = subprocess.Popen(
                args, stdin=self.popen.stdout, stdout=stdout, stderr=stderr)
            # The consumer now owns the read end of the pipe. Close this
            # process's copy so that the producer receives SIGPIPE if the
            # consumer exits early, and so that the descriptor isn't leaked
            # for the lifetime of this Work.
            self.popen.stdout.close()
            self.popen.stdout = None
        else:
            self.popen = subprocess.Popen(
                self.cmd, stdout=stdout, stderr=stderr)

    def remove_empty_err_file(self):
        """remove_empty_err_file."""

        if os.path.exists(self.stderr_name):
            if os.path.getsize(self.stderr_name) == 0:
                os.unlink(self.stderr_name)

    def errors(self):
        """errors."""

        if os.path.exists(self.stderr_name):
            if os.path.getsize(self.stderr_name) != 0:
                return [f"Non-empty error file {self.stderr_name}"]
        return []

    def tidy_up(self):
        """tidy_up."""

        self.remove_empty_err_file()

    def raw_poll_wait(self, p, wait):
        """raw_poll_wait."""

        if wait:
            return p.wait()
        return p.poll()

    def poll(self, wait=False):
        """poll."""

        if not self.popen:
            return None
        result = self.raw_poll_wait(self.popen, wait)
        if self.consumer:
            res = result
            result = self.raw_poll_wait(self.consumer, wait)
            if result is not None and res is None:
                self.popen.kill()
                result = None
            elif result == 0 and res is not None and res != 0:
                result = res
        if result is not None:
            self.tidy_up()
        return result

    def wait(self):
        """wait."""

        return self.poll(wait=True)

    def kill(self):
        """kill."""

        if not self.popen:
            return
        self.popen.kill()
        if self.consumer:
            self.consumer.kill()


def kill_work(worklist, _verbosity):
    """kill_work."""

    for w in worklist:
        w.kill()
    for w in worklist:
        w.wait()


def number_of_cp_us():
    """number_of_cp_us."""

    return os.sysconf("SC_NPROCESSORS_ONLN")


def nano_secs_to_secs_str(x):
    """nano_secs_to_secs_str."""

    if x is None:
        return ""
    x = str(x)
    if len(x) < 10:
        x = "0" * (10 - len(x)) + x
    return x[:len(x) - 9] + "." + x[-9:]


def insert_option_after(cmd, option, after):
    """insert_option_after."""

    try:
        pos = cmd.index(after)
        cmd.insert(pos + 1, option)
    except (OSError, ValueError, RuntimeError):
        cmd.append(option)


def create_work_list(cmd, pipe_to, output_dir, cpus, time_ranges_by_cpu):
    """create_work_list."""

    max_len = len(str(cpus[-1]))
    cpu_dir_fmt = f"cpu-%.{max_len}u"
    worklist = []
    pos = 0
    for cpu in cpus:
        if cpu >= 0:
            cpu_dir = os.path.join(output_dir, cpu_dir_fmt % cpu)
            cpu_option = f"--cpu={cpu}"
        else:
            cpu_dir = output_dir
            cpu_option = None

        tr_dir_fmt = "time-range"

        if len(time_ranges_by_cpu) > 1:
            time_ranges = time_ranges_by_cpu[pos]
            tr_dir_fmt += f"-{pos}"
            pos += 1
        else:
            time_ranges = time_ranges_by_cpu[0]

        max_len = len(str(len(time_ranges)))
        tr_dir_fmt += f"-%.{max_len}u"

        i = 0
        for r in time_ranges:
            if r == [None, None]:
                time_option = None
                work_output_dir = cpu_dir
            else:
                time_option = "--time=" + \
                    nano_secs_to_secs_str(r[0]) + "," + \
                    nano_secs_to_secs_str(r[1])
                work_output_dir = os.path.join(cpu_dir, tr_dir_fmt % i)
                i += 1
            work_cmd = list(cmd)
            if time_option is not None:
                insert_option_after(work_cmd, time_option, "script")
            if cpu_option is not None:
                insert_option_after(work_cmd, cpu_option, "script")
            w = Work(work_cmd, pipe_to, work_output_dir)
            worklist.append(w)
    return worklist


def do_run_work(worklist: list[Work], nr_jobs: int,
                _verbosity: Verbosity) -> bool:
    """do_run_work."""

    nr_to_do = len(worklist)
    not_started = list(worklist)
    running: list[Work] = []
    done: list[Work] = []
    chg = False
    while True:
        nr_done = len(done)
        if chg and _verbosity.normal:
            nr_run = len(running)
            print(
                f"\rThere are {nr_to_do} jobs: {nr_done} completed, {nr_run} running",
                flush=True, end=" ")
            if _verbosity.verbose:
                print()
            chg = False
        if nr_done == nr_to_do:
            break
        while len(running) < nr_jobs and len(not_started):
            w = not_started.pop(0)
            running.append(w)
            if _verbosity.verbose:
                print("Starting:", w.command())
            w.start()
            chg = True
        if len(running):
            time.sleep(0.1)
        finished: list[Work] = []
        not_finished: list[Work] = []
        while len(running):
            w = running.pop(0)
            r = w.poll()
            if r is None:
                not_finished.append(w)
                continue
            if r == 0:
                if _verbosity.verbose:
                    print("Finished:", w.command())
                finished.append(w)
                chg = True
                continue
            if _verbosity.normal and not _verbosity.verbose:
                print()
            print("Job failed!\n    return code:", r,
                  "\n    command:    ", w.command())
            if w.pipe_to:
                print("    piped to:   ", w.pipe_to)
            print("Killing outstanding jobs")
            kill_work(not_finished, _verbosity)
            kill_work(running, _verbosity)
            return False
        running = not_finished
        done += finished
    errorlist: list[str] = []
    for w in worklist:
        errorlist += w.errors()
    if len(errorlist):
        print("errors:")
        for e in errorlist:
            print(e)
    elif _verbosity.normal:
        print("\r", " "*50, "\rAll jobs finished successfully", flush=True)
    return True


def run_work(worklist: list[Work], nr_jobs: int = number_of_cp_us(),
             _verbosity: Verbosity = Verbosity()) -> bool:
    """run_work."""
    try:
        return do_run_work(worklist, nr_jobs, _verbosity)
    except BaseException:
        for w in worklist:
            w.kill()
        raise


def read_header(perf, file_name):
    """read_header."""

    cmd = [perf, "script", "--header-only", "--input", file_name]
    with subprocess.Popen(cmd, stdout=subprocess.PIPE) as proc:
        out = proc.stdout.read() if proc.stdout else b""
    return out.decode("utf-8")


def parse_header(hdr):
    """parse_header."""

    result = {}
    lines = hdr.split("\n")
    for line in lines:
        if ":" in line and line[0] == "#":
            pos = line.index(":")
            name = line[1:pos-1].strip()
            value = line[pos+1:].strip()
            if name in result:
                orig_name = name
                nr = 2
                while True:
                    name = f"{orig_name} {nr}"
                    if name not in result:
                        break
                    nr += 1
            result[name] = value
    return result


def header_field(hdr_dict, hdr_fld):
    """header_field."""

    if hdr_fld not in hdr_dict:
        raise RuntimeError(f"'{hdr_fld}' missing from header information")
    return hdr_dict[hdr_fld]

# Represent the position of an option within a command string
# and provide the option value and/or remove the option


class OptPos():

    def init(self, opt_element=-1, value_element=-1, opt_pos=-1, value_pos=-1, error=None):
        """init."""

        self.opt_element = opt_element		# list element that contains option
        self.value_element = value_element  # list element that contains option value
        self.opt_pos = opt_pos			# string position of option
        self.value_pos = value_pos		# string position of value
        self.error = error			# error message string

    def __init__(self, args, short_name, long_name, default=None):
        """__init__."""

        self.args = list(args)
        self.default = default
        n = 2 + len(long_name)
        m = len(short_name)
        pos = -1
        for opt in args:
            pos += 1
            if m and opt[:2] == f"-{short_name}":
                if len(opt) == 2:
                    if pos + 1 < len(args):
                        self.init(pos, pos + 1, 0, 0)
                    else:
                        self.init(error=f"-{short_name} option missing value")
                else:
                    self.init(pos, pos, 0, 2)
                return
            if opt[:n] == f"--{long_name}":
                if len(opt) == n:
                    if pos + 1 < len(args):
                        self.init(pos, pos + 1, 0, 0)
                    else:
                        self.init(error=f"--{long_name} option missing value")
                elif opt[n] == "=":
                    self.init(pos, pos, 0, n + 1)
                else:
                    self.init(error=f"--{long_name} option expected '='")
                return
            if m and opt[:1] == "-" and opt[:2] != "--" and short_name in opt:
                ipos = opt.index(short_name)
                if "-" in opt[1:]:
                    hpos = opt[1:].index("-")
                    if hpos < ipos:
                        continue
                if ipos + 1 == len(opt):
                    if pos + 1 < len(args):
                        self.init(pos, pos + 1, ipos, 0)
                    else:
                        self.init(error=f"-{short_name} option missing value")
                else:
                    self.init(pos, pos, ipos, ipos + 1)
                return
        self.init()

    def value(self):
        """value."""

        if self.opt_element >= 0:
            if self.opt_element != self.value_element:
                return self.args[self.value_element]
            else:
                return self.args[self.value_element][self.value_pos:]
        return self.default

    def remove(self, args):
        """remove."""

        if self.opt_element == -1:
            return
        if self.opt_element != self.value_element:
            del args[self.value_element]
        if self.opt_pos:
            args[self.opt_element] = args[self.opt_element][:self.opt_pos]
        else:
            del args[self.opt_element]


def determine_input_file_name(cmd):
    """determine_input_file_name."""

    p = OptPos(cmd, "i", "input", "perf.data")
    if p.error:
        raise RuntimeError(f"perf command {p.error}")
    file_name = p.value()
    if not os.path.exists(file_name):
        raise RuntimeError(f"perf command input file '{file_name}' not found")
    return file_name


def read_option(args, short_name, long_name, err_prefix, remove=False):
    """read_option."""

    p = OptPos(args, short_name, long_name)
    if p.error:
        raise RuntimeError(f"{err_prefix}{p.error}")
    value = p.value()
    if remove:
        p.remove(args)
    return value


def extract_option(args, short_name, long_name, err_prefix):
    """extract_option."""

    return read_option(args, short_name, long_name, err_prefix, True)


def read_perf_option(args, short_name, long_name):
    """read_perf_option."""

    return read_option(args, short_name, long_name, "perf command ")


def extract_perf_option(args, short_name, long_name):
    """extract_perf_option."""

    return extract_option(args, short_name, long_name, "perf command ")


def perf_double_quick_commands(cmd, file_name):
    """perf_double_quick_commands."""

    cpu_str = read_perf_option(cmd, "C", "cpu")
    time_str = read_perf_option(cmd, "", "time")
    # Use double-quick sampling to determine trace data density
    times_cmd = ["perf", "script", "--ns",
                 "--input", file_name, "--itrace=qqi"]
    if cpu_str is not None and cpu_str != "":
        times_cmd.append(f"--cpu={cpu_str}")
    if time_str is not None and time_str != "":
        times_cmd.append(f"--time={time_str}")
    cnts_cmd = list(times_cmd)
    cnts_cmd.append("-Fcpu")
    times_cmd.append("-Fcpu,time")
    return cnts_cmd, times_cmd


class CPUTimeRange():
    def __init__(self, cpu):
        """__init__."""

        self.cpu = cpu
        self.sample_cnt = 0
        self.time_ranges = None
        self.interval = 0
        self.interval_remaining = 0
        self.remaining = 0
        self.tr_pos = 0


def calc_time_ranges_by_cpu(line, cpu, cpu_time_ranges, max_time):
    """calc_time_ranges_by_cpu."""

    cpu_time_range = cpu_time_ranges[cpu]
    cpu_time_range.remaining -= 1
    cpu_time_range.interval_remaining -= 1
    if cpu_time_range.remaining == 0:
        cpu_time_range.time_ranges[cpu_time_range.tr_pos][1] = max_time
        return
    if cpu_time_range.interval_remaining == 0:
        ts = time_val(line[1][:-1], 0)
        time_ranges = cpu_time_range.time_ranges
        time_ranges[cpu_time_range.tr_pos][1] = ts - 1
        time_ranges.append([ts, max_time])
        cpu_time_range.tr_pos += 1
        cpu_time_range.interval_remaining = cpu_time_range.interval


def count_samples_by_cpu(_line: list[str], cpu: int,
                         cpu_time_ranges: list[Any]) -> None:
    """count_samples_by_cpu."""

    try:
        cpu_time_ranges[cpu].sample_cnt += 1
    except (OSError, ValueError, RuntimeError, IndexError):
        print("exception")
        print("cpu", cpu)
        print("len(cpu_time_ranges)", len(cpu_time_ranges))
        raise


def process_command_output_lines(cmd, per_cpu, fn, *x):
    """process_command_output_lines."""

    # Assume CPU number is at beginning of line and enclosed by []
    pat = re.compile(r"\s*\[[0-9]+\]")
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    while True:
        line = p.stdout.readline()
        if line:
            line = line.decode("utf-8")
            if pat.match(line):
                line = line.split()
                if per_cpu:
                    # Assumes CPU number is enclosed by []
                    cpu = int(line[0][1:-1])
                else:
                    cpu = 0
                fn(line, cpu, *x)
        else:
            break
    p.wait()


def intersect_time_ranges(new_time_ranges, time_ranges):
    """intersect_time_ranges."""

    pos = 0
    new_pos = 0
    # Can assume len(time_ranges) != 0 and len(new_time_ranges) != 0
    # Note also, there *must* be at least one intersection.
    while pos < len(time_ranges) and new_pos < len(new_time_ranges):
        # new end < old start => no intersection, remove new
        if new_time_ranges[new_pos][1] < time_ranges[pos][0]:
            del new_time_ranges[new_pos]
            continue
        # new start > old end => no intersection, check next
        if new_time_ranges[new_pos][0] > time_ranges[pos][1]:
            pos += 1
            if pos < len(time_ranges):
                continue
            # no next, so remove remaining
            while new_pos < len(new_time_ranges):
                del new_time_ranges[new_pos]
            return
        # Found an intersection
        # new start < old start => adjust new start = old start
        if new_time_ranges[new_pos][0] < time_ranges[pos][0]:
            new_time_ranges[new_pos][0] = time_ranges[pos][0]
        # new end > old end => keep the overlap, insert the remainder
        if new_time_ranges[new_pos][1] > time_ranges[pos][1]:
            r = [time_ranges[pos][1] + 1, new_time_ranges[new_pos][1]]
            new_time_ranges[new_pos][1] = time_ranges[pos][1]
            new_pos += 1
            new_time_ranges.insert(new_pos, r)
            continue
        # new [start, end] is within old [start, end]
        new_pos += 1


def split_time_ranges_by_trace_data_density(
    time_ranges, cpus, nr, cmd, file_name, per_cpu, min_size, min_interval, _verbosity
):
    """split_time_ranges_by_trace_data_density."""

    if _verbosity.normal:
        print("\rAnalyzing...", flush=True, end=" ")
        if _verbosity.verbose:
            print()
    cnts_cmd, times_cmd = perf_double_quick_commands(cmd, file_name)

    nr_cpus = cpus[-1] + 1 if per_cpu else 1
    if per_cpu:
        nr_cpus = cpus[-1] + 1
        cpu_time_ranges = [CPUTimeRange(cpu) for cpu in range(nr_cpus)]
    else:
        nr_cpus = 1
        cpu_time_ranges = [CPUTimeRange(-1)]

    if _verbosity.debug:
        print("nr_cpus", nr_cpus)
        print("cnts_cmd", cnts_cmd)
        print("times_cmd", times_cmd)

    # Count the number of "double quick" samples per CPU
    process_command_output_lines(
        cnts_cmd, per_cpu, count_samples_by_cpu, cpu_time_ranges)

    tot = 0
    mx = 0
    for cpu_time_range in cpu_time_ranges:
        cnt = cpu_time_range.sample_cnt
        tot += cnt
        if cnt > mx:
            mx = cnt
        if _verbosity.debug:
            print("cpu:", cpu_time_range.cpu, "sample_cnt", cnt)

    if min_size < 1:
        min_size = 1

    if mx < min_size:
        # Too little data to be worth splitting
        if _verbosity.debug:
            print("Too little data to split by time")
        if nr == 0:
            nr = 1
        return [split_time_ranges_into_n(time_ranges, nr, min_interval)]

    if nr:
        divisor = nr
        min_size = 1
    else:
        divisor = number_of_cp_us()

    interval = int(round(tot / divisor, 0))
    if interval < min_size:
        interval = min_size

    if _verbosity.debug:
        print("divisor", divisor)
        print("min_size", min_size)
        print("interval", interval)

    min_time = time_ranges[0][0]
    max_time = time_ranges[-1][1]

    for cpu_time_range in cpu_time_ranges:
        cnt = cpu_time_range.sample_cnt
        if cnt == 0:
            cpu_time_range.time_ranges = copy.deepcopy(time_ranges)
            continue
        # Adjust target interval for CPU to give approximately equal interval sizes
        # Determine number of intervals, rounding to nearest integer
        n = int(round(cnt / interval, 0))
        if n < 1:
            n = 1
        # Determine interval size, rounding up
        d, m = divmod(cnt, n)
        if m:
            d += 1
        cpu_time_range.interval = d
        cpu_time_range.interval_remaining = d
        cpu_time_range.remaining = cnt
        # init. time ranges for each CPU with the start time
        cpu_time_range.time_ranges = [[min_time, max_time]]

    # Set time ranges so that the same number of "double quick" samples
    # will fall into each time range.
    process_command_output_lines(
        times_cmd, per_cpu, calc_time_ranges_by_cpu, cpu_time_ranges, max_time)

    for cpu_time_range in cpu_time_ranges:
        if cpu_time_range.sample_cnt:
            intersect_time_ranges(cpu_time_range.time_ranges, time_ranges)

    return [cpu_time_ranges[cpu].time_ranges for cpu in cpus]


def split_single_time_range_into_n(time_range, n):
    """split_single_time_range_into_n."""

    if n <= 1:
        return [time_range]
    start = time_range[0]
    end = time_range[1]
    duration = int((end - start + 1) / n)
    if duration < 1:
        return [time_range]
    time_ranges = []
    for _i in range(n):
        time_ranges.append([start, start + duration - 1])
        start += duration
    time_ranges[-1][1] = end
    return time_ranges


def time_range_duration(r):
    """time_range_duration."""

    return r[1] - r[0] + 1


def total_duration(time_ranges):
    """total_duration."""

    duration = 0
    for r in time_ranges:
        duration += time_range_duration(r)
    return duration


def split_time_ranges_by_interval(time_ranges, interval):
    """split_time_ranges_by_interval."""

    new_ranges = []
    for r in time_ranges:
        duration = time_range_duration(r)
        n = duration / interval
        n = int(round(n, 0))
        new_ranges += split_single_time_range_into_n(r, n)
    return new_ranges


def split_time_ranges_into_n(time_ranges, n, min_interval):
    """split_time_ranges_into_n."""

    if n <= len(time_ranges):
        return time_ranges
    duration = total_duration(time_ranges)
    interval = duration / n
    if interval < min_interval:
        interval = min_interval
    return split_time_ranges_by_interval(time_ranges, interval)


def recombine_time_ranges(tr):
    """recombine_time_ranges."""

    new_tr = copy.deepcopy(tr)
    i = 1
    while i < len(new_tr):
        # if prev end + 1 == cur start, combine them
        if new_tr[i - 1][1] + 1 == new_tr[i][0]:
            new_tr[i][0] = new_tr[i - 1][0]
            del new_tr[i - 1]
        else:
            i += 1
    return new_tr


def open_time_range_ends(time_ranges, min_time, max_time):
    """open_time_range_ends."""

    if time_ranges[0][0] <= min_time:
        time_ranges[0][0] = None
    if time_ranges[-1][1] >= max_time:
        time_ranges[-1][1] = None


def bad_time_str(time_str):
    """bad_time_str."""

    raise RuntimeError(
        f"perf command bad time option: '{time_str}'\n"
        "Check also 'time of first sample' and 'time of last sample' "
        "in perf script --header-only"
    )


def validate_time_ranges(time_ranges, time_str):
    """validate_time_ranges."""

    n = len(time_ranges)
    for i in range(n):
        start = time_ranges[i][0]
        end = time_ranges[i][1]
        if i != 0 and start <= time_ranges[i - 1][1]:
            bad_time_str(time_str)
        if start > end:
            bad_time_str(time_str)


def time_val(s, dflt):
    """time_val."""

    s = s.strip()
    if s == "":
        return dflt
    a = s.split(".")
    if len(a) > 2:
        raise RuntimeError(f"Bad time value'{s}'")
    x = int(a[0])
    if x < 0:
        raise RuntimeError("Negative time not allowed")
    x *= 1000000000
    if len(a) > 1:
        x += int((a[1] + "000000000")[:9])
    return x


def bad_cpu_str(cpu_str):
    """bad_cpu_str."""

    raise RuntimeError(
        f"perf command bad cpu option: '{cpu_str}'\n"
        "Check also 'nrcpus avail' in perf script --header-only"
    )


def parse_time_str(time_str, min_time, max_time):
    """parse_time_str."""

    if time_str is None or time_str == "":
        return [[min_time, max_time]]
    time_ranges = []
    for r in time_str.split():
        a = r.split(",")
        if len(a) != 2:
            bad_time_str(time_str)
        try:
            start = time_val(a[0], min_time)
            end = time_val(a[1], max_time)
        except (OSError, ValueError, RuntimeError):
            bad_time_str(time_str)
        time_ranges.append([start, end])
    validate_time_ranges(time_ranges, time_str)
    return time_ranges


def parse_cpu_str(cpu_str, nr_cpus):
    """parse_cpu_str."""

    if cpu_str is None or cpu_str == "":
        return [-1]
    cpus = []
    for r in cpu_str.split(","):
        a = r.split("-")
        if len(a) < 1 or len(a) > 2:
            bad_cpu_str(cpu_str)
        try:
            start = int(a[0].strip())
            if len(a) > 1:
                end = int(a[1].strip())
            else:
                end = start
        except (OSError, ValueError, RuntimeError):
            bad_cpu_str(cpu_str)
        if start < 0 or end < 0 or end < start or end >= nr_cpus:
            bad_cpu_str(cpu_str)
        cpus.extend(range(start, end + 1))
    cpus = list(set(cpus))  # remove duplicates
    cpus.sort()
    return cpus


class ParallelPerf():

    def __init__(self, a):
        """init."""
        self.nr = 0
        self.jobs = 0
        self.file_name = None
        self.hdr = None
        self.hdr_dict = None
        self.cmd_line = None
        self.min_time = None
        self.max_time = None
        self.time_str = None
        self.time_ranges = None
        self.cpu_str = None
        self.cpus = None
        self.split_time_ranges_for_each_cpu = None
        self.worklist = None
        self.per_cpu = None
        self.interval = None
        self.min_size = None
        self.min_interval = None
        self.pipe_to = None
        self.output_dir = None
        self.cmd = getattr(a, 'cmd', None)
        self.no_per_cpu = getattr(a, 'no_per_cpu', False)
        self.dry_run = getattr(a, 'dry_run', False)
        self._verbosity = getattr(a, "_verbosity", Verbosity())

        for arg_name in vars(a):
            setattr(self, arg_name, getattr(a, arg_name))
        self.orig_nr = self.nr
        self.orig_cmd = list(self.cmd)
        self.perf = self.cmd[0]
        if os.path.exists(self.output_dir):
            raise RuntimeError(f"Output '{self.output_dir}' already exists")
        if self.jobs < 0 or self.nr < 0 or self.interval < 0:
            raise RuntimeError(
                "Bad options (negative values): try -h option for help")
        if self.nr != 0 and self.interval != 0:
            raise RuntimeError(
                "Cannot specify number of time subdivisions and time interval")
        if self.jobs == 0:
            self.jobs = number_of_cp_us()
        if self.nr == 0 and self.interval == 0:
            if self.per_cpu:
                self.nr = 1
            else:
                self.nr = self.jobs

    def init(self):
        """init."""

        if self._verbosity.debug:
            print("cmd", self.cmd)
        self.file_name = determine_input_file_name(self.cmd)
        self.hdr = read_header(self.perf, self.file_name)
        self.hdr_dict = parse_header(self.hdr)
        self.cmd_line = header_field(self.hdr_dict, "cmdline")

    def extract_time_info(self):
        """extract_time_info."""

        self.min_time = time_val(header_field(
            self.hdr_dict, "time of first sample"), 0)
        self.max_time = time_val(header_field(
            self.hdr_dict, "time of last sample"), 0)
        self.time_str = extract_perf_option(self.cmd, "", "time")
        self.time_ranges = parse_time_str(
            self.time_str, self.min_time, self.max_time)
        if self._verbosity.debug:
            print("time_ranges", self.time_ranges)

    def extract_cpu_info(self):
        """extract_cpu_info."""

        if self.per_cpu:
            nr_cpus = int(header_field(self.hdr_dict, "nrcpus avail"))
            self.cpu_str = extract_perf_option(self.cmd, "C", "cpu")
            if self.cpu_str is None or self.cpu_str == "":
                self.cpus = [x for x in range(nr_cpus)]
            else:
                self.cpus = parse_cpu_str(self.cpu_str, nr_cpus)
        else:
            self.cpu_str = None
            self.cpus = [-1]
        if self._verbosity.debug:
            print("cpus", self.cpus)

    def is_intel_pt(self):
        """is_intel_pt."""

        return self.cmd_line.find("intel_pt") >= 0

    def split_time_ranges(self):
        """split_time_ranges."""

        if self.is_intel_pt() and self.interval == 0:
            self.split_time_ranges_for_each_cpu = (
                split_time_ranges_by_trace_data_density(
                    self.time_ranges, self.cpus, self.orig_nr,
                    self.orig_cmd, self.file_name, self.per_cpu,
                    self.min_size, self.min_interval, self._verbosity
                )
            )
        elif self.nr:
            self.split_time_ranges_for_each_cpu = [split_time_ranges_into_n(
                self.time_ranges, self.nr, self.min_interval)]
        else:
            self.split_time_ranges_for_each_cpu = [
                split_time_ranges_by_interval(self.time_ranges, self.interval)]

    def check_time_ranges(self):
        """check_time_ranges."""

        for tr in self.split_time_ranges_for_each_cpu:
            # Re-combined time ranges should be the same
            new_tr = recombine_time_ranges(tr)
            if new_tr != self.time_ranges:
                if self._verbosity.debug:
                    print("tr", tr)
                    print("new_tr", new_tr)
                raise RuntimeError("Self test failed!")

    def open_time_range_ends(self):
        """open_time_range_ends."""

        for time_ranges in self.split_time_ranges_for_each_cpu:
            open_time_range_ends(time_ranges, self.min_time, self.max_time)

    def create_work_list(self):
        """create_work_list."""

        self.worklist = create_work_list(
            self.cmd, self.pipe_to, self.output_dir, self.cpus, self.split_time_ranges_for_each_cpu)

    def perf_data_recorded_per_cpu(self):
        """perf_data_recorded_per_cpu."""

        if "--per-thread" in self.cmd_line.split():
            return False
        return True

    def default_to_per_cpu(self):
        """default_to_per_cpu."""

        # --no-per-cpu option takes precedence
        if self.no_per_cpu:
            return False
        if not self.perf_data_recorded_per_cpu():
            return False
        # Default to per-cpu for Intel PT data that was recorded per-cpu,
        # because decoding can be done for each CPU separately.
        if self.is_intel_pt():
            return True
        return False

    def config(self):
        """config."""

        self.init()
        self.extract_time_info()
        if not self.per_cpu:
            self.per_cpu = self.default_to_per_cpu()
        if self._verbosity.debug:
            print("per_cpu", self.per_cpu)
        self.extract_cpu_info()
        self.split_time_ranges()
        if self._verbosity.self_test:
            self.check_time_ranges()
        # Prefer open-ended time range to starting / ending with min_time / max_time resp.
        self.open_time_range_ends()
        self.create_work_list()

    def run(self):
        """run."""

        if self.dry_run:
            print(len(self.worklist), "jobs:")
            for w in self.worklist:
                print(w.command())
            return True
        result = run_work(self.worklist, self.jobs, _verbosity=self._verbosity)
        if self._verbosity.verbose:
            print(glb_prog_name, "done")
        return result


def run_parallel_perf(a):
    """run_parallel_perf."""

    pp = ParallelPerf(a)
    pp.config()
    return pp.run()


def main(args):
    """main."""

    ap = argparse.ArgumentParser(
            prog=glb_prog_name, formatter_class=argparse.RawDescriptionHelpFormatter,
            description="""
run a perf script command multiple times in parallel, using perf script options
--cpu and --time so that each job processes a different chunk of the data.
""",
            epilog="""
Follow the options by '--' and then the perf script command e.g.

	$ perf record -a -- sleep 10
	$ parallel-perf.py --nr=4 -- perf script --ns
	All jobs finished successfully
	$ tree parallel-perf-output/
	parallel-perf-output/
	├── time-range-0
	│   ├── cmd.txt
	│   └── out.txt
	├── time-range-1
	│   ├── cmd.txt
	│   └── out.txt
	├── time-range-2
	│   ├── cmd.txt
	│   └── out.txt
	└── time-range-3
	    ├── cmd.txt
	    └── out.txt
	$ find parallel-perf-output -name cmd.txt | sort | xargs grep -H .
	parallel-perf-output/time-range-0/cmd.txt:perf script --time=,9466.504461499 --ns
	parallel-perf-output/time-range-1/cmd.txt:perf script --time=9466.504461500,9469.005396999 --ns
	parallel-perf-output/time-range-2/cmd.txt:perf script --time=9469.005397000,9471.506332499 --ns
	parallel-perf-output/time-range-3/cmd.txt:perf script --time=9471.506332500, --ns

Any perf script command can be used, including the use of perf script options
--dlfilter and --script, so that the benefit of running parallel jobs
naturally extends to them also.

If option --pipe-to is used, standard output is first piped through that
command. Beware, if the command fails (e.g. grep with no matches), it will be
considered a fatal error.

Final standard output is redirected to files named out.txt in separate
subdirectories under the output directory. Similarly, standard error is
written to files named err.txt. In addition, files named cmd.txt contain the
corresponding perf script command. After processing, err.txt files are removed
if they are empty.

If any job exits with a non-zero exit code, then all jobs are killed and no
more are started. A message is printed if any job results in a non-empty
err.txt file.

There is a separate output subdirectory for each time range. If the --per-cpu
option is used, these are further grouped under cpu-n subdirectories, e.g.

	$ parallel-perf.py --per-cpu --nr=2 -- perf script --ns --cpu=0,1
	All jobs finished successfully
	$ tree parallel-perf-output
	parallel-perf-output/
	├── cpu-0
	│   ├── time-range-0
	│   │   ├── cmd.txt
	│   │   └── out.txt
	│   └── time-range-1
	│       ├── cmd.txt
	│       └── out.txt
	└── cpu-1
	    ├── time-range-0
	    │   ├── cmd.txt
	    │   └── out.txt
	    └── time-range-1
	        ├── cmd.txt
	        └── out.txt
	$ find parallel-perf-output -name cmd.txt | sort | xargs grep -H .
	parallel-perf-output/cpu-0/time-range-0/cmd.txt:perf script --cpu=0 --time=,9469.005396999 --ns
	parallel-perf-output/cpu-0/time-range-1/cmd.txt:perf script --cpu=0 --time=9469.005397000, --ns
	parallel-perf-output/cpu-1/time-range-0/cmd.txt:perf script --cpu=1 --time=,9469.005396999 --ns
	parallel-perf-output/cpu-1/time-range-1/cmd.txt:perf script --cpu=1 --time=9469.005397000, --ns

Subdivisions of time range, and cpus if the --per-cpu option is used, are
expressed by the --time and --cpu perf script options respectively. If the
supplied perf script command has a --time option, then that time range is
subdivided, otherwise the time range given by 'time of first sample' to
'time of last sample' is used (refer perf script --header-only). Similarly, the
supplied perf script command may provide a --cpu option, and only those CPUs
will be processed.

To prevent time intervals becoming too small, the --min-interval option can
be used.

Note there is special handling for processing Intel PT traces. If an interval is
not specified and the perf record command contained the intel_pt event, then the
time range will be subdivided in order to produce subdivisions that contain
approximately the same amount of trace data. That is accomplished by counting
double-quick (--itrace=qqi) samples, and choosing time ranges that encompass
approximately the same number of samples. In that case, time ranges may not be
the same for each CPU processed. For Intel PT, --per-cpu is the default, but
that can be overridden by --no-per-cpu. Note, for Intel PT, double-quick
decoding produces 1 sample for each PSB synchronization packet, which in turn
come after a certain number of bytes output, determined by psb_period (refer
perf Intel PT documentation). The minimum number of double-quick samples that
will define a time range can be set by the --min_size option, which defaults to
64.
""")
    ap.add_argument("-o", "--output-dir", default="parallel-perf-output",
                    help="output directory (default 'parallel-perf-output')")
    ap.add_argument("-j", "--jobs", type=int, default=0,
                    help="maximum number of jobs to run in parallel at one time "
                         "(default is the number of CPUs)")
    ap.add_argument("-n", "--nr", type=int, default=0,
                    help="number of time subdivisions (default is the number of jobs)")
    ap.add_argument("-i", "--interval", type=float, default=0,
                    help="subdivide the time range using this time interval "
                         "(in seconds e.g. 0.1 for a tenth of a second)")
    ap.add_argument("-c", "--per-cpu", action="store_true",
                    help="process data for each CPU in parallel")
    ap.add_argument("-m", "--min-interval", type=float, default=glb_min_interval,
                    help=f"minimum interval (default {glb_min_interval} seconds)")
    ap.add_argument("-p", "--pipe-to",
                    help="command to pipe output to (optional)")
    ap.add_argument("-N", "--no-per-cpu", action="store_true",
                    help="do not process data for each CPU in parallel")
    ap.add_argument("-b", "--min_size", type=int, default=glb_min_samples,
                    help="minimum data size (for Intel PT in PSBs)")
    ap.add_argument("-D", "--dry-run", action="store_true",
                    help="do not run any jobs, just show the perf script commands")
    ap.add_argument("-q", "--quiet", action="store_true",
                    help="do not print any messages except errors")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="print more messages")
    ap.add_argument("-d", "--debug", action="store_true",
                    help="print debugging messages")
    cmd_line = list(args)
    try:
        split_pos = cmd_line.index("--")
        cmd = cmd_line[split_pos + 1:]
        args = cmd_line[:split_pos]
    except (OSError, ValueError, RuntimeError):
        cmd = None
        args = cmd_line
    a = ap.parse_args(args=args[1:])
    a.cmd = cmd
    setattr(a, "_verbosity", Verbosity(a.quiet, a.verbose, a.debug))
    try:
        if not a.cmd:
            if a.cmd is None and len(args) <= 1:
                ap.print_help()
                return True
            raise RuntimeError(
                "command line must contain '--' before perf command")
        return run_parallel_perf(a)
    except (OSError, ValueError, RuntimeError, KeyboardInterrupt) as e:
        print("Fatal error: ", str(e))
        if a.debug:
            raise
        return False


if __name__ == "__main__":
    if not main(sys.argv):
        sys.exit(1)
