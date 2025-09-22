# Input filter for doxygen.
# Copyright (C) 2003 Free Software Foundation, Inc.
# Phil Edwards <pme@gcc.gnu.org>

# single+capital is easy
s/_Tp/Type/g
s/_\([A-Z]\)/\1/g

# double+lower is not so easy; some names should be left alone.
# The following is a sloppy start.  Possibly just require GNU tools
# and use extensions.
s/__a/a/g
s/__c/c/g
s/__first/first/g
s/__in/in/g
s/__last/last/g
s/__n/n/g
s/__out/out/g
s/__pred/pred/g
s/__position/position/g
s/__pos/position/g
s/__s/s/g
s/__value/value/g
s/__x/x/g
s/__y/y/g

