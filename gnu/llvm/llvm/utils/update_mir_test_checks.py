#!/usr/bin/env python3

"""Updates FileCheck checks in MIR tests.

This script is a utility to update MIR based tests with new FileCheck
patterns.

The checks added by this script will cover the entire body of each
function it handles. Virtual registers used are given names via
FileCheck patterns, so if you do want to check a subset of the body it
should be straightforward to trim out the irrelevant parts. None of
the YAML metadata will be checked, other than function names, and fixedStack
if the --print-fixed-stack option is used.

If there are multiple llc commands in a test, the full set of checks
will be repeated for each different check pattern. Checks for patterns
that are common between different commands will be left as-is by
default, or removed if the --remove-common-prefixes flag is provided.
"""

from __future__ import print_function

import argparse
import collections
import glob
import os
import re
import subprocess
import sys

from UpdateTestChecks import common

MIR_FUNC_NAME_RE = re.compile(r" *name: *(?P<func>[A-Za-z0-9_.-]+)")
MIR_BODY_BEGIN_RE = re.compile(r" *body: *\|")
MIR_BASIC_BLOCK_RE = re.compile(r" *bb\.[0-9]+.*:$")
VREG_RE = re.compile(r"(%[0-9]+)(?:\.[a-z0-9_]+)?(?::[a-z0-9_]+)?(?:\([<>a-z0-9 ]+\))?")
MI_FLAGS_STR = (
    r"(frame-setup |frame-destroy |nnan |ninf |nsz |arcp |contract |afn "
    r"|reassoc |nuw |nsw |exact |nofpexcept |nomerge )*"
)
VREG_DEF_FLAGS_STR = r"(?:dead |undef )*"
VREG_DEF_RE = re.compile(
    r"^ *(?P<vregs>{2}{0}(?:, {2}{0})*) = "
    r"{1}(?P<opcode>[A-Zt][A-Za-z0-9_]+)".format(
        VREG_RE.pattern, MI_FLAGS_STR, VREG_DEF_FLAGS_STR
    )
)
MIR_PREFIX_DATA_RE = re.compile(r"^ *(;|bb.[0-9].*: *$|[a-z]+:( |$)|$)")

IR_FUNC_NAME_RE = re.compile(
    r"^\s*define\s+(?:internal\s+)?[^@]*@(?P<func>[A-Za-z0-9_.]+)\s*\("
)
IR_PREFIX_DATA_RE = re.compile(r"^ *(;|$)")

MIR_FUNC_RE = re.compile(
    r"^---$"
    r"\n"
    r"^ *name: *(?P<func>[A-Za-z0-9_.-]+)$"
    r".*?"
    r"(?:^ *fixedStack: *(\[\])? *\n"
    r"(?P<fixedStack>.*?)\n?"
    r"^ *stack:"
    r".*?)?"
    r"^ *body: *\|\n"
    r"(?P<body>.*?)\n"
    r"^\.\.\.$",
    flags=(re.M | re.S),
)


class LLC:
    def __init__(self, bin):
        self.bin = bin

    def __call__(self, args, ir):
        if ir.endswith(".mir"):
            args = "{} -x mir".format(args)
        with open(ir) as ir_file:
            stdout = subprocess.check_output(
                "{} {}".format(self.bin, args), shell=True, stdin=ir_file
            )
            if sys.version_info[0] > 2:
                stdout = stdout.decode()
            # Fix line endings to unix CR style.
            stdout = stdout.replace("\r\n", "\n")
        return stdout


class Run:
    def __init__(self, prefixes, cmd_args, triple):
        self.prefixes = prefixes
        self.cmd_args = cmd_args
        self.triple = triple

    def __getitem__(self, index):
        return [self.prefixes, self.cmd_args, self.triple][index]


def log(msg, verbose=True):
    if verbose:
        print(msg, file=sys.stderr)


def find_triple_in_ir(lines, verbose=False):
    for l in lines:
        m = common.TRIPLE_IR_RE.match(l)
        if m:
            return m.group(1)
    return None


def build_run_list(test, run_lines, verbose=False):
    run_list = []
    all_prefixes = []
    for l in run_lines:
        if "|" not in l:
            common.warn("Skipping unparsable RUN line: " + l)
            continue

        commands = [cmd.strip() for cmd in l.split("|", 1)]
        llc_cmd = commands[0]
        filecheck_cmd = commands[1] if len(commands) > 1 else ""
        common.verify_filecheck_prefixes(filecheck_cmd)

        if not llc_cmd.startswith("llc "):
            common.warn("Skipping non-llc RUN line: {}".format(l), test_file=test)
            continue
        if not filecheck_cmd.startswith("FileCheck "):
            common.warn(
                "Skipping non-FileChecked RUN line: {}".format(l), test_file=test
            )
            continue

        triple = None
        m = common.TRIPLE_ARG_RE.search(llc_cmd)
        if m:
            triple = m.group(1)
        # If we find -march but not -mtriple, use that.
        m = common.MARCH_ARG_RE.search(llc_cmd)
        if m and not triple:
            triple = "{}--".format(m.group(1))

        cmd_args = llc_cmd[len("llc") :].strip()
        cmd_args = cmd_args.replace("< %s", "").replace("%s", "").strip()
        check_prefixes = common.get_check_prefixes(filecheck_cmd)
        all_prefixes += check_prefixes

        run_list.append(Run(check_prefixes, cmd_args, triple))

    # Sort prefixes that are shared between run lines before unshared prefixes.
    # This causes us to prefer printing shared prefixes.
    for run in run_list:
        run.prefixes.sort(key=lambda prefix: -all_prefixes.count(prefix))

    return run_list


def find_functions_with_one_bb(lines, verbose=False):
    result = []
    cur_func = None
    bbs = 0
    for line in lines:
        m = MIR_FUNC_NAME_RE.match(line)
        if m:
            if bbs == 1:
                result.append(cur_func)
            cur_func = m.group("func")
            bbs = 0
        m = MIR_BASIC_BLOCK_RE.match(line)
        if m:
            bbs += 1
    if bbs == 1:
        result.append(cur_func)
    return result


class FunctionInfo:
    def __init__(self, body, fixedStack):
        self.body = body
        self.fixedStack = fixedStack

    def __eq__(self, other):
        if not isinstance(other, FunctionInfo):
            return False
        return self.body == other.body and self.fixedStack == other.fixedStack


def build_function_info_dictionary(
    test, raw_tool_output, triple, prefixes, func_dict, verbose
):
    for m in MIR_FUNC_RE.finditer(raw_tool_output):
        func = m.group("func")
        fixedStack = m.group("fixedStack")
        body = m.group("body")
        if verbose:
            log("Processing function: {}".format(func))
            for l in body.splitlines():
                log("  {}".format(l))

        # Vreg mangling
        mangled = []
        vreg_map = {}
        for func_line in body.splitlines(keepends=True):
            m = VREG_DEF_RE.match(func_line)
            if m:
                for vreg in VREG_RE.finditer(m.group("vregs")):
                    if vreg.group(1) in vreg_map:
                        name = vreg_map[vreg.group(1)]
                    else:
                        name = mangle_vreg(m.group("opcode"), vreg_map.values())
                        vreg_map[vreg.group(1)] = name
                    func_line = func_line.replace(
                        vreg.group(1), "[[{}:%[0-9]+]]".format(name), 1
                    )
            for number, name in vreg_map.items():
                func_line = re.sub(
                    r"{}\b".format(number), "[[{}]]".format(name), func_line
                )
            mangled.append(func_line)
        body = "".join(mangled)

        for prefix in prefixes:
            info = FunctionInfo(body, fixedStack)
            if func in func_dict[prefix]:
                if func_dict[prefix][func] != info:
                    func_dict[prefix][func] = None
            else:
                func_dict[prefix][func] = info


def add_checks_for_function(
    test, output_lines, run_list, func_dict, func_name, single_bb, args
):
    printed_prefixes = set()
    for run in run_list:
        for prefix in run.prefixes:
            if prefix in printed_prefixes:
                break
            if not func_dict[prefix][func_name]:
                continue
            if printed_prefixes:
                # Add some space between different check prefixes.
                indent = len(output_lines[-1]) - len(output_lines[-1].lstrip(" "))
                output_lines.append(" "*indent + ";")
            printed_prefixes.add(prefix)
            log("Adding {} lines for {}".format(prefix, func_name), args.verbose)
            add_check_lines(
                test,
                output_lines,
                prefix,
                func_name,
                single_bb,
                func_dict[prefix][func_name],
                args,
            )
            break
        else:
            common.warn(
                "Found conflicting asm for function: {}".format(func_name),
                test_file=test,
            )
    return output_lines


def add_check_lines(
    test, output_lines, prefix, func_name, single_bb, func_info: FunctionInfo, args
):
    func_body = func_info.body.splitlines()
    if single_bb:
        # Don't bother checking the basic block label for a single BB
        func_body.pop(0)

    if not func_body:
        common.warn(
            "Function has no instructions to check: {}".format(func_name),
            test_file=test,
        )
        return

    first_line = func_body[0]
    indent = len(first_line) - len(first_line.lstrip(" "))
    # A check comment, indented the appropriate amount
    check = "{:>{}}; {}".format("", indent, prefix)

    output_lines.append("{}-LABEL: name: {}".format(check, func_name))

    if args.print_fixed_stack:
        output_lines.append("{}: fixedStack:".format(check))
        for stack_line in func_info.fixedStack.splitlines():
            filecheck_directive = check + "-NEXT"
            output_lines.append("{}: {}".format(filecheck_directive, stack_line))

    first_check = True
    for func_line in func_body:
        if not func_line.strip():
            # The mir printer prints leading whitespace so we can't use CHECK-EMPTY:
            output_lines.append(check + "-NEXT: {{" + func_line + "$}}")
            continue
        filecheck_directive = check if first_check else check + "-NEXT"
        first_check = False
        check_line = "{}: {}".format(filecheck_directive, func_line[indent:]).rstrip()
        output_lines.append(check_line)


def mangle_vreg(opcode, current_names):
    base = opcode
    # Simplify some common prefixes and suffixes
    if opcode.startswith("G_"):
        base = base[len("G_") :]
    if opcode.endswith("_PSEUDO"):
        base = base[: len("_PSEUDO")]
    # Shorten some common opcodes with long-ish names
    base = dict(
        IMPLICIT_DEF="DEF",
        GLOBAL_VALUE="GV",
        CONSTANT="C",
        FCONSTANT="C",
        MERGE_VALUES="MV",
        UNMERGE_VALUES="UV",
        INTRINSIC="INT",
        INTRINSIC_W_SIDE_EFFECTS="INT",
        INSERT_VECTOR_ELT="IVEC",
        EXTRACT_VECTOR_ELT="EVEC",
        SHUFFLE_VECTOR="SHUF",
    ).get(base, base)
    # Avoid ambiguity when opcodes end in numbers
    if len(base.rstrip("0123456789")) < len(base):
        base += "_"

    i = 0
    for name in current_names:
        if name.rstrip("0123456789") == base:
            i += 1
    if i:
        return "{}{}".format(base, i)
    return base


def should_add_line_to_output(input_line, prefix_set):
    # Skip any check lines that we're handling as well as comments
    m = common.CHECK_RE.match(input_line)
    if (m and m.group(1) in prefix_set) or re.search("^[ \t]*;", input_line):
        return False
    return True


def update_test_file(args, test, autogenerated_note):
    with open(test) as fd:
        input_lines = [l.rstrip() for l in fd]

    triple_in_ir = find_triple_in_ir(input_lines, args.verbose)
    run_lines = common.find_run_lines(test, input_lines)
    run_list = build_run_list(test, run_lines, args.verbose)

    simple_functions = find_functions_with_one_bb(input_lines, args.verbose)

    func_dict = {}
    for run in run_list:
        for prefix in run.prefixes:
            func_dict.update({prefix: dict()})
    for prefixes, llc_args, triple_in_cmd in run_list:
        log("Extracted LLC cmd: llc {}".format(llc_args), args.verbose)
        log("Extracted FileCheck prefixes: {}".format(prefixes), args.verbose)

        raw_tool_output = args.llc_binary(llc_args, test)
        if not triple_in_cmd and not triple_in_ir:
            common.warn("No triple found: skipping file", test_file=test)
            return

        build_function_info_dictionary(
            test,
            raw_tool_output,
            triple_in_cmd or triple_in_ir,
            prefixes,
            func_dict,
            args.verbose,
        )

    state = "toplevel"
    func_name = None
    prefix_set = set([prefix for run in run_list for prefix in run.prefixes])
    log("Rewriting FileCheck prefixes: {}".format(prefix_set), args.verbose)

    output_lines = []
    output_lines.append(autogenerated_note)

    for input_line in input_lines:
        if input_line == autogenerated_note:
            continue

        if state == "toplevel":
            m = IR_FUNC_NAME_RE.match(input_line)
            if m:
                state = "ir function prefix"
                func_name = m.group("func")
            if input_line.rstrip("| \r\n") == "---":
                state = "document"
            output_lines.append(input_line)
        elif state == "document":
            m = MIR_FUNC_NAME_RE.match(input_line)
            if m:
                state = "mir function metadata"
                func_name = m.group("func")
            if input_line.strip() == "...":
                state = "toplevel"
                func_name = None
            if should_add_line_to_output(input_line, prefix_set):
                output_lines.append(input_line)
        elif state == "mir function metadata":
            if should_add_line_to_output(input_line, prefix_set):
                output_lines.append(input_line)
            m = MIR_BODY_BEGIN_RE.match(input_line)
            if m:
                if func_name in simple_functions:
                    # If there's only one block, put the checks inside it
                    state = "mir function prefix"
                    continue
                state = "mir function body"
                add_checks_for_function(
                    test,
                    output_lines,
                    run_list,
                    func_dict,
                    func_name,
                    single_bb=False,
                    args=args,
                )
        elif state == "mir function prefix":
            m = MIR_PREFIX_DATA_RE.match(input_line)
            if not m:
                state = "mir function body"
                add_checks_for_function(
                    test,
                    output_lines,
                    run_list,
                    func_dict,
                    func_name,
                    single_bb=True,
                    args=args,
                )

            if should_add_line_to_output(input_line, prefix_set):
                output_lines.append(input_line)
        elif state == "mir function body":
            if input_line.strip() == "...":
                state = "toplevel"
                func_name = None
            if should_add_line_to_output(input_line, prefix_set):
                output_lines.append(input_line)
        elif state == "ir function prefix":
            m = IR_PREFIX_DATA_RE.match(input_line)
            if not m:
                state = "ir function body"
                add_checks_for_function(
                    test,
                    output_lines,
                    run_list,
                    func_dict,
                    func_name,
                    single_bb=False,
                    args=args,
                )

            if should_add_line_to_output(input_line, prefix_set):
                output_lines.append(input_line)
        elif state == "ir function body":
            if input_line.strip() == "}":
                state = "toplevel"
                func_name = None
            if should_add_line_to_output(input_line, prefix_set):
                output_lines.append(input_line)

    log("Writing {} lines to {}...".format(len(output_lines), test), args.verbose)

    with open(test, "wb") as fd:
        fd.writelines(["{}\n".format(l).encode("utf-8") for l in output_lines])


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument(
        "--llc-binary",
        default="llc",
        type=LLC,
        help='The "llc" binary to generate the test case with',
    )
    parser.add_argument(
        "--print-fixed-stack",
        action="store_true",
        help="Add check lines for fixedStack",
    )
    parser.add_argument("tests", nargs="+")
    args = common.parse_commandline_args(parser)

    script_name = os.path.basename(__file__)
    for ti in common.itertests(args.tests, parser, script_name="utils/" + script_name):
        try:
            update_test_file(ti.args, ti.path, ti.test_autogenerated_note)
        except Exception:
            common.warn("Error processing file", test_file=ti.path)
            raise


if __name__ == "__main__":
    main()
