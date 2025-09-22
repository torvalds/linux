#!/usr/bin/env python

import argparse
import sys
import os

from subprocess import call

SCRIPTS_DIR = os.path.dirname(os.path.realpath(__file__))
PROJECTS_DIR = os.path.join(SCRIPTS_DIR, "projects")
DEFAULT_LLVM_DIR = os.path.realpath(
    os.path.join(SCRIPTS_DIR, os.path.pardir, os.path.pardir, os.path.pardir)
)


def add(parser, args):
    import SATestAdd
    from ProjectMap import ProjectInfo

    if args.source == "git" and (args.origin == "" or args.commit == ""):
        parser.error("Please provide both --origin and --commit if source is 'git'")

    if args.source != "git" and (args.origin != "" or args.commit != ""):
        parser.error(
            "Options --origin and --commit don't make sense when " "source is not 'git'"
        )

    project = ProjectInfo(
        args.name[0], args.mode, args.source, args.origin, args.commit
    )

    SATestAdd.add_new_project(project)


def build(parser, args):
    import SATestBuild

    SATestBuild.VERBOSE = args.verbose

    projects = get_projects(parser, args)
    tester = SATestBuild.RegressionTester(
        args.jobs,
        projects,
        args.override_compiler,
        args.extra_analyzer_config,
        args.extra_checkers,
        args.regenerate,
        args.strictness,
    )
    tests_passed = tester.test_all()

    if not tests_passed:
        sys.stderr.write("ERROR: Tests failed.\n")
        sys.exit(42)


def compare(parser, args):
    import CmpRuns

    choices = [
        CmpRuns.HistogramType.RELATIVE.value,
        CmpRuns.HistogramType.LOG_RELATIVE.value,
        CmpRuns.HistogramType.ABSOLUTE.value,
    ]

    if args.histogram is not None and args.histogram not in choices:
        parser.error(
            "Incorrect histogram type, available choices are {}".format(choices)
        )

    dir_old = CmpRuns.ResultsDirectory(args.old[0], args.root_old)
    dir_new = CmpRuns.ResultsDirectory(args.new[0], args.root_new)

    CmpRuns.dump_scan_build_results_diff(
        dir_old,
        dir_new,
        show_stats=args.show_stats,
        stats_only=args.stats_only,
        histogram=args.histogram,
        verbose_log=args.verbose_log,
    )


def update(parser, args):
    import SATestUpdateDiffs
    from ProjectMap import ProjectMap

    project_map = ProjectMap()
    for project in project_map.projects:
        SATestUpdateDiffs.update_reference_results(project, args.git)


def benchmark(parser, args):
    from SATestBenchmark import Benchmark

    projects = get_projects(parser, args)
    benchmark = Benchmark(projects, args.iterations, args.output)
    benchmark.run()


def benchmark_compare(parser, args):
    import SATestBenchmark

    SATestBenchmark.compare(args.old, args.new, args.output)


def get_projects(parser, args):
    from ProjectMap import ProjectMap, Size

    project_map = ProjectMap()
    projects = project_map.projects

    def filter_projects(projects, predicate, force=False):
        return [
            project.with_fields(
                enabled=(force or project.enabled) and predicate(project)
            )
            for project in projects
        ]

    if args.projects:
        projects_arg = args.projects.split(",")
        available_projects = [project.name for project in projects]

        # validate that given projects are present in the project map file
        for manual_project in projects_arg:
            if manual_project not in available_projects:
                parser.error(
                    "Project '{project}' is not found in "
                    "the project map file. Available projects are "
                    "{all}.".format(project=manual_project, all=available_projects)
                )

        projects = filter_projects(
            projects, lambda project: project.name in projects_arg, force=True
        )

    try:
        max_size = Size.from_str(args.max_size)
    except ValueError as e:
        parser.error("{}".format(e))

    projects = filter_projects(projects, lambda project: project.size <= max_size)

    return projects


def docker(parser, args):
    if len(args.rest) > 0:
        if args.rest[0] != "--":
            parser.error("REST arguments should start with '--'")
        args.rest = args.rest[1:]

    if args.build_image:
        docker_build_image()
    elif args.shell:
        docker_shell(args)
    else:
        sys.exit(docker_run(args, " ".join(args.rest)))


def docker_build_image():
    sys.exit(call("docker build --tag satest-image {}".format(SCRIPTS_DIR), shell=True))


def docker_shell(args):
    try:
        # First we need to start the docker container in a waiting mode,
        # so it doesn't do anything, but most importantly keeps working
        # while the shell session is in progress.
        docker_run(args, "--wait", "--detach")
        # Since the docker container is running, we can actually connect to it
        call("docker exec -it satest bash", shell=True)

    except KeyboardInterrupt:
        pass

    finally:
        docker_cleanup()


def docker_run(args, command, docker_args=""):
    try:
        return call(
            "docker run --rm --name satest "
            "-v {llvm}:/llvm-project "
            "-v {build}:/build "
            "-v {clang}:/analyzer "
            "-v {scripts}:/scripts "
            "-v {projects}:/projects "
            "{docker_args} "
            "satest-image:latest {command}".format(
                llvm=args.llvm_project_dir,
                build=args.build_dir,
                clang=args.clang_dir,
                scripts=SCRIPTS_DIR,
                projects=PROJECTS_DIR,
                docker_args=docker_args,
                command=command,
            ),
            shell=True,
        )

    except KeyboardInterrupt:
        docker_cleanup()


def docker_cleanup():
    print("Please wait for docker to clean up")
    call("docker stop satest", shell=True)


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers()

    # add subcommand
    add_parser = subparsers.add_parser(
        "add", help="Add a new project for the analyzer testing."
    )
    # TODO: Add an option not to build.
    # TODO: Set the path to the Repository directory.
    add_parser.add_argument("name", nargs=1, help="Name of the new project")
    add_parser.add_argument(
        "--mode",
        action="store",
        default=1,
        type=int,
        choices=[0, 1, 2],
        help="Build mode: 0 for single file project, "
        "1 for scan_build, "
        "2 for single file c++11 project",
    )
    add_parser.add_argument(
        "--source",
        action="store",
        default="script",
        choices=["script", "git", "zip"],
        help="Source type of the new project: "
        "'git' for getting from git "
        "(please provide --origin and --commit), "
        "'zip' for unpacking source from a zip file, "
        "'script' for downloading source by running "
        "a custom script",
    )
    add_parser.add_argument(
        "--origin", action="store", default="", help="Origin link for a git repository"
    )
    add_parser.add_argument(
        "--commit", action="store", default="", help="Git hash for a commit to checkout"
    )
    add_parser.set_defaults(func=add)

    # build subcommand
    build_parser = subparsers.add_parser(
        "build",
        help="Build projects from the project map and compare results with "
        "the reference.",
    )
    build_parser.add_argument(
        "--strictness",
        dest="strictness",
        type=int,
        default=0,
        help="0 to fail on runtime errors, 1 to fail "
        "when the number of found bugs are different "
        "from the reference, 2 to fail on any "
        "difference from the reference. Default is 0.",
    )
    build_parser.add_argument(
        "-r",
        dest="regenerate",
        action="store_true",
        default=False,
        help="Regenerate reference output.",
    )
    build_parser.add_argument(
        "--override-compiler",
        action="store_true",
        default=False,
        help="Call scan-build with " "--override-compiler option.",
    )
    build_parser.add_argument(
        "-j",
        "--jobs",
        dest="jobs",
        type=int,
        default=0,
        help="Number of projects to test concurrently",
    )
    build_parser.add_argument(
        "--extra-analyzer-config",
        dest="extra_analyzer_config",
        type=str,
        default="",
        help="Arguments passed to to -analyzer-config",
    )
    build_parser.add_argument(
        "--extra-checkers",
        dest="extra_checkers",
        type=str,
        default="",
        help="Extra checkers to enable",
    )
    build_parser.add_argument(
        "--projects",
        action="store",
        default="",
        help="Comma-separated list of projects to test",
    )
    build_parser.add_argument(
        "--max-size",
        action="store",
        default=None,
        help="Maximum size for the projects to test",
    )
    build_parser.add_argument("-v", "--verbose", action="count", default=0)
    build_parser.set_defaults(func=build)

    # compare subcommand
    cmp_parser = subparsers.add_parser(
        "compare",
        help="Comparing two static analyzer runs in terms of "
        "reported warnings and execution time statistics.",
    )
    cmp_parser.add_argument(
        "--root-old",
        dest="root_old",
        help="Prefix to ignore on source files for " "OLD directory",
        action="store",
        type=str,
        default="",
    )
    cmp_parser.add_argument(
        "--root-new",
        dest="root_new",
        help="Prefix to ignore on source files for " "NEW directory",
        action="store",
        type=str,
        default="",
    )
    cmp_parser.add_argument(
        "--verbose-log",
        dest="verbose_log",
        help="Write additional information to LOG " "[default=None]",
        action="store",
        type=str,
        default=None,
        metavar="LOG",
    )
    cmp_parser.add_argument(
        "--stats-only",
        action="store_true",
        dest="stats_only",
        default=False,
        help="Only show statistics on reports",
    )
    cmp_parser.add_argument(
        "--show-stats",
        action="store_true",
        dest="show_stats",
        default=False,
        help="Show change in statistics",
    )
    cmp_parser.add_argument(
        "--histogram",
        action="store",
        default=None,
        help="Show histogram of paths differences. " "Requires matplotlib",
    )
    cmp_parser.add_argument("old", nargs=1, help="Directory with old results")
    cmp_parser.add_argument("new", nargs=1, help="Directory with new results")
    cmp_parser.set_defaults(func=compare)

    # update subcommand
    upd_parser = subparsers.add_parser(
        "update",
        help="Update static analyzer reference results based on the previous "
        "run of SATest build. Assumes that SATest build was just run.",
    )
    upd_parser.add_argument(
        "--git", action="store_true", help="Stage updated results using git."
    )
    upd_parser.set_defaults(func=update)

    # docker subcommand
    dock_parser = subparsers.add_parser(
        "docker", help="Run regression system in the docker."
    )

    dock_parser.add_argument(
        "--build-image",
        action="store_true",
        help="Build docker image for running tests.",
    )
    dock_parser.add_argument(
        "--shell", action="store_true", help="Start a shell on docker."
    )
    dock_parser.add_argument(
        "--llvm-project-dir",
        action="store",
        default=DEFAULT_LLVM_DIR,
        help="Path to LLVM source code. Defaults "
        "to the repo where this script is located. ",
    )
    dock_parser.add_argument(
        "--build-dir",
        action="store",
        default="",
        help="Path to a directory where docker should " "build LLVM code.",
    )
    dock_parser.add_argument(
        "--clang-dir",
        action="store",
        default="",
        help="Path to find/install LLVM installation.",
    )
    dock_parser.add_argument(
        "rest",
        nargs=argparse.REMAINDER,
        default=[],
        help="Additional args that will be forwarded " "to the docker's entrypoint.",
    )
    dock_parser.set_defaults(func=docker)

    # benchmark subcommand
    bench_parser = subparsers.add_parser(
        "benchmark", help="Run benchmarks by building a set of projects multiple times."
    )

    bench_parser.add_argument(
        "-i",
        "--iterations",
        action="store",
        type=int,
        default=20,
        help="Number of iterations for building each " "project.",
    )
    bench_parser.add_argument(
        "-o",
        "--output",
        action="store",
        default="benchmark.csv",
        help="Output csv file for the benchmark results",
    )
    bench_parser.add_argument(
        "--projects",
        action="store",
        default="",
        help="Comma-separated list of projects to test",
    )
    bench_parser.add_argument(
        "--max-size",
        action="store",
        default=None,
        help="Maximum size for the projects to test",
    )
    bench_parser.set_defaults(func=benchmark)

    bench_subparsers = bench_parser.add_subparsers()
    bench_compare_parser = bench_subparsers.add_parser(
        "compare", help="Compare benchmark runs."
    )
    bench_compare_parser.add_argument(
        "--old",
        action="store",
        required=True,
        help="Benchmark reference results to " "compare agains.",
    )
    bench_compare_parser.add_argument(
        "--new", action="store", required=True, help="New benchmark results to check."
    )
    bench_compare_parser.add_argument(
        "-o", "--output", action="store", required=True, help="Output file for plots."
    )
    bench_compare_parser.set_defaults(func=benchmark_compare)

    args = parser.parse_args()
    args.func(parser, args)


if __name__ == "__main__":
    main()
