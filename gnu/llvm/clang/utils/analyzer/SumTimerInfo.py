#!/usr/bin/env python

"""
Script to Summarize statistics in the scan-build output.

Statistics are enabled by passing '-internal-stats' option to scan-build
(or '-analyzer-stats' to the analyzer).
"""
import sys

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: ", sys.argv[0], "scan_build_output_file", file=sys.stderr)
        sys.exit(-1)

    f = open(sys.argv[1], "r")
    time = 0.0
    total_time = 0.0
    max_time = 0.0
    warnings = 0
    count = 0
    functions_analyzed = 0
    reachable_blocks = 0
    reached_max_steps = 0
    num_steps = 0
    num_inlined_call_sites = 0
    num_bifurcated_call_sites = 0
    max_cfg_size = 0

    for line in f:
        if "Analyzer total time" in line:
            s = line.split()
            time = time + float(s[6])
            count = count + 1
            if float(s[6]) > max_time:
                max_time = float(s[6])
        if "warning generated." in line or "warnings generated" in line:
            s = line.split()
            warnings = warnings + int(s[0])
        if "The # of functions analysed (as top level)" in line:
            s = line.split()
            functions_analyzed = functions_analyzed + int(s[0])
        if "The % of reachable basic blocks" in line:
            s = line.split()
            reachable_blocks = reachable_blocks + int(s[0])
        if "The # of times we reached the max number of steps" in line:
            s = line.split()
            reached_max_steps = reached_max_steps + int(s[0])
        if "The maximum number of basic blocks in a function" in line:
            s = line.split()
            if max_cfg_size < int(s[0]):
                max_cfg_size = int(s[0])
        if "The # of steps executed" in line:
            s = line.split()
            num_steps = num_steps + int(s[0])
        if "The # of times we inlined a call" in line:
            s = line.split()
            num_inlined_call_sites = num_inlined_call_sites + int(s[0])
        if (
            "The # of times we split the path due \
                to imprecise dynamic dispatch info"
            in line
        ):
            s = line.split()
            num_bifurcated_call_sites = num_bifurcated_call_sites + int(s[0])
        if ")  Total" in line:
            s = line.split()
            total_time = total_time + float(s[6])

    print(f"TU count {count}")
    print(f"Time {time}")
    print(f"Warnings {warnings}")
    print(f"Functions analyzed {functions_analyzed}")
    print(f"Reachable blocks {reachable_blocks}")
    print(f"Reached max steps {reached_max_steps}")
    print(f"Number of steps {num_steps}")
    print(
        f"Number of inlined calls {num_inlined_call_sites} "
        f"(bifurcated {num_bifurcated_call_sites})"
    )
    print(f"Max time {max_time}")
    print(f"Total time {total_time}")
    print(f"Max CFG Size {max_cfg_size}")
