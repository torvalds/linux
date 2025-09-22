import os
import re

import lit.util

expr = re.compile(r"^(\\)?((\| )?)\W+b(\S+)\\b\W*$")
wordifier = re.compile(r"(\W*)(\b[^\b]+\b)")


class FindTool(object):
    def __init__(self, name):
        self.name = name

    def resolve(self, config, dirs):
        # Check for a user explicitly overriding a tool. This allows:
        #     llvm-lit -D llc="llc -enable-misched -verify-machineinstrs"
        command = config.lit_config.params.get(self.name)
        if command is None:
            # Then check out search paths.
            command = lit.util.which(self.name, dirs)
            if not command:
                return None

        if self.name == "llc" and os.environ.get("LLVM_ENABLE_MACHINE_VERIFIER") == "1":
            command += " -verify-machineinstrs"
        return command


class ToolSubst(object):
    """String-like class used to build regex substitution patterns for llvm
    tools.

    Handles things like adding word-boundary patterns, and filtering
    characters from the beginning an end of a tool name

    """

    def __init__(
        self,
        key,
        command=None,
        pre=r".-^/\<",
        post="-.",
        verbatim=False,
        unresolved="warn",
        extra_args=None,
    ):
        """Construct a ToolSubst.

        key: The text which is to be substituted.

        command: The command to substitute when the key is matched. By default,
        this will treat `key` as a tool name and search for it. If it is a
        string, it is interpreted as an exact path. If it is an instance of
        FindTool, the specified tool name is searched for on disk.

        pre: If specified, the substitution will not find matches where
        the character immediately preceding the word-boundary that begins
        `key` is any of the characters in the string `pre`.

        post: If specified, the substitution will not find matches where
        the character immediately after the word-boundary that ends `key`
        is any of the characters specified in the string `post`.

        verbatim: If True, `key` is an exact regex that is passed to the
        underlying substitution

        unresolved: Action to take if the tool substitution cannot be
        resolved. Valid values:
            'warn' - log a warning but add the substitution anyway.
            'fatal' - Exit the test suite and log a fatal error.
            'break' - Don't add any of the substitutions from the current
                      group, and return a value indicating a failure.
            'ignore' - Don't add the substitution, and don't log an error

        extra_args: If specified, represents a list of arguments that will be
        appended to the tool's substitution.

        """
        self.unresolved = unresolved
        self.extra_args = extra_args
        self.key = key
        self.command = command if command is not None else FindTool(key)
        self.was_resolved = False
        if verbatim:
            self.regex = key
            return

        def not_in(chars, where=""):
            if not chars:
                return ""
            pattern_str = "|".join(re.escape(x) for x in chars)
            return r"(?{}!({}))".format(where, pattern_str)

        def wordify(word):
            match = wordifier.match(word)
            introducer = match.group(1)
            word = match.group(2)
            return introducer + r"\b" + word + r"\b"

        self.regex = not_in(pre, "<") + wordify(key) + not_in(post)

    def resolve(self, config, search_dirs):
        # Extract the tool name from the pattern. This relies on the tool name
        # being surrounded by \b word match operators. If the pattern starts
        # with "| ", include it in the string to be substituted.

        tool_match = expr.match(self.regex)
        if not tool_match:
            return None

        tool_pipe = tool_match.group(2)
        tool_name = tool_match.group(4)

        if isinstance(self.command, FindTool):
            command_str = self.command.resolve(config, search_dirs)
        else:
            command_str = str(self.command)

        if command_str:
            if self.extra_args:
                command_str = " ".join([command_str] + self.extra_args)
        else:
            if self.unresolved == "warn":
                # Warn, but still provide a substitution.
                config.lit_config.note(
                    "Did not find " + tool_name + " in %s" % search_dirs
                )
                command_str = os.path.join(config.config.llvm_tools_dir, tool_name)
            elif self.unresolved == "fatal":
                # The function won't even return in this case, this leads to
                # sys.exit
                config.lit_config.fatal(
                    "Did not find " + tool_name + " in %s" % search_dirs
                )
            elif self.unresolved == "break":
                # By returning a valid result with an empty command, the
                # caller treats this as a failure.
                pass
            elif self.unresolved == "ignore":
                # By returning None, the caller just assumes there was no
                # match in the first place.
                return None
            else:
                raise "Unexpected value for ToolSubst.unresolved"
        if command_str:
            self.was_resolved = True
        return (self.regex, tool_pipe, command_str)
