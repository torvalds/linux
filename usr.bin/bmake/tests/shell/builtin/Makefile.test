#
# In compat mode (the default without -j) a line that contains no shell
# meta characters and no shell builtins is not passed to the shell but
# executed directly. In our example the ls line without meta characters
# will really execute ls, while the line with meta characters will execute
# our special shell.
#
# $FreeBSD$

.SHELL: path="${.CURDIR}/sh"

.PHONY: builtin no-builtin

builtin:
	@exec ls -d .

no-builtin:
	@ls -d .
