#!/bin/sh -ex

if [ -z "$origin_master" ]; then
    origin_master="origin/master"
fi

git remote -v

UPSTREAM=git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git
LKL=github.com[:/]lkl/linux

upstream=`git remote -v | grep $UPSTREAM | cut -f1 | head -n1`
lkl=`git remote -v | grep $LKL | cut -f1 | head -n1`

if [ -z "$upstream" ]; then
    upstream=git://$UPSTREAM
fi

if [ -z "$lkl" ]; then
    echo "can't find lkl remote, quiting"
    exit 1
fi

git fetch $lkl
git fetch --tags $upstream

# find the last upstream tag to avoid checking upstream commits during
# upstream merges
tag=`git tag --sort='-*authordate' | grep ^v | head -n1`
tmp=`mktemp -d`

commits=$(git log --no-merges --pretty=format:%h HEAD ^$lkl/master ^$tag)
for c in $commits; do
    git format-patch -1 -o $tmp $c
done

if [ -z "$c" ]; then
    echo "there are not commits/patches to check, quiting."
    rmdir $tmp
    exit 0
fi

./scripts/checkpatch.pl --ignore FILE_PATH_CHANGES $tmp/*.patch
rm $tmp/*.patch

# checkpatch.pl does not know how to deal with 3 way diffs which would
# be useful to check the conflict resolutions during merges...
#for c in `git log --merges --pretty=format:%h HEAD ^$origin_master ^$tag`; do
#    git log --pretty=email $c -1 > $tmp/$c.patch
#    git diff $c $c^1 $c^2 >> $tmp/$c.patch
#done

rmdir $tmp

