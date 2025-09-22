#!/usr/bin/env python3
#
# ======- github-automation - LLVM GitHub Automation Routines--*- python -*--==#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ==-------------------------------------------------------------------------==#

import argparse
from git import Repo  # type: ignore
import html
import json
import github
import os
import re
import requests
import sys
import time
from typing import List, Optional

beginner_comment = """
Hi!

This issue may be a good introductory issue for people new to working on LLVM. If you would like to work on this issue, your first steps are:

1. Check that no other contributor has already been assigned to this issue. If you believe that no one is actually working on it despite an assignment, ping the person. After one week without a response, the assignee may be changed.
1. In the comments of this issue, request for it to be assigned to you, or just create a [pull request](https://github.com/llvm/llvm-project/pulls) after following the steps below. [Mention](https://docs.github.com/en/issues/tracking-your-work-with-issues/linking-a-pull-request-to-an-issue) this issue in the description of the pull request.
1. Fix the issue locally.
1. [Run the test suite](https://llvm.org/docs/TestingGuide.html#unit-and-regression-tests) locally. Remember that the subdirectories under `test/` create fine-grained testing targets, so you can e.g. use `make check-clang-ast` to only run Clang's AST tests.
1. Create a Git commit.
1. Run [`git clang-format HEAD~1`](https://clang.llvm.org/docs/ClangFormat.html#git-integration) to format your changes.
1. Open a [pull request](https://github.com/llvm/llvm-project/pulls) to the [upstream repository](https://github.com/llvm/llvm-project) on GitHub. Detailed instructions can be found [in GitHub's documentation](https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/creating-a-pull-request). [Mention](https://docs.github.com/en/issues/tracking-your-work-with-issues/linking-a-pull-request-to-an-issue) this issue in the description of the pull request.

If you have any further questions about this issue, don't hesitate to ask via a comment in the thread below.
"""


def _get_current_team(team_name, teams) -> Optional[github.Team.Team]:
    for team in teams:
        if team_name == team.name.lower():
            return team
    return None


def escape_description(str):
    # If the description of an issue/pull request is empty, the Github API
    # library returns None instead of an empty string. Handle this here to
    # avoid failures from trying to manipulate None.
    if str is None:
        return ""
    # https://github.com/github/markup/issues/1168#issuecomment-494946168
    str = html.escape(str, False)
    # '@' followed by alphanum is a user name
    str = re.sub("@(?=\w)", "@<!-- -->", str)
    # '#' followed by digits is considered an issue number
    str = re.sub("#(?=\d)", "#<!-- -->", str)
    return str


class IssueSubscriber:
    @property
    def team_name(self) -> str:
        return self._team_name

    def __init__(self, token: str, repo: str, issue_number: int, label_name: str):
        self.repo = github.Github(token).get_repo(repo)
        self.org = github.Github(token).get_organization(self.repo.organization.login)
        self.issue = self.repo.get_issue(issue_number)
        self._team_name = "issue-subscribers-{}".format(label_name).lower()

    def run(self) -> bool:
        team = _get_current_team(self.team_name, self.org.get_teams())
        if not team:
            print(f"couldn't find team named {self.team_name}")
            return False

        comment = ""
        if team.slug == "issue-subscribers-good-first-issue":
            comment = "{}\n".format(beginner_comment)
            self.issue.create_comment(comment)

        body = escape_description(self.issue.body)
        comment = f"""
@llvm/{team.slug}

Author: {self.issue.user.name} ({self.issue.user.login})

<details>
{body}
</details>
"""

        self.issue.create_comment(comment)
        return True


def human_readable_size(size, decimal_places=2):
    for unit in ["B", "KiB", "MiB", "GiB", "TiB", "PiB"]:
        if size < 1024.0 or unit == "PiB":
            break
        size /= 1024.0
    return f"{size:.{decimal_places}f} {unit}"


class PRSubscriber:
    @property
    def team_name(self) -> str:
        return self._team_name

    def __init__(self, token: str, repo: str, pr_number: int, label_name: str):
        self.repo = github.Github(token).get_repo(repo)
        self.org = github.Github(token).get_organization(self.repo.organization.login)
        self.pr = self.repo.get_issue(pr_number).as_pull_request()
        self._team_name = "pr-subscribers-{}".format(
            label_name.replace("+", "x")
        ).lower()
        self.COMMENT_TAG = "<!--LLVM PR SUMMARY COMMENT-->\n"

    def get_summary_comment(self) -> github.IssueComment.IssueComment:
        for comment in self.pr.as_issue().get_comments():
            if self.COMMENT_TAG in comment.body:
                return comment
        return None

    def run(self) -> bool:
        patch = None
        team = _get_current_team(self.team_name, self.org.get_teams())
        if not team:
            print(f"couldn't find team named {self.team_name}")
            return False

        # GitHub limits comments to 65,536 characters, let's limit the diff
        # and the file list to 20kB each.
        STAT_LIMIT = 20 * 1024
        DIFF_LIMIT = 20 * 1024

        # Get statistics for each file
        diff_stats = f"{self.pr.changed_files} Files Affected:\n\n"
        for file in self.pr.get_files():
            diff_stats += f"- ({file.status}) {file.filename} ("
            if file.additions:
                diff_stats += f"+{file.additions}"
            if file.deletions:
                diff_stats += f"-{file.deletions}"
            diff_stats += ") "
            if file.status == "renamed":
                print(f"(from {file.previous_filename})")
            diff_stats += "\n"
            if len(diff_stats) > STAT_LIMIT:
                break

        # Get the diff
        try:
            patch = requests.get(self.pr.diff_url).text
        except:
            patch = ""

        patch_link = f"Full diff: {self.pr.diff_url}\n"
        if len(patch) > DIFF_LIMIT:
            patch_link = f"\nPatch is {human_readable_size(len(patch))}, truncated to {human_readable_size(DIFF_LIMIT)} below, full version: {self.pr.diff_url}\n"
            patch = patch[0:DIFF_LIMIT] + "...\n[truncated]\n"
        team_mention = "@llvm/{}".format(team.slug)

        body = escape_description(self.pr.body)
        # Note: the comment is in markdown and the code below
        # is sensible to line break
        comment = f"""
{self.COMMENT_TAG}
{team_mention}

Author: {self.pr.user.name} ({self.pr.user.login})

<details>
<summary>Changes</summary>

{body}

---
{patch_link}

{diff_stats}

``````````diff
{patch}
``````````

</details>
"""

        summary_comment = self.get_summary_comment()
        if not summary_comment:
            self.pr.as_issue().create_comment(comment)
        elif team_mention + "\n" in summary_comment.body:
            print("Team {} already mentioned.".format(team.slug))
        else:
            summary_comment.edit(
                summary_comment.body.replace(
                    self.COMMENT_TAG, self.COMMENT_TAG + team_mention + "\n"
                )
            )
        return True

    def _get_current_team(self) -> Optional[github.Team.Team]:
        for team in self.org.get_teams():
            if self.team_name == team.name.lower():
                return team
        return None


class PRGreeter:
    COMMENT_TAG = "<!--LLVM NEW CONTRIBUTOR COMMENT-->\n"

    def __init__(self, token: str, repo: str, pr_number: int):
        repo = github.Github(token).get_repo(repo)
        self.pr = repo.get_issue(pr_number).as_pull_request()

    def run(self) -> bool:
        # We assume that this is only called for a PR that has just been opened
        # by a user new to LLVM and/or GitHub itself.

        # This text is using Markdown formatting.

        comment = f"""\
{PRGreeter.COMMENT_TAG}
Thank you for submitting a Pull Request (PR) to the LLVM Project!

This PR will be automatically labeled and the relevant teams will be
notified.

If you wish to, you can add reviewers by using the "Reviewers" section on this page.

If this is not working for you, it is probably because you do not have write
permissions for the repository. In which case you can instead tag reviewers by
name in a comment by using `@` followed by their GitHub username.

If you have received no comments on your PR for a week, you can request a review
by "ping"ing the PR by adding a comment “Ping”. The common courtesy "ping" rate
is once a week. Please remember that you are asking for valuable time from other developers.

If you have further questions, they may be answered by the [LLVM GitHub User Guide](https://llvm.org/docs/GitHub.html).

You can also ask questions in a comment on this PR, on the [LLVM Discord](https://discord.com/invite/xS7Z362) or on the [forums](https://discourse.llvm.org/)."""
        self.pr.as_issue().create_comment(comment)
        return True


class PRBuildbotInformation:
    COMMENT_TAG = "<!--LLVM BUILDBOT INFORMATION COMMENT-->\n"

    def __init__(self, token: str, repo: str, pr_number: int, author: str):
        repo = github.Github(token).get_repo(repo)
        self.pr = repo.get_issue(pr_number).as_pull_request()
        self.author = author

    def should_comment(self) -> bool:
        # As soon as a new contributor has a PR merged, they are no longer a new contributor.
        # We can tell that they were a new contributor previously because we would have
        # added a new contributor greeting comment when they opened the PR.
        found_greeting = False
        for comment in self.pr.as_issue().get_comments():
            if PRGreeter.COMMENT_TAG in comment.body:
                found_greeting = True
            elif PRBuildbotInformation.COMMENT_TAG in comment.body:
                # When an issue is reopened, then closed as merged again, we should not
                # add a second comment. This event will be rare in practice as it seems
                # like it's only possible when the main branch is still at the exact
                # revision that the PR was merged on to, beyond that it's closed forever.
                return False
        return found_greeting

    def run(self) -> bool:
        if not self.should_comment():
            return

        # This text is using Markdown formatting. Some of the lines are longer
        # than others so that the final text is some reasonable looking paragraphs
        # after the long URLs are rendered.
        comment = f"""\
{PRBuildbotInformation.COMMENT_TAG}
@{self.author} Congratulations on having your first Pull Request (PR) merged into the LLVM Project!

Your changes will be combined with recent changes from other authors, then tested
by our [build bots](https://lab.llvm.org/buildbot/). If there is a problem with a build, you may receive a report in an email or a comment on this PR.

Please check whether problems have been caused by your change specifically, as
the builds can include changes from many authors. It is not uncommon for your
change to be included in a build that fails due to someone else's changes, or
infrastructure issues.

How to do this, and the rest of the post-merge process, is covered in detail [here](https://llvm.org/docs/MyFirstTypoFix.html#myfirsttypofix-issues-after-landing-your-pr).

If your change does cause a problem, it may be reverted, or you can revert it yourself.
This is a normal part of [LLVM development](https://llvm.org/docs/DeveloperPolicy.html#patch-reversion-policy). You can fix your changes and open a new PR to merge them again.

If you don't get any reports, no action is required from you. Your changes are working as expected, well done!
"""
        self.pr.as_issue().create_comment(comment)
        return True


def setup_llvmbot_git(git_dir="."):
    """
    Configure the git repo in `git_dir` with the llvmbot account so
    commits are attributed to llvmbot.
    """
    repo = Repo(git_dir)
    with repo.config_writer() as config:
        config.set_value("user", "name", "llvmbot")
        config.set_value("user", "email", "llvmbot@llvm.org")


def extract_commit_hash(arg: str):
    """
    Extract the commit hash from the argument passed to /action github
    comment actions. We currently only support passing the commit hash
    directly or use the github URL, such as
    https://github.com/llvm/llvm-project/commit/2832d7941f4207f1fcf813b27cf08cecc3086959
    """
    github_prefix = "https://github.com/llvm/llvm-project/commit/"
    if arg.startswith(github_prefix):
        return arg[len(github_prefix) :]
    return arg


class ReleaseWorkflow:
    CHERRY_PICK_FAILED_LABEL = "release:cherry-pick-failed"

    """
    This class implements the sub-commands for the release-workflow command.
    The current sub-commands are:
        * create-branch
        * create-pull-request

    The execute_command method will automatically choose the correct sub-command
    based on the text in stdin.
    """

    def __init__(
        self,
        token: str,
        repo: str,
        issue_number: int,
        branch_repo_name: str,
        branch_repo_token: str,
        llvm_project_dir: str,
        requested_by: str,
    ) -> None:
        self._token = token
        self._repo_name = repo
        self._issue_number = issue_number
        self._branch_repo_name = branch_repo_name
        if branch_repo_token:
            self._branch_repo_token = branch_repo_token
        else:
            self._branch_repo_token = self.token
        self._llvm_project_dir = llvm_project_dir
        self._requested_by = requested_by

    @property
    def token(self) -> str:
        return self._token

    @property
    def repo_name(self) -> str:
        return self._repo_name

    @property
    def issue_number(self) -> int:
        return self._issue_number

    @property
    def branch_repo_owner(self) -> str:
        return self.branch_repo_name.split("/")[0]

    @property
    def branch_repo_name(self) -> str:
        return self._branch_repo_name

    @property
    def branch_repo_token(self) -> str:
        return self._branch_repo_token

    @property
    def llvm_project_dir(self) -> str:
        return self._llvm_project_dir

    @property
    def requested_by(self) -> str:
        return self._requested_by

    @property
    def repo(self) -> github.Repository.Repository:
        return github.Github(self.token).get_repo(self.repo_name)

    @property
    def issue(self) -> github.Issue.Issue:
        return self.repo.get_issue(self.issue_number)

    @property
    def push_url(self) -> str:
        return "https://{}@github.com/{}".format(
            self.branch_repo_token, self.branch_repo_name
        )

    @property
    def branch_name(self) -> str:
        return "issue{}".format(self.issue_number)

    @property
    def release_branch_for_issue(self) -> Optional[str]:
        issue = self.issue
        milestone = issue.milestone
        if milestone is None:
            return None
        m = re.search("branch: (.+)", milestone.description)
        if m:
            return m.group(1)
        return None

    def print_release_branch(self) -> None:
        print(self.release_branch_for_issue)

    def issue_notify_branch(self) -> None:
        self.issue.create_comment(
            "/branch {}/{}".format(self.branch_repo_name, self.branch_name)
        )

    def issue_notify_pull_request(self, pull: github.PullRequest.PullRequest) -> None:
        self.issue.create_comment(
            "/pull-request {}#{}".format(self.repo_name, pull.number)
        )

    def make_ignore_comment(self, comment: str) -> str:
        """
        Returns the comment string with a prefix that will cause
        a Github workflow to skip parsing this comment.

        :param str comment: The comment to ignore
        """
        return "<!--IGNORE-->\n" + comment

    def issue_notify_no_milestone(self, comment: List[str]) -> None:
        message = "{}\n\nError: Command failed due to missing milestone.".format(
            "".join([">" + line for line in comment])
        )
        self.issue.create_comment(self.make_ignore_comment(message))

    @property
    def action_url(self) -> str:
        if os.getenv("CI"):
            return "https://github.com/{}/actions/runs/{}".format(
                os.getenv("GITHUB_REPOSITORY"), os.getenv("GITHUB_RUN_ID")
            )
        return ""

    def issue_notify_cherry_pick_failure(
        self, commit: str
    ) -> github.IssueComment.IssueComment:
        message = self.make_ignore_comment(
            "Failed to cherry-pick: {}\n\n".format(commit)
        )
        action_url = self.action_url
        if action_url:
            message += action_url + "\n\n"
        message += "Please manually backport the fix and push it to your github fork.  Once this is done, please create a [pull request](https://github.com/llvm/llvm-project/compare)"
        issue = self.issue
        comment = issue.create_comment(message)
        issue.add_to_labels(self.CHERRY_PICK_FAILED_LABEL)
        return comment

    def issue_notify_pull_request_failure(
        self, branch: str
    ) -> github.IssueComment.IssueComment:
        message = "Failed to create pull request for {} ".format(branch)
        message += self.action_url
        return self.issue.create_comment(message)

    def issue_remove_cherry_pick_failed_label(self):
        if self.CHERRY_PICK_FAILED_LABEL in [l.name for l in self.issue.labels]:
            self.issue.remove_from_labels(self.CHERRY_PICK_FAILED_LABEL)

    def get_main_commit(self, cherry_pick_sha: str) -> github.Commit.Commit:
        commit = self.repo.get_commit(cherry_pick_sha)
        message = commit.commit.message
        m = re.search("\(cherry picked from commit ([0-9a-f]+)\)", message)
        if not m:
            return None
        return self.repo.get_commit(m.group(1))

    def pr_request_review(self, pr: github.PullRequest.PullRequest):
        """
        This function will try to find the best reviewers for `commits` and
        then add a comment requesting review of the backport and add them as
        reviewers.

        The reviewers selected are those users who approved the pull request
        for the main branch.
        """
        reviewers = []
        for commit in pr.get_commits():
            main_commit = self.get_main_commit(commit.sha)
            if not main_commit:
                continue
            for pull in main_commit.get_pulls():
                for review in pull.get_reviews():
                    if review.state != "APPROVED":
                        continue
                reviewers.append(review.user.login)
        if len(reviewers):
            message = "{} What do you think about merging this PR to the release branch?".format(
                " ".join(["@" + r for r in reviewers])
            )
            pr.create_issue_comment(message)
            pr.create_review_request(reviewers)

    def create_branch(self, commits: List[str]) -> bool:
        """
        This function attempts to backport `commits` into the branch associated
        with `self.issue_number`.

        If this is successful, then the branch is pushed to `self.branch_repo_name`, if not,
        a comment is added to the issue saying that the cherry-pick failed.

        :param list commits: List of commits to cherry-pick.

        """
        print("cherry-picking", commits)
        branch_name = self.branch_name
        local_repo = Repo(self.llvm_project_dir)
        local_repo.git.checkout(self.release_branch_for_issue)

        for c in commits:
            try:
                local_repo.git.cherry_pick("-x", c)
            except Exception as e:
                self.issue_notify_cherry_pick_failure(c)
                raise e

        push_url = self.push_url
        print("Pushing to {} {}".format(push_url, branch_name))
        local_repo.git.push(push_url, "HEAD:{}".format(branch_name), force=True)

        self.issue_remove_cherry_pick_failed_label()
        return self.create_pull_request(
            self.branch_repo_owner, self.repo_name, branch_name, commits
        )

    def check_if_pull_request_exists(
        self, repo: github.Repository.Repository, head: str
    ) -> bool:
        pulls = repo.get_pulls(head=head)
        return pulls.totalCount != 0

    def create_pull_request(
        self, owner: str, repo_name: str, branch: str, commits: List[str]
    ) -> bool:
        """
        Create a pull request in `self.repo_name`.  The base branch of the
        pull request will be chosen based on the the milestone attached to
        the issue represented by `self.issue_number`  For example if the milestone
        is Release 13.0.1, then the base branch will be release/13.x. `branch`
        will be used as the compare branch.
        https://docs.github.com/en/get-started/quickstart/github-glossary#base-branch
        https://docs.github.com/en/get-started/quickstart/github-glossary#compare-branch
        """
        repo = github.Github(self.token).get_repo(self.repo_name)
        issue_ref = "{}#{}".format(self.repo_name, self.issue_number)
        pull = None
        release_branch_for_issue = self.release_branch_for_issue
        if release_branch_for_issue is None:
            return False

        head = f"{owner}:{branch}"
        if self.check_if_pull_request_exists(repo, head):
            print("PR already exists...")
            return True
        try:
            commit_message = repo.get_commit(commits[-1]).commit.message
            message_lines = commit_message.splitlines()
            title = "{}: {}".format(release_branch_for_issue, message_lines[0])
            body = "Backport {}\n\nRequested by: @{}".format(
                " ".join(commits), self.requested_by
            )
            pull = repo.create_pull(
                title=title,
                body=body,
                base=release_branch_for_issue,
                head=head,
                maintainer_can_modify=True,
            )

            pull.as_issue().edit(milestone=self.issue.milestone)

            # Once the pull request has been created, we can close the
            # issue that was used to request the cherry-pick
            self.issue.edit(state="closed", state_reason="completed")

            try:
                self.pr_request_review(pull)
            except Exception as e:
                print("error: Failed while searching for reviewers", e)

        except Exception as e:
            self.issue_notify_pull_request_failure(branch)
            raise e

        if pull is None:
            return False

        self.issue_notify_pull_request(pull)
        self.issue_remove_cherry_pick_failed_label()

        # TODO(tstellar): Do you really want to always return True?
        return True

    def execute_command(self) -> bool:
        """
        This function reads lines from STDIN and executes the first command
        that it finds.  The supported command is:
        /cherry-pick< ><:> commit0 <commit1> <commit2> <...>
        """
        for line in sys.stdin:
            line.rstrip()
            m = re.search(r"/cherry-pick\s*:? *(.*)", line)
            if not m:
                continue

            args = m.group(1)

            arg_list = args.split()
            commits = list(map(lambda a: extract_commit_hash(a), arg_list))
            return self.create_branch(commits)

        print("Do not understand input:")
        print(sys.stdin.readlines())
        return False


def request_release_note(token: str, repo_name: str, pr_number: int):
    repo = github.Github(token).get_repo(repo_name)
    pr = repo.get_issue(pr_number).as_pull_request()
    submitter = pr.user.login
    if submitter == "llvmbot":
        m = re.search("Requested by: @(.+)$", pr.body)
        if not m:
            submitter = None
            print("Warning could not determine user who requested backport.")
        submitter = m.group(1)

    mention = ""
    if submitter:
        mention = f"@{submitter}"

    comment = f"{mention} (or anyone else). If you would like to add a note about this fix in the release notes (completely optional). Please reply to this comment with a one or two sentence description of the fix.  When you are done, please add the release:note label to this PR. "
    try:
        pr.as_issue().create_comment(comment)
    except:
        # Failed to create comment so emit file instead
        with open("comments", "w") as file:
            data = [{"body": comment}]
            json.dump(data, file)


parser = argparse.ArgumentParser()
parser.add_argument(
    "--token", type=str, required=True, help="GitHub authentication token"
)
parser.add_argument(
    "--repo",
    type=str,
    default=os.getenv("GITHUB_REPOSITORY", "llvm/llvm-project"),
    help="The GitHub repository that we are working with in the form of <owner>/<repo> (e.g. llvm/llvm-project)",
)
subparsers = parser.add_subparsers(dest="command")

issue_subscriber_parser = subparsers.add_parser("issue-subscriber")
issue_subscriber_parser.add_argument("--label-name", type=str, required=True)
issue_subscriber_parser.add_argument("--issue-number", type=int, required=True)

pr_subscriber_parser = subparsers.add_parser("pr-subscriber")
pr_subscriber_parser.add_argument("--label-name", type=str, required=True)
pr_subscriber_parser.add_argument("--issue-number", type=int, required=True)

pr_greeter_parser = subparsers.add_parser("pr-greeter")
pr_greeter_parser.add_argument("--issue-number", type=int, required=True)

pr_buildbot_information_parser = subparsers.add_parser("pr-buildbot-information")
pr_buildbot_information_parser.add_argument("--issue-number", type=int, required=True)
pr_buildbot_information_parser.add_argument("--author", type=str, required=True)

release_workflow_parser = subparsers.add_parser("release-workflow")
release_workflow_parser.add_argument(
    "--llvm-project-dir",
    type=str,
    default=".",
    help="directory containing the llvm-project checkout",
)
release_workflow_parser.add_argument(
    "--issue-number", type=int, required=True, help="The issue number to update"
)
release_workflow_parser.add_argument(
    "--branch-repo-token",
    type=str,
    help="GitHub authentication token to use for the repository where new branches will be pushed. Defaults to TOKEN.",
)
release_workflow_parser.add_argument(
    "--branch-repo",
    type=str,
    default="llvmbot/llvm-project",
    help="The name of the repo where new branches will be pushed (e.g. llvm/llvm-project)",
)
release_workflow_parser.add_argument(
    "sub_command",
    type=str,
    choices=["print-release-branch", "auto"],
    help="Print to stdout the name of the release branch ISSUE_NUMBER should be backported to",
)

llvmbot_git_config_parser = subparsers.add_parser(
    "setup-llvmbot-git",
    help="Set the default user and email for the git repo in LLVM_PROJECT_DIR to llvmbot",
)
release_workflow_parser.add_argument(
    "--requested-by",
    type=str,
    required=True,
    help="The user that requested this backport",
)

request_release_note_parser = subparsers.add_parser(
    "request-release-note",
    help="Request a release note for a pull request",
)
request_release_note_parser.add_argument(
    "--pr-number",
    type=int,
    required=True,
    help="The pull request to request the release note",
)


args = parser.parse_args()

if args.command == "issue-subscriber":
    issue_subscriber = IssueSubscriber(
        args.token, args.repo, args.issue_number, args.label_name
    )
    issue_subscriber.run()
elif args.command == "pr-subscriber":
    pr_subscriber = PRSubscriber(
        args.token, args.repo, args.issue_number, args.label_name
    )
    pr_subscriber.run()
elif args.command == "pr-greeter":
    pr_greeter = PRGreeter(args.token, args.repo, args.issue_number)
    pr_greeter.run()
elif args.command == "pr-buildbot-information":
    pr_buildbot_information = PRBuildbotInformation(
        args.token, args.repo, args.issue_number, args.author
    )
    pr_buildbot_information.run()
elif args.command == "release-workflow":
    release_workflow = ReleaseWorkflow(
        args.token,
        args.repo,
        args.issue_number,
        args.branch_repo,
        args.branch_repo_token,
        args.llvm_project_dir,
        args.requested_by,
    )
    if not release_workflow.release_branch_for_issue:
        release_workflow.issue_notify_no_milestone(sys.stdin.readlines())
        sys.exit(1)
    if args.sub_command == "print-release-branch":
        release_workflow.print_release_branch()
    else:
        if not release_workflow.execute_command():
            sys.exit(1)
elif args.command == "setup-llvmbot-git":
    setup_llvmbot_git()
elif args.command == "request-release-note":
    request_release_note(args.token, args.repo, args.pr_number)
