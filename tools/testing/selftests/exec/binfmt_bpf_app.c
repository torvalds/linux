// SPDX-License-Identifier: GPL-2.0
/*
 * A relocatable binary for the binfmt_misc_bpf $ORIGIN case. The Makefile
 * links it with PT_INTERP set to the literal "$ORIGIN/binfmt_bpf_interp"
 * (-Wl,--dynamic-linker), which the kernel ELF loader cannot resolve. The
 * nix_origin bpf handler resolves it relative to this binary's directory and
 * routes execution to the co-located interpreter.
 */
int main(void)
{
	return 0;
}
