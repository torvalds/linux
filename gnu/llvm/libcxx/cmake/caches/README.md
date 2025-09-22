# libc++ / libc++abi configuration caches

This directory contains CMake caches for the supported configurations of libc++.
Some of the configurations are specific to a vendor, others are generic and not
tied to any vendor.

While we won't explicitly work to break configurations not listed here, any
configuration not listed here is not explicitly supported. If you use or ship
libc++ under a configuration not listed here, you should work with the libc++
maintainers to make it into a supported configuration and add it here.

Similarly, adding any new configuration that's not already covered must be
discussed with the libc++ maintainers as it entails a maintenance burden.
