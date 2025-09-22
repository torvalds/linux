#!/usr/bin/env python
"""A ladder graph creation program.

This is a python program that creates c source code that will generate
CFGs that are ladder graphs.  Ladder graphs are generally the worst case
for a lot of dominance related algorithms (Dominance frontiers, etc),
and often generate N^2 or worse behavior.

One good use of this program is to test whether your linear time algorithm is
really behaving linearly.
"""

from __future__ import print_function

import argparse


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "rungs", type=int, help="Number of ladder rungs. Must be a multiple of 2"
    )
    args = parser.parse_args()
    if (args.rungs % 2) != 0:
        print("Rungs must be a multiple of 2")
        return
    print("int ladder(int *foo, int *bar, int x) {")
    rung1 = range(0, args.rungs, 2)
    rung2 = range(1, args.rungs, 2)
    for i in rung1:
        print("rung1%d:" % i)
        print("*foo = x++;")
        if i != rung1[-1]:
            print("if (*bar) goto rung1%d;" % (i + 2))
            print("else goto rung2%d;" % (i + 1))
        else:
            print("goto rung2%d;" % (i + 1))
    for i in rung2:
        print("rung2%d:" % i)
        print("*foo = x++;")
        if i != rung2[-1]:
            print("goto rung2%d;" % (i + 2))
        else:
            print("return *foo;")
    print("}")


if __name__ == "__main__":
    main()
