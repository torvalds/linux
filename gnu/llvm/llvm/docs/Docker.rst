=========================================
A guide to Dockerfiles for building LLVM
=========================================

Introduction
============
You can find a number of sources to build docker images with LLVM components in
``llvm/utils/docker``. They can be used by anyone who wants to build the docker
images for their own use, or as a starting point for someone who wants to write
their own Dockerfiles.

We currently provide Dockerfiles with ``debian10`` and ``nvidia-cuda`` base images.
We also provide an ``example`` image, which contains placeholders that one would need
to fill out in order to produce Dockerfiles for a new docker image.

Why?
----
Docker images provide a way to produce binary distributions of
software inside a controlled environment. Having Dockerfiles to builds docker images
inside LLVM repo makes them much more discoverable than putting them into any other
place.

Docker basics
-------------
If you've never heard about Docker before, you might find this section helpful
to get a very basic explanation of it.
`Docker <https://www.docker.com/>`_ is a popular solution for running programs in
an isolated and reproducible environment, especially to maintain releases for
software deployed to large distributed fleets.
It uses linux kernel namespaces and cgroups to provide a lightweight isolation
inside currently running linux kernel.
A single active instance of dockerized environment is called a *docker
container*.
A snapshot of a docker container filesystem is called a *docker image*.
One can start a container from a prebuilt docker image.

Docker images are built from a so-called *Dockerfile*, a source file written in
a specialized language that defines instructions to be used when build
the docker image (see `official
documentation <https://docs.docker.com/engine/reference/builder/>`_ for more
details). A minimal Dockerfile typically contains a base image and a number
of RUN commands that have to be executed to build the image. When building a new
image, docker will first download your base image, mount its filesystem as
read-only and then add a writable overlay on top of it to keep track of all
filesystem modifications, performed while building your image. When the build
process is finished, a diff between your image's final filesystem state and the
base image's filesystem is stored in the resulting image.

Overview
========
The ``llvm/utils/docker`` folder contains Dockerfiles and simple bash scripts to
serve as a basis for anyone who wants to create their own Docker image with
LLVM components, compiled from sources. The sources are checked out from the
upstream git repository when building the image.

The resulting image contains only the requested LLVM components and a few extra
packages to make the image minimally useful for C++ development, e.g. libstdc++
and binutils.

The interface to run the build is ``build_docker_image.sh`` script. It accepts a
list of LLVM repositories to checkout and arguments for CMake invocation.

If you want to write your own docker image, start with an ``example/`` subfolder.
It provides an incomplete Dockerfile with (very few) FIXMEs explaining the steps
you need to take in order to make your Dockerfiles functional.

Usage
=====
The ``llvm/utils/build_docker_image.sh`` script provides a rather high degree of
control on how to run the build. It allows you to specify the projects to
checkout from git and provide a list of CMake arguments to use during when
building LLVM inside docker container.

Here's a very simple example of getting a docker image with clang binary,
compiled by the system compiler in the debian10 image:

.. code-block:: bash

    ./llvm/utils/docker/build_docker_image.sh \
	--source debian10 \
	--docker-repository clang-debian10 --docker-tag "staging" \
	-p clang -i install-clang -i install-clang-resource-headers \
	-- \
	-DCMAKE_BUILD_TYPE=Release

Note that a build like that doesn't use a 2-stage build process that
you probably want for clang. Running a 2-stage build is a little more intricate,
this command will do that:

.. code-block:: bash

    # Run a 2-stage build.
    #   LLVM_TARGETS_TO_BUILD=Native is to reduce stage1 compile time.
    #   Options, starting with BOOTSTRAP_* are passed to stage2 cmake invocation.
    ./build_docker_image.sh \
	--source debian10 \
	--docker-repository clang-debian10 --docker-tag "staging" \
	-p clang -i stage2-install-clang -i stage2-install-clang-resource-headers \
	-- \
	-DLLVM_TARGETS_TO_BUILD=Native -DCMAKE_BUILD_TYPE=Release \
	-DBOOTSTRAP_CMAKE_BUILD_TYPE=Release \
	-DCLANG_ENABLE_BOOTSTRAP=ON -DCLANG_BOOTSTRAP_TARGETS="install-clang;install-clang-resource-headers"
	
This will produce a new image ``clang-debian10:staging`` from the latest
upstream revision.
After the image is built you can run bash inside a container based on your image
like this:

.. code-block:: bash

    docker run -ti clang-debian10:staging bash

Now you can run bash commands as you normally would:

.. code-block:: bash

    root@80f351b51825:/# clang -v
    clang version 5.0.0 (trunk 305064)
    Target: x86_64-unknown-linux-gnu
    Thread model: posix
    InstalledDir: /bin
    Found candidate GCC installation: /usr/lib/gcc/x86_64-linux-gnu/4.8
    Found candidate GCC installation: /usr/lib/gcc/x86_64-linux-gnu/4.8.4
    Found candidate GCC installation: /usr/lib/gcc/x86_64-linux-gnu/4.9
    Found candidate GCC installation: /usr/lib/gcc/x86_64-linux-gnu/4.9.2
    Selected GCC installation: /usr/lib/gcc/x86_64-linux-gnu/4.9
    Candidate multilib: .;@m64
    Selected multilib: .;@m64


Which image should I choose?
============================
We currently provide two images: Debian10-based and nvidia-cuda-based. They
differ in the base image that they use, i.e. they have a different set of
preinstalled binaries. Debian8 is very minimal, nvidia-cuda is larger, but has
preinstalled CUDA libraries and allows to access a GPU, installed on your
machine.

If you need a minimal linux distribution with only clang and libstdc++ included,
you should try Debian10-based image.

If you want to use CUDA libraries and have access to a GPU on your machine,
you should choose nvidia-cuda-based image and use `nvidia-docker
<https://github.com/NVIDIA/nvidia-docker>`_ to run your docker containers. Note
that you don't need nvidia-docker to build the images, but you need it in order
to have an access to GPU from a docker container that is running the built
image.

If you have a different use-case, you could create your own image based on
``example/`` folder.

Any docker image can be built and run using only the docker binary, i.e. you can
run debian10 build on Fedora or any other Linux distribution. You don't need to
install CMake, compilers or any other clang dependencies. It is all handled
during the build process inside Docker's isolated environment.

Stable build
============
If you want a somewhat recent and somewhat stable build, use the
``branches/google/stable`` branch, i.e. the following command will produce a
Debian10-based image using the latest ``google/stable`` sources for you:

.. code-block:: bash

    ./llvm/utils/docker/build_docker_image.sh \
	-s debian10 --d clang-debian10 -t "staging" \
	--branch branches/google/stable \
	-p clang -i install-clang -i install-clang-resource-headers \
	-- \
	-DCMAKE_BUILD_TYPE=Release


Minimizing docker image size
============================
Due to how Docker's filesystem works, all intermediate writes are persisted in
the resulting image, even if they are removed in the following commands.
To minimize the resulting image size we use `multi-stage Docker builds
<https://docs.docker.com/develop/develop-images/multistage-build/>`_.
Internally Docker builds two images. The first image does all the work: installs
build dependencies, checks out LLVM source code, compiles LLVM, etc.
The first image is only used during build and does not have a descriptive name,
i.e. it is only accessible via the hash value after the build is finished.
The second image is our resulting image. It contains only the built binaries
and not any build dependencies. It is also accessible via a descriptive name
(specified by -d and -t flags).
