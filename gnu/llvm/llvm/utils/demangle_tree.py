# Given a path to llvm-objdump and a directory tree, spider the directory tree
# dumping every object file encountered with correct options needed to demangle
# symbols in the object file, and collect statistics about failed / crashed
# demanglings.  Useful for stress testing the demangler against a large corpus
# of inputs.

from __future__ import print_function

import argparse
import functools
import os
import re
import sys
import subprocess
import traceback
from multiprocessing import Pool
import multiprocessing

args = None


def parse_line(line):
    question = line.find("?")
    if question == -1:
        return None, None

    open_paren = line.find("(", question)
    if open_paren == -1:
        return None, None
    close_paren = line.rfind(")", open_paren)
    if open_paren == -1:
        return None, None
    mangled = line[question:open_paren]
    demangled = line[open_paren + 1 : close_paren]
    return mangled.strip(), demangled.strip()


class Result(object):
    def __init__(self):
        self.crashed = []
        self.file = None
        self.nsymbols = 0
        self.errors = set()
        self.nfiles = 0


class MapContext(object):
    def __init__(self):
        self.rincomplete = None
        self.rcumulative = Result()
        self.pending_objs = []
        self.npending = 0


def process_file(path, objdump):
    r = Result()
    r.file = path

    popen_args = [objdump, "-t", "-demangle", path]
    p = subprocess.Popen(popen_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = p.communicate()
    if p.returncode != 0:
        r.crashed = [r.file]
        return r

    output = stdout.decode("utf-8")

    for line in output.splitlines():
        mangled, demangled = parse_line(line)
        if mangled is None:
            continue
        r.nsymbols += 1
        if "invalid mangled name" in demangled:
            r.errors.add(mangled)
    return r


def add_results(r1, r2):
    r1.crashed.extend(r2.crashed)
    r1.errors.update(r2.errors)
    r1.nsymbols += r2.nsymbols
    r1.nfiles += r2.nfiles


def print_result_row(directory, result):
    print(
        "[{0} files, {1} crashes, {2} errors, {3} symbols]: '{4}'".format(
            result.nfiles,
            len(result.crashed),
            len(result.errors),
            result.nsymbols,
            directory,
        )
    )


def process_one_chunk(pool, chunk_size, objdump, context):
    objs = []

    incomplete = False
    dir_results = {}
    ordered_dirs = []
    while context.npending > 0 and len(objs) < chunk_size:
        this_dir = context.pending_objs[0][0]
        ordered_dirs.append(this_dir)
        re = Result()
        if context.rincomplete is not None:
            re = context.rincomplete
            context.rincomplete = None

        dir_results[this_dir] = re
        re.file = this_dir

        nneeded = chunk_size - len(objs)
        objs_this_dir = context.pending_objs[0][1]
        navail = len(objs_this_dir)
        ntaken = min(nneeded, navail)
        objs.extend(objs_this_dir[0:ntaken])
        remaining_objs_this_dir = objs_this_dir[ntaken:]
        context.pending_objs[0] = (context.pending_objs[0][0], remaining_objs_this_dir)
        context.npending -= ntaken
        if ntaken == navail:
            context.pending_objs.pop(0)
        else:
            incomplete = True

        re.nfiles += ntaken

    assert len(objs) == chunk_size or context.npending == 0

    copier = functools.partial(process_file, objdump=objdump)
    mapped_results = list(pool.map(copier, objs))

    for mr in mapped_results:
        result_dir = os.path.dirname(mr.file)
        result_entry = dir_results[result_dir]
        add_results(result_entry, mr)

    # It's only possible that a single item is incomplete, and it has to be the
    # last item.
    if incomplete:
        context.rincomplete = dir_results[ordered_dirs[-1]]
        ordered_dirs.pop()

    # Now ordered_dirs contains a list of all directories which *did* complete.
    for c in ordered_dirs:
        re = dir_results[c]
        add_results(context.rcumulative, re)
        print_result_row(c, re)


def process_pending_files(pool, chunk_size, objdump, context):
    while context.npending >= chunk_size:
        process_one_chunk(pool, chunk_size, objdump, context)


def go():
    global args

    obj_dir = args.dir
    extensions = args.extensions.split(",")
    extensions = [x if x[0] == "." else "." + x for x in extensions]

    pool_size = 48
    pool = Pool(processes=pool_size)

    try:
        nfiles = 0
        context = MapContext()

        for root, dirs, files in os.walk(obj_dir):
            root = os.path.normpath(root)
            pending = []
            for f in files:
                file, ext = os.path.splitext(f)
                if not ext in extensions:
                    continue

                nfiles += 1
                full_path = os.path.join(root, f)
                full_path = os.path.normpath(full_path)
                pending.append(full_path)

            # If this directory had no object files, just print a default
            # status line and continue with the next dir
            if len(pending) == 0:
                print_result_row(root, Result())
                continue

            context.npending += len(pending)
            context.pending_objs.append((root, pending))
            # Drain the tasks, `pool_size` at a time, until we have less than
            # `pool_size` tasks remaining.
            process_pending_files(pool, pool_size, args.objdump, context)

        assert context.npending < pool_size
        process_one_chunk(pool, pool_size, args.objdump, context)

        total = context.rcumulative
        nfailed = len(total.errors)
        nsuccess = total.nsymbols - nfailed
        ncrashed = len(total.crashed)

        if nfailed > 0:
            print("Failures:")
            for m in sorted(total.errors):
                print("  " + m)
        if ncrashed > 0:
            print("Crashes:")
            for f in sorted(total.crashed):
                print("  " + f)
        print("Summary:")
        spct = float(nsuccess) / float(total.nsymbols)
        fpct = float(nfailed) / float(total.nsymbols)
        cpct = float(ncrashed) / float(nfiles)
        print("Processed {0} object files.".format(nfiles))
        print(
            "{0}/{1} symbols successfully demangled ({2:.4%})".format(
                nsuccess, total.nsymbols, spct
            )
        )
        print("{0} symbols could not be demangled ({1:.4%})".format(nfailed, fpct))
        print("{0} files crashed while demangling ({1:.4%})".format(ncrashed, cpct))

    except:
        traceback.print_exc()

    pool.close()
    pool.join()


if __name__ == "__main__":
    def_obj = "obj" if sys.platform == "win32" else "o"

    parser = argparse.ArgumentParser(
        description="Demangle all symbols in a tree of object files, looking for failures."
    )
    parser.add_argument(
        "dir", type=str, help="the root directory at which to start crawling"
    )
    parser.add_argument(
        "--objdump",
        type=str,
        help="path to llvm-objdump.  If not specified "
        + "the tool is located as if by `which llvm-objdump`.",
    )
    parser.add_argument(
        "--extensions",
        type=str,
        default=def_obj,
        help="comma separated list of extensions to demangle (e.g. `o,obj`).  "
        + "By default this will be `obj` on Windows and `o` otherwise.",
    )

    args = parser.parse_args()

    multiprocessing.freeze_support()
    go()
