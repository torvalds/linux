"""
Static Analyzer qualification infrastructure.

This source file contains all the functionality related to benchmarking
the analyzer on a set projects.  Right now, this includes measuring
execution time and peak memory usage.  Benchmark runs analysis on every
project multiple times to get a better picture about the distribution
of measured values.

Additionally, this file includes a comparison routine for two benchmarking
results that plots the result together on one chart.
"""

import SATestUtils as utils
from SATestBuild import ProjectTester, stdout, TestInfo
from ProjectMap import ProjectInfo

import pandas as pd
from typing import List, Tuple


INDEX_COLUMN = "index"


def _save(data: pd.DataFrame, file_path: str):
    data.to_csv(file_path, index_label=INDEX_COLUMN)


def _load(file_path: str) -> pd.DataFrame:
    return pd.read_csv(file_path, index_col=INDEX_COLUMN)


class Benchmark:
    """
    Becnhmark class encapsulates one functionality: it runs the analysis
    multiple times for the given set of projects and stores results in the
    specified file.
    """

    def __init__(self, projects: List[ProjectInfo], iterations: int, output_path: str):
        self.projects = projects
        self.iterations = iterations
        self.out = output_path

    def run(self):
        results = [self._benchmark_project(project) for project in self.projects]

        data = pd.concat(results, ignore_index=True)
        _save(data, self.out)

    def _benchmark_project(self, project: ProjectInfo) -> pd.DataFrame:
        if not project.enabled:
            stdout(f" \n\n--- Skipping disabled project {project.name}\n")
            return

        stdout(f" \n\n--- Benchmarking project {project.name}\n")

        test_info = TestInfo(project)
        tester = ProjectTester(test_info, silent=True)
        project_dir = tester.get_project_dir()
        output_dir = tester.get_output_dir()

        raw_data = []

        for i in range(self.iterations):
            stdout(f"Iteration #{i + 1}")
            time, mem = tester.build(project_dir, output_dir)
            raw_data.append(
                {"time": time, "memory": mem, "iteration": i, "project": project.name}
            )
            stdout(
                f"time: {utils.time_to_str(time)}, "
                f"peak memory: {utils.memory_to_str(mem)}"
            )

        return pd.DataFrame(raw_data)


def compare(old_path: str, new_path: str, plot_file: str):
    """
    Compare two benchmarking results stored as .csv files
    and produce a plot in the specified file.
    """
    old = _load(old_path)
    new = _load(new_path)

    old_projects = set(old["project"])
    new_projects = set(new["project"])
    common_projects = old_projects & new_projects

    # Leave only rows for projects common to both dataframes.
    old = old[old["project"].isin(common_projects)]
    new = new[new["project"].isin(common_projects)]

    old, new = _normalize(old, new)

    # Seaborn prefers all the data to be in one dataframe.
    old["kind"] = "old"
    new["kind"] = "new"
    data = pd.concat([old, new], ignore_index=True)

    # TODO: compare data in old and new dataframes using statistical tests
    #       to check if they belong to the same distribution
    _plot(data, plot_file)


def _normalize(
    old: pd.DataFrame, new: pd.DataFrame
) -> Tuple[pd.DataFrame, pd.DataFrame]:
    # This creates a dataframe with all numerical data averaged.
    means = old.groupby("project").mean()
    return _normalize_impl(old, means), _normalize_impl(new, means)


def _normalize_impl(data: pd.DataFrame, means: pd.DataFrame):
    # Right now 'means' has one row corresponding to one project,
    # while 'data' has N rows for each project (one for each iteration).
    #
    # In order for us to work easier with this data, we duplicate
    # 'means' data to match the size of the 'data' dataframe.
    #
    # All the columns from 'data' will maintain their names, while
    # new columns coming from 'means' will have "_mean" suffix.
    joined_data = data.merge(means, on="project", suffixes=("", "_mean"))
    _normalize_key(joined_data, "time")
    _normalize_key(joined_data, "memory")
    return joined_data


def _normalize_key(data: pd.DataFrame, key: str):
    norm_key = _normalized_name(key)
    mean_key = f"{key}_mean"
    data[norm_key] = data[key] / data[mean_key]


def _normalized_name(name: str) -> str:
    return f"normalized {name}"


def _plot(data: pd.DataFrame, plot_file: str):
    import matplotlib
    import seaborn as sns
    from matplotlib import pyplot as plt

    sns.set_style("whitegrid")
    # We want to have time and memory charts one above the other.
    figure, (ax1, ax2) = plt.subplots(2, 1, figsize=(8, 6))

    def _subplot(key: str, ax: matplotlib.axes.Axes):
        sns.boxplot(
            x="project",
            y=_normalized_name(key),
            hue="kind",
            data=data,
            palette=sns.color_palette("BrBG", 2),
            ax=ax,
        )

    _subplot("time", ax1)
    # No need to have xlabels on both top and bottom charts.
    ax1.set_xlabel("")

    _subplot("memory", ax2)
    # The legend on the top chart is enough.
    ax2.get_legend().remove()

    figure.savefig(plot_file)
