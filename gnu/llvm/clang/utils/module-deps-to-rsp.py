#!/usr/bin/env python3

# Converts clang-scan-deps output into response files.
#
# Usage:
#
#   clang-scan-deps -compilation-database compile_commands.json ... > deps.json
#   module-deps-to-rsp.py deps.json --module-name=ModuleName > module_name.cc1.rsp
#   module-deps-to-rsp.py deps.json --tu-index=0 > tu.rsp
#   clang @module_name.cc1.rsp
#   clang @tu.rsp

import argparse
import json
import sys


class ModuleNotFoundError(Exception):
    def __init__(self, module_name):
        self.module_name = module_name


class FullDeps:
    def __init__(self):
        self.modules = {}
        self.translation_units = []


def findModule(module_name, full_deps):
    for m in full_deps.modules.values():
        if m["name"] == module_name:
            return m
    raise ModuleNotFoundError(module_name)


def parseFullDeps(json):
    ret = FullDeps()
    for m in json["modules"]:
        ret.modules[m["name"] + "-" + m["context-hash"]] = m
    ret.translation_units = json["translation-units"]
    return ret


def quote(str):
    return '"' + str.replace("\\", "\\\\") + '"'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "full_deps_file", help="Path to the full dependencies json file", type=str
    )
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument(
        "--module-name", help="The name of the module to get arguments for", type=str
    )
    action.add_argument(
        "--tu-index",
        help="The index of the translation unit to get arguments for",
        type=int,
    )
    parser.add_argument(
        "--tu-cmd-index",
        help="The index of the command within the translation unit (default=0)",
        type=int,
        default=0,
    )
    args = parser.parse_args()

    full_deps = parseFullDeps(json.load(open(args.full_deps_file, "r")))

    try:
        cmd = []

        if args.module_name:
            cmd = findModule(args.module_name, full_deps)["command-line"]
        elif args.tu_index != None:
            tu = full_deps.translation_units[args.tu_index]
            cmd = tu["commands"][args.tu_cmd_index]["command-line"]

        print(" ".join(map(quote, cmd)))
    except:
        print("Unexpected error:", sys.exc_info()[0])
        raise


if __name__ == "__main__":
    main()
