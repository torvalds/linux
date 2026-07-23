#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2019-2022 Red Hat, Inc. Daniel Bristot de Oliveira <bristot@kernel.org>
#
# dot2k: transform dot files into a monitor for the Linux kernel.
#
# For further information, see:
#   Documentation/trace/rv/monitor_synthesis.rst

if __name__ == '__main__':
    from rvgen.dot2k import da2k, ha2k
    from rvgen.generator import Monitor
    from rvgen.container import Container
    from rvgen.ltl2k import ltl2k
    from rvgen.kunit import KUnit, KUnitError
    from rvgen.automata import AutomataError
    from rvgen.ltl2ba import LTLError
    import argparse
    import sys

    parser = argparse.ArgumentParser(description='Generate kernel rv monitor')

    parent_parser = argparse.ArgumentParser(add_help=False)
    parent_parser.add_argument("-D", "--description", dest="description", required=False)
    parent_parser.add_argument("-a", "--auto_patch", dest="auto_patch",
                        action="store_true", required=False,
                        help="Patch the kernel in place")

    subparsers = parser.add_subparsers(dest="subcmd", required=True)

    monitor_parser = subparsers.add_parser("monitor", parents=[parent_parser])
    monitor_parser.add_argument('-n', "--model_name", dest="model_name")
    monitor_parser.add_argument("-p", "--parent", dest="parent",
                                required=False, help="Create a monitor nested to parent")
    monitor_parser.add_argument('-c', "--class", dest="monitor_class", required=True,
                                help="Monitor class, either \"da\", \"ha\" or \"ltl\"")
    monitor_parser.add_argument('-s', "--spec", dest="spec", required=True,
                                help="Monitor specification file")
    monitor_parser.add_argument('-t', "--monitor_type", dest="monitor_type", required=True,
                                help=f"Available options: {', '.join(Monitor.monitor_types.keys())}")

    container_parser = subparsers.add_parser("container", parents=[parent_parser])
    container_parser.add_argument('-n', "--model_name", dest="model_name", required=True)

    kunit_parser = subparsers.add_parser("kunit", parents=[parent_parser])
    kunit_parser.add_argument('-n', "--model_name", dest="model_name", required=True)
    kunit_parser.add_argument('-l', "--local", dest="local", action="store_true", required=False,
                               help="Force looking for the monitor in the current directory only")

    params = parser.parse_args()

    try:
        if params.subcmd == "monitor":
            print(f"Opening and parsing the specification file {params.spec}")
            if params.monitor_class == "da":
                monitor = da2k(params.spec, params.monitor_type, vars(params))
            elif params.monitor_class == "ha":
                monitor = ha2k(params.spec, params.monitor_type, vars(params))
            elif params.monitor_class == "ltl":
                monitor = ltl2k(params.spec, params.monitor_type, vars(params))
            else:
                print("Unknown monitor class:", params.monitor_class)
                sys.exit(1)
        elif params.subcmd == "container":
            monitor = Container(vars(params))
        elif params.subcmd == "kunit":
            monitor = KUnit(vars(params))
            monitor.print_files()
            sys.exit(0)
    except (AutomataError, LTLError) as e:
        print(f"There was an error processing {params.spec}:\n{e}", file=sys.stderr)
        sys.exit(1)
    except KUnitError as e:
        print(f"There was an error generating KUnit files:\n{e}", file=sys.stderr)
        sys.exit(1)

    print(f"Writing the monitor into the directory {monitor.name}")
    monitor.print_files()
    print("Almost done, checklist")
    if params.subcmd == "monitor":
        print(f"  - Edit the {monitor.name}/{monitor.name}.c to add the instrumentation")
        print(monitor.fill_tracepoint_tooltip())
    print(monitor.fill_makefile_tooltip())
    print(monitor.fill_kconfig_tooltip())
    print(monitor.fill_monitor_tooltip())
