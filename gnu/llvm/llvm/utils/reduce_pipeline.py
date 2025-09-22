#!/usr/bin/env python3

# Automatically formatted with yapf (https://github.com/google/yapf)

# Script for automatic 'opt' pipeline reduction for when using the new
# pass-manager (NPM). Based around the '-print-pipeline-passes' option.
#
# The reduction algorithm consists of several phases (steps).
#
# Step #0: Verify that input fails with the given pipeline and make note of the
# error code.
#
# Step #1: Split pipeline in two starting from front and move forward as long as
# first pipeline exits normally and the second pipeline fails with the expected
# error code. Move on to step #2 with the IR from the split point and the
# pipeline from the second invocation.
#
# Step #2: Remove passes from end of the pipeline as long as the pipeline fails
# with the expected error code.
#
# Step #3: Make several sweeps over the remaining pipeline trying to remove one
# pass at a time. Repeat sweeps until unable to remove any more passes.
#
# Usage example:
# reduce_pipeline.py --opt-binary=./build-all-Debug/bin/opt --input=input.ll --output=output.ll --passes=PIPELINE [EXTRA-OPT-ARGS ...]

import argparse
import pipeline
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(
    description="Automatic opt pipeline reducer. Unrecognized arguments are forwarded to opt."
)
parser.add_argument("--opt-binary", action="store", dest="opt_binary", default="opt")
parser.add_argument("--passes", action="store", dest="passes", required=True)
parser.add_argument("--input", action="store", dest="input", required=True)
parser.add_argument("--output", action="store", dest="output")
parser.add_argument(
    "--dont-expand-passes",
    action="store_true",
    dest="dont_expand_passes",
    help="Do not expand pipeline before starting reduction.",
)
parser.add_argument(
    "--dont-remove-empty-pm",
    action="store_true",
    dest="dont_remove_empty_pm",
    help="Do not remove empty pass-managers from the pipeline during reduction.",
)
[args, extra_opt_args] = parser.parse_known_args()

print("The following extra args will be passed to opt: {}".format(extra_opt_args))

lst = pipeline.fromStr(args.passes)
ll_input = args.input

# Step #-1
# Launch 'opt' once with '-print-pipeline-passes' to expand pipeline before
# starting reduction. Allows specifying a default pipelines (e.g.
# '-passes=default<O3>').
if not args.dont_expand_passes:
    run_args = [
        args.opt_binary,
        "-disable-symbolication",
        "-disable-output",
        "-print-pipeline-passes",
        "-passes={}".format(pipeline.toStr(lst)),
        ll_input,
    ]
    run_args.extend(extra_opt_args)
    opt = subprocess.run(run_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if opt.returncode != 0:
        print("Failed to expand passes. Aborting.")
        print(run_args)
        print("exitcode: {}".format(opt.returncode))
        print(opt.stderr.decode())
        exit(1)
    stdout = opt.stdout.decode()
    stdout = stdout[: stdout.rfind("\n")]
    lst = pipeline.fromStr(stdout)
    print("Expanded pass sequence: {}".format(pipeline.toStr(lst)))

# Step #0
# Confirm that the given input, passes and options result in failure.
print("---Starting step #0---")
run_args = [
    args.opt_binary,
    "-disable-symbolication",
    "-disable-output",
    "-passes={}".format(pipeline.toStr(lst)),
    ll_input,
]
run_args.extend(extra_opt_args)
opt = subprocess.run(run_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
if opt.returncode >= 0:
    print("Input does not result in failure as expected. Aborting.")
    print(run_args)
    print("exitcode: {}".format(opt.returncode))
    print(opt.stderr.decode())
    exit(1)

expected_error_returncode = opt.returncode
print('-passes="{}"'.format(pipeline.toStr(lst)))

# Step #1
# Try to narrow down the failing pass sequence by splitting the pipeline in two
# opt invocations (A and B) starting with invocation A only running the first
# pipeline pass and invocation B the remaining. Keep moving the split point
# forward as long as invocation A exits normally and invocation B fails with
# the expected error. This will accomplish two things first the input IR will be
# further reduced and second, with that IR, the reduced pipeline for invocation
# B will be sufficient to reproduce.
print("---Starting step #1---")
prevLstB = None
prevIntermediate = None
tmpd = tempfile.TemporaryDirectory()

for idx in range(pipeline.count(lst)):
    [lstA, lstB] = pipeline.split(lst, idx)
    if not args.dont_remove_empty_pm:
        lstA = pipeline.prune(lstA)
        lstB = pipeline.prune(lstB)

    intermediate = "intermediate-0.ll" if idx % 2 else "intermediate-1.ll"
    intermediate = tmpd.name + "/" + intermediate
    run_args = [
        args.opt_binary,
        "-disable-symbolication",
        "-S",
        "-o",
        intermediate,
        "-passes={}".format(pipeline.toStr(lstA)),
        ll_input,
    ]
    run_args.extend(extra_opt_args)
    optA = subprocess.run(run_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    run_args = [
        args.opt_binary,
        "-disable-symbolication",
        "-disable-output",
        "-passes={}".format(pipeline.toStr(lstB)),
        intermediate,
    ]
    run_args.extend(extra_opt_args)
    optB = subprocess.run(run_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if not (optA.returncode == 0 and optB.returncode == expected_error_returncode):
        break
    prevLstB = lstB
    prevIntermediate = intermediate
if prevLstB:
    lst = prevLstB
    ll_input = prevIntermediate
print('-passes="{}"'.format(pipeline.toStr(lst)))

# Step #2
# Try removing passes from the end of the remaining pipeline while still
# reproducing the error.
print("---Starting step #2---")
prevLstA = None
for idx in reversed(range(pipeline.count(lst))):
    [lstA, lstB] = pipeline.split(lst, idx)
    if not args.dont_remove_empty_pm:
        lstA = pipeline.prune(lstA)
    run_args = [
        args.opt_binary,
        "-disable-symbolication",
        "-disable-output",
        "-passes={}".format(pipeline.toStr(lstA)),
        ll_input,
    ]
    run_args.extend(extra_opt_args)
    optA = subprocess.run(run_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if optA.returncode != expected_error_returncode:
        break
    prevLstA = lstA
if prevLstA:
    lst = prevLstA
print('-passes="{}"'.format(pipeline.toStr(lst)))

# Step #3
# Now that we have a pipeline that is reduced both front and back we do
# exhaustive sweeps over the remainder trying to remove one pass at a time.
# Repeat as long as reduction is possible.
print("---Starting step #3---")
while True:
    keepGoing = False
    for idx in range(pipeline.count(lst)):
        candLst = pipeline.remove(lst, idx)
        if not args.dont_remove_empty_pm:
            candLst = pipeline.prune(candLst)
        run_args = [
            args.opt_binary,
            "-disable-symbolication",
            "-disable-output",
            "-passes={}".format(pipeline.toStr(candLst)),
            ll_input,
        ]
        run_args.extend(extra_opt_args)
        opt = subprocess.run(run_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if opt.returncode == expected_error_returncode:
            lst = candLst
            keepGoing = True
    if not keepGoing:
        break
print('-passes="{}"'.format(pipeline.toStr(lst)))

print("---FINISHED---")
if args.output:
    shutil.copy(ll_input, args.output)
    print("Wrote output to '{}'.".format(args.output))
print('-passes="{}"'.format(pipeline.toStr(lst)))
exit(0)
