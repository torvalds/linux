#!/bin/sh -e

# Sometime we might want to skip certain checkpatch.pl errors
# (e.g. when trying to preserve existing code style that checkpatch.pl
# complains about). In that case just add a new variable in the
# CircleCI project settings and rebuild. Don't forget to remove them
# after restarting the build.
if [ -n "$SKIP_CHECKPATCH" ]; then
    exit 0
fi

if [ -z "$origin_master" ]; then
    origin_master="origin/master"
fi

# make sure we fetch to avoid caching effects
git fetch origin

# find the last upstream tag to avoid checking upstream commits during
# upstream merges
tag=`git tag --sort='-*authordate' | grep ^v | head -n1`
tmp=`mktemp -d`

for c in `git log --no-merges --pretty=format:%h HEAD ^$origin_master ^$tag`; do
    git format-patch -1 -o $tmp $c
done

./scripts/checkpatch.pl --ignore FILE_PATH_CHANGES $tmp/*.patch
rm $tmp/*.patch

# checkpatch.pl does not know how to deal with 3 way diffs which would
# be useful to check the conflict resolutions during merges...
#for c in `git log --merges --pretty=format:%h HEAD ^$origin_master ^$tag`; do
#    git log --pretty=email $c -1 > $tmp/$c.patch
#    git diff $c $c^1 $c^2 >> $tmp/$c.patch
#done

rmdir $tmp

