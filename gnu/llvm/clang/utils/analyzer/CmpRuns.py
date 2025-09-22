#!/usr/bin/env python

"""
CmpRuns - A simple tool for comparing two static analyzer runs to determine
which reports have been added, removed, or changed.

This is designed to support automated testing using the static analyzer, from
two perspectives:
  1. To monitor changes in the static analyzer's reports on real code bases,
     for regression testing.

  2. For use by end users who want to integrate regular static analyzer testing
     into a buildbot like environment.

Usage:

    # Load the results of both runs, to obtain lists of the corresponding
    # AnalysisDiagnostic objects.
    #
    resultsA = load_results_from_single_run(singleRunInfoA, delete_empty)
    resultsB = load_results_from_single_run(singleRunInfoB, delete_empty)

    # Generate a relation from diagnostics in run A to diagnostics in run B
    # to obtain a list of triples (a, b, confidence).
    diff = compare_results(resultsA, resultsB)

"""
import json
import os
import plistlib
import re
import sys

from math import log
from collections import defaultdict
from copy import copy
from enum import Enum
from typing import (
    Any,
    DefaultDict,
    Dict,
    List,
    NamedTuple,
    Optional,
    Sequence,
    Set,
    TextIO,
    TypeVar,
    Tuple,
    Union,
)


Number = Union[int, float]
Stats = Dict[str, Dict[str, Number]]
Plist = Dict[str, Any]
JSON = Dict[str, Any]
# Diff in a form: field -> (before, after)
JSONDiff = Dict[str, Tuple[str, str]]
# Type for generics
T = TypeVar("T")

STATS_REGEXP = re.compile(r"Statistics: (\{.+\})", re.MULTILINE | re.DOTALL)


class Colors:
    """
    Color for terminal highlight.
    """

    RED = "\x1b[2;30;41m"
    GREEN = "\x1b[6;30;42m"
    CLEAR = "\x1b[0m"


class HistogramType(str, Enum):
    RELATIVE = "relative"
    LOG_RELATIVE = "log-relative"
    ABSOLUTE = "absolute"


class ResultsDirectory(NamedTuple):
    path: str
    root: str = ""


class SingleRunInfo:
    """
    Information about analysis run:
    path - the analysis output directory
    root - the name of the root directory, which will be disregarded when
    determining the source file name
    """

    def __init__(self, results: ResultsDirectory, verbose_log: Optional[str] = None):
        self.path = results.path
        self.root = results.root.rstrip("/\\")
        self.verbose_log = verbose_log


class AnalysisDiagnostic:
    def __init__(
        self, data: Plist, report: "AnalysisReport", html_report: Optional[str]
    ):
        self._data = data
        self._loc = self._data["location"]
        self._report = report
        self._html_report = html_report
        self._report_size = len(self._data["path"])

    def get_file_name(self) -> str:
        root = self._report.run.root
        file_name = self._report.files[self._loc["file"]]

        if file_name.startswith(root) and len(root) > 0:
            return file_name[len(root) + 1 :]

        return file_name

    def get_root_file_name(self) -> str:
        path = self._data["path"]

        if not path:
            return self.get_file_name()

        p = path[0]
        if "location" in p:
            file_index = p["location"]["file"]
        else:  # control edge
            file_index = path[0]["edges"][0]["start"][0]["file"]

        out = self._report.files[file_index]
        root = self._report.run.root

        if out.startswith(root):
            return out[len(root) :]

        return out

    def get_line(self) -> int:
        return self._loc["line"]

    def get_column(self) -> int:
        return self._loc["col"]

    def get_path_length(self) -> int:
        return self._report_size

    def get_category(self) -> str:
        return self._data["category"]

    def get_description(self) -> str:
        return self._data["description"]

    def get_location(self) -> str:
        return f"{self.get_file_name()}:{self.get_line()}:{self.get_column()}"

    def get_issue_identifier(self) -> str:
        id = self.get_file_name() + "+"

        if "issue_context" in self._data:
            id += self._data["issue_context"] + "+"

        if "issue_hash_content_of_line_in_context" in self._data:
            id += str(self._data["issue_hash_content_of_line_in_context"])

        return id

    def get_html_report(self) -> str:
        if self._html_report is None:
            return " "

        return os.path.join(self._report.run.path, self._html_report)

    def get_readable_name(self) -> str:
        if "issue_context" in self._data:
            funcname_postfix = "#" + self._data["issue_context"]
        else:
            funcname_postfix = ""

        root_filename = self.get_root_file_name()
        file_name = self.get_file_name()

        if root_filename != file_name:
            file_prefix = f"[{root_filename}] {file_name}"
        else:
            file_prefix = root_filename

        line = self.get_line()
        col = self.get_column()
        return (
            f"{file_prefix}{funcname_postfix}:{line}:{col}"
            f", {self.get_category()}: {self.get_description()}"
        )

    KEY_FIELDS = ["check_name", "category", "description"]

    def is_similar_to(self, other: "AnalysisDiagnostic") -> bool:
        # We consider two diagnostics similar only if at least one
        # of the key fields is the same in both diagnostics.
        return len(self.get_diffs(other)) != len(self.KEY_FIELDS)

    def get_diffs(self, other: "AnalysisDiagnostic") -> JSONDiff:
        return {
            field: (self._data[field], other._data[field])
            for field in self.KEY_FIELDS
            if self._data[field] != other._data[field]
        }

    # Note, the data format is not an API and may change from one analyzer
    # version to another.
    def get_raw_data(self) -> Plist:
        return self._data

    def __eq__(self, other: object) -> bool:
        return hash(self) == hash(other)

    def __ne__(self, other: object) -> bool:
        return hash(self) != hash(other)

    def __hash__(self) -> int:
        return hash(self.get_issue_identifier())


class AnalysisRun:
    def __init__(self, info: SingleRunInfo):
        self.path = info.path
        self.root = info.root
        self.info = info
        self.reports: List[AnalysisReport] = []
        # Cumulative list of all diagnostics from all the reports.
        self.diagnostics: List[AnalysisDiagnostic] = []
        self.clang_version: Optional[str] = None
        self.raw_stats: List[JSON] = []

    def get_clang_version(self) -> Optional[str]:
        return self.clang_version

    def read_single_file(self, path: str, delete_empty: bool):
        with open(path, "rb") as plist_file:
            data = plistlib.load(plist_file)

        if "statistics" in data:
            self.raw_stats.append(json.loads(data["statistics"]))
            data.pop("statistics")

        # We want to retrieve the clang version even if there are no
        # reports. Assume that all reports were created using the same
        # clang version (this is always true and is more efficient).
        if "clang_version" in data:
            if self.clang_version is None:
                self.clang_version = data.pop("clang_version")
            else:
                data.pop("clang_version")

        # Ignore/delete empty reports.
        if not data["files"]:
            if delete_empty:
                os.remove(path)
            return

        # Extract the HTML reports, if they exists.
        htmlFiles = []
        for d in data["diagnostics"]:
            if "HTMLDiagnostics_files" in d:
                # FIXME: Why is this named files, when does it have multiple
                # files?
                assert len(d["HTMLDiagnostics_files"]) == 1
                htmlFiles.append(d.pop("HTMLDiagnostics_files")[0])
            else:
                htmlFiles.append(None)

        report = AnalysisReport(self, data.pop("files"))
        # Python 3.10 offers zip(..., strict=True). The following assertion
        # mimics it.
        assert len(data["diagnostics"]) == len(htmlFiles)
        diagnostics = [
            AnalysisDiagnostic(d, report, h)
            for d, h in zip(data.pop("diagnostics"), htmlFiles)
        ]

        assert not data

        report.diagnostics.extend(diagnostics)
        self.reports.append(report)
        self.diagnostics.extend(diagnostics)


class AnalysisReport:
    def __init__(self, run: AnalysisRun, files: List[str]):
        self.run = run
        self.files = files
        self.diagnostics: List[AnalysisDiagnostic] = []


def load_results(
    results: ResultsDirectory,
    delete_empty: bool = True,
    verbose_log: Optional[str] = None,
) -> AnalysisRun:
    """
    Backwards compatibility API.
    """
    return load_results_from_single_run(
        SingleRunInfo(results, verbose_log), delete_empty
    )


def load_results_from_single_run(
    info: SingleRunInfo, delete_empty: bool = True
) -> AnalysisRun:
    """
    # Load results of the analyzes from a given output folder.
    # - info is the SingleRunInfo object
    # - delete_empty specifies if the empty plist files should be deleted

    """
    path = info.path
    run = AnalysisRun(info)

    if os.path.isfile(path):
        run.read_single_file(path, delete_empty)
    else:
        for dirpath, dirnames, filenames in os.walk(path):
            for f in filenames:
                if not f.endswith("plist"):
                    continue

                p = os.path.join(dirpath, f)
                run.read_single_file(p, delete_empty)

    return run


def cmp_analysis_diagnostic(d):
    return d.get_issue_identifier()


AnalysisDiagnosticPair = Tuple[AnalysisDiagnostic, AnalysisDiagnostic]


class ComparisonResult:
    def __init__(self):
        self.present_in_both: List[AnalysisDiagnostic] = []
        self.present_only_in_old: List[AnalysisDiagnostic] = []
        self.present_only_in_new: List[AnalysisDiagnostic] = []
        self.changed_between_new_and_old: List[AnalysisDiagnosticPair] = []

    def add_common(self, issue: AnalysisDiagnostic):
        self.present_in_both.append(issue)

    def add_removed(self, issue: AnalysisDiagnostic):
        self.present_only_in_old.append(issue)

    def add_added(self, issue: AnalysisDiagnostic):
        self.present_only_in_new.append(issue)

    def add_changed(self, old_issue: AnalysisDiagnostic, new_issue: AnalysisDiagnostic):
        self.changed_between_new_and_old.append((old_issue, new_issue))


GroupedDiagnostics = DefaultDict[str, List[AnalysisDiagnostic]]


def get_grouped_diagnostics(
    diagnostics: List[AnalysisDiagnostic],
) -> GroupedDiagnostics:
    result: GroupedDiagnostics = defaultdict(list)
    for diagnostic in diagnostics:
        result[diagnostic.get_location()].append(diagnostic)
    return result


def compare_results(
    results_old: AnalysisRun,
    results_new: AnalysisRun,
    histogram: Optional[HistogramType] = None,
) -> ComparisonResult:
    """
    compare_results - Generate a relation from diagnostics in run A to
    diagnostics in run B.

    The result is the relation as a list of triples (a, b) where
    each element {a,b} is None or a matching element from the respective run
    """

    res = ComparisonResult()

    # Map size_before -> size_after
    path_difference_data: List[float] = []

    diags_old = get_grouped_diagnostics(results_old.diagnostics)
    diags_new = get_grouped_diagnostics(results_new.diagnostics)

    locations_old = set(diags_old.keys())
    locations_new = set(diags_new.keys())

    common_locations = locations_old & locations_new

    for location in common_locations:
        old = diags_old[location]
        new = diags_new[location]

        # Quadratic algorithms in this part are fine because 'old' and 'new'
        # are most commonly of size 1.
        common: Set[AnalysisDiagnostic] = set()
        for a in old:
            for b in new:
                if a.get_issue_identifier() == b.get_issue_identifier():
                    a_path_len = a.get_path_length()
                    b_path_len = b.get_path_length()

                    if a_path_len != b_path_len:

                        if histogram == HistogramType.RELATIVE:
                            path_difference_data.append(float(a_path_len) / b_path_len)

                        elif histogram == HistogramType.LOG_RELATIVE:
                            path_difference_data.append(
                                log(float(a_path_len) / b_path_len)
                            )

                        elif histogram == HistogramType.ABSOLUTE:
                            path_difference_data.append(a_path_len - b_path_len)

                    res.add_common(b)
                    common.add(a)

        old = filter_issues(old, common)
        new = filter_issues(new, common)
        common = set()

        for a in old:
            for b in new:
                if a.is_similar_to(b):
                    res.add_changed(a, b)
                    common.add(a)
                    common.add(b)

        old = filter_issues(old, common)
        new = filter_issues(new, common)

        # Whatever is left in 'old' doesn't have a corresponding diagnostic
        # in 'new', so we need to mark it as 'removed'.
        for a in old:
            res.add_removed(a)

        # Whatever is left in 'new' doesn't have a corresponding diagnostic
        # in 'old', so we need to mark it as 'added'.
        for b in new:
            res.add_added(b)

    only_old_locations = locations_old - common_locations
    for location in only_old_locations:
        for a in diags_old[location]:
            # These locations have been found only in the old build, so we
            # need to mark all of therm as 'removed'
            res.add_removed(a)

    only_new_locations = locations_new - common_locations
    for location in only_new_locations:
        for b in diags_new[location]:
            # These locations have been found only in the new build, so we
            # need to mark all of therm as 'added'
            res.add_added(b)

    # FIXME: Add fuzzy matching. One simple and possible effective idea would
    # be to bin the diagnostics, print them in a normalized form (based solely
    # on the structure of the diagnostic), compute the diff, then use that as
    # the basis for matching. This has the nice property that we don't depend
    # in any way on the diagnostic format.

    if histogram:
        from matplotlib import pyplot

        pyplot.hist(path_difference_data, bins=100)
        pyplot.show()

    return res


def filter_issues(
    origin: List[AnalysisDiagnostic], to_remove: Set[AnalysisDiagnostic]
) -> List[AnalysisDiagnostic]:
    return [diag for diag in origin if diag not in to_remove]


def compute_percentile(values: Sequence[T], percentile: float) -> T:
    """
    Return computed percentile.
    """
    return sorted(values)[int(round(percentile * len(values) + 0.5)) - 1]


def derive_stats(results: AnalysisRun) -> Stats:
    # Assume all keys are the same in each statistics bucket.
    combined_data = defaultdict(list)

    # Collect data on paths length.
    for report in results.reports:
        for diagnostic in report.diagnostics:
            combined_data["PathsLength"].append(diagnostic.get_path_length())

    for stat in results.raw_stats:
        for key, value in stat.items():
            combined_data[str(key)].append(value)

    combined_stats: Stats = {}

    for key, values in combined_data.items():
        combined_stats[key] = {
            "max": max(values),
            "min": min(values),
            "mean": sum(values) / len(values),
            "90th %tile": compute_percentile(values, 0.9),
            "95th %tile": compute_percentile(values, 0.95),
            "median": sorted(values)[len(values) // 2],
            "total": sum(values),
        }

    return combined_stats


# TODO: compare_results decouples comparison from the output, we should
#       do it here as well
def compare_stats(
    results_old: AnalysisRun, results_new: AnalysisRun, out: TextIO = sys.stdout
):
    stats_old = derive_stats(results_old)
    stats_new = derive_stats(results_new)

    old_keys = set(stats_old.keys())
    new_keys = set(stats_new.keys())
    keys = sorted(old_keys & new_keys)

    for key in keys:
        out.write(f"{key}\n")

        nested_keys = sorted(set(stats_old[key]) & set(stats_new[key]))

        for nested_key in nested_keys:
            val_old = float(stats_old[key][nested_key])
            val_new = float(stats_new[key][nested_key])

            report = f"{val_old:.3f} -> {val_new:.3f}"

            # Only apply highlighting when writing to TTY and it's not Windows
            if out.isatty() and os.name != "nt":
                if val_new != 0:
                    ratio = (val_new - val_old) / val_new
                    if ratio < -0.2:
                        report = Colors.GREEN + report + Colors.CLEAR
                    elif ratio > 0.2:
                        report = Colors.RED + report + Colors.CLEAR

            out.write(f"\t {nested_key} {report}\n")

    removed_keys = old_keys - new_keys
    if removed_keys:
        out.write(f"REMOVED statistics: {removed_keys}\n")

    added_keys = new_keys - old_keys
    if added_keys:
        out.write(f"ADDED statistics: {added_keys}\n")

    out.write("\n")


def dump_scan_build_results_diff(
    dir_old: ResultsDirectory,
    dir_new: ResultsDirectory,
    delete_empty: bool = True,
    out: TextIO = sys.stdout,
    show_stats: bool = False,
    stats_only: bool = False,
    histogram: Optional[HistogramType] = None,
    verbose_log: Optional[str] = None,
):
    """
    Compare directories with analysis results and dump results.

    :param delete_empty: delete empty plist files
    :param out: buffer to dump comparison results to.
    :param show_stats: compare execution stats as well.
    :param stats_only: compare ONLY execution stats.
    :param histogram: optional histogram type to plot path differences.
    :param verbose_log: optional path to an additional log file.
    """
    results_old = load_results(dir_old, delete_empty, verbose_log)
    results_new = load_results(dir_new, delete_empty, verbose_log)

    if show_stats or stats_only:
        compare_stats(results_old, results_new)
    if stats_only:
        return

    # Open the verbose log, if given.
    if verbose_log:
        aux_log: Optional[TextIO] = open(verbose_log, "w")
    else:
        aux_log = None

    diff = compare_results(results_old, results_new, histogram)
    found_diffs = 0
    total_added = 0
    total_removed = 0
    total_modified = 0

    for new in diff.present_only_in_new:
        out.write(f"ADDED: {new.get_readable_name()}\n\n")
        found_diffs += 1
        total_added += 1
        if aux_log:
            aux_log.write(
                f"('ADDED', {new.get_readable_name()}, " f"{new.get_html_report()})\n"
            )

    for old in diff.present_only_in_old:
        out.write(f"REMOVED: {old.get_readable_name()}\n\n")
        found_diffs += 1
        total_removed += 1
        if aux_log:
            aux_log.write(
                f"('REMOVED', {old.get_readable_name()}, " f"{old.get_html_report()})\n"
            )

    for old, new in diff.changed_between_new_and_old:
        out.write(f"MODIFIED: {old.get_readable_name()}\n")
        found_diffs += 1
        total_modified += 1
        diffs = old.get_diffs(new)
        str_diffs = [
            f"          '{key}' changed: " f"'{old_value}' -> '{new_value}'"
            for key, (old_value, new_value) in diffs.items()
        ]
        out.write(",\n".join(str_diffs) + "\n\n")
        if aux_log:
            aux_log.write(
                f"('MODIFIED', {old.get_readable_name()}, "
                f"{old.get_html_report()})\n"
            )

    total_reports = len(results_new.diagnostics)
    out.write(f"TOTAL REPORTS: {total_reports}\n")
    out.write(f"TOTAL ADDED: {total_added}\n")
    out.write(f"TOTAL REMOVED: {total_removed}\n")
    out.write(f"TOTAL MODIFIED: {total_modified}\n")

    if aux_log:
        aux_log.write(f"('TOTAL NEW REPORTS', {total_reports})\n")
        aux_log.write(f"('TOTAL DIFFERENCES', {found_diffs})\n")
        aux_log.close()

    # TODO: change to NamedTuple
    return found_diffs, len(results_old.diagnostics), len(results_new.diagnostics)


if __name__ == "__main__":
    print("CmpRuns.py should not be used on its own.")
    print("Please use 'SATest.py compare' instead")
    sys.exit(1)
