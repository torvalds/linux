#!/bin/sh

# Remove the phabricator tags from the commit at HEAD
git log -1 --pretty=%B | \
  sed  's/^Summary://'  | \
  awk '/Reviewers:|Subscribers:/{p=1} /Reviewed By:|Differential Revision:/{p=0} !p && !/^Summary:$/ {sub(/^Summary: /,"");print}' | \
  git commit --amend --date=now -F -
