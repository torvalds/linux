###
# scripts contains sources for various helper programs used throughout
# the kernel for the build process.
# ---------------------------------------------------------------------------
# kallsyms:      Find all symbols in vmlinux
# pnmttologo:    Convert pnm files to logo files
# conmakehash:   Create chartable
# conmakehash:	 Create arrays for initializing the kernel console tables

hostprogs-$(CONFIG_KALLSYMS)     += kallsyms
hostprogs-$(CONFIG_LOGO)         += pnmtologo
hostprogs-$(CONFIG_VT)           += conmakehash
hostprogs-$(CONFIG_PROM_CONSOLE) += conmakehash
hostprogs-$(CONFIG_IKCONFIG)     += bin2c

always		:= $(hostprogs-y)

subdir-$(CONFIG_MODVERSIONS) += genksyms
subdir-$(CONFIG_MODULES)     += mod

# Let clean descend into subdirs
subdir-	+= basic kconfig package
