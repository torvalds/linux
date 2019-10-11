#!/usr/bin/env bash

set -e

if [ -n "$(git status --porcelain)" ]
then
    echo "ERROR: uncommitted changes"
    exit 1
fi

package="$(dpkg-parsechangelog --file "debian.master/changelog" --show-field Source)"
version="$(dpkg-parsechangelog --file "debian.master/changelog" --show-field Version)"

linux_version="$(echo "${version}" | cut -d "-" -f1)"
debian_version="$(echo "${version}" | cut -d "-" -f2-)"

if [[ "${debian_version}" == "76"* ]]
then
    echo "${package} ${version} already updated for system76"
else
    new_version="${linux_version}-76${debian_version}"
    sed -i "s/${package} (${version})/${package} (${new_version})/" "debian.master/changelog"
    dch --changelog "debian.master/changelog" --release 'Release for System76'
fi

fakeroot debian/rules clean

git add .
git commit -s -m "DROP ON REBASE: ${new_version} based on ${version}"
