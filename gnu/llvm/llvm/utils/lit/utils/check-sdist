#!/bin/sh

if [ $# = 1 ]; then
    cd $1
fi

# Create a list of all the files in the source tree, excluding various things we
# know don't belong.
echo "Creating current directory contents list."
find . | \
    grep -v '^\./.gitignore' | \
    grep -v '^\./dist' | \
    grep -v '^\./utils' | \
    grep -v '^\./venv' | \
    grep -v '^\./notes.txt' | \
    grep -v '^\./lit.egg-info' | \
    grep -v '^\./lit/ExampleTests' | \
    grep -v '/Output' | \
    grep -v '__pycache__' | \
    grep -v '.pyc$' | grep -v '~$' | \
    sort > /tmp/lit_source_files.txt

# Create the source distribution.
echo "Creating source distribution."
rm -rf lit.egg-info dist
python setup.py sdist > /tmp/lit_sdist_log.txt

# Creating list of files in source distribution.
echo "Creating source distribution file list."
tar zft dist/lit*.tar.gz | \
    sed -e 's#lit-[0-9.dev]*/#./#' | \
    sed -e 's#/$##' | \
    grep -v '^\./PKG-INFO' | \
    grep -v '^\./setup.cfg' | \
    grep -v '^\./lit.egg-info' | \
    sort > /tmp/lit_sdist_files.txt

# Diff the files.
echo "Running diff..."
if (diff /tmp/lit_source_files.txt /tmp/lit_sdist_files.txt); then
    echo "Diff is clean!"
else
    echo "error: there were differences in the source lists!"
    exit 1
fi
