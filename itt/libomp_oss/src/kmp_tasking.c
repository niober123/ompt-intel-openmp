/*
 * kmp_tasking.c -- OpenMP 3.0 tasking support.
 * $Revision: 42181 $
 * $Date: 2013-03-26 15:04:45 -0500 (Tue, 26 Mar 2013) $
 */

/* <copyright>
    Copyright (c) 1997-2013 Intel Corporation.  All Rights Reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

      * Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.
      * Neither the name of Intel Corporation nor the names of its
        contributors may be used to endorse or promote products derived
        from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


------------------------------------------------------------------------

    Portions of this software are protected under the following patents:
        U.S. Patent 5,812,852
        U.S. Patent 6,792,599
        U.S. Patent 7,069,556
        U.S. Patent 7,328,433
        U.S. Patent 7,500,242

</copyright> */

#include "kmp.h"
#include "kmp_i18n.h"


#if OMP_30_ENABLED

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */


/* forward declaration */
static void __kmp_enable_tasking( kmp_task_team_t *task_team, kmp_info_t *this_thr );
static void __kmp_alloc_task_deque( kmp_info_t *thread, kmp_thread_data_t *thread_data );
static int  __kmp_realloc_task_threads_data( kmp_info_t *thread, kmp_task_team_t *task_team );

#ifndef KMP_DEBUG
# define __kmp_static_delay( arg )     /* nothing to do */
#else

static void
__kmp_static_delay( int arg )
{
/* Work around weird code-gen bug that causes assert to trip */
# if KMP_ARCH_X86_64 && KMP_OS_LINUX
    KMP_ASSERT( arg != 0 );
# else
    KMP_ASSERT( arg >= 0 );
# endif
}
#endif /* KMP_DEBUG */

static void
__kmp_static_yield( int arg )
{
    __kmp_yield( arg );
}

#ifdef BUILD_TIED_TASK_STACK

//---------------------------------------------------------------------------
//  __kmp_trace_task_stack: print the tied tasks from the task stack in order
//     from top do bottom
//
//  gtid: global thread identifier for thread containing stack
//  thread_data: thread data for task team thread containing stack
//  threshold: value above which the trace statement triggers
//  location: string identifying call site of this function (for trace)

static void
__kmp_trace_task_stack( kmp_int32 gtid, kmp_thread_data_t *thread_data, int threshold, char *location )
{
    kmp_task_stack_t *task_stack = & thread_data->td.td_susp_tied_tasks;
    kmp_taskdata_t **stack_top = task_stack -> ts_top;
    kmp_int32 entries = task_stack -> ts_entries;
    kmp_taskdata_t *tied_task;

    KA_TRACE(threshold, ("__kmp_trace_task_stack(start): location = %s, gtid = %d, entries = %d, "
                         "first_block = %p, stack_top = %p \n",
                         location, gtid, entries, task_stack->ts_first_block, stack_top ) );

    KMP_DEBUG_ASSERT( stack_top != NULL );
    KMP_DEBUG_ASSERT( entries > 0 );

    while ( entries != 0 )
    {
        KMP_DEBUG_ASSERT( stack_top != & task_stack->ts_first_block.sb_block[0] );
        // fix up ts_top if we need to pop from previous block
        if ( entries & TASK_STACK_INDEX_MASK == 0 )
        {
            kmp_stack_block_t *stack_block = (kmp_stack_block_t *) (stack_top) ;

            stack_block = stack_block -> sb_prev;
            stack_top = & stack_block -> sb_block[TASK_STACK_BLOCK_SIZE];
        }

        // finish bookkeeping
        stack_top--;
        entries--;

        tied_task = * stack_top;

        KMP_DEBUG_ASSERT( tied_task != NULL );
        KMP_DEBUG_ASSERT( tied_task -> td_flags.tasktype == TASK_TIED );

        KA_TRACE(threshold, ("__kmp_trace_task_stack(%s):             gtid=%d, entry=%d, "
                             "stack_top=%p, tied_task=%p\n",
                             location, gtid, entries, stack_top, tied_task ) );
    }
    KMP_DEBUG_ASSERT( stack_top == & task_stack->ts_first_block.sb_block[0] );

    KA_TRACE(threshold, ("__kmp_trace_task_stack(exit): location = %s, gtid = %d\n",
                         location, gtid ) );
}

//---------------------------------------------------------------------------
//  __kmp_init_task_stack: initialize the task stack for the first time
//    after a thread_data structure is created.
//    It should not be necessary to do this again (assuming the stack works).
//
//  gtid: global thread identifier of calling thread
//  thread_data: thread data for task team thread containing stack

static void
__kmp_init_task_stack( kmp_int32 gtid, kmp_thread_data_t *thread_data )
{
    kmp_task_stack_t *task_stack = & thread_data->td.td_susp_tied_tasks;
    kmp_stack_block_t *first_block;

    // set up the first block of the stack
    first_block = & task_stack -> ts_first_block;
    task_stack -> ts_top = (kmp_taskdata_t **) first_block;
    memset( (void *) first_block, '\0', TASK_STACK_BLOCK_SIZE * sizeof(kmp_taskdata_t *));

    // initialize the stack to be empty
    task_stack  -> ts_entries = TASK_STACK_EMPTY;
    first_block -> sb_next = NULL;
    first_block -> sb_prev = NULL;
}


//---------------------------------------------------------------------------
//  __kmp_free_task_stack: free the task stack when thread_data is destroyed.
//
//  gtid: global thread identifier for calling thread
//  thread_data: thread info for thread containing stack

static void
__kmp_free_task_stack( kmp_int32 gtid, kmp_thread_data_t *thread_data )
{
    kmp_task_stack_t *task_stack = & thread_data->td.td_susp_tied_tasks;
    kmp_stack_block_t *stack_block = & task_stack -> ts_first_block;

    KMP_DEBUG_ASSERT( task_stack -> ts_entries == TASK_STACK_EMPTY );
    // free from the second block of the stack
    while ( stack_block != NULL ) {
        kmp_stack_block_t *next_block = (stack_block) ? stack_block -> sb_next : NULL;

        stack_block -> sb_next = NULL;
        stack_block -> sb_prev = NULL;
        if (stack_block != & task_stack -> ts_first_block) {
            __kmp_thread_free( thread, stack_block );  // free the block, if not the first
        }
        stack_block = next_block;
    }
    // initialize the stack to be empty
    task_stack -> ts_entries = 0;
    task_stack -> ts_top = NULL;
}


//---------------------------------------------------------------------------
//  __kmp_push_task_stack: Push the tied task onto the task stack.
//     Grow the stack if necessary by allocating another block.
//
//  gtid: global thread identifier for calling thread
//  thread: thread info for thread containing stack
//  tied_task: the task to push on the stack

static void
__kmp_push_task_stack( kmp_int32 gtid, kmp_info_t *thread, kmp_taskdata_t * tied_task )
{
    // GEH - need to consider what to do if tt_threads_data not allocated yet
    kmp_thread_data_t *thread_data = & thread -> th.th_task_team ->
                                        tt.tt_threads_data[ __kmp_tid_from_gtid( gtid ) ];
    kmp_task_stack_t *task_stack = & thread_data->td.td_susp_tied_tasks ;

    if ( tied_task->td_flags.team_serial || tied_task->td_flags.tasking_ser ) {
        return;  // Don't push anything on stack if team or team tasks are serialized
    }

    KMP_DEBUG_ASSERT( tied_task -> td_flags.tasktype == TASK_TIED );
    KMP_DEBUG_ASSERT( task_stack -> ts_top != NULL );

    KA_TRACE(20, ("__kmp_push_task_stack(enter): GTID: %d; THREAD: %p; TASK: %p\n",
                  gtid, thread, tied_task ) );
    // Store entry
    * (task_stack -> ts_top) = tied_task;

    // Do bookkeeping for next push
    task_stack -> ts_top++;
    task_stack -> ts_entries++;

    if ( task_stack -> ts_entries & TASK_STACK_INDEX_MASK == 0 )
    {
        // Find beginning of this task block
        kmp_stack_block_t *stack_block =
             (kmp_stack_block_t *) (task_stack -> ts_top - TASK_STACK_BLOCK_SIZE);

        // Check if we already have a block
        if ( stack_block -> sb_next != NULL )
        {    // reset ts_top to beginning of next block
            task_stack -> ts_top = & stack_block -> sb_next -> sb_block[0];
        }
        else
        {   // Alloc new block and link it up
            kmp_stack_block_t *new_block = (kmp_stack_block_t *)
              __kmp_thread_calloc(thread, sizeof(kmp_stack_block_t));

            task_stack -> ts_top  = & new_block -> sb_block[0];
            stack_block -> sb_next = new_block;
            new_block  -> sb_prev = stack_block;
            new_block  -> sb_next = NULL;

            KA_TRACE(30, ("__kmp_push_task_stack(): GTID: %d; TASK: %p; Alloc new block: %p\n",
                          gtid, tied_task, new_block ) );
        }
    }
    KA_TRACE(20, ("__kmp_push_task_stack(exit): GTID: %d; TASK: %p\n", gtid, tied_task ) );
}

//---------------------------------------------------------------------------
//  __kmp_pop_task_stack: Pop the tied task from the task stack.  Don't return
//     the task, just check to make sure it matches the ending task passed in.
//
//  gtid: global thread identifier for the calling thread
//  thread: thread info structure containing stack
//  tied_task: the task popped off the stack
//  ending_task: the task that is ending (should match popped task)

static void
__kmp_pop_task_stack( kmp_int32 gtid, kmp_info_t *thread, kmp_taskdata_t *ending_task )
{
    // GEH - need to consider what to do if tt_threads_data not allocated yet
    kmp_thread_data_t *thread_data = & thread -> th.th_task_team -> tt_threads_data[ __kmp_tid_from_gtid( gtid ) ];
    kmp_task_stack_t *task_stack = & thread_data->td.td_susp_tied_tasks ;
    kmp_taskdata_t *tied_task;

    if ( ending_task->td_flags.team_serial || ending_task->td_flags.tasking_ser ) {
        return;  // Don't pop anything from stack if team or team tasks are serialized
    }

    KMP_DEBUG_ASSERT( task_stack -> ts_top != NULL );
    KMP_DEBUG_ASSERT( task_stack -> ts_entries > 0 );

    KA_TRACE(20, ("__kmp_pop_task_stack(enter): GTID: %d; THREAD: %p\n", gtid, thread ) );

    // fix up ts_top if we need to pop from previous block
    if ( task_stack -> ts_entries & TASK_STACK_INDEX_MASK == 0 )
    {
        kmp_stack_block_t *stack_block =
           (kmp_stack_block_t *) (task_stack -> ts_top) ;

        stack_block = stack_block -> sb_prev;
        task_stack -> ts_top = & stack_block -> sb_block[TASK_STACK_BLOCK_SIZE];
    }

    // finish bookkeeping
    task_stack -> ts_top--;
    task_stack -> ts_entries--;

    tied_task = * (task_stack -> ts_top );

    KMP_DEBUG_ASSERT( tied_task != NULL );
    KMP_DEBUG_ASSERT( tied_task -> td_flags.tasktype == TASK_TIED );
    KMP_DEBUG_ASSERT( tied_task == ending_task );  // If we built the stack correctly

    KA_TRACE(20, ("__kmp_pop_task_stack(exit): GTID: %d; TASK: %p\n", gtid, tied_task ) );
    return;
}
#endif /* BUILD_TIED_TASK_STACK */

//---------------------------------------------------
//  __kmp_push_task: Add a task to the thread's deque

static kmp_int32
__kmp_push_task(kmp_int32 gtid, kmp_task_t * task )
{
    kmp_info_t *        thread = __kmp_threads[ gtid ];
    kmp_taskdata_t *    taskdata = KMP_TASK_TO_TASKDATA(task);
    kmp_task_team_t *   task_team = thread->th.th_task_team;
    kmp_int32           tid = __kmp_tid_from_gtid( gtid );
    kmp_thread_data_t * thread_data;

    KA_TRACE(20, ("__kmp_push_task: T#%d trying to push task %p.\n", gtid, taskdata ) );

    // The first check avoids building task_team thread data if serialized
    if ( taskdata->td_flags.task_serial ) {
        KA_TRACE(20, ( "__kmp_push_task: T#%d team serialized; returning TASK_NOT_PUSHED for task %p\n",
                       gtid, taskdata ) );
        return TASK_NOT_PUSHED;
    }

    // Now that serialized tasks have returned, we can assume that we are not in immediate exec mode
    KMP_DEBUG_ASSERT( __kmp_tasking_mode != tskm_immediate_exec );
    if ( ! KMP_TASKING_ENABLED( task_team, thread->th.th_task_state ) ) {
         __kmp_enable_tasking( task_team, thread );
    }
    KMP_DEBUG_ASSERT( TCR_4(task_team -> tt.tt_found_tasks) == TRUE );
    KMP_DEBUG_ASSERT( TCR_PTR(task_team -> tt.tt_threads_data) != NULL );

    // Find tasking deque specific to encountering thread
    thread_data = & task_team -> tt.tt_threads_data[ tid ];

    // No lock needed since only owner can allocate
    if (thread_data -> td.td_deque == NULL ) {
        __kmp_alloc_task_deque( thread, thread_data );
    }

    // Check if deque is full
    if ( TCR_4(thread_data -> td.td_deque_ntasks) >= TASK_DEQUE_SIZE )
    {
        KA_TRACE(20, ( "__kmp_push_task: T#%d deque is full; returning TASK_NOT_PUSHED for task %p\n",
                       gtid, taskdata ) );
        return TASK_NOT_PUSHED;
    }

    // Lock the deque for the task push operation
    __kmp_acquire_bootstrap_lock( & thread_data -> td.td_deque_lock );

    // Must have room since no thread can add tasks but calling thread
    KMP_DEBUG_ASSERT( TCR_4(thread_data -> td.td_deque_ntasks) < TASK_DEQUE_SIZE );

    thread_data -> td.td_deque[ thread_data -> td.td_deque_tail ] = taskdata;  // Push taskdata
    // Wrap index.
    thread_data -> td.td_deque_tail = ( thread_data -> td.td_deque_tail + 1 ) & TASK_DEQUE_MASK;
    TCW_4(thread_data -> td.td_deque_ntasks, TCR_4(thread_data -> td.td_deque_ntasks) + 1);             // Adjust task count

    __kmp_release_bootstrap_lock( & thread_data -> td.td_deque_lock );

    KA_TRACE(20, ("__kmp_push_task: T#%d returning TASK_SUCCESSFULLY_PUSHED: "
                  "task=%p ntasks=%d head=%u tail=%u\n",
                  gtid, taskdata, thread_data->td.td_deque_ntasks,
                  thread_data->td.td_deque_tail, thread_data->td.td_deque_head) );

    return TASK_SUCCESSFULLY_PUSHED;
}


//-----------------------------------------------------------------------------------------
// __kmp_pop_current_task_from_thread: set up current task from called thread when team ends
// this_thr: thread structure to set current_task in.

void
__kmp_pop_current_task_from_thread( kmp_info_t *this_thr )
{
    KF_TRACE( 10, ("__kmp_pop_current_task_from_thread(enter): T#%d this_thread=%p, curtask=%p, "
                   "curtask_parent=%p\n",
                   0, this_thr, this_thr -> th.th_current_task,
                   this_thr -> th.th_current_task -> td_parent ) );

    this_thr -> th.th_current_task = this_thr -> th.th_current_task -> td_parent;

    KF_TRACE( 10, ("__kmp_pop_current_task_from_thread(exit): T#%d this_thread=%p, curtask=%p, "
                   "curtask_parent=%p\n",
                   0, this_thr, this_thr -> th.th_current_task,
                   this_thr -> th.th_current_task -> td_parent ) );
}


//---------------------------------------------------------------------------------------
// __kmp_push_current_task_to_thread: set up current task in called thread for a new team
// this_thr: thread structure to set up
// team: team for implicit task data
// tid: thread within team to set up

void
__kmp_push_current_task_to_thread( kmp_info_t *this_thr, kmp_team_t *team, int tid )
{
    // current task of the thread is a parent of the new just created implicit tasks of new team
    KF_TRACE( 10, ( "__kmp_push_current_task_to_thread(enter): T#%d this_thread=%p curtask=%p "
                    "parent_task=%p\n",
                    tid, this_thr, this_thr->th.th_current_task,
                    team->t.t_implicit_task_taskdata[tid].td_parent ) );

    KMP_DEBUG_ASSERT (this_thr != NULL);

    if( tid == 0 ) {
        if( this_thr->th.th_current_task != & team -> t.t_implicit_task_taskdata[ 0 ] ) {
            team -> t.t_implicit_task_taskdata[ 0 ].td_parent = this_thr->th.th_current_task;
            this_thr->th.th_current_task = & team -> t.t_implicit_task_taskdata[ 0 ];
        }
    } else {
        team -> t.t_implicit_task_taskdata[ tid ].td_parent = team -> t.t_implicit_task_taskdata[ 0 ].td_parent;
        this_thr->th.th_current_task = & team -> t.t_implicit_task_taskdata[ tid ];
    }

    KF_TRACE( 10, ( "__kmp_push_current_task_to_thread(exit): T#%d this_thread=%p curtask=%p "
                    "parent_task=%p\n",
                    tid, this_thr, this_thr->th.th_current_task,
                    team->t.t_implicit_task_taskdata[tid].td_parent ) );
}


//----------------------------------------------------------------------
// __kmp_task_start: bookkeeping for a task starting execution
// GTID: global thread id of calling thread
// task: task starting execution
// current_task: task suspending

static void
__kmp_task_start( kmp_int32 gtid, kmp_task_t * task, kmp_taskdata_t * current_task )
{
    kmp_taskdata_t * taskdata = KMP_TASK_TO_TASKDATA(task);
    kmp_info_t * thread = __kmp_threads[ gtid ];

    KA_TRACE(10, ("__kmp_task_start(enter): T#%d starting task %p: current_task=%p\n",
                  gtid, taskdata, current_task) );

    KMP_DEBUG_ASSERT( taskdata -> td_flags.tasktype == TASK_EXPLICIT );

    // mark currently executing task as suspended
    // TODO: GEH - make sure root team implicit task is initialized properly.
    // KMP_DEBUG_ASSERT( current_task -> td_flags.executing == 1 );
    current_task -> td_flags.executing = 0;

    // Add task to stack if tied
#ifdef BUILD_TIED_TASK_STACK
    if ( taskdata -> td_flags.tiedness == TASK_TIED )
    {
        __kmp_push_task_stack( gtid, thread, taskdata );
    }
#endif /* BUILD_TIED_TASK_STACK */

    // mark starting task as executing and as current task
    thread -> th.th_current_task = taskdata;

    KMP_DEBUG_ASSERT( taskdata -> td_flags.started == 0 );
    KMP_DEBUG_ASSERT( taskdata -> td_flags.executing == 0 );
    taskdata -> td_flags.started = 1;
    taskdata -> td_flags.executing = 1;
    KMP_DEBUG_ASSERT( taskdata -> td_flags.complete == 0 );
    KMP_DEBUG_ASSERT( taskdata -> td_flags.freed == 0 );

    // GEH TODO: shouldn't we pass some sort of location identifier here?
    // APT: yes, we will pass location here.
    // need to store current thread state (in a thread or taskdata structure)
    // before setting work_state, otherwise wrong state is set after end of task

    KA_TRACE(10, ("__kmp_task_start(exit): T#%d task=%p\n",
                  gtid, taskdata ) );

#if OMPT_SUPPORT
   if ((ompt_status == ompt_status_track_callback)) {
     if (ompt_callbacks.ompt_callback(ompt_event_task_create)) {
       kmp_taskdata_t *parent = current_task->td_parent;
       ompt_callbacks.ompt_callback(ompt_event_task_create)
	 (parent ? &(parent->ompt_task_info.data) : NULL,
	  parent ? &(parent->ompt_task_info.frame) : NULL,
	  &(current_task->ompt_task_info.data));
     }
   }
#endif

    return;
}


//----------------------------------------------------------------------
// __kmpc_omp_task_begin_if0: report that a given serialized task has started execution
// loc_ref: source location information; points to beginning of task block.
// gtid: global thread number.
// task: task thunk for the started task.

void
__kmpc_omp_task_begin_if0( ident_t *loc_ref, kmp_int32 gtid, kmp_task_t * task )
{
    kmp_taskdata_t * taskdata = KMP_TASK_TO_TASKDATA(task);
    kmp_taskdata_t * current_task = __kmp_threads[ gtid ] -> th.th_current_task;

    KA_TRACE(10, ("__kmpc_omp_task_begin_if0(enter): T#%d loc=%p task=%p current_task=%p\n",
                  gtid, loc_ref, taskdata, current_task ) );

    taskdata -> td_flags.task_serial = 1;  // Execute this task immediately, not deferred.
    __kmp_task_start( gtid, task, current_task );

    KA_TRACE(10, ("__kmpc_omp_task_begin_if0(exit): T#%d loc=%p task=%p,\n",
                  gtid, loc_ref, taskdata ) );

    return;
}

#ifdef TASK_UNUSED
//----------------------------------------------------------------------
// __kmpc_omp_task_begin: report that a given task has started execution
// NEVER GENERATED BY COMPILER, DEPRECATED!!!

void
__kmpc_omp_task_begin( ident_t *loc_ref, kmp_int32 gtid, kmp_task_t * task )
{
    kmp_taskdata_t * current_task = __kmp_threads[ gtid ] -> th.th_current_task;

    KA_TRACE(10, ("__kmpc_omp_task_begin(enter): T#%d loc=%p task=%p current_task=%p\n",
                  gtid, loc_ref, KMP_TASK_TO_TASKDATA(task), current_task ) );

    __kmp_task_start( gtid, task, current_task );

    KA_TRACE(10, ("__kmpc_omp_task_begin(exit): T#%d loc=%p task=%p,\n",
                  gtid, loc_ref, KMP_TASK_TO_TASKDATA(task) ) );

    return;
}
#endif // TASK_UNUSED


//-------------------------------------------------------------------------------------
// __kmp_free_task: free the current task space and the space for shareds
// gtid: Global thread ID of calling thread
// taskdata: task to free
// thread: thread data structure of caller

static void
__kmp_free_task( kmp_int32 gtid, kmp_taskdata_t * taskdata, kmp_info_t * thread )
{
    KA_TRACE(30, ("__kmp_free_task: T#%d freeing data from task %p\n",
                  gtid, taskdata) );

    // Check to make sure all flags and counters have the correct values
    KMP_DEBUG_ASSERT( taskdata->td_flags.tasktype == TASK_EXPLICIT );
    KMP_DEBUG_ASSERT( taskdata->td_flags.executing == 0 );
    KMP_DEBUG_ASSERT( taskdata->td_flags.complete == 1 );
    KMP_DEBUG_ASSERT( taskdata->td_flags.freed == 0 );
    KMP_DEBUG_ASSERT( TCR_4(taskdata->td_allocated_child_tasks) == 0  || taskdata->td_flags.task_serial == 1);
    KMP_DEBUG_ASSERT( TCR_4(taskdata->td_incomplete_child_tasks) == 0 );

    taskdata->td_flags.freed = 1;
    // deallocate the taskdata and shared variable blocks associated with this task
    #if USE_FAST_MEMORY
        __kmp_fast_free( thread, taskdata );
    #else /* ! USE_FAST_MEMORY */
        __kmp_thread_free( thread, taskdata );
    #endif

    KA_TRACE(20, ("__kmp_free_task: T#%d freed task %p\n",
                  gtid, taskdata) );
}

//-------------------------------------------------------------------------------------
// __kmp_free_task_and_ancestors: free the current task and ancestors without children
//
// gtid: Global thread ID of calling thread
// taskdata: task to free
// thread: thread data structure of caller

static void
__kmp_free_task_and_ancestors( kmp_int32 gtid, kmp_taskdata_t * taskdata, kmp_info_t * thread )
{
    kmp_int32 children = 0;
    kmp_int32 team_or_tasking_serialized = taskdata -> td_flags.team_serial || taskdata -> td_flags.tasking_ser;

    KMP_DEBUG_ASSERT( taskdata -> td_flags.tasktype == TASK_EXPLICIT );

    if ( !team_or_tasking_serialized ) {
        children = KMP_TEST_THEN_DEC32( (kmp_int32 *)(& taskdata -> td_allocated_child_tasks) ) - 1;
        KMP_DEBUG_ASSERT( children >= 0 );
    }

    // Now, go up the ancestor tree to see if any ancestors can now be freed.
    while ( children == 0 )
    {
        kmp_taskdata_t * parent_taskdata = taskdata -> td_parent;

        KA_TRACE(20, ("__kmp_free_task_and_ancestors(enter): T#%d task %p complete "
                      "and freeing itself\n", gtid, taskdata) );

        // --- Deallocate my ancestor task ---
        __kmp_free_task( gtid, taskdata, thread );

        taskdata = parent_taskdata;

        // Stop checking ancestors at implicit task or if tasking serialized
        // instead of walking up ancestor tree to avoid premature deallocation of ancestors.
        if ( team_or_tasking_serialized || taskdata -> td_flags.tasktype == TASK_IMPLICIT )
            return;

        if ( !team_or_tasking_serialized ) {
            // Predecrement simulated by "- 1" calculation
            children = KMP_TEST_THEN_DEC32( (kmp_int32 *)(& taskdata -> td_allocated_child_tasks) ) - 1;
            KMP_DEBUG_ASSERT( children >= 0 );
        }
    }

    KA_TRACE(20, ("__kmp_free_task_and_ancestors(exit): T#%d task %p has %d children; "
                  "not freeing it yet\n", gtid, taskdata, children) );
}

//---------------------------------------------------------------------
// __kmp_task_finish: bookkeeping to do when a task finishes execution
// gtid: global thread ID for calling thread
// task: task to be finished
// resumed_task: task to be resumed.  (may be NULL if task is serialized)

static void
__kmp_task_finish( kmp_int32 gtid, kmp_task_t *task, kmp_taskdata_t *resumed_task )
{
    kmp_taskdata_t * taskdata = KMP_TASK_TO_TASKDATA(task);
    kmp_info_t * thread = __kmp_threads[ gtid ];
    kmp_int32 children = 0;

#if OMPT_SUPPORT
   if ((ompt_status == ompt_status_track_callback)) {
     if (ompt_callbacks.ompt_callback(ompt_event_task_exit)) {
       ompt_callbacks.ompt_callback(ompt_event_task_exit)(&(taskdata->ompt_task_info.data));
     }
   }
#endif

    KA_TRACE(10, ("__kmp_task_finish(enter): T#%d finishing task %p and resuming task %p\n",
                  gtid, taskdata, resumed_task) );

    KMP_DEBUG_ASSERT( taskdata -> td_flags.tasktype == TASK_EXPLICIT );

    // Pop task from stack if tied
#ifdef BUILD_TIED_TASK_STACK
    if ( taskdata -> td_flags.tiedness == TASK_TIED )
    {
        __kmp_pop_task_stack( gtid, thread, taskdata );
    }
#endif /* BUILD_TIED_TASK_STACK */

    KMP_DEBUG_ASSERT( taskdata -> td_flags.executing == 1 );
    KMP_DEBUG_ASSERT( taskdata -> td_flags.complete == 0 );
    taskdata -> td_flags.executing = 0;  // suspend the finishing task
    taskdata -> td_flags.complete = 1;   // mark the task as completed
    KMP_DEBUG_ASSERT( taskdata -> td_flags.started == 1 );
    KMP_DEBUG_ASSERT( taskdata -> td_flags.freed == 0 );

    // Only need to keep track of count if team parallel and tasking not serialized
    if ( !( taskdata -> td_flags.team_serial || taskdata -> td_flags.tasking_ser ) ) {
        // Predecrement simulated by "- 1" calculation
        children = KMP_TEST_THEN_DEC32( (kmp_int32 *)(& taskdata -> td_parent -> td_incomplete_child_tasks) ) - 1;
        KMP_DEBUG_ASSERT( children >= 0 );
#if OMP_40_ENABLED
        if ( taskdata->td_taskgroup )
            KMP_TEST_THEN_DEC32( (kmp_int32 *)(& taskdata->td_taskgroup->count) );
#endif
    }

    KA_TRACE(20, ("__kmp_task_finish: T#%d finished task %p, %d incomplete children\n",
                  gtid, taskdata, children) );

    // bookkeeping for resuming task:
    // GEH - note tasking_ser => task_serial
    KMP_DEBUG_ASSERT( (taskdata->td_flags.tasking_ser || taskdata->td_flags.task_serial) ==
                       taskdata->td_flags.task_serial);
    if ( taskdata->td_flags.task_serial )
    {
        if (resumed_task == NULL) {
            resumed_task = taskdata->td_parent;  // In a serialized task, the resumed task is the parent
        }
        else {
            // verify resumed task passed in points to parent
            KMP_DEBUG_ASSERT( resumed_task == taskdata->td_parent );
        }
    }
    else {
        KMP_DEBUG_ASSERT( resumed_task != NULL );        // verify that resumed task is passed as arguemnt
    }

    // Free this task and then ancestor tasks if they have no children.
    __kmp_free_task_and_ancestors(gtid, taskdata, thread);

    __kmp_threads[ gtid ] -> th.th_current_task = resumed_task; // restore current_task

    // TODO: GEH - make sure root team implicit task is initialized properly.
    // KMP_DEBUG_ASSERT( resumed_task->td_flags.executing == 0 );
    resumed_task->td_flags.executing = 1;  // resume previous task

    KA_TRACE(10, ("__kmp_task_finish(exit): T#%d finished task %p, resuming task %p\n",
                  gtid, taskdata, resumed_task) );

    return;
}

//---------------------------------------------------------------------
// __kmpc_omp_task_complete_if0: report that a task has completed execution
// loc_ref: source location information; points to end of task block.
// gtid: global thread number.
// task: task thunk for the completed task.

void
__kmpc_omp_task_complete_if0( ident_t *loc_ref, kmp_int32 gtid, kmp_task_t *task )
{
    KA_TRACE(10, ("__kmpc_omp_task_complete_if0(enter): T#%d loc=%p task=%p\n",
                  gtid, loc_ref, KMP_TASK_TO_TASKDATA(task) ) );

    __kmp_task_finish( gtid, task, NULL );  // this routine will provide task to resume

    KA_TRACE(10, ("__kmpc_omp_task_complete_if0(exit): T#%d loc=%p task=%p\n",
                  gtid, loc_ref, KMP_TASK_TO_TASKDATA(task) ) );

    return;
}

#ifdef TASK_UNUSED
//---------------------------------------------------------------------
// __kmpc_omp_task_complete: report that a task has completed execution
// NEVER GENERATED BY COMPILER, DEPRECATED!!!

void
__kmpc_omp_task_complete( ident_t *loc_ref, kmp_int32 gtid, kmp_task_t *task )
{
    KA_TRACE(10, ("__kmpc_omp_task_complete(enter): T#%d loc=%p task=%p\n",
                  gtid, loc_ref, KMP_TASK_TO_TASKDATA(task) ) );

    __kmp_task_finish( gtid, task, NULL );  // Not sure how to find task to resume

    KA_TRACE(10, ("__kmpc_omp_task_complete(exit): T#%d loc=%p task=%p\n",
                  gtid, loc_ref, KMP_TASK_TO_TASKDATA(task) ) );
    return;
}
#endif // TASK_UNUSED


//----------------------------------------------------------------------------------------------------
// __kmp_init_implicit_task: Initialize the appropriate fields in the implicit task for a given thread
//
// loc_ref:  reference to source location of parallel region
// this_thr:  thread data structure corresponding to implicit task
// team: team for this_thr
// tid: thread id of given thread within team
// set_curr_task: TRUE if need to push current task to thread
// NOTE: Routine does not set up the implicit task ICVS.  This is assumed to have already been done elsewhere.
// TODO: Get better loc_ref.  Value passed in may be NULL

void
__kmp_init_implicit_task( ident_t *loc_ref, kmp_info_t *this_thr, kmp_team_t *team, int tid, int set_curr_task )
{
    kmp_taskdata_t * task   = & team->t.t_implicit_task_taskdata[ tid ];

    KF_TRACE(10, ("__kmp_init_implicit_task(enter): T#:%d team=%p task=%p, reinit=%s\n",
                  tid, team, task, set_curr_task ? "TRUE" : "FALSE" ) );

    task->td_task_id  = KMP_GEN_TASK_ID();
    task->td_team     = team;
//    task->td_parent   = NULL;  // fix for CQ230101 (broken parent task info in debugger)
    task->td_ident    = loc_ref;
    task->td_taskwait_ident   = NULL;
    task->td_taskwait_counter = 0;
    task->td_taskwait_thread  = 0;

    task->td_flags.tiedness    = TASK_TIED;
    task->td_flags.tasktype    = TASK_IMPLICIT;
    // All implicit tasks are executed immediately, not deferred
    task->td_flags.task_serial = 1;
    task->td_flags.tasking_ser = ( __kmp_tasking_mode == tskm_immediate_exec );
    task->td_flags.team_serial = ( team->t.t_serialized ) ? 1 : 0;

    task->td_flags.started     = 1;
    task->td_flags.executing   = 1;
    task->td_flags.complete    = 0;
    task->td_flags.freed       = 0;

    if (set_curr_task) {  // only do this initialization the first time a thread is created
        task->td_incomplete_child_tasks = 0;
        task->td_allocated_child_tasks  = 0; // Not used because do not need to deallocate implicit task
#if OMP_40_ENABLED
        task->td_taskgroup = NULL;           // An implicit task does not have taskgroup
#endif
        __kmp_push_current_task_to_thread( this_thr, team, tid );
    } else {
        KMP_DEBUG_ASSERT(task->td_incomplete_child_tasks == 0);
        KMP_DEBUG_ASSERT(task->td_allocated_child_tasks  == 0);
    }

    KF_TRACE(10, ("__kmp_init_implicit_task(exit): T#:%d team=%p task=%p\n",
                  tid, team, task ) );
}

// Round up a size to a power of two specified by val
// Used to insert padding between structures co-allocated using a single malloc() call
static size_t
__kmp_round_up_to_val( size_t size, size_t val ) {
    if ( size & ( val - 1 ) ) {
        size &= ~ ( val - 1 );
        if ( size <= KMP_SIZE_T_MAX - val ) {
            size += val;    // Round up if there is no overflow.
        }; // if
    }; // if
    return size;
} // __kmp_round_up_to_va


//---------------------------------------------------------------------------------
// __kmp_task_alloc: Allocate the taskdata and task data structures for a task
//
// loc_ref: source location information
// gtid: global thread number.
// flags: include tiedness & task type (explicit vs. implicit) of the ''new'' task encountered.
//        Converted from kmp_int32 to kmp_tasking_flags_t in routine.
// sizeof_kmp_task_t:  Size in bytes of kmp_task_t data structure including private vars accessed in task.
// sizeof_shareds:  Size in bytes of array of pointers to shared vars accessed in task.
// task_entry: Pointer to task code entry point generated by compiler.
// returns: a pointer to the allocated kmp_task_t structure (task).

kmp_task_t *
__kmp_task_alloc( ident_t *loc_ref, kmp_int32 gtid, kmp_tasking_flags_t *flags,
                  size_t sizeof_kmp_task_t, size_t sizeof_shareds,
                  kmp_routine_entry_t task_entry )
{
    kmp_task_t *task;
    kmp_taskdata_t *taskdata;
    kmp_info_t *thread = __kmp_threads[ gtid ];
    kmp_team_t *team = thread->th.th_team;
    kmp_taskdata_t *parent_task = thread->th.th_current_task;
    size_t shareds_offset;

    KA_TRACE(10, ("__kmp_task_alloc(enter): T#%d loc=%p, flags=(0x%x) "
                  "sizeof_task=%ld sizeof_shared=%ld entry=%p\n",
                  gtid, loc_ref, *((kmp_int32 *)flags), sizeof_kmp_task_t,
                  sizeof_shareds, task_entry) );

    if ( parent_task->td_flags.final ) {
        if (flags->merged_if0) {
        }
        flags->final = 1;
    }

    // Calculate shared structure offset including padding after kmp_task_t struct
    // to align pointers in shared struct
    shareds_offset = sizeof( kmp_taskdata_t ) + sizeof_kmp_task_t;
    shareds_offset = __kmp_round_up_to_val( shareds_offset, sizeof( void * ));

    // Allocate a kmp_taskdata_t block and a kmp_task_t block.
    KA_TRACE(30, ("__kmp_task_alloc: T#%d First malloc size: %ld\n",
                  gtid, shareds_offset) );
    KA_TRACE(30, ("__kmp_task_alloc: T#%d Second malloc size: %ld\n",
                  gtid, sizeof_shareds) );

    // Avoid double allocation here by combining shareds with taskdata
    #if USE_FAST_MEMORY
    taskdata = (kmp_taskdata_t *) __kmp_fast_allocate( thread, shareds_offset + sizeof_shareds );
    #else /* ! USE_FAST_MEMORY */
    taskdata = (kmp_taskdata_t *) __kmp_thread_malloc( thread, shareds_offset + sizeof_shareds );
    #endif /* USE_FAST_MEMORY */

    task                      = KMP_TASKDATA_TO_TASK(taskdata);

    // Make sure task & taskdata are aligned appropriately
#if KMP_ARCH_X86
    KMP_DEBUG_ASSERT( ( ((kmp_uintptr_t)taskdata) & (sizeof(double)-1) ) == 0 );
    KMP_DEBUG_ASSERT( ( ((kmp_uintptr_t)task) & (sizeof(double)-1) ) == 0 );
#else
    KMP_DEBUG_ASSERT( ( ((kmp_uintptr_t)taskdata) & (sizeof(_Quad)-1) ) == 0 );
    KMP_DEBUG_ASSERT( ( ((kmp_uintptr_t)task) & (sizeof(_Quad)-1) ) == 0 );
#endif
    if (sizeof_shareds > 0) {
        // Avoid double allocation here by combining shareds with taskdata
        task->shareds         = & ((char *) taskdata)[ shareds_offset ];
        // Make sure shareds struct is aligned to pointer size
        KMP_DEBUG_ASSERT( ( ((kmp_uintptr_t)task->shareds) & (sizeof(void *)-1) ) == 0 );
    } else {
        task->shareds         = NULL;
    }
    task->routine             = task_entry;
    task->part_id             = 0;      // AC: Always start with 0 part id

    taskdata->td_task_id      = KMP_GEN_TASK_ID();
    taskdata->td_team         = team;
    taskdata->td_alloc_thread = thread; 
    taskdata->td_parent       = parent_task;
    taskdata->td_level        = parent_task->td_level + 1; // increment nesting level
    taskdata->td_ident        = loc_ref;
    taskdata->td_taskwait_ident   = NULL;
    taskdata->td_taskwait_counter = 0;
    taskdata->td_taskwait_thread  = 0;
    KMP_DEBUG_ASSERT( taskdata->td_parent != NULL );
    copy_icvs( &taskdata->td_icvs, &taskdata->td_parent->td_icvs );

    taskdata->td_flags.tiedness    = flags->tiedness;
    taskdata->td_flags.final       = flags->final;
    taskdata->td_flags.merged_if0  = flags->merged_if0;
    taskdata->td_flags.tasktype    = TASK_EXPLICIT;

    // GEH - TODO: fix this to copy parent task's value of tasking_ser flag
    taskdata->td_flags.tasking_ser = ( __kmp_tasking_mode == tskm_immediate_exec );

    // GEH - TODO: fix this to copy parent task's value of team_serial flag
    taskdata->td_flags.team_serial = ( team->t.t_serialized ) ? 1 : 0;

    // GEH - Note we serialize the task if the team is serialized to make sure implicit parallel region
    //       tasks are not left until program termination to execute.  Also, it helps locality to execute
    //       immediately.
    taskdata->td_flags.task_serial = ( taskdata->td_flags.final
      || taskdata->td_flags.team_serial || taskdata->td_flags.tasking_ser );

    taskdata->td_flags.started     = 0;
    taskdata->td_flags.executing   = 0;
    taskdata->td_flags.complete    = 0;
    taskdata->td_flags.freed       = 0;

    taskdata->td_flags.native      = flags->native;

    taskdata->td_incomplete_child_tasks = 0;
    taskdata->td_allocated_child_tasks  = 1; // start at one because counts current task and children
#if OMP_40_ENABLED
    taskdata->td_taskgroup = parent_task->td_taskgroup; // task inherits the taskgroup from the parent task
#endif
    // Only need to keep track of child task counts if team parallel and tasking not serialized
    if ( !( taskdata -> td_flags.team_serial || taskdata -> td_flags.tasking_ser ) ) {
        KMP_TEST_THEN_INC32( (kmp_int32 *)(& parent_task->td_incomplete_child_tasks) );
#if OMP_40_ENABLED
        if ( parent_task->td_taskgroup )
            KMP_TEST_THEN_INC32( (kmp_int32 *)(& parent_task->td_taskgroup->count) );
#endif
        // Only need to keep track of allocated child tasks for explicit tasks since implicit not deallocated
        if ( taskdata->td_parent->td_flags.tasktype == TASK_EXPLICIT ) {
            KMP_TEST_THEN_INC32( (kmp_int32 *)(& taskdata->td_parent->td_allocated_child_tasks) );
        }
    }

    KA_TRACE(20, ("__kmp_task_alloc(exit): T#%d created task %p parent=%p\n",
                  gtid, taskdata, taskdata->td_parent) );

    return task;
}


kmp_task_t *
__kmpc_omp_task_alloc( ident_t *loc_ref, kmp_int32 gtid, kmp_int32 flags,
                       size_t sizeof_kmp_task_t, size_t sizeof_shareds,
                       kmp_routine_entry_t task_entry )
{
    kmp_task_t *retval;
    kmp_tasking_flags_t *input_flags = (kmp_tasking_flags_t *) & flags;

    input_flags->native = FALSE;
    // __kmp_task_alloc() sets up all other runtime flags

    KA_TRACE(10, ("__kmpc_omp_task_alloc(enter): T#%d loc=%p, flags=(%s) "
                  "sizeof_task=%ld sizeof_shared=%ld entry=%p\n",
                  gtid, loc_ref, input_flags->tiedness ? "tied  " : "untied",
                  sizeof_kmp_task_t, sizeof_shareds, task_entry) );

    retval = __kmp_task_alloc( loc_ref, gtid, input_flags, sizeof_kmp_task_t,
                               sizeof_shareds, task_entry );

    KA_TRACE(20, ("__kmpc_omp_task_alloc(exit): T#%d retval %p\n", gtid, retval) );

    return retval;
}

//-----------------------------------------------------------
//  __kmp_invoke_task: invoke the specified task
//
// gtid: global thread ID of caller
// task: the task to invoke
// current_task: the task to resume after task invokation

static void
__kmp_invoke_task( kmp_int32 gtid, kmp_task_t *task, kmp_taskdata_t * current_task )
{
    kmp_taskdata_t * taskdata = KMP_TASK_TO_TASKDATA(task);
    KA_TRACE(30, ("__kmp_invoke_task(enter): T#%d invoking task %p, current_task=%p\n",
                  gtid, taskdata, current_task) );

    __kmp_task_start( gtid, task, current_task );

    //
    // Invoke the task routine and pass in relevant data.
    // Thunks generated by gcc take a different argument list.
    //
#ifdef KMP_GOMP_COMPAT
    if (taskdata->td_flags.native) {
        ((void (*)(void *))(*(task->routine)))(task->shareds);
    }
    else
#endif /* KMP_GOMP_COMPAT */
    {
        (*(task->routine))(gtid, task);
    }

    __kmp_task_finish( gtid, task, current_task );

    KA_TRACE(30, ("__kmp_inovke_task(exit): T#%d completed task %p, resuming task %p\n",
                  gtid, taskdata, current_task) );
    return;
}

//-----------------------------------------------------------------------
// __kmpc_omp_task_parts: Schedule a thread-switchable task for execution
//
// loc_ref: location of original task pragma (ignored)
// gtid: Global Thread ID of encountering thread
// new_task: task thunk allocated by __kmp_omp_task_alloc() for the ''new task''
// Returns:
//    TASK_CURRENT_NOT_QUEUED (0) if did not suspend and queue current task to be resumed later.
//    TASK_CURRENT_QUEUED (1) if suspended and queued the current task to be resumed later.

kmp_int32
__kmpc_omp_task_parts( ident_t *loc_ref, kmp_int32 gtid, kmp_task_t * new_task)
{
    kmp_taskdata_t * new_taskdata = KMP_TASK_TO_TASKDATA(new_task);

    KA_TRACE(10, ("__kmpc_omp_task_parts(enter): T#%d loc=%p task=%p\n",
                  gtid, loc_ref, new_taskdata ) );

    /* Should we execute the new task or queue it?   For now, let's just always try to
       queue it.  If the queue fills up, then we'll execute it.  */

    if ( __kmp_push_task( gtid, new_task ) == TASK_NOT_PUSHED ) // if cannot defer
    {                                                           // Execute this task immediately
        kmp_taskdata_t * current_task = __kmp_threads[ gtid ] -> th.th_current_task;
        new_taskdata->td_flags.task_serial = 1;
        __kmp_invoke_task( gtid, new_task, current_task );
    }

    KA_TRACE(10, ("__kmpc_omp_task_parts(exit): T#%d returning TASK_CURRENT_NOT_QUEUED: "
                  "loc=%p task=%p, return: TASK_CURRENT_NOT_QUEUED\n", gtid, loc_ref,
                  new_taskdata ) );

    return TASK_CURRENT_NOT_QUEUED;
}


//---------------------------------------------------------------------
// __kmpc_omp_task: Schedule a non-thread-switchable task for execution
// loc_ref: location of original task pragma (ignored)
// gtid: Global Thread ID of encountering thread
// new_task: non-thread-switchable task thunk allocated by __kmp_omp_task_alloc()
// returns:
//
//    TASK_CURRENT_NOT_QUEUED (0) if did not suspend and queue current task to be resumed later.
//    TASK_CURRENT_QUEUED (1) if suspended and queued the current task to be resumed later.

kmp_int32
__kmpc_omp_task( ident_t *loc_ref, kmp_int32 gtid, kmp_task_t * new_task)
{
    kmp_taskdata_t * new_taskdata = KMP_TASK_TO_TASKDATA(new_task);
    kmp_int32 rc;

    KA_TRACE(10, ("__kmpc_omp_task(enter): T#%d loc=%p task=%p\n",
                  gtid, loc_ref, new_taskdata ) );

    /* Should we execute the new task or queue it?   For now, let's just always try to
       queue it.  If the queue fills up, then we'll execute it.  */

    if ( __kmp_push_task( gtid, new_task ) == TASK_NOT_PUSHED ) // if cannot defer
    {                                                           // Execute this task immediately
        kmp_taskdata_t * current_task = __kmp_threads[ gtid ] -> th.th_current_task;
        new_taskdata -> td_flags.task_serial = 1;
        __kmp_invoke_task( gtid, new_task, current_task );
    }

    KA_TRACE(10, ("__kmpc_omp_task(exit): T#%d returning TASK_CURRENT_NOT_QUEUED: loc=%p task=%p\n",
                  gtid, loc_ref, new_taskdata ) );

    return TASK_CURRENT_NOT_QUEUED;
}


//-------------------------------------------------------------------------------------
// __kmpc_omp_taskwait: Wait until all tasks generated by the current task are complete

kmp_int32
__kmpc_omp_taskwait( ident_t *loc_ref, kmp_int32 gtid )
{
    kmp_taskdata_t * taskdata;
    kmp_info_t * thread;
    int thread_finished = FALSE;

    KA_TRACE(10, ("__kmpc_omp_taskwait(enter): T#%d loc=%p\n",
                  gtid, loc_ref) );

    if ( __kmp_tasking_mode != tskm_immediate_exec ) {
        // GEH TODO: shouldn't we have some sort of OMPRAP API calls here to mark begin wait?

        thread = __kmp_threads[ gtid ];
        taskdata = thread -> th.th_current_task;
        taskdata->td_taskwait_counter += 1;
        taskdata->td_taskwait_ident    = loc_ref;
        taskdata->td_taskwait_thread   = gtid + 1;


        if ( ! taskdata->td_flags.team_serial ) {
            // GEH: if team serialized, avoid reading the volatile variable below.
            while ( TCR_4(taskdata -> td_incomplete_child_tasks) != 0 ) {
                __kmp_execute_tasks( thread, gtid, &(taskdata->td_incomplete_child_tasks),
                                     0, FALSE, &thread_finished, 
                                     __kmp_task_stealing_constraint );
            }
        }

        // GEH TODO: shouldn't we have some sort of OMPRAP API calls here to mark end of wait?
        taskdata->td_taskwait_thread = - taskdata->td_taskwait_thread;
    }

    KA_TRACE(10, ("__kmpc_omp_taskwait(exit): T#%d task %p finished waiting, "
                  "returning TASK_CURRENT_NOT_QUEUED\n", gtid, taskdata) );

    return TASK_CURRENT_NOT_QUEUED;
}


//-------------------------------------------------
// __kmpc_omp_taskyield: switch to a different task

kmp_int32
__kmpc_omp_taskyield( ident_t *loc_ref, kmp_int32 gtid, int end_part )
{
    kmp_taskdata_t * taskdata;
    kmp_info_t * thread;
    int thread_finished = FALSE;

    KA_TRACE(10, ("__kmpc_omp_taskyield(enter): T#%d loc=%p end_part = %d\n",
                  gtid, loc_ref, end_part) );

    if ( __kmp_tasking_mode != tskm_immediate_exec ) {
        // GEH TODO: shouldn't we have some sort of OMPRAP API calls here to mark begin wait?

        thread = __kmp_threads[ gtid ];
        taskdata = thread -> th.th_current_task;
        // Should we model this as a task wait or not?
        taskdata->td_taskwait_counter += 1;
        taskdata->td_taskwait_ident    = loc_ref;
        taskdata->td_taskwait_thread   = gtid + 1;

        if ( ! taskdata->td_flags.team_serial ) {
            __kmp_execute_tasks( thread, gtid, NULL, 0, FALSE, &thread_finished,
                                 __kmp_task_stealing_constraint );
        }


        // GEH TODO: shouldn't we have some sort of OMPRAP API calls here to mark end of wait?
        taskdata->td_taskwait_thread = - taskdata->td_taskwait_thread;
    }

    KA_TRACE(10, ("__kmpc_omp_taskyield(exit): T#%d task %p resuming, "
                  "returning TASK_CURRENT_NOT_QUEUED\n", gtid, taskdata) );

    return TASK_CURRENT_NOT_QUEUED;
}


#if OMP_40_ENABLED
//-------------------------------------------------------------------------------------
// __kmpc_taskgroup: Start a new taskgroup

void
__kmpc_taskgroup( ident* loc, int gtid )
{
    kmp_info_t      * thread = __kmp_threads[ gtid ];
    kmp_taskdata_t  * taskdata = thread->th.th_current_task;
    kmp_taskgroup_t * tg_new =
        (kmp_taskgroup_t *)__kmp_thread_malloc( thread, sizeof( kmp_taskgroup_t ) );
    KA_TRACE(10, ("__kmpc_taskgroup: T#%d loc=%p group=%p\n", gtid, loc, tg_new) );
    tg_new->count = 0;
    tg_new->parent = taskdata->td_taskgroup;
    taskdata->td_taskgroup = tg_new;
}


//-------------------------------------------------------------------------------------
// __kmpc_end_taskgroup: Wait until all tasks generated by the current task
//                       and its descendants are complete

void
__kmpc_end_taskgroup( ident* loc, int gtid )
{
    kmp_info_t      * thread = __kmp_threads[ gtid ];
    kmp_taskdata_t  * taskdata = thread->th.th_current_task;
    kmp_taskgroup_t * taskgroup = taskdata->td_taskgroup;
    int thread_finished = FALSE;

    KA_TRACE(10, ("__kmpc_end_taskgroup(enter): T#%d loc=%p\n", gtid, loc) );
    KMP_DEBUG_ASSERT( taskgroup != NULL );

    if ( __kmp_tasking_mode != tskm_immediate_exec ) {

        if ( ! taskdata->td_flags.team_serial ) {
            while ( TCR_4(taskgroup->count) != 0 ) {
                __kmp_execute_tasks( thread, gtid, &(taskgroup->count),
                                     0, FALSE, &thread_finished, 
                                     __kmp_task_stealing_constraint );
            }
        }

    }
    KMP_DEBUG_ASSERT( taskgroup->count == 0 );

    // Restore parent taskgroup for the current task
    taskdata->td_taskgroup = taskgroup->parent;
    __kmp_thread_free( thread, taskgroup );

    KA_TRACE(10, ("__kmpc_end_taskgroup(exit): T#%d task %p finished waiting\n", gtid, taskdata) );
}
#endif


//------------------------------------------------------
// __kmp_remove_my_task: remove a task from my own deque

static kmp_task_t *
__kmp_remove_my_task( kmp_info_t * thread, kmp_int32 gtid, kmp_task_team_t *task_team,
                      kmp_int32 is_constrained )
{
    kmp_task_t * task;
    kmp_taskdata_t * taskdata;
    kmp_thread_data_t *thread_data;
    kmp_uint32 tail;

    KMP_DEBUG_ASSERT( __kmp_tasking_mode != tskm_immediate_exec );
    KMP_DEBUG_ASSERT( task_team -> tt.tt_threads_data != NULL ); // Caller should check this condition

        thread_data = & task_team -> tt.tt_threads_data[ __kmp_tid_from_gtid( gtid ) ];

    KA_TRACE(10, ("__kmp_remove_my_task(enter): T#%d ntasks=%d head=%u tail=%u\n",
                  gtid, thread_data->td.td_deque_ntasks, thread_data->td.td_deque_head,
                  thread_data->td.td_deque_tail) );

    if (TCR_4(thread_data -> td.td_deque_ntasks) == 0) {
        KA_TRACE(10, ("__kmp_remove_my_task(exit #1): T#%d No tasks to remove: ntasks=%d head=%u tail=%u\n",
                      gtid, thread_data->td.td_deque_ntasks, thread_data->td.td_deque_head,
                      thread_data->td.td_deque_tail) );
        return NULL;
    }

    __kmp_acquire_bootstrap_lock( & thread_data -> td.td_deque_lock );

    if (TCR_4(thread_data -> td.td_deque_ntasks) == 0) {
        __kmp_release_bootstrap_lock( & thread_data -> td.td_deque_lock );
        KA_TRACE(10, ("__kmp_remove_my_task(exit #2): T#%d No tasks to remove: ntasks=%d head=%u tail=%u\n",
                      gtid, thread_data->td.td_deque_ntasks, thread_data->td.td_deque_head,
                      thread_data->td.td_deque_tail) );
        return NULL;
    }

    tail = ( thread_data -> td.td_deque_tail - 1 ) & TASK_DEQUE_MASK;  // Wrap index.
    taskdata = thread_data -> td.td_deque[ tail ];

    if (is_constrained) {
        // we need to check if the candidate obeys task scheduling constraint:
        // only child of current task can be scheduled
        kmp_taskdata_t * current = thread->th.th_current_task;
        kmp_int32        level = current->td_level;
        kmp_taskdata_t * parent = taskdata->td_parent;
        while ( parent != current && parent->td_level > level ) {
            parent = parent->td_parent;  // check generation up to the level of the current task
            KMP_DEBUG_ASSERT(parent != NULL);
        }
        if ( parent != current ) {
            // If the tail task is not a child, then no other childs can appear in the deque.
            __kmp_release_bootstrap_lock( & thread_data -> td.td_deque_lock );
            KA_TRACE(10, ("__kmp_remove_my_task(exit #2): T#%d No tasks to remove: ntasks=%d head=%u tail=%u\n",
                          gtid, thread_data->td.td_deque_ntasks, thread_data->td.td_deque_head,
                          thread_data->td.td_deque_tail) );
            return NULL;
        }
    }

    thread_data -> td.td_deque_tail = tail;
    TCW_4(thread_data -> td.td_deque_ntasks, thread_data -> td.td_deque_ntasks - 1);

    __kmp_release_bootstrap_lock( & thread_data->td.td_deque_lock );

    KA_TRACE(10, ("__kmp_remove_my_task(exit #2): T#%d task %p removed: ntasks=%d head=%u tail=%u\n",
                  gtid, taskdata, thread_data->td.td_deque_ntasks, thread_data->td.td_deque_head,
                  thread_data->td.td_deque_tail) );

    task = KMP_TASKDATA_TO_TASK( taskdata );
    return task;
}


//-----------------------------------------------------------
// __kmp_steal_task: remove a task from another thread's deque
// Assume that calling thread has already checked existence of
// task_team thread_data before calling this routine.

static kmp_task_t *
__kmp_steal_task( kmp_info_t *victim, kmp_int32 gtid, kmp_task_team_t *task_team,
                  volatile kmp_uint32 *unfinished_threads, int *thread_finished,
                  kmp_int32 is_constrained )
{
    kmp_task_t * task;
    kmp_taskdata_t * taskdata;
    kmp_thread_data_t *victim_td, *threads_data;
    kmp_int32 victim_tid, thread_tid;

    KMP_DEBUG_ASSERT( __kmp_tasking_mode != tskm_immediate_exec );

    threads_data = task_team -> tt.tt_threads_data;
    KMP_DEBUG_ASSERT( threads_data != NULL );  // Caller should check this condition

    victim_tid = victim->th.th_info.ds.ds_tid;
    victim_td = & threads_data[ victim_tid ];

    KA_TRACE(10, ("__kmp_steal_task(enter): T#%d try to steal from T#%d: task_team=%p ntasks=%d "
                  "head=%u tail=%u\n",
                  gtid, __kmp_gtid_from_thread( victim ), task_team, victim_td->td.td_deque_ntasks,
                  victim_td->td.td_deque_head, victim_td->td.td_deque_tail) );

    if ( (TCR_4(victim_td -> td.td_deque_ntasks) == 0) || // Caller should not check this condition
         (TCR_PTR(victim->th.th_task_team) != task_team)) // GEH: why would this happen?
    {
        KA_TRACE(10, ("__kmp_steal_task(exit #1): T#%d could not steal from T#%d: task_team=%p "
                      "ntasks=%d head=%u tail=%u\n",
                      gtid, __kmp_gtid_from_thread( victim ), task_team, victim_td->td.td_deque_ntasks,
                      victim_td->td.td_deque_head, victim_td->td.td_deque_tail) );
        return NULL;
    }

    __kmp_acquire_bootstrap_lock( & victim_td -> td.td_deque_lock );

    // Check again after we acquire the lock
    if ( (TCR_4(victim_td -> td.td_deque_ntasks) == 0) ||
         (TCR_PTR(victim->th.th_task_team) != task_team)) // GEH: why would this happen?
    {
        __kmp_release_bootstrap_lock( & victim_td -> td.td_deque_lock );
        KA_TRACE(10, ("__kmp_steal_task(exit #2): T#%d could not steal from T#%d: task_team=%p "
                      "ntasks=%d head=%u tail=%u\n",
                      gtid, __kmp_gtid_from_thread( victim ), task_team, victim_td->td.td_deque_ntasks,
                      victim_td->td.td_deque_head, victim_td->td.td_deque_tail) );
        return NULL;
    }

    KMP_DEBUG_ASSERT( victim_td -> td.td_deque != NULL );

    if ( !is_constrained ) {
        taskdata = victim_td -> td.td_deque[ victim_td -> td.td_deque_head ];
        // Bump head pointer and Wrap.
        victim_td -> td.td_deque_head = ( victim_td -> td.td_deque_head + 1 ) & TASK_DEQUE_MASK;
    } else {
        // While we have postponed tasks let's steal from tail of the deque (smaller tasks)
        kmp_int32 tail = ( victim_td -> td.td_deque_tail - 1 ) & TASK_DEQUE_MASK;  // Wrap index.
        taskdata = victim_td -> td.td_deque[ tail ];
        // we need to check if the candidate obeys task scheduling constraint:
        // only child of current task can be scheduled
        kmp_taskdata_t * current = __kmp_threads[ gtid ]->th.th_current_task;
        kmp_int32        level = current->td_level;
        kmp_taskdata_t * parent = taskdata->td_parent;
        while ( parent != current && parent->td_level > level ) {
            parent = parent->td_parent;  // check generation up to the level of the current task
            KMP_DEBUG_ASSERT(parent != NULL);
        }
        if ( parent != current ) {
            // If the tail task is not a child, then no other childs can appear in the deque (?).
            __kmp_release_bootstrap_lock( & victim_td -> td.td_deque_lock );
            KA_TRACE(10, ("__kmp_steal_task(exit #2): T#%d could not steal from T#%d: task_team=%p "
                          "ntasks=%d head=%u tail=%u\n",
                          gtid, __kmp_gtid_from_thread( threads_data[victim_tid].td.td_thr ),
                          task_team, victim_td->td.td_deque_ntasks,
                          victim_td->td.td_deque_head, victim_td->td.td_deque_tail) );
            return NULL;
        }
        victim_td -> td.td_deque_tail = tail;
    }
    if (*thread_finished) {
        // We need to un-mark this victim as a finished victim.  This must be done before
        // releasing the lock, or else other threads (starting with the master victim)
        // might be prematurely released from the barrier!!!
        kmp_uint32 count = KMP_TEST_THEN_INC32( (kmp_int32 *)unfinished_threads );

        KA_TRACE(20, ("__kmp_steal_task: T#%d inc unfinished_threads to %d: task_team=%p\n",
                      gtid, count + 1, task_team) );

        *thread_finished = FALSE;
    }
    TCW_4(victim_td -> td.td_deque_ntasks, TCR_4(victim_td -> td.td_deque_ntasks) - 1);

    __kmp_release_bootstrap_lock( & victim_td -> td.td_deque_lock );

    KA_TRACE(10, ("__kmp_steal_task(exit #3): T#%d stole task %p from T#d: task_team=%p "
                  "ntasks=%d head=%u tail=%u\n",
                  gtid, taskdata, __kmp_gtid_from_thread( victim ), task_team,
                  victim_td->td.td_deque_ntasks, victim_td->td.td_deque_head,
                  victim_td->td.td_deque_tail) );

    task = KMP_TASKDATA_TO_TASK( taskdata );
    return task;
}


//-----------------------------------------------------------------------------
// __kmp_execute_tasks: Choose and execute tasks until either the condition
// is statisfied (return true) or there are none left (return false).
// final_spin is TRUE if this is the spin at the release barrier.
// thread_finished indicates whether the thread is finished executing all
// the tasks it has on its deque, and is at the release barrier.
// spinner is the location on which to spin.
// spinner == NULL means only execute a single task and return.
// checker is the value to check to terminate the spin.

int
__kmp_execute_tasks( kmp_info_t *thread, 
                     kmp_int32 gtid, 
                     volatile kmp_uint *spinner,
                     kmp_uint checker,
                     int final_spin, 
                     int *thread_finished, 
                     kmp_int32 is_constrained )
{
    kmp_task_team_t *     task_team;
    kmp_team_t *          team;
    kmp_thread_data_t *   threads_data;
    kmp_task_t *          task;
    kmp_taskdata_t *      current_task = thread -> th.th_current_task;
    volatile kmp_uint32 * unfinished_threads;
    kmp_int32             nthreads, last_stolen, k, tid;

    KMP_DEBUG_ASSERT( __kmp_tasking_mode != tskm_immediate_exec );
    KMP_DEBUG_ASSERT( thread == __kmp_threads[ gtid ] );

    task_team = thread -> th.th_task_team;
    KMP_DEBUG_ASSERT( task_team != NULL );

    KA_TRACE(15, ("__kmp_execute_tasks(enter): T#%d final_spin=%d *thread_finished=%d\n",
                  gtid, final_spin, *thread_finished) );

    threads_data = (kmp_thread_data_t *)TCR_PTR(task_team -> tt.tt_threads_data);
    KMP_DEBUG_ASSERT( threads_data != NULL );

    nthreads = task_team -> tt.tt_nproc;
    unfinished_threads = &(task_team -> tt.tt_unfinished_threads);
    KMP_DEBUG_ASSERT( nthreads > 1 );
    KMP_DEBUG_ASSERT( TCR_4((int)*unfinished_threads) >= 0 );

    // Choose tasks from our own work queue.
    start:
    while (( task = __kmp_remove_my_task( thread, gtid, task_team, is_constrained )) != NULL ) {
        __kmp_invoke_task( gtid, task, current_task );

        // If this thread is only partway through the barrier and the condition
        // is met, then return now, so that the barrier gather/release pattern can proceed.
        // If this thread is in the last spin loop in the barrier, waiting to be
        // released, we know that the termination condition will not be satisified,
        // so don't waste any cycles checking it.
        if ((spinner == NULL) || ((!final_spin) && (TCR_4(*spinner) == checker))) {
            KA_TRACE(15, ("__kmp_execute_tasks(exit #1): T#%d spin condition satisfied\n", gtid) );
            return TRUE;
        }
        KMP_YIELD( __kmp_library == library_throughput );   // Yield before executing next task
    }

    // This thread's work queue is empty.  If we are in the final spin loop
    // of the barrier, check and see if the termination condition is satisfied.
    if (final_spin) {
        // First, decrement the #unfinished threads, if that has not already
        // been done.  This decrement might be to the spin location, and
        // result in the termination condition being satisfied.
        if (! *thread_finished) {
            kmp_uint32 count = KMP_TEST_THEN_DEC32( (kmp_int32 *)unfinished_threads ) - 1;
            KA_TRACE(20, ("__kmp_execute_tasks(dec #1): T#%d dec unfinished_threads to %d task_team=%p\n",
                          gtid, count, task_team) );
            *thread_finished = TRUE;
        }

        // It is now unsafe to reference thread->th.th_team !!!
        // Decrementing task_team->tt.tt_unfinished_threads can allow the master
        // thread to pass through the barrier, where it might reset each thread's
        // th.th_team field for the next parallel region.
        // If we can steal more work, we know that this has not happened yet.
        if ((spinner != NULL) && (TCR_4(*spinner) == checker)) {
            KA_TRACE(15, ("__kmp_execute_tasks(exit #2): T#%d spin condition satisfied\n", gtid) );
            return TRUE;
        }
    }

    // Try to steal from the last place I stole from successfully.
    tid = thread -> th.th_info.ds.ds_tid;//__kmp_tid_from_gtid( gtid );
    last_stolen = threads_data[ tid ].td.td_deque_last_stolen;

    if (last_stolen != -1) {
        kmp_info_t *other_thread = threads_data[last_stolen].td.td_thr;

        while ((task = __kmp_steal_task( other_thread, gtid, task_team, unfinished_threads,
                                         thread_finished, is_constrained )) != NULL)
        {
            __kmp_invoke_task( gtid, task, current_task );

            // Check to see if this thread can proceed.
            if ((spinner == NULL) || ((!final_spin) && (TCR_4(*spinner) == checker))) {
                KA_TRACE(15, ("__kmp_execute_tasks(exit #3): T#%d spin condition satisfied\n",
                              gtid) );
                return TRUE;
            }

            KMP_YIELD( __kmp_library == library_throughput );   // Yield before executing next task
            // If the execution of the stolen task resulted in more tasks being
            // placed on our run queue, then restart the whole process.
            if (TCR_4(threads_data[ tid ].td.td_deque_ntasks) != 0) {
                KA_TRACE(20, ("__kmp_execute_tasks: T#%d stolen task spawned other tasks, restart\n",
                              gtid) );
                goto start;
            }
        }

        // Don't give priority to stealing from this thread anymore.
        threads_data[ tid ].td.td_deque_last_stolen = -1;

        // The victims's work queue is empty.  If we are in the final spin loop
        // of the barrier, check and see if the termination condition is satisfied.
        if (final_spin) {
            // First, decrement the #unfinished threads, if that has not already
            // been done.  This decrement might be to the spin location, and
            // result in the termination condition being satisfied.
            if (! *thread_finished) {
                kmp_uint32 count = KMP_TEST_THEN_DEC32( (kmp_int32 *)unfinished_threads ) - 1;
                KA_TRACE(20, ("__kmp_execute_tasks(dec #2): T#%d dec unfinished_threads to %d "
                              "task_team=%p\n", gtid, count, task_team) );
                *thread_finished = TRUE;
            }

            // If __kmp_tasking_mode != tskm_immediate_exec
            // then it is now unsafe to reference thread->th.th_team !!!
            // Decrementing task_team->tt.tt_unfinished_threads can allow the master
            // thread to pass through the barrier, where it might reset each thread's
            // th.th_team field for the next parallel region.
            // If we can steal more work, we know that this has not happened yet.
            if ((spinner != NULL) && (TCR_4(*spinner) == checker)) {
                KA_TRACE(15, ("__kmp_execute_tasks(exit #4): T#%d spin condition satisfied\n",
                              gtid) );
                return TRUE;
            }
        }
    }

    // Find a different thread to steal work from.  Pick a random thread.
    // My initial plan was to cycle through all the threads, and only return
    // if we tried to steal from every thread, and failed.  Arch says that's
    // not such a great idea.
    // GEH - need yield code in this loop for throughput library mode?
    new_victim:
    k = __kmp_get_random( thread ) % (nthreads - 1);
    if ( k >= thread -> th.th_info.ds.ds_tid ) {
        ++k;               // Adjusts random distribution to exclude self
    }
    {
        kmp_info_t *other_thread = threads_data[k].td.td_thr;
        int first;

        // There is a slight chance that __kmp_enable_tasking() did not wake up
        // all threads waiting at the barrier.  If this thread is sleeping, then
        // then wake it up.  Since we weree going to pay the cache miss penalty
        // for referenceing another thread's kmp_info_t struct anyway, the check
        // shouldn't cost too much performance at this point.
        // In extra barrier mode, tasks do not sleep at the separate tasking
        // barrier, so this isn't a problem.
        if ( ( __kmp_tasking_mode == tskm_task_teams ) &&
             (__kmp_dflt_blocktime != KMP_MAX_BLOCKTIME) &&
             (TCR_PTR(other_thread->th.th_sleep_loc) != NULL))
        {
            __kmp_resume( __kmp_gtid_from_thread( other_thread ), NULL );

            // A sleeping thread should not have any tasks on it's queue.
            // There is a slight possiblility that it resumes, steals a task from
            // another thread, which spawns more tasks, all in the that it takes
            // this thread to check => don't write an assertion that the victim's
            // queue is empty.  Try stealing from a different thread.
            goto new_victim;
        }

        // Now try to steal work from the selected thread
        first = TRUE;
        while ((task = __kmp_steal_task( other_thread, gtid, task_team, unfinished_threads,
                                         thread_finished, is_constrained )) != NULL)
        {
            __kmp_invoke_task( gtid, task, current_task );

            // Try stealing from this victim again, in the future.
            if (first) {
                threads_data[ tid ].td.td_deque_last_stolen = k;
                first = FALSE;
            }

            // Check to see if this thread can proceed.
            if ((spinner == NULL) || ((!final_spin) && (TCR_4(*spinner) == checker))) {
                KA_TRACE(15, ("__kmp_execute_tasks(exit #5): T#%d spin condition satisfied\n",
                              gtid) );
                return TRUE;
            }
            KMP_YIELD( __kmp_library == library_throughput );   // Yield before executing next task

            // If the execution of the stolen task resulted in more tasks being
            // placed on our run queue, then restart the whole process.
            if (TCR_4(threads_data[ tid ].td.td_deque_ntasks) != 0) {
                KA_TRACE(20, ("__kmp_execute_tasks: T#%d stolen task spawned other tasks, restart\n",
                              gtid) );
                goto start;
            }
        }

        // The victims's work queue is empty.  If we are in the final spin loop
        // of the barrier, check and see if the termination condition is satisfied.
        // Going on and finding a new victim to steal from is expensive, as it
        // involves a lot of cache misses, so we definitely want to re-check the
        // termination condition before doing that.
        if (final_spin) {
            // First, decrement the #unfinished threads, if that has not already
            // been done.  This decrement might be to the spin location, and
            // result in the termination condition being satisfied.
            if (! *thread_finished) {
                kmp_uint32 count = KMP_TEST_THEN_DEC32( (kmp_int32 *)unfinished_threads ) - 1;
                KA_TRACE(20, ("__kmp_execute_tasks(dec #3): T#%d dec unfinished_threads to %d; "
                              "task_team=%p\n",
                              gtid, count, task_team) );
                *thread_finished = TRUE;
            }

            // If __kmp_tasking_mode != tskm_immediate_exec,
            // then it is now unsafe to reference thread->th.th_team !!!
            // Decrementing task_team->tt.tt_unfinished_threads can allow the master
            // thread to pass through the barrier, where it might reset each thread's
            // th.th_team field for the next parallel region.
            // If we can steal more work, we know that this has not happened yet.
            if ((spinner != NULL) && (TCR_4(*spinner) == checker)) {
                KA_TRACE(15, ("__kmp_execute_tasks(exit #6): T#%d spin condition satisfied\n",
                              gtid) );
                return TRUE;
            }
        }
    }

    KA_TRACE(15, ("__kmp_execute_tasks(exit #7): T#%d can't find work\n", gtid) );
    return FALSE;
}


//-----------------------------------------------------------------------------
// __kmp_enable_tasking: Allocate task team and resume threads sleeping at the
// next barrier so they can assist in executing enqueued tasks.
// First thread in allocates the task team atomically.

static void
__kmp_enable_tasking( kmp_task_team_t *task_team, kmp_info_t *this_thr )
{
    kmp_team_t *team = this_thr->th.th_team;
    kmp_thread_data_t *threads_data;
    int nthreads, i, is_init_thread;

    KA_TRACE( 10, ( "__kmp_enable_tasking(enter): T#%d\n",
                    __kmp_gtid_from_thread( this_thr ) ) );

    KMP_DEBUG_ASSERT(task_team != NULL);
    KMP_DEBUG_ASSERT(team != NULL);

    nthreads = task_team->tt.tt_nproc;
    KMP_DEBUG_ASSERT(nthreads > 0);
    KMP_DEBUG_ASSERT(nthreads == team->t.t_nproc);

    // Allocate or increase the size of threads_data if necessary
    is_init_thread = __kmp_realloc_task_threads_data( this_thr, task_team );

    if (!is_init_thread) {
        // Some other thread already set up the array.
        KA_TRACE( 20, ( "__kmp_enable_tasking(exit): T#%d: threads array already set up.\n",
                        __kmp_gtid_from_thread( this_thr ) ) );
        return;
    }
    threads_data = (kmp_thread_data_t *)TCR_PTR(task_team -> tt.tt_threads_data);
    KMP_DEBUG_ASSERT( threads_data != NULL );

    if ( ( __kmp_tasking_mode == tskm_task_teams ) &&
         ( __kmp_dflt_blocktime != KMP_MAX_BLOCKTIME ) )
    {
        // Release any threads sleeping at the barrier, so that they can steal
        // tasks and execute them.  In extra barrier mode, tasks do not sleep
        // at the separate tasking barrier, so this isn't a problem.
        for (i = 0; i < nthreads; i++) {
            volatile kmp_uint *sleep_loc;
            kmp_info_t *thread = threads_data[i].td.td_thr;

            if (i == this_thr->th.th_info.ds.ds_tid) {
                continue;
            }
            // Since we haven't locked the thread's suspend mutex lock at this
            // point, there is a small window where a thread might be putting
            // itself to sleep, but hasn't set the th_sleep_loc field yet.
            // To work around this, __kmp_execute_tasks() periodically checks
            // see if other threads are sleeping (using the same random
            // mechanism that is used for task stealing) and awakens them if
            // they are.
            if ( ( sleep_loc =  (volatile kmp_uint *)
                                TCR_PTR( thread -> th.th_sleep_loc) ) != NULL )
            {
                KF_TRACE( 50, ( "__kmp_enable_tasking: T#%d waking up thread T#%d\n",
                                 __kmp_gtid_from_thread( this_thr ),
                                 __kmp_gtid_from_thread( thread ) ) );
                __kmp_resume( __kmp_gtid_from_thread( thread ), sleep_loc );
            }
            else {
                KF_TRACE( 50, ( "__kmp_enable_tasking: T#%d don't wake up thread T#%d\n",
                                 __kmp_gtid_from_thread( this_thr ),
                                 __kmp_gtid_from_thread( thread ) ) );
            }
        }
    }

    KA_TRACE( 10, ( "__kmp_enable_tasking(exit): T#%d\n",
                    __kmp_gtid_from_thread( this_thr ) ) );
}


/* ------------------------------------------------------------------------ */
/*
 * Utility routines for "task teams".  A task team (kmp_task_t) is kind of
 * like a shadow of the kmp_team_t data struct, with a different lifetime.
 * After a child * thread checks into a barrier and calls __kmp_release() from
 * the particular variant of __kmp_<barrier_kind>_barrier_gather(), it can no
 * longer assume that the kmp_team_t structure is intact (at any moment, the
 * master thread may exit the barrier code and free the team data structure,
 * and return the threads to the thread pool).
 *
 * This does not work with the the tasking code, as the thread is still
 * expected to participate in the execution of any tasks that may have been
 * spawned my a member of the team, and the thread still needs access to all
 * to each thread in the team, so that it can steal work from it.
 *
 * Enter the existence of the kmp_task_team_t struct.  It employs a reference
 * counting mechanims, and is allocated by the master thread before calling
 * __kmp_<barrier_kind>_release, and then is release by the last thread to
 * exit __kmp_<barrier_kind>_release at the next barrier.  I.e. the lifetimes
 * of the kmp_task_team_t structs for consecutive barriers can overlap
 * (and will, unless the master thread is the last thread to exit the barrier
 * release phase, which is not typical).
 *
 * The existence of such a struct is useful outside the context of tasking,
 * but for now, I'm trying to keep it specific to the OMP_30_ENABLED macro,
 * so that any performance differences show up when comparing the 2.5 vs. 3.0
 * libraries.
 *
 * We currently use the existence of the threads array as an indicator that
 * tasks were spawned since the last barrier.  If the structure is to be
 * useful outside the context of tasking, then this will have to change, but
 * not settting the field minimizes the performance impact of tasking on
 * barriers, when no explicit tasks were spawned (pushed, actually).
 */

static kmp_task_team_t *__kmp_free_task_teams = NULL;           // Free list for task_team data structures
// Lock for task team data structures
static kmp_bootstrap_lock_t __kmp_task_team_lock = KMP_BOOTSTRAP_LOCK_INITIALIZER( __kmp_task_team_lock );


//------------------------------------------------------------------------------
// __kmp_alloc_task_deque:
// Allocates a task deque for a particular thread, and initialize the necessary
// data structures relating to the deque.  This only happens once per thread
// per task team since task teams are recycled.
// No lock is needed during allocation since each thread allocates its own
// deque.

static void
__kmp_alloc_task_deque( kmp_info_t *thread, kmp_thread_data_t *thread_data )
{
    __kmp_init_bootstrap_lock( & thread_data -> td.td_deque_lock );
    KMP_DEBUG_ASSERT( thread_data -> td.td_deque == NULL );

    // Initialize last stolen task field to "none"
    thread_data -> td.td_deque_last_stolen = -1;

    KMP_DEBUG_ASSERT( TCR_4(thread_data -> td.td_deque_ntasks) == 0 );
    KMP_DEBUG_ASSERT( thread_data -> td.td_deque_head == 0 );
    KMP_DEBUG_ASSERT( thread_data -> td.td_deque_tail == 0 );

    KE_TRACE( 10, ( "__kmp_alloc_task_deque: T#%d allocating deque[%d] for thread_data %p\n",
                   __kmp_gtid_from_thread( thread ), TASK_DEQUE_SIZE, thread_data ) );
    // Allocate space for task deque, and zero the deque
    // Cannot use __kmp_thread_calloc() because threads not around for
    // kmp_reap_task_team( ).
    thread_data -> td.td_deque = (kmp_taskdata_t **)
            __kmp_allocate( TASK_DEQUE_SIZE * sizeof(kmp_taskdata_t *));
}


//------------------------------------------------------------------------------
// __kmp_free_task_deque:
// Deallocates a task deque for a particular thread.
// Happens at library deallocation so don't need to reset all thread data fields.

static void
__kmp_free_task_deque( kmp_thread_data_t *thread_data )
{
    __kmp_acquire_bootstrap_lock( & thread_data -> td.td_deque_lock );

    if ( thread_data -> td.td_deque != NULL ) {
        TCW_4(thread_data -> td.td_deque_ntasks, 0);
         __kmp_free( thread_data -> td.td_deque );
        thread_data -> td.td_deque = NULL;
    }
    __kmp_release_bootstrap_lock( & thread_data -> td.td_deque_lock );

#ifdef BUILD_TIED_TASK_STACK
    // GEH: Figure out what to do here for td_susp_tied_tasks
    if ( thread_data -> td.td_susp_tied_tasks.ts_entries != TASK_STACK_EMPTY ) {
        __kmp_free_task_stack( __kmp_thread_from_gtid( gtid ), thread_data );
    }
#endif // BUILD_TIED_TASK_STACK
}


//------------------------------------------------------------------------------
// __kmp_realloc_task_threads_data:
// Allocates a threads_data array for a task team, either by allocating an initial
// array or enlarging an existing array.  Only the first thread to get the lock
// allocs or enlarges the array and re-initializes the array eleemnts.
// That thread returns "TRUE", the rest return "FALSE".
// Assumes that the new array size is given by task_team -> tt.tt_nproc.
// The current size is given by task_team -> tt.tt_max_threads.

static int
__kmp_realloc_task_threads_data( kmp_info_t *thread, kmp_task_team_t *task_team )
{
    kmp_thread_data_t ** threads_data_p;
    kmp_int32            nthreads, maxthreads;
    int                  is_init_thread = FALSE;

    if ( TCR_4(task_team -> tt.tt_found_tasks) ) {
        // Already reallocated and initialized.
        return FALSE;
    }

    threads_data_p = & task_team -> tt.tt_threads_data;
    nthreads   = task_team -> tt.tt_nproc;
    maxthreads = task_team -> tt.tt_max_threads;

    // All threads must lock when they encounter the first task of the implicit task
    // region to make sure threads_data fields are (re)initialized before used.
    __kmp_acquire_bootstrap_lock( & task_team -> tt.tt_threads_lock );

    if ( ! TCR_4(task_team -> tt.tt_found_tasks) ) {
        // first thread to enable tasking
        kmp_team_t *team = thread -> th.th_team;
        int i;

        is_init_thread = TRUE;
        if ( maxthreads < nthreads ) {

            if ( *threads_data_p != NULL ) {
                kmp_thread_data_t *old_data = *threads_data_p;
                kmp_thread_data_t *new_data = NULL;

                KE_TRACE( 10, ( "__kmp_realloc_task_threads_data: T#%d reallocating "
                               "threads data for task_team %p, new_size = %d, old_size = %d\n",
                               __kmp_gtid_from_thread( thread ), task_team,
                               nthreads, maxthreads ) );
                // Reallocate threads_data to have more elements than current array
                // Cannot use __kmp_thread_realloc() because threads not around for
                // kmp_reap_task_team( ).  Note all new array entries are initialized
                // to zero by __kmp_allocate().
                new_data = (kmp_thread_data_t *)
                            __kmp_allocate( nthreads * sizeof(kmp_thread_data_t) );
                // copy old data to new data
                memcpy( (void *) new_data, (void *) old_data,
                        maxthreads * sizeof(kmp_taskdata_t *) );

#ifdef BUILD_TIED_TASK_STACK
                // GEH: Figure out if this is the right thing to do
                for (i = maxthreads; i < nthreads; i++) {
                    kmp_thread_data_t *thread_data = & (*threads_data_p)[i];
                    __kmp_init_task_stack( __kmp_gtid_from_thread( thread ), thread_data );
                }
#endif // BUILD_TIED_TASK_STACK
                // Install the new data and free the old data
                (*threads_data_p) = new_data;
                __kmp_free( old_data );
            }
            else {
                KE_TRACE( 10, ( "__kmp_realloc_task_threads_data: T#%d allocating "
                               "threads data for task_team %p, size = %d\n",
                               __kmp_gtid_from_thread( thread ), task_team, nthreads ) );
                // Make the initial allocate for threads_data array, and zero entries
                // Cannot use __kmp_thread_calloc() because threads not around for
                // kmp_reap_task_team( ).
                *threads_data_p = (kmp_thread_data_t *)
                                  __kmp_allocate( nthreads * sizeof(kmp_thread_data_t) );
#ifdef BUILD_TIED_TASK_STACK
                // GEH: Figure out if this is the right thing to do
                for (i = 0; i < nthreads; i++) {
                    kmp_thread_data_t *thread_data = & (*threads_data_p)[i];
                    __kmp_init_task_stack( __kmp_gtid_from_thread( thread ), thread_data );
                }
#endif // BUILD_TIED_TASK_STACK
            }
            task_team -> tt.tt_max_threads = nthreads;
        }
        else {
            // If array has (more than) enough elements, go ahead and use it
            KMP_DEBUG_ASSERT( *threads_data_p != NULL );
        }

        // initialize threads_data pointers back to thread_info structures
        for (i = 0; i < nthreads; i++) {
            kmp_thread_data_t *thread_data = & (*threads_data_p)[i];
            thread_data -> td.td_thr = team -> t.t_threads[i];

            if ( thread_data -> td.td_deque_last_stolen >= nthreads) {
                // The last stolen field survives across teams / barrier, and the number
                // of threads may have changed.  It's possible (likely?) that a new
                // parallel region will exhibit the same behavior as the previous region.
                thread_data -> td.td_deque_last_stolen = -1;
            }
        }

        KMP_MB();
        TCW_SYNC_4(task_team -> tt.tt_found_tasks, TRUE);
    }

    __kmp_release_bootstrap_lock( & task_team -> tt.tt_threads_lock );
    return is_init_thread;
}


//------------------------------------------------------------------------------
// __kmp_free_task_threads_data:
// Deallocates a threads_data array for a task team, including any attached
// tasking deques.  Only occurs at library shutdown.

static void
__kmp_free_task_threads_data( kmp_task_team_t *task_team )
{
    __kmp_acquire_bootstrap_lock( & task_team -> tt.tt_threads_lock );
    if ( task_team -> tt.tt_threads_data != NULL ) {
        int i;
        for (i = 0; i < task_team->tt.tt_max_threads; i++ ) {
            __kmp_free_task_deque( & task_team -> tt.tt_threads_data[i] );
        }
        __kmp_free( task_team -> tt.tt_threads_data );
        task_team -> tt.tt_threads_data = NULL;
    }
    __kmp_release_bootstrap_lock( & task_team -> tt.tt_threads_lock );
}


//------------------------------------------------------------------------------
// __kmp_allocate_task_team:
// Allocates a task team associated with a specific team, taking it from
// the global task team free list if possible.  Also initializes data structures.

static kmp_task_team_t *
__kmp_allocate_task_team( kmp_info_t *thread, kmp_team_t *team )
{
    kmp_task_team_t *task_team = NULL;
    int nthreads;

    KA_TRACE( 20, ( "__kmp_allocate_task_team: T#%d entering; team = %p\n",
                    (thread ? __kmp_gtid_from_thread( thread ) : -1), team ) );

    if (TCR_PTR(__kmp_free_task_teams) != NULL) {
        // Take a task team from the task team pool
        __kmp_acquire_bootstrap_lock( &__kmp_task_team_lock );
        if (__kmp_free_task_teams != NULL) {
            task_team = __kmp_free_task_teams;
            TCW_PTR(__kmp_free_task_teams, task_team -> tt.tt_next);
            task_team -> tt.tt_next = NULL;
        }
        __kmp_release_bootstrap_lock( &__kmp_task_team_lock );
    }

    if (task_team == NULL) {
        KE_TRACE( 10, ( "__kmp_allocate_task_team: T#%d allocating "
                       "task team for team %p\n",
                       __kmp_gtid_from_thread( thread ), team ) );
        // Allocate a new task team if one is not available.
        // Cannot use __kmp_thread_malloc() because threads not around for
        // kmp_reap_task_team( ).
        task_team = (kmp_task_team_t *) __kmp_allocate( sizeof(kmp_task_team_t) );
        __kmp_init_bootstrap_lock( & task_team -> tt.tt_threads_lock );
        //task_team -> tt.tt_threads_data = NULL;   // AC: __kmp_allocate zeroes returned memory
        //task_team -> tt.tt_max_threads = 0;
        //task_team -> tt.tt_next = NULL;
    }

    TCW_4(task_team -> tt.tt_found_tasks, FALSE);
    task_team -> tt.tt_nproc = nthreads = team->t.t_nproc;

    task_team -> tt.tt_state = 0;
    TCW_4( task_team -> tt.tt_unfinished_threads, nthreads );
    TCW_4( task_team -> tt.tt_active, TRUE );
    TCW_4( task_team -> tt.tt_ref_ct, nthreads - 1);

    KA_TRACE( 20, ( "__kmp_allocate_task_team: T#%d exiting; task_team = %p\n",
                    (thread ? __kmp_gtid_from_thread( thread ) : -1), task_team ) );
    return task_team;
}


//------------------------------------------------------------------------------
// __kmp_free_task_team:
// Frees the task team associated with a specific thread, and adds it
// to the global task team free list.
//

static void
__kmp_free_task_team( kmp_info_t *thread, kmp_task_team_t *task_team )
{
    KA_TRACE( 20, ( "__kmp_free_task_team: T#%d task_team = %p\n",
                    thread ? __kmp_gtid_from_thread( thread ) : -1, task_team ) );

    KMP_DEBUG_ASSERT( TCR_4(task_team -> tt.tt_ref_ct) == 0 );

    // Put task team back on free list
    __kmp_acquire_bootstrap_lock( & __kmp_task_team_lock );

    KMP_DEBUG_ASSERT( task_team -> tt.tt_next == NULL );
    task_team -> tt.tt_next = __kmp_free_task_teams;
    TCW_4(task_team -> tt.tt_found_tasks, FALSE);
    TCW_PTR(__kmp_free_task_teams, task_team);

    __kmp_release_bootstrap_lock( & __kmp_task_team_lock );
}


//------------------------------------------------------------------------------
// __kmp_reap_task_teams:
// Free all the task teams on the task team free list.
// Should only be done during library shutdown.
// Cannot do anything that needs a thread structure or gtid since they are already gone.

void
__kmp_reap_task_teams( void )
{
    kmp_task_team_t   *task_team;

    if ( TCR_PTR(__kmp_free_task_teams) != NULL ) {
        // Free all task_teams on the free list
        __kmp_acquire_bootstrap_lock( &__kmp_task_team_lock );
        while ( ( task_team = __kmp_free_task_teams ) != NULL ) {
            __kmp_free_task_teams = task_team -> tt.tt_next;
            task_team -> tt.tt_next = NULL;

            // Free threads_data if necessary
            if ( task_team -> tt.tt_threads_data != NULL ) {
                __kmp_free_task_threads_data( task_team );
            }
            __kmp_free( task_team );
        }
        __kmp_release_bootstrap_lock( &__kmp_task_team_lock );
    }
}


//------------------------------------------------------------------------------
// __kmp_unref_task_teams:
// Remove one thread from referencing the task team structure by
// decreasing the reference count and deallocate task team if no more
// references to it.
//
void
__kmp_unref_task_team( kmp_task_team_t *task_team, kmp_info_t *thread )
{
    kmp_uint ref_ct;

    ref_ct = KMP_TEST_THEN_DEC32( (kmp_int32 *)(& task_team->tt.tt_ref_ct) ) - 1;

    KA_TRACE( 20, ( "__kmp_unref_task_team: T#%d task_team = %p ref_ct = %d\n",
                    __kmp_gtid_from_thread( thread ), task_team, ref_ct ) );


    if ( ref_ct == 0 ) {
        __kmp_free_task_team( thread, task_team );
    }

    TCW_PTR( *((volatile kmp_task_team_t **)(&thread->th.th_task_team)), NULL );
}


//------------------------------------------------------------------------------
// __kmp_wait_to_unref_task_teams:
// Some threads could still be in the fork barrier release code, possibly
// trying to steal tasks.  Wait for each thread to unreference its task team.
//
void
__kmp_wait_to_unref_task_teams(void)
{
    kmp_info_t *thread;
    kmp_uint32 spins;
    int done;

    KMP_INIT_YIELD( spins );


    for (;;) {
        done = TRUE;

        // TODO: GEH - this may be is wrong because some sync would be necessary
        //             in case threads are added to the pool during the traversal.
        //             Need to verify that lock for thread pool is held when calling
        //             this routine.
        for (thread = (kmp_info_t *)__kmp_thread_pool;
             thread != NULL;
             thread = thread->th.th_next_pool)
        {
            volatile kmp_uint *sleep_loc;
#if KMP_OS_WINDOWS
            DWORD exit_val;
#endif
            if ( TCR_PTR(thread->th.th_task_team) == NULL ) {
                KA_TRACE( 10, ("__kmp_wait_to_unref_task_team: T#%d task_team == NULL\n",
                               __kmp_gtid_from_thread( thread ) ) );
                continue;
            }
#if KMP_OS_WINDOWS
            // TODO: GEH - add this check for Linux* OS / OS X* as well?
            if (!__kmp_is_thread_alive(thread, &exit_val)) {
                if (TCR_PTR(thread->th.th_task_team) != NULL) {
                    __kmp_unref_task_team( thread->th.th_task_team, thread );
                }
                continue;
            }
#endif

            done = FALSE;  // Because th_task_team pointer is not NULL for this thread

            KA_TRACE( 10, ("__kmp_wait_to_unref_task_team: Waiting for T#%d to unreference task_team\n",
                           __kmp_gtid_from_thread( thread ) ) );

            if ( __kmp_dflt_blocktime != KMP_MAX_BLOCKTIME ) {
                // If the thread is sleeping, awaken it.
                if ( ( sleep_loc = (volatile kmp_uint *) TCR_PTR( thread->th.th_sleep_loc) ) != NULL ) {
                    KA_TRACE( 10, ( "__kmp_wait_to_unref_task_team: T#%d waking up thread T#%d\n",
                                    __kmp_gtid_from_thread( thread ), __kmp_gtid_from_thread( thread ) ) );
                    __kmp_resume( __kmp_gtid_from_thread( thread ), sleep_loc );
                }
            }
        }
        if (done) {
            break;
        }

        // If we are oversubscribed,
        // or have waited a bit (and library mode is throughput), yield.
        // Pause is in the following code.
        KMP_YIELD( TCR_4(__kmp_nth) > __kmp_avail_proc );
        KMP_YIELD_SPIN( spins );        // Yields only if KMP_LIBRARY=throughput
    }


}


//------------------------------------------------------------------------------
// __kmp_task_team_setup:  Create a task_team for the current team, but use
// an already created, unused one if it already exists.
// This may be called by any thread, but only for teams with # threads >1.

void
__kmp_task_team_setup( kmp_info_t *this_thr, kmp_team_t *team )
{
    KMP_DEBUG_ASSERT( __kmp_tasking_mode != tskm_immediate_exec );

    if ( ( team->t.t_task_team == NULL ) && ( team->t.t_nproc > 1 ) ) {
        // Allocate a new task team, which will be propagated to
        // all of the worker threads after the barrier.  As they
        // spin in the barrier release phase, then will continue
        // to use the previous task team struct, until they receive
        // the signal to stop checking for tasks (they can't safely
        // reference the kmp_team_t struct, which could be reallocated
        // by the master thread).
        team->t.t_task_team = __kmp_allocate_task_team( this_thr, team );
        KA_TRACE( 20, ( "__kmp_task_team_setup: Master T#%d created new "
                        "task_team %p for team %d\n",
                        __kmp_gtid_from_thread( this_thr ), team->t.t_task_team,
                        ((team != NULL) ? team->t.t_id : -1)) );
    }
    else {
        // All threads have reported in, and no tasks were spawned
        // for this release->gather region.  Leave the old task
        // team struct in place for the upcoming region.  No task
        // teams are formed for serialized teams.
    }
    if ( team->t.t_task_team != NULL ) {
        // Toggle the state flag so that we can tell which side of
        // the barrier we are on.
        team->t.t_task_team->tt.tt_state = 1 - this_thr->th.th_task_state;
    }
}


//------------------------------------------------------------------------------
// __kmp_task_team_sync: Propagation of task team data from team to threads
// which happens just after the release phase of a team barrier.  This may be
// called by any thread, but only for teams with # threads > 1.

void
__kmp_task_team_sync( kmp_info_t *this_thr, kmp_team_t *team )
{
    KMP_DEBUG_ASSERT( __kmp_tasking_mode != tskm_immediate_exec );

    // On the rare chance that this thread never saw that the task
    // team was no longer active, then unref/deallocate it now.
    if ( this_thr->th.th_task_team != NULL ) {
        if ( ! TCR_SYNC_4( this_thr->th.th_task_team->tt.tt_active ) ) {
            KMP_DEBUG_ASSERT( ! KMP_MASTER_TID( __kmp_tid_from_gtid( __kmp_gtid_from_thread( this_thr ) ) ) );
            __kmp_unref_task_team( this_thr->th.th_task_team, this_thr );
        } else {
            //
            // We are re-using a task team that was never enabled.
            //
            KMP_DEBUG_ASSERT( this_thr->th.th_task_team == team->t.t_task_team );
        }
    }

    //
    // It is now safe to propagate the task team pointer from the
    // team struct to the current thread.
    //
    TCW_PTR(this_thr->th.th_task_team, team->t.t_task_team);
    if ( this_thr->th.th_task_team != NULL ) {
        //
        // Toggle the th_task_state field, instead of reading it from
        // the task team.  Reading the tt_state field at this point
        // causes a 30% regression on EPCC parallel - toggling it
        // is much cheaper.
        //
        this_thr->th.th_task_state = 1 - this_thr->th.th_task_state;
        KMP_DEBUG_ASSERT( this_thr->th.th_task_state == TCR_4(team->t.t_task_team->tt.tt_state) );
    }
    KA_TRACE( 20, ( "__kmp_task_team_sync: Thread T#%d task team assigned pointer (%p) from Team #%d task team\n",
                    __kmp_gtid_from_thread( this_thr ), &this_thr->th.th_task_team,
                    this_thr->th.th_task_team, ((team != NULL) ? (team->t.t_id) : -1) ) );
}


//------------------------------------------------------------------------------
// __kmp_task_team_wait: Master thread waits for outstanding tasks after
// the barrier gather phase.  Only called by master thread if #threads
// in team > 1 !

void
__kmp_task_team_wait( kmp_info_t *this_thr, 
                      kmp_team_t *team
                      )
{
    kmp_task_team_t *task_team = team->t.t_task_team;

    KMP_DEBUG_ASSERT( __kmp_tasking_mode != tskm_immediate_exec );
    KMP_DEBUG_ASSERT( task_team == this_thr->th.th_task_team );

    if ( ( task_team != NULL ) && KMP_TASKING_ENABLED( task_team, this_thr->th.th_task_state ) ) {
        KA_TRACE( 20, ( "__kmp_task_team_wait: Master T#%d waiting for all tasks: task_team = %p\n",
                          __kmp_gtid_from_thread( this_thr ), task_team ) );
        //
        // All worker threads might have dropped through to the
        // release phase, but could still be executing tasks.
        // Wait here for all tasks to complete.  To avoid memory
        // contention, only the master thread checks for the
        // termination condition.
        //
        __kmp_wait_sleep( this_thr, &task_team->tt.tt_unfinished_threads, 0, TRUE
                          );

        //
        // Kill the old task team, so that the worker threads will
        // stop referencing it while spinning.  They will
        // deallocate it when the reference count reaches zero.
        // The master thread is not included in the ref count.
        //
        KA_TRACE( 20, ( "__kmp_task_team_wait: Master T#%d deactivating task_team %p\n",
                          __kmp_gtid_from_thread( this_thr ), task_team ) );
        KMP_DEBUG_ASSERT( task_team->tt.tt_nproc > 1 );
        TCW_SYNC_4( task_team->tt.tt_active, FALSE );
        KMP_MB();

        TCW_PTR(this_thr->th.th_task_team, NULL);
        team->t.t_task_team = NULL;
    }
}


//------------------------------------------------------------------------------
// __kmp_tasking_barrier:
// Internal function to execute all tasks prior to a regular barrier or a
// join barrier.  It is a full barrier itself, which unfortunately turns
// regular barriers into double barriers and join barriers into 1 1/2
// barriers.
// This routine may only called when __kmp_tasking_mode == tskm_extra_barrier.

void
__kmp_tasking_barrier( kmp_team_t *team, kmp_info_t *thread, int gtid )
{
    volatile kmp_uint32 *spin = &team->t.t_task_team->tt.tt_unfinished_threads;
    int flag = FALSE;
    KMP_DEBUG_ASSERT( __kmp_tasking_mode == tskm_extra_barrier );

    while (! __kmp_execute_tasks( thread, gtid, spin, 0, TRUE, &flag, NULL ) ) {

        if( TCR_4(__kmp_global.g.g_done) ) {
            if( __kmp_global.g.g_abort )
                __kmp_abort_thread( );
            break;
        }
        KMP_YIELD( TRUE );       // GH: We always yield here
    }
}

#endif // OMP_30_ENABLED

