# Implementation Status and Issues #

## Known Bugs ##

  * 5 February 2015 - parallel\_ids are created in fork\_call and also in kmpc\_serialized\_parallel. This causes parallel\_ids to be leaked when a parallel region is serialized. fix: parallel\_id should be passed to kmpc\_serialized\_parallel.

  * 5 February 2015 - handling nested parallel regions needs further work when used with the GNU compiler. the implicit task in the master thread doesn't get its begin/end callbacks with gcc-generated code for serialized parallel regions. no task\_id is created for the implicit task for the master thread either if the parallel region is serialized.


## Open Issues ##
  * Adjust OMPT implementation so that it supports GOMP interface as well
    * Where possible, migrate OMPT support from kmp\_csupport.c into kmp\_runtime.c so that both the C and GOMP wrappers use common support
    * Code that initializes reenter\_runtime\_frame must be in both kmp\_csupport.c and kmp\_gsupport.c
    * Add new code to kmp\_gsupport.c as necessary
  * Missing callbacks for some tracing events
    * ompt\_event\_wait\_taskwait\_begin
    * ompt\_event\_wait\_taskwait\_end
    * ompt\_event\_wait\_taskgroup\_begin
    * ompt\_event\_wait\_taskgroup\_end
    * ompt\_event\_implicit\_task\_begin
    * ompt\_event\_implicit\_task\_end
    * ompt\_event\_initial\_task\_begin
    * ompt\_event\_initial\_task\_end
    * ompt\_event\_task\_switch
    * ompt\_event\_section\_begin
    * ompt\_event\_section\_end
    * ompt\_event\_taskwait\_begin
    * ompt\_event\_taskwait\_end
    * ompt\_event\_taskgroup\_begin
    * ompt\_event\_taskgroup\_end
    * ompt\_event\_wait\_lock
    * ompt\_event\_wait\_nest\_lock
    * ompt\_event\_wait\_critical
    * ompt\_event\_acquired\_lock
    * ompt\_event\_acquired\_nest\_lock\_first
    * ompt\_event\_acquired\_nest\_lock\_next
    * ompt\_event\_acquired\_critical
    * ompt\_event\_init\_lock
    * ompt\_event\_init\_nest\_lock
    * ompt\_event\_destroy\_lock
    * ompt\_event\_destroy\_nest\_lock
    * ompt\_event\_flush
  * Threads
    * Implementation of GTID that goes negative is awkward
    * Need non-zero thread id
    * Need support for OpenMP initial' threads
      * need to mark initial threads as OpenMP threads
  * Parallel regions
    * Support for degenerate parallel regions is ugly; however,  Jim seems to think that the ugliness may be necessary to deal with degenerate parallel regions with only one task (and no team to speak of)
  * Tasking
    * Support for tasks is largely untested
      * Implicit tasks in parallel region
      * Explicit tasks
      * Nested tasks
  * Overhead
    * Want to have support for tracing out-of-band
      * Overhead of checking calls expected to be too high
    * Needs evaluation
  * Maintaining IDs
    * Lower cost implementation would avoid atomic add for thread ids.
      * Good to maintain separate pool of ids per thread, but not clear where to put it.
    * Thread data gets reinitialized MULTIPLE times.
    * Regression testing
    * Need regression tests that introspect the information (thread ids, task ids, region ids, frame information) and validate that state is as expected.
  * Handling thread local state
    * Write up a question about thread local data at openmprtl.org
      * Is `__thread` declaration will be OK
      * Otherwise, could find local state by doing range checking on the stack pointer

## Issues Resolved ##
  * Support for initialization and finalization of the serial task is in place