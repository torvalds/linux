#!/usr/bin/env python3

"""Write content into file."""

import argparse
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("filepath")
    parser.add_argument("content")

    args = parser.parse_args()

    with open(args.filepath, "w") as f:
        f.write(args.content)


if __name__ == "__main__":
    sys.exit(main())
