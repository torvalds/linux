#!/usr/bin/env python3
r"""Emulates the bits of CMake's configure_file() function needed in LLVM.

The CMake build uses configure_file() for several things.  This emulates that
function for the GN build.  In the GN build, this runs at build time instead
of at generator time.

Takes a list of KEY=VALUE pairs (where VALUE can be empty).

The sequence `\` `n` in each VALUE is replaced by a newline character.

On each line, replaces '${KEY}' or '@KEY@' with VALUE.

Then, handles these special cases (note that FOO= sets the value of FOO to the
empty string, which is falsy, but FOO=0 sets it to '0' which is truthy):

1.) #cmakedefine01 FOO
    Checks if key FOO is set to a truthy value, and depending on that prints
    one of the following two lines:

        #define FOO 1
        #define FOO 0

2.) #cmakedefine FOO [...]
    Checks if key FOO is set to a truthy value, and depending on that prints
    one of the following two lines:

        #define FOO [...]
        /* #undef FOO */

Fails if any of the KEY=VALUE arguments aren't needed for processing the
input file, or if the input file references keys that weren't passed in.
"""

import argparse
import os
import re
import sys


def main():
    parser = argparse.ArgumentParser(
        epilog=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("input", help="input file")
    parser.add_argument("values", nargs="*", help="several KEY=VALUE pairs")
    parser.add_argument("-o", "--output", required=True, help="output file")
    args = parser.parse_args()

    values = {}
    for value in args.values:
        key, val = value.split("=", 1)
        if key in values:
            print('duplicate key "%s" in args' % key, file=sys.stderr)
            return 1
        values[key] = val.replace("\\n", "\n")
    unused_values = set(values.keys())

    # Matches e.g. '${FOO}' or '@FOO@' and captures FOO in group 1 or 2.
    var_re = re.compile(r"\$\{([^}]*)\}|@([^@]*)@")

    with open(args.input) as f:
        in_lines = f.readlines()
    out_lines = []
    for in_line in in_lines:

        def repl(m):
            key = m.group(1) or m.group(2)
            unused_values.discard(key)
            return values[key]

        in_line = var_re.sub(repl, in_line)
        if in_line.startswith("#cmakedefine01 "):
            _, var = in_line.split()
            if values[var] == "0":
                print('error: "%s=0" used with #cmakedefine01 %s' % (var, var))
                print("       '0' evaluates as truthy with #cmakedefine01")
                print('       use "%s=" instead' % var)
                return 1
            in_line = "#define %s %d\n" % (var, 1 if values[var] else 0)
            unused_values.discard(var)
        elif in_line.startswith("#cmakedefine "):
            _, var = in_line.split(None, 1)
            try:
                var, val = var.split(None, 1)
                in_line = "#define %s %s" % (var, val)  # val ends in \n.
            except:
                var = var.rstrip()
                in_line = "#define %s\n" % var
            if not values[var]:
                in_line = "/* #undef %s */\n" % var
            unused_values.discard(var)
        out_lines.append(in_line)

    if unused_values:
        print("unused values args:", file=sys.stderr)
        print("    " + "\n    ".join(unused_values), file=sys.stderr)
        return 1

    output = "".join(out_lines)

    leftovers = var_re.findall(output)
    if leftovers:
        print(
            "unprocessed values:\n",
            "\n".join([x[0] or x[1] for x in leftovers]),
            file=sys.stderr,
        )
        return 1

    def read(filename):
        with open(args.output) as f:
            return f.read()

    if not os.path.exists(args.output) or read(args.output) != output:
        with open(args.output, "w") as f:
            f.write(output)
        os.chmod(args.output, os.stat(args.input).st_mode & 0o777)


if __name__ == "__main__":
    sys.exit(main())
