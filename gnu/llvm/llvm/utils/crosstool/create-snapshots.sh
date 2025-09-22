#!/bin/bash
#
# Creates LLVM SVN snapshots: llvm-$REV.tar.bz2 and llvm-gcc-4.2-$REV.tar.bz2,
# where $REV is an SVN revision of LLVM.  This is used for creating stable
# tarballs which can be used to build known-to-work crosstools.
#
# Syntax:
#   $0 [REV] -- grabs the revision $REV from SVN; if not specified, grabs the
#   latest SVN revision.

set -o nounset
set -o errexit

readonly LLVM_PROJECT_SVN="http://llvm.org/svn/llvm-project"

getLatestRevisionFromSVN() {
  svn info ${LLVM_PROJECT_SVN} | egrep ^Revision | sed 's/^Revision: //'
}

readonly REV="${1:-$(getLatestRevisionFromSVN)}"

createTarballFromSVN() {
  local module=$1
  local log="${module}.log"
  echo "Running: svn export -r ${REV} ${module}; log in ${log}"
  svn -q export -r ${REV} ${LLVM_PROJECT_SVN}/${module}/trunk \
      ${module} > ${log} 2>&1

  # Create "module-revision.tar.bz2" packages from the SVN checkout dirs.
  local tarball="${module}-${REV}.tar.bz2"
  echo "Creating tarball: ${tarball}"
  tar cjf ${tarball} ${module}

  echo "Cleaning up '${module}'"
  rm -rf ${module} ${log}
}

for module in "llvm" "llvm-gcc-4.2"; do
  createTarballFromSVN ${module}
done

