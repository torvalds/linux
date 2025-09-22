#!/usr/bin/env python3

"""Helps to keep BUILD.gn files in sync with the corresponding CMakeLists.txt.

For each BUILD.gn file in the tree, checks if the list of cpp files in
it is identical to the list of cpp files in the corresponding CMakeLists.txt
file, and prints the difference if not.

Also checks that each CMakeLists.txt file below unittests/ folders that define
binaries have corresponding BUILD.gn files.

If --write is passed, tries to write modified .gn files and adds one git
commit for each cmake commit this merges. If an error is reported, the state
of HEAD is unspecified; run `git reset --hard origin/main` if this happens.
"""

from collections import defaultdict
import os
import re
import subprocess
import sys


def patch_gn_file(gn_file, add, remove):
    with open(gn_file) as f:
        gn_contents = f.read()
    if add:
        srcs_tok = "sources = ["
        tokloc = gn_contents.find(srcs_tok)
        while gn_contents.startswith("sources = []", tokloc):
            tokloc = gn_contents.find(srcs_tok, tokloc + 1)
        if tokloc == -1:
            raise ValueError(gn_file + ": No source list")
        if gn_contents.find(srcs_tok, tokloc + 1) != -1:
            raise ValueError(gn_file + ": Multiple source lists")
        if gn_contents.find("# NOSORT", 0, tokloc) != -1:
            raise ValueError(gn_file + ": Found # NOSORT, needs manual merge")
        tokloc += len(srcs_tok)
        for a in add:
            gn_contents = gn_contents[:tokloc] + ('"%s",' % a) + gn_contents[tokloc:]
    for r in remove:
        gn_contents = gn_contents.replace('"%s",' % r, "")
    with open(gn_file, "w") as f:
        f.write(gn_contents)

    # Run `gn format`.
    gn = os.path.join(os.path.dirname(__file__), "..", "gn.py")
    subprocess.check_call([sys.executable, gn, "format", "-q", gn_file])


def sync_source_lists(write):
    # Use shell=True on Windows in case git is a bat file.
    def git(args):
        subprocess.check_call(["git"] + args, shell=os.name == "nt")

    def git_out(args):
        return subprocess.check_output(
            ["git"] + args, shell=os.name == "nt", universal_newlines=True
        )

    gn_files = git_out(["ls-files", "*BUILD.gn"]).splitlines()

    # Matches e.g. |   "foo.cpp",|, captures |foo| in group 1.
    gn_cpp_re = re.compile(r'^\s*"([^$"]+\.(?:cpp|c|h|S))",$', re.MULTILINE)
    # Matches e.g. |   bar_sources = [ "foo.cpp" ]|, captures |foo| in group 1.
    gn_cpp_re2 = re.compile(
        r'^\s*(?:.*_)?sources \+?= \[ "([^$"]+\.(?:cpp|c|h|S))" ]$', re.MULTILINE
    )
    # Matches e.g. |   foo.cpp|, captures |foo| in group 1.
    cmake_cpp_re = re.compile(r"^\s*([A-Za-z_0-9./-]+\.(?:cpp|c|h|S))$", re.MULTILINE)

    changes_by_rev = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))

    def find_gitrev(touched_line, in_file):
        # re.escape() escapes e.g. '-', which works in practice but has
        # undefined behavior according to the POSIX extended regex spec.
        posix_re_escape = lambda s: re.sub(r"([.[{()\\*+?|^$])", r"\\\1", s)
        cmd = [
            "log",
            "--format=%h",
            "-1",
            "--pickaxe-regex",
            # `\<` / `\>` cause issues on Windows (and is a GNU extension).
            # `\b` is a GNU extension and stopped working in Apple Git-143
            # (Xcode 13.3).
            # `[:space:]` is over 10x faster than `^[:alnum:]` and hopefully
            # good enough.
            r"-S[[:space:]]%s[[:space:]]" % posix_re_escape(touched_line),
            in_file,
        ]
        return git_out(cmd).rstrip()

    # Collect changes to gn files, grouped by revision.
    for gn_file in gn_files:
        # The CMakeLists.txt for llvm/utils/gn/secondary/foo/BUILD.gn is
        # at foo/CMakeLists.txt.
        strip_prefix = "llvm/utils/gn/secondary/"
        if not gn_file.startswith(strip_prefix):
            continue
        cmake_file = os.path.join(
            os.path.dirname(gn_file[len(strip_prefix) :]), "CMakeLists.txt"
        )
        if not os.path.exists(cmake_file):
            continue

        def get_sources(source_re, text):
            return set([m.group(1) for m in source_re.finditer(text)])

        gn_cpp = get_sources(gn_cpp_re, open(gn_file).read())
        gn_cpp |= get_sources(gn_cpp_re2, open(gn_file).read())
        cmake_cpp = get_sources(cmake_cpp_re, open(cmake_file).read())

        if gn_cpp == cmake_cpp:
            continue

        def by_rev(files, key):
            for f in files:
                rev = find_gitrev(f, cmake_file)
                changes_by_rev[rev][gn_file][key].append(f)

        by_rev(sorted(cmake_cpp - gn_cpp), "add")
        by_rev(sorted(gn_cpp - cmake_cpp), "remove")

    # Output necessary changes grouped by revision.
    for rev in sorted(changes_by_rev):
        commit_url = 'https://github.com/llvm/llvm-project/commit'
        print("[gn build] Port {0} -- {1}/{0}".format(rev, commit_url))
        for gn_file, data in sorted(changes_by_rev[rev].items()):
            add = data.get("add", [])
            remove = data.get("remove", [])
            if write:
                patch_gn_file(gn_file, add, remove)
                git(["add", gn_file])
            else:
                print("  " + gn_file)
                if add:
                    print("   add:\n" + "\n".join('    "%s",' % a for a in add))
                if remove:
                    print("   remove:\n    " + "\n    ".join(remove))
                print()
        if write:
            git(["commit", "-m", "[gn build] Port %s" % rev])
        else:
            print()

    return bool(changes_by_rev) and not write


def sync_unittests():
    # Matches e.g. |add_llvm_unittest_with_input_files|.
    unittest_re = re.compile(r"^add_\S+_unittest", re.MULTILINE)

    checked = ["bolt", "clang", "clang-tools-extra", "lld", "llvm"]
    changed = False
    for c in checked:
        for root, _, _ in os.walk(os.path.join(c, "unittests")):
            cmake_file = os.path.join(root, "CMakeLists.txt")
            if not os.path.exists(cmake_file):
                continue
            if not unittest_re.search(open(cmake_file).read()):
                continue  # Skip CMake files that just add subdirectories.
            gn_file = os.path.join("llvm/utils/gn/secondary", root, "BUILD.gn")
            if not os.path.exists(gn_file):
                changed = True
                print(
                    "missing GN file %s for unittest CMake file %s"
                    % (gn_file, cmake_file)
                )
    return changed


def main():
    src = sync_source_lists(len(sys.argv) > 1 and sys.argv[1] == "--write")
    tests = sync_unittests()
    if src or tests:
        sys.exit(1)


if __name__ == "__main__":
    main()
