


call graph profile:
          The sum of self and descendents is the major sort
          for this listing.

          function entries:

index     the index of the function in the call graph
          listing, as an aid to locating it (see below).

%time     the percentage of the total time of the program
          accounted for by this function and its
          descendents.

self      the number of seconds spent in this function
          itself.

descendents
          the number of seconds spent in the descendents of
          this function on behalf of this function.

called    the number of times this function is called (other
          than recursive calls).

self      the number of times this function calls itself
          recursively.

name      the name of the function, with an indication of
          its membership in a cycle, if any.

index     the index of the function in the call graph
          listing, as an aid to locating it.



          parent listings:

self*     the number of seconds of this function's self time
          which is due to calls from this parent.

descendents*
          the number of seconds of this function's
          descendent time which is due to calls from this
          parent.

called**  the number of times this function is called by
          this parent.  This is the numerator of the
          fraction which divides up the function's time to
          its parents.

total*    the number of times this function was called by
          all of its parents.  This is the denominator of
          the propagation fraction.

parents   the name of this parent, with an indication of the
          parent's membership in a cycle, if any.

index     the index of this parent in the call graph
          listing, as an aid in locating it.



          children listings:

self*     the number of seconds of this child's self time
          which is due to being called by this function.

descendent*
          the number of seconds of this child's descendent's
          time which is due to being called by this
          function.

called**  the number of times this child is called by this
          function.  This is the numerator of the
          propagation fraction for this child.

total*    the number of times this child is called by all
          functions.  This is the denominator of the
          propagation fraction.

children  the name of this child, and an indication of its
          membership in a cycle, if any.

index     the index of this child in the call graph listing,
          as an aid to locating it.



          * these fields are omitted for parents (or
          children) in the same cycle as the function.  If
          the function (or child) is a member of a cycle,
          the propagated times and propagation denominator
          represent the self time and descendent time of the
          cycle as a whole.

          ** static-only parents and children are indicated
          by a call count of 0.



          cycle listings:
          the cycle as a whole is listed with the same
          fields as a function entry.  Below it are listed
          the members of the cycle, and their contributions
          to the time and call counts of the cycle.

