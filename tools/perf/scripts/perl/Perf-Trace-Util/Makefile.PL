use 5.010000;
use ExtUtils::MakeMaker;
# See lib/ExtUtils/MakeMaker.pm for details of how to influence
# the contents of the Makefile that is written.
WriteMakefile(
    NAME              => 'Perf::Trace::Context',
    VERSION_FROM      => 'lib/Perf/Trace/Context.pm', # finds $VERSION
    PREREQ_PM         => {}, # e.g., Module::Name => 1.1
    ($] >= 5.005 ?     ## Add these new keywords supported since 5.005
      (ABSTRACT_FROM  => 'lib/Perf/Trace/Context.pm', # retrieve abstract from module
       AUTHOR         => 'Tom Zanussi <tzanussi@gmail.com>') : ()),
    LIBS              => [''], # e.g., '-lm'
    DEFINE            => '-I ../..', # e.g., '-DHAVE_SOMETHING'
    INC               => '-I.', # e.g., '-I. -I/usr/include/other'
	# Un-comment this if you add C files to link with later:
    OBJECT            => 'Context.o', # link all the C files too
);
