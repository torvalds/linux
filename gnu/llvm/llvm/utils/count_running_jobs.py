# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Tool for counting the number of currently running Github actions jobs.

This tool counts and enumerates the currently active jobs in Github actions
for the monorepo.

python3 ./count_running_jobs.py --token=<github token>

Note that the token argument is optional. If it is not specified, the queries
will be performed unauthenticated.
"""

import argparse
import github
import sys
import time


def main(token, filter_gha_runners):
    workflows = (
        github.Github(args.token)
        .get_repo("llvm/llvm-project")
        .get_workflow_runs(status="in_progress")
    )

    in_progress_jobs = 0

    for workflow in workflows:
        for job in workflow.jobs():
            if job.status == "in_progress":
                # TODO(boomanaiden154): Remove the try/except block once we are able
                # to pull in a PyGithub release that has the runner_group_name property
                # for workflow jobs.
                try:
                    if filter_gha_runners and job.runner_group_name != "GitHub Actions":
                        continue
                except:
                    print(
                        "Failed to filter runners. Your PyGithub version is "
                        "most likely too old."
                    )
                print(f"{workflow.name}/{job.name}")
                in_progress_jobs += 1

    print(f"\nFound {in_progress_jobs} running jobs.")

    return in_progress_jobs


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="A tool for listing and counting Github actions jobs"
    )
    parser.add_argument(
        "--token",
        type=str,
        help="The Github token to use to authorize with the API",
        default=None,
        nargs="?",
    )
    parser.add_argument(
        "--output-file",
        type=str,
        help="The output file to write time-series data to",
        default=None,
        nargs="?",
    )
    parser.add_argument(
        "--data-collection-interval",
        type=int,
        help="The number of seconds between data collection intervals",
        default=None,
        nargs="?",
    )
    parser.add_argument(
        "--filter-gha-runners",
        help="Only consider jobs running on hosted Github actions runners",
        action="store_true",
    )
    parser.add_argument(
        "--no-filter-gha-runners",
        dest="filter_gha_runners",
        action="store_false",
        help="Consider all running jobs",
    )
    parser.set_defaults(filter_gha_runners=False)
    args = parser.parse_args()

    # Perform some basic argument validation

    # If an output file is specified, the user must also specify the data
    # collection interval.
    if bool(args.output_file) and not bool(args.data_collection_interval):
        print("A data collection interval must be specified when --output_file is used")
        sys.exit(1)

    if args.data_collection_interval:
        while True:
            current_time = time.localtime()
            current_time_string = time.strftime("%Y/%m/%d %H:%M:%S", current_time)

            print(f"Collecting data at {current_time_string}")

            current_job_count = main(args.token, args.filter_gha_runners)

            if args.output_file:
                with open(args.output_file, "a") as output_file_handle:
                    output_file_handle.write(
                        f"{current_time_string},{current_job_count}\n"
                    )

            time.sleep(args.data_collection_interval)
    else:
        main(args.token, args.filter_gha_runners)
