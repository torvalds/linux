#!/usr/bin/env python
#
# Debugify summary for the original debug info testing.
#

from __future__ import print_function
import argparse
import os
import sys
from json import loads
from collections import defaultdict
from collections import OrderedDict


class DILocBug:
    def __init__(self, action, bb_name, fn_name, instr):
        self.action = action
        self.bb_name = bb_name
        self.fn_name = fn_name
        self.instr = instr

    def __str__(self):
        return self.action + self.bb_name + self.fn_name + self.instr


class DISPBug:
    def __init__(self, action, fn_name):
        self.action = action
        self.fn_name = fn_name

    def __str__(self):
        return self.action + self.fn_name


class DIVarBug:
    def __init__(self, action, name, fn_name):
        self.action = action
        self.name = name
        self.fn_name = fn_name

    def __str__(self):
        return self.action + self.name + self.fn_name


# Report the bugs in form of html.
def generate_html_report(
    di_location_bugs,
    di_subprogram_bugs,
    di_var_bugs,
    di_location_bugs_summary,
    di_sp_bugs_summary,
    di_var_bugs_summary,
    html_file,
):
    fileout = open(html_file, "w")

    html_header = """ <html>
  <head>
  <style>
  table, th, td {
    border: 1px solid black;
  }
  table.center {
    margin-left: auto;
    margin-right: auto;
  }
  </style>
  </head>
  <body>
  """

    # Create the table for Location bugs.
    table_title_di_loc = "Location Bugs found by the Debugify"

    table_di_loc = """<table>
  <caption><b>{}</b></caption>
  <tr>
  """.format(
        table_title_di_loc
    )

    header_di_loc = [
        "File",
        "LLVM Pass Name",
        "LLVM IR Instruction",
        "Function Name",
        "Basic Block Name",
        "Action",
    ]

    for column in header_di_loc:
        table_di_loc += "    <th>{0}</th>\n".format(column.strip())
    table_di_loc += "  </tr>\n"

    at_least_one_bug_found = False

    # Handle loction bugs.
    for file, per_file_bugs in di_location_bugs.items():
        for llvm_pass, per_pass_bugs in per_file_bugs.items():
            # No location bugs for the pass.
            if len(per_pass_bugs) == 0:
                continue
            at_least_one_bug_found = True
            row = []
            table_di_loc += "  </tr>\n"
            # Get the bugs info.
            for x in per_pass_bugs:
                row.append("    <tr>\n")
                row.append(file)
                row.append(llvm_pass)
                row.append(x.instr)
                row.append(x.fn_name)
                row.append(x.bb_name)
                row.append(x.action)
                row.append("    </tr>\n")
            # Dump the bugs info into the table.
            for column in row:
                # The same file-pass pair can have multiple bugs.
                if column == "    <tr>\n" or column == "    </tr>\n":
                    table_di_loc += column
                    continue
                table_di_loc += "    <td>{0}</td>\n".format(column.strip())
            table_di_loc += "  <tr>\n"

    if not at_least_one_bug_found:
        table_di_loc += """  <tr>
        <td colspan='7'> No bugs found </td>
      </tr>
    """
    table_di_loc += "</table>\n"

    # Create the summary table for the loc bugs.
    table_title_di_loc_sum = "Summary of Location Bugs"
    table_di_loc_sum = """<table>
  <caption><b>{}</b></caption>
  <tr>
  """.format(
        table_title_di_loc_sum
    )

    header_di_loc_sum = ["LLVM Pass Name", "Number of bugs"]

    for column in header_di_loc_sum:
        table_di_loc_sum += "    <th>{0}</th>\n".format(column.strip())
    table_di_loc_sum += "  </tr>\n"

    # Print the summary.
    row = []
    for llvm_pass, num in sorted(di_location_bugs_summary.items()):
        row.append("    <tr>\n")
        row.append(llvm_pass)
        row.append(str(num))
        row.append("    </tr>\n")
    for column in row:
        if column == "    <tr>\n" or column == "    </tr>\n":
            table_di_loc_sum += column
            continue
        table_di_loc_sum += "    <td>{0}</td>\n".format(column.strip())
    table_di_loc_sum += "  <tr>\n"

    if not at_least_one_bug_found:
        table_di_loc_sum += """<tr>
        <td colspan='2'> No bugs found </td>
      </tr>
    """
    table_di_loc_sum += "</table>\n"

    # Create the table for SP bugs.
    table_title_di_sp = "SP Bugs found by the Debugify"
    table_di_sp = """<table>
  <caption><b>{}</b></caption>
  <tr>
  """.format(
        table_title_di_sp
    )

    header_di_sp = ["File", "LLVM Pass Name", "Function Name", "Action"]

    for column in header_di_sp:
        table_di_sp += "    <th>{0}</th>\n".format(column.strip())
    table_di_sp += "  </tr>\n"

    at_least_one_bug_found = False

    # Handle fn bugs.
    for file, per_file_bugs in di_subprogram_bugs.items():
        for llvm_pass, per_pass_bugs in per_file_bugs.items():
            # No SP bugs for the pass.
            if len(per_pass_bugs) == 0:
                continue
            at_least_one_bug_found = True
            row = []
            table_di_sp += "  </tr>\n"
            # Get the bugs info.
            for x in per_pass_bugs:
                row.append("    <tr>\n")
                row.append(file)
                row.append(llvm_pass)
                row.append(x.fn_name)
                row.append(x.action)
                row.append("    </tr>\n")
            # Dump the bugs info into the table.
            for column in row:
                # The same file-pass pair can have multiple bugs.
                if column == "    <tr>\n" or column == "    </tr>\n":
                    table_di_sp += column
                    continue
                table_di_sp += "    <td>{0}</td>\n".format(column.strip())
            table_di_sp += "  <tr>\n"

    if not at_least_one_bug_found:
        table_di_sp += """<tr>
        <td colspan='4'> No bugs found </td>
      </tr>
    """
    table_di_sp += "</table>\n"

    # Create the summary table for the sp bugs.
    table_title_di_sp_sum = "Summary of SP Bugs"
    table_di_sp_sum = """<table>
  <caption><b>{}</b></caption>
  <tr>
  """.format(
        table_title_di_sp_sum
    )

    header_di_sp_sum = ["LLVM Pass Name", "Number of bugs"]

    for column in header_di_sp_sum:
        table_di_sp_sum += "    <th>{0}</th>\n".format(column.strip())
    table_di_sp_sum += "  </tr>\n"

    # Print the summary.
    row = []
    for llvm_pass, num in sorted(di_sp_bugs_summary.items()):
        row.append("    <tr>\n")
        row.append(llvm_pass)
        row.append(str(num))
        row.append("    </tr>\n")
    for column in row:
        if column == "    <tr>\n" or column == "    </tr>\n":
            table_di_sp_sum += column
            continue
        table_di_sp_sum += "    <td>{0}</td>\n".format(column.strip())
    table_di_sp_sum += "  <tr>\n"

    if not at_least_one_bug_found:
        table_di_sp_sum += """<tr>
        <td colspan='2'> No bugs found </td>
      </tr>
    """
    table_di_sp_sum += "</table>\n"

    # Create the table for Variable bugs.
    table_title_di_var = "Variable Location Bugs found by the Debugify"
    table_di_var = """<table>
  <caption><b>{}</b></caption>
  <tr>
  """.format(
        table_title_di_var
    )

    header_di_var = ["File", "LLVM Pass Name", "Variable", "Function", "Action"]

    for column in header_di_var:
        table_di_var += "    <th>{0}</th>\n".format(column.strip())
    table_di_var += "  </tr>\n"

    at_least_one_bug_found = False

    # Handle var bugs.
    for file, per_file_bugs in di_var_bugs.items():
        for llvm_pass, per_pass_bugs in per_file_bugs.items():
            # No SP bugs for the pass.
            if len(per_pass_bugs) == 0:
                continue
            at_least_one_bug_found = True
            row = []
            table_di_var += "  </tr>\n"
            # Get the bugs info.
            for x in per_pass_bugs:
                row.append("    <tr>\n")
                row.append(file)
                row.append(llvm_pass)
                row.append(x.name)
                row.append(x.fn_name)
                row.append(x.action)
                row.append("    </tr>\n")
            # Dump the bugs info into the table.
            for column in row:
                # The same file-pass pair can have multiple bugs.
                if column == "    <tr>\n" or column == "    </tr>\n":
                    table_di_var += column
                    continue
                table_di_var += "    <td>{0}</td>\n".format(column.strip())
            table_di_var += "  <tr>\n"

    if not at_least_one_bug_found:
        table_di_var += """<tr>
        <td colspan='4'> No bugs found </td>
      </tr>
    """
    table_di_var += "</table>\n"

    # Create the summary table for the sp bugs.
    table_title_di_var_sum = "Summary of Variable Location Bugs"
    table_di_var_sum = """<table>
  <caption><b>{}</b></caption>
  <tr>
  """.format(
        table_title_di_var_sum
    )

    header_di_var_sum = ["LLVM Pass Name", "Number of bugs"]

    for column in header_di_var_sum:
        table_di_var_sum += "    <th>{0}</th>\n".format(column.strip())
    table_di_var_sum += "  </tr>\n"

    # Print the summary.
    row = []
    for llvm_pass, num in sorted(di_var_bugs_summary.items()):
        row.append("    <tr>\n")
        row.append(llvm_pass)
        row.append(str(num))
        row.append("    </tr>\n")
    for column in row:
        if column == "    <tr>\n" or column == "    </tr>\n":
            table_di_var_sum += column
            continue
        table_di_var_sum += "    <td>{0}</td>\n".format(column.strip())
    table_di_var_sum += "  <tr>\n"

    if not at_least_one_bug_found:
        table_di_var_sum += """<tr>
        <td colspan='2'> No bugs found </td>
      </tr>
    """
    table_di_var_sum += "</table>\n"

    # Finish the html page.
    html_footer = """</body>
  </html>"""

    new_line = "<br>\n"

    fileout.writelines(html_header)
    fileout.writelines(table_di_loc)
    fileout.writelines(new_line)
    fileout.writelines(table_di_loc_sum)
    fileout.writelines(new_line)
    fileout.writelines(new_line)
    fileout.writelines(table_di_sp)
    fileout.writelines(new_line)
    fileout.writelines(table_di_sp_sum)
    fileout.writelines(new_line)
    fileout.writelines(new_line)
    fileout.writelines(table_di_var)
    fileout.writelines(new_line)
    fileout.writelines(table_di_var_sum)
    fileout.writelines(html_footer)
    fileout.close()

    print("The " + html_file + " generated.")


# Read the JSON file in chunks.
def get_json_chunk(file, start, size):
    json_parsed = None
    di_checker_data = []
    skipped_lines = 0
    line = 0

    # The file contains json object per line.
    # An example of the line (formatted json):
    # {
    #  "file": "simple.c",
    #  "pass": "Deduce function attributes in RPO",
    #  "bugs": [
    #    [
    #      {
    #        "action": "drop",
    #        "metadata": "DISubprogram",
    #        "name": "fn2"
    #      },
    #      {
    #        "action": "drop",
    #        "metadata": "DISubprogram",
    #        "name": "fn1"
    #      }
    #    ]
    #  ]
    # }
    with open(file) as json_objects_file:
        for json_object_line in json_objects_file:
            line += 1
            if line < start:
                continue
            if line >= start + size:
                break
            try:
                json_object = loads(json_object_line)
            except:
                skipped_lines += 1
            else:
                di_checker_data.append(json_object)

    return (di_checker_data, skipped_lines, line)


# Parse the program arguments.
def parse_program_args(parser):
    parser.add_argument("file_name", type=str, help="json file to process")
    parser.add_argument("html_file", type=str, help="html file to output data")
    parser.add_argument(
        "-compress", action="store_true", help="create reduced html report"
    )

    return parser.parse_args()


def Main():
    parser = argparse.ArgumentParser()
    opts = parse_program_args(parser)

    if not opts.html_file.endswith(".html"):
        print("error: The output file must be '.html'.")
        sys.exit(1)

    # Use the defaultdict in order to make multidim dicts.
    di_location_bugs = defaultdict(lambda: defaultdict(dict))
    di_subprogram_bugs = defaultdict(lambda: defaultdict(dict))
    di_variable_bugs = defaultdict(lambda: defaultdict(dict))

    # Use the ordered dict to make a summary.
    di_location_bugs_summary = OrderedDict()
    di_sp_bugs_summary = OrderedDict()
    di_var_bugs_summary = OrderedDict()

    # Compress similar bugs.
    # DILocBugs with same pass & instruction name.
    di_loc_pass_instr_set = set()
    # DISPBugs with same pass & function name.
    di_sp_pass_fn_set = set()
    # DIVarBugs with same pass & variable name.
    di_var_pass_var_set = set()

    start_line = 0
    chunk_size = 1000000
    end_line = chunk_size - 1
    skipped_lines = 0
    skipped_bugs = 0
    # Process each chunk of 1 million JSON lines.
    while True:
        if start_line > end_line:
            break
        (debug_info_bugs, skipped, end_line) = get_json_chunk(
            opts.file_name, start_line, chunk_size
        )
        start_line += chunk_size
        skipped_lines += skipped

        # Map the bugs into the file-pass pairs.
        for bugs_per_pass in debug_info_bugs:
            try:
                bugs_file = bugs_per_pass["file"]
                bugs_pass = bugs_per_pass["pass"]
                bugs = bugs_per_pass["bugs"][0]
            except:
                skipped_lines += 1
                continue

            di_loc_bugs = []
            di_sp_bugs = []
            di_var_bugs = []

            # Omit duplicated bugs.
            di_loc_set = set()
            di_sp_set = set()
            di_var_set = set()
            for bug in bugs:
                try:
                    bugs_metadata = bug["metadata"]
                except:
                    skipped_bugs += 1
                    continue

                if bugs_metadata == "DILocation":
                    try:
                        action = bug["action"]
                        bb_name = bug["bb-name"]
                        fn_name = bug["fn-name"]
                        instr = bug["instr"]
                    except:
                        skipped_bugs += 1
                        continue
                    di_loc_bug = DILocBug(action, bb_name, fn_name, instr)
                    if not str(di_loc_bug) in di_loc_set:
                        di_loc_set.add(str(di_loc_bug))
                        if opts.compress:
                            pass_instr = bugs_pass + instr
                            if not pass_instr in di_loc_pass_instr_set:
                                di_loc_pass_instr_set.add(pass_instr)
                                di_loc_bugs.append(di_loc_bug)
                        else:
                            di_loc_bugs.append(di_loc_bug)

                    # Fill the summary dict.
                    if bugs_pass in di_location_bugs_summary:
                        di_location_bugs_summary[bugs_pass] += 1
                    else:
                        di_location_bugs_summary[bugs_pass] = 1
                elif bugs_metadata == "DISubprogram":
                    try:
                        action = bug["action"]
                        name = bug["name"]
                    except:
                        skipped_bugs += 1
                        continue
                    di_sp_bug = DISPBug(action, name)
                    if not str(di_sp_bug) in di_sp_set:
                        di_sp_set.add(str(di_sp_bug))
                        if opts.compress:
                            pass_fn = bugs_pass + name
                            if not pass_fn in di_sp_pass_fn_set:
                                di_sp_pass_fn_set.add(pass_fn)
                                di_sp_bugs.append(di_sp_bug)
                        else:
                            di_sp_bugs.append(di_sp_bug)

                    # Fill the summary dict.
                    if bugs_pass in di_sp_bugs_summary:
                        di_sp_bugs_summary[bugs_pass] += 1
                    else:
                        di_sp_bugs_summary[bugs_pass] = 1
                elif bugs_metadata == "dbg-var-intrinsic":
                    try:
                        action = bug["action"]
                        fn_name = bug["fn-name"]
                        name = bug["name"]
                    except:
                        skipped_bugs += 1
                        continue
                    di_var_bug = DIVarBug(action, name, fn_name)
                    if not str(di_var_bug) in di_var_set:
                        di_var_set.add(str(di_var_bug))
                        if opts.compress:
                            pass_var = bugs_pass + name
                            if not pass_var in di_var_pass_var_set:
                                di_var_pass_var_set.add(pass_var)
                                di_var_bugs.append(di_var_bug)
                        else:
                            di_var_bugs.append(di_var_bug)

                    # Fill the summary dict.
                    if bugs_pass in di_var_bugs_summary:
                        di_var_bugs_summary[bugs_pass] += 1
                    else:
                        di_var_bugs_summary[bugs_pass] = 1
                else:
                    # Unsupported metadata.
                    skipped_bugs += 1
                    continue

            di_location_bugs[bugs_file][bugs_pass] = di_loc_bugs
            di_subprogram_bugs[bugs_file][bugs_pass] = di_sp_bugs
            di_variable_bugs[bugs_file][bugs_pass] = di_var_bugs

    generate_html_report(
        di_location_bugs,
        di_subprogram_bugs,
        di_variable_bugs,
        di_location_bugs_summary,
        di_sp_bugs_summary,
        di_var_bugs_summary,
        opts.html_file,
    )

    if skipped_lines > 0:
        print("Skipped lines: " + str(skipped_lines))
    if skipped_bugs > 0:
        print("Skipped bugs: " + str(skipped_bugs))


if __name__ == "__main__":
    Main()
    sys.exit(0)
