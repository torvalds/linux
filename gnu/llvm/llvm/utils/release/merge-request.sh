#!/usr/bin/env bash
#===-- merge-request.sh  ---------------------------------------------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===------------------------------------------------------------------------===#
#
# Submit a merge request to bugzilla.
#
#===------------------------------------------------------------------------===#

dryrun=""
stable_version=""
revisions=""
BUGZILLA_BIN=""
BUGZILLA_CMD=""
release_metabug=""
bugzilla_product="new-bugs"
bugzilla_component="new bugs"
bugzilla_assigned_to=""
bugzilla_user=""
bugzilla_version=""
bugzilla_url="https://bugs.llvm.org/xmlrpc.cgi"

function usage() {
  echo "usage: `basename $0` -user EMAIL -stable-version X.Y -r NUM"
  echo ""
  echo " -user EMAIL             Your email address for logging into bugzilla."
  echo " -stable-version X.Y     The stable release version (e.g. 4.0, 5.0)."
  echo " -r NUM                  Revision number to merge (e.g. 1234567)."
  echo "                         This option can be specified multiple times."
  echo " -bugzilla-bin PATH      Path to bugzilla binary (optional)."
  echo " -assign-to EMAIL        Assign bug to user with EMAIL (optional)."
  echo " -dry-run                Print commands instead of executing them."
}

while [ $# -gt 0 ]; do
  case $1 in
    -user)
      shift
      bugzilla_user="$1"
      ;;
    -stable-version)
      shift
      stable_version="$1"
      ;;
    -r)
      shift
      revisions="$revisions $1"
      ;;
    -project)
      shift
      project="$1"
      ;;
    -component)
      shift
      bugzilla_component="$1"
      ;;
    -bugzilla-bin)
      shift
      BUGZILLA_BIN="$1"
      ;;
    -assign-to)
      shift
      bugzilla_assigned_to="--assigned_to=$1"
      ;;
    -dry-run)
      dryrun="echo"
      ;;
    -help | --help | -h | --h | -\? )
      usage
      exit 0
      ;;
    * )
      echo "unknown option: $1"
      usage
      exit 1
      ;;
  esac
  shift
done

if [ -z "$stable_version" ]; then
  echo "error: no stable version specified"
  exit 1
fi

case $stable_version in
  4.0)
    release_metabug="32061"
    ;;
  5.0)
    release_metabug="34492"
    ;;
  6.0)
    release_metabug="36649"
    ;;
  7.0)
    release_metabug="39106"
    ;;
  8.0)
    release_metabug="41221"
    ;;
  9.0)
    release_metabug="43360"
    ;;
  *)
    echo "error: invalid stable version"
    exit 1
esac
bugzilla_version=$stable_version

if [ -z "$revisions" ]; then
  echo "error: no revisions specified"
  exit 1
fi

if [ -z "$bugzilla_user" ]; then
  echo "error: bugzilla username not specified."
  exit 1
fi

if [ -z "$BUGZILLA_BIN" ]; then
  BUGZILLA_BIN=`which bugzilla`
  if [ $? -ne 0 ]; then
    echo "error: could not find bugzilla executable."
    echo "Make sure the bugzilla cli tool is installed on your system: "
    echo "pip install python-bugzilla (recommended)"
    echo ""
    echo "Fedora: dnf install python-bugzilla"
    echo "Ubuntu/Debian: apt-get install bugzilla-cli"
    exit 1
  fi
fi

BUGZILLA_MAJOR_VERSION=`$BUGZILLA_BIN --version 2>&1 | cut -d . -f 1`

if [ $BUGZILLA_MAJOR_VERSION -eq 1 ]; then

  echo "***************************** Error ** ********************************"
  echo "You are using an older version of the bugzilla cli tool, which is not "
  echo "supported.  You need to use bugzilla cli version 2.0.0 or higher:"
  echo "***********************************************************************"
  exit 1
fi

BUGZILLA_CMD="$BUGZILLA_BIN --bugzilla=$bugzilla_url"

rev_string=""
for r in $revisions; do
  rev_string="$rev_string r$r"
done

echo "Checking for duplicate bugs..."

check_duplicates=`$BUGZILLA_CMD query --blocked=$release_metabug --field="cf_fixed_by_commits=$rev_string"`

if [ -n "$check_duplicates" ]; then
  echo "Duplicate bug found:"
  echo $check_duplicates
  exit 1
fi

echo "Done"

# Get short commit summary.  To avoid having a huge summary, we just
# use the commit message for the first commit.
commit_summary=''
for r in $revisions; do
  commit_msg=`svn log -r $r https://llvm.org/svn/llvm-project/`
  if [ $? -ne 0 ]; then
    echo "warning: failed to get commit message."
    commit_msg=""
  fi
  break
done

if [ -n "$commit_msg" ]; then
  commit_summary=`echo "$commit_msg" | sed '4q;d' | cut -c1-80`
  commit_summary=" : ${commit_summary}"
fi

bug_summary="Merge${rev_string} into the $stable_version branch${commit_summary}"

set -x

# Login to bugzilla
$BUGZILLA_CMD login $bugzilla_user

bug_id=`${dryrun} $BUGZILLA_CMD --ensure-logged-in new \
  -p "$bugzilla_product" \
  -c "$bugzilla_component" --blocked=$release_metabug \
  -o All --priority=P --arch All -v $bugzilla_version \
  --field="cf_fixed_by_commits=$rev_string" \
  --summary "${bug_summary}" \
  -l "Is it OK to merge the following revision(s) to the $stable_version branch?" \
  $bugzilla_assigned_to \
  -i`

if [ -n "$dryrun" ]; then
  exit 0
fi

set +x

if [ -z "$bug_id" ]; then
  echo "Failed to create bug."
  exit 1
fi

echo " Created new bug:"
echo https://llvm.org/PR$bug_id

# Add links to revisions
for r in $revisions; do
  $BUGZILLA_CMD --ensure-logged-in modify -l "https://reviews.llvm.org/rL$r" $bug_id
done
