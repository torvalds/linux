#!/usr/bin/env bash

set -e

version="$(dpkg-parsechangelog -S Version | sed 's/-76/*76/g')"
sudo dpkg -i \
    ../linux-generic_${version}_amd64.deb \
    ../linux-headers-*_${version}_*.deb \
    ../linux-image-*_${version}_amd64.deb \
    ../linux-libc-dev_${version}_amd64.deb \
    ../linux-modules-*_${version}_amd64.deb \
    ../linux-system76_${version}_amd64.deb \
    ../linux-tools-*_${version}_amd64.deb \
    ../linux-tools-common_${version}_all.deb
