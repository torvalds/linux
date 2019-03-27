This directory contains scripts and files to package libsodium for .NET Core.

*Note:* The NuGet package is intended for the implementation of language
bindings such as [NSec](https://github.com/ektrah/nsec). It does not provide a
.NET API itself.

In .NET Core, it is customary to provide pre-compiled binaries for all platforms
as NuGet packages. The purpose of the `prepare.py` script in this directory is
to generate a `Makefile` that downloads and builds libsodium binaries for a
number of platforms and assembles them in a NuGet package that can be uploaded
to [nuget.org](https://nuget.org/).

* For Windows, binaries are obtained from
  [download.libsodium.org](https://download.libsodium.org/libsodium/releases/).
* For macOS, binaries are extracted from the
  [Homebrew libsodium bottle](https://bintray.com/homebrew/bottles/libsodium).
* For Linux, libsodium is compiled in Docker containers.

See `prepare.py` for the complete list of supported platforms.

The metadata for the NuGet package is located in `libsodium.props`.


**Versioning**

Version numbers for the packages for .NET Core consist of three components:

* *libsodium version*  
  The libsodium version is in the format `X.Y.Z`.
* *package revision*  
  It may be necessary to release more than one package for a libsodium version,
  e.g., when adding support for a new platform or if a release contains a broken
  binary. In this case, a package revision number is added as a fourth part to
  the libsodium version, starting at `1`. For example, `1.0.16` is the initial
  release of the package for libsodium 1.0.16 and `1.0.16.5` is the fifth
  revision (sixth release) of that package.
* *pre-release label*  
  If a package is a pre-release, a label is appended to the version number in
  `-preview-##` format where `##` is the number of the pre-release, starting at
  `01`. For example, `1.0.16-preview-01` is the first pre-release of the package
  for libsodium 1.0.16 and `1.0.16.5-preview-02` the second pre-release of the
  fifth revision of the package for libsodium 1.0.16.


**Making a release**

1. Update any existing Docker images.
2. Run `python3 prepare.py <version>` to generate the `Makefile`, where
   `<version>` is the package version number in the format described above.
3. Take a look at the generated `Makefile`. It uses `sudo` a few times.
4. Run `make` to download and build the binaries and create the NuGet package.
   You may need to install `docker`, `make`, `curl`, `tar` and `unzip` first.
5. Grab a cup of coffee. Downloading the Docker images and compiling the Linux
   binaries takes a while. When done, the NuGet package is output as a `.nupkg`
   file in the `build` directory.
6. Run `make test` to perform a quick test of the NuGet package. Verify that
   everything else in the `.nupkg` file is in place.
7. Publish the release by uploading the `.nupkg` file to
   [nuget.org](https://nuget.org/).
