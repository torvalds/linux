======================
Clang Offload Packager
======================

.. contents::
   :local:

.. _clang-offload-packager:

Introduction
============

This tool bundles device files into a single image containing necessary
metadata. We use a custom binary format for bundling all the device images
together. The image format is a small header wrapping around a string map. This
tool creates bundled binaries so that they can be embedded into the host to
create a fat-binary.

Binary Format
=============

The binary format is marked by the ``0x10FF10AD`` magic bytes, followed by a
version. Each created binary contains its own magic bytes. This allows us to
locate all the embedded offloading sections even after they may have been merged
by the linker, such as when using relocatable linking. Conceptually, this binary
format is a serialization of a string map and an image buffer. The binary header
is described in the following :ref:`table<table-binary_header>`.

.. table:: Offloading Binary Header
    :name: table-binary_header

    +----------+--------------+----------------------------------------------------+
    |   Type   |  Identifier  | Description                                        |
    +==========+==============+====================================================+
    | uint8_t  |    magic     | The magic bytes for the binary format (0x10FF10AD) |
    +----------+--------------+----------------------------------------------------+
    | uint32_t |   version    | Version of this format (currently version 1)       |
    +----------+--------------+----------------------------------------------------+
    | uint64_t |    size      | Size of this binary in bytes                       |
    +----------+--------------+----------------------------------------------------+
    | uint64_t | entry offset | Absolute offset of the offload entries in bytes    |
    +----------+--------------+----------------------------------------------------+
    | uint64_t |  entry size  | Size of the offload entries in bytes               |
    +----------+--------------+----------------------------------------------------+

Once identified through the magic bytes, we use the size field to take a slice
of the binary blob containing the information for a single offloading image. We
can then use the offset field to find the actual offloading entries containing
the image and metadata. The offload entry contains information about the device
image. It contains the fields shown in the following
:ref:`table<table-binary_entry>`.

.. table:: Offloading Entry Table
    :name: table-binary_entry

    +----------+---------------+----------------------------------------------------+
    |   Type   |   Identifier  | Description                                        |
    +==========+===============+====================================================+
    | uint16_t |  image kind   | The kind of the device image (e.g. bc, cubin)      |
    +----------+---------------+----------------------------------------------------+
    | uint16_t | offload kind  | The producer of the image (e.g. openmp, cuda)      |
    +----------+---------------+----------------------------------------------------+
    | uint32_t |     flags     | Generic flags for the image                        |
    +----------+---------------+----------------------------------------------------+
    | uint64_t | string offset | Absolute offset of the string metadata table       |
    +----------+---------------+----------------------------------------------------+
    | uint64_t |  num strings  | Number of string entries in the table              |
    +----------+---------------+----------------------------------------------------+
    | uint64_t |  image offset | Absolute offset of the device image in bytes       |
    +----------+---------------+----------------------------------------------------+
    | uint64_t |   image size  | Size of the device image in bytes                  |
    +----------+---------------+----------------------------------------------------+

This table contains the offsets of the string table and the device image itself
along with some other integer information. The image kind lets us easily
identify the type of image stored here without needing to inspect the binary.
The offloading kind is used to determine which registration code or linking
semantics are necessary for this image. These are stored as enumerations with
the following values for the :ref:`offload kind<table-offload_kind>` and the
:ref:`image kind<table-image_kind>`.

.. table:: Image Kind
    :name: table-image_kind

    +---------------+-------+---------------------------------------+
    |      Name     | Value | Description                           |
    +===============+=======+=======================================+
    | IMG_None      | 0x00  | No image information provided         |
    +---------------+-------+---------------------------------------+
    | IMG_Object    | 0x01  | The image is a generic object file    |
    +---------------+-------+---------------------------------------+
    | IMG_Bitcode   | 0x02  | The image is an LLVM-IR bitcode file  |
    +---------------+-------+---------------------------------------+
    | IMG_Cubin     | 0x03  | The image is a CUDA object file       |
    +---------------+-------+---------------------------------------+
    | IMG_Fatbinary | 0x04  | The image is a CUDA fatbinary file    |
    +---------------+-------+---------------------------------------+
    | IMG_PTX       | 0x05  | The image is a CUDA PTX file          |
    +---------------+-------+---------------------------------------+

.. table:: Offload Kind
    :name: table-offload_kind

    +------------+-------+---------------------------------------+
    |      Name  | Value | Description                           |
    +============+=======+=======================================+
    | OFK_None   | 0x00  | No offloading information provided    |
    +------------+-------+---------------------------------------+
    | OFK_OpenMP | 0x01  | The producer was OpenMP offloading    |
    +------------+-------+---------------------------------------+
    | OFK_CUDA   | 0x02  | The producer was CUDA                 |
    +------------+-------+---------------------------------------+
    | OFK_HIP    | 0x03  | The producer was HIP                  |
    +------------+-------+---------------------------------------+

The flags are used to signify certain conditions, such as the presence of
debugging information or whether or not LTO was used. The string entry table is
used to generically contain any arbitrary key-value pair. This is stored as an
array of the :ref:`string entry<table-binary_string>` format.

.. table:: Offloading String Entry
    :name: table-binary_string

    +----------+--------------+-------------------------------------------------------+
    |   Type   |   Identifier | Description                                           |
    +==========+==============+=======================================================+
    | uint64_t |  key offset  | Absolute byte offset of the key in the string table   |
    +----------+--------------+-------------------------------------------------------+
    | uint64_t | value offset | Absolute byte offset of the value in the string table |
    +----------+--------------+-------------------------------------------------------+

The string entries simply provide offsets to a key and value pair in the
binary images string table. The string table is simply a collection of null
terminated strings with defined offsets in the image. The string entry allows us
to create a key-value pair from this string table. This is used for passing
arbitrary arguments to the image, such as the triple and architecture.

All of these structures are combined to form a single binary blob, the order
does not matter because of the use of absolute offsets. This makes it easier to
extend in the future. As mentioned previously, multiple offloading images are
bundled together by simply concatenating them in this format. Because we have
the magic bytes and size of each image, we can extract them as-needed.

Usage
=====

This tool can be used with the following arguments. Generally information is
passed as a key-value pair to the ``image=`` argument. The ``file`` and
``triple``, arguments are considered mandatory to make a valid image.
The ``arch`` argument is suggested.

.. code-block:: console

  OVERVIEW: A utility for bundling several object files into a single binary.
  The output binary can then be embedded into the host section table
  to create a fatbinary containing offloading code.

  USAGE: clang-offload-packager [options]

  OPTIONS:

  Generic Options:

    --help                      - Display available options (--help-hidden for more)
    --help-list                 - Display list of available options (--help-list-hidden for more)
    --version                   - Display the version of this program

  clang-offload-packager options:

    --image=<<key>=<value>,...> - List of key and value arguments. Required
                                  keywords are 'file' and 'triple'.
    -o <file>                   - Write output to <file>.

Example
=======

This tool simply takes many input files from the ``image`` option and creates a
single output file with all the images combined.

.. code-block:: console

  clang-offload-packager -o out.bin --image=file=input.o,triple=nvptx64,arch=sm_70

The inverse operation can be performed instead by passing the packaged binary as
input. In this mode the matching images will either be placed in the output
specified by the ``file`` option. If no ``file`` argument is provided a name
will be generated for each matching image.

.. code-block:: console

  clang-offload-packager in.bin --image=file=output.o,triple=nvptx64,arch=sm_70
