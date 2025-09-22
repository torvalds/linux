.. _AddingNewCIJobs:

==================
Adding New CI Jobs
==================

.. contents::
  :local:

Adding The Job
==============

libc++ uses Buildkite for running its CI. Setting up new CI jobs is easy, and
these jobs can run either on our existing infrastructure, or on your own.

If you need to run the job on your own machines, please follow the
`Buildkite guide <https://buildkite.com/docs/agent/v3>`_ to setup your
own agents. Make sure you tag your agents in a way that you'll be able
to recognize them when defining your job below. Finally, in order for the
agent to register itself to Buildkite, it will need a BuildKite Agent token.
Please contact a maintainer to get your token.

Then, simply add a job to the Buildkite pipeline by editing ``libcxx/utils/ci/buildkite-pipeline.yml``.
Take a look at how the surrounding jobs are defined and do something similar.
An example of a job definition is:

.. code-block:: yaml

  - label: "C++11"
    command: "libcxx/utils/ci/run-buildbot generic-cxx11"
    artifact_paths:
      - "**/test-results.xml"
    agents:
      queue: "libcxx-builders"
      os: "linux"
    retry:
      [...]

If you create your own agents, put them in the ``libcxx-builders`` queue and
use agent tags to allow targeting your agents from the Buildkite pipeline
config appropriately.

We try to keep the pipeline definition file as simple as possible, and to
keep any script used for CI inside ``libcxx/utils/ci``. This ensures that
it's possible to reproduce CI issues locally with ease, understanding of
course that some setups may require access to special hardware that is not
available.

Finally, add your contact info to ``libcxx/utils/ci/BOT_OWNERS.txt``. This will
be used to contact you when there are issues with the bot.

Testing Your New Job
====================

Testing your new job is easy -- once your agent is set up (if any), just open
a code review and the libc++ CI pipeline will run, including any changes you
might have made to the pipeline definition itself.

Service Level Agreement
=======================

To keep the libc++ CI useful for everyone, we aim for a quick turnaround time
for all CI jobs. This allows the overall pipeline to finish in a reasonable
amount of time, which is important because it directly affects our development
velocity. We also try to make sure that jobs run on reliable infrastructure in
order to avoid flaky failures, which reduce the value of CI for everyone.

We may be reluctant to add and support CI jobs that take a long time to finish
or that are too flaky.
