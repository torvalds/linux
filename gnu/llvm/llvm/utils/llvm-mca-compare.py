#!/usr/bin/env python3

import argparse
import sys
import os
from json import loads
from subprocess import Popen, PIPE

# Holds code regions statistics.
class Summary:
    def __init__(
        self,
        name,
        block_rthroughput,
        dispatch_width,
        ipc,
        instructions,
        iterations,
        total_cycles,
        total_uops,
        uops_per_cycle,
        iteration_resource_pressure,
        name_target_info_resources,
    ):
        self.name = name
        self.block_rthroughput = block_rthroughput
        self.dispatch_width = dispatch_width
        self.ipc = ipc
        self.instructions = instructions
        self.iterations = iterations
        self.total_cycles = total_cycles
        self.total_uops = total_uops
        self.uops_per_cycle = uops_per_cycle
        self.iteration_resource_pressure = iteration_resource_pressure
        self.name_target_info_resources = name_target_info_resources


# Parse the program arguments.
def parse_program_args(parser):
    parser.add_argument(
        "file_names",
        nargs="+",
        type=str,
        help="Names of files which llvm-mca tool process.",
    )
    parser.add_argument(
        "--llvm-mca-binary",
        nargs=1,
        required=True,
        type=str,
        action="store",
        metavar="[=<path to llvm-mca>]",
        help="Specified relative path to binary of llvm-mca.",
    )
    parser.add_argument(
        "--args",
        nargs=1,
        type=str,
        action="store",
        metavar="[='-option1=<arg> -option2=<arg> ...']",
        default=["-"],
        help="Forward options to lvm-mca tool.",
    )
    parser.add_argument(
        "-plot",
        action="store_true",
        default=False,
        help="Draw plots of statistics for input files.",
    )
    parser.add_argument(
        "-plot-resource-pressure",
        action="store_true",
        default=False,
        help="Draw plots of resource pressure per iterations for input files.",
    )
    parser.add_argument(
        "--plot-path",
        nargs=1,
        type=str,
        action="store",
        metavar="[=<path>]",
        default=["-"],
        help="Specify relative path where you want to save the plots.",
    )
    parser.add_argument(
        "-v",
        action="store_true",
        default=False,
        help="More details about the running lvm-mca tool.",
    )
    return parser.parse_args()


# Verify that the program inputs meet the requirements.
def verify_program_inputs(opts):
    if opts.plot_path[0] != "-" and not opts.plot and not opts.plot_resource_pressure:
        print(
            "error: Please specify --plot-path only with the -plot or -plot-resource-pressure options."
        )
        return False

    return True


# Returns the name of the file to be analyzed from the path it is on.
def get_filename_from_path(path):
    index_of_slash = path.rfind("/")
    return path[(index_of_slash + 1) : len(path)]


# Returns the results of the running llvm-mca tool for the input file.
def run_llvm_mca_tool(opts, file_name):
    # Get the path of the llvm-mca binary file.
    llvm_mca_cmd = opts.llvm_mca_binary[0]

    # The statistics llvm-mca options.
    if opts.args[0] != "-":
        llvm_mca_cmd += " " + opts.args[0]
    llvm_mca_cmd += " -json"

    # Set file which llvm-mca tool will process.
    llvm_mca_cmd += " " + file_name

    if opts.v:
        print("run: $ " + llvm_mca_cmd + "\n")

    # Generate the stats with the llvm-mca.
    subproc = Popen(
        llvm_mca_cmd.split(" "),
        stdin=PIPE,
        stdout=PIPE,
        stderr=PIPE,
        universal_newlines=True,
    )

    cmd_stdout, cmd_stderr = subproc.communicate()

    try:
        json_parsed = loads(cmd_stdout)
    except:
        print("error: No valid llvm-mca statistics found.")
        print(cmd_stderr)
        sys.exit(1)

    if opts.v:
        print("Simulation Parameters: ")
        simulation_parameters = json_parsed["SimulationParameters"]
        for key in simulation_parameters:
            print(key, ":", simulation_parameters[key])
        print("\n")

    code_regions_len = len(json_parsed["CodeRegions"])
    array_of_code_regions = [None] * code_regions_len

    for i in range(code_regions_len):
        code_region_instructions_len = len(
            json_parsed["CodeRegions"][i]["Instructions"]
        )
        target_info_resources_len = len(json_parsed["TargetInfo"]["Resources"])
        iteration_resource_pressure = ["-" for k in range(target_info_resources_len)]
        resource_pressure_info = json_parsed["CodeRegions"][i]["ResourcePressureView"][
            "ResourcePressureInfo"
        ]

        name_target_info_resources = json_parsed["TargetInfo"]["Resources"]

        for s in range(len(resource_pressure_info)):
            obj_of_resource_pressure_info = resource_pressure_info[s]
            if (
                obj_of_resource_pressure_info["InstructionIndex"]
                == code_region_instructions_len
            ):
                iteration_resource_pressure[
                    obj_of_resource_pressure_info["ResourceIndex"]
                ] = str(round(obj_of_resource_pressure_info["ResourceUsage"], 2))

        array_of_code_regions[i] = Summary(
            file_name,
            json_parsed["CodeRegions"][i]["SummaryView"]["BlockRThroughput"],
            json_parsed["CodeRegions"][i]["SummaryView"]["DispatchWidth"],
            json_parsed["CodeRegions"][i]["SummaryView"]["IPC"],
            json_parsed["CodeRegions"][i]["SummaryView"]["Instructions"],
            json_parsed["CodeRegions"][i]["SummaryView"]["Iterations"],
            json_parsed["CodeRegions"][i]["SummaryView"]["TotalCycles"],
            json_parsed["CodeRegions"][i]["SummaryView"]["TotaluOps"],
            json_parsed["CodeRegions"][i]["SummaryView"]["uOpsPerCycle"],
            iteration_resource_pressure,
            name_target_info_resources,
        )

    return array_of_code_regions


# Print statistics in console for single file or for multiple files.
def console_print_results(matrix_of_code_regions, opts):
    try:
        import termtables as tt
    except ImportError:
        print("error: termtables not found.")
        sys.exit(1)

    headers_names = [None] * (len(opts.file_names) + 1)
    headers_names[0] = " "

    max_code_regions = 0

    print("Input files:")
    for i in range(len(matrix_of_code_regions)):
        if max_code_regions < len(matrix_of_code_regions[i]):
            max_code_regions = len(matrix_of_code_regions[i])
        print("[f" + str(i + 1) + "]: " + get_filename_from_path(opts.file_names[i]))
        headers_names[i + 1] = "[f" + str(i + 1) + "]: "

    print("\nITERATIONS: " + str(matrix_of_code_regions[0][0].iterations) + "\n")

    for i in range(max_code_regions):

        print(
            "\n-----------------------------------------\nCode region: "
            + str(i + 1)
            + "\n"
        )

        table_values = [
            [[None] for i in range(len(matrix_of_code_regions) + 1)] for j in range(7)
        ]

        table_values[0][0] = "Instructions: "
        table_values[1][0] = "Total Cycles: "
        table_values[2][0] = "Total uOps: "
        table_values[3][0] = "Dispatch Width: "
        table_values[4][0] = "uOps Per Cycle: "
        table_values[5][0] = "IPC: "
        table_values[6][0] = "Block RThroughput: "

        for j in range(len(matrix_of_code_regions)):
            if len(matrix_of_code_regions[j]) > i:
                table_values[0][j + 1] = str(matrix_of_code_regions[j][i].instructions)
                table_values[1][j + 1] = str(matrix_of_code_regions[j][i].total_cycles)
                table_values[2][j + 1] = str(matrix_of_code_regions[j][i].total_uops)
                table_values[3][j + 1] = str(
                    matrix_of_code_regions[j][i].dispatch_width
                )
                table_values[4][j + 1] = str(
                    round(matrix_of_code_regions[j][i].uops_per_cycle, 2)
                )
                table_values[5][j + 1] = str(round(matrix_of_code_regions[j][i].ipc, 2))
                table_values[6][j + 1] = str(
                    round(matrix_of_code_regions[j][i].block_rthroughput, 2)
                )
            else:
                table_values[0][j + 1] = "-"
                table_values[1][j + 1] = "-"
                table_values[2][j + 1] = "-"
                table_values[3][j + 1] = "-"
                table_values[4][j + 1] = "-"
                table_values[5][j + 1] = "-"
                table_values[6][j + 1] = "-"

        tt.print(
            table_values,
            header=headers_names,
            style=tt.styles.ascii_thin_double,
            padding=(0, 1),
        )

        print("\nResource pressure per iteration: \n")

        table_values = [
            [
                [None]
                for i in range(
                    len(matrix_of_code_regions[0][0].iteration_resource_pressure) + 1
                )
            ]
            for j in range(len(matrix_of_code_regions) + 1)
        ]

        table_values[0] = [" "] + matrix_of_code_regions[0][
            0
        ].name_target_info_resources

        for j in range(len(matrix_of_code_regions)):
            if len(matrix_of_code_regions[j]) > i:
                table_values[j + 1] = [
                    "[f" + str(j + 1) + "]: "
                ] + matrix_of_code_regions[j][i].iteration_resource_pressure
            else:
                table_values[j + 1] = ["[f" + str(j + 1) + "]: "] + len(
                    matrix_of_code_regions[0][0].iteration_resource_pressure
                ) * ["-"]

        tt.print(
            table_values,
            style=tt.styles.ascii_thin_double,
            padding=(0, 1),
        )
        print("\n")


# Based on the obtained results (summary view) of llvm-mca tool, draws plots for multiple input files.
def draw_plot_files_summary(array_of_summary, opts):
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("error: matplotlib.pyplot not found.")
        sys.exit(1)
    try:
        from matplotlib.cm import get_cmap
    except ImportError:
        print("error: get_cmap (matplotlib.cm) not found.")
        sys.exit(1)

    names = [
        "Block RThroughput",
        "Dispatch Width",
        "IPC",
        "uOps Per Cycle",
        "Instructions",
        "Total Cycles",
        "Total uOps",
    ]

    rows, cols = (len(opts.file_names), 7)

    values = [[0 for x in range(cols)] for y in range(rows)]

    for i in range(len(opts.file_names)):
        values[i][0] = array_of_summary[i].block_rthroughput
        values[i][1] = array_of_summary[i].dispatch_width
        values[i][2] = array_of_summary[i].ipc
        values[i][3] = array_of_summary[i].uops_per_cycle
        values[i][4] = array_of_summary[i].instructions
        values[i][5] = array_of_summary[i].total_cycles
        values[i][6] = array_of_summary[i].total_uops

    fig, axs = plt.subplots(4, 2)
    fig.suptitle(
        "Machine code statistics", fontsize=20, fontweight="bold", color="black"
    )
    i = 0

    for x in range(4):
        for y in range(2):
            cmap = get_cmap("tab20")
            colors = cmap.colors
            if not (x == 0 and y == 1) and i < 7:
                axs[x][y].grid(True, color="grey", linestyle="--")
                maxValue = 0
                if i == 0:
                    for j in range(len(opts.file_names)):
                        if maxValue < values[j][i]:
                            maxValue = values[j][i]
                        axs[x][y].bar(
                            0.3 * j,
                            values[j][i],
                            width=0.1,
                            color=colors[j],
                            label=get_filename_from_path(opts.file_names[j]),
                        )
                else:
                    for j in range(len(opts.file_names)):
                        if maxValue < values[j][i]:
                            maxValue = values[j][i]
                        axs[x][y].bar(0.3 * j, values[j][i], width=0.1, color=colors[j])
                axs[x][y].set_axisbelow(True)
                axs[x][y].set_xlim([-0.3, len(opts.file_names) / 3])
                axs[x][y].set_ylim([0, maxValue + (maxValue / 2)])
                axs[x][y].set_title(names[i], fontsize=15, fontweight="bold")
                axs[x][y].axes.xaxis.set_visible(False)
                for j in range(len(opts.file_names)):
                    axs[x][y].text(
                        0.3 * j,
                        values[j][i] + (maxValue / 40),
                        s=str(values[j][i]),
                        color="black",
                        fontweight="bold",
                        fontsize=4,
                    )
                i = i + 1

    axs[0][1].set_visible(False)
    fig.legend(prop={"size": 15})
    figg = plt.gcf()
    figg.set_size_inches((25, 15), forward=False)
    if opts.plot_path[0] == "-":
        plt.savefig("llvm-mca-plot.png", dpi=500)
        print("The plot was saved within llvm-mca-plot.png")
    else:
        plt.savefig(
            os.path.normpath(os.path.join(opts.plot_path[0], "llvm-mca-plot.png")),
            dpi=500,
        )
        print(
            "The plot was saved within {}.".format(
                os.path.normpath(os.path.join(opts.plot_path[0], "llvm-mca-plot.png"))
            )
        )


# Calculates the average value (summary view) per region.
def summary_average_code_region(array_of_code_regions, file_name):
    summary = Summary(file_name, 0, 0, 0, 0, 0, 0, 0, 0, None, None)
    for i in range(len(array_of_code_regions)):
        summary.block_rthroughput += array_of_code_regions[i].block_rthroughput
        summary.dispatch_width += array_of_code_regions[i].dispatch_width
        summary.ipc += array_of_code_regions[i].ipc
        summary.instructions += array_of_code_regions[i].instructions
        summary.iterations += array_of_code_regions[i].iterations
        summary.total_cycles += array_of_code_regions[i].total_cycles
        summary.total_uops += array_of_code_regions[i].total_uops
        summary.uops_per_cycle += array_of_code_regions[i].uops_per_cycle
    summary.block_rthroughput = round(
        summary.block_rthroughput / len(array_of_code_regions), 2
    )
    summary.dispatch_width = round(
        summary.dispatch_width / len(array_of_code_regions), 2
    )
    summary.ipc = round(summary.ipc / len(array_of_code_regions), 2)
    summary.instructions = round(summary.instructions / len(array_of_code_regions), 2)
    summary.iterations = round(summary.iterations / len(array_of_code_regions), 2)
    summary.total_cycles = round(summary.total_cycles / len(array_of_code_regions), 2)
    summary.total_uops = round(summary.total_uops / len(array_of_code_regions), 2)
    summary.uops_per_cycle = round(
        summary.uops_per_cycle / len(array_of_code_regions), 2
    )
    return summary


# Based on the obtained results (resource pressure per iter) of llvm-mca tool, draws plots for multiple input files.
def draw_plot_resource_pressure(
    array_average_resource_pressure_per_file, opts, name_target_info_resources
):
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("error: matplotlib.pyplot not found.")
        sys.exit(1)
    try:
        from matplotlib.cm import get_cmap
    except ImportError:
        print("error: get_cmap (matplotlib.cm) not found.")
        sys.exit(1)

    fig, axs = plt.subplots()
    fig.suptitle(
        "Resource pressure per iterations",
        fontsize=20,
        fontweight="bold",
        color="black",
    )

    maxValue = 0
    for j in range(len(opts.file_names)):
        if maxValue < max(array_average_resource_pressure_per_file[j]):
            maxValue = max(array_average_resource_pressure_per_file[j])

    cmap = get_cmap("tab20")
    colors = cmap.colors

    xticklabels = [None] * len(opts.file_names) * len(name_target_info_resources)
    index = 0

    for j in range(len(name_target_info_resources)):
        for i in range(len(opts.file_names)):
            if i == 0:
                axs.bar(
                    j * len(opts.file_names) * 10 + i * 10,
                    array_average_resource_pressure_per_file[i][j],
                    width=1,
                    color=colors[j],
                    label=name_target_info_resources[j],
                )
            else:
                axs.bar(
                    j * len(opts.file_names) * 10 + i * 10,
                    array_average_resource_pressure_per_file[i][j],
                    width=1,
                    color=colors[j],
                )
            axs.text(
                j * len(opts.file_names) * 10 + i * 10,
                array_average_resource_pressure_per_file[i][j] + (maxValue / 40),
                s=str(array_average_resource_pressure_per_file[i][j]),
                color=colors[j],
                fontweight="bold",
                fontsize=3,
            )
            xticklabels[index] = opts.file_names[i]
            index = index + 1

    axs.set_xticks(
        [
            j * len(opts.file_names) * 10 + i * 10
            for j in range(len(name_target_info_resources))
            for i in range(len(opts.file_names))
        ]
    )
    axs.set_xticklabels(xticklabels, rotation=65)

    axs.set_axisbelow(True)
    axs.set_xlim([-0.5, len(opts.file_names) * len(name_target_info_resources) * 10])
    axs.set_ylim([0, maxValue + maxValue / 10])

    fig.legend(prop={"size": 15})
    figg = plt.gcf()
    figg.set_size_inches((25, 15), forward=False)
    if opts.plot_path[0] == "-":
        plt.savefig("llvm-mca-plot-resource-pressure.png", dpi=500)
        print("The plot was saved within llvm-mca-plot-resource-pressure.png")
    else:
        plt.savefig(
            os.path.normpath(
                os.path.join(opts.plot_path[0], "llvm-mca-plot-resource-pressure.png")
            ),
            dpi=500,
        )
        print(
            "The plot was saved within {}.".format(
                os.path.normpath(
                    os.path.join(
                        opts.plot_path[0], "llvm-mca-plot-resource-pressure.png"
                    )
                )
            )
        )


# Calculates the average value (resource pressure per iter) per region.
def average_code_region_resource_pressure(array_of_code_regions, file_name):
    resource_pressure_per_iter_one_file = [0] * len(
        array_of_code_regions[0].iteration_resource_pressure
    )
    for i in range(len(array_of_code_regions)):
        for j in range(len(array_of_code_regions[i].iteration_resource_pressure)):
            if array_of_code_regions[i].iteration_resource_pressure[j] != "-":
                resource_pressure_per_iter_one_file[j] += float(
                    array_of_code_regions[i].iteration_resource_pressure[j]
                )
    for i in range(len(resource_pressure_per_iter_one_file)):
        resource_pressure_per_iter_one_file[i] = round(
            resource_pressure_per_iter_one_file[i] / len(array_of_code_regions), 2
        )
    return resource_pressure_per_iter_one_file


def Main():
    parser = argparse.ArgumentParser()
    opts = parse_program_args(parser)

    if not verify_program_inputs(opts):
        parser.print_help()
        sys.exit(1)

    matrix_of_code_regions = [None] * len(opts.file_names)

    for i in range(len(opts.file_names)):
        matrix_of_code_regions[i] = run_llvm_mca_tool(opts, opts.file_names[i])
    if not opts.plot and not opts.plot_resource_pressure:
        console_print_results(matrix_of_code_regions, opts)
    else:
        if opts.plot:
            array_average_summary_per_file = [None] * len(matrix_of_code_regions)
            for j in range(len(matrix_of_code_regions)):
                array_average_summary_per_file[j] = summary_average_code_region(
                    matrix_of_code_regions[j], opts.file_names[j]
                )
            draw_plot_files_summary(array_average_summary_per_file, opts)
        if opts.plot_resource_pressure:
            array_average_resource_pressure_per_file = [None] * len(
                matrix_of_code_regions
            )
            for j in range(len(matrix_of_code_regions)):
                array_average_resource_pressure_per_file[
                    j
                ] = average_code_region_resource_pressure(
                    matrix_of_code_regions[j], opts.file_names[j]
                )
            draw_plot_resource_pressure(
                array_average_resource_pressure_per_file,
                opts,
                matrix_of_code_regions[0][0].name_target_info_resources,
            )


if __name__ == "__main__":
    Main()
    sys.exit(0)
