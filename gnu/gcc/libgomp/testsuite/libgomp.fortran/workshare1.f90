function foo ()
  integer :: foo
  logical :: foo_seen
  common /foo_seen/ foo_seen
  foo_seen = .true.
  foo = 3
end
function bar ()
  integer :: bar
  logical :: bar_seen
  common /bar_seen/ bar_seen
  bar_seen = .true.
  bar = 3
end
  integer :: a (10), b (10), foo, bar
  logical :: foo_seen, bar_seen
  common /foo_seen/ foo_seen
  common /bar_seen/ bar_seen

  foo_seen = .false.
  bar_seen = .false.
!$omp parallel workshare if (foo () .gt. 2) num_threads (bar () + 1)
  a = 10
  b = 20
  a(1:5) = max (a(1:5), b(1:5))
!$omp end parallel workshare
  if (any (a(1:5) .ne. 20)) call abort
  if (any (a(6:10) .ne. 10)) call abort
  if (.not. foo_seen .or. .not. bar_seen) call abort
end
