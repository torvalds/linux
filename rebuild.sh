#!/usr/bin/env bash

set -ex

if ! diff -u debian/changelog debian.master/changelog
then
    fakeroot debian/rules clean
fi

dh_clean

rm -rf debian/build/build-generic/_____________________________________dkms/

time debuild --no-lintian -b -nc -uc -us
