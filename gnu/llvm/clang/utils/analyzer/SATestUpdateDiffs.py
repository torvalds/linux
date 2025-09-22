#!/usr/bin/env python

"""
Update reference results for static analyzer.
"""
import SATestBuild
from ProjectMap import ProjectInfo, ProjectMap

import os
import shutil
import sys

from subprocess import check_call

Verbose = 0


def update_reference_results(project: ProjectInfo, git: bool = False):
    test_info = SATestBuild.TestInfo(project)
    tester = SATestBuild.ProjectTester(test_info)
    project_dir = tester.get_project_dir()

    tester.is_reference_build = True
    ref_results_path = tester.get_output_dir()

    tester.is_reference_build = False
    created_results_path = tester.get_output_dir()

    if not os.path.exists(created_results_path):
        print(
            f"Skipping project '{project.name}', " f"it doesn't have newer results.",
            file=sys.stderr,
        )
        return

    build_log_path = SATestBuild.get_build_log_path(ref_results_path)
    build_log_dir = os.path.dirname(os.path.abspath(build_log_path))

    os.makedirs(build_log_dir)

    with open(build_log_path, "w+") as build_log_file:

        def run_cmd(command: str):
            if Verbose:
                print(f"Executing {command}")
            check_call(command, shell=True, stdout=build_log_file)

        # Remove reference results: in git, and then again for a good measure
        # with rm, as git might not remove things fully if there are empty
        # directories involved.
        if git:
            run_cmd(f"git rm -r -q '{ref_results_path}'")
        shutil.rmtree(ref_results_path)

        # Replace reference results with a freshly computed once.
        shutil.copytree(created_results_path, ref_results_path, symlinks=True)

        # Run cleanup script.
        SATestBuild.run_cleanup_script(project_dir, build_log_file)

        SATestBuild.normalize_reference_results(
            project_dir, ref_results_path, project.mode
        )

        # Clean up the generated difference results.
        SATestBuild.cleanup_reference_results(ref_results_path)

        if git:
            run_cmd(f"git add '{ref_results_path}'")


if __name__ == "__main__":
    print("SATestUpdateDiffs.py should not be used on its own.")
    print("Please use 'SATest.py update' instead")
    sys.exit(1)
