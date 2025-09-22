#!/bin/sh

usage() {
  echo "usage: $0 <source root>"
  echo "  Prints the source control revision of the given source directory,"
  echo "  the exact format of the revision string depends on the source "
  echo "  control system. If the source control system isn't known, the output"
  echo "  is empty and the exit code is 1."
  exit 1
}

if [ $# != 1 ] || [ ! -d $1 ]; then
  usage;
fi

cd $1
if [ -d .svn ]; then
  svnversion | sed -e "s#\([0-9]*\)[A-Z]*#\1#"
elif [ -f .git/svn/.metadata ]; then
  git svn info | grep 'Revision:' | cut -d: -f2-
elif [ -d .git ]; then
  git log -1 --pretty=format:%H
else
  exit 1;
fi

exit 0
