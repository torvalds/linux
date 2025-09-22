#!/bin/bash
#===- llvm/utils/docker/build_docker_image.sh ----------------------------===//
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===//
set -e

IMAGE_SOURCE=""
DOCKER_REPOSITORY=""
DOCKER_TAG=""
BUILDSCRIPT_ARGS=""
CHECKOUT_ARGS=""
CMAKE_ENABLED_PROJECTS=""

function show_usage() {
  cat << EOF
Usage: build_docker_image.sh [options] [-- [cmake_args]...]

Available options:
  General:
    -h|--help               show this help message
  Docker-specific:
    -s|--source             image source dir (i.e. debian10, nvidia-cuda, etc)
    -d|--docker-repository  docker repository for the image
    -t|--docker-tag         docker tag for the image
  Checkout arguments:
    -b|--branch         git branch to checkout, i.e. 'main',
                        'release/10.x'
                        (default: 'main')
    -r|--revision       git revision to checkout
    -c|--cherrypick     revision to cherry-pick. Can be specified multiple times.
                        Cherry-picks are performed in the sorted order using the
                        following command:
                        'git cherry-pick \$rev'.
    -p|--llvm-project   Add the project to a list LLVM_ENABLE_PROJECTS, passed to
                        CMake.
                        Can be specified multiple times.
    -c|--checksums      name of a file, containing checksums of llvm checkout.
                        Script will fail if checksums of the checkout do not
                        match.
  Build-specific:
    -i|--install-target name of a cmake install target to build and include in
                        the resulting archive. Can be specified multiple times.

Required options: --source and --docker-repository, at least one
  --install-target.

All options after '--' are passed to CMake invocation.

For example, running:
$ build_docker_image.sh -s debian10 -d mydocker/debian10-clang -t latest \
  -p clang -i install-clang -i install-clang-resource-headers
will produce two docker images:
    mydocker/debian10-clang-build:latest - an intermediate image used to compile
      clang.
    mydocker/clang-debian10:latest       - a small image with preinstalled clang.
Please note that this example produces a not very useful installation, since it
doesn't override CMake defaults, which produces a Debug and non-boostrapped
version of clang.

To get a 2-stage clang build, you could use this command:
$ ./build_docker_image.sh -s debian10 -d mydocker/clang-debian10 -t "latest" \
    -p clang -i stage2-install-clang -i stage2-install-clang-resource-headers \ 
    -- \ 
    -DLLVM_TARGETS_TO_BUILD=Native -DCMAKE_BUILD_TYPE=Release \ 
    -DBOOTSTRAP_CMAKE_BUILD_TYPE=Release \ 
    -DCLANG_ENABLE_BOOTSTRAP=ON \ 
    -DCLANG_BOOTSTRAP_TARGETS="install-clang;install-clang-resource-headers"
EOF
}

CHECKSUMS_FILE=""
SEEN_INSTALL_TARGET=0
SEEN_CMAKE_ARGS=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      show_usage
      exit 0
      ;;
    -s|--source)
      shift
      IMAGE_SOURCE="$1"
      shift
      ;;
    -d|--docker-repository)
      shift
      DOCKER_REPOSITORY="$1"
      shift
      ;;
    -t|--docker-tag)
      shift
      DOCKER_TAG="$1"
      shift
      ;;
    -r|--revision|-c|--cherrypick|-b|--branch)
      CHECKOUT_ARGS="$CHECKOUT_ARGS $1 $2"
      shift 2
      ;;
    -i|--install-target)
      SEEN_INSTALL_TARGET=1
      BUILDSCRIPT_ARGS="$BUILDSCRIPT_ARGS $1 $2"
      shift 2
      ;;
    -p|--llvm-project)
      PROJ="$2"
      CMAKE_ENABLED_PROJECTS="$CMAKE_ENABLED_PROJECTS;$PROJ"
      shift 2
      ;;
    -c|--checksums)
      shift
      CHECKSUMS_FILE="$1"
      shift
      ;;
    --)
      shift
      BUILDSCRIPT_ARGS="$BUILDSCRIPT_ARGS -- $*"
      SEEN_CMAKE_ARGS=1
      shift $#
      ;;
    *)
      echo "Unknown argument $1"
      exit 1
      ;;
  esac
done


if [ "$CMAKE_ENABLED_PROJECTS" != "" ]; then
  # Remove the leading ';' character.
  CMAKE_ENABLED_PROJECTS="${CMAKE_ENABLED_PROJECTS:1}"

  if [[ $SEEN_CMAKE_ARGS -eq 0 ]]; then
    BUILDSCRIPT_ARGS="$BUILDSCRIPT_ARGS --"
  fi
  BUILDSCRIPT_ARGS="$BUILDSCRIPT_ARGS -DLLVM_ENABLE_PROJECTS=$CMAKE_ENABLED_PROJECTS"
fi

command -v docker >/dev/null ||
  {
    echo "Docker binary cannot be found. Please install Docker to use this script."
    exit 1
  }

if [ "$IMAGE_SOURCE" == "" ]; then
  echo "Required argument missing: --source"
  exit 1
fi

if [ "$DOCKER_REPOSITORY" == "" ]; then
  echo "Required argument missing: --docker-repository"
  exit 1
fi

if [ $SEEN_INSTALL_TARGET -eq 0 ]; then
  echo "Please provide at least one --install-target"
  exit 1
fi

SOURCE_DIR=$(dirname $0)
if [ ! -d "$SOURCE_DIR/$IMAGE_SOURCE" ]; then
  echo "No sources for '$IMAGE_SOURCE' were found in $SOURCE_DIR"
  exit 1
fi

BUILD_DIR=$(mktemp -d)
trap "rm -rf $BUILD_DIR" EXIT
echo "Using a temporary directory for the build: $BUILD_DIR"

cp -r "$SOURCE_DIR/$IMAGE_SOURCE" "$BUILD_DIR/$IMAGE_SOURCE"
cp -r "$SOURCE_DIR/scripts" "$BUILD_DIR/scripts"

mkdir "$BUILD_DIR/checksums"
if [ "$CHECKSUMS_FILE" != "" ]; then
  cp "$CHECKSUMS_FILE" "$BUILD_DIR/checksums/checksums.txt"
fi

if [ "$DOCKER_TAG" != "" ]; then
  DOCKER_TAG=":$DOCKER_TAG"
fi

echo "Building ${DOCKER_REPOSITORY}${DOCKER_TAG} from $IMAGE_SOURCE"
docker build -t "${DOCKER_REPOSITORY}${DOCKER_TAG}" \
  --build-arg "checkout_args=$CHECKOUT_ARGS" \
  --build-arg "buildscript_args=$BUILDSCRIPT_ARGS" \
  -f "$BUILD_DIR/$IMAGE_SOURCE/Dockerfile" \
  "$BUILD_DIR"
echo "Done"
