# SPDX-License-Identifier: GPL-2.0-only

obj-$(CONFIG_CRYPTO_CHACHA20_NEON) += chacha-neon.o
chacha-neon-y := chacha-neon-core.o chacha-neon-glue.o

obj-$(CONFIG_CRYPTO_POLY1305_NEON) += poly1305-neon.o
poly1305-neon-y := poly1305-core.o poly1305-glue.o
AFLAGS_poly1305-core.o += -Dpoly1305_init=poly1305_block_init_arch
AFLAGS_poly1305-core.o += -Dpoly1305_emit=poly1305_emit_arch

quiet_cmd_perlasm = PERLASM $@
      cmd_perlasm = $(PERL) $(<) void $(@)

$(obj)/%-core.S: $(src)/%-armv8.pl
	$(call cmd,perlasm)

clean-files += poly1305-core.S
