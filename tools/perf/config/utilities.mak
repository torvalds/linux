# This allows us to work with the newline character:
define newline


endef
newline := $(newline)

# nl-escape
#
# Usage: escape = $(call nl-escape[,escape])
#
# This is used as the common way to specify
# what should replace a newline when escaping
# newlines; the default is a bizarre string.
#
nl-escape = $(or $(1),m822df3020w6a44id34bt574ctac44eb9f4n)

# escape-nl
#
# Usage: escaped-text = $(call escape-nl,text[,escape])
#
# GNU make's $(shell ...) function converts to a
# single space each newline character in the output
# produced during the expansion; this may not be
# desirable.
#
# The only solution is to change each newline into
# something that won't be converted, so that the
# information can be recovered later with
# $(call unescape-nl...)
#
escape-nl = $(subst $(newline),$(call nl-escape,$(2)),$(1))

# unescape-nl
#
# Usage: text = $(call unescape-nl,escaped-text[,escape])
#
# See escape-nl.
#
unescape-nl = $(subst $(call nl-escape,$(2)),$(newline),$(1))

# shell-escape-nl
#
# Usage: $(shell some-command | $(call shell-escape-nl[,escape]))
#
# Use this to escape newlines from within a shell call;
# the default escape is a bizarre string.
#
# NOTE: The escape is used directly as a string constant
#       in an `awk' program that is delimited by shell
#       single-quotes, so be wary of the characters
#       that are chosen.
#
define shell-escape-nl
awk 'NR==1 {t=$$0} NR>1 {t=t "$(nl-escape)" $$0} END {printf t}'
endef

# shell-unescape-nl
#
# Usage: $(shell some-command | $(call shell-unescape-nl[,escape]))
#
# Use this to unescape newlines from within a shell call;
# the default escape is a bizarre string.
#
# NOTE: The escape is used directly as an extended regular
#       expression constant in an `awk' program that is
#       delimited by shell single-quotes, so be wary
#       of the characters that are chosen.
#
# (The bash shell has a bug where `{gsub(...),...}' is
#  misinterpreted as a brace expansion; this can be
#  overcome by putting a space between `{' and `gsub').
#
define shell-unescape-nl
awk 'NR==1 {t=$$0} NR>1 {t=t "\n" $$0} END { gsub(/$(nl-escape)/,"\n",t); printf t }'
endef

# escape-for-shell-sq
#
# Usage: embeddable-text = $(call escape-for-shell-sq,text)
#
# This function produces text that is suitable for
# embedding in a shell string that is delimited by
# single-quotes.
#
escape-for-shell-sq =  $(subst ','\'',$(1))

# shell-sq
#
# Usage: single-quoted-and-escaped-text = $(call shell-sq,text)
#
shell-sq = '$(escape-for-shell-sq)'

# shell-wordify
#
# Usage: wordified-text = $(call shell-wordify,text)
#
# For instance:
#
#  |define text
#  |hello
#  |world
#  |endef
#  |
#  |target:
#  |	echo $(call shell-wordify,$(text))
#
# At least GNU make gets confused by expanding a newline
# within the context of a command line of a makefile rule
# (this is in constrast to a `$(shell ...)' function call,
# which can handle it just fine).
#
# This function avoids the problem by producing a string
# that works as a shell word, regardless of whether or
# not it contains a newline.
#
# If the text to be wordified contains a newline, then
# an intrictate shell command substitution is constructed
# to render the text as a single line; when the shell
# processes the resulting escaped text, it transforms
# it into the original unescaped text.
#
# If the text does not contain a newline, then this function
# produces the same results as the `$(shell-sq)' function.
#
shell-wordify = $(if $(findstring $(newline),$(1)),$(_sw-esc-nl),$(shell-sq))
define _sw-esc-nl
"$$(echo $(call escape-nl,$(shell-sq),$(2)) | $(call shell-unescape-nl,$(2)))"
endef

# is-absolute
#
# Usage: bool-value = $(call is-absolute,path)
#
is-absolute = $(shell echo $(shell-sq) | grep ^/ -q && echo y)

# lookup
#
# Usage: absolute-executable-path-or-empty = $(call lookup,path)
#
# (It's necessary to use `sh -c' because GNU make messes up by
#  trying too hard and getting things wrong).
#
lookup = $(call unescape-nl,$(shell sh -c $(_l-sh)))
_l-sh = $(call shell-sq,command -v $(shell-sq) | $(call shell-escape-nl,))

# is-executable
#
# Usage: bool-value = $(call is-executable,path)
#
# (It's necessary to use `sh -c' because GNU make messes up by
#  trying too hard and getting things wrong).
#
is-executable = $(call _is-executable-helper,$(shell-sq))
_is-executable-helper = $(shell sh -c $(_is-executable-sh))
_is-executable-sh = $(call shell-sq,test -f $(1) -a -x $(1) && echo y)

# get-executable
#
# Usage: absolute-executable-path-or-empty = $(call get-executable,path)
#
# The goal is to get an absolute path for an executable;
# the `command -v' is defined by POSIX, but it's not
# necessarily very portable, so it's only used if
# relative path resolution is requested, as determined
# by the presence of a leading `/'.
#
get-executable = $(if $(1),$(if $(is-absolute),$(_ge-abspath),$(lookup)))
_ge-abspath = $(if $(is-executable),$(1))

# get-supplied-or-default-executable
#
# Usage: absolute-executable-path-or-empty = $(call get-executable-or-default,variable,default)
#
define get-executable-or-default
$(if $($(1)),$(call _ge_attempt,$($(1)),$(1)),$(call _ge_attempt,$(2)))
endef
_ge_attempt = $(or $(get-executable),$(_gea_warn),$(call _gea_err,$(2)))
_gea_warn = $(warning The path '$(1)' is not executable.)
_gea_err  = $(if $(1),$(error Please set '$(1)' appropriately))

# try-cc
# Usage: option = $(call try-cc, source-to-build, cc-options, msg)
ifndef V
TRY_CC_OUTPUT= > /dev/null 2>&1
endif
TRY_CC_MSG=echo "    CHK $(3)" 1>&2;

try-cc = $(shell sh -c						  \
	'TMP="$(OUTPUT)$(TMPOUT).$$$$";				  \
	 $(TRY_CC_MSG)						  \
	 echo "$(1)" |						  \
	 $(CC) -x c - $(2) -o "$$TMP" $(TRY_CC_OUTPUT) && echo y; \
	 rm -f "$$TMP"')
