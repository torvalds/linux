from __future__ import print_function

import argparse
import bisect
import collections
import copy
import glob
import itertools
import os
import re
import subprocess
import sys
import shlex

from typing import List, Mapping, Set

##### Common utilities for update_*test_checks.py


_verbose = False
_prefix_filecheck_ir_name = ""

"""
Version changelog:

1: Initial version, used by tests that don't specify --version explicitly.
2: --function-signature is now enabled by default and also checks return
   type/attributes.
3: Opening parenthesis of function args is kept on the first LABEL line
   in case arguments are split to a separate SAME line.
4: --check-globals now has a third option ('smart'). The others are now called
   'none' and 'all'. 'smart' is the default.
5: Basic block labels are matched by FileCheck expressions
"""
DEFAULT_VERSION = 5


SUPPORTED_ANALYSES = {
    "Branch Probability Analysis",
    "Cost Model Analysis",
    "Loop Access Analysis",
    "Scalar Evolution Analysis",
}


class Regex(object):
    """Wrap a compiled regular expression object to allow deep copy of a regexp.
    This is required for the deep copy done in do_scrub.

    """

    def __init__(self, regex):
        self.regex = regex

    def __deepcopy__(self, memo):
        result = copy.copy(self)
        result.regex = self.regex
        return result

    def search(self, line):
        return self.regex.search(line)

    def sub(self, repl, line):
        return self.regex.sub(repl, line)

    def pattern(self):
        return self.regex.pattern

    def flags(self):
        return self.regex.flags


class Filter(Regex):
    """Augment a Regex object with a flag indicating whether a match should be
    added (!is_filter_out) or removed (is_filter_out) from the generated checks.

    """

    def __init__(self, regex, is_filter_out):
        super(Filter, self).__init__(regex)
        self.is_filter_out = is_filter_out

    def __deepcopy__(self, memo):
        result = copy.deepcopy(super(Filter, self), memo)
        result.is_filter_out = copy.deepcopy(self.is_filter_out, memo)
        return result


def parse_commandline_args(parser):
    class RegexAction(argparse.Action):
        """Add a regular expression option value to a list of regular expressions.
        This compiles the expression, wraps it in a Regex and adds it to the option
        value list."""

        def __init__(self, option_strings, dest, nargs=None, **kwargs):
            if nargs is not None:
                raise ValueError("nargs not allowed")
            super(RegexAction, self).__init__(option_strings, dest, **kwargs)

        def do_call(self, namespace, values, flags):
            value_list = getattr(namespace, self.dest)
            if value_list is None:
                value_list = []

            try:
                value_list.append(Regex(re.compile(values, flags)))
            except re.error as error:
                raise ValueError(
                    "{}: Invalid regular expression '{}' ({})".format(
                        option_string, error.pattern, error.msg
                    )
                )

            setattr(namespace, self.dest, value_list)

        def __call__(self, parser, namespace, values, option_string=None):
            self.do_call(namespace, values, 0)

    class FilterAction(RegexAction):
        """Add a filter to a list of filter option values."""

        def __init__(self, option_strings, dest, nargs=None, **kwargs):
            super(FilterAction, self).__init__(option_strings, dest, nargs, **kwargs)

        def __call__(self, parser, namespace, values, option_string=None):
            super(FilterAction, self).__call__(parser, namespace, values, option_string)

            value_list = getattr(namespace, self.dest)

            is_filter_out = option_string == "--filter-out"

            value_list[-1] = Filter(value_list[-1].regex, is_filter_out)

            setattr(namespace, self.dest, value_list)

    filter_group = parser.add_argument_group(
        "filtering",
        """Filters are applied to each output line according to the order given. The
    first matching filter terminates filter processing for that current line.""",
    )

    filter_group.add_argument(
        "--filter",
        action=FilterAction,
        dest="filters",
        metavar="REGEX",
        help="Only include lines matching REGEX (may be specified multiple times)",
    )
    filter_group.add_argument(
        "--filter-out",
        action=FilterAction,
        dest="filters",
        metavar="REGEX",
        help="Exclude lines matching REGEX",
    )

    parser.add_argument(
        "--include-generated-funcs",
        action="store_true",
        help="Output checks for functions not in source",
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Show verbose output"
    )
    parser.add_argument(
        "-u",
        "--update-only",
        action="store_true",
        help="Only update test if it was already autogened",
    )
    parser.add_argument(
        "--force-update",
        action="store_true",
        help="Update test even if it was autogened by a different script",
    )
    parser.add_argument(
        "--enable",
        action="store_true",
        dest="enabled",
        default=True,
        help="Activate CHECK line generation from this point forward",
    )
    parser.add_argument(
        "--disable",
        action="store_false",
        dest="enabled",
        help="Deactivate CHECK line generation from this point forward",
    )
    parser.add_argument(
        "--replace-value-regex",
        nargs="+",
        default=[],
        help="List of regular expressions to replace matching value names",
    )
    parser.add_argument(
        "--prefix-filecheck-ir-name",
        default="",
        help="Add a prefix to FileCheck IR value names to avoid conflicts with scripted names",
    )
    parser.add_argument(
        "--global-value-regex",
        nargs="+",
        default=[],
        help="List of regular expressions that a global value declaration must match to generate a check (has no effect if checking globals is not enabled)",
    )
    parser.add_argument(
        "--global-hex-value-regex",
        nargs="+",
        default=[],
        help="List of regular expressions such that, for matching global value declarations, literal integer values should be encoded in hex in the associated FileCheck directives",
    )
    # FIXME: in 3.9, we can use argparse.BooleanOptionalAction. At that point,
    # we need to rename the flag to just -generate-body-for-unused-prefixes.
    parser.add_argument(
        "--no-generate-body-for-unused-prefixes",
        action="store_false",
        dest="gen_unused_prefix_body",
        default=True,
        help="Generate a function body that always matches for unused prefixes. This is useful when unused prefixes are desired, and it avoids needing to annotate each FileCheck as allowing them.",
    )
    # This is the default when regenerating existing tests. The default when
    # generating new tests is determined by DEFAULT_VERSION.
    parser.add_argument(
        "--version", type=int, default=1, help="The version of output format"
    )
    args = parser.parse_args()
    # TODO: This should not be handled differently from the other options
    global _verbose, _global_value_regex, _global_hex_value_regex
    _verbose = args.verbose
    _global_value_regex = args.global_value_regex
    _global_hex_value_regex = args.global_hex_value_regex
    return args


def parse_args(parser, argv):
    args = parser.parse_args(argv)
    if args.version >= 2:
        args.function_signature = True
    # TODO: This should not be handled differently from the other options
    global _verbose, _global_value_regex, _global_hex_value_regex
    _verbose = args.verbose
    _global_value_regex = args.global_value_regex
    _global_hex_value_regex = args.global_hex_value_regex
    if "check_globals" in args and args.check_globals == "default":
        args.check_globals = "none" if args.version < 4 else "smart"
    return args


class InputLineInfo(object):
    def __init__(self, line, line_number, args, argv):
        self.line = line
        self.line_number = line_number
        self.args = args
        self.argv = argv


class TestInfo(object):
    def __init__(
        self,
        test,
        parser,
        script_name,
        input_lines,
        args,
        argv,
        comment_prefix,
        argparse_callback,
    ):
        self.parser = parser
        self.argparse_callback = argparse_callback
        self.path = test
        self.args = args
        if args.prefix_filecheck_ir_name:
            global _prefix_filecheck_ir_name
            _prefix_filecheck_ir_name = args.prefix_filecheck_ir_name
        self.argv = argv
        self.input_lines = input_lines
        self.run_lines = find_run_lines(test, self.input_lines)
        self.comment_prefix = comment_prefix
        if self.comment_prefix is None:
            if self.path.endswith(".mir"):
                self.comment_prefix = "#"
            else:
                self.comment_prefix = ";"
        self.autogenerated_note_prefix = self.comment_prefix + " " + UTC_ADVERT
        self.test_autogenerated_note = self.autogenerated_note_prefix + script_name
        self.test_autogenerated_note += get_autogennote_suffix(parser, self.args)
        self.test_unused_note = (
            self.comment_prefix + self.comment_prefix + " " + UNUSED_NOTE
        )

    def ro_iterlines(self):
        for line_num, input_line in enumerate(self.input_lines):
            args, argv = check_for_command(
                input_line, self.parser, self.args, self.argv, self.argparse_callback
            )
            yield InputLineInfo(input_line, line_num, args, argv)

    def iterlines(self, output_lines):
        output_lines.append(self.test_autogenerated_note)
        for line_info in self.ro_iterlines():
            input_line = line_info.line
            # Discard any previous script advertising.
            if input_line.startswith(self.autogenerated_note_prefix):
                continue
            self.args = line_info.args
            self.argv = line_info.argv
            if not self.args.enabled:
                output_lines.append(input_line)
                continue
            yield line_info

    def get_checks_for_unused_prefixes(
        self, run_list, used_prefixes: List[str]
    ) -> List[str]:
        run_list = [element for element in run_list if element[0] is not None]
        unused_prefixes = set(
            [prefix for sublist in run_list for prefix in sublist[0]]
        ).difference(set(used_prefixes))

        ret = []
        if not unused_prefixes:
            return ret
        ret.append(self.test_unused_note)
        for unused in sorted(unused_prefixes):
            ret.append(
                "{comment} {prefix}: {match_everything}".format(
                    comment=self.comment_prefix,
                    prefix=unused,
                    match_everything=r"""{{.*}}""",
                )
            )
        return ret


def itertests(
    test_patterns, parser, script_name, comment_prefix=None, argparse_callback=None
):
    for pattern in test_patterns:
        # On Windows we must expand the patterns ourselves.
        tests_list = glob.glob(pattern)
        if not tests_list:
            warn("Test file pattern '%s' was not found. Ignoring it." % (pattern,))
            continue
        for test in tests_list:
            with open(test) as f:
                input_lines = [l.rstrip() for l in f]
            first_line = input_lines[0] if input_lines else ""
            if UTC_AVOID in first_line:
                warn("Skipping test that must not be autogenerated: " + test)
                continue
            is_regenerate = UTC_ADVERT in first_line

            # If we're generating a new test, set the default version to the latest.
            argv = sys.argv[:]
            if not is_regenerate:
                argv.insert(1, "--version=" + str(DEFAULT_VERSION))

            args = parse_args(parser, argv[1:])
            if argparse_callback is not None:
                argparse_callback(args)
            if is_regenerate:
                if script_name not in first_line and not args.force_update:
                    warn(
                        "Skipping test which wasn't autogenerated by " + script_name,
                        test,
                    )
                    continue
                args, argv = check_for_command(
                    first_line, parser, args, argv, argparse_callback
                )
            elif args.update_only:
                assert UTC_ADVERT not in first_line
                warn("Skipping test which isn't autogenerated: " + test)
                continue
            final_input_lines = []
            for l in input_lines:
                if UNUSED_NOTE in l:
                    break
                final_input_lines.append(l)
            yield TestInfo(
                test,
                parser,
                script_name,
                final_input_lines,
                args,
                argv,
                comment_prefix,
                argparse_callback,
            )


def should_add_line_to_output(
    input_line,
    prefix_set,
    *,
    skip_global_checks=False,
    skip_same_checks=False,
    comment_marker=";",
):
    # Skip any blank comment lines in the IR.
    if not skip_global_checks and input_line.strip() == comment_marker:
        return False
    # Skip a special double comment line we use as a separator.
    if input_line.strip() == comment_marker + SEPARATOR:
        return False
    # Skip any blank lines in the IR.
    # if input_line.strip() == '':
    #  return False
    # And skip any CHECK lines. We're building our own.
    m = CHECK_RE.match(input_line)
    if m and m.group(1) in prefix_set:
        if skip_same_checks and CHECK_SAME_RE.match(input_line):
            # The previous CHECK line was removed, so don't leave this dangling
            return False
        if skip_global_checks:
            # Skip checks only if they are of global value definitions
            global_ir_value_re = re.compile(r"(\[\[|@)", flags=(re.M))
            is_global = global_ir_value_re.search(input_line)
            return not is_global
        return False

    return True


def collect_original_check_lines(ti: TestInfo, prefix_set: set):
    """
    Collect pre-existing check lines into a dictionary `result` which is
    returned.

    result[func_name][prefix] is filled with a list of right-hand-sides of check
    lines.
    """
    result = collections.defaultdict(lambda: {})

    current_prefix = None
    current_function = None
    for input_line_info in ti.ro_iterlines():
        input_line = input_line_info.line
        if input_line.lstrip().startswith(";"):
            m = CHECK_RE.match(input_line)
            if m is not None:
                prefix = m.group(1)
                check_kind = m.group(2)
                line = input_line[m.end() :].strip()

                if prefix != current_prefix:
                    current_function = None
                    current_prefix = None

                if check_kind not in ["LABEL", "SAME"]:
                    if current_function is not None:
                        current_function.append(line)
                    continue

                if check_kind == "SAME":
                    continue

                if check_kind == "LABEL":
                    m = IR_FUNCTION_RE.match(line)
                    if m is not None:
                        func_name = m.group(1)
                        if (
                            ti.args.function is not None
                            and func_name != ti.args.function
                        ):
                            # When filtering on a specific function, skip all others.
                            continue

                        current_prefix = prefix
                        current_function = result[func_name][prefix] = []
                        continue

        current_function = None

    return result


# Perform lit-like substitutions
def getSubstitutions(sourcepath):
    sourcedir = os.path.dirname(sourcepath)
    return [
        ("%s", sourcepath),
        ("%S", sourcedir),
        ("%p", sourcedir),
        ("%{pathsep}", os.pathsep),
    ]


def applySubstitutions(s, substitutions):
    for a, b in substitutions:
        s = s.replace(a, b)
    return s


# Invoke the tool that is being tested.
def invoke_tool(exe, cmd_args, ir, preprocess_cmd=None, verbose=False):
    with open(ir) as ir_file:
        substitutions = getSubstitutions(ir)

        # TODO Remove the str form which is used by update_test_checks.py and
        # update_llc_test_checks.py
        # The safer list form is used by update_cc_test_checks.py
        if preprocess_cmd:
            # Allow pre-processing the IR file (e.g. using sed):
            assert isinstance(
                preprocess_cmd, str
            )  # TODO: use a list instead of using shell
            preprocess_cmd = applySubstitutions(preprocess_cmd, substitutions).strip()
            if verbose:
                print(
                    "Pre-processing input file: ",
                    ir,
                    " with command '",
                    preprocess_cmd,
                    "'",
                    sep="",
                    file=sys.stderr,
                )
            # Python 2.7 doesn't have subprocess.DEVNULL:
            with open(os.devnull, "w") as devnull:
                pp = subprocess.Popen(
                    preprocess_cmd, shell=True, stdin=devnull, stdout=subprocess.PIPE
                )
                ir_file = pp.stdout

        if isinstance(cmd_args, list):
            args = [applySubstitutions(a, substitutions) for a in cmd_args]
            stdout = subprocess.check_output([exe] + args, stdin=ir_file)
        else:
            stdout = subprocess.check_output(
                exe + " " + applySubstitutions(cmd_args, substitutions),
                shell=True,
                stdin=ir_file,
            )
        if sys.version_info[0] > 2:
            # FYI, if you crashed here with a decode error, your run line probably
            # results in bitcode or other binary format being written to the pipe.
            # For an opt test, you probably want to add -S or -disable-output.
            stdout = stdout.decode()
    # Fix line endings to unix CR style.
    return stdout.replace("\r\n", "\n")


##### LLVM IR parser
RUN_LINE_RE = re.compile(r"^\s*(?://|[;#])\s*RUN:\s*(.*)$")
CHECK_PREFIX_RE = re.compile(r"--?check-prefix(?:es)?[= ](\S+)")
PREFIX_RE = re.compile("^[a-zA-Z0-9_-]+$")
CHECK_RE = re.compile(
    r"^\s*(?://|[;#])\s*([^:]+?)(?:-(NEXT|NOT|DAG|LABEL|SAME|EMPTY))?:"
)
CHECK_SAME_RE = re.compile(r"^\s*(?://|[;#])\s*([^:]+?)(?:-SAME)?:")

UTC_ARGS_KEY = "UTC_ARGS:"
UTC_ARGS_CMD = re.compile(r".*" + UTC_ARGS_KEY + r"\s*(?P<cmd>.*)\s*$")
UTC_ADVERT = "NOTE: Assertions have been autogenerated by "
UTC_AVOID = "NOTE: Do not autogenerate"
UNUSED_NOTE = "NOTE: These prefixes are unused and the list is autogenerated. Do not add tests below this line:"

OPT_FUNCTION_RE = re.compile(
    r"^(\s*;\s*Function\sAttrs:\s(?P<attrs>[\w\s():,]+?))?\s*define\s+(?P<funcdef_attrs_and_ret>[^@]*)@(?P<func>[\w.$-]+?)\s*"
    r"(?P<args_and_sig>\((\)|(.*?[\w.-]+?)\))[^{]*\{)\n(?P<body>.*?)^\}$",
    flags=(re.M | re.S),
)

ANALYZE_FUNCTION_RE = re.compile(
    r"^\s*\'(?P<analysis>[\w\s-]+?)\'\s+for\s+function\s+\'(?P<func>[\w.$-]+?)\':"
    r"\s*\n(?P<body>.*)$",
    flags=(re.X | re.S),
)

LOOP_PASS_DEBUG_RE = re.compile(
    r"^\s*\'(?P<func>[\w.$-]+?)\'[^\n]*" r"\s*\n(?P<body>.*)$", flags=(re.X | re.S)
)

IR_FUNCTION_RE = re.compile(r'^\s*define\s+(?:internal\s+)?[^@]*@"?([\w.$-]+)"?\s*\(')
TRIPLE_IR_RE = re.compile(r'^\s*target\s+triple\s*=\s*"([^"]+)"$')
TRIPLE_ARG_RE = re.compile(r"-mtriple[= ]([^ ]+)")
MARCH_ARG_RE = re.compile(r"-march[= ]([^ ]+)")
DEBUG_ONLY_ARG_RE = re.compile(r"-debug-only[= ]([^ ]+)")

SCRUB_LEADING_WHITESPACE_RE = re.compile(r"^(\s+)")
SCRUB_WHITESPACE_RE = re.compile(r"(?!^(|  \w))[ \t]+", flags=re.M)
SCRUB_PRESERVE_LEADING_WHITESPACE_RE = re.compile(r"((?!^)[ \t]*(\S))[ \t]+")
SCRUB_TRAILING_WHITESPACE_RE = re.compile(r"[ \t]+$", flags=re.M)
SCRUB_TRAILING_WHITESPACE_TEST_RE = SCRUB_TRAILING_WHITESPACE_RE
SCRUB_TRAILING_WHITESPACE_AND_ATTRIBUTES_RE = re.compile(
    r"([ \t]|(#[0-9]+))+$", flags=re.M
)
SCRUB_KILL_COMMENT_RE = re.compile(r"^ *#+ +kill:.*\n")
SCRUB_LOOP_COMMENT_RE = re.compile(
    r"# =>This Inner Loop Header:.*|# in Loop:.*", flags=re.M
)
SCRUB_TAILING_COMMENT_TOKEN_RE = re.compile(r"(?<=\S)+[ \t]*#$", flags=re.M)

SEPARATOR = "."


def error(msg, test_file=None):
    if test_file:
        msg = "{}: {}".format(msg, test_file)
    print("ERROR: {}".format(msg), file=sys.stderr)


def warn(msg, test_file=None):
    if test_file:
        msg = "{}: {}".format(msg, test_file)
    print("WARNING: {}".format(msg), file=sys.stderr)


def debug(*args, **kwargs):
    # Python2 does not allow def debug(*args, file=sys.stderr, **kwargs):
    if "file" not in kwargs:
        kwargs["file"] = sys.stderr
    if _verbose:
        print(*args, **kwargs)


def find_run_lines(test, lines):
    debug("Scanning for RUN lines in test file:", test)
    raw_lines = [m.group(1) for m in [RUN_LINE_RE.match(l) for l in lines] if m]
    run_lines = [raw_lines[0]] if len(raw_lines) > 0 else []
    for l in raw_lines[1:]:
        if run_lines[-1].endswith("\\"):
            run_lines[-1] = run_lines[-1].rstrip("\\") + " " + l
        else:
            run_lines.append(l)
    debug("Found {} RUN lines in {}:".format(len(run_lines), test))
    for l in run_lines:
        debug("  RUN: {}".format(l))
    return run_lines


def get_triple_from_march(march):
    triples = {
        "amdgcn": "amdgcn",
        "r600": "r600",
        "mips": "mips",
        "sparc": "sparc",
        "hexagon": "hexagon",
        "ve": "ve",
    }
    for prefix, triple in triples.items():
        if march.startswith(prefix):
            return triple
    print("Cannot find a triple. Assume 'x86'", file=sys.stderr)
    return "x86"


def apply_filters(line, filters):
    has_filter = False
    for f in filters:
        if not f.is_filter_out:
            has_filter = True
        if f.search(line):
            return False if f.is_filter_out else True
    # If we only used filter-out, keep the line, otherwise discard it since no
    # filter matched.
    return False if has_filter else True


def do_filter(body, filters):
    return (
        body
        if not filters
        else "\n".join(
            filter(lambda line: apply_filters(line, filters), body.splitlines())
        )
    )


def scrub_body(body):
    # Scrub runs of whitespace out of the assembly, but leave the leading
    # whitespace in place.
    body = SCRUB_PRESERVE_LEADING_WHITESPACE_RE.sub(lambda m: m.group(2) + " ", body)

    # Expand the tabs used for indentation.
    body = str.expandtabs(body, 2)
    # Strip trailing whitespace.
    body = SCRUB_TRAILING_WHITESPACE_TEST_RE.sub(r"", body)
    return body


def do_scrub(body, scrubber, scrubber_args, extra):
    if scrubber_args:
        local_args = copy.deepcopy(scrubber_args)
        local_args[0].extra_scrub = extra
        return scrubber(body, *local_args)
    return scrubber(body, *scrubber_args)


# Build up a dictionary of all the function bodies.
class function_body(object):
    def __init__(
        self,
        string,
        extra,
        funcdef_attrs_and_ret,
        args_and_sig,
        attrs,
        func_name_separator,
        ginfo,
    ):
        self.scrub = string
        self.extrascrub = extra
        self.funcdef_attrs_and_ret = funcdef_attrs_and_ret
        self.args_and_sig = args_and_sig
        self.attrs = attrs
        self.func_name_separator = func_name_separator
        self._ginfo = ginfo

    def is_same_except_arg_names(
        self, extrascrub, funcdef_attrs_and_ret, args_and_sig, attrs
    ):
        arg_names = set()

        def drop_arg_names(match):
            nameless_value = self._ginfo.get_nameless_value_from_match(match)
            if nameless_value.check_key == "%":
                arg_names.add(self._ginfo.get_name_from_match(match))
                substitute = ""
            else:
                substitute = match.group(2)
            return match.group(1) + substitute + match.group(match.lastindex)

        def repl_arg_names(match):
            nameless_value = self._ginfo.get_nameless_value_from_match(match)
            if (
                nameless_value.check_key == "%"
                and self._ginfo.get_name_from_match(match) in arg_names
            ):
                return match.group(1) + match.group(match.lastindex)
            return match.group(1) + match.group(2) + match.group(match.lastindex)

        if self.funcdef_attrs_and_ret != funcdef_attrs_and_ret:
            return False
        if self.attrs != attrs:
            return False

        regexp = self._ginfo.get_regexp()
        ans0 = regexp.sub(drop_arg_names, self.args_and_sig)
        ans1 = regexp.sub(drop_arg_names, args_and_sig)
        if ans0 != ans1:
            return False
        if self._ginfo.is_asm():
            # Check without replacements, the replacements are not applied to the
            # body for backend checks.
            return self.extrascrub == extrascrub

        es0 = regexp.sub(repl_arg_names, self.extrascrub)
        es1 = regexp.sub(repl_arg_names, extrascrub)
        es0 = SCRUB_IR_COMMENT_RE.sub(r"", es0)
        es1 = SCRUB_IR_COMMENT_RE.sub(r"", es1)
        return es0 == es1

    def __str__(self):
        return self.scrub


class FunctionTestBuilder:
    def __init__(self, run_list, flags, scrubber_args, path, ginfo):
        self._verbose = flags.verbose
        self._record_args = flags.function_signature
        self._check_attributes = flags.check_attributes
        # Strip double-quotes if input was read by UTC_ARGS
        self._filters = (
            list(
                map(
                    lambda f: Filter(
                        re.compile(f.pattern().strip('"'), f.flags()), f.is_filter_out
                    ),
                    flags.filters,
                )
            )
            if flags.filters
            else []
        )
        self._scrubber_args = scrubber_args
        self._path = path
        self._ginfo = ginfo
        # Strip double-quotes if input was read by UTC_ARGS
        self._replace_value_regex = list(
            map(lambda x: x.strip('"'), flags.replace_value_regex)
        )
        self._func_dict = {}
        self._func_order = {}
        self._global_var_dict = {}
        self._processed_prefixes = set()
        for tuple in run_list:
            for prefix in tuple[0]:
                self._func_dict.update({prefix: dict()})
                self._func_order.update({prefix: []})
                self._global_var_dict.update({prefix: dict()})

    def finish_and_get_func_dict(self):
        for prefix in self.get_failed_prefixes():
            warn(
                "Prefix %s had conflicting output from different RUN lines for all functions in test %s"
                % (
                    prefix,
                    self._path,
                )
            )
        return self._func_dict

    def func_order(self):
        return self._func_order

    def global_var_dict(self):
        return self._global_var_dict

    def is_filtered(self):
        return bool(self._filters)

    def process_run_line(self, function_re, scrubber, raw_tool_output, prefixes):
        build_global_values_dictionary(
            self._global_var_dict, raw_tool_output, prefixes, self._ginfo
        )
        for m in function_re.finditer(raw_tool_output):
            if not m:
                continue
            func = m.group("func")
            body = m.group("body")
            # func_name_separator is the string that is placed right after function name at the
            # beginning of assembly function definition. In most assemblies, that is just a
            # colon: `foo:`. But, for example, in nvptx it is a brace: `foo(`. If is_backend is
            # False, just assume that separator is an empty string.
            if self._ginfo.is_asm():
                # Use ':' as default separator.
                func_name_separator = (
                    m.group("func_name_separator")
                    if "func_name_separator" in m.groupdict()
                    else ":"
                )
            else:
                func_name_separator = ""
            attrs = m.group("attrs") if self._check_attributes else ""
            funcdef_attrs_and_ret = (
                m.group("funcdef_attrs_and_ret") if self._record_args else ""
            )
            # Determine if we print arguments, the opening brace, or nothing after the
            # function name
            if self._record_args and "args_and_sig" in m.groupdict():
                args_and_sig = scrub_body(m.group("args_and_sig").strip())
            elif "args_and_sig" in m.groupdict():
                args_and_sig = "("
            else:
                args_and_sig = ""
            filtered_body = do_filter(body, self._filters)
            scrubbed_body = do_scrub(
                filtered_body, scrubber, self._scrubber_args, extra=False
            )
            scrubbed_extra = do_scrub(
                filtered_body, scrubber, self._scrubber_args, extra=True
            )
            if "analysis" in m.groupdict():
                analysis = m.group("analysis")
                if analysis not in SUPPORTED_ANALYSES:
                    warn("Unsupported analysis mode: %r!" % (analysis,))
            if func.startswith("stress"):
                # We only use the last line of the function body for stress tests.
                scrubbed_body = "\n".join(scrubbed_body.splitlines()[-1:])
            if self._verbose:
                print("Processing function: " + func, file=sys.stderr)
                for l in scrubbed_body.splitlines():
                    print("  " + l, file=sys.stderr)
            for prefix in prefixes:
                # Replace function names matching the regex.
                for regex in self._replace_value_regex:
                    # Pattern that matches capture groups in the regex in leftmost order.
                    group_regex = re.compile(r"\(.*?\)")
                    # Replace function name with regex.
                    match = re.match(regex, func)
                    if match:
                        func_repl = regex
                        # Replace any capture groups with their matched strings.
                        for g in match.groups():
                            func_repl = group_regex.sub(
                                re.escape(g), func_repl, count=1
                            )
                        func = re.sub(func_repl, "{{" + func_repl + "}}", func)

                    # Replace all calls to regex matching functions.
                    matches = re.finditer(regex, scrubbed_body)
                    for match in matches:
                        func_repl = regex
                        # Replace any capture groups with their matched strings.
                        for g in match.groups():
                            func_repl = group_regex.sub(
                                re.escape(g), func_repl, count=1
                            )
                        # Substitute function call names that match the regex with the same
                        # capture groups set.
                        scrubbed_body = re.sub(
                            func_repl, "{{" + func_repl + "}}", scrubbed_body
                        )

                if func in self._func_dict[prefix]:
                    if self._func_dict[prefix][func] is not None and (
                        str(self._func_dict[prefix][func]) != scrubbed_body
                        or self._func_dict[prefix][func].args_and_sig != args_and_sig
                        or self._func_dict[prefix][func].attrs != attrs
                        or self._func_dict[prefix][func].funcdef_attrs_and_ret
                        != funcdef_attrs_and_ret
                    ):
                        if self._func_dict[prefix][func].is_same_except_arg_names(
                            scrubbed_extra,
                            funcdef_attrs_and_ret,
                            args_and_sig,
                            attrs,
                        ):
                            self._func_dict[prefix][func].scrub = scrubbed_extra
                            self._func_dict[prefix][func].args_and_sig = args_and_sig
                        else:
                            # This means a previous RUN line produced a body for this function
                            # that is different from the one produced by this current RUN line,
                            # so the body can't be common across RUN lines. We use None to
                            # indicate that.
                            self._func_dict[prefix][func] = None
                else:
                    if prefix not in self._processed_prefixes:
                        self._func_dict[prefix][func] = function_body(
                            scrubbed_body,
                            scrubbed_extra,
                            funcdef_attrs_and_ret,
                            args_and_sig,
                            attrs,
                            func_name_separator,
                            self._ginfo,
                        )
                        self._func_order[prefix].append(func)
                    else:
                        # An earlier RUN line used this check prefixes but didn't produce
                        # a body for this function. This happens in Clang tests that use
                        # preprocesser directives to exclude individual functions from some
                        # RUN lines.
                        self._func_dict[prefix][func] = None

    def processed_prefixes(self, prefixes):
        """
        Mark a set of prefixes as having had at least one applicable RUN line fully
        processed. This is used to filter out function bodies that don't have
        outputs for all RUN lines.
        """
        self._processed_prefixes.update(prefixes)

    def get_failed_prefixes(self):
        # This returns the list of those prefixes that failed to match any function,
        # because there were conflicting bodies produced by different RUN lines, in
        # all instances of the prefix.
        for prefix in self._func_dict:
            if self._func_dict[prefix] and (
                not [
                    fct
                    for fct in self._func_dict[prefix]
                    if self._func_dict[prefix][fct] is not None
                ]
            ):
                yield prefix


##### Generator of LLVM IR CHECK lines

SCRUB_IR_COMMENT_RE = re.compile(r"\s*;.*")

# TODO: We should also derive check lines for global, debug, loop declarations, etc..


class NamelessValue:
    """
    A NamelessValue object represents a type of value in the IR whose "name" we
    generalize in the generated check lines; where the "name" could be an actual
    name (as in e.g. `@some_global` or `%x`) or just a number (as in e.g. `%12`
    or `!4`).
    """

    def __init__(
        self,
        check_prefix,
        check_key,
        ir_prefix,
        ir_regexp,
        global_ir_rhs_regexp,
        *,
        is_before_functions=False,
        is_number=False,
        replace_number_with_counter=False,
        match_literally=False,
        interlaced_with_previous=False,
        ir_suffix=r"",
    ):
        self.check_prefix = check_prefix
        self.check_key = check_key
        self.ir_prefix = ir_prefix
        self.ir_regexp = ir_regexp
        self.ir_suffix = ir_suffix
        self.global_ir_rhs_regexp = global_ir_rhs_regexp
        self.is_before_functions = is_before_functions
        self.is_number = is_number
        # Some variable numbers (e.g. MCINST1234) will change based on unrelated
        # modifications to LLVM, replace those with an incrementing counter.
        self.replace_number_with_counter = replace_number_with_counter
        self.match_literally = match_literally
        self.interlaced_with_previous = interlaced_with_previous
        self.variable_mapping = {}

    # Return true if this kind of IR value is defined "locally" to functions,
    # which we assume is only the case precisely for LLVM IR local values.
    def is_local_def_ir_value(self):
        return self.check_key == "%"

    # Return the IR regexp we use for this kind or IR value, e.g., [\w.-]+? for locals
    def get_ir_regex(self):
        # for backwards compatibility we check locals with '.*'
        if self.is_local_def_ir_value():
            return ".*"
        return self.ir_regexp

    # Create a FileCheck variable name based on an IR name.
    def get_value_name(self, var: str, check_prefix: str):
        var = var.replace("!", "")
        if self.replace_number_with_counter:
            assert var
            replacement = self.variable_mapping.get(var, None)
            if replacement is None:
                # Replace variable with an incrementing counter
                replacement = str(len(self.variable_mapping) + 1)
                self.variable_mapping[var] = replacement
            var = replacement
        # This is a nameless value, prepend check_prefix.
        if var.isdigit():
            var = check_prefix + var
        else:
            # This is a named value that clashes with the check_prefix, prepend with
            # _prefix_filecheck_ir_name, if it has been defined.
            if (
                may_clash_with_default_check_prefix_name(check_prefix, var)
                and _prefix_filecheck_ir_name
            ):
                var = _prefix_filecheck_ir_name + var
        var = var.replace(".", "_")
        var = var.replace("-", "_")
        return var.upper()

    def get_affixes_from_match(self, match):
        prefix = re.match(self.ir_prefix, match.group(2)).group(0)
        suffix = re.search(self.ir_suffix + "$", match.group(2)).group(0)
        return prefix, suffix


class GeneralizerInfo:
    """
    A GeneralizerInfo object holds information about how check lines should be generalized
    (e.g., variable names replaced by FileCheck meta variables) as well as per-test-file
    state (e.g. information about IR global variables).
    """

    MODE_IR = 0
    MODE_ASM = 1
    MODE_ANALYZE = 2

    def __init__(
        self,
        version,
        mode,
        nameless_values: List[NamelessValue],
        regexp_prefix,
        regexp_suffix,
    ):
        self._version = version
        self._mode = mode
        self._nameless_values = nameless_values

        self._regexp_prefix = regexp_prefix
        self._regexp_suffix = regexp_suffix

        self._regexp, _ = self._build_regexp(False, False)
        (
            self._unstable_globals_regexp,
            self._unstable_globals_values,
        ) = self._build_regexp(True, True)

    def _build_regexp(self, globals_only, unstable_only):
        matches = []
        values = []
        for nameless_value in self._nameless_values:
            is_global = nameless_value.global_ir_rhs_regexp is not None
            if globals_only and not is_global:
                continue
            if unstable_only and nameless_value.match_literally:
                continue

            match = f"(?:{nameless_value.ir_prefix}({nameless_value.ir_regexp}){nameless_value.ir_suffix})"
            if self.is_ir() and not globals_only and is_global:
                match = "^" + match
            matches.append(match)
            values.append(nameless_value)

        regexp_string = r"|".join(matches)

        return (
            re.compile(
                self._regexp_prefix + r"(" + regexp_string + r")" + self._regexp_suffix
            ),
            values,
        )

    def get_version(self):
        return self._version

    def is_ir(self):
        return self._mode == GeneralizerInfo.MODE_IR

    def is_asm(self):
        return self._mode == GeneralizerInfo.MODE_ASM

    def is_analyze(self):
        return self._mode == GeneralizerInfo.MODE_ANALYZE

    def get_nameless_values(self):
        return self._nameless_values

    def get_regexp(self):
        return self._regexp

    def get_unstable_globals_regexp(self):
        return self._unstable_globals_regexp

    # The entire match is group 0, the prefix has one group (=1), the entire
    # IR_VALUE_REGEXP_STRING is one group (=2), and then the nameless values start.
    FIRST_NAMELESS_GROUP_IN_MATCH = 3

    def get_match_info(self, match):
        """
        Returns (name, nameless_value) for the given match object
        """
        if match.re == self._regexp:
            values = self._nameless_values
        else:
            match.re == self._unstable_globals_regexp
            values = self._unstable_globals_values
        for i in range(len(values)):
            g = match.group(i + GeneralizerInfo.FIRST_NAMELESS_GROUP_IN_MATCH)
            if g is not None:
                return g, values[i]
        error("Unable to identify the kind of IR value from the match!")
        return None, None

    # See get_idx_from_match
    def get_name_from_match(self, match):
        return self.get_match_info(match)[0]

    def get_nameless_value_from_match(self, match) -> NamelessValue:
        return self.get_match_info(match)[1]


def make_ir_generalizer(version):
    values = []

    if version >= 5:
        values += [
            NamelessValue(r"BB", "%", r"label %", r"[\w$.-]+?", None),
            NamelessValue(r"BB", "%", r"^", r"[\w$.-]+?", None, ir_suffix=r":"),
        ]

    values += [
        #            check_prefix   check_key  ir_prefix           ir_regexp                global_ir_rhs_regexp
        NamelessValue(r"TMP", "%", r"%", r"[\w$.-]+?", None),
        NamelessValue(r"ATTR", "#", r"#", r"[0-9]+", None),
        NamelessValue(r"ATTR", "#", r"attributes #", r"[0-9]+", r"{[^}]*}"),
        NamelessValue(r"GLOB", "@", r"@", r"[0-9]+", None),
        NamelessValue(r"GLOB", "@", r"@", r"[0-9]+", r".+", is_before_functions=True),
        NamelessValue(
            r"GLOBNAMED",
            "@",
            r"@",
            r"[a-zA-Z0-9_$\"\\.-]*[a-zA-Z_$\"\\.-][a-zA-Z0-9_$\"\\.-]*",
            r".+",
            is_before_functions=True,
            match_literally=True,
            interlaced_with_previous=True,
        ),
        NamelessValue(r"DBG", "!", r"!dbg ", r"![0-9]+", None),
        NamelessValue(r"DIASSIGNID", "!", r"!DIAssignID ", r"![0-9]+", None),
        NamelessValue(r"PROF", "!", r"!prof ", r"![0-9]+", None),
        NamelessValue(r"TBAA", "!", r"!tbaa ", r"![0-9]+", None),
        NamelessValue(r"TBAA_STRUCT", "!", r"!tbaa.struct ", r"![0-9]+", None),
        NamelessValue(r"RNG", "!", r"!range ", r"![0-9]+", None),
        NamelessValue(r"LOOP", "!", r"!llvm.loop ", r"![0-9]+", None),
        NamelessValue(r"META", "!", r"", r"![0-9]+", r"(?:distinct |)!.*"),
        NamelessValue(r"ACC_GRP", "!", r"!llvm.access.group ", r"![0-9]+", None),
        NamelessValue(r"META", "!", r"![a-z.]+ ", r"![0-9]+", None),
        NamelessValue(r"META", "!", r"[, (]", r"![0-9]+", None),
    ]

    prefix = r"(\s*)"
    suffix = r"([,\s\(\)\}]|\Z)"

    # values = [
    #     nameless_value
    #     for nameless_value in IR_NAMELESS_VALUES
    #     if not (globals_only and nameless_value.global_ir_rhs_regexp is None) and
    #        not (unstable_ids_only and nameless_value.match_literally)
    # ]

    return GeneralizerInfo(version, GeneralizerInfo.MODE_IR, values, prefix, suffix)


def make_asm_generalizer(version):
    values = [
        NamelessValue(
            r"MCINST",
            "Inst#",
            "<MCInst #",
            r"\d+",
            r".+",
            is_number=True,
            replace_number_with_counter=True,
        ),
        NamelessValue(
            r"MCREG",
            "Reg:",
            "<MCOperand Reg:",
            r"\d+",
            r".+",
            is_number=True,
            replace_number_with_counter=True,
        ),
    ]

    prefix = r"((?:#|//)\s*)"
    suffix = r"([>\s]|\Z)"

    return GeneralizerInfo(version, GeneralizerInfo.MODE_ASM, values, prefix, suffix)


def make_analyze_generalizer(version):
    values = [
        NamelessValue(
            r"GRP",
            "#",
            r"",
            r"0x[0-9a-f]+",
            None,
            replace_number_with_counter=True,
        ),
    ]

    prefix = r"(\s*)"
    suffix = r"(\)?:)"

    return GeneralizerInfo(
        version, GeneralizerInfo.MODE_ANALYZE, values, prefix, suffix
    )


# Return true if var clashes with the scripted FileCheck check_prefix.
def may_clash_with_default_check_prefix_name(check_prefix, var):
    return check_prefix and re.match(
        r"^" + check_prefix + r"[0-9]+?$", var, re.IGNORECASE
    )


def find_diff_matching(lhs: List[str], rhs: List[str]) -> List[tuple]:
    """
    Find a large ordered matching between strings in lhs and rhs.

    Think of this as finding the *unchanged* lines in a diff, where the entries
    of lhs and rhs are lines of the files being diffed.

    Returns a list of matched (lhs_idx, rhs_idx) pairs.
    """

    if not lhs or not rhs:
        return []

    # Collect matches in reverse order.
    matches = []

    # First, collect a set of candidate matching edges. We limit this to a
    # constant multiple of the input size to avoid quadratic runtime.
    patterns = collections.defaultdict(lambda: ([], []))

    for idx in range(len(lhs)):
        patterns[lhs[idx]][0].append(idx)
    for idx in range(len(rhs)):
        patterns[rhs[idx]][1].append(idx)

    multiple_patterns = []

    candidates = []
    for pattern in patterns.values():
        if not pattern[0] or not pattern[1]:
            continue

        if len(pattern[0]) == len(pattern[1]) == 1:
            candidates.append((pattern[0][0], pattern[1][0]))
        else:
            multiple_patterns.append(pattern)

    multiple_patterns.sort(key=lambda pattern: len(pattern[0]) * len(pattern[1]))

    for pattern in multiple_patterns:
        if len(candidates) + len(pattern[0]) * len(pattern[1]) > 2 * (
            len(lhs) + len(rhs)
        ):
            break
        for lhs_idx in pattern[0]:
            for rhs_idx in pattern[1]:
                candidates.append((lhs_idx, rhs_idx))

    if not candidates:
        # The LHS and RHS either share nothing in common, or lines are just too
        # identical. In that case, let's give up and not match anything.
        return []

    # Compute a maximal crossing-free matching via an algorithm that is
    # inspired by a mixture of dynamic programming and line-sweeping in
    # discrete geometry.
    #
    # I would be surprised if this algorithm didn't exist somewhere in the
    # literature, but I found it without consciously recalling any
    # references, so you'll have to make do with the explanation below.
    # Sorry.
    #
    # The underlying graph is bipartite:
    #  - nodes on the LHS represent lines in the original check
    #  - nodes on the RHS represent lines in the new (updated) check
    #
    # Nodes are implicitly sorted by the corresponding line number.
    # Edges (unique_matches) are sorted by the line number on the LHS.
    #
    # Here's the geometric intuition for the algorithm.
    #
    #  * Plot the edges as points in the plane, with the original line
    #    number on the X axis and the updated line number on the Y axis.
    #  * The goal is to find a longest "chain" of points where each point
    #    is strictly above and to the right of the previous point.
    #  * The algorithm proceeds by sweeping a vertical line from left to
    #    right.
    #  * The algorithm maintains a table where `table[N]` answers the
    #    question "What is currently the 'best' way to build a chain of N+1
    #    points to the left of the vertical line". Here, 'best' means
    #    that the last point of the chain is a as low as possible (minimal
    #    Y coordinate).
    #   * `table[N]` is `(y, point_idx)` where `point_idx` is the index of
    #     the last point in the chain and `y` is its Y coordinate
    #   * A key invariant is that the Y values in the table are
    #     monotonically increasing
    #  * Thanks to these properties, the table can be used to answer the
    #    question "What is the longest chain that can be built to the left
    #    of the vertical line using only points below a certain Y value",
    #    using a binary search over the table.
    #  * The algorithm also builds a backlink structure in which every point
    #    links back to the previous point on a best (longest) chain ending
    #    at that point
    #
    # The core loop of the algorithm sweeps the line and updates the table
    # and backlink structure for every point that we cross during the sweep.
    # Therefore, the algorithm is trivially O(M log M) in the number of
    # points.
    candidates.sort(key=lambda candidate: (candidate[0], -candidate[1]))

    backlinks = []
    table_rhs_idx = []
    table_candidate_idx = []
    for _, rhs_idx in candidates:
        candidate_idx = len(backlinks)
        ti = bisect.bisect_left(table_rhs_idx, rhs_idx)

        # Update the table to record a best chain ending in the current point.
        # There always is one, and if any of the previously visited points had
        # a higher Y coordinate, then there is always a previously recorded best
        # chain that can be improved upon by using the current point.
        #
        # There is only one case where there is some ambiguity. If the
        # pre-existing entry table[ti] has the same Y coordinate / rhs_idx as
        # the current point (this can only happen if the same line appeared
        # multiple times on the LHS), then we could choose to keep the
        # previously recorded best chain instead. That would bias the algorithm
        # differently but should have no systematic impact on the quality of the
        # result.
        if ti < len(table_rhs_idx):
            table_rhs_idx[ti] = rhs_idx
            table_candidate_idx[ti] = candidate_idx
        else:
            table_rhs_idx.append(rhs_idx)
            table_candidate_idx.append(candidate_idx)
        if ti > 0:
            backlinks.append(table_candidate_idx[ti - 1])
        else:
            backlinks.append(None)

    # Commit to names in the matching by walking the backlinks. Recursively
    # attempt to fill in more matches in-betweem.
    match_idx = table_candidate_idx[-1]
    while match_idx is not None:
        current = candidates[match_idx]
        matches.append(current)
        match_idx = backlinks[match_idx]

    matches.reverse()
    return matches


VARIABLE_TAG = "[[@@]]"
METAVAR_RE = re.compile(r"\[\[([A-Z0-9_]+)(?::[^]]+)?\]\]")
NUMERIC_SUFFIX_RE = re.compile(r"[0-9]*$")


class TestVar:
    def __init__(self, nameless_value: NamelessValue, prefix: str, suffix: str):
        self._nameless_value = nameless_value

        self._prefix = prefix
        self._suffix = suffix

    def seen(self, nameless_value: NamelessValue, prefix: str, suffix: str):
        if prefix != self._prefix:
            self._prefix = ""
        if suffix != self._suffix:
            self._suffix = ""

    def get_variable_name(self, text):
        return self._nameless_value.get_value_name(
            text, self._nameless_value.check_prefix
        )

    def get_def(self, name, prefix, suffix):
        if self._nameless_value.is_number:
            return f"{prefix}[[#{name}:]]{suffix}"
        if self._prefix:
            assert self._prefix == prefix
            prefix = ""
        if self._suffix:
            assert self._suffix == suffix
            suffix = ""
        return f"{prefix}[[{name}:{self._prefix}{self._nameless_value.get_ir_regex()}{self._suffix}]]{suffix}"

    def get_use(self, name, prefix, suffix):
        if self._nameless_value.is_number:
            return f"{prefix}[[#{name}]]{suffix}"
        if self._prefix:
            assert self._prefix == prefix
            prefix = ""
        if self._suffix:
            assert self._suffix == suffix
            suffix = ""
        return f"{prefix}[[{name}]]{suffix}"


class CheckValueInfo:
    def __init__(
        self,
        key,
        text,
        name: str,
        prefix: str,
        suffix: str,
    ):
        # Key for the value, e.g. '%'
        self.key = key

        # Text to be matched by the FileCheck variable (without any prefix or suffix)
        self.text = text

        # Name of the FileCheck variable
        self.name = name

        # Prefix and suffix that were captured by the NamelessValue regular expression
        self.prefix = prefix
        self.suffix = suffix


# Represent a check line in a way that allows us to compare check lines while
# ignoring some or all of the FileCheck variable names.
class CheckLineInfo:
    def __init__(self, line, values):
        # Line with all FileCheck variable name occurrences replaced by VARIABLE_TAG
        self.line: str = line

        # Information on each FileCheck variable name occurrences in the line
        self.values: List[CheckValueInfo] = values

    def __repr__(self):
        return f"CheckLineInfo(line={self.line}, self.values={self.values})"


def remap_metavar_names(
    old_line_infos: List[CheckLineInfo],
    new_line_infos: List[CheckLineInfo],
    committed_names: Set[str],
) -> Mapping[str, str]:
    """
    Map all FileCheck variable names that appear in new_line_infos to new
    FileCheck variable names in an attempt to reduce the diff from old_line_infos
    to new_line_infos.

    This is done by:
    * Matching old check lines and new check lines using a diffing algorithm
      applied after replacing names with wildcards.
    * Committing to variable names such that the matched lines become equal
      (without wildcards) if possible
    * This is done recursively to handle cases where many lines are equal
      after wildcard replacement
    """
    # Initialize uncommitted identity mappings
    new_mapping = {}
    for line in new_line_infos:
        for value in line.values:
            new_mapping[value.name] = value.name

    # Recursively commit to the identity mapping or find a better one
    def recurse(old_begin, old_end, new_begin, new_end):
        if old_begin == old_end or new_begin == new_end:
            return

        # Find a matching of lines where uncommitted names are replaced
        # with a placeholder.
        def diffify_line(line, mapper):
            values = []
            for value in line.values:
                mapped = mapper(value.name)
                values.append(mapped if mapped in committed_names else "?")
            return line.line.strip() + " @@@ " + " @ ".join(values)

        lhs_lines = [
            diffify_line(line, lambda x: x)
            for line in old_line_infos[old_begin:old_end]
        ]
        rhs_lines = [
            diffify_line(line, lambda x: new_mapping[x])
            for line in new_line_infos[new_begin:new_end]
        ]

        candidate_matches = find_diff_matching(lhs_lines, rhs_lines)

        # Apply commits greedily on a match-by-match basis
        matches = [(-1, -1)]
        committed_anything = False
        for lhs_idx, rhs_idx in candidate_matches:
            lhs_line = old_line_infos[lhs_idx]
            rhs_line = new_line_infos[rhs_idx]

            local_commits = {}

            for lhs_value, rhs_value in zip(lhs_line.values, rhs_line.values):
                if new_mapping[rhs_value.name] in committed_names:
                    # The new value has already been committed. If it was mapped
                    # to the same name as the original value, we can consider
                    # committing other values from this line. Otherwise, we
                    # should ignore this line.
                    if new_mapping[rhs_value.name] == lhs_value.name:
                        continue
                    else:
                        break

                if rhs_value.name in local_commits:
                    # Same, but for a possible commit happening on the same line
                    if local_commits[rhs_value.name] == lhs_value.name:
                        continue
                    else:
                        break

                if lhs_value.name in committed_names:
                    # We can't map this value because the name we would map it to has already been
                    # committed for something else. Give up on this line.
                    break

                local_commits[rhs_value.name] = lhs_value.name
            else:
                # No reason not to add any commitments for this line
                for rhs_var, lhs_var in local_commits.items():
                    new_mapping[rhs_var] = lhs_var
                    committed_names.add(lhs_var)
                    committed_anything = True

                    if (
                        lhs_var != rhs_var
                        and lhs_var in new_mapping
                        and new_mapping[lhs_var] == lhs_var
                    ):
                        new_mapping[lhs_var] = "conflict_" + lhs_var

                matches.append((lhs_idx, rhs_idx))

        matches.append((old_end, new_end))

        # Recursively handle sequences between matches
        if committed_anything:
            for (lhs_prev, rhs_prev), (lhs_next, rhs_next) in zip(matches, matches[1:]):
                recurse(lhs_prev + 1, lhs_next, rhs_prev + 1, rhs_next)

    recurse(0, len(old_line_infos), 0, len(new_line_infos))

    # Commit to remaining names and resolve conflicts
    for new_name, mapped_name in new_mapping.items():
        if mapped_name in committed_names:
            continue
        if not mapped_name.startswith("conflict_"):
            assert mapped_name == new_name
            committed_names.add(mapped_name)

    for new_name, mapped_name in new_mapping.items():
        if mapped_name in committed_names:
            continue
        assert mapped_name.startswith("conflict_")

        m = NUMERIC_SUFFIX_RE.search(new_name)
        base_name = new_name[: m.start()]
        suffix = int(new_name[m.start() :]) if m.start() != m.end() else 1
        while True:
            candidate = f"{base_name}{suffix}"
            if candidate not in committed_names:
                new_mapping[new_name] = candidate
                committed_names.add(candidate)
                break
            suffix += 1

    return new_mapping


def generalize_check_lines(
    lines,
    ginfo: GeneralizerInfo,
    vars_seen,
    global_vars_seen,
    preserve_names=False,
    original_check_lines=None,
    *,
    unstable_globals_only=False,
):
    if unstable_globals_only:
        regexp = ginfo.get_unstable_globals_regexp()
    else:
        regexp = ginfo.get_regexp()

    multiple_braces_re = re.compile(r"({{+)|(}}+)")

    def escape_braces(match_obj):
        return "{{" + re.escape(match_obj.group(0)) + "}}"

    if ginfo.is_ir():
        for i, line in enumerate(lines):
            # An IR variable named '%.' matches the FileCheck regex string.
            line = line.replace("%.", "%dot")
            for regex in _global_hex_value_regex:
                if re.match("^@" + regex + " = ", line):
                    line = re.sub(
                        r"\bi([0-9]+) ([0-9]+)",
                        lambda m: "i"
                        + m.group(1)
                        + " [[#"
                        + hex(int(m.group(2)))
                        + "]]",
                        line,
                    )
                    break
            # Ignore any comments, since the check lines will too.
            scrubbed_line = SCRUB_IR_COMMENT_RE.sub(r"", line)
            lines[i] = scrubbed_line

    if not preserve_names:
        committed_names = set(
            test_var.get_variable_name(name)
            for (name, _), test_var in vars_seen.items()
        )
        defs = set()

        # Collect information about new check lines, and generalize global reference
        new_line_infos = []
        for line in lines:
            filtered_line = ""
            values = []
            while True:
                m = regexp.search(line)
                if m is None:
                    filtered_line += line
                    break

                name = ginfo.get_name_from_match(m)
                nameless_value = ginfo.get_nameless_value_from_match(m)
                prefix, suffix = nameless_value.get_affixes_from_match(m)
                if may_clash_with_default_check_prefix_name(
                    nameless_value.check_prefix, name
                ):
                    warn(
                        "Change IR value name '%s' or use --prefix-filecheck-ir-name to prevent possible conflict"
                        " with scripted FileCheck name." % (name,)
                    )

                # Record the variable as seen and (for locals) accumulate
                # prefixes/suffixes
                is_local_def = nameless_value.is_local_def_ir_value()
                if is_local_def:
                    vars_dict = vars_seen
                else:
                    vars_dict = global_vars_seen

                key = (name, nameless_value.check_key)

                if is_local_def:
                    test_prefix = prefix
                    test_suffix = suffix
                else:
                    test_prefix = ""
                    test_suffix = ""

                if key in vars_dict:
                    vars_dict[key].seen(nameless_value, test_prefix, test_suffix)
                else:
                    vars_dict[key] = TestVar(nameless_value, test_prefix, test_suffix)
                    defs.add(key)

                var = vars_dict[key].get_variable_name(name)

                # Replace with a [[@@]] tag, but be sure to keep the spaces and commas.
                filtered_line += (
                    line[: m.start()] + m.group(1) + VARIABLE_TAG + m.group(m.lastindex)
                )
                line = line[m.end() :]

                values.append(
                    CheckValueInfo(
                        key=nameless_value.check_key,
                        text=name,
                        name=var,
                        prefix=prefix,
                        suffix=suffix,
                    )
                )

            new_line_infos.append(CheckLineInfo(filtered_line, values))

        committed_names.update(
            test_var.get_variable_name(name)
            for (name, _), test_var in global_vars_seen.items()
        )

        # Collect information about original check lines, if any.
        orig_line_infos = []
        for line in original_check_lines or []:
            filtered_line = ""
            values = []
            while True:
                m = METAVAR_RE.search(line)
                if m is None:
                    filtered_line += line
                    break

                # Replace with a [[@@]] tag, but be sure to keep the spaces and commas.
                filtered_line += line[: m.start()] + VARIABLE_TAG
                line = line[m.end() :]
                values.append(
                    CheckValueInfo(
                        key=None,
                        text=None,
                        name=m.group(1),
                        prefix="",
                        suffix="",
                    )
                )
            orig_line_infos.append(CheckLineInfo(filtered_line, values))

        # Compute the variable name mapping
        mapping = remap_metavar_names(orig_line_infos, new_line_infos, committed_names)

        # Apply the variable name mapping
        for i, line_info in enumerate(new_line_infos):
            line_template = line_info.line
            line = ""

            for value in line_info.values:
                idx = line_template.find(VARIABLE_TAG)
                line += line_template[:idx]
                line_template = line_template[idx + len(VARIABLE_TAG) :]

                key = (value.text, value.key)
                if value.key == "%":
                    vars_dict = vars_seen
                else:
                    vars_dict = global_vars_seen

                if key in defs:
                    line += vars_dict[key].get_def(
                        mapping[value.name], value.prefix, value.suffix
                    )
                    defs.remove(key)
                else:
                    line += vars_dict[key].get_use(
                        mapping[value.name], value.prefix, value.suffix
                    )

            line += line_template

            lines[i] = line

    if ginfo.is_analyze():
        for i, _ in enumerate(lines):
            # Escape multiple {{ or }} as {{}} denotes a FileCheck regex.
            scrubbed_line = multiple_braces_re.sub(escape_braces, lines[i])
            lines[i] = scrubbed_line

    return lines


def add_checks(
    output_lines,
    comment_marker,
    prefix_list,
    func_dict,
    func_name,
    check_label_format,
    ginfo,
    global_vars_seen_dict,
    is_filtered,
    preserve_names=False,
    original_check_lines: Mapping[str, List[str]] = {},
):
    # prefix_exclusions are prefixes we cannot use to print the function because it doesn't exist in run lines that use these prefixes as well.
    prefix_exclusions = set()
    printed_prefixes = []
    for p in prefix_list:
        checkprefixes = p[0]
        # If not all checkprefixes of this run line produced the function we cannot check for it as it does not
        # exist for this run line. A subset of the check prefixes might know about the function but only because
        # other run lines created it.
        if any(
            map(
                lambda checkprefix: func_name not in func_dict[checkprefix],
                checkprefixes,
            )
        ):
            prefix_exclusions |= set(checkprefixes)
            continue

    # prefix_exclusions is constructed, we can now emit the output
    for p in prefix_list:
        global_vars_seen = {}
        checkprefixes = p[0]
        for checkprefix in checkprefixes:
            if checkprefix in global_vars_seen_dict:
                global_vars_seen.update(global_vars_seen_dict[checkprefix])
            else:
                global_vars_seen_dict[checkprefix] = {}
            if checkprefix in printed_prefixes:
                break

            # Check if the prefix is excluded.
            if checkprefix in prefix_exclusions:
                continue

            # If we do not have output for this prefix we skip it.
            if not func_dict[checkprefix][func_name]:
                continue

            # Add some space between different check prefixes, but not after the last
            # check line (before the test code).
            if ginfo.is_asm():
                if len(printed_prefixes) != 0:
                    output_lines.append(comment_marker)

            if checkprefix not in global_vars_seen_dict:
                global_vars_seen_dict[checkprefix] = {}

            global_vars_seen_before = [key for key in global_vars_seen.keys()]

            vars_seen = {}
            printed_prefixes.append(checkprefix)
            attrs = str(func_dict[checkprefix][func_name].attrs)
            attrs = "" if attrs == "None" else attrs
            if ginfo.get_version() > 1:
                funcdef_attrs_and_ret = func_dict[checkprefix][
                    func_name
                ].funcdef_attrs_and_ret
            else:
                funcdef_attrs_and_ret = ""

            if attrs:
                output_lines.append(
                    "%s %s: Function Attrs: %s" % (comment_marker, checkprefix, attrs)
                )
            args_and_sig = str(func_dict[checkprefix][func_name].args_and_sig)
            if args_and_sig:
                args_and_sig = generalize_check_lines(
                    [args_and_sig],
                    ginfo,
                    vars_seen,
                    global_vars_seen,
                    preserve_names,
                    original_check_lines=[],
                )[0]
            func_name_separator = func_dict[checkprefix][func_name].func_name_separator
            if "[[" in args_and_sig:
                # Captures in label lines are not supported, thus split into a -LABEL
                # and a separate -SAME line that contains the arguments with captures.
                args_and_sig_prefix = ""
                if ginfo.get_version() >= 3 and args_and_sig.startswith("("):
                    # Ensure the "(" separating function name and arguments is in the
                    # label line. This is required in case of function names that are
                    # prefixes of each other. Otherwise, the label line for "foo" might
                    # incorrectly match on "foo.specialized".
                    args_and_sig_prefix = args_and_sig[0]
                    args_and_sig = args_and_sig[1:]

                # Removing args_and_sig from the label match line requires
                # func_name_separator to be empty. Otherwise, the match will not work.
                assert func_name_separator == ""
                output_lines.append(
                    check_label_format
                    % (
                        checkprefix,
                        funcdef_attrs_and_ret,
                        func_name,
                        args_and_sig_prefix,
                        func_name_separator,
                    )
                )
                output_lines.append(
                    "%s %s-SAME: %s" % (comment_marker, checkprefix, args_and_sig)
                )
            else:
                output_lines.append(
                    check_label_format
                    % (
                        checkprefix,
                        funcdef_attrs_and_ret,
                        func_name,
                        args_and_sig,
                        func_name_separator,
                    )
                )
            func_body = str(func_dict[checkprefix][func_name]).splitlines()
            if not func_body:
                # We have filtered everything.
                continue

            # For ASM output, just emit the check lines.
            if ginfo.is_asm():
                body_start = 1
                if is_filtered:
                    # For filtered output we don't add "-NEXT" so don't add extra spaces
                    # before the first line.
                    body_start = 0
                else:
                    output_lines.append(
                        "%s %s:       %s" % (comment_marker, checkprefix, func_body[0])
                    )
                func_lines = generalize_check_lines(
                    func_body[body_start:], ginfo, vars_seen, global_vars_seen
                )
                for func_line in func_lines:
                    if func_line.strip() == "":
                        output_lines.append(
                            "%s %s-EMPTY:" % (comment_marker, checkprefix)
                        )
                    else:
                        check_suffix = "-NEXT" if not is_filtered else ""
                        output_lines.append(
                            "%s %s%s:  %s"
                            % (comment_marker, checkprefix, check_suffix, func_line)
                        )
                # Remember new global variables we have not seen before
                for key in global_vars_seen:
                    if key not in global_vars_seen_before:
                        global_vars_seen_dict[checkprefix][key] = global_vars_seen[key]
                break
            # For analyze output, generalize the output, and emit CHECK-EMPTY lines as well.
            elif ginfo.is_analyze():
                func_body = generalize_check_lines(
                    func_body, ginfo, vars_seen, global_vars_seen
                )
                for func_line in func_body:
                    if func_line.strip() == "":
                        output_lines.append(
                            "{} {}-EMPTY:".format(comment_marker, checkprefix)
                        )
                    else:
                        check_suffix = "-NEXT" if not is_filtered else ""
                        output_lines.append(
                            "{} {}{}:  {}".format(
                                comment_marker, checkprefix, check_suffix, func_line
                            )
                        )

                # Add space between different check prefixes and also before the first
                # line of code in the test function.
                output_lines.append(comment_marker)

                # Remember new global variables we have not seen before
                for key in global_vars_seen:
                    if key not in global_vars_seen_before:
                        global_vars_seen_dict[checkprefix][key] = global_vars_seen[key]
                break
            # For IR output, change all defs to FileCheck variables, so we're immune
            # to variable naming fashions.
            else:
                func_body = generalize_check_lines(
                    func_body,
                    ginfo,
                    vars_seen,
                    global_vars_seen,
                    preserve_names,
                    original_check_lines=original_check_lines.get(checkprefix),
                )

                # This could be selectively enabled with an optional invocation argument.
                # Disabled for now: better to check everything. Be safe rather than sorry.

                # Handle the first line of the function body as a special case because
                # it's often just noise (a useless asm comment or entry label).
                # if func_body[0].startswith("#") or func_body[0].startswith("entry:"):
                #  is_blank_line = True
                # else:
                #  output_lines.append('%s %s:       %s' % (comment_marker, checkprefix, func_body[0]))
                #  is_blank_line = False

                is_blank_line = False

                for func_line in func_body:
                    if func_line.strip() == "":
                        is_blank_line = True
                        continue
                    # Do not waste time checking IR comments.
                    func_line = SCRUB_IR_COMMENT_RE.sub(r"", func_line)

                    # Skip blank lines instead of checking them.
                    if is_blank_line:
                        output_lines.append(
                            "{} {}:       {}".format(
                                comment_marker, checkprefix, func_line
                            )
                        )
                    else:
                        check_suffix = "-NEXT" if not is_filtered else ""
                        output_lines.append(
                            "{} {}{}:  {}".format(
                                comment_marker, checkprefix, check_suffix, func_line
                            )
                        )
                    is_blank_line = False

                # Add space between different check prefixes and also before the first
                # line of code in the test function.
                output_lines.append(comment_marker)

                # Remember new global variables we have not seen before
                for key in global_vars_seen:
                    if key not in global_vars_seen_before:
                        global_vars_seen_dict[checkprefix][key] = global_vars_seen[key]
                break
    return printed_prefixes


def add_ir_checks(
    output_lines,
    comment_marker,
    prefix_list,
    func_dict,
    func_name,
    preserve_names,
    function_sig,
    ginfo: GeneralizerInfo,
    global_vars_seen_dict,
    is_filtered,
    original_check_lines={},
):
    assert ginfo.is_ir()
    # Label format is based on IR string.
    if function_sig and ginfo.get_version() > 1:
        function_def_regex = "define %s"
    elif function_sig:
        function_def_regex = "define {{[^@]+}}%s"
    else:
        function_def_regex = "%s"
    check_label_format = "{} %s-LABEL: {}@%s%s%s".format(
        comment_marker, function_def_regex
    )
    return add_checks(
        output_lines,
        comment_marker,
        prefix_list,
        func_dict,
        func_name,
        check_label_format,
        ginfo,
        global_vars_seen_dict,
        is_filtered,
        preserve_names,
        original_check_lines=original_check_lines,
    )


def add_analyze_checks(
    output_lines,
    comment_marker,
    prefix_list,
    func_dict,
    func_name,
    ginfo: GeneralizerInfo,
    is_filtered,
):
    assert ginfo.is_analyze()
    check_label_format = "{} %s-LABEL: '%s%s%s%s'".format(comment_marker)
    global_vars_seen_dict = {}
    return add_checks(
        output_lines,
        comment_marker,
        prefix_list,
        func_dict,
        func_name,
        check_label_format,
        ginfo,
        global_vars_seen_dict,
        is_filtered,
    )


def build_global_values_dictionary(glob_val_dict, raw_tool_output, prefixes, ginfo):
    for nameless_value in ginfo.get_nameless_values():
        if nameless_value.global_ir_rhs_regexp is None:
            continue

        lhs_re_str = nameless_value.ir_prefix + nameless_value.ir_regexp
        rhs_re_str = nameless_value.global_ir_rhs_regexp

        global_ir_value_re_str = r"^" + lhs_re_str + r"\s=\s" + rhs_re_str + r"$"
        global_ir_value_re = re.compile(global_ir_value_re_str, flags=(re.M))
        lines = []
        for m in global_ir_value_re.finditer(raw_tool_output):
            # Attach the substring's start index so that CHECK lines
            # can be sorted properly even if they are matched by different nameless values.
            # This is relevant for GLOB and GLOBNAMED since they may appear interlaced.
            lines.append((m.start(), m.group(0)))

        for prefix in prefixes:
            if glob_val_dict[prefix] is None:
                continue
            if nameless_value.check_prefix in glob_val_dict[prefix]:
                if lines == glob_val_dict[prefix][nameless_value.check_prefix]:
                    continue
                if prefix == prefixes[-1]:
                    warn("Found conflicting asm under the same prefix: %r!" % (prefix,))
                else:
                    glob_val_dict[prefix][nameless_value.check_prefix] = None
                    continue
            glob_val_dict[prefix][nameless_value.check_prefix] = lines


def filter_globals_according_to_preference(
    global_val_lines_w_index, global_vars_seen, nameless_value, global_check_setting
):
    if global_check_setting == "none":
        return []
    if global_check_setting == "all":
        return global_val_lines_w_index
    assert global_check_setting == "smart"

    if nameless_value.check_key == "#":
        # attribute sets are usually better checked by --check-attributes
        return []

    def extract(line, nv):
        p = (
            "^"
            + nv.ir_prefix
            + "("
            + nv.ir_regexp
            + ") = ("
            + nv.global_ir_rhs_regexp
            + ")"
        )
        match = re.match(p, line)
        return (match.group(1), re.findall(nv.ir_regexp, match.group(2)))

    transitively_visible = set()
    contains_refs_to = {}

    def add(var):
        nonlocal transitively_visible
        nonlocal contains_refs_to
        if var in transitively_visible:
            return
        transitively_visible.add(var)
        if not var in contains_refs_to:
            return
        for x in contains_refs_to[var]:
            add(x)

    for i, line in global_val_lines_w_index:
        (var, refs) = extract(line, nameless_value)
        contains_refs_to[var] = refs
    for var, check_key in global_vars_seen:
        if check_key != nameless_value.check_key:
            continue
        add(var)
    return [
        (i, line)
        for i, line in global_val_lines_w_index
        if extract(line, nameless_value)[0] in transitively_visible
    ]


METADATA_FILTERS = [
    (
        r"(?<=\")(.+ )?(\w+ version )[\d.]+(?:[^\" ]*)(?: \([^)]+\))?",
        r"{{.*}}\2{{.*}}",
    ),  # preface with glob also, to capture optional CLANG_VENDOR
    (r'(!DIFile\(filename: ".+", directory: )".+"', r"\1{{.*}}"),
]
METADATA_FILTERS_RE = [(re.compile(f), r) for (f, r) in METADATA_FILTERS]


def filter_unstable_metadata(line):
    for f, replacement in METADATA_FILTERS_RE:
        line = f.sub(replacement, line)
    return line


def flush_current_checks(output_lines, new_lines_w_index, comment_marker):
    if not new_lines_w_index:
        return
    output_lines.append(comment_marker + SEPARATOR)
    new_lines_w_index.sort()
    for _, line in new_lines_w_index:
        output_lines.append(line)
    new_lines_w_index.clear()


def add_global_checks(
    glob_val_dict,
    comment_marker,
    prefix_list,
    output_lines,
    ginfo: GeneralizerInfo,
    global_vars_seen_dict,
    preserve_names,
    is_before_functions,
    global_check_setting,
):
    printed_prefixes = set()
    output_lines_loc = {}  # Allows GLOB and GLOBNAMED to be sorted correctly
    for nameless_value in ginfo.get_nameless_values():
        if nameless_value.global_ir_rhs_regexp is None:
            continue
        if nameless_value.is_before_functions != is_before_functions:
            continue
        for p in prefix_list:
            global_vars_seen = {}
            checkprefixes = p[0]
            if checkprefixes is None:
                continue
            for checkprefix in checkprefixes:
                if checkprefix in global_vars_seen_dict:
                    global_vars_seen.update(global_vars_seen_dict[checkprefix])
                else:
                    global_vars_seen_dict[checkprefix] = {}
                if (checkprefix, nameless_value.check_prefix) in printed_prefixes:
                    break
                if not glob_val_dict[checkprefix]:
                    continue
                if nameless_value.check_prefix not in glob_val_dict[checkprefix]:
                    continue
                if not glob_val_dict[checkprefix][nameless_value.check_prefix]:
                    continue

                check_lines = []
                global_vars_seen_before = [key for key in global_vars_seen.keys()]
                lines_w_index = glob_val_dict[checkprefix][nameless_value.check_prefix]
                lines_w_index = filter_globals_according_to_preference(
                    lines_w_index,
                    global_vars_seen_before,
                    nameless_value,
                    global_check_setting,
                )
                for i, line in lines_w_index:
                    if _global_value_regex:
                        matched = False
                        for regex in _global_value_regex:
                            if re.match("^@" + regex + " = ", line) or re.match(
                                "^!" + regex + " = ", line
                            ):
                                matched = True
                                break
                        if not matched:
                            continue
                    [new_line] = generalize_check_lines(
                        [line],
                        ginfo,
                        {},
                        global_vars_seen,
                        preserve_names,
                        unstable_globals_only=True,
                    )
                    new_line = filter_unstable_metadata(new_line)
                    check_line = "%s %s: %s" % (comment_marker, checkprefix, new_line)
                    check_lines.append((i, check_line))
                if not check_lines:
                    continue

                if not checkprefix in output_lines_loc:
                    output_lines_loc[checkprefix] = []
                if not nameless_value.interlaced_with_previous:
                    flush_current_checks(
                        output_lines, output_lines_loc[checkprefix], comment_marker
                    )
                for check_line in check_lines:
                    output_lines_loc[checkprefix].append(check_line)

                printed_prefixes.add((checkprefix, nameless_value.check_prefix))

                # Remembe new global variables we have not seen before
                for key in global_vars_seen:
                    if key not in global_vars_seen_before:
                        global_vars_seen_dict[checkprefix][key] = global_vars_seen[key]
                break

    if printed_prefixes:
        for p in prefix_list:
            if p[0] is None:
                continue
            for checkprefix in p[0]:
                if checkprefix not in output_lines_loc:
                    continue
                flush_current_checks(
                    output_lines, output_lines_loc[checkprefix], comment_marker
                )
                break
        output_lines.append(comment_marker + SEPARATOR)
    return printed_prefixes


def check_prefix(prefix):
    if not PREFIX_RE.match(prefix):
        hint = ""
        if "," in prefix:
            hint = " Did you mean '--check-prefixes=" + prefix + "'?"
        warn(
            (
                "Supplied prefix '%s' is invalid. Prefix must contain only alphanumeric characters, hyphens and underscores."
                + hint
            )
            % (prefix)
        )


def get_check_prefixes(filecheck_cmd):
    check_prefixes = [
        item
        for m in CHECK_PREFIX_RE.finditer(filecheck_cmd)
        for item in m.group(1).split(",")
    ]
    if not check_prefixes:
        check_prefixes = ["CHECK"]
    return check_prefixes


def verify_filecheck_prefixes(fc_cmd):
    fc_cmd_parts = fc_cmd.split()
    for part in fc_cmd_parts:
        if "check-prefix=" in part:
            prefix = part.split("=", 1)[1]
            check_prefix(prefix)
        elif "check-prefixes=" in part:
            prefixes = part.split("=", 1)[1].split(",")
            for prefix in prefixes:
                check_prefix(prefix)
                if prefixes.count(prefix) > 1:
                    warn(
                        "Supplied prefix '%s' is not unique in the prefix list."
                        % (prefix,)
                    )


def get_autogennote_suffix(parser, args):
    autogenerated_note_args = ""
    for action in parser._actions:
        if not hasattr(args, action.dest):
            continue  # Ignore options such as --help that aren't included in args
        # Ignore parameters such as paths to the binary or the list of tests
        if action.dest in (
            "tests",
            "update_only",
            "tool_binary",
            "opt_binary",
            "llc_binary",
            "clang",
            "opt",
            "llvm_bin",
            "verbose",
            "force_update",
            "reset_variable_names",
        ):
            continue
        value = getattr(args, action.dest)
        if action.dest == "check_globals":
            default_value = "none" if args.version < 4 else "smart"
            if value == default_value:
                continue
            autogenerated_note_args += action.option_strings[0] + " "
            if args.version < 4 and value == "all":
                continue
            autogenerated_note_args += "%s " % value
            continue
        if action.const is not None:  # action stores a constant (usually True/False)
            # Skip actions with different constant values (this happens with boolean
            # --foo/--no-foo options)
            if value != action.const:
                continue
        if parser.get_default(action.dest) == value:
            continue  # Don't add default values
        if action.dest == "function_signature" and args.version >= 2:
            continue  # Enabled by default in version 2
        if action.dest == "filters":
            # Create a separate option for each filter element.  The value is a list
            # of Filter objects.
            for elem in value:
                opt_name = "filter-out" if elem.is_filter_out else "filter"
                opt_value = elem.pattern()
                new_arg = '--%s "%s" ' % (opt_name, opt_value.strip('"'))
                if new_arg not in autogenerated_note_args:
                    autogenerated_note_args += new_arg
        else:
            autogenerated_note_args += action.option_strings[0] + " "
            if action.const is None:  # action takes a parameter
                if action.nargs == "+":
                    value = " ".join(map(lambda v: '"' + v.strip('"') + '"', value))
                autogenerated_note_args += "%s " % value
    if autogenerated_note_args:
        autogenerated_note_args = " %s %s" % (
            UTC_ARGS_KEY,
            autogenerated_note_args[:-1],
        )
    return autogenerated_note_args


def check_for_command(line, parser, args, argv, argparse_callback):
    cmd_m = UTC_ARGS_CMD.match(line)
    if cmd_m:
        for option in shlex.split(cmd_m.group("cmd").strip()):
            if option:
                argv.append(option)
        args = parse_args(parser, filter(lambda arg: arg not in args.tests, argv))
        if argparse_callback is not None:
            argparse_callback(args)
    return args, argv


def find_arg_in_test(test_info, get_arg_to_check, arg_string, is_global):
    result = get_arg_to_check(test_info.args)
    if not result and is_global:
        # See if this has been specified via UTC_ARGS.  This is a "global" option
        # that affects the entire generation of test checks.  If it exists anywhere
        # in the test, apply it to everything.
        saw_line = False
        for line_info in test_info.ro_iterlines():
            line = line_info.line
            if not line.startswith(";") and line.strip() != "":
                saw_line = True
            result = get_arg_to_check(line_info.args)
            if result:
                if warn and saw_line:
                    # We saw the option after already reading some test input lines.
                    # Warn about it.
                    print(
                        "WARNING: Found {} in line following test start: ".format(
                            arg_string
                        )
                        + line,
                        file=sys.stderr,
                    )
                    print(
                        "WARNING: Consider moving {} to top of file".format(arg_string),
                        file=sys.stderr,
                    )
                break
    return result


def dump_input_lines(output_lines, test_info, prefix_set, comment_string):
    for input_line_info in test_info.iterlines(output_lines):
        line = input_line_info.line
        args = input_line_info.args
        if line.strip() == comment_string:
            continue
        if line.strip() == comment_string + SEPARATOR:
            continue
        if line.lstrip().startswith(comment_string):
            m = CHECK_RE.match(line)
            if m and m.group(1) in prefix_set:
                continue
        output_lines.append(line.rstrip("\n"))


def add_checks_at_end(
    output_lines, prefix_list, func_order, comment_string, check_generator
):
    added = set()
    generated_prefixes = set()
    for prefix in prefix_list:
        prefixes = prefix[0]
        tool_args = prefix[1]
        for prefix in prefixes:
            for func in func_order[prefix]:
                # The func order can contain the same functions multiple times.
                # If we see one again we are done.
                if (func, prefix) in added:
                    continue
                if added:
                    output_lines.append(comment_string)

                # The add_*_checks routines expect a run list whose items are
                # tuples that have a list of prefixes as their first element and
                # tool command args string as their second element.  They output
                # checks for each prefix in the list of prefixes.  By doing so, it
                # implicitly assumes that for each function every run line will
                # generate something for that function.  That is not the case for
                # generated functions as some run lines might not generate them
                # (e.g. -fopenmp vs. no -fopenmp).
                #
                # Therefore, pass just the prefix we're interested in.  This has
                # the effect of generating all of the checks for functions of a
                # single prefix before moving on to the next prefix.  So checks
                # are ordered by prefix instead of by function as in "normal"
                # mode.
                for generated_prefix in check_generator(
                    output_lines, [([prefix], tool_args)], func
                ):
                    added.add((func, generated_prefix))
                    generated_prefixes.add(generated_prefix)
    return generated_prefixes
