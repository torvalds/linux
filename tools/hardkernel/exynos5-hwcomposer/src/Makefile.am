lib_LTLIBRARIES = libhwcomposer.la

AM_CFLAGS = -fPIC -Wno-unused-result \
	    -I$(top_srcdir)/include \
	    -DHAVE_PTHREADS \
	    -Ihardware/libhardware/include \
	    -Ihardware/libhardware_legacy/include \
	    -Ihardware/samsung_slsi/exynos/include \
	    -Ihardware/samsung_slsi/exynos5/include \
	    -Ihardware/samsung_slsi/exynos/libexynosutils \
	    -Iframeworks/native/include \
	    -Isystem/core/include
AM_CXXFLAGS = -fPIC -Wno-unused-result \
	      -I$(top_srcdir)/include \
	      -DHAVE_PTHREADS \
	      -Ihardware/libhardware/include \
	      -Ihardware/libhardware_legacy/include \
	      -Ihardware/samsung_slsi/exynos/include \
	      -Ihardware/samsung_slsi/exynos5/include \
	      -Ihardware/samsung_slsi/exynos/libexynosutils \
	      -Iframeworks/native/include \
	      -Isystem/core/include
AM_LDFLAGS = -fPIC -lpthread

libhwcomposer_la_SOURCES = \
	hardware/samsung_slsi/exynos5/libgscaler/exynos_gscaler.c \
	hardware/samsung_slsi/exynos/libexynosutils/ExynosMutex.cpp \
	hardware/samsung_slsi/exynos/libexynosutils/exynos5_format_v4l2.c \
	hardware/samsung_slsi/exynos/libv4l2/exynos_v4l2.c \
	hardware/samsung_slsi/exynos/libv4l2/exynos_mc.c \
	hardware/samsung_slsi/exynos/libv4l2/exynos_subdev.c \
	hardware/libhardware_legacy/uevent/uevent.c \
	system/core/libsync/sync.c
