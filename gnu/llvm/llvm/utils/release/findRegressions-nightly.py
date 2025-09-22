#!/usr/bin/env python
from __future__ import print_function

import re, string, sys, os, time

DEBUG = 0
testDirName = "llvm-test"
test = ["compile", "llc", "jit", "cbe"]
exectime = [
    "llc-time",
    "jit-time",
    "cbe-time",
]
comptime = ["llc", "jit-comptime", "compile"]

(tp, exp) = ("compileTime_", "executeTime_")


def parse(file):
    f = open(file, "r")
    d = f.read()

    # Cleanup weird stuff
    d = re.sub(r",\d+:\d", "", d)

    r = re.findall(r"TEST-(PASS|FAIL|RESULT.*?):\s+(.*?)\s+(.*?)\r*\n", d)

    test = {}
    fname = ""
    for t in r:
        if DEBUG:
            print(t)
        if t[0] == "PASS" or t[0] == "FAIL":
            tmp = t[2].split(testDirName)

            if DEBUG:
                print(tmp)

            if len(tmp) == 2:
                fname = tmp[1].strip("\r\n")
            else:
                fname = tmp[0].strip("\r\n")

            if fname not in test:
                test[fname] = {}

            for k in test:
                test[fname][k] = "NA"
                test[fname][t[1]] = t[0]
                if DEBUG:
                    print(test[fname][t[1]])
        else:
            try:
                n = t[0].split("RESULT-")[1]

                if DEBUG:
                    print(n)

                if n == "llc" or n == "jit-comptime" or n == "compile":
                    test[fname][tp + n] = float(t[2].split(" ")[2])
                    if DEBUG:
                        print(test[fname][tp + n])

                elif n.endswith("-time"):
                    test[fname][exp + n] = float(t[2].strip("\r\n"))
                    if DEBUG:
                        print(test[fname][exp + n])

                else:
                    print("ERROR!")
                    sys.exit(1)

            except:
                continue

    return test


# Diff results and look for regressions.
def diffResults(d_old, d_new):

    for t in sorted(d_old.keys()):
        if DEBUG:
            print(t)

        if t in d_new:

            # Check if the test passed or failed.
            for x in test:
                if x in d_old[t]:
                    if x in d_new[t]:
                        if d_old[t][x] == "PASS":
                            if d_new[t][x] != "PASS":
                                print(t + " *** REGRESSION (" + x + ")\n")
                        else:
                            if d_new[t][x] == "PASS":
                                print(t + " * NEW PASS (" + x + ")\n")

                    else:
                        print(t + "*** REGRESSION (" + x + ")\n")

                # For execution time, if there is no result, its a fail.
                for x in exectime:
                    if tp + x in d_old[t]:
                        if tp + x not in d_new[t]:
                            print(t + " *** REGRESSION (" + tp + x + ")\n")

                    else:
                        if tp + x in d_new[t]:
                            print(t + " * NEW PASS (" + tp + x + ")\n")

                for x in comptime:
                    if exp + x in d_old[t]:
                        if exp + x not in d_new[t]:
                            print(t + " *** REGRESSION (" + exp + x + ")\n")

                    else:
                        if exp + x in d_new[t]:
                            print(t + " * NEW PASS (" + exp + x + ")\n")

        else:
            print(t + ": Removed from test-suite.\n")


# Main
if len(sys.argv) < 3:
    print("Usage:", sys.argv[0], "<old log> <new log>")
    sys.exit(-1)

d_old = parse(sys.argv[1])
d_new = parse(sys.argv[2])


diffResults(d_old, d_new)
