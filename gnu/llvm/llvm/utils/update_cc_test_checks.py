#!/usr/bin/env python3
"""A utility to update LLVM IR CHECK lines in C/C++ FileCheck test files.

Example RUN lines in .c/.cc test files:

// RUN: %clang -emit-llvm -S %s -o - -O2 | FileCheck %s
// RUN: %clangxx -emit-llvm -S %s -o - -O2 | FileCheck -check-prefix=CHECK-A %s

Usage:

% utils/update_cc_test_checks.py --llvm-bin=release/bin test/a.cc
% utils/update_cc_test_checks.py --clang=release/bin/clang /tmp/c/a.cc
"""

from __future__ import print_function

import argparse
import collections
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

from UpdateTestChecks import common

SUBST = {
    "%clang": [],
    "%clang_cc1": ["-cc1"],
    "%clangxx": ["--driver-mode=g++"],
}


def get_line2func_list(args, clang_args):
    ret = collections.defaultdict(list)
    # Use clang's JSON AST dump to get the mangled name
    json_dump_args = [args.clang] + clang_args + ["-fsyntax-only", "-o", "-"]
    if "-cc1" not in json_dump_args:
        # For tests that invoke %clang instead if %clang_cc1 we have to use
        # -Xclang -ast-dump=json instead:
        json_dump_args.append("-Xclang")
    json_dump_args.append("-ast-dump=json")
    common.debug("Running", " ".join(json_dump_args))

    popen = subprocess.Popen(
        json_dump_args,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
    )
    stdout, stderr = popen.communicate()
    if popen.returncode != 0:
        sys.stderr.write("Failed to run " + " ".join(json_dump_args) + "\n")
        sys.stderr.write(stderr)
        sys.stderr.write(stdout)
        sys.exit(2)

    # Parse the clang JSON and add all children of type FunctionDecl.
    # TODO: Should we add checks for global variables being emitted?
    def parse_clang_ast_json(node, loc, search):
        node_kind = node["kind"]
        # Recurse for the following nodes that can contain nested function decls:
        if node_kind in (
            "NamespaceDecl",
            "LinkageSpecDecl",
            "TranslationUnitDecl",
            "CXXRecordDecl",
            "ClassTemplateSpecializationDecl",
        ):
            # Specializations must use the loc from the specialization, not the
            # template, and search for the class's spelling as the specialization
            # does not mention the method names in the source.
            if node_kind == "ClassTemplateSpecializationDecl":
                inner_loc = node["loc"]
                inner_search = node["name"]
            else:
                inner_loc = None
                inner_search = None
            if "inner" in node:
                for inner in node["inner"]:
                    parse_clang_ast_json(inner, inner_loc, inner_search)
        # Otherwise we ignore everything except functions:
        if node_kind not in (
            "FunctionDecl",
            "CXXMethodDecl",
            "CXXConstructorDecl",
            "CXXDestructorDecl",
            "CXXConversionDecl",
        ):
            return
        if loc is None:
            loc = node["loc"]
        if node.get("isImplicit") is True and node.get("storageClass") == "extern":
            common.debug("Skipping builtin function:", node["name"], "@", loc)
            return
        common.debug("Found function:", node["kind"], node["name"], "@", loc)
        line = loc.get("line")
        # If there is no line it is probably a builtin function -> skip
        if line is None:
            common.debug(
                "Skipping function without line number:", node["name"], "@", loc
            )
            return

        # If there is no 'inner' object, it is a function declaration and we can
        # skip it. However, function declarations may also contain an 'inner' list,
        # but in that case it will only contains ParmVarDecls. If we find an entry
        # that is not a ParmVarDecl, we know that this is a function definition.
        has_body = False
        if "inner" in node:
            for i in node["inner"]:
                if i.get("kind", "ParmVarDecl") != "ParmVarDecl":
                    has_body = True
                    break
        if not has_body:
            common.debug("Skipping function without body:", node["name"], "@", loc)
            return
        spell = node["name"]
        if search is None:
            search = spell
        mangled = node.get("mangledName", spell)
        ret[int(line) - 1].append((spell, mangled, search))

    ast = json.loads(stdout)
    if ast["kind"] != "TranslationUnitDecl":
        common.error("Clang AST dump JSON format changed?")
        sys.exit(2)
    parse_clang_ast_json(ast, None, None)

    for line, funcs in sorted(ret.items()):
        for func in funcs:
            common.debug(
                "line {}: found function {}".format(line + 1, func), file=sys.stderr
            )
    if not ret:
        common.warn("Did not find any functions using", " ".join(json_dump_args))
    return ret


def str_to_commandline(value):
    if not value:
        return []
    return shlex.split(value)


def infer_dependent_args(args):
    if not args.clang:
        if not args.llvm_bin:
            args.clang = "clang"
        else:
            args.clang = os.path.join(args.llvm_bin, "clang")
    if not args.opt:
        if not args.llvm_bin:
            args.opt = "opt"
        else:
            args.opt = os.path.join(args.llvm_bin, "opt")


def find_executable(executable):
    _, ext = os.path.splitext(executable)
    if sys.platform == "win32" and ext != ".exe":
        executable = executable + ".exe"

    return shutil.which(executable)


def config():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument("--llvm-bin", help="llvm $prefix/bin path")
    parser.add_argument(
        "--clang", help='"clang" executable, defaults to $llvm_bin/clang'
    )
    parser.add_argument(
        "--clang-args",
        default=[],
        type=str_to_commandline,
        help="Space-separated extra args to clang, e.g. --clang-args=-v",
    )
    parser.add_argument("--opt", help='"opt" executable, defaults to $llvm_bin/opt')
    parser.add_argument(
        "--functions",
        nargs="+",
        help="A list of function name regexes. "
        "If specified, update CHECK lines for functions matching at least one regex",
    )
    parser.add_argument(
        "--x86_extra_scrub",
        action="store_true",
        help="Use more regex for x86 matching to reduce diffs between various subtargets",
    )
    parser.add_argument(
        "--function-signature",
        action="store_true",
        help="Keep function signature information around for the check line",
    )
    parser.add_argument(
        "--check-attributes",
        action="store_true",
        help='Check "Function Attributes" for functions',
    )
    parser.add_argument(
        "--check-globals",
        nargs="?",
        const="all",
        default="default",
        choices=["none", "smart", "all"],
        help="Check global entries (global variables, metadata, attribute sets, ...) for functions",
    )
    parser.add_argument("tests", nargs="+")
    args = common.parse_commandline_args(parser)
    infer_dependent_args(args)

    if not find_executable(args.clang):
        print("Please specify --llvm-bin or --clang", file=sys.stderr)
        sys.exit(1)

    # Determine the builtin includes directory so that we can update tests that
    # depend on the builtin headers. See get_clang_builtin_include_dir() and
    # use_clang() in llvm/utils/lit/lit/llvm/config.py.
    try:
        builtin_include_dir = (
            subprocess.check_output([args.clang, "-print-file-name=include"])
            .decode()
            .strip()
        )
        SUBST["%clang_cc1"] = [
            "-cc1",
            "-internal-isystem",
            builtin_include_dir,
            "-nostdsysteminc",
        ]
    except subprocess.CalledProcessError:
        common.warn(
            "Could not determine clang builtins directory, some tests "
            "might not update correctly."
        )

    if not find_executable(args.opt):
        # Many uses of this tool will not need an opt binary, because it's only
        # needed for updating a test that runs clang | opt | FileCheck. So we
        # defer this error message until we find that opt is actually needed.
        args.opt = None

    return args, parser


def get_function_body(builder, args, filename, clang_args, extra_commands, prefixes):
    # TODO Clean up duplication of asm/common build_function_body_dictionary
    # Invoke external tool and extract function bodies.
    raw_tool_output = common.invoke_tool(args.clang, clang_args, filename)
    for extra_command in extra_commands:
        extra_args = shlex.split(extra_command)
        with tempfile.NamedTemporaryFile() as f:
            f.write(raw_tool_output.encode())
            f.flush()
            if extra_args[0] == "opt":
                if args.opt is None:
                    print(
                        filename,
                        "needs to run opt. " "Please specify --llvm-bin or --opt",
                        file=sys.stderr,
                    )
                    sys.exit(1)
                extra_args[0] = args.opt
            raw_tool_output = common.invoke_tool(extra_args[0], extra_args[1:], f.name)
    if "-emit-llvm" in clang_args:
        builder.process_run_line(
            common.OPT_FUNCTION_RE, common.scrub_body, raw_tool_output, prefixes
        )
        builder.processed_prefixes(prefixes)
    else:
        print(
            "The clang command line should include -emit-llvm as asm tests "
            "are discouraged in Clang testsuite.",
            file=sys.stderr,
        )
        sys.exit(1)


def exec_run_line(exe):
    popen = subprocess.Popen(
        exe, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True
    )
    stdout, stderr = popen.communicate()
    if popen.returncode != 0:
        sys.stderr.write("Failed to run " + " ".join(exe) + "\n")
        sys.stderr.write(stderr)
        sys.stderr.write(stdout)
        sys.exit(3)


def main():
    initial_args, parser = config()
    script_name = os.path.basename(__file__)

    for ti in common.itertests(
        initial_args.tests,
        parser,
        "utils/" + script_name,
        comment_prefix="//",
        argparse_callback=infer_dependent_args,
    ):
        # Build a list of filechecked and non-filechecked RUN lines.
        run_list = []
        line2func_list = collections.defaultdict(list)

        subs = {
            "%s": ti.path,
            "%t": tempfile.NamedTemporaryFile().name,
            "%S": os.path.dirname(ti.path),
        }

        for l in ti.run_lines:
            commands = [cmd.strip() for cmd in l.split("|")]

            triple_in_cmd = None
            m = common.TRIPLE_ARG_RE.search(commands[0])
            if m:
                triple_in_cmd = m.groups()[0]

            # Parse executable args.
            exec_args = shlex.split(commands[0])
            # Execute non-clang runline.
            if exec_args[0] not in SUBST:
                # Do lit-like substitutions.
                for s in subs:
                    exec_args = [
                        i.replace(s, subs[s]) if s in i else i for i in exec_args
                    ]
                run_list.append((None, exec_args, None, None))
                continue
            # This is a clang runline, apply %clang substitution rule, do lit-like substitutions,
            # and append args.clang_args
            clang_args = exec_args
            clang_args[0:1] = SUBST[clang_args[0]]
            for s in subs:
                clang_args = [
                    i.replace(s, subs[s]) if s in i else i for i in clang_args
                ]
            clang_args += ti.args.clang_args

            # Extract -check-prefix in FileCheck args
            filecheck_cmd = commands[-1]
            common.verify_filecheck_prefixes(filecheck_cmd)
            if not filecheck_cmd.startswith("FileCheck "):
                # Execute non-filechecked clang runline.
                exe = [ti.args.clang] + clang_args
                run_list.append((None, exe, None, None))
                continue

            check_prefixes = common.get_check_prefixes(filecheck_cmd)
            run_list.append((check_prefixes, clang_args, commands[1:-1], triple_in_cmd))

        # Execute clang, generate LLVM IR, and extract functions.

        # Store only filechecked runlines.
        filecheck_run_list = [i for i in run_list if i[0]]
        ginfo = common.make_ir_generalizer(version=ti.args.version)
        builder = common.FunctionTestBuilder(
            run_list=filecheck_run_list,
            flags=ti.args,
            scrubber_args=[],
            path=ti.path,
            ginfo=ginfo,
        )

        for prefixes, args, extra_commands, triple_in_cmd in run_list:
            # Execute non-filechecked runline.
            if not prefixes:
                print(
                    "NOTE: Executing non-FileChecked RUN line: " + " ".join(args),
                    file=sys.stderr,
                )
                exec_run_line(args)
                continue

            clang_args = args
            common.debug("Extracted clang cmd: clang {}".format(clang_args))
            common.debug("Extracted FileCheck prefixes: {}".format(prefixes))

            get_function_body(
                builder, ti.args, ti.path, clang_args, extra_commands, prefixes
            )

            # Invoke clang -Xclang -ast-dump=json to get mapping from start lines to
            # mangled names. Forward all clang args for now.
            for k, v in get_line2func_list(ti.args, clang_args).items():
                line2func_list[k].extend(v)

        func_dict = builder.finish_and_get_func_dict()
        global_vars_seen_dict = {}
        prefix_set = set([prefix for p in filecheck_run_list for prefix in p[0]])
        output_lines = []
        has_checked_pre_function_globals = False

        include_generated_funcs = common.find_arg_in_test(
            ti,
            lambda args: ti.args.include_generated_funcs,
            "--include-generated-funcs",
            True,
        )
        generated_prefixes = []
        if include_generated_funcs:
            # Generate the appropriate checks for each function.  We need to emit
            # these in the order according to the generated output so that CHECK-LABEL
            # works properly.  func_order provides that.

            # It turns out that when clang generates functions (for example, with
            # -fopenmp), it can sometimes cause functions to be re-ordered in the
            # output, even functions that exist in the source file.  Therefore we
            # can't insert check lines before each source function and instead have to
            # put them at the end.  So the first thing to do is dump out the source
            # lines.
            common.dump_input_lines(output_lines, ti, prefix_set, "//")

            # Now generate all the checks.
            def check_generator(my_output_lines, prefixes, func):
                return common.add_ir_checks(
                    my_output_lines,
                    "//",
                    prefixes,
                    func_dict,
                    func,
                    False,
                    ti.args.function_signature,
                    ginfo,
                    global_vars_seen_dict,
                    is_filtered=builder.is_filtered(),
                )

            if ti.args.check_globals != 'none':
                generated_prefixes.extend(
                    common.add_global_checks(
                        builder.global_var_dict(),
                        "//",
                        run_list,
                        output_lines,
                        ginfo,
                        global_vars_seen_dict,
                        False,
                        True,
                        ti.args.check_globals,
                    )
                )
            generated_prefixes.extend(
                common.add_checks_at_end(
                    output_lines,
                    filecheck_run_list,
                    builder.func_order(),
                    "//",
                    lambda my_output_lines, prefixes, func: check_generator(
                        my_output_lines, prefixes, func
                    ),
                )
            )
        else:
            # Normal mode.  Put checks before each source function.
            for line_info in ti.iterlines(output_lines):
                idx = line_info.line_number
                line = line_info.line
                args = line_info.args
                include_line = True
                m = common.CHECK_RE.match(line)
                if m and m.group(1) in prefix_set:
                    continue  # Don't append the existing CHECK lines
                # Skip special separator comments added by commmon.add_global_checks.
                if line.strip() == "//" + common.SEPARATOR:
                    continue
                if idx in line2func_list:
                    added = set()
                    for spell, mangled, search in line2func_list[idx]:
                        # One line may contain multiple function declarations.
                        # Skip if the mangled name has been added before.
                        # The line number may come from an included file, we simply require
                        # the search string (normally the function's spelling name, but is
                        # the class's spelling name for class specializations) to appear on
                        # the line to exclude functions from other files.
                        if mangled in added or search not in line:
                            continue
                        if args.functions is None or any(
                            re.search(regex, spell) for regex in args.functions
                        ):
                            last_line = output_lines[-1].strip()
                            while last_line == "//":
                                # Remove the comment line since we will generate a new  comment
                                # line as part of common.add_ir_checks()
                                output_lines.pop()
                                last_line = output_lines[-1].strip()
                            if (
                                ti.args.check_globals != 'none'
                                and not has_checked_pre_function_globals
                            ):
                                generated_prefixes.extend(
                                    common.add_global_checks(
                                        builder.global_var_dict(),
                                        "//",
                                        run_list,
                                        output_lines,
                                        ginfo,
                                        global_vars_seen_dict,
                                        False,
                                        True,
                                        ti.args.check_globals,
                                    )
                                )
                                has_checked_pre_function_globals = True
                            if added:
                                output_lines.append("//")
                            added.add(mangled)
                            generated_prefixes.extend(
                                common.add_ir_checks(
                                    output_lines,
                                    "//",
                                    filecheck_run_list,
                                    func_dict,
                                    mangled,
                                    False,
                                    args.function_signature,
                                    ginfo,
                                    global_vars_seen_dict,
                                    is_filtered=builder.is_filtered(),
                                )
                            )
                            if line.rstrip("\n") == "//":
                                include_line = False

                if include_line:
                    output_lines.append(line.rstrip("\n"))

        if ti.args.check_globals != 'none':
            generated_prefixes.extend(
                common.add_global_checks(
                    builder.global_var_dict(),
                    "//",
                    run_list,
                    output_lines,
                    ginfo,
                    global_vars_seen_dict,
                    False,
                    False,
                    ti.args.check_globals,
                )
            )
        if ti.args.gen_unused_prefix_body:
            output_lines.extend(
                ti.get_checks_for_unused_prefixes(run_list, generated_prefixes)
            )
        common.debug("Writing %d lines to %s..." % (len(output_lines), ti.path))
        with open(ti.path, "wb") as f:
            f.writelines(["{}\n".format(l).encode("utf-8") for l in output_lines])

    return 0


if __name__ == "__main__":
    sys.exit(main())
