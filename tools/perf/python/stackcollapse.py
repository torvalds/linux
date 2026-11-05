#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
stackcollapse.py - format perf samples with one line per distinct call stack

This script's output has two space-separated fields.  The first is a semicolon
separated stack including the program name (from the "comm" field) and the
function names from the call stack.  The second is a count:

 swapper;start_kernel;rest_init;cpu_idle;default_idle;native_safe_halt 2

The file is sorted according to the first field.

Ported from tools/perf/scripts/python/stackcollapse.py
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import os
import sys
import perf


class StackCollapseAnalyzer:
    """Accumulates call stacks and prints them collapsed."""

    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.lines: dict[str, int] = defaultdict(int)
        self.session: perf.session | None = None

    def tidy_function_name(self, sym: str, dso: str) -> str:
        """Beautify function names based on options."""
        if sym is None:
            sym = "[unknown]"

        sym = sym.replace(";", ":")
        if self.args.tidy_java:
            # Beautify Java signatures
            sym = sym.replace("<", "")
            sym = sym.replace(">", "")
            if sym.startswith("L") and "/" in sym:
                sym = sym[1:]
            try:
                sym = sym[:sym.index("(")]
            except ValueError:
                pass

        if self.args.annotate_kernel and dso == "[kernel.kallsyms]":
            return sym + "_[k]"
        return sym

    def process_event(self, sample: perf.sample_event) -> None:
        """Collect call stack for each sample."""
        stack = []
        callchain = sample.callchain
        if callchain is not None:
            for node in callchain:
                stack.append(self.tidy_function_name(node.symbol, node.dso))
        else:
            # Fallback if no callchain
            sym = (sample.symbol or '[unknown]')
            dso = (sample.dso or '[unknown]')
            stack.append(self.tidy_function_name(sym, dso))

        if self.args.include_comm:
            comm = "Unknown"
            if self.session is not None:
                try:
                    proc = self.session.find_thread(
                        sample.sample_pid, sample.sample_tid
                    )
                    if proc:
                        proc_comm = proc.comm()
                        if proc_comm is not None:
                            comm = proc_comm
                except TypeError:
                    pass
            comm = str(comm).replace(" ", "_")
            sep = "-"
            if self.args.include_pid:
                comm = f"{comm}{sep}{(sample.sample_pid or 0)}"
                sep = "/"
            if self.args.include_tid:
                comm = f"{comm}{sep}{(sample.sample_tid or 0)}"
            stack.append(comm)

        stack_string = ";".join(reversed(stack))
        self.lines[stack_string] += 1

    def print_totals(self) -> None:
        """Print sorted collapsed stacks."""
        try:
            for stack in sorted(self.lines):
                print(f"{stack} {self.lines[stack]}")
            sys.stdout.flush()
        except BrokenPipeError:
            devnull = os.open(os.devnull, os.O_WRONLY)
            os.dup2(devnull, sys.stdout.fileno())
            os.close(devnull)


def main():
    """Main function."""
    ap = argparse.ArgumentParser(
        description="Format perf samples with one line per distinct call stack"
    )
    ap.add_argument("-i", "--input", default="perf.data", help="Input file name")
    ap.add_argument("--include-tid", action="store_true", help="include thread id in stack")
    ap.add_argument("--include-pid", action="store_true", help="include process id in stack")
    ap.add_argument("--no-comm", dest="include_comm", action="store_false", default=True,
                    help="do not separate stacks according to comm")
    ap.add_argument("--tidy-java", action="store_true", help="beautify Java signatures")
    ap.add_argument("--kernel", dest="annotate_kernel", action="store_true",
                    help="annotate kernel functions with _[k]")

    args = ap.parse_args()

    if args.include_tid and not args.include_comm:
        print("requesting tid but not comm is invalid", file=sys.stderr)
        sys.exit(1)
    if args.include_pid and not args.include_comm:
        print("requesting pid but not comm is invalid", file=sys.stderr)
        sys.exit(1)

    analyzer = StackCollapseAnalyzer(args)

    try:
        session = perf.session(perf.data(args.input), sample=analyzer.process_event)
        analyzer.session = session
        session.process_events()
    except IOError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        pass
    finally:
        analyzer.session = None

    analyzer.print_totals()


if __name__ == "__main__":
    main()
