#!/usr/bin/env python
# A tool to parse creates a document outlining how clang formatted the
# LLVM project is.

import sys
import os
import subprocess
from datetime import datetime


def get_git_revision_short_hash():
    """Get the get SHA in short hash form."""
    return (
        subprocess.check_output(["git", "rev-parse", "--short", "HEAD"])
        .decode(sys.stdout.encoding)
        .strip()
    )


def get_style(count, passed):
    """Determine if this directory is good based on  the number of clean
    files vs the number of files in total."""
    if passed == count:
        return ":good:"
    if passed != 0:
        return ":part:"
    return ":none:"


TOP_DIR = os.path.join(os.path.dirname(__file__), "../../..")
CLANG_DIR = os.path.join(os.path.dirname(__file__), "../..")
DOC_FILE = os.path.join(CLANG_DIR, "docs/ClangFormattedStatus.rst")
CLEAN_FILE = os.path.join(CLANG_DIR, "docs/tools/clang-formatted-files.txt")

rootdir = TOP_DIR

skipped_dirs = [".git", "test"]
suffixes = (".cpp", ".h")

RST_PREFIX = """\
.. raw:: html

      <style type="text/css">
        .total {{ font-weight: bold; }}
        .none {{ background-color: #FFFF99; height: 20px; display: inline-block; width: 120px; text-align: center; border-radius: 5px; color: #000000; font-family="Verdana,Geneva,DejaVu Sans,sans-serif" }}
        .part {{ background-color: #FFCC99; height: 20px; display: inline-block; width: 120px; text-align: center; border-radius: 5px; color: #000000; font-family="Verdana,Geneva,DejaVu Sans,sans-serif" }}
        .good {{ background-color: #2CCCFF; height: 20px; display: inline-block; width: 120px; text-align: center; border-radius: 5px; color: #000000; font-family="Verdana,Geneva,DejaVu Sans,sans-serif" }}
      </style>

.. role:: none
.. role:: part
.. role:: good
.. role:: total

======================
Clang Formatted Status
======================

:doc:`ClangFormattedStatus` describes the state of LLVM source
tree in terms of conformance to :doc:`ClangFormat` as of: {today} (`{sha} <https://github.com/llvm/llvm-project/commit/{sha}>`_).


.. list-table:: LLVM Clang-Format Status
   :widths: 50 25 25 25 25
   :header-rows: 1\n
   * - Directory
     - Total Files
     - Formatted Files
     - Unformatted Files
     - % Complete
"""

TABLE_ROW = """\
   * - {path}
     - {style}`{count}`
     - {style}`{passes}`
     - {style}`{fails}`
     - {style2}`{percent}%`
"""

FNULL = open(os.devnull, "w")


with open(DOC_FILE, "wb") as output:
    cleanfiles = open(CLEAN_FILE, "wb")
    sha = get_git_revision_short_hash()
    today = datetime.now().strftime("%B %d, %Y %H:%M:%S")
    output.write(bytes(RST_PREFIX.format(today=today, sha=sha).encode("utf-8")))

    total_files_count = 0
    total_files_pass = 0
    total_files_fail = 0
    for root, subdirs, files in os.walk(rootdir):
        for subdir in subdirs:
            if any(sd == subdir for sd in skipped_dirs):
                subdirs.remove(subdir)
            else:
                act_sub_dir = os.path.join(root, subdir)
                # Check the git index to see if the directory contains tracked
                # files. Reditect the output to a null descriptor as we aren't
                # interested in it, just the return code.
                git_check = subprocess.Popen(
                    ["git", "ls-files", "--error-unmatch", act_sub_dir],
                    stdout=FNULL,
                    stderr=FNULL,
                )
                if git_check.wait() != 0:
                    print("Skipping directory: ", act_sub_dir)
                    subdirs.remove(subdir)

        path = os.path.relpath(root, TOP_DIR)
        path = path.replace("\\", "/")

        file_count = 0
        file_pass = 0
        file_fail = 0
        for filename in files:
            file_path = os.path.join(root, filename)
            ext = os.path.splitext(file_path)[-1].lower()
            if not ext.endswith(suffixes):
                continue

            file_count += 1

            args = ["clang-format", "-n", file_path]
            cmd = subprocess.Popen(args, stderr=subprocess.PIPE)
            stdout, err = cmd.communicate()

            relpath = os.path.relpath(file_path, TOP_DIR)
            relpath = relpath.replace("\\", "/")
            if err.decode(sys.stdout.encoding).find(": warning:") > 0:
                print(relpath, ":", "FAIL")
                file_fail += 1
            else:
                print(relpath, ":", "PASS")
                file_pass += 1
                cleanfiles.write(bytes(relpath + "\n"))
                cleanfiles.flush()

        total_files_count += file_count
        total_files_pass += file_pass
        total_files_fail += file_fail

        if file_count > 0:
            percent = int(100.0 * (float(file_pass) / float(file_count)))
            style = get_style(file_count, file_pass)
            output.write(
                bytes(
                    TABLE_ROW.format(
                        path=path,
                        count=file_count,
                        passes=file_pass,
                        fails=file_fail,
                        percent=str(percent),
                        style="",
                        style2=style,
                    ).encode("utf-8")
                )
            )
            output.flush()

            print("----\n")
            print(path, file_count, file_pass, file_fail, percent)
            print("----\n")

    total_percent = float(total_files_pass) / float(total_files_count)
    percent_str = str(int(100.0 * total_percent))
    output.write(
        bytes(
            TABLE_ROW.format(
                path="Total",
                count=total_files_count,
                passes=total_files_pass,
                fails=total_files_fail,
                percent=percent_str,
                style=":total:",
                style2=":total:",
            ).encode("utf-8")
        )
    )
