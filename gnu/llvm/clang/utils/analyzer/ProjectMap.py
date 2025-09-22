import json
import os

from enum import auto, Enum
from typing import Any, Dict, List, NamedTuple, Optional, Tuple


JSON = Dict[str, Any]


DEFAULT_MAP_FILE = "projects.json"


class DownloadType(str, Enum):
    GIT = "git"
    ZIP = "zip"
    SCRIPT = "script"


class Size(int, Enum):
    """
    Size of the project.

    Sizes do not directly correspond to the number of lines or files in the
    project.  The key factor that is important for the developers of the
    analyzer is the time it takes to analyze the project.  Here is how
    the following sizes map to times:

    TINY:  <1min
    SMALL: 1min-10min
    BIG:   10min-1h
    HUGE:  >1h

    The borders are a bit of a blur, especially because analysis time varies
    from one machine to another.  However, the relative times will stay pretty
    similar, and these groupings will still be helpful.

    UNSPECIFIED is a very special case, which is intentionally last in the list
    of possible sizes.  If the user wants to filter projects by one of the
    possible sizes, we want projects with UNSPECIFIED size to be filtered out
    for any given size.
    """

    TINY = auto()
    SMALL = auto()
    BIG = auto()
    HUGE = auto()
    UNSPECIFIED = auto()

    @staticmethod
    def from_str(raw_size: Optional[str]) -> "Size":
        """
        Construct a Size object from an optional string.

        :param raw_size: optional string representation of the desired Size
                         object.  None will produce UNSPECIFIED size.

        This method is case-insensitive, so raw sizes 'tiny', 'TINY', and
        'TiNy' will produce the same result.
        """
        if raw_size is None:
            return Size.UNSPECIFIED

        raw_size_upper = raw_size.upper()
        # The implementation is decoupled from the actual values of the enum,
        # so we can easily add or modify it without bothering about this
        # function.
        for possible_size in Size:
            if possible_size.name == raw_size_upper:
                return possible_size

        possible_sizes = [
            size.name.lower()
            for size in Size
            # no need in showing our users this size
            if size != Size.UNSPECIFIED
        ]
        raise ValueError(
            f"Incorrect project size '{raw_size}'. "
            f"Available sizes are {possible_sizes}"
        )


class ProjectInfo(NamedTuple):
    """
    Information about a project to analyze.
    """

    name: str
    mode: int
    source: DownloadType = DownloadType.SCRIPT
    origin: str = ""
    commit: str = ""
    enabled: bool = True
    size: Size = Size.UNSPECIFIED

    def with_fields(self, **kwargs) -> "ProjectInfo":
        """
        Create a copy of this project info with customized fields.
        NamedTuple is immutable and this is a way to create modified copies.

          info.enabled = True
          info.mode = 1

        can be done as follows:

          modified = info.with_fields(enbled=True, mode=1)
        """
        return ProjectInfo(**{**self._asdict(), **kwargs})


class ProjectMap:
    """
    Project map stores info about all the "registered" projects.
    """

    def __init__(self, path: Optional[str] = None, should_exist: bool = True):
        """
        :param path: optional path to a project JSON file, when None defaults
                     to DEFAULT_MAP_FILE.
        :param should_exist: flag to tell if it's an exceptional situation when
                             the project file doesn't exist, creates an empty
                             project list instead if we are not expecting it to
                             exist.
        """
        if path is None:
            path = os.path.join(os.path.abspath(os.curdir), DEFAULT_MAP_FILE)

        if not os.path.exists(path):
            if should_exist:
                raise ValueError(
                    f"Cannot find the project map file {path}"
                    f"\nRunning script for the wrong directory?\n"
                )
            else:
                self._create_empty(path)

        self.path = path
        self._load_projects()

    def save(self):
        """
        Save project map back to its original file.
        """
        self._save(self.projects, self.path)

    def _load_projects(self):
        with open(self.path) as raw_data:
            raw_projects = json.load(raw_data)

            if not isinstance(raw_projects, list):
                raise ValueError("Project map should be a list of JSON objects")

            self.projects = self._parse(raw_projects)

    @staticmethod
    def _parse(raw_projects: List[JSON]) -> List[ProjectInfo]:
        return [ProjectMap._parse_project(raw_project) for raw_project in raw_projects]

    @staticmethod
    def _parse_project(raw_project: JSON) -> ProjectInfo:
        try:
            name: str = raw_project["name"]
            build_mode: int = raw_project["mode"]
            enabled: bool = raw_project.get("enabled", True)
            source: DownloadType = raw_project.get("source", "zip")
            size = Size.from_str(raw_project.get("size", None))

            if source == DownloadType.GIT:
                origin, commit = ProjectMap._get_git_params(raw_project)
            else:
                origin, commit = "", ""

            return ProjectInfo(name, build_mode, source, origin, commit, enabled, size)

        except KeyError as e:
            raise ValueError(f"Project info is required to have a '{e.args[0]}' field")

    @staticmethod
    def _get_git_params(raw_project: JSON) -> Tuple[str, str]:
        try:
            return raw_project["origin"], raw_project["commit"]
        except KeyError as e:
            raise ValueError(
                f"Profect info is required to have a '{e.args[0]}' field "
                f"if it has a 'git' source"
            )

    @staticmethod
    def _create_empty(path: str):
        ProjectMap._save([], path)

    @staticmethod
    def _save(projects: List[ProjectInfo], path: str):
        with open(path, "w") as output:
            json.dump(ProjectMap._convert_infos_to_dicts(projects), output, indent=2)

    @staticmethod
    def _convert_infos_to_dicts(projects: List[ProjectInfo]) -> List[JSON]:
        return [ProjectMap._convert_info_to_dict(project) for project in projects]

    @staticmethod
    def _convert_info_to_dict(project: ProjectInfo) -> JSON:
        whole_dict = project._asdict()
        defaults = project._field_defaults

        # there is no need in serializing fields with default values
        for field, default_value in defaults.items():
            if whole_dict[field] == default_value:
                del whole_dict[field]

        return whole_dict
