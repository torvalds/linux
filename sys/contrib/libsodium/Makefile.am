ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = \
	autogen.sh \
	libsodium.sln \
	libsodium.vcxproj \
	libsodium.vcxproj.filters \
	LICENSE \
	README.markdown \
	THANKS

SUBDIRS = \
	builds \
	contrib \
	dist-build \
	msvc-scripts \
	src \
	test

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = @PACKAGE_NAME@.pc

DISTCLEANFILES = $(pkgconfig_DATA)

