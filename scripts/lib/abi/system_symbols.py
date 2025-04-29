#!/usr/bin/env python3
# pylint: disable=R0902,R0912,R0914,R0915,R1702
# Copyright(c) 2025: Mauro Carvalho Chehab <mchehab@kernel.org>.
# SPDX-License-Identifier: GPL-2.0

"""
Parse ABI documentation and produce results from it.
"""

import os
import re
import sys

from concurrent import futures
from datetime import datetime
from random import shuffle

from helpers import AbiDebug

class SystemSymbols:
    """Stores arguments for the class and initialize class vars"""

    def graph_add_file(self, path, link=None):
        """
        add a file path to the sysfs graph stored at self.root
        """

        if path in self.files:
            return

        name = ""
        ref = self.root
        for edge in path.split("/"):
            name += edge + "/"
            if edge not in ref:
                ref[edge] = {"__name": [name.rstrip("/")]}

            ref = ref[edge]

        if link and link not in ref["__name"]:
            ref["__name"].append(link.rstrip("/"))

        self.files.add(path)

    def print_graph(self, root_prefix="", root=None, level=0):
        """Prints a reference tree graph using UTF-8 characters"""

        if not root:
            root = self.root
            level = 0

        # Prevent endless traverse
        if level > 5:
            return

        if level > 0:
            prefix = "├──"
            last_prefix = "└──"
        else:
            prefix = ""
            last_prefix = ""

        items = list(root.items())

        names = root.get("__name", [])
        for k, edge in items:
            if k == "__name":
                continue

            if not k:
                k = "/"

            if len(names) > 1:
                k += " links: " + ",".join(names[1:])

            if edge == items[-1][1]:
                print(root_prefix + last_prefix + k)
                p = root_prefix
                if level > 0:
                    p += "   "
                self.print_graph(p, edge, level + 1)
            else:
                print(root_prefix + prefix + k)
                p = root_prefix + "│   "
                self.print_graph(p, edge, level + 1)

    def _walk(self, root):
        """
        Walk through sysfs to get all devnodes that aren't ignored.

        By default, uses /sys as sysfs mounting point. If another
        directory is used, it replaces them to /sys at the patches.
        """

        with os.scandir(root) as obj:
            for entry in obj:
                path = os.path.join(root, entry.name)
                if self.sysfs:
                    p = path.replace(self.sysfs, "/sys", count=1)
                else:
                    p = path

                if self.re_ignore.search(p):
                    return

                # Handle link first to avoid directory recursion
                if entry.is_symlink():
                    real = os.path.realpath(path)
                    if not self.sysfs:
                        self.aliases[path] = real
                    else:
                        real = real.replace(self.sysfs, "/sys", count=1)

                    # Add absfile location to graph if it doesn't exist
                    if not self.re_ignore.search(real):
                        # Add link to the graph
                        self.graph_add_file(real, p)

                elif entry.is_file():
                    self.graph_add_file(p)

                elif entry.is_dir():
                    self._walk(path)

    def __init__(self, abi, sysfs="/sys", hints=False):
        """
        Initialize internal variables and get a list of all files inside
        sysfs that can currently be parsed.

        Please notice that there are several entries on sysfs that aren't
        documented as ABI. Ignore those.

        The real paths will be stored under self.files. Aliases will be
        stored in separate, as self.aliases.
        """

        self.abi = abi
        self.log = abi.log

        if sysfs != "/sys":
            self.sysfs = sysfs.rstrip("/")
        else:
            self.sysfs = None

        self.hints = hints

        self.root = {}
        self.aliases = {}
        self.files = set()

        dont_walk = [
            # Those require root access and aren't documented at ABI
            f"^{sysfs}/kernel/debug",
            f"^{sysfs}/kernel/tracing",
            f"^{sysfs}/fs/pstore",
            f"^{sysfs}/fs/bpf",
            f"^{sysfs}/fs/fuse",

            # This is not documented at ABI
            f"^{sysfs}/module",

            f"^{sysfs}/fs/cgroup",  # this is big and has zero docs under ABI
            f"^{sysfs}/firmware",   # documented elsewhere: ACPI, DT bindings
            "sections|notes",       # aren't actually part of ABI

            # kernel-parameters.txt - not easy to parse
            "parameters",
        ]

        self.re_ignore = re.compile("|".join(dont_walk))

        print(f"Reading {sysfs} directory contents...", file=sys.stderr)
        self._walk(sysfs)

    def check_file(self, refs, found):
        """Check missing ABI symbols for a given sysfs file"""

        res_list = []

        try:
            for names in refs:
                fname = names[0]

                res = {
                    "found": False,
                    "fname": fname,
                    "msg": "",
                }
                res_list.append(res)

                re_what = self.abi.get_regexes(fname)
                if not re_what:
                    self.abi.log.warning(f"missing rules for {fname}")
                    continue

                for name in names:
                    for r in re_what:
                        if self.abi.debug & AbiDebug.UNDEFINED:
                            self.log.debug("check if %s matches '%s'", name, r.pattern)
                        if r.match(name):
                            res["found"] = True
                            if found:
                                res["msg"] += f"  {fname}: regex:\n\t"
                            continue

                if self.hints and not res["found"]:
                    res["msg"] += f"  {fname} not found. Tested regexes:\n"
                    for r in re_what:
                        res["msg"] += "    " + r.pattern + "\n"

        except KeyboardInterrupt:
            pass

        return res_list

    def _ref_interactor(self, root):
        """Recursive function to interact over the sysfs tree"""

        for k, v in root.items():
            if isinstance(v, dict):
                yield from self._ref_interactor(v)

            if root == self.root or k == "__name":
                continue

            if self.abi.re_string:
                fname = v["__name"][0]
                if self.abi.re_string.search(fname):
                    yield v
            else:
                yield v


    def get_fileref(self, all_refs, chunk_size):
        """Interactor to group refs into chunks"""

        n = 0
        refs = []

        for ref in all_refs:
            refs.append(ref)

            n += 1
            if n >= chunk_size:
                yield refs
                n = 0
                refs = []

        yield refs

    def check_undefined_symbols(self, max_workers=None, chunk_size=50,
                                found=None, dry_run=None):
        """Seach ABI for sysfs symbols missing documentation"""

        self.abi.parse_abi()

        if self.abi.debug & AbiDebug.GRAPH:
            self.print_graph()

        all_refs = []
        for ref in self._ref_interactor(self.root):
            all_refs.append(ref["__name"])

        if dry_run:
            print("Would check", file=sys.stderr)
            for ref in all_refs:
                print(", ".join(ref))

            return

        print("Starting to search symbols (it may take several minutes):",
              file=sys.stderr)
        start = datetime.now()
        old_elapsed = None

        # Python doesn't support multithreading due to limitations on its
        # global lock (GIL). While Python 3.13 finally made GIL optional,
        # there are still issues related to it. Also, we want to have
        # backward compatibility with older versions of Python.
        #
        # So, use instead multiprocess. However, Python is very slow passing
        # data from/to multiple processes. Also, it may consume lots of memory
        # if the data to be shared is not small.  So, we need to group workload
        # in chunks that are big enough to generate performance gains while
        # not being so big that would cause out-of-memory.

        num_refs = len(all_refs)
        print(f"Number of references to parse: {num_refs}", file=sys.stderr)

        if not max_workers:
            max_workers = os.cpu_count()
        elif max_workers > os.cpu_count():
            max_workers = os.cpu_count()

        max_workers = max(max_workers, 1)

        max_chunk_size = int((num_refs + max_workers - 1) / max_workers)
        chunk_size = min(chunk_size, max_chunk_size)
        chunk_size = max(1, chunk_size)

        if max_workers > 1:
            executor = futures.ProcessPoolExecutor

            # Place references in a random order. This may help improving
            # performance, by mixing complex/simple expressions when creating
            # chunks
            shuffle(all_refs)
        else:
            # Python has a high overhead with processes. When there's just
            # one worker, it is faster to not create a new process.
            # Yet, User still deserves to have a progress print. So, use
            # python's "thread", which is actually a single process, using
            # an internal schedule to switch between tasks. No performance
            # gains for non-IO tasks, but still it can be quickly interrupted
            # from time to time to display progress.
            executor = futures.ThreadPoolExecutor

        not_found = []
        f_list = []
        with executor(max_workers=max_workers) as exe:
            for refs in self.get_fileref(all_refs, chunk_size):
                if refs:
                    try:
                        f_list.append(exe.submit(self.check_file, refs, found))

                    except KeyboardInterrupt:
                        return

            total = len(f_list)

            if not total:
                if self.abi.re_string:
                    print(f"No ABI symbol matches {self.abi.search_string}")
                else:
                    self.abi.log.warning("No ABI symbols found")
                return

            print(f"{len(f_list):6d} jobs queued on {max_workers} workers",
                  file=sys.stderr)

            while f_list:
                try:
                    t = futures.wait(f_list, timeout=1,
                                     return_when=futures.FIRST_COMPLETED)

                    done = t[0]

                    for fut in done:
                        res_list = fut.result()

                        for res in res_list:
                            if not res["found"]:
                                not_found.append(res["fname"])
                            if res["msg"]:
                                print(res["msg"])

                        f_list.remove(fut)
                except KeyboardInterrupt:
                    return

                except RuntimeError as e:
                    self.abi.log.warning(f"Future: {e}")
                    break

                if sys.stderr.isatty():
                    elapsed = str(datetime.now() - start).split(".", maxsplit=1)[0]
                    if len(f_list) < total:
                        elapsed += f" ({total - len(f_list)}/{total} jobs completed).  "
                    if elapsed != old_elapsed:
                        print(elapsed + "\r", end="", flush=True,
                              file=sys.stderr)
                        old_elapsed = elapsed

        elapsed = str(datetime.now() - start).split(".", maxsplit=1)[0]
        print(elapsed, file=sys.stderr)

        for f in sorted(not_found):
            print(f"{f} not found.")
