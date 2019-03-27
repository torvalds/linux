# $FreeBSD$

# Read global symbols from object file.
BEGIN {
        while ("${NM:='nm'} " ARGV[1] | getline) {
                if (match($0, /^[^[:space:]]+ [^AU] (.*)$/)) {
                        syms[$3] = $2
                }
        }
        delete ARGV[1]
}

# Strip commons, make everything else local.
END {
        for (member in syms) {
                printf("--redefine-sym=%s=%s%s\n", member, prefix, member);
        }
}
