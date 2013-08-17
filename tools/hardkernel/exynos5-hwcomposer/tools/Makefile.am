bin_PROGRAMS = exynos5-hwcomposer

AM_CXXFLAGS = -fPIC -Wno-unused-result \
	      -I$(top_srcdir)/include \
	      -I$(top_srcdir)/src/hardware/libhardware/include \
	      -I$(top_srcdir)/src/hardware/libhardware_legacy/include \
	      -I$(top_srcdir)/src/hardware/samsung_slsi/exynos/include \
	      -I$(top_srcdir)/src/hardware/samsung_slsi/exynos5/include \
	      -I$(top_srcdir)/src/hardware/samsung_slsi/exynos5410 \
	      -I$(top_srcdir)/src/hardware/samsung_slsi/exynos5410/include \
	      -I$(top_srcdir)/src/hardware/samsung_slsi/exynos/libexynosutils \
	      -I$(top_srcdir)/src/frameworks/native/include \
	      -I$(top_srcdir)/src/system/core/include

AM_LDFLAGS = -fPIC \
              -L$(top_builddir)/src/.libs -lpthread -lhwcomposer
 
exynos5_hwcomposer_SOURCES = exynos5-hwcomposer.cpp
