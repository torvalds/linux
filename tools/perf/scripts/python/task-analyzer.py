# task-analyzer.py - comprehensive perf tasks analysis
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022, Hagen Paul Pfeifer <hagen@jauu.net>
# Licensed under the terms of the GNU GPL License version 2
#
# Usage:
#
#     perf record -e sched:sched_switch -a -- sleep 10
#     perf script report task-analyzer
#

from __future__ import print_function
import sys
import os
import string
import argparse
import decimal


sys.path.append(
    os.environ["PERF_EXEC_PATH"] + "/scripts/python/Perf-Trace-Util/lib/Perf/Trace"
)
from perf_trace_context import *
from Core import *

# Definition of possible ASCII color codes
_COLORS = {
    "grey": "\033[90m",
    "red": "\033[91m",
    "green": "\033[92m",
    "yellow": "\033[93m",
    "blue": "\033[94m",
    "violet": "\033[95m",
    "reset": "\033[0m",
}

# Columns will have a static size to align everything properly
# Support of 116 days of active update with nano precision
LEN_SWITCHED_IN = len("9999999.999999999")  # 17
LEN_SWITCHED_OUT = len("9999999.999999999")  # 17
LEN_CPU = len("000")
LEN_PID = len("maxvalue")  # 8
LEN_TID = len("maxvalue")  # 8
LEN_COMM = len("max-comms-length")  # 16
LEN_RUNTIME = len("999999.999")  # 10
# Support of 3.45 hours of timespans
LEN_OUT_IN = len("99999999999.999")  # 15
LEN_OUT_OUT = len("99999999999.999")  # 15
LEN_IN_IN = len("99999999999.999")  # 15
LEN_IN_OUT = len("99999999999.999")  # 15


# py2/py3 compatibility layer, see PEP469
try:
    dict.iteritems
except AttributeError:
    # py3
    def itervalues(d):
        return iter(d.values())

    def iteritems(d):
        return iter(d.items())

else:
    # py2
    def itervalues(d):
        return d.itervalues()

    def iteritems(d):
        return d.iteritems()


def _check_color():
    global _COLORS
    """user enforced no-color or if stdout is no tty we disable colors"""
    if sys.stdout.isatty() and args.stdio_color != "never":
        return
    _COLORS = {
        "grey": "",
        "red": "",
        "green": "",
        "yellow": "",
        "blue": "",
        "violet": "",
        "reset": "",
    }


def _parse_args():
    global args
    parser = argparse.ArgumentParser(description="Analyze tasks behavior")
    parser.add_argument(
        "--time-limit",
        default=[],
        help=
            "print tasks only in time[s] window e.g"
        " --time-limit 123.111:789.222(print all between 123.111 and 789.222)"
        " --time-limit 123: (print all from 123)"
        " --time-limit :456 (print all until incl. 456)",
    )
    parser.add_argument(
        "--summary", action="store_true", help="print addtional runtime information"
    )
    parser.add_argument(
        "--summary-only", action="store_true", help="print only summary without traces"
    )
    parser.add_argument(
        "--summary-extended",
        action="store_true",
        help="print the summary with additional information of max inter task times"
            " relative to the prev task",
    )
    parser.add_argument(
        "--ns", action="store_true", help="show timestamps in nanoseconds"
    )
    parser.add_argument(
        "--ms", action="store_true", help="show timestamps in miliseconds"
    )
    parser.add_argument(
        "--extended-times",
        action="store_true",
        help="Show the elapsed times between schedule in/schedule out"
            " of this task and the schedule in/schedule out of previous occurrence"
            " of the same task",
    )
    parser.add_argument(
        "--filter-tasks",
        default=[],
        help="filter out unneeded tasks by tid, pid or processname."
        " E.g --filter-task 1337,/sbin/init ",
    )
    parser.add_argument(
        "--limit-to-tasks",
        default=[],
        help="limit output to selected task by tid, pid, processname."
        " E.g --limit-to-tasks 1337,/sbin/init",
    )
    parser.add_argument(
        "--highlight-tasks",
        default="",
        help="colorize special tasks by their pid/tid/comm."
        " E.g. --highlight-tasks 1:red,mutt:yellow"
        " Colors available: red,grey,yellow,blue,violet,green",
    )
    parser.add_argument(
        "--rename-comms-by-tids",
        default="",
        help="rename task names by using tid (<tid>:<newname>,<tid>:<newname>)"
            " This option is handy for inexpressive processnames like python interpreted"
            " process. E.g --rename 1337:my-python-app",
    )
    parser.add_argument(
        "--stdio-color",
        default="auto",
        choices=["always", "never", "auto"],
        help="always, never or auto, allowing configuring color output"
            " via the command line",
    )
    args = parser.parse_args()
    args.tid_renames = dict()

    _argument_filter_sanity_check()
    _argument_prepare_check()


def time_uniter(unit):
    picker = {
        "s": 1,
        "ms": 1e3,
        "us": 1e6,
        "ns": 1e9,
    }
    return picker[unit]


def _init_db():
    global db
    db = dict()
    db["running"] = dict()
    db["cpu"] = dict()
    db["tid"] = dict()
    db["global"] = []
    if args.summary or args.summary_extended or args.summary_only:
        db["task_info"] = dict()
        db["runtime_info"] = dict()
        # min values for summary depending on the header
        db["task_info"]["pid"] = len("PID")
        db["task_info"]["tid"] = len("TID")
        db["task_info"]["comm"] = len("Comm")
        db["runtime_info"]["runs"] = len("Runs")
        db["runtime_info"]["acc"] = len("Accumulated")
        db["runtime_info"]["max"] = len("Max")
        db["runtime_info"]["max_at"] = len("Max At")
        db["runtime_info"]["min"] = len("Min")
        db["runtime_info"]["mean"] = len("Mean")
        db["runtime_info"]["median"] = len("Median")
        if args.summary_extended:
            db["inter_times"] = dict()
            db["inter_times"]["out_in"] = len("Out-In")
            db["inter_times"]["inter_at"] = len("At")
            db["inter_times"]["out_out"] = len("Out-Out")
            db["inter_times"]["in_in"] = len("In-In")
            db["inter_times"]["in_out"] = len("In-Out")


def _median(numbers):
    """phython3 hat statistics module - we have nothing"""
    n = len(numbers)
    index = n // 2
    if n % 2:
        return sorted(numbers)[index]
    return sum(sorted(numbers)[index - 1 : index + 1]) / 2


def _mean(numbers):
    return sum(numbers) / len(numbers)


class Timespans(object):
    """
    The elapsed time between two occurrences of the same task is being tracked with the
    help of this class. There are 4 of those Timespans Out-Out, In-Out, Out-In and
    In-In.
    The first half of the name signals the first time point of the
    first task. The second half of the name represents the second
    timepoint of the second task.
    """

    def __init__(self):
        self._last_start = None
        self._last_finish = None
        self.out_out = -1
        self.in_out = -1
        self.out_in = -1
        self.in_in = -1
        if args.summary_extended:
            self._time_in = -1
            self.max_out_in = -1
            self.max_at = -1
            self.max_in_out = -1
            self.max_in_in = -1
            self.max_out_out = -1

    def feed(self, task):
        """
        Called for every recorded trace event to find process pair and calculate the
        task timespans. Chronological ordering, feed does not do reordering
        """
        if not self._last_finish:
            self._last_start = task.time_in(time_unit)
            self._last_finish = task.time_out(time_unit)
            return
        self._time_in = task.time_in()
        time_in = task.time_in(time_unit)
        time_out = task.time_out(time_unit)
        self.in_in = time_in - self._last_start
        self.out_in = time_in - self._last_finish
        self.in_out = time_out - self._last_start
        self.out_out = time_out - self._last_finish
        if args.summary_extended:
            self._update_max_entries()
        self._last_finish = task.time_out(time_unit)
        self._last_start = task.time_in(time_unit)

    def _update_max_entries(self):
        if self.in_in > self.max_in_in:
            self.max_in_in = self.in_in
        if self.out_out > self.max_out_out:
            self.max_out_out = self.out_out
        if self.in_out > self.max_in_out:
            self.max_in_out = self.in_out
        if self.out_in > self.max_out_in:
            self.max_out_in = self.out_in
            self.max_at = self._time_in




class Summary(object):
    """
    Primary instance for calculating the summary output. Processes the whole trace to
    find and memorize relevant data such as mean, max et cetera. This instance handles
    dynamic alignment aspects for summary output.
    """

    def __init__(self):
        self._body = []

    class AlignmentHelper:
        """
        Used to calculated the alignment for the output of the summary.
        """
        def __init__(self, pid, tid, comm, runs, acc, mean,
                    median, min, max, max_at):
            self.pid = pid
            self.tid = tid
            self.comm = comm
            self.runs = runs
            self.acc = acc
            self.mean = mean
            self.median = median
            self.min = min
            self.max = max
            self.max_at = max_at
            if args.summary_extended:
                self.out_in = None
                self.inter_at = None
                self.out_out = None
                self.in_in = None
                self.in_out = None

    def _print_header(self):
        '''
        Output is trimmed in _format_stats thus additional adjustment in the header
        is needed, depending on the choice of timeunit. The adjustment corresponds
        to the amount of column titles being adjusted in _column_titles.
        '''
        decimal_precision = 6 if not args.ns else 9
        fmt = " {{:^{}}}".format(sum(db["task_info"].values()))
        fmt += " {{:^{}}}".format(
            sum(db["runtime_info"].values()) - 2 * decimal_precision
            )
        _header = ("Task Information", "Runtime Information")

        if args.summary_extended:
            fmt += " {{:^{}}}".format(
                sum(db["inter_times"].values()) - 4 * decimal_precision
                )
            _header += ("Max Inter Task Times",)
        print(fmt.format(*_header))

    def _column_titles(self):
        """
        Cells are being processed and displayed in different way so an alignment adjust
        is implemented depeding on the choice of the timeunit. The positions of the max
        values are being displayed in grey. Thus in their format two additional {},
        are placed for color set and reset.
        """
        decimal_precision, time_precision = _prepare_fmt_precision()
        fmt = " {{:>{}}}".format(db["task_info"]["pid"])
        fmt += " {{:>{}}}".format(db["task_info"]["tid"])
        fmt += " {{:>{}}}".format(db["task_info"]["comm"])
        fmt += " {{:>{}}}".format(db["runtime_info"]["runs"])
        fmt += " {{:>{}}}".format(db["runtime_info"]["acc"])
        fmt += " {{:>{}}}".format(db["runtime_info"]["mean"])
        fmt += " {{:>{}}}".format(db["runtime_info"]["median"])
        fmt += " {{:>{}}}".format(db["runtime_info"]["min"] - decimal_precision)
        fmt += " {{:>{}}}".format(db["runtime_info"]["max"] - decimal_precision)
        fmt += " {{}}{{:>{}}}{{}}".format(db["runtime_info"]["max_at"] - time_precision)

        column_titles = ("PID", "TID", "Comm")
        column_titles += ("Runs", "Accumulated", "Mean", "Median", "Min", "Max")
        column_titles += (_COLORS["grey"], "At", _COLORS["reset"])

        if args.summary_extended:
            fmt += " {{:>{}}}".format(db["inter_times"]["out_in"] - decimal_precision)
            fmt += " {{}}{{:>{}}}{{}}".format(
                db["inter_times"]["inter_at"] - time_precision
            )
            fmt += " {{:>{}}}".format(db["inter_times"]["out_out"] - decimal_precision)
            fmt += " {{:>{}}}".format(db["inter_times"]["in_in"] - decimal_precision)
            fmt += " {{:>{}}}".format(db["inter_times"]["in_out"] - decimal_precision)

            column_titles += ("Out-In", _COLORS["grey"], "Max At", _COLORS["reset"],
                        "Out-Out", "In-In", "In-Out")
        print(fmt.format(*column_titles))

    def _task_stats(self):
        """calculates the stats of every task and constructs the printable summary"""
        for tid in sorted(db["tid"]):
            color_one_sample = _COLORS["grey"]
            color_reset = _COLORS["reset"]
            no_executed = 0
            runtimes = []
            time_in = []
            timespans = Timespans()
            for task in db["tid"][tid]:
                pid = task.pid
                comm = task.comm
                no_executed += 1
                runtimes.append(task.runtime(time_unit))
                time_in.append(task.time_in())
                timespans.feed(task)
            if len(runtimes) > 1:
                color_one_sample = ""
                color_reset = ""
            time_max = max(runtimes)
            time_min = min(runtimes)
            max_at = time_in[runtimes.index(max(runtimes))]

            # The size of the decimal after sum,mean and median varies, thus we cut
            # the decimal number, by rounding it. It has no impact on the output,
            # because we have a precision of the decimal points at the output.
            time_sum = round(sum(runtimes), 3)
            time_mean = round(_mean(runtimes), 3)
            time_median = round(_median(runtimes), 3)

            align_helper = self.AlignmentHelper(pid, tid, comm, no_executed, time_sum,
                                    time_mean, time_median, time_min, time_max, max_at)
            self._body.append([pid, tid, comm, no_executed, time_sum, color_one_sample,
                                time_mean, time_median, time_min, time_max,
                                _COLORS["grey"], max_at, _COLORS["reset"], color_reset])
            if args.summary_extended:
                self._body[-1].extend([timespans.max_out_in,
                                _COLORS["grey"], timespans.max_at,
                                _COLORS["reset"], timespans.max_out_out,
                                timespans.max_in_in,
                                timespans.max_in_out])
                align_helper.out_in = timespans.max_out_in
                align_helper.inter_at = timespans.max_at
                align_helper.out_out = timespans.max_out_out
                align_helper.in_in = timespans.max_in_in
                align_helper.in_out = timespans.max_in_out
            self._calc_alignments_summary(align_helper)

    def _format_stats(self):
        decimal_precision, time_precision = _prepare_fmt_precision()
        fmt = " {{:>{}d}}".format(db["task_info"]["pid"])
        fmt += " {{:>{}d}}".format(db["task_info"]["tid"])
        fmt += " {{:>{}}}".format(db["task_info"]["comm"])
        fmt += " {{:>{}d}}".format(db["runtime_info"]["runs"])
        fmt += " {{:>{}.{}f}}".format(db["runtime_info"]["acc"], time_precision)
        fmt += " {{}}{{:>{}.{}f}}".format(db["runtime_info"]["mean"], time_precision)
        fmt += " {{:>{}.{}f}}".format(db["runtime_info"]["median"], time_precision)
        fmt += " {{:>{}.{}f}}".format(
            db["runtime_info"]["min"] - decimal_precision, time_precision
            )
        fmt += " {{:>{}.{}f}}".format(
            db["runtime_info"]["max"] - decimal_precision, time_precision
            )
        fmt += " {{}}{{:>{}.{}f}}{{}}{{}}".format(
            db["runtime_info"]["max_at"] - time_precision, decimal_precision
        )
        if args.summary_extended:
            fmt += " {{:>{}.{}f}}".format(
                db["inter_times"]["out_in"] - decimal_precision, time_precision
            )
            fmt += " {{}}{{:>{}.{}f}}{{}}".format(
                db["inter_times"]["inter_at"] - time_precision, decimal_precision
            )
            fmt += " {{:>{}.{}f}}".format(
                db["inter_times"]["out_out"] - decimal_precision, time_precision
            )
            fmt += " {{:>{}.{}f}}".format(
                db["inter_times"]["in_in"] - decimal_precision, time_precision
            )
            fmt += " {{:>{}.{}f}}".format(
                db["inter_times"]["in_out"] - decimal_precision, time_precision
            )
        return fmt


    def _calc_alignments_summary(self, align_helper):
        # Length is being cut in 3 groups so that further addition is easier to handle.
        # The length of every argument from the alignment helper is being checked if it
        # is longer than the longest until now. In that case the length is being saved.
        for key in db["task_info"]:
            if len(str(getattr(align_helper, key))) > db["task_info"][key]:
                db["task_info"][key] = len(str(getattr(align_helper, key)))
        for key in db["runtime_info"]:
            if len(str(getattr(align_helper, key))) > db["runtime_info"][key]:
                db["runtime_info"][key] = len(str(getattr(align_helper, key)))
        if args.summary_extended:
            for key in db["inter_times"]:
                if len(str(getattr(align_helper, key))) > db["inter_times"][key]:
                    db["inter_times"][key] = len(str(getattr(align_helper, key)))


    def print(self):
        print("\nSummary")
        self._task_stats()
        self._print_header()
        self._column_titles()
        fmt = self._format_stats()
        for i in range(len(self._body)):
            print(fmt.format(*tuple(self._body[i])))



class Task(object):
    """ The class is used to handle the information of a given task."""

    def __init__(self, id, tid, cpu, comm):
        self.id = id
        self.tid = tid
        self.cpu = cpu
        self.comm = comm
        self.pid = None
        self._time_in = None
        self._time_out = None

    def schedule_in_at(self, time):
        """set the time where the task was scheduled in"""
        self._time_in = time

    def schedule_out_at(self, time):
        """set the time where the task was scheduled out"""
        self._time_out = time

    def time_out(self, unit="s"):
        """return time where a given task was scheduled out"""
        factor = time_uniter(unit)
        return self._time_out * decimal.Decimal(factor)

    def time_in(self, unit="s"):
        """return time where a given task was scheduled in"""
        factor = time_uniter(unit)
        return self._time_in * decimal.Decimal(factor)

    def runtime(self, unit="us"):
        factor = time_uniter(unit)
        return (self._time_out - self._time_in) * decimal.Decimal(factor)

    def update_pid(self, pid):
        self.pid = pid


def _task_id(pid, cpu):
    """returns a "unique-enough" identifier, please do not change"""
    return "{}-{}".format(pid, cpu)


def _filter_non_printable(unfiltered):
    """comm names may contain loony chars like '\x00000'"""
    filtered = ""
    for char in unfiltered:
        if char not in string.printable:
            continue
        filtered += char
    return filtered


def _fmt_header():
    fmt = "{{:>{}}}".format(LEN_SWITCHED_IN)
    fmt += " {{:>{}}}".format(LEN_SWITCHED_OUT)
    fmt += " {{:>{}}}".format(LEN_CPU)
    fmt += " {{:>{}}}".format(LEN_PID)
    fmt += "  {{:>{}}}".format(LEN_TID)
    fmt += "  {{:>{}}}".format(LEN_COMM)
    fmt += " {{:>{}}}".format(LEN_RUNTIME)
    fmt += " {{:>{}}}".format(LEN_OUT_IN)
    if args.extended_times:
        fmt += " {{:>{}}}".format(LEN_OUT_OUT)
        fmt += " {{:>{}}}".format(LEN_IN_IN)
        fmt += " {{:>{}}}".format(LEN_IN_OUT)
    return fmt


def _fmt_body():
    decimal_precision, time_precision = _prepare_fmt_precision()
    fmt = "{{}}{{:{}.{}f}}".format(LEN_SWITCHED_IN, decimal_precision)
    fmt += " {{:{}.{}f}}".format(LEN_SWITCHED_OUT, decimal_precision)
    fmt += " {{:{}d}}".format(LEN_CPU)
    fmt += " {{:{}d}}".format(LEN_PID)
    fmt += "  {{}}{{:{}d}}{{}}".format(LEN_TID)
    fmt += "  {{}}{{:>{}}}".format(LEN_COMM)
    fmt += " {{:{}.{}f}}".format(LEN_RUNTIME, time_precision)
    if args.extended_times:
        fmt += " {{:{}.{}f}}".format(LEN_OUT_IN, time_precision)
        fmt += " {{:{}.{}f}}".format(LEN_OUT_OUT, time_precision)
        fmt += " {{:{}.{}f}}".format(LEN_IN_IN, time_precision)
        fmt += " {{:{}.{}f}}{{}}".format(LEN_IN_OUT, time_precision)
    else:
        fmt += " {{:{}.{}f}}{{}}".format(LEN_OUT_IN, time_precision)
    return fmt


def _print_header():
    fmt = _fmt_header()
    header = ("Switched-In", "Switched-Out", "CPU", "PID", "TID", "Comm", "Runtime",
            "Time Out-In")
    if args.extended_times:
        header += ("Time Out-Out", "Time In-In", "Time In-Out")
    print(fmt.format(*header))


def _print_task_finish(task):
    """calculating every entry of a row and printing it immediately"""
    c_row_set = ""
    c_row_reset = ""
    out_in = -1
    out_out = -1
    in_in = -1
    in_out = -1
    fmt = _fmt_body()

    # depending on user provided highlight option we change the color
    # for particular tasks
    if str(task.tid) in args.highlight_tasks_map:
        c_row_set = _COLORS[args.highlight_tasks_map[str(task.tid)]]
        c_row_reset = _COLORS["reset"]
    if task.comm in args.highlight_tasks_map:
        c_row_set = _COLORS[args.highlight_tasks_map[task.comm]]
        c_row_reset = _COLORS["reset"]
    # grey-out entries if PID == TID, they
    # are identical, no threaded model so the
    # thread id (tid) do not matter
    c_tid_set = ""
    c_tid_reset = ""
    if task.pid == task.tid:
        c_tid_set = _COLORS["grey"]
        c_tid_reset = _COLORS["reset"]
    if task.tid in db["tid"]:
        # get last task of tid
        last_tid_task = db["tid"][task.tid][-1]
        # feed the timespan calculate, last in tid db
        # and second the current one
        timespan_gap_tid = Timespans()
        timespan_gap_tid.feed(last_tid_task)
        timespan_gap_tid.feed(task)
        out_in = timespan_gap_tid.out_in
        out_out = timespan_gap_tid.out_out
        in_in = timespan_gap_tid.in_in
        in_out = timespan_gap_tid.in_out
    if args.extended_times:
        print(fmt.format(c_row_set, task.time_in(), task.time_out(), task.cpu, task.pid,
                        c_tid_set, task.tid, c_tid_reset, c_row_set, task.comm,
                        task.runtime(time_unit), out_in, out_out, in_in, in_out,
                        c_row_reset))
    else:
        print(fmt.format(c_row_set, task.time_in(), task.time_out(), task.cpu, task.pid,
                        c_tid_set, task.tid, c_tid_reset, c_row_set, task.comm,
                        task.runtime(time_unit), out_in, c_row_reset))


def _record_cleanup(_list):
    """
    no need to store more then one element if --summarize
    is not enabled
    """
    if not args.summary and len(_list) > 1:
        _list = _list[len(_list) - 1 :]


def _record_by_tid(task):
    tid = task.tid
    if tid not in db["tid"]:
        db["tid"][tid] = []
    db["tid"][tid].append(task)
    _record_cleanup(db["tid"][tid])


def _record_by_cpu(task):
    cpu = task.cpu
    if cpu not in db["cpu"]:
        db["cpu"][cpu] = []
    db["cpu"][cpu].append(task)
    _record_cleanup(db["cpu"][cpu])


def _record_global(task):
    """record all executed task, ordered by finish chronological"""
    db["global"].append(task)
    _record_cleanup(db["global"])


def _handle_task_finish(tid, cpu, time, perf_sample_dict):
    if tid == 0:
        return
    _id = _task_id(tid, cpu)
    if _id not in db["running"]:
        # may happen, if we missed the switch to
        # event. Seen in combination with --exclude-perf
        # where the start is filtered out, but not the
        # switched in. Probably a bug in exclude-perf
        # option.
        return
    task = db["running"][_id]
    task.schedule_out_at(time)

    # record tid, during schedule in the tid
    # is not available, update now
    pid = int(perf_sample_dict["sample"]["pid"])

    task.update_pid(pid)
    del db["running"][_id]

    # print only tasks which are not being filtered and no print of trace
    # for summary only, but record every task.
    if not _limit_filtered(tid, pid, task.comm) and not args.summary_only:
        _print_task_finish(task)
    _record_by_tid(task)
    _record_by_cpu(task)
    _record_global(task)


def _handle_task_start(tid, cpu, comm, time):
    if tid == 0:
        return
    if tid in args.tid_renames:
        comm = args.tid_renames[tid]
    _id = _task_id(tid, cpu)
    if _id in db["running"]:
        # handle corner cases where already running tasks
        # are switched-to again - saw this via --exclude-perf
        # recorded traces. We simple ignore this "second start"
        # event.
        return
    assert _id not in db["running"]
    task = Task(_id, tid, cpu, comm)
    task.schedule_in_at(time)
    db["running"][_id] = task


def _time_to_internal(time_ns):
    """
    To prevent float rounding errors we use Decimal internally
    """
    return decimal.Decimal(time_ns) / decimal.Decimal(1e9)


def _limit_filtered(tid, pid, comm):
    if args.filter_tasks:
        if str(tid) in args.filter_tasks or comm in args.filter_tasks:
            return True
        else:
            return False
    if args.limit_to_tasks:
        if str(tid) in args.limit_to_tasks or comm in args.limit_to_tasks:
            return False
        else:
            return True


def _argument_filter_sanity_check():
    if args.limit_to_tasks and args.filter_tasks:
        sys.exit("Error: Filter and Limit at the same time active.")
    if args.extended_times and args.summary_only:
        sys.exit("Error: Summary only and extended times active.")
    if args.time_limit and ":" not in args.time_limit:
        sys.exit(
            "Error: No bound set for time limit. Please set bound by ':' e.g :123."
        )
    if args.time_limit and (args.summary or args.summary_only or args.summary_extended):
        sys.exit("Error: Cannot set time limit and print summary")


def _argument_prepare_check():
    global time_unit
    if args.filter_tasks:
        args.filter_tasks = args.filter_tasks.split(",")
    if args.limit_to_tasks:
        args.limit_to_tasks = args.limit_to_tasks.split(",")
    if args.time_limit:
        args.time_limit = args.time_limit.split(":")
    for rename_tuple in args.rename_comms_by_tids.split(","):
        tid_name = rename_tuple.split(":")
        if len(tid_name) != 2:
            continue
        args.tid_renames[int(tid_name[0])] = tid_name[1]
    args.highlight_tasks_map = dict()
    for highlight_tasks_tuple in args.highlight_tasks.split(","):
        tasks_color_map = highlight_tasks_tuple.split(":")
        # default highlight color to red if no color set by user
        if len(tasks_color_map) == 1:
            tasks_color_map.append("red")
        if args.highlight_tasks and tasks_color_map[1].lower() not in _COLORS:
            sys.exit(
                "Error: Color not defined, please choose from grey,red,green,yellow,blue,"
                "violet"
            )
        if len(tasks_color_map) != 2:
            continue
        args.highlight_tasks_map[tasks_color_map[0]] = tasks_color_map[1]
    time_unit = "us"
    if args.ns:
        time_unit = "ns"
    elif args.ms:
        time_unit = "ms"


def _is_within_timelimit(time):
    """
    Check if a time limit was given by parameter, if so ignore the rest. If not,
    process the recorded trace in its entirety.
    """
    if not args.time_limit:
        return True
    lower_time_limit = args.time_limit[0]
    upper_time_limit = args.time_limit[1]
    # check for upper limit
    if upper_time_limit == "":
        if time >= decimal.Decimal(lower_time_limit):
            return True
    # check for lower limit
    if lower_time_limit == "":
        if time <= decimal.Decimal(upper_time_limit):
            return True
        # quit if time exceeds upper limit. Good for big datasets
        else:
            quit()
    if lower_time_limit != "" and upper_time_limit != "":
        if (time >= decimal.Decimal(lower_time_limit) and
            time <= decimal.Decimal(upper_time_limit)):
            return True
        # quit if time exceeds upper limit. Good for big datasets
        elif time > decimal.Decimal(upper_time_limit):
            quit()

def _prepare_fmt_precision():
    decimal_precision = 6
    time_precision = 3
    if args.ns:
       decimal_precision = 9
       time_precision = 0
    return decimal_precision, time_precision


def trace_unhandled(event_name, context, event_fields_dict, perf_sample_dict):
    pass


def trace_begin():
    _parse_args()
    _check_color()
    _init_db()
    if not args.summary_only:
        _print_header()

def trace_end():
    if args.summary or args.summary_extended or args.summary_only:
        Summary().print()

def sched__sched_switch(event_name, context, common_cpu, common_secs, common_nsecs,
                        common_pid, common_comm, common_callchain, prev_comm,
                        prev_pid, prev_prio, prev_state, next_comm, next_pid,
                        next_prio, perf_sample_dict):
    # ignore common_secs & common_nsecs cause we need
    # high res timestamp anyway, using the raw value is
    # faster
    time = _time_to_internal(perf_sample_dict["sample"]["time"])
    if not _is_within_timelimit(time):
        # user specific --time-limit a:b set
        return

    next_comm = _filter_non_printable(next_comm)
    _handle_task_finish(prev_pid, common_cpu, time, perf_sample_dict)
    _handle_task_start(next_pid, common_cpu, next_comm, time)
