#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
This script helps track the translation status of the documentation
in different locales, e.g., zh_CN. More specially, it uses `git log`
commit to find the latest english commit from the translation commit
(order by author date) and the latest english commits from HEAD. If
differences occur, report the file and commits that need to be updated.

The usage is as follows:
- ./scripts/checktransupdate.py -l zh_CN
This will print all the files that need to be updated or translated in the zh_CN locale.
- ./scripts/checktransupdate.py Documentation/translations/zh_CN/dev-tools/testing-overview.rst
This will only print the status of the specified file.

The output is something like:
Documentation/dev-tools/kfence.rst
No translation in the locale of zh_CN

Documentation/translations/zh_CN/dev-tools/testing-overview.rst
commit 42fb9cfd5b18 ("Documentation: dev-tools: Add link to RV docs")
1 commits needs resolving in total
"""

import os
import re
import time
import logging
from argparse import ArgumentParser, ArgumentTypeError, BooleanOptionalAction
from datetime import datetime


def get_origin_path(file_path):
    """Get the origin path from the translation path"""
    paths = file_path.split("/")
    tidx = paths.index("translations")
    opaths = paths[:tidx]
    opaths += paths[tidx + 2 :]
    return "/".join(opaths)


def get_latest_commit_from(file_path, commit):
    """Get the latest commit from the specified commit for the specified file"""
    command = f"git log --pretty=format:%H%n%aD%n%cD%n%n%B {commit} -1 -- {file_path}"
    logging.debug(command)
    pipe = os.popen(command)
    result = pipe.read()
    result = result.split("\n")
    if len(result) <= 1:
        return None

    logging.debug("Result: %s", result[0])

    return {
        "hash": result[0],
        "author_date": datetime.strptime(result[1], "%a, %d %b %Y %H:%M:%S %z"),
        "commit_date": datetime.strptime(result[2], "%a, %d %b %Y %H:%M:%S %z"),
        "message": result[4:],
    }


def get_origin_from_trans(origin_path, t_from_head):
    """Get the latest origin commit from the translation commit"""
    o_from_t = get_latest_commit_from(origin_path, t_from_head["hash"])
    while o_from_t is not None and o_from_t["author_date"] > t_from_head["author_date"]:
        o_from_t = get_latest_commit_from(origin_path, o_from_t["hash"] + "^")
    if o_from_t is not None:
        logging.debug("tracked origin commit id: %s", o_from_t["hash"])
    return o_from_t


def get_origin_from_trans_smartly(origin_path, t_from_head):
    """Get the latest origin commit from the formatted translation commit:
    (1) update to commit HASH (TITLE)
    (2) Update the translation through commit HASH (TITLE)
    """
    # catch flag for 12-bit commit hash
    HASH = r'([0-9a-f]{12})'
    # pattern 1: contains "update to commit HASH"
    pat_update_to = re.compile(rf'update to commit {HASH}')
    # pattern 2: contains "Update the translation through commit HASH"
    pat_update_translation = re.compile(rf'Update the translation through commit {HASH}')

    origin_commit_hash = None
    for line in t_from_head["message"]:
        # check if the line matches the first pattern
        match = pat_update_to.search(line)
        if match:
            origin_commit_hash = match.group(1)
            break
        # check if the line matches the second pattern
        match = pat_update_translation.search(line)
        if match:
            origin_commit_hash = match.group(1)
            break
    if origin_commit_hash is None:
        return None
    o_from_t = get_latest_commit_from(origin_path, origin_commit_hash)
    if o_from_t is not None:
        logging.debug("tracked origin commit id: %s", o_from_t["hash"])
    return o_from_t


def get_commits_count_between(opath, commit1, commit2):
    """Get the commits count between two commits for the specified file"""
    command = f"git log --pretty=format:%H {commit1}...{commit2} -- {opath}"
    logging.debug(command)
    pipe = os.popen(command)
    result = pipe.read().split("\n")
    # filter out empty lines
    result = list(filter(lambda x: x != "", result))
    return result


def pretty_output(commit):
    """Pretty print the commit message"""
    command = f"git log --pretty='format:%h (\"%s\")' -1 {commit}"
    logging.debug(command)
    pipe = os.popen(command)
    return pipe.read()


def valid_commit(commit):
    """Check if the commit is valid or not"""
    msg = pretty_output(commit)
    return "Merge tag" not in msg

def check_per_file(file_path):
    """Check the translation status for the specified file"""
    opath = get_origin_path(file_path)

    if not os.path.isfile(opath):
        logging.error("Cannot find the origin path for {file_path}")
        return

    o_from_head = get_latest_commit_from(opath, "HEAD")
    t_from_head = get_latest_commit_from(file_path, "HEAD")

    if o_from_head is None or t_from_head is None:
        logging.error("Cannot find the latest commit for %s", file_path)
        return

    o_from_t = get_origin_from_trans_smartly(opath, t_from_head)
    # notice, o_from_t from get_*_smartly() is always more accurate than from get_*()
    if o_from_t is None:
        o_from_t = get_origin_from_trans(opath, t_from_head)

    if o_from_t is None:
        logging.error("Error: Cannot find the latest origin commit for %s", file_path)
        return

    if o_from_head["hash"] == o_from_t["hash"]:
        logging.debug("No update needed for %s", file_path)
    else:
        logging.info(file_path)
        commits = get_commits_count_between(
            opath, o_from_t["hash"], o_from_head["hash"]
        )
        count = 0
        for commit in commits:
            if valid_commit(commit):
                logging.info("commit %s", pretty_output(commit))
                count += 1
        logging.info("%d commits needs resolving in total\n", count)


def valid_locales(locale):
    """Check if the locale is valid or not"""
    script_path = os.path.dirname(os.path.abspath(__file__))
    linux_path = os.path.join(script_path, "..")
    if not os.path.isdir(f"{linux_path}/Documentation/translations/{locale}"):
        raise ArgumentTypeError("Invalid locale: {locale}")
    return locale


def list_files_with_excluding_folders(folder, exclude_folders, include_suffix):
    """List all files with the specified suffix in the folder and its subfolders"""
    files = []
    stack = [folder]

    while stack:
        pwd = stack.pop()
        # filter out the exclude folders
        if os.path.basename(pwd) in exclude_folders:
            continue
        # list all files and folders
        for item in os.listdir(pwd):
            ab_item = os.path.join(pwd, item)
            if os.path.isdir(ab_item):
                stack.append(ab_item)
            else:
                if ab_item.endswith(include_suffix):
                    files.append(ab_item)

    return files


class DmesgFormatter(logging.Formatter):
    """Custom dmesg logging formatter"""
    def format(self, record):
        timestamp = time.time()
        formatted_time = f"[{timestamp:>10.6f}]"
        log_message = f"{formatted_time} {record.getMessage()}"
        return log_message


def config_logging(log_level, log_file="checktransupdate.log"):
    """configure logging based on the log level"""
    # set up the root logger
    logger = logging.getLogger()
    logger.setLevel(log_level)

    # Create console handler
    console_handler = logging.StreamHandler()
    console_handler.setLevel(log_level)

    # Create file handler
    file_handler = logging.FileHandler(log_file)
    file_handler.setLevel(log_level)

    # Create formatter and add it to the handlers
    formatter = DmesgFormatter()
    console_handler.setFormatter(formatter)
    file_handler.setFormatter(formatter)

    # Add the handler to the logger
    logger.addHandler(console_handler)
    logger.addHandler(file_handler)


def main():
    """Main function of the script"""
    script_path = os.path.dirname(os.path.abspath(__file__))
    linux_path = os.path.join(script_path, "..")

    parser = ArgumentParser(description="Check the translation update")
    parser.add_argument(
        "-l",
        "--locale",
        default="zh_CN",
        type=valid_locales,
        help="Locale to check when files are not specified",
    )

    parser.add_argument(
        "--print-missing-translations",
        action=BooleanOptionalAction,
        default=True,
        help="Print files that do not have translations",
    )

    parser.add_argument(
        '--log',
        default='INFO',
        choices=['DEBUG', 'INFO', 'WARNING', 'ERROR', 'CRITICAL'],
        help='Set the logging level')

    parser.add_argument(
        '--logfile',
        default='checktransupdate.log',
        help='Set the logging file (default: checktransupdate.log)')

    parser.add_argument(
        "files", nargs="*", help="Files to check, if not specified, check all files"
    )
    args = parser.parse_args()

    # Configure logging based on the --log argument
    log_level = getattr(logging, args.log.upper(), logging.INFO)
    config_logging(log_level)

    # Get files related to linux path
    files = args.files
    if len(files) == 0:
        offical_files = list_files_with_excluding_folders(
            os.path.join(linux_path, "Documentation"), ["translations", "output"], "rst"
        )

        for file in offical_files:
            # split the path into parts
            path_parts = file.split(os.sep)
            # find the index of the "Documentation" directory
            kindex = path_parts.index("Documentation")
            # insert the translations and locale after the Documentation directory
            new_path_parts = path_parts[:kindex + 1] + ["translations", args.locale] \
                           + path_parts[kindex + 1 :]
            # join the path parts back together
            new_file = os.sep.join(new_path_parts)
            if os.path.isfile(new_file):
                files.append(new_file)
            else:
                if args.print_missing_translations:
                    logging.info(os.path.relpath(os.path.abspath(file), linux_path))
                    logging.info("No translation in the locale of %s\n", args.locale)

    files = list(map(lambda x: os.path.relpath(os.path.abspath(x), linux_path), files))

    # cd to linux root directory
    os.chdir(linux_path)

    for file in files:
        check_per_file(file)


if __name__ == "__main__":
    main()
