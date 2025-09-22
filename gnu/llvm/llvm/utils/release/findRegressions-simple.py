#!/usr/bin/env python

from __future__ import print_function
import re, string, sys, os, time, math

DEBUG = 0

(tp, exp) = ("compile", "exec")


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
            tmp = t[2].split("llvm-test/")

            if DEBUG:
                print(tmp)

            if len(tmp) == 2:
                fname = tmp[1].strip("\r\n")
            else:
                fname = tmp[0].strip("\r\n")

            if fname not in test:
                test[fname] = {}

            test[fname][t[1] + " state"] = t[0]
            test[fname][t[1] + " time"] = float("nan")
        else:
            try:
                n = t[0].split("RESULT-")[1]

                if DEBUG:
                    print("n == ", n)

                if n == "compile-success":
                    test[fname]["compile time"] = float(
                        t[2].split("program")[1].strip("\r\n")
                    )

                elif n == "exec-success":
                    test[fname]["exec time"] = float(
                        t[2].split("program")[1].strip("\r\n")
                    )
                    if DEBUG:
                        print(test[fname][string.replace(n, "-success", "")])

                else:
                    # print "ERROR!"
                    sys.exit(1)

            except:
                continue

    return test


# Diff results and look for regressions.
def diffResults(d_old, d_new):
    regressions = {}
    passes = {}
    removed = ""

    for x in ["compile state", "compile time", "exec state", "exec time"]:
        regressions[x] = ""
        passes[x] = ""

    for t in sorted(d_old.keys()):
        if t in d_new:

            # Check if the test passed or failed.
            for x in ["compile state", "compile time", "exec state", "exec time"]:

                if x not in d_old[t] and x not in d_new[t]:
                    continue

                if x in d_old[t]:
                    if x in d_new[t]:

                        if d_old[t][x] == "PASS":
                            if d_new[t][x] != "PASS":
                                regressions[x] += t + "\n"
                        else:
                            if d_new[t][x] == "PASS":
                                passes[x] += t + "\n"

                    else:
                        regressions[x] += t + "\n"

                if x == "compile state" or x == "exec state":
                    continue

                # For execution time, if there is no result it's a fail.
                if x not in d_old[t] and x not in d_new[t]:
                    continue
                elif x not in d_new[t]:
                    regressions[x] += t + "\n"
                elif x not in d_old[t]:
                    passes[x] += t + "\n"

                if math.isnan(d_old[t][x]) and math.isnan(d_new[t][x]):
                    continue

                elif math.isnan(d_old[t][x]) and not math.isnan(d_new[t][x]):
                    passes[x] += t + "\n"

                elif not math.isnan(d_old[t][x]) and math.isnan(d_new[t][x]):
                    regressions[x] += t + ": NaN%\n"

                if (
                    d_new[t][x] > d_old[t][x]
                    and d_old[t][x] > 0.0
                    and (d_new[t][x] - d_old[t][x]) / d_old[t][x] > 0.05
                ):
                    regressions[x] += (
                        t
                        + ": "
                        + "{0:.1f}".format(
                            100 * (d_new[t][x] - d_old[t][x]) / d_old[t][x]
                        )
                        + "%\n"
                    )

        else:
            removed += t + "\n"

    if len(regressions["compile state"]) != 0:
        print("REGRESSION: Compilation Failed")
        print(regressions["compile state"])

    if len(regressions["exec state"]) != 0:
        print("REGRESSION: Execution Failed")
        print(regressions["exec state"])

    if len(regressions["compile time"]) != 0:
        print("REGRESSION: Compilation Time")
        print(regressions["compile time"])

    if len(regressions["exec time"]) != 0:
        print("REGRESSION: Execution Time")
        print(regressions["exec time"])

    if len(passes["compile state"]) != 0:
        print("NEW PASSES: Compilation")
        print(passes["compile state"])

    if len(passes["exec state"]) != 0:
        print("NEW PASSES: Execution")
        print(passes["exec state"])

    if len(removed) != 0:
        print("REMOVED TESTS")
        print(removed)


# Main
if len(sys.argv) < 3:
    print("Usage:", sys.argv[0], "<old log> <new log>")
    sys.exit(-1)

d_old = parse(sys.argv[1])
d_new = parse(sys.argv[2])

diffResults(d_old, d_new)
