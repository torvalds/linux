#!/usr/bin/env python

from __future__ import print_function

import os
import sys


def check_path(argv):
    if len(argv) < 3:
        print("Wrong number of args")
        return 1

    type = argv[1]
    paths = argv[2:]
    exit_code = 0

    if type == "dir":
        for idx, dir in enumerate(paths):
            print(os.path.isdir(dir))
    elif type == "file":
        for idx, file in enumerate(paths):
            print(os.path.isfile(file))
    else:
        print("Unrecognised type {}".format(type))
        exit_code = 1
    return exit_code


if __name__ == "__main__":
    sys.exit(check_path(sys.argv))
