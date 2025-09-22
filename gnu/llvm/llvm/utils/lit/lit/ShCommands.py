class Command:
    def __init__(self, args, redirects):
        self.args = list(args)
        self.redirects = list(redirects)

    def __repr__(self):
        return "Command(%r, %r)" % (self.args, self.redirects)

    def __eq__(self, other):
        if not isinstance(other, Command):
            return False

        return (self.args, self.redirects) == (other.args, other.redirects)

    def toShell(self, file):
        for arg in self.args:
            if "'" not in arg:
                quoted = "'%s'" % arg
            elif '"' not in arg and "$" not in arg:
                quoted = '"%s"' % arg
            else:
                raise NotImplementedError("Unable to quote %r" % arg)
            file.write(quoted)

            # For debugging / validation.
            import ShUtil

            dequoted = list(ShUtil.ShLexer(quoted).lex())
            if dequoted != [arg]:
                raise NotImplementedError("Unable to quote %r" % arg)

        for r in self.redirects:
            if len(r[0]) == 1:
                file.write("%s '%s'" % (r[0][0], r[1]))
            else:
                file.write("%s%s '%s'" % (r[0][1], r[0][0], r[1]))


class GlobItem:
    def __init__(self, pattern):
        self.pattern = pattern

    def __repr__(self):
        return self.pattern

    def __eq__(self, other):
        if not isinstance(other, Command):
            return False

        return self.pattern == other.pattern

    def resolve(self, cwd):
        import glob
        import os

        if os.path.isabs(self.pattern):
            abspath = self.pattern
        else:
            abspath = os.path.join(cwd, self.pattern)
        results = glob.glob(abspath)
        return [self.pattern] if len(results) == 0 else results


class Pipeline:
    def __init__(self, commands, negate=False, pipe_err=False):
        self.commands = commands
        self.negate = negate
        self.pipe_err = pipe_err

    def __repr__(self):
        return "Pipeline(%r, %r, %r)" % (self.commands, self.negate, self.pipe_err)

    def __eq__(self, other):
        if not isinstance(other, Pipeline):
            return False

        return (self.commands, self.negate, self.pipe_err) == (
            other.commands,
            other.negate,
            self.pipe_err,
        )

    def toShell(self, file, pipefail=False):
        if pipefail != self.pipe_err:
            raise ValueError('Inconsistent "pipefail" attribute!')
        if self.negate:
            file.write("! ")
        for cmd in self.commands:
            cmd.toShell(file)
            if cmd is not self.commands[-1]:
                file.write("|\n  ")


class Seq:
    def __init__(self, lhs, op, rhs):
        assert op in (";", "&", "||", "&&")
        self.op = op
        self.lhs = lhs
        self.rhs = rhs

    def __repr__(self):
        return "Seq(%r, %r, %r)" % (self.lhs, self.op, self.rhs)

    def __eq__(self, other):
        if not isinstance(other, Seq):
            return False

        return (self.lhs, self.op, self.rhs) == (other.lhs, other.op, other.rhs)

    def toShell(self, file, pipefail=False):
        self.lhs.toShell(file, pipefail)
        file.write(" %s\n" % self.op)
        self.rhs.toShell(file, pipefail)
