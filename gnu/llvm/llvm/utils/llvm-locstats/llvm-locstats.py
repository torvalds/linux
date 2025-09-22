#!/usr/bin/env python
#
# This is a tool that works like debug location coverage calculator.
# It parses the llvm-dwarfdump --statistics output by reporting it
# in a more human readable way.
#

from __future__ import print_function
import argparse
import os
import sys
from json import loads
from math import ceil
from collections import OrderedDict
from subprocess import Popen, PIPE

# This special value has been used to mark statistics that overflowed.
TAINT_VALUE = "tainted"

# Initialize the plot.
def init_plot(plt):
    plt.title("Debug Location Statistics", fontweight="bold")
    plt.xlabel("location buckets")
    plt.ylabel("number of variables in the location buckets")
    plt.xticks(rotation=45, fontsize="x-small")
    plt.yticks()


# Finalize the plot.
def finish_plot(plt):
    plt.legend()
    plt.grid(color="grey", which="major", axis="y", linestyle="-", linewidth=0.3)
    plt.savefig("locstats.png")
    print('The plot was saved within "locstats.png".')


# Holds the debug location statistics.
class LocationStats:
    def __init__(
        self,
        file_name,
        variables_total,
        variables_total_locstats,
        variables_with_loc,
        variables_scope_bytes_covered,
        variables_scope_bytes,
        variables_coverage_map,
    ):
        self.file_name = file_name
        self.variables_total = variables_total
        self.variables_total_locstats = variables_total_locstats
        self.variables_with_loc = variables_with_loc
        self.scope_bytes_covered = variables_scope_bytes_covered
        self.scope_bytes = variables_scope_bytes
        self.variables_coverage_map = variables_coverage_map

    # Get the PC ranges coverage.
    def get_pc_coverage(self):
        if self.scope_bytes_covered == TAINT_VALUE or self.scope_bytes == TAINT_VALUE:
            return TAINT_VALUE
        pc_ranges_covered = int(
            ceil(self.scope_bytes_covered * 100.0) / self.scope_bytes
        )
        return pc_ranges_covered

    # Pretty print the debug location buckets.
    def pretty_print(self):
        if self.scope_bytes == 0:
            print("No scope bytes found.")
            return -1

        pc_ranges_covered = self.get_pc_coverage()
        variables_coverage_per_map = {}
        for cov_bucket in coverage_buckets():
            variables_coverage_per_map[cov_bucket] = None
            if (
                self.variables_coverage_map[cov_bucket] == TAINT_VALUE
                or self.variables_total_locstats == TAINT_VALUE
            ):
                variables_coverage_per_map[cov_bucket] = TAINT_VALUE
            else:
                variables_coverage_per_map[cov_bucket] = int(
                    ceil(self.variables_coverage_map[cov_bucket] * 100.0)
                    / self.variables_total_locstats
                )

        print(" =================================================")
        print("            Debug Location Statistics       ")
        print(" =================================================")
        print("     cov%           samples         percentage(~)  ")
        print(" -------------------------------------------------")
        for cov_bucket in coverage_buckets():
            if (
                self.variables_coverage_map[cov_bucket]
                or self.variables_total_locstats == TAINT_VALUE
            ):
                print(
                    "   {0:10}     {1:8}              {2:3}%".format(
                        cov_bucket,
                        self.variables_coverage_map[cov_bucket],
                        variables_coverage_per_map[cov_bucket],
                    )
                )
            else:
                print(
                    "   {0:10}     {1:8d}              {2:3d}%".format(
                        cov_bucket,
                        self.variables_coverage_map[cov_bucket],
                        variables_coverage_per_map[cov_bucket],
                    )
                )
        print(" =================================================")
        print(
            " -the number of debug variables processed: "
            + str(self.variables_total_locstats)
        )
        print(" -PC ranges covered: " + str(pc_ranges_covered) + "%")

        # Only if we are processing all the variables output the total
        # availability.
        if self.variables_total and self.variables_with_loc:
            total_availability = None
            if (
                self.variables_total == TAINT_VALUE
                or self.variables_with_loc == TAINT_VALUE
            ):
                total_availability = TAINT_VALUE
            else:
                total_availability = int(
                    ceil(self.variables_with_loc * 100.0) / self.variables_total
                )
            print(" -------------------------------------------------")
            print(" -total availability: " + str(total_availability) + "%")
        print(" =================================================")

        return 0

    # Draw a plot representing the location buckets.
    def draw_plot(self):
        from matplotlib import pyplot as plt

        buckets = range(len(self.variables_coverage_map))
        plt.figure(figsize=(12, 8))
        init_plot(plt)
        plt.bar(
            buckets,
            self.variables_coverage_map.values(),
            align="center",
            tick_label=self.variables_coverage_map.keys(),
            label="variables of {}".format(self.file_name),
        )

        # Place the text box with the coverage info.
        pc_ranges_covered = self.get_pc_coverage()
        props = dict(boxstyle="round", facecolor="wheat", alpha=0.5)
        plt.text(
            0.02,
            0.90,
            "PC ranges covered: {}%".format(pc_ranges_covered),
            transform=plt.gca().transAxes,
            fontsize=12,
            verticalalignment="top",
            bbox=props,
        )

        finish_plot(plt)

    # Compare the two LocationStats objects and draw a plot showing
    # the difference.
    def draw_location_diff(self, locstats_to_compare):
        from matplotlib import pyplot as plt

        pc_ranges_covered = self.get_pc_coverage()
        pc_ranges_covered_to_compare = locstats_to_compare.get_pc_coverage()

        buckets = range(len(self.variables_coverage_map))
        buckets_to_compare = range(len(locstats_to_compare.variables_coverage_map))

        fig = plt.figure(figsize=(12, 8))
        ax = fig.add_subplot(111)
        init_plot(plt)

        comparison_keys = list(coverage_buckets())
        ax.bar(
            buckets,
            self.variables_coverage_map.values(),
            align="edge",
            width=0.4,
            label="variables of {}".format(self.file_name),
        )
        ax.bar(
            buckets_to_compare,
            locstats_to_compare.variables_coverage_map.values(),
            color="r",
            align="edge",
            width=-0.4,
            label="variables of {}".format(locstats_to_compare.file_name),
        )
        ax.set_xticks(range(len(comparison_keys)))
        ax.set_xticklabels(comparison_keys)

        props = dict(boxstyle="round", facecolor="wheat", alpha=0.5)
        plt.text(
            0.02,
            0.88,
            "{} PC ranges covered: {}%".format(self.file_name, pc_ranges_covered),
            transform=plt.gca().transAxes,
            fontsize=12,
            verticalalignment="top",
            bbox=props,
        )
        plt.text(
            0.02,
            0.83,
            "{} PC ranges covered: {}%".format(
                locstats_to_compare.file_name, pc_ranges_covered_to_compare
            ),
            transform=plt.gca().transAxes,
            fontsize=12,
            verticalalignment="top",
            bbox=props,
        )

        finish_plot(plt)


# Define the location buckets.
def coverage_buckets():
    yield "0%"
    yield "(0%,10%)"
    for start in range(10, 91, 10):
        yield "[{0}%,{1}%)".format(start, start + 10)
    yield "100%"


# Parse the JSON representing the debug statistics, and create a
# LocationStats object.
def parse_locstats(opts, binary):
    # These will be different due to different options enabled.
    variables_total = None
    variables_total_locstats = None
    variables_with_loc = None
    variables_scope_bytes_covered = None
    variables_scope_bytes = None
    variables_scope_bytes_entry_values = None
    variables_coverage_map = OrderedDict()

    # Get the directory of the LLVM tools.
    llvm_dwarfdump_cmd = os.path.join(os.path.dirname(__file__), "llvm-dwarfdump")
    # The statistics llvm-dwarfdump option.
    llvm_dwarfdump_stats_opt = "--statistics"

    # Generate the stats with the llvm-dwarfdump.
    subproc = Popen(
        [llvm_dwarfdump_cmd, llvm_dwarfdump_stats_opt, binary],
        stdin=PIPE,
        stdout=PIPE,
        stderr=PIPE,
        universal_newlines=True,
    )
    cmd_stdout, cmd_stderr = subproc.communicate()

    # TODO: Handle errors that are coming from llvm-dwarfdump.

    # Get the JSON and parse it.
    json_parsed = None

    try:
        json_parsed = loads(cmd_stdout)
    except:
        print("error: No valid llvm-dwarfdump statistics found.")
        sys.exit(1)

    # TODO: Parse the statistics Version from JSON.

    def init_field(name):
        if json_parsed[name] == "overflowed":
            print('warning: "' + name + '" field overflowed.')
            return TAINT_VALUE
        return json_parsed[name]

    if opts.only_variables:
        # Read the JSON only for local variables.
        variables_total_locstats = init_field(
            "#local vars processed by location statistics"
        )
        variables_scope_bytes_covered = init_field(
            "sum_all_local_vars(#bytes in parent scope covered" " by DW_AT_location)"
        )
        variables_scope_bytes = init_field("sum_all_local_vars(#bytes in parent scope)")
        if not opts.ignore_debug_entry_values:
            for cov_bucket in coverage_buckets():
                cov_category = (
                    "#local vars with {} of parent scope covered "
                    "by DW_AT_location".format(cov_bucket)
                )
                variables_coverage_map[cov_bucket] = init_field(cov_category)
        else:
            variables_scope_bytes_entry_values = init_field(
                "sum_all_local_vars(#bytes in parent scope "
                "covered by DW_OP_entry_value)"
            )
            if (
                variables_scope_bytes_covered != TAINT_VALUE
                and variables_scope_bytes_entry_values != TAINT_VALUE
            ):
                variables_scope_bytes_covered = (
                    variables_scope_bytes_covered - variables_scope_bytes_entry_values
                )
            for cov_bucket in coverage_buckets():
                cov_category = (
                    "#local vars - entry values with {} of parent scope "
                    "covered by DW_AT_location".format(cov_bucket)
                )
                variables_coverage_map[cov_bucket] = init_field(cov_category)
    elif opts.only_formal_parameters:
        # Read the JSON only for formal parameters.
        variables_total_locstats = init_field(
            "#params processed by location statistics"
        )
        variables_scope_bytes_covered = init_field(
            "sum_all_params(#bytes in parent scope covered " "by DW_AT_location)"
        )
        variables_scope_bytes = init_field("sum_all_params(#bytes in parent scope)")
        if not opts.ignore_debug_entry_values:
            for cov_bucket in coverage_buckets():
                cov_category = (
                    "#params with {} of parent scope covered "
                    "by DW_AT_location".format(cov_bucket)
                )
                variables_coverage_map[cov_bucket] = init_field(cov_category)
        else:
            variables_scope_bytes_entry_values = init_field(
                "sum_all_params(#bytes in parent scope covered " "by DW_OP_entry_value)"
            )
            if (
                variables_scope_bytes_covered != TAINT_VALUE
                and variables_scope_bytes_entry_values != TAINT_VALUE
            ):
                variables_scope_bytes_covered = (
                    variables_scope_bytes_covered - variables_scope_bytes_entry_values
                )
            for cov_bucket in coverage_buckets():
                cov_category = (
                    "#params - entry values with {} of parent scope covered"
                    " by DW_AT_location".format(cov_bucket)
                )
                variables_coverage_map[cov_bucket] = init_field(cov_category)
    else:
        # Read the JSON for both local variables and formal parameters.
        variables_total = init_field("#source variables")
        variables_with_loc = init_field("#source variables with location")
        variables_total_locstats = init_field(
            "#variables processed by location statistics"
        )
        variables_scope_bytes_covered = init_field(
            "sum_all_variables(#bytes in parent scope covered " "by DW_AT_location)"
        )
        variables_scope_bytes = init_field("sum_all_variables(#bytes in parent scope)")

        if not opts.ignore_debug_entry_values:
            for cov_bucket in coverage_buckets():
                cov_category = (
                    "#variables with {} of parent scope covered "
                    "by DW_AT_location".format(cov_bucket)
                )
                variables_coverage_map[cov_bucket] = init_field(cov_category)
        else:
            variables_scope_bytes_entry_values = init_field(
                "sum_all_variables(#bytes in parent scope covered "
                "by DW_OP_entry_value)"
            )
            if (
                variables_scope_bytes_covered != TAINT_VALUE
                and variables_scope_bytes_entry_values != TAINT_VALUE
            ):
                variables_scope_bytes_covered = (
                    variables_scope_bytes_covered - variables_scope_bytes_entry_values
                )
            for cov_bucket in coverage_buckets():
                cov_category = (
                    "#variables - entry values with {} of parent scope covered "
                    "by DW_AT_location".format(cov_bucket)
                )
                variables_coverage_map[cov_bucket] = init_field(cov_category)

    return LocationStats(
        binary,
        variables_total,
        variables_total_locstats,
        variables_with_loc,
        variables_scope_bytes_covered,
        variables_scope_bytes,
        variables_coverage_map,
    )


# Parse the program arguments.
def parse_program_args(parser):
    parser.add_argument(
        "--only-variables",
        action="store_true",
        default=False,
        help="calculate the location statistics only for local variables",
    )
    parser.add_argument(
        "--only-formal-parameters",
        action="store_true",
        default=False,
        help="calculate the location statistics only for formal parameters",
    )
    parser.add_argument(
        "--ignore-debug-entry-values",
        action="store_true",
        default=False,
        help="ignore the location statistics on locations with " "entry values",
    )
    parser.add_argument(
        "--draw-plot",
        action="store_true",
        default=False,
        help="show histogram of location buckets generated (requires " "matplotlib)",
    )
    parser.add_argument(
        "--compare",
        action="store_true",
        default=False,
        help="compare the debug location coverage on two files provided, "
        "and draw a plot showing the difference  (requires "
        "matplotlib)",
    )
    parser.add_argument("file_names", nargs="+", type=str, help="file to process")

    return parser.parse_args()


# Verify that the program inputs meet the requirements.
def verify_program_inputs(opts):
    if len(sys.argv) < 2:
        print("error: Too few arguments.")
        return False

    if opts.only_variables and opts.only_formal_parameters:
        print("error: Please use just one --only* option.")
        return False

    if not opts.compare and len(opts.file_names) != 1:
        print("error: Please specify only one file to process.")
        return False

    if opts.compare and len(opts.file_names) != 2:
        print("error: Please specify two files to process.")
        return False

    if opts.draw_plot or opts.compare:
        try:
            import matplotlib
        except ImportError:
            print("error: matplotlib not found.")
            return False

    return True


def Main():
    parser = argparse.ArgumentParser()
    opts = parse_program_args(parser)

    if not verify_program_inputs(opts):
        parser.print_help()
        sys.exit(1)

    binary_file = opts.file_names[0]
    locstats = parse_locstats(opts, binary_file)

    if not opts.compare:
        if opts.draw_plot:
            # Draw a histogram representing the location buckets.
            locstats.draw_plot()
        else:
            # Pretty print collected info on the standard output.
            if locstats.pretty_print() == -1:
                sys.exit(0)
    else:
        binary_file_to_compare = opts.file_names[1]
        locstats_to_compare = parse_locstats(opts, binary_file_to_compare)
        # Draw a plot showing the difference in debug location coverage between
        # two files.
        locstats.draw_location_diff(locstats_to_compare)


if __name__ == "__main__":
    Main()
    sys.exit(0)
