#!/usr/bin/env python
"""
Summarize the information in the given coverage files.

Emits the number of rules covered or the percentage of rules covered depending
on whether --num-rules has been used to specify the total number of rules.
"""
from __future__ import print_function

import argparse
import struct


class FileFormatError(Exception):
    pass


def backend_int_pair(s):
    backend, sep, value = s.partition("=")
    if sep is None:
        raise argparse.ArgumentTypeError("'=' missing, expected name=value")
    if not backend:
        raise argparse.ArgumentTypeError("Expected name=value")
    if not value:
        raise argparse.ArgumentTypeError("Expected name=value")
    return backend, int(value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", nargs="+")
    parser.add_argument(
        "--num-rules",
        type=backend_int_pair,
        action="append",
        metavar="BACKEND=NUM",
        help="Specify the number of rules for a backend",
    )
    args = parser.parse_args()

    covered_rules = {}

    for input_filename in args.input:
        with open(input_filename, "rb") as input_fh:
            data = input_fh.read()
            pos = 0
            while data:
                backend, _, data = data.partition("\0")
                pos += len(backend)
                pos += 1

                if len(backend) == 0:
                    raise FileFormatError()
                (backend,) = struct.unpack("%ds" % len(backend), backend)

                while data:
                    if len(data) < 8:
                        raise FileFormatError()
                    (rule_id,) = struct.unpack("Q", data[:8])
                    pos += 8
                    data = data[8:]
                    if rule_id == (2**64) - 1:
                        break
                    covered_rules[backend] = covered_rules.get(backend, {})
                    covered_rules[backend][rule_id] = (
                        covered_rules[backend].get(rule_id, 0) + 1
                    )

    num_rules = dict(args.num_rules)
    for backend, rules_for_backend in covered_rules.items():
        if backend in num_rules:
            print(
                "%s: %3.2f%% of rules covered"
                % (backend, float(len(rules_for_backend)) / num_rules[backend])
                * 100
            )
        else:
            print("%s: %d rules covered" % (backend, len(rules_for_backend)))


if __name__ == "__main__":
    main()
