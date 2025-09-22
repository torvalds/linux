#!/usr/bin/env python

from __future__ import print_function

import argparse
import email.mime.multipart
import email.mime.text
import logging
import os.path
import pickle
import re
import smtplib
import subprocess
import sys
from datetime import datetime, timedelta
from phabricator import Phabricator

# Setting up a virtualenv to run this script can be done by running the
# following commands:
# $ virtualenv venv
# $ . ./venv/bin/activate
# $ pip install Phabricator

GIT_REPO_METADATA = (("llvm-monorepo", "https://github.com/llvm/llvm-project"),)

# The below PhabXXX classes represent objects as modelled by Phabricator.
# The classes can be serialized to disk, to try and make sure that we don't
# needlessly have to re-fetch lots of data from Phabricator, as that would
# make this script unusably slow.


class PhabObject:
    OBJECT_KIND = None

    def __init__(self, id):
        self.id = id


class PhabObjectCache:
    def __init__(self, PhabObjectClass):
        self.PhabObjectClass = PhabObjectClass
        self.most_recent_info = None
        self.oldest_info = None
        self.id2PhabObjects = {}

    def get_name(self):
        return self.PhabObjectClass.OBJECT_KIND + "sCache"

    def get(self, id):
        if id not in self.id2PhabObjects:
            self.id2PhabObjects[id] = self.PhabObjectClass(id)
        return self.id2PhabObjects[id]

    def get_ids_in_cache(self):
        return list(self.id2PhabObjects.keys())

    def get_objects(self):
        return list(self.id2PhabObjects.values())

    DEFAULT_DIRECTORY = "PhabObjectCache"

    def _get_pickle_name(self, directory):
        file_name = "Phab" + self.PhabObjectClass.OBJECT_KIND + "s.pickle"
        return os.path.join(directory, file_name)

    def populate_cache_from_disk(self, directory=DEFAULT_DIRECTORY):
        """
        FIXME: consider if serializing to JSON would bring interoperability
        advantages over serializing to pickle.
        """
        try:
            f = open(self._get_pickle_name(directory), "rb")
        except IOError as err:
            print("Could not find cache. Error message: {0}. Continuing...".format(err))
        else:
            with f:
                try:
                    d = pickle.load(f)
                    self.__dict__.update(d)
                except EOFError as err:
                    print(
                        "Cache seems to be corrupt. "
                        + "Not using cache. Error message: {0}".format(err)
                    )

    def write_cache_to_disk(self, directory=DEFAULT_DIRECTORY):
        if not os.path.exists(directory):
            os.makedirs(directory)
        with open(self._get_pickle_name(directory), "wb") as f:
            pickle.dump(self.__dict__, f)
        print(
            "wrote cache to disk, most_recent_info= {0}".format(
                datetime.fromtimestamp(self.most_recent_info)
                if self.most_recent_info is not None
                else None
            )
        )


class PhabReview(PhabObject):
    OBJECT_KIND = "Review"

    def __init__(self, id):
        PhabObject.__init__(self, id)

    def update(self, title, dateCreated, dateModified, author):
        self.title = title
        self.dateCreated = dateCreated
        self.dateModified = dateModified
        self.author = author

    def setPhabDiffs(self, phabDiffs):
        self.phabDiffs = phabDiffs


class PhabUser(PhabObject):
    OBJECT_KIND = "User"

    def __init__(self, id):
        PhabObject.__init__(self, id)

    def update(self, phid, realName):
        self.phid = phid
        self.realName = realName


class PhabHunk:
    def __init__(self, rest_api_hunk):
        self.oldOffset = int(rest_api_hunk["oldOffset"])
        self.oldLength = int(rest_api_hunk["oldLength"])
        # self.actual_lines_changed_offset will contain the offsets of the
        # lines that were changed in this hunk.
        self.actual_lines_changed_offset = []
        offset = self.oldOffset
        inHunk = False
        hunkStart = -1
        contextLines = 3
        for line in rest_api_hunk["corpus"].split("\n"):
            if line.startswith("+"):
                # line is a new line that got introduced in this patch.
                # Do not record it as a changed line.
                if inHunk is False:
                    inHunk = True
                    hunkStart = max(self.oldOffset, offset - contextLines)
                continue
            if line.startswith("-"):
                # line was changed or removed from the older version of the
                # code. Record it as a changed line.
                if inHunk is False:
                    inHunk = True
                    hunkStart = max(self.oldOffset, offset - contextLines)
                offset += 1
                continue
            # line is a context line.
            if inHunk is True:
                inHunk = False
                hunkEnd = offset + contextLines
                self.actual_lines_changed_offset.append((hunkStart, hunkEnd))
            offset += 1
        if inHunk is True:
            hunkEnd = offset + contextLines
            self.actual_lines_changed_offset.append((hunkStart, hunkEnd))

        # The above algorithm could result in adjacent or overlapping ranges
        # being recorded into self.actual_lines_changed_offset.
        # Merge the adjacent and overlapping ranges in there:
        t = []
        lastRange = None
        for start, end in self.actual_lines_changed_offset + [
            (sys.maxsize, sys.maxsize)
        ]:
            if lastRange is None:
                lastRange = (start, end)
            else:
                if lastRange[1] >= start:
                    lastRange = (lastRange[0], end)
                else:
                    t.append(lastRange)
                    lastRange = (start, end)
        self.actual_lines_changed_offset = t


class PhabChange:
    def __init__(self, rest_api_change):
        self.oldPath = rest_api_change["oldPath"]
        self.hunks = [PhabHunk(h) for h in rest_api_change["hunks"]]


class PhabDiff(PhabObject):
    OBJECT_KIND = "Diff"

    def __init__(self, id):
        PhabObject.__init__(self, id)

    def update(self, rest_api_results):
        self.revisionID = rest_api_results["revisionID"]
        self.dateModified = int(rest_api_results["dateModified"])
        self.dateCreated = int(rest_api_results["dateCreated"])
        self.changes = [PhabChange(c) for c in rest_api_results["changes"]]


class ReviewsCache(PhabObjectCache):
    def __init__(self):
        PhabObjectCache.__init__(self, PhabReview)


class UsersCache(PhabObjectCache):
    def __init__(self):
        PhabObjectCache.__init__(self, PhabUser)


reviews_cache = ReviewsCache()
users_cache = UsersCache()


def init_phab_connection():
    phab = Phabricator()
    phab.update_interfaces()
    return phab


def update_cached_info(
    phab,
    cache,
    phab_query,
    order,
    record_results,
    max_nr_entries_per_fetch,
    max_nr_days_to_cache,
):
    q = phab
    LIMIT = max_nr_entries_per_fetch
    for query_step in phab_query:
        q = getattr(q, query_step)
    results = q(order=order, limit=LIMIT)
    most_recent_info, oldest_info = record_results(cache, results, phab)
    oldest_info_to_fetch = datetime.fromtimestamp(most_recent_info) - timedelta(
        days=max_nr_days_to_cache
    )
    most_recent_info_overall = most_recent_info
    cache.write_cache_to_disk()
    after = results["cursor"]["after"]
    print("after: {0!r}".format(after))
    print("most_recent_info: {0}".format(datetime.fromtimestamp(most_recent_info)))
    while (
        after is not None and datetime.fromtimestamp(oldest_info) > oldest_info_to_fetch
    ):
        need_more_older_data = (
            cache.oldest_info is None
            or datetime.fromtimestamp(cache.oldest_info) > oldest_info_to_fetch
        )
        print(
            (
                "need_more_older_data={0} cache.oldest_info={1} "
                + "oldest_info_to_fetch={2}"
            ).format(
                need_more_older_data,
                datetime.fromtimestamp(cache.oldest_info)
                if cache.oldest_info is not None
                else None,
                oldest_info_to_fetch,
            )
        )
        need_more_newer_data = (
            cache.most_recent_info is None or cache.most_recent_info < most_recent_info
        )
        print(
            (
                "need_more_newer_data={0} cache.most_recent_info={1} "
                + "most_recent_info={2}"
            ).format(need_more_newer_data, cache.most_recent_info, most_recent_info)
        )
        if not need_more_older_data and not need_more_newer_data:
            break
        results = q(order=order, after=after, limit=LIMIT)
        most_recent_info, oldest_info = record_results(cache, results, phab)
        after = results["cursor"]["after"]
        print("after: {0!r}".format(after))
        print("most_recent_info: {0}".format(datetime.fromtimestamp(most_recent_info)))
        cache.write_cache_to_disk()
    cache.most_recent_info = most_recent_info_overall
    if after is None:
        # We did fetch all records. Mark the cache to contain all info since
        # the start of time.
        oldest_info = 0
    cache.oldest_info = oldest_info
    cache.write_cache_to_disk()


def record_reviews(cache, reviews, phab):
    most_recent_info = None
    oldest_info = None
    for reviewInfo in reviews["data"]:
        if reviewInfo["type"] != "DREV":
            continue
        id = reviewInfo["id"]
        # phid = reviewInfo["phid"]
        dateModified = int(reviewInfo["fields"]["dateModified"])
        dateCreated = int(reviewInfo["fields"]["dateCreated"])
        title = reviewInfo["fields"]["title"]
        author = reviewInfo["fields"]["authorPHID"]
        phabReview = cache.get(id)
        if (
            "dateModified" not in phabReview.__dict__
            or dateModified > phabReview.dateModified
        ):
            diff_results = phab.differential.querydiffs(revisionIDs=[id])
            diff_ids = sorted(diff_results.keys())
            phabDiffs = []
            for diff_id in diff_ids:
                diffInfo = diff_results[diff_id]
                d = PhabDiff(diff_id)
                d.update(diffInfo)
                phabDiffs.append(d)
            phabReview.update(title, dateCreated, dateModified, author)
            phabReview.setPhabDiffs(phabDiffs)
            print(
                "Updated D{0} modified on {1} ({2} diffs)".format(
                    id, datetime.fromtimestamp(dateModified), len(phabDiffs)
                )
            )

        if most_recent_info is None:
            most_recent_info = dateModified
        elif most_recent_info < dateModified:
            most_recent_info = dateModified

        if oldest_info is None:
            oldest_info = dateModified
        elif oldest_info > dateModified:
            oldest_info = dateModified
    return most_recent_info, oldest_info


def record_users(cache, users, phab):
    most_recent_info = None
    oldest_info = None
    for info in users["data"]:
        if info["type"] != "USER":
            continue
        id = info["id"]
        phid = info["phid"]
        dateModified = int(info["fields"]["dateModified"])
        # dateCreated = int(info["fields"]["dateCreated"])
        realName = info["fields"]["realName"]
        phabUser = cache.get(id)
        phabUser.update(phid, realName)
        if most_recent_info is None:
            most_recent_info = dateModified
        elif most_recent_info < dateModified:
            most_recent_info = dateModified
        if oldest_info is None:
            oldest_info = dateModified
        elif oldest_info > dateModified:
            oldest_info = dateModified
    return most_recent_info, oldest_info


PHABCACHESINFO = (
    (
        reviews_cache,
        ("differential", "revision", "search"),
        "updated",
        record_reviews,
        5,
        7,
    ),
    (users_cache, ("user", "search"), "newest", record_users, 100, 1000),
)


def load_cache():
    for cache, phab_query, order, record_results, _, _ in PHABCACHESINFO:
        cache.populate_cache_from_disk()
        print(
            "Loaded {0} nr entries: {1}".format(
                cache.get_name(), len(cache.get_ids_in_cache())
            )
        )
        print(
            "Loaded {0} has most recent info: {1}".format(
                cache.get_name(),
                datetime.fromtimestamp(cache.most_recent_info)
                if cache.most_recent_info is not None
                else None,
            )
        )


def update_cache(phab):
    load_cache()
    for (
        cache,
        phab_query,
        order,
        record_results,
        max_nr_entries_per_fetch,
        max_nr_days_to_cache,
    ) in PHABCACHESINFO:
        update_cached_info(
            phab,
            cache,
            phab_query,
            order,
            record_results,
            max_nr_entries_per_fetch,
            max_nr_days_to_cache,
        )
        ids_in_cache = cache.get_ids_in_cache()
        print("{0} objects in {1}".format(len(ids_in_cache), cache.get_name()))
        cache.write_cache_to_disk()


def get_most_recent_reviews(days):
    newest_reviews = sorted(reviews_cache.get_objects(), key=lambda r: -r.dateModified)
    if len(newest_reviews) == 0:
        return newest_reviews
    most_recent_review_time = datetime.fromtimestamp(newest_reviews[0].dateModified)
    cut_off_date = most_recent_review_time - timedelta(days=days)
    result = []
    for review in newest_reviews:
        if datetime.fromtimestamp(review.dateModified) < cut_off_date:
            return result
        result.append(review)
    return result


# All of the above code is about fetching data from Phabricator and caching it
# on local disk. The below code contains the actual "business logic" for this
# script.

_userphid2realname = None


def get_real_name_from_author(user_phid):
    global _userphid2realname
    if _userphid2realname is None:
        _userphid2realname = {}
        for user in users_cache.get_objects():
            _userphid2realname[user.phid] = user.realName
    return _userphid2realname.get(user_phid, "unknown")


def print_most_recent_reviews(phab, days, filter_reviewers):
    msgs = []

    def add_msg(msg):
        msgs.append(msg)
        print(msg.encode("utf-8"))

    newest_reviews = get_most_recent_reviews(days)
    add_msg(
        "These are the reviews that look interesting to be reviewed. "
        + "The report below has 2 sections. The first "
        + "section is organized per review; the second section is organized "
        + "per potential reviewer.\n"
    )
    oldest_review = newest_reviews[-1] if len(newest_reviews) > 0 else None
    oldest_datetime = (
        datetime.fromtimestamp(oldest_review.dateModified) if oldest_review else None
    )
    add_msg(
        (
            "The report below is based on analyzing the reviews that got "
            + "touched in the past {0} days (since {1}). "
            + "The script found {2} such reviews.\n"
        ).format(days, oldest_datetime, len(newest_reviews))
    )
    reviewer2reviews_and_scores = {}
    for i, review in enumerate(newest_reviews):
        matched_reviewers = find_reviewers_for_review(review)
        matched_reviewers = filter_reviewers(matched_reviewers)
        if len(matched_reviewers) == 0:
            continue
        add_msg(
            (
                "{0:>3}. https://reviews.llvm.org/D{1} by {2}\n     {3}\n"
                + "     Last updated on {4}"
            ).format(
                i,
                review.id,
                get_real_name_from_author(review.author),
                review.title,
                datetime.fromtimestamp(review.dateModified),
            )
        )
        for reviewer, scores in matched_reviewers:
            add_msg(
                "    potential reviewer {0}, score {1}".format(
                    reviewer,
                    "(" + "/".join(["{0:.1f}%".format(s) for s in scores]) + ")",
                )
            )
            if reviewer not in reviewer2reviews_and_scores:
                reviewer2reviews_and_scores[reviewer] = []
            reviewer2reviews_and_scores[reviewer].append((review, scores))

    # Print out a summary per reviewer.
    for reviewer in sorted(reviewer2reviews_and_scores.keys()):
        reviews_and_scores = reviewer2reviews_and_scores[reviewer]
        reviews_and_scores.sort(key=lambda rs: rs[1], reverse=True)
        add_msg(
            "\n\nSUMMARY FOR {0} (found {1} reviews):".format(
                reviewer, len(reviews_and_scores)
            )
        )
        for review, scores in reviews_and_scores:
            add_msg(
                "[{0}] https://reviews.llvm.org/D{1} '{2}' by {3}".format(
                    "/".join(["{0:.1f}%".format(s) for s in scores]),
                    review.id,
                    review.title,
                    get_real_name_from_author(review.author),
                )
            )
    return "\n".join(msgs)


def get_git_cmd_output(cmd):
    output = None
    try:
        logging.debug(cmd)
        output = subprocess.check_output(cmd, shell=True, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
        logging.debug(str(e))
    if output is None:
        return None
    return output.decode("utf-8", errors="ignore")


reAuthorMail = re.compile("^author-mail <([^>]*)>.*$")


def parse_blame_output_line_porcelain(blame_output_lines):
    email2nr_occurences = {}
    if blame_output_lines is None:
        return email2nr_occurences
    for line in blame_output_lines:
        m = reAuthorMail.match(line)
        if m:
            author_email_address = m.group(1)
            if author_email_address not in email2nr_occurences:
                email2nr_occurences[author_email_address] = 1
            else:
                email2nr_occurences[author_email_address] += 1
    return email2nr_occurences


class BlameOutputCache:
    def __init__(self):
        self.cache = {}

    def _populate_cache_for(self, cache_key):
        assert cache_key not in self.cache
        git_repo, base_revision, path = cache_key
        cmd = (
            "git -C {0} blame --encoding=utf-8 --date iso -f -e -w "
            + "--line-porcelain {1} -- {2}"
        ).format(git_repo, base_revision, path)
        blame_output = get_git_cmd_output(cmd)
        self.cache[cache_key] = (
            blame_output.split("\n") if blame_output is not None else None
        )
        # FIXME: the blame cache could probably be made more effective still if
        # instead of storing the requested base_revision in the cache, the last
        # revision before the base revision this file/path got changed in gets
        # stored. That way multiple project revisions for which this specific
        # file/patch hasn't changed would get cache hits (instead of misses in
        # the current implementation).

    def get_blame_output_for(
        self, git_repo, base_revision, path, start_line=-1, end_line=-1
    ):
        cache_key = (git_repo, base_revision, path)
        if cache_key not in self.cache:
            self._populate_cache_for(cache_key)
        assert cache_key in self.cache
        all_blame_lines = self.cache[cache_key]
        if all_blame_lines is None:
            return None
        if start_line == -1 and end_line == -1:
            return all_blame_lines
        assert start_line >= 0
        assert end_line >= 0
        assert end_line <= len(all_blame_lines)
        assert start_line <= len(all_blame_lines)
        assert start_line <= end_line
        return all_blame_lines[start_line:end_line]

    def get_parsed_git_blame_for(
        self, git_repo, base_revision, path, start_line=-1, end_line=-1
    ):
        return parse_blame_output_line_porcelain(
            self.get_blame_output_for(
                git_repo, base_revision, path, start_line, end_line
            )
        )


blameOutputCache = BlameOutputCache()


def find_reviewers_for_diff_heuristic(diff):
    # Heuristic 1: assume good reviewers are the ones that touched the same
    # lines before as this patch is touching.
    # Heuristic 2: assume good reviewers are the ones that touched the same
    # files before as this patch is touching.
    reviewers2nr_lines_touched = {}
    reviewers2nr_files_touched = {}
    # Assume last revision before diff was modified is the revision the diff
    # applies to.
    assert len(GIT_REPO_METADATA) == 1
    git_repo = os.path.join("git_repos", GIT_REPO_METADATA[0][0])
    cmd = 'git -C {0} rev-list -n 1 --before="{1}" main'.format(
        git_repo,
        datetime.fromtimestamp(diff.dateModified).strftime("%Y-%m-%d %H:%M:%s"),
    )
    base_revision = get_git_cmd_output(cmd).strip()
    logging.debug("Base revision={0}".format(base_revision))
    for change in diff.changes:
        path = change.oldPath
        # Compute heuristic 1: look at context of patch lines.
        for hunk in change.hunks:
            for start_line, end_line in hunk.actual_lines_changed_offset:
                # Collect git blame results for authors in those ranges.
                for (
                    reviewer,
                    nr_occurences,
                ) in blameOutputCache.get_parsed_git_blame_for(
                    git_repo, base_revision, path, start_line, end_line
                ).items():
                    if reviewer not in reviewers2nr_lines_touched:
                        reviewers2nr_lines_touched[reviewer] = 0
                    reviewers2nr_lines_touched[reviewer] += nr_occurences
        # Compute heuristic 2: don't look at context, just at files touched.
        # Collect git blame results for authors in those ranges.
        for reviewer, nr_occurences in blameOutputCache.get_parsed_git_blame_for(
            git_repo, base_revision, path
        ).items():
            if reviewer not in reviewers2nr_files_touched:
                reviewers2nr_files_touched[reviewer] = 0
            reviewers2nr_files_touched[reviewer] += 1

    # Compute "match scores"
    total_nr_lines = sum(reviewers2nr_lines_touched.values())
    total_nr_files = len(diff.changes)
    reviewers_matchscores = [
        (
            reviewer,
            (
                reviewers2nr_lines_touched.get(reviewer, 0) * 100.0 / total_nr_lines
                if total_nr_lines != 0
                else 0,
                reviewers2nr_files_touched[reviewer] * 100.0 / total_nr_files
                if total_nr_files != 0
                else 0,
            ),
        )
        for reviewer, nr_lines in reviewers2nr_files_touched.items()
    ]
    reviewers_matchscores.sort(key=lambda i: i[1], reverse=True)
    return reviewers_matchscores


def find_reviewers_for_review(review):
    # Process the newest diff first.
    diffs = sorted(review.phabDiffs, key=lambda d: d.dateModified, reverse=True)
    if len(diffs) == 0:
        return
    diff = diffs[0]
    matched_reviewers = find_reviewers_for_diff_heuristic(diff)
    # Show progress, as this is a slow operation:
    sys.stdout.write(".")
    sys.stdout.flush()
    logging.debug("matched_reviewers: {0}".format(matched_reviewers))
    return matched_reviewers


def update_git_repos():
    git_repos_directory = "git_repos"
    for name, url in GIT_REPO_METADATA:
        dirname = os.path.join(git_repos_directory, name)
        if not os.path.exists(dirname):
            cmd = "git clone {0} {1}".format(url, dirname)
            output = get_git_cmd_output(cmd)
        cmd = "git -C {0} pull --rebase".format(dirname)
        output = get_git_cmd_output(cmd)


def send_emails(email_addresses, sender, msg):
    s = smtplib.SMTP()
    s.connect()
    for email_address in email_addresses:
        email_msg = email.mime.multipart.MIMEMultipart()
        email_msg["From"] = sender
        email_msg["To"] = email_address
        email_msg["Subject"] = "LLVM patches you may be able to review."
        email_msg.attach(email.mime.text.MIMEText(msg.encode("utf-8"), "plain"))
        # python 3.x: s.send_message(email_msg)
        s.sendmail(email_msg["From"], email_msg["To"], email_msg.as_string())
    s.quit()


def filter_reviewers_to_report_for(people_to_look_for):
    # The below is just an example filter, to only report potential reviews
    # to do for the people that will receive the report email.
    return lambda potential_reviewers: [
        r for r in potential_reviewers if r[0] in people_to_look_for
    ]


def main():
    parser = argparse.ArgumentParser(
        description="Match open reviews to potential reviewers."
    )
    parser.add_argument(
        "--no-update-cache",
        dest="update_cache",
        action="store_false",
        default=True,
        help="Do not update cached Phabricator objects",
    )
    parser.add_argument(
        "--email-report",
        dest="email_report",
        nargs="*",
        default="",
        help="A email addresses to send the report to.",
    )
    parser.add_argument(
        "--sender",
        dest="sender",
        default="",
        help="The email address to use in 'From' on messages emailed out.",
    )
    parser.add_argument(
        "--email-addresses",
        dest="email_addresses",
        nargs="*",
        help="The email addresses (as known by LLVM git) of "
        + "the people to look for reviews for.",
    )
    parser.add_argument("--verbose", "-v", action="count")

    args = parser.parse_args()

    if args.verbose >= 1:
        logging.basicConfig(level=logging.DEBUG)

    people_to_look_for = [e.decode("utf-8") for e in args.email_addresses]
    logging.debug(
        "Will look for reviews that following contributors could "
        + "review: {}".format(people_to_look_for)
    )
    logging.debug("Will email a report to: {}".format(args.email_report))

    phab = init_phab_connection()

    if args.update_cache:
        update_cache(phab)

    load_cache()
    update_git_repos()
    msg = print_most_recent_reviews(
        phab,
        days=1,
        filter_reviewers=filter_reviewers_to_report_for(people_to_look_for),
    )

    if args.email_report != []:
        send_emails(args.email_report, args.sender, msg)


if __name__ == "__main__":
    main()
