#!/usr/bin/env python
"""lldb-repro

lldb-repro is a utility to transparently capture and replay debugger sessions
through the command line driver. Its used to test the reproducers by running
the test suite twice.

During the first run, with 'capture' as its first argument, it captures a
reproducer for every lldb invocation and saves it to a well-know location
derived from the arguments and current working directory.

During the second run, with 'replay' as its first argument, the test suite is
run again but this time every invocation of lldb replays the previously
recorded session.
"""

import hashlib
import os
import shutil
import subprocess
import sys
import tempfile


def help():
    print("usage: {} capture|replay [args]".format(sys.argv[0]))


def main():
    if len(sys.argv) < 2:
        help()
        return 1

    # Compute an MD5 hash based on the input arguments and the current working
    # directory.
    h = hashlib.md5()
    h.update(" ".join(sys.argv[2:]).encode("utf-8"))
    h.update(os.getcwd().encode("utf-8"))
    input_hash = h.hexdigest()

    # Use the hash to "uniquely" identify a reproducer path.
    reproducer_path = os.path.join(tempfile.gettempdir(), input_hash)

    # Create a new lldb invocation with capture or replay enabled.
    lldb = os.path.join(os.path.dirname(sys.argv[0]), "lldb")
    new_args = [lldb]
    if sys.argv[1] == "replay":
        new_args.extend(["--replay", reproducer_path])
    elif sys.argv[1] == "capture":
        new_args.extend(
            [
                "--capture",
                "--capture-path",
                reproducer_path,
                "--reproducer-generate-on-exit",
            ]
        )
        new_args.extend(sys.argv[2:])
    else:
        help()
        return 1

    exit_code = subprocess.call(new_args)

    # The driver always exists with a zero exit code during replay. Store the
    # exit code and return that for tests that expect a non-zero exit code.
    exit_code_path = os.path.join(reproducer_path, "exit_code.txt")
    if sys.argv[1] == "replay":
        replay_exit_code = exit_code
        with open(exit_code_path, "r") as f:
            exit_code = int(f.read())
        if replay_exit_code != 0:
            print("error: replay failed with exit code {}".format(replay_exit_code))
            print("invocation: " + " ".join(new_args))
            # Return 1 if the expected exit code is 0 or vice versa.
            return 1 if (exit_code == 0) else 0
        shutil.rmtree(reproducer_path, True)
    elif sys.argv[1] == "capture":
        with open(exit_code_path, "w") as f:
            f.write("%d" % exit_code)

    return exit_code


if __name__ == "__main__":
    exit(main())
