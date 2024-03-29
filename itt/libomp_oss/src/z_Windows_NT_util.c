/*
 * z_Windows_NT_util.c -- platform specific routines.
 * $Revision: 42248 $
 * $Date: 2013-04-03 07:08:13 -0500 (Wed, 03 Apr 2013) $
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
#include "kmp_io.h"



/* ----------------------------------------------------------------------------------- */
/* ----------------------------------------------------------------------------------- */

/* This code is related to NtQuerySystemInformation() function. This function 
   is used in the Load balance algorithm for OMP_DYNAMIC=true to find the 
   number of running threads in the system. */

#include <ntstatus.h>
#include <ntsecapi.h>   // UNICODE_STRING

enum SYSTEM_INFORMATION_CLASS {
    SystemProcessInformation = 5
}; // SYSTEM_INFORMATION_CLASS

struct CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
}; // struct CLIENT_ID

enum THREAD_STATE {
    StateInitialized,
    StateReady,
    StateRunning,
    StateStandby,
    StateTerminated,
    StateWait,
    StateTransition,
    StateUnknown
}; // enum THREAD_STATE

struct VM_COUNTERS {
    SIZE_T        PeakVirtualSize;
    SIZE_T        VirtualSize;
    ULONG         PageFaultCount;
    SIZE_T        PeakWorkingSetSize;
    SIZE_T        WorkingSetSize;
    SIZE_T        QuotaPeakPagedPoolUsage;
    SIZE_T        QuotaPagedPoolUsage;
    SIZE_T        QuotaPeakNonPagedPoolUsage;
    SIZE_T        QuotaNonPagedPoolUsage;
    SIZE_T        PagefileUsage;
    SIZE_T        PeakPagefileUsage;
    SIZE_T        PrivatePageCount;
}; // struct VM_COUNTERS

struct SYSTEM_THREAD {
  LARGE_INTEGER   KernelTime;
  LARGE_INTEGER   UserTime;
  LARGE_INTEGER   CreateTime;
  ULONG           WaitTime;
  LPVOID          StartAddress;
  CLIENT_ID       ClientId;
  DWORD           Priority;
  LONG            BasePriority;
  ULONG           ContextSwitchCount;
  THREAD_STATE    State;
  ULONG           WaitReason;
}; // SYSTEM_THREAD

KMP_BUILD_ASSERT( offsetof( SYSTEM_THREAD, KernelTime ) == 0 );
#if KMP_ARCH_X86
    KMP_BUILD_ASSERT( offsetof( SYSTEM_THREAD, StartAddress ) == 28 );
    KMP_BUILD_ASSERT( offsetof( SYSTEM_THREAD, State        ) == 52 );
#else
    KMP_BUILD_ASSERT( offsetof( SYSTEM_THREAD, StartAddress ) == 32 );
    KMP_BUILD_ASSERT( offsetof( SYSTEM_THREAD, State        ) == 68 );
#endif

struct SYSTEM_PROCESS_INFORMATION {
  ULONG           NextEntryOffset;
  ULONG           NumberOfThreads;
  LARGE_INTEGER   Reserved[ 3 ];
  LARGE_INTEGER   CreateTime;
  LARGE_INTEGER   UserTime;
  LARGE_INTEGER   KernelTime;
  UNICODE_STRING  ImageName;
  DWORD           BasePriority;
  HANDLE          ProcessId;
  HANDLE          ParentProcessId;
  ULONG           HandleCount;
  ULONG           Reserved2[ 2 ];
  VM_COUNTERS     VMCounters;
  IO_COUNTERS     IOCounters;
  SYSTEM_THREAD   Threads[ 1 ];
}; // SYSTEM_PROCESS_INFORMATION
typedef SYSTEM_PROCESS_INFORMATION * PSYSTEM_PROCESS_INFORMATION;

KMP_BUILD_ASSERT( offsetof( SYSTEM_PROCESS_INFORMATION, NextEntryOffset ) ==  0 );
KMP_BUILD_ASSERT( offsetof( SYSTEM_PROCESS_INFORMATION, CreateTime      ) == 32 );
KMP_BUILD_ASSERT( offsetof( SYSTEM_PROCESS_INFORMATION, ImageName       ) == 56 );
#if KMP_ARCH_X86
    KMP_BUILD_ASSERT( offsetof( SYSTEM_PROCESS_INFORMATION, ProcessId       ) ==  68 );
    KMP_BUILD_ASSERT( offsetof( SYSTEM_PROCESS_INFORMATION, HandleCount     ) ==  76 );
    KMP_BUILD_ASSERT( offsetof( SYSTEM_PROCESS_INFORMATION, VMCounters      ) ==  88 );
    KMP_BUILD_ASSERT( offsetof( SYSTEM_PROCESS_INFORMATION, IOCounters      ) == 136 );
    KMP_BUILD_ASSERT( offsetof( SYSTEM_PROCESS_INFORMATION, Threads         ) == 184 );
#else
    KMP_BUILD_ASSERT( offsetof( SYSTEM_PROCESS_INFORMATION, ProcessId       ) ==  80 );
    KMP_BUILD_ASSERT( offsetof( SYSTEM_PROCESS_INFORMATION, HandleCount     ) ==  96 );
    KMP_BUILD_ASSERT( offsetof( SYSTEM_PROCESS_INFORMATION, VMCounters      ) == 112 );
    KMP_BUILD_ASSERT( offsetof( SYSTEM_PROCESS_INFORMATION, IOCounters      ) == 208 );
    KMP_BUILD_ASSERT( offsetof( SYSTEM_PROCESS_INFORMATION, Threads         ) == 256 );
#endif

typedef NTSTATUS (NTAPI *NtQuerySystemInformation_t)( SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG );
NtQuerySystemInformation_t NtQuerySystemInformation = NULL;

HMODULE ntdll = NULL;

/* End of NtQuerySystemInformation()-related code */

#if KMP_ARCH_X86_64
static HMODULE kernel32 = NULL;
#endif /* KMP_ARCH_X86_64 */

/* ----------------------------------------------------------------------------------- */
/* ----------------------------------------------------------------------------------- */


// Why do we have multiple copies of __kmp_static_delay() and __kmp_static_yield() in many files?
#ifdef KMP_DEBUG

static void
__kmp_static_delay( int arg ) {
    /* Work around weird code-gen bug that causes assert to trip */
    #if KMP_ARCH_X86_64 && KMP_OS_LINUX
        KMP_ASSERT( arg != 0 );
    #else
        KMP_ASSERT( arg >= 0 );
    #endif
}

#else

    #define __kmp_static_delay( arg )     /* nothing to do */

#endif /* KMP_DEBUG */

static void
__kmp_static_yield( int arg )
{
    __kmp_yield( arg );
}

#if KMP_HANDLE_SIGNALS
    typedef void    (* sig_func_t )( int );
    static sig_func_t  __kmp_sighldrs[ NSIG ];
    static int         __kmp_siginstalled[ NSIG ];
#endif

static HANDLE   __kmp_monitor_ev;
static kmp_int64 __kmp_win32_time;
double __kmp_win32_tick;

int __kmp_init_runtime = FALSE;
CRITICAL_SECTION __kmp_win32_section;

void
__kmp_win32_mutex_init( kmp_win32_mutex_t *mx )
{
    InitializeCriticalSection( & mx->cs );
}

void
__kmp_win32_mutex_destroy( kmp_win32_mutex_t *mx )
{
    DeleteCriticalSection( & mx->cs );
}

void
__kmp_win32_mutex_lock( kmp_win32_mutex_t *mx )
{
    EnterCriticalSection( & mx->cs );
}

void
__kmp_win32_mutex_unlock( kmp_win32_mutex_t *mx )
{
    LeaveCriticalSection( & mx->cs );
}

void
__kmp_win32_cond_init( kmp_win32_cond_t *cv )
{
    cv->waiters_count_         = 0;
    cv->wait_generation_count_ = 0;
    cv->release_count_         = 0;

    /* Initialize the critical section */
    __kmp_win32_mutex_init( & cv->waiters_count_lock_ );

    /* Create a manual-reset event. */
    cv->event_ = CreateEvent( NULL,     // no security
                              TRUE,     // manual-reset
                              FALSE,    // non-signaled initially
                              NULL );   // unnamed
}

void
__kmp_win32_cond_destroy( kmp_win32_cond_t *cv )
{
    __kmp_win32_mutex_destroy( & cv->waiters_count_lock_ );
    __kmp_free_handle( cv->event_ );
    memset( cv, '\0', sizeof( *cv ) );
}

/* TODO associate cv with a team instead of a thread so as to optimize
 * the case where we wake up a whole team */

void
__kmp_win32_cond_wait( kmp_win32_cond_t *cv, kmp_win32_mutex_t *mx, kmp_info_t *th, int need_decrease_load )
{
    int my_generation;
    int last_waiter;

    /* Avoid race conditions */
    __kmp_win32_mutex_lock( &cv->waiters_count_lock_ );

    /* Increment count of waiters */
    cv->waiters_count_++;

    /* Store current generation in our activation record. */
    my_generation = cv->wait_generation_count_;

    __kmp_win32_mutex_unlock( &cv->waiters_count_lock_ );
    __kmp_win32_mutex_unlock( mx );


    for (;;) {
        int wait_done;

        /* Wait until the event is signaled */
        WaitForSingleObject( cv->event_, INFINITE );

        __kmp_win32_mutex_lock( &cv->waiters_count_lock_ );

        /* Exit the loop when the <cv->event_> is signaled and
         * there are still waiting threads from this <wait_generation>
         * that haven't been released from this wait yet.              */
        wait_done = ( cv->release_count_ > 0 ) &&
                    ( cv->wait_generation_count_ != my_generation );

        __kmp_win32_mutex_unlock( &cv->waiters_count_lock_);

        /* there used to be a semicolon after the if statement,
         * it looked like a bug, so i removed it */
        if( wait_done )
            break;
    }

    __kmp_win32_mutex_lock( mx );
    __kmp_win32_mutex_lock( &cv->waiters_count_lock_ );

    cv->waiters_count_--;
    cv->release_count_--;

    last_waiter =  ( cv->release_count_ == 0 );

    __kmp_win32_mutex_unlock( &cv->waiters_count_lock_ );

    if( last_waiter ) {
        /* We're the last waiter to be notified, so reset the manual event. */
        ResetEvent( cv->event_ );
    }
}

void
__kmp_win32_cond_broadcast( kmp_win32_cond_t *cv )
{
    __kmp_win32_mutex_lock( &cv->waiters_count_lock_ );

    if( cv->waiters_count_ > 0 ) {
        SetEvent( cv->event_ );
        /* Release all the threads in this generation. */

        cv->release_count_ = cv->waiters_count_;

        /* Start a new generation. */
        cv->wait_generation_count_++;
    }

    __kmp_win32_mutex_unlock( &cv->waiters_count_lock_ );
}

void
__kmp_win32_cond_signal( kmp_win32_cond_t *cv )
{
    __kmp_win32_cond_broadcast( cv );
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

void
__kmp_enable( int new_state )
{
    if (__kmp_init_runtime)
        LeaveCriticalSection( & __kmp_win32_section );
}

void
__kmp_disable( int *old_state )
{
    *old_state = 0;

    if (__kmp_init_runtime)
        EnterCriticalSection( & __kmp_win32_section );
}

void
__kmp_suspend_initialize( void )
{
    /* do nothing */
}

static void
__kmp_suspend_initialize_thread( kmp_info_t *th )
{
    if ( ! TCR_4( th->th.th_suspend_init ) ) {
      /* this means we haven't initialized the suspension pthread objects for this thread
         in this instance of the process */
        __kmp_win32_cond_init(  &th->th.th_suspend_cv );
        __kmp_win32_mutex_init( &th->th.th_suspend_mx );
        TCW_4( th->th.th_suspend_init, TRUE );
    }
}

void
__kmp_suspend_uninitialize_thread( kmp_info_t *th )
{
    if ( TCR_4( th->th.th_suspend_init ) ) {
      /* this means we have initialize the suspension pthread objects for this thread
         in this instance of the process */
      __kmp_win32_cond_destroy( & th->th.th_suspend_cv );
      __kmp_win32_mutex_destroy( & th->th.th_suspend_mx );
      TCW_4( th->th.th_suspend_init, FALSE );
    }
}

/*
 * This routine puts the calling thread to sleep after setting the
 * sleep bit for the indicated spin variable to true.
 */

void
__kmp_suspend( int th_gtid, volatile kmp_uint *spinner, kmp_uint checker )
{
    kmp_info_t *th = __kmp_threads[th_gtid];
    int status;
    kmp_uint old_spin;

    KF_TRACE( 30, ("__kmp_suspend: T#%d enter for spin = %p\n", th_gtid, spinner ) );

    __kmp_suspend_initialize_thread( th );

    __kmp_win32_mutex_lock( &th->th.th_suspend_mx );

    KF_TRACE( 10, ( "__kmp_suspend: T#%d setting sleep bit for spin(%p)\n",
                    th_gtid, spinner ) );

    /* TODO: shouldn't this use release semantics to ensure that __kmp_suspend_initialize_thread
       gets called first?
    */
    old_spin = __kmp_test_then_or32( (volatile kmp_int32 *) spinner,
                                     KMP_BARRIER_SLEEP_STATE );

    KF_TRACE( 5, ( "__kmp_suspend: T#%d set sleep bit for spin(%p)==%d\n",
                                   th_gtid, spinner, *spinner ) );

    if ( old_spin == checker ) {
        __kmp_test_then_and32( (volatile kmp_int32 *) spinner, ~(KMP_BARRIER_SLEEP_STATE) );

        KF_TRACE( 5, ( "__kmp_suspend: T#%d false alarm, reset sleep bit for spin(%p)\n",
                       th_gtid, spinner) );
    } else {
#ifdef DEBUG_SUSPEND
        __kmp_suspend_count++;
#endif

        /* Encapsulate in a loop as the documentation states that this may
         * "with low probability" return when the condition variable has
         * not been signaled or broadcast
         */
        int deactivated = FALSE;
        TCW_PTR(th->th.th_sleep_loc, spinner);
        while ( TCR_4( *spinner ) & KMP_BARRIER_SLEEP_STATE ) {

            KF_TRACE( 15, ("__kmp_suspend: T#%d about to perform kmp_win32_cond_wait()\n",
                     th_gtid ) );


            //
            // Mark the thread as no longer active
            // (only in the first iteration of the loop).
            //
            if ( ! deactivated ) {
                th->th.th_active = FALSE;
                if ( th->th.th_active_in_pool ) {
                    th->th.th_active_in_pool = FALSE;
                    KMP_TEST_THEN_DEC32(
                      (kmp_int32 *) &__kmp_thread_pool_active_nth );
                    KMP_DEBUG_ASSERT( TCR_4(__kmp_thread_pool_active_nth) >= 0 );
                }
                deactivated = TRUE;


                __kmp_win32_cond_wait( &th->th.th_suspend_cv, &th->th.th_suspend_mx, 0, 0 );
            }
            else {
                __kmp_win32_cond_wait( &th->th.th_suspend_cv, &th->th.th_suspend_mx, 0, 0 );
            }

#ifdef KMP_DEBUG
            if( (*spinner) & KMP_BARRIER_SLEEP_STATE ) {
                KF_TRACE( 100, ("__kmp_suspend: T#%d spurious wakeup\n", th_gtid ));
            }
#endif /* KMP_DEBUG */

	} // while

        //
        // Mark the thread as active again
        // (if it was previous marked as inactive)
        //
        if ( deactivated ) {
            th->th.th_active = TRUE;
            if ( TCR_4(th->th.th_in_pool) ) {
                KMP_TEST_THEN_INC32(
                  (kmp_int32 *) &__kmp_thread_pool_active_nth );
                th->th.th_active_in_pool = TRUE;
            }
        }
    }


    __kmp_win32_mutex_unlock( &th->th.th_suspend_mx );

    KF_TRACE( 30, ("__kmp_suspend: T#%d exit\n", th_gtid ) );
}

/* This routine signals the thread specified by target_gtid to wake up
 * after setting the sleep bit indicated by the spin argument to FALSE
 */
void
__kmp_resume( int target_gtid, volatile kmp_uint *spin )
{
    kmp_info_t *th = __kmp_threads[target_gtid];
    int status;
    kmp_uint32 old_spin;

#ifdef KMP_DEBUG
    int gtid = TCR_4(__kmp_init_gtid) ? __kmp_get_gtid() : -1;
#endif

    KF_TRACE( 30, ( "__kmp_resume: T#%d wants to wakeup T#%d enter\n",
                     gtid, target_gtid ) );

    __kmp_suspend_initialize_thread( th );

    __kmp_win32_mutex_lock( &th->th.th_suspend_mx );

    if ( spin == NULL ) {
        spin = (volatile kmp_uint *)TCR_PTR(th->th.th_sleep_loc);
        if ( spin == NULL ) {
            KF_TRACE( 5, ( "__kmp_resume: T#%d exiting, thread T#%d already awake -  spin(%p)\n",
                       gtid, target_gtid, spin ) );

            __kmp_win32_mutex_unlock( &th->th.th_suspend_mx );
            return;
        }
    }

    TCW_PTR(th->th.th_sleep_loc, NULL);
    old_spin = __kmp_test_then_and32( (kmp_int32 volatile *) spin, ~( KMP_BARRIER_SLEEP_STATE ) );

    if ( ( old_spin & KMP_BARRIER_SLEEP_STATE ) == 0 ) {
        KF_TRACE( 5, ( "__kmp_resume: T#%d exiting, thread T#%d already awake - spin(%p): "
                   "%u => %u\n",
                   gtid, target_gtid, spin, old_spin, *spin ) );

        __kmp_win32_mutex_unlock( &th->th.th_suspend_mx );
        return;
    }
    TCW_PTR(th->th.th_sleep_loc, NULL);

    KF_TRACE( 5, ( "__kmp_resume: T#%d about to wakeup T#%d, reset sleep bit for spin(%p)\n",
                    gtid, target_gtid, spin) );


    __kmp_win32_cond_signal(  &th->th.th_suspend_cv );

    __kmp_win32_mutex_unlock( &th->th.th_suspend_mx );

    KF_TRACE( 30, ( "__kmp_resume: T#%d exiting after signaling wake up for T#%d\n",
                    gtid, target_gtid ) );
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

void
__kmp_yield( int cond )
{
    if (cond)
        Sleep(0);
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

void
__kmp_gtid_set_specific( int gtid )
{
    KA_TRACE( 50, ("__kmp_gtid_set_specific: T#%d key:%d\n",
                gtid, __kmp_gtid_threadprivate_key ));
    KMP_ASSERT( __kmp_init_runtime );
    if( ! TlsSetValue( __kmp_gtid_threadprivate_key, (LPVOID)(gtid+1)) )
        KMP_FATAL( TLSSetValueFailed );
}

int
__kmp_gtid_get_specific()
{
    int gtid;
    if( !__kmp_init_runtime ) {
        KA_TRACE( 50, ("__kmp_get_specific: runtime shutdown, returning KMP_GTID_SHUTDOWN\n" ) );
        return KMP_GTID_SHUTDOWN;
    }
    gtid = (int)(kmp_intptr_t)TlsGetValue( __kmp_gtid_threadprivate_key );
    if ( gtid == 0 ) {
        gtid = KMP_GTID_DNE;
    }
    else {
        gtid--;
    }
    KA_TRACE( 50, ("__kmp_gtid_get_specific: key:%d gtid:%d\n",
                __kmp_gtid_threadprivate_key, gtid ));
    return gtid;
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

#if KMP_ARCH_X86_64

//
// Only 1 DWORD in the mask should have any procs set.
// Return the appropriate index, or -1 for an invalid mask.
//
int
__kmp_get_proc_group( kmp_affin_mask_t const *mask )
{
    int i;
    int group = -1;
    struct GROUP_AFFINITY new_ga, prev_ga;
    for (i = 0; i < __kmp_num_proc_groups; i++) {
        if (mask[i] == 0) {
            continue;
        }
        if (group >= 0) {
            return -1;
        }
        group = i;
    }
    return group;
}

#endif /* KMP_ARCH_X86_64 */

int
__kmp_set_system_affinity( kmp_affin_mask_t const *mask, int abort_on_error )
{

#if KMP_ARCH_X86_64

    if (__kmp_num_proc_groups > 1) {
        //
        // Check for a valid mask.
        //
        struct GROUP_AFFINITY ga;
        int group = __kmp_get_proc_group( mask );
        if (group < 0) {
            if (abort_on_error) {
                KMP_FATAL(AffinityInvalidMask, "kmp_set_affinity");
            }
            return -1;
        }

        //
        // Transform the bit vector into a GROUP_AFFINITY struct
        // and make the system call to set affinity.
        //
        ga.group = group;
        ga.mask = mask[group];
        ga.reserved[0] = ga.reserved[1] = ga.reserved[2] = 0;

        KMP_DEBUG_ASSERT(__kmp_SetThreadGroupAffinity != NULL);
        if (__kmp_SetThreadGroupAffinity(GetCurrentThread(), &ga, NULL) == 0) {
            DWORD error = GetLastError();
            if (abort_on_error) {
                __kmp_msg(
                    kmp_ms_fatal,
                    KMP_MSG( CantSetThreadAffMask ),
                    KMP_ERR( error ),
                    __kmp_msg_null
                );
            }
            return error;
        }
    }
    else

#endif /* KMP_ARCH_X86_64 */

    {
        if (!SetThreadAffinityMask( GetCurrentThread(), *mask )) {
            DWORD error = GetLastError();
            if (abort_on_error) {
                __kmp_msg(
                    kmp_ms_fatal,
                    KMP_MSG( CantSetThreadAffMask ),
                    KMP_ERR( error ),
                    __kmp_msg_null
                );
            }
            return error;
        }
    }
    return 0;
}

int
__kmp_get_system_affinity( kmp_affin_mask_t *mask, int abort_on_error )
{

#if KMP_ARCH_X86_64

    if (__kmp_num_proc_groups > 1) {
        KMP_CPU_ZERO(mask);
        struct GROUP_AFFINITY ga;
        KMP_DEBUG_ASSERT(__kmp_GetThreadGroupAffinity != NULL);

        if (__kmp_GetThreadGroupAffinity(GetCurrentThread(), &ga) == 0) {
            DWORD error = GetLastError();
            if (abort_on_error) {
                __kmp_msg(
                    kmp_ms_fatal,
                    KMP_MSG(FunctionError, "GetThreadGroupAffinity()"),
                    KMP_ERR(error),
                    __kmp_msg_null
                );
            }
            return error;
        }

        if ((ga.group < 0) || (ga.group > __kmp_num_proc_groups)
          || (ga.mask == 0)) {
            return -1;
        }

        mask[ga.group] = ga.mask;
    }
    else

#endif /* KMP_ARCH_X86_64 */

    {
        kmp_affin_mask_t newMask, sysMask, retval;

        if (!GetProcessAffinityMask(GetCurrentProcess(), &newMask, &sysMask)) {
            DWORD error = GetLastError();
            if (abort_on_error) {
                __kmp_msg(
                    kmp_ms_fatal,
                    KMP_MSG(FunctionError, "GetProcessAffinityMask()"),
                    KMP_ERR(error),
                    __kmp_msg_null
                );
            }
            return error;
        }
        retval = SetThreadAffinityMask(GetCurrentThread(), newMask);
        if (! retval) {
            DWORD error = GetLastError();
            if (abort_on_error) {
                __kmp_msg(
                    kmp_ms_fatal,
                    KMP_MSG(FunctionError, "SetThreadAffinityMask()"),
                    KMP_ERR(error),
                    __kmp_msg_null
                );
            }
            return error;
        }
        newMask = SetThreadAffinityMask(GetCurrentThread(), retval);
        if (! newMask) {
            DWORD error = GetLastError();
            if (abort_on_error) {
                __kmp_msg(
                    kmp_ms_fatal,
                    KMP_MSG(FunctionError, "SetThreadAffinityMask()"),
                    KMP_ERR(error),
                    __kmp_msg_null
                );
            }
        }
        *mask = retval;
    }
    return 0;
}

void
__kmp_affinity_bind_thread( int proc )
{

#if KMP_ARCH_X86_64

    if (__kmp_num_proc_groups > 1) {
        //
        // Form the GROUP_AFFINITY struct directly, rather than filling
        // out a bit vector and calling __kmp_set_system_affinity().
        //
        struct GROUP_AFFINITY ga;
        KMP_DEBUG_ASSERT((proc >= 0) && (proc < (__kmp_num_proc_groups
           * CHAR_BIT * sizeof(DWORD_PTR))));
        ga.group = proc / (CHAR_BIT * sizeof(DWORD_PTR));
        ga.mask = 1 << (proc % (CHAR_BIT * sizeof(DWORD_PTR)));
        ga.reserved[0] = ga.reserved[1] = ga.reserved[2] = 0;

        KMP_DEBUG_ASSERT(__kmp_SetThreadGroupAffinity != NULL);
        if (__kmp_SetThreadGroupAffinity(GetCurrentThread(), &ga, NULL) == 0) {
            DWORD error = GetLastError();
            if (__kmp_affinity_verbose) { // AC: continue silently if not verbose
                __kmp_msg(
                    kmp_ms_warning,
                    KMP_MSG( CantSetThreadAffMask ),
                    KMP_ERR( error ),
                    __kmp_msg_null
                );
            }
        }
    }
    else

#endif /* KMP_ARCH_X86_64 */

    {
        kmp_affin_mask_t mask;
        KMP_CPU_ZERO(&mask);
        KMP_CPU_SET(proc, &mask);
        __kmp_set_system_affinity(&mask, TRUE);
    }
}

void
__kmp_affinity_determine_capable( const char *env_var )
{
    //
    // All versions of Windows* OS (since Win '95) support SetThreadAffinityMask().
    //

#if KMP_ARCH_X86_64
    __kmp_affin_mask_size = __kmp_num_proc_groups * sizeof(kmp_affin_mask_t);
#else
    __kmp_affin_mask_size = sizeof(kmp_affin_mask_t);
#endif

    KA_TRACE( 10, (
        "__kmp_affinity_determine_capable: "
            "Windows* OS affinity interface functional (mask size = %" KMP_SIZE_T_SPEC ").\n",
        __kmp_affin_mask_size
    ) );
}

double
__kmp_read_cpu_time( void )
{
    FILETIME    CreationTime, ExitTime, KernelTime, UserTime;
    int         status;
    double      cpu_time;

    cpu_time = 0;

    status = GetProcessTimes( GetCurrentProcess(), &CreationTime,
                              &ExitTime, &KernelTime, &UserTime );

    if (status) {
        double  sec = 0;

        sec += KernelTime.dwHighDateTime;
        sec += UserTime.dwHighDateTime;

        /* Shift left by 32 bits */
        sec *= (double) (1 << 16) * (double) (1 << 16);

        sec += KernelTime.dwLowDateTime;
        sec += UserTime.dwLowDateTime;

        cpu_time += (sec * 100.0) / NSEC_PER_SEC;
    }

    return cpu_time;
}

int
__kmp_read_system_info( struct kmp_sys_info *info )
{
    info->maxrss  = 0;                   /* the maximum resident set size utilized (in kilobytes)     */
    info->minflt  = 0;                   /* the number of page faults serviced without any I/O        */
    info->majflt  = 0;                   /* the number of page faults serviced that required I/O      */
    info->nswap   = 0;                   /* the number of times a process was "swapped" out of memory */
    info->inblock = 0;                   /* the number of times the file system had to perform input  */
    info->oublock = 0;                   /* the number of times the file system had to perform output */
    info->nvcsw   = 0;                   /* the number of times a context switch was voluntarily      */
    info->nivcsw  = 0;                   /* the number of times a context switch was forced           */

    return 1;
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */


void
__kmp_runtime_initialize( void )
{
    SYSTEM_INFO info;
    kmp_str_buf_t path;
    UINT path_size;

    if ( __kmp_init_runtime ) {
        return;
    };

    InitializeCriticalSection( & __kmp_win32_section );
    __kmp_initialize_system_tick();

    #if (KMP_ARCH_X86 || KMP_ARCH_X86_64)
        if ( ! __kmp_cpuinfo.initialized ) {
            __kmp_query_cpuid( & __kmp_cpuinfo );
        }; // if
    #endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

    if ( __kmp_cpu_frequency == 0 ) {
        // __kmp_hardware_timestamp() calls to QueryPerformanceCounter(). If
        // __kmp_hardware_timestamp() rewritten to use RDTSC instruction (or its 64 analog),
        // probably we should try to get frequency from __kmp_cpuinfo.frequency first (see
        // z_Linux_util.c).
        LARGE_INTEGER freq;
        BOOL          rc;
        rc = QueryPerformanceFrequency( & freq );
        if ( rc ) {
            KMP_DEBUG_ASSERT( sizeof( __kmp_cpu_frequency ) >= sizeof( freq.QuadPart ) );
            KMP_DEBUG_ASSERT( freq.QuadPart >= 0 );
            __kmp_cpu_frequency = freq.QuadPart;
            KA_TRACE( 5, ( "cpu frequency: %" KMP_UINT64_SPEC "\n", __kmp_cpu_frequency ) );
        } else {
            __kmp_cpu_frequency = ~ 0;
        }; // if
    }; // if

    /* Set up minimum number of threads to switch to TLS gtid */
    #if KMP_OS_WINDOWS && ! defined GUIDEDLL_EXPORTS 
        // Windows* OS, static library.
        /*
            New thread may use stack space previously used by another thread, currently terminated.
            On Windows* OS, in case of static linking, we do not know the moment of thread termination,
            and our structures (__kmp_threads and __kmp_root arrays) are still keep info about dead
            threads. This leads to problem in __kmp_get_global_thread_id() function: it wrongly
            finds gtid (by searching through stack addresses of all known threads) for unregistered
            foreign tread.

            Setting __kmp_tls_gtid_min to 0 workarounds this problem: __kmp_get_global_thread_id()
            does not search through stacks, but get gtid from TLS immediatelly.

            --ln
        */
        __kmp_tls_gtid_min = 0;
    #else
        __kmp_tls_gtid_min = KMP_TLS_GTID_MIN;
    #endif

    /* for the static library */
    if ( !__kmp_gtid_threadprivate_key ) {
        __kmp_gtid_threadprivate_key = TlsAlloc();
        if( __kmp_gtid_threadprivate_key == TLS_OUT_OF_INDEXES ) {
            KMP_FATAL( TLSOutOfIndexes );
        }
    }


    //
    // Load ntdll.dll.
    //
    /*
        Simple
            GetModuleHandle( "ntdll.dl" )
        is not suitable due to security issue (see
        http://www.microsoft.com/technet/security/advisory/2269637.mspx). We have to specify full
        path to the library.
    */
    __kmp_str_buf_init( & path );
    path_size = GetSystemDirectory( path.str, path.size );
    KMP_DEBUG_ASSERT( path_size > 0 );
    if ( path_size >= path.size ) {
        //
        // Buffer is too short.  Expand the buffer and try again.
        //
        __kmp_str_buf_reserve( & path, path_size );
        path_size = GetSystemDirectory( path.str, path.size );
        KMP_DEBUG_ASSERT( path_size > 0 );
    }; // if
    if ( path_size > 0 && path_size < path.size ) {
        //
        // Now we have system directory name in the buffer.
        // Append backslash and name of dll to form full path,
        //
        path.used = path_size;
        __kmp_str_buf_print( & path, "\\%s", "ntdll.dll" );

        //
        // Now load ntdll using full path.
        //
        ntdll = GetModuleHandle( path.str );
    }

    KMP_DEBUG_ASSERT( ntdll != NULL );
    if ( ntdll != NULL ) {
        NtQuerySystemInformation = (NtQuerySystemInformation_t) GetProcAddress( ntdll, "NtQuerySystemInformation" );
    }
    KMP_DEBUG_ASSERT( NtQuerySystemInformation != NULL );

#if KMP_ARCH_X86_64
    //
    // Load kernel32.dll.
    // Same caveat - must use full system path name.
    //
    if ( path_size > 0 && path_size < path.size ) {
        //
        // Truncate the buffer back to just the system path length,
        // discarding "\\ntdll.dll", and replacing it with "kernel32.dll".
        //
        path.used = path_size;
        __kmp_str_buf_print( & path, "\\%s", "kernel32.dll" );

        //
        // Load kernel32.dll using full path.
        //
        kernel32 = GetModuleHandle( path.str );

        //
        // Load the function pointers to kernel32.dll routines
        // that may or may not exist on this system.
        //
        if ( kernel32 != NULL ) {
            __kmp_GetActiveProcessorCount = (kmp_GetActiveProcessorCount_t) GetProcAddress( kernel32, "GetActiveProcessorCount" );
            __kmp_GetActiveProcessorGroupCount = (kmp_GetActiveProcessorGroupCount_t) GetProcAddress( kernel32, "GetActiveProcessorGroupCount" );
            __kmp_GetThreadGroupAffinity = (kmp_GetThreadGroupAffinity_t) GetProcAddress( kernel32, "GetThreadGroupAffinity" );
            __kmp_SetThreadGroupAffinity = (kmp_SetThreadGroupAffinity_t) GetProcAddress( kernel32, "SetThreadGroupAffinity" );

            //
            // See if group affinity is supported on this system.
            // If so, calculate the #groups and #procs.
            //
            // Group affinity was introduced with Windows* 7 OS and 
            // Windows* Server 2008 R2 OS.
            //
            if ( ( __kmp_GetActiveProcessorCount != NULL )
              && ( __kmp_GetActiveProcessorGroupCount != NULL )
              && ( __kmp_GetThreadGroupAffinity != NULL )
              && ( __kmp_SetThreadGroupAffinity != NULL )
              && ( ( __kmp_num_proc_groups
              = __kmp_GetActiveProcessorGroupCount() ) > 1 ) ) {
                //
                // Calculate the total number of active OS procs.
                //
                int i;

                KA_TRACE( 10, ("__kmp_runtime_initialize: %d processor groups detected\n", __kmp_num_proc_groups ) );

                __kmp_xproc = 0;

                for ( i = 0; i < __kmp_num_proc_groups; i++ ) {
                    DWORD size = __kmp_GetActiveProcessorCount( i );
                    __kmp_xproc += size;
                    KA_TRACE( 20, ("__kmp_runtime_initialize: proc group %d size = %d\n", i, size ) );
                }
            }
        }
    }
    if ( __kmp_num_proc_groups <= 1 ) {
        GetSystemInfo( & info );
        __kmp_xproc = info.dwNumberOfProcessors;
    }
#else
    GetSystemInfo( & info );
    __kmp_xproc = info.dwNumberOfProcessors;
#endif // KMP_ARCH_X86_64

    //
    // If the OS said there were 0 procs, take a guess and use a value of 2.
    // This is done for Linux* OS, also.  Do we need error / warning?
    //
    if ( __kmp_xproc <= 0 ) {
        __kmp_xproc = 2;
    }

    KA_TRACE( 5, ("__kmp_runtime_initialize: total processors = %d\n", __kmp_xproc) );

    __kmp_str_buf_free( & path );


    __kmp_init_runtime = TRUE;
} // __kmp_runtime_initialize

void
__kmp_runtime_destroy( void )
{
    if ( ! __kmp_init_runtime ) {
        return;
    }


    /* we can't DeleteCriticalsection( & __kmp_win32_section ); */
    /* due to the KX_TRACE() commands */
    KA_TRACE( 40, ("__kmp_runtime_destroy\n" ));

    if( __kmp_gtid_threadprivate_key ) {
        TlsFree( __kmp_gtid_threadprivate_key );
        __kmp_gtid_threadprivate_key = 0;
    }

    __kmp_affinity_uninitialize();
    DeleteCriticalSection( & __kmp_win32_section );

    ntdll = NULL;
    NtQuerySystemInformation = NULL;

#if KMP_ARCH_X86_64
    kernel32 = NULL;
    __kmp_GetActiveProcessorCount = NULL;
    __kmp_GetActiveProcessorGroupCount = NULL;
    __kmp_GetThreadGroupAffinity = NULL;
    __kmp_SetThreadGroupAffinity = NULL;
#endif // KMP_ARCH_X86_64

    __kmp_init_runtime = FALSE;
}


void
__kmp_terminate_thread( int gtid )
{
    kmp_info_t  *th = __kmp_threads[ gtid ];

    if( !th ) return;

    KA_TRACE( 10, ("__kmp_terminate_thread: kill (%d)\n", gtid ) );

    if (TerminateThread( th->th.th_info.ds.ds_thread, (DWORD) -1) == FALSE) {
        /* It's OK, the thread may have exited already */
    }
    __kmp_free_handle( th->th.th_info.ds.ds_thread );
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

void
__kmp_clear_system_time( void )
{
    BOOL status;
    LARGE_INTEGER time;
    status = QueryPerformanceCounter( & time );
    __kmp_win32_time = (kmp_int64) time.QuadPart;
}

void
__kmp_initialize_system_tick( void )
{
    {
  BOOL status;
  LARGE_INTEGER freq;

  status = QueryPerformanceFrequency( & freq );
  if (! status) {
        DWORD error = GetLastError();
        __kmp_msg(
            kmp_ms_fatal,
            KMP_MSG( FunctionError, "QueryPerformanceFrequency()" ),
            KMP_ERR( error ),
            __kmp_msg_null
        );

  }
  else {
      __kmp_win32_tick = ((double) 1.0) / (double) freq.QuadPart;
  }
    }
}

/* Calculate the elapsed wall clock time for the user */

void
__kmp_elapsed( double *t )
{
    BOOL status;
    LARGE_INTEGER now;
    status = QueryPerformanceCounter( & now );
    *t = ((double) now.QuadPart) * __kmp_win32_tick;
}

/* Calculate the elapsed wall clock tick for the user */

void
__kmp_elapsed_tick( double *t )
{
    *t = __kmp_win32_tick;
}

void
__kmp_read_system_time( double *delta )
{

    if (delta != NULL) {
        BOOL status;
        LARGE_INTEGER now;

        status = QueryPerformanceCounter( & now );

        *delta = ((double) (((kmp_int64) now.QuadPart) - __kmp_win32_time))
    * __kmp_win32_tick;
    }
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

/*
 * Change thread to the affinity mask pointed to by affin_mask argument
 * and return a pointer to the old value in the old_mask argument, if argument
 * is non-NULL.
 */

void
__kmp_change_thread_affinity_mask( int gtid, kmp_affin_mask_t *new_mask,
                                   kmp_affin_mask_t *old_mask )
{
    kmp_info_t  *th = __kmp_threads[ gtid ];

    KMP_DEBUG_ASSERT( *new_mask != 0 );

    if ( old_mask != NULL ) {
        *old_mask = SetThreadAffinityMask( th -> th.th_info.ds.ds_thread, *new_mask );

        if (! *old_mask ) {
            DWORD error = GetLastError();
            __kmp_msg(
                kmp_ms_fatal,
                KMP_MSG( CantSetThreadAffMask ),
                KMP_ERR( error ),
                __kmp_msg_null
            );
        }
    }
    if (__kmp_affinity_verbose)
            KMP_INFORM( ChangeAffMask, "KMP_AFFINITY (Bind)", gtid, *old_mask, *new_mask );

    /* Make sure old value is correct in thread data structures */
    KMP_DEBUG_ASSERT( old_mask != NULL && *old_mask == *(th -> th.th_affin_mask ));

    KMP_CPU_COPY(th -> th.th_affin_mask, new_mask);
}


/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

void * __stdcall
__kmp_launch_worker( void *arg )
{
    volatile void *stack_data;
    void *exit_val;
    void *padding = 0;
    kmp_info_t *this_thr = (kmp_info_t *) arg;
    int gtid;

    gtid = this_thr->th.th_info.ds.ds_gtid;
    __kmp_gtid_set_specific( gtid );
#ifdef KMP_TDATA_GTID
    #error "This define causes problems with LoadLibrary() + declspec(thread) " \
        "on Windows* OS.  See CQ50564, tests kmp_load_library*.c and this MSDN " \
        "reference: http://support.microsoft.com/kb/118816"
    //__kmp_gtid = gtid;
#endif


    __kmp_affinity_set_init_mask( gtid, FALSE );

#if KMP_ARCH_X86 || KMP_ARCH_X86_64
    //
    // Set the FP control regs to be a copy of
    // the parallel initialization thread's.
    //
    __kmp_clear_x87_fpu_status_word();
    __kmp_load_x87_fpu_control_word( &__kmp_init_x87_fpu_control_word );
    __kmp_load_mxcsr( &__kmp_init_mxcsr );
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

    if ( __kmp_stkoffset > 0 && gtid > 0 ) {
        padding = _alloca( gtid * __kmp_stkoffset );
    }

    this_thr -> th.th_info.ds.ds_thread_id = GetCurrentThreadId();
    TCW_4( this_thr -> th.th_info.ds.ds_alive, TRUE );

    if ( TCR_4(__kmp_gtid_mode) < 2 ) { // check stack only if it is used to get gtid
        TCW_PTR(this_thr->th.th_info.ds.ds_stackbase, &stack_data);
        KMP_ASSERT( this_thr -> th.th_info.ds.ds_stackgrow == FALSE );
        __kmp_check_stack_overlap( this_thr );
    }
    KMP_MB();
    exit_val = __kmp_launch_thread( this_thr );
    TCW_4( this_thr -> th.th_info.ds.ds_alive, FALSE );
    KMP_MB();
    return exit_val;
}


/* The monitor thread controls all of the threads in the complex */

void * __stdcall
__kmp_launch_monitor( void *arg )
{
    DWORD        wait_status;
    kmp_thread_t monitor;
    int          status;
    int          interval;
    kmp_info_t *this_thr = (kmp_info_t *) arg;

    KMP_DEBUG_ASSERT(__kmp_init_monitor);
    TCW_4( __kmp_init_monitor, 2 );    // AC: Signal the library that monitor has started
                                       // TODO: hide "2" in enum (like {true,false,started})
    this_thr -> th.th_info.ds.ds_thread_id = GetCurrentThreadId();
    TCW_4( this_thr -> th.th_info.ds.ds_alive, TRUE );

    KMP_MB();       /* Flush all pending memory write invalidates.  */
    KA_TRACE( 10, ("__kmp_launch_monitor: launched\n" ) );

    monitor = GetCurrentThread();

    /* set thread priority */
    status = SetThreadPriority( monitor, THREAD_PRIORITY_HIGHEST );
    if (! status) {
        DWORD error = GetLastError();
        __kmp_msg(
            kmp_ms_fatal,
            KMP_MSG( CantSetThreadPriority ),
            KMP_ERR( error ),
            __kmp_msg_null
        );
    }

    /* register us as monitor */
    __kmp_gtid_set_specific( KMP_GTID_MONITOR );
#ifdef KMP_TDATA_GTID
    #error "This define causes problems with LoadLibrary() + declspec(thread) " \
        "on Windows* OS.  See CQ50564, tests kmp_load_library*.c and this MSDN " \
        "reference: http://support.microsoft.com/kb/118816"
    //__kmp_gtid = KMP_GTID_MONITOR;
#endif


    KMP_MB();       /* Flush all pending memory write invalidates.  */

    interval = ( 1000 / __kmp_monitor_wakeups ); /* in milliseconds */

    while (! TCR_4(__kmp_global.g.g_done)) {
        /*  This thread monitors the state of the system */

        KA_TRACE( 15, ( "__kmp_launch_monitor: update\n" ) );

        wait_status = WaitForSingleObject( __kmp_monitor_ev, interval );

        if (wait_status == WAIT_TIMEOUT) {
            TCW_4( __kmp_global.g.g_time.dt.t_value,
              TCR_4( __kmp_global.g.g_time.dt.t_value ) + 1 );
        }

        KMP_MB();       /* Flush all pending memory write invalidates.  */
    }

    KA_TRACE( 10, ("__kmp_launch_monitor: finished\n" ) );

    status = SetThreadPriority( monitor, THREAD_PRIORITY_NORMAL );
    if (! status) {
        DWORD error = GetLastError();
        __kmp_msg(
            kmp_ms_fatal,
            KMP_MSG( CantSetThreadPriority ),
            KMP_ERR( error ),
            __kmp_msg_null
        );
    }

    if (__kmp_global.g.g_abort != 0) {
        /* now we need to terminate the worker threads   */
        /* the value of t_abort is the signal we caught */

        int gtid;

        KA_TRACE( 10, ("__kmp_launch_monitor: terminate sig=%d\n", (__kmp_global.g.g_abort) ) );

        /* terminate the OpenMP worker threads */
        /* TODO this is not valid for sibling threads!!
         * the uber master might not be 0 anymore.. */
        for (gtid = 1; gtid < __kmp_threads_capacity; ++gtid)
            __kmp_terminate_thread( gtid );

        __kmp_cleanup();

        Sleep( 0 );

        KA_TRACE( 10, ("__kmp_launch_monitor: raise sig=%d\n", (__kmp_global.g.g_abort) ) );

        if (__kmp_global.g.g_abort > 0) {
            raise( __kmp_global.g.g_abort );
        }
    }

    TCW_4( this_thr -> th.th_info.ds.ds_alive, FALSE );

    KMP_MB();
    return arg;
}

void
__kmp_create_worker( int gtid, kmp_info_t *th, size_t stack_size )
{
    kmp_thread_t   handle;
    DWORD          idThread;
    
    KA_TRACE( 10, ("__kmp_create_worker: try to create thread (%d)\n", gtid ) );
    
    th->th.th_info.ds.ds_gtid = gtid;
    
    if ( KMP_UBER_GTID(gtid) ) {
        int     stack_data;

        /* TODO: GetCurrentThread() returns a pseudo-handle that is unsuitable for other threads to use.
           Is it appropriate to just use GetCurrentThread?  When should we close this handle?  When
           unregistering the root?
        */
        {
            BOOL rc;
            rc = DuplicateHandle(
                                 GetCurrentProcess(),
                                 GetCurrentThread(),
                                 GetCurrentProcess(),
                                 &th->th.th_info.ds.ds_thread,
                                 0,
                                 FALSE,
                                 DUPLICATE_SAME_ACCESS
                                 );
            KMP_ASSERT( rc );
            KA_TRACE( 10, (" __kmp_create_worker: ROOT Handle duplicated, th = %p, handle = %" KMP_UINTPTR_SPEC "\n",
                           (LPVOID)th,
                           th->th.th_info.ds.ds_thread ) );
            th->th.th_info.ds.ds_thread_id = GetCurrentThreadId();
        }
        if ( TCR_4(__kmp_gtid_mode) < 2 ) { // check stack only if it is used to get gtid
            /* we will dynamically update the stack range if gtid_mode == 1 */
            TCW_PTR(th->th.th_info.ds.ds_stackbase, &stack_data);
            TCW_PTR(th->th.th_info.ds.ds_stacksize, 0);
            TCW_4(th->th.th_info.ds.ds_stackgrow, TRUE);
            __kmp_check_stack_overlap( th );
        }
    }
    else {
        KMP_MB();       /* Flush all pending memory write invalidates.  */

        /* Set stack size for this thread now. */
        KA_TRACE( 10, ( "__kmp_create_worker: stack_size = %" KMP_SIZE_T_SPEC
                        " bytes\n", stack_size ) );
        
        stack_size += gtid * __kmp_stkoffset;
        
        TCW_PTR(th->th.th_info.ds.ds_stacksize, stack_size);
        TCW_4(th->th.th_info.ds.ds_stackgrow, FALSE);
        
        KA_TRACE( 10, ( "__kmp_create_worker: (before) stack_size = %"
                        KMP_SIZE_T_SPEC
                        " bytes, &__kmp_launch_worker = %p, th = %p, "
                        "&idThread = %p\n",
                        (SIZE_T) stack_size,
                        (LPTHREAD_START_ROUTINE) & __kmp_launch_worker,
                        (LPVOID) th, &idThread ) );
        
            {
                handle = CreateThread( NULL, (SIZE_T) stack_size,
                                       (LPTHREAD_START_ROUTINE) __kmp_launch_worker,
                                       (LPVOID) th, STACK_SIZE_PARAM_IS_A_RESERVATION, &idThread );
            }
        
        KA_TRACE( 10, ( "__kmp_create_worker: (after) stack_size = %"
                        KMP_SIZE_T_SPEC
                        " bytes, &__kmp_launch_worker = %p, th = %p, "
                        "idThread = %u, handle = %" KMP_UINTPTR_SPEC "\n",
                        (SIZE_T) stack_size,
                        (LPTHREAD_START_ROUTINE) & __kmp_launch_worker,
                        (LPVOID) th, idThread, handle ) );
        
            {
                if ( handle == 0 ) {
                    DWORD error = GetLastError();
                    __kmp_msg(
                              kmp_ms_fatal,
                              KMP_MSG( CantCreateThread ),
                              KMP_ERR( error ),
                              __kmp_msg_null
                              );
                } else {
                    th->th.th_info.ds.ds_thread = handle;
                }
            }
        KMP_MB();       /* Flush all pending memory write invalidates.  */
    }
    
    KA_TRACE( 10, ("__kmp_create_worker: done creating thread (%d)\n", gtid ) );
}

int
__kmp_still_running(kmp_info_t *th) {
    return (WAIT_TIMEOUT == WaitForSingleObject( th->th.th_info.ds.ds_thread, 0));
}

void
__kmp_create_monitor( kmp_info_t *th )
{
    kmp_thread_t        handle;
    DWORD               idThread;
    int                 ideal, new_ideal;
    int     caller_gtid = __kmp_get_gtid();

    KA_TRACE( 10, ("__kmp_create_monitor: try to create monitor\n" ) );

    KMP_MB();       /* Flush all pending memory write invalidates.  */

    __kmp_monitor_ev = CreateEvent( NULL, TRUE, FALSE, NULL );
    if ( __kmp_monitor_ev == NULL ) {
        DWORD error = GetLastError();
        __kmp_msg(
            kmp_ms_fatal,
            KMP_MSG( CantCreateEvent ),
            KMP_ERR( error ),
            __kmp_msg_null
        );
    }; // if

    th->th.th_info.ds.ds_tid  = KMP_GTID_MONITOR;
    th->th.th_info.ds.ds_gtid = KMP_GTID_MONITOR;

    // FIXME - on Windows* OS, if __kmp_monitor_stksize = 0, figure out how
    // to automatically expand stacksize based on CreateThread error code.
    if ( __kmp_monitor_stksize == 0 ) {
        __kmp_monitor_stksize = KMP_DEFAULT_MONITOR_STKSIZE;
    }
    if ( __kmp_monitor_stksize < __kmp_sys_min_stksize ) {
        __kmp_monitor_stksize = __kmp_sys_min_stksize;
    }

    KA_TRACE( 10, ("__kmp_create_monitor: requested stacksize = %d bytes\n",
                   (int) __kmp_monitor_stksize ) );

    TCW_4( __kmp_global.g.g_time.dt.t_value, 0 );

    handle = CreateThread( NULL, (SIZE_T) __kmp_monitor_stksize,
                           (LPTHREAD_START_ROUTINE) __kmp_launch_monitor,
                           (LPVOID) th, STACK_SIZE_PARAM_IS_A_RESERVATION, &idThread );
    if (handle == 0) {
        DWORD error = GetLastError();
        __kmp_msg(
            kmp_ms_fatal,
            KMP_MSG( CantCreateThread ),
            KMP_ERR( error ),
            __kmp_msg_null
        );
    }
    else
        th->th.th_info.ds.ds_thread = handle;

    KMP_MB();       /* Flush all pending memory write invalidates.  */

    KA_TRACE( 10, ("__kmp_create_monitor: monitor created %p\n",
                   (void *) th->th.th_info.ds.ds_thread ) );
}

/*
  Check to see if thread is still alive.

  NOTE:  The ExitProcess(code) system call causes all threads to Terminate
         with a exit_val = code.  Because of this we can not rely on
         exit_val having any particular value.  So this routine may
         return STILL_ALIVE in exit_val even after the thread is dead.
*/

int
__kmp_is_thread_alive( kmp_info_t * th, DWORD *exit_val )
{
    DWORD rc;
    rc = GetExitCodeThread( th->th.th_info.ds.ds_thread, exit_val );
    if ( rc == 0 ) {
        DWORD error = GetLastError();
        __kmp_msg(
            kmp_ms_fatal,
            KMP_MSG( FunctionError, "GetExitCodeThread()" ),
            KMP_ERR( error ),
            __kmp_msg_null
        );
    }; // if
    return ( *exit_val == STILL_ACTIVE );
}


void
__kmp_exit_thread(
    int exit_status
) {
    ExitThread( exit_status );
} // __kmp_exit_thread

/*
    This is a common part for both __kmp_reap_worker() and __kmp_reap_monitor().
*/
static void
__kmp_reap_common( kmp_info_t * th )
{
    DWORD exit_val;

    KMP_MB();       /* Flush all pending memory write invalidates.  */

    KA_TRACE( 10, ( "__kmp_reap_common: try to reap (%d)\n", th->th.th_info.ds.ds_gtid ) );

    /*
        2006-10-19:

        There are two opposite situations:

            1. Windows* OS keep thread alive after it resets ds_alive flag and exits from thread
               function. (For example, see C70770/Q394281 "unloading of dll based on OMP is very
               slow".)
            2. Windows* OS may kill thread before it resets ds_alive flag.

        Right solution seems to be waiting for *either* thread termination *or* ds_alive resetting.

    */

    {
        // TODO: This code is very similar to KMP_WAIT_YIELD. Need to generalize KMP_WAIT_YIELD to
        // cover this usage also.
        void * obj = NULL;
        register kmp_uint32 spins;
        KMP_INIT_YIELD( spins );
        do {
            __kmp_is_thread_alive( th, &exit_val );
            __kmp_static_delay( TRUE );
            KMP_YIELD( TCR_4(__kmp_nth) > __kmp_avail_proc );
            KMP_YIELD_SPIN( spins );
        } while ( exit_val == STILL_ACTIVE && TCR_4( th->th.th_info.ds.ds_alive ) );
    }

    __kmp_free_handle( th->th.th_info.ds.ds_thread );

    /*
     * NOTE:  The ExitProcess(code) system call causes all threads to Terminate
     *        with a exit_val = code.  Because of this we can not rely on
     *        exit_val having any particular value.
     */
    if ( exit_val == STILL_ACTIVE ) {
        KA_TRACE( 1, ( "__kmp_reap_common: thread still active.\n" ) );
    } else if ( (void *) exit_val != (void *) th) {
        KA_TRACE( 1, ( "__kmp_reap_common: ExitProcess / TerminateThread used?\n" ) );
    }; // if

    KA_TRACE( 10,
        (
            "__kmp_reap_common: done reaping (%d), handle = %" KMP_UINTPTR_SPEC "\n",
            th->th.th_info.ds.ds_gtid,
            th->th.th_info.ds.ds_thread
        )
    );

    th->th.th_info.ds.ds_thread    = 0;
    th->th.th_info.ds.ds_tid       = KMP_GTID_DNE;
    th->th.th_info.ds.ds_gtid      = KMP_GTID_DNE;
    th->th.th_info.ds.ds_thread_id = 0;

    KMP_MB();       /* Flush all pending memory write invalidates.  */
}

void
__kmp_reap_monitor( kmp_info_t *th )
{
    int status;

    KA_TRACE( 10, ("__kmp_reap_monitor: try to reap %p\n",
                   (void *) th->th.th_info.ds.ds_thread ) );

    // If monitor has been created, its tid and gtid should be KMP_GTID_MONITOR.
    // If both tid and gtid are 0, it means the monitor did not ever start.
    // If both tid and gtid are KMP_GTID_DNE, the monitor has been shut down.
    KMP_DEBUG_ASSERT( th->th.th_info.ds.ds_tid == th->th.th_info.ds.ds_gtid );
    if ( th->th.th_info.ds.ds_gtid != KMP_GTID_MONITOR ) {
        return;
    }; // if

    KMP_MB();       /* Flush all pending memory write invalidates.  */

    status = SetEvent( __kmp_monitor_ev );
    if ( status == FALSE ) {
        DWORD error = GetLastError();
        __kmp_msg(
            kmp_ms_fatal,
            KMP_MSG( CantSetEvent ),
            KMP_ERR( error ),
            __kmp_msg_null
        );
    }
    KA_TRACE( 10, ( "__kmp_reap_monitor: reaping thread (%d)\n", th->th.th_info.ds.ds_gtid ) );
    __kmp_reap_common( th );

    __kmp_free_handle( __kmp_monitor_ev );

    KMP_MB();       /* Flush all pending memory write invalidates.  */
}

void
__kmp_reap_worker( kmp_info_t * th )
{
    KA_TRACE( 10, ( "__kmp_reap_worker: reaping thread (%d)\n", th->th.th_info.ds.ds_gtid ) );
    __kmp_reap_common( th );
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

#if KMP_HANDLE_SIGNALS


static void
__kmp_team_handler( int signo )
{
    if ( __kmp_global.g.g_abort == 0 ) {
        // Stage 1 signal handler, let's shut down all of the threads.
        if ( __kmp_debug_buf ) {
            __kmp_dump_debug_buffer();
        }; // if
        KMP_MB();       // Flush all pending memory write invalidates.
        TCW_4( __kmp_global.g.g_abort, signo );
        KMP_MB();       // Flush all pending memory write invalidates.
        TCW_4( __kmp_global.g.g_done, TRUE );
        KMP_MB();       // Flush all pending memory write invalidates.
    }
} // __kmp_team_handler



static
sig_func_t __kmp_signal( int signum, sig_func_t handler ) {
    sig_func_t old = signal( signum, handler );
    if ( old == SIG_ERR ) {
        int error = errno;
        __kmp_msg( kmp_ms_fatal, KMP_MSG( FunctionError, "signal" ), KMP_ERR( error ), __kmp_msg_null );
    }; // if
    return old;
}

static void
__kmp_install_one_handler(
    int           sig,
    sig_func_t    handler,
    int           parallel_init
) {
    sig_func_t old;
    KMP_MB();       /* Flush all pending memory write invalidates.  */
    KB_TRACE( 60, ("__kmp_install_one_handler: called: sig=%d\n", sig ) );
    if ( parallel_init ) {
        old = __kmp_signal( sig, handler );
        // SIG_DFL on Windows* OS in NULL or 0.
        if ( old == __kmp_sighldrs[ sig ] ) {
            __kmp_siginstalled[ sig ] = 1;
        } else {
            // Restore/keep user's handler if one previously installed.
            old = __kmp_signal( sig, old );
        }; // if
    } else {
        // Save initial/system signal handlers to see if user handlers installed.
        // 2009-09-23: It is a dead code. On Windows* OS __kmp_install_signals called once with
        // parallel_init == TRUE.
        old = __kmp_signal( sig, SIG_DFL );
        __kmp_sighldrs[ sig ] = old;
        __kmp_signal( sig, old );
    }; // if
    KMP_MB();       /* Flush all pending memory write invalidates.  */
} // __kmp_install_one_handler

static void
__kmp_remove_one_handler( int sig ) {
    if ( __kmp_siginstalled[ sig ] ) {
        sig_func_t old;
        KMP_MB();       // Flush all pending memory write invalidates.
        KB_TRACE( 60, ( "__kmp_remove_one_handler: called: sig=%d\n", sig ) );
        old = __kmp_signal( sig, __kmp_sighldrs[ sig ] );
        if ( old != __kmp_team_handler ) {
            KB_TRACE( 10, ( "__kmp_remove_one_handler: oops, not our handler, restoring: sig=%d\n", sig ) );
            old = __kmp_signal( sig, old );
        }; // if
        __kmp_sighldrs[ sig ] = NULL;
        __kmp_siginstalled[ sig ] = 0;
        KMP_MB();       // Flush all pending memory write invalidates.
    }; // if
} // __kmp_remove_one_handler


void
__kmp_install_signals( int parallel_init )
{
    KB_TRACE( 10, ( "__kmp_install_signals: called\n" ) );
    if ( ! __kmp_handle_signals ) {
        KB_TRACE( 10, ( "__kmp_install_signals: KMP_HANDLE_SIGNALS is false - handlers not installed\n" ) );
        return;
    }; // if
    __kmp_install_one_handler( SIGINT,  __kmp_team_handler, parallel_init );
    __kmp_install_one_handler( SIGILL,  __kmp_team_handler, parallel_init );
    __kmp_install_one_handler( SIGABRT, __kmp_team_handler, parallel_init );
    __kmp_install_one_handler( SIGFPE,  __kmp_team_handler, parallel_init );
    __kmp_install_one_handler( SIGSEGV, __kmp_team_handler, parallel_init );
    __kmp_install_one_handler( SIGTERM, __kmp_team_handler, parallel_init );
} // __kmp_install_signals


void
__kmp_remove_signals( void )
{
    int sig;
    KB_TRACE( 10, ("__kmp_remove_signals: called\n" ) );
    for ( sig = 1; sig < NSIG; ++ sig ) {
        __kmp_remove_one_handler( sig );
    }; // for sig
} // __kmp_remove_signals


#endif // KMP_HANDLE_SIGNALS

/* Put the thread to sleep for a time period */
void
__kmp_thread_sleep( int millis )
{
    DWORD status;

    status = SleepEx( (DWORD) millis, FALSE );
    if ( status ) {
        DWORD error = GetLastError();
        __kmp_msg(
            kmp_ms_fatal,
            KMP_MSG( FunctionError, "SleepEx()" ),
            KMP_ERR( error ),
            __kmp_msg_null
        );
    }
}

/* Determine whether the given address is mapped into the current address space. */
int
__kmp_is_address_mapped( void * addr )
{
    DWORD status;
    MEMORY_BASIC_INFORMATION lpBuffer;
    SIZE_T dwLength;

    dwLength = sizeof(MEMORY_BASIC_INFORMATION);

    status = VirtualQuery( addr, &lpBuffer, dwLength );

    return !((( lpBuffer.State == MEM_RESERVE) || ( lpBuffer.State == MEM_FREE )) ||
       (( lpBuffer.Protect == PAGE_NOACCESS ) || ( lpBuffer.Protect == PAGE_EXECUTE )));
}

kmp_uint64
__kmp_hardware_timestamp(void)
{
    kmp_uint64 r = 0;

    QueryPerformanceCounter((LARGE_INTEGER*) &r);
    return r;
}

/* Free handle and check the error code */
void
__kmp_free_handle( kmp_thread_t tHandle )
{
/* called with parameter type HANDLE also, thus suppose kmp_thread_t defined as HANDLE */
    BOOL rc;
    rc = CloseHandle( tHandle );
    if ( !rc ) {
        DWORD error = GetLastError();
        __kmp_msg(
            kmp_ms_fatal,
            KMP_MSG( CantCloseHandle ),
            KMP_ERR( error ),
            __kmp_msg_null
        );
    }
}

int
__kmp_get_load_balance( int max ) {

    static ULONG glb_buff_size = 100 * 1024;

    static int     glb_running_threads  = 0;  /* Saved count of the running threads for the thread balance algortihm */
    static double  glb_call_time        = 0;  /* Thread balance algorithm call time */

    int running_threads = 0;              // Number of running threads in the system.
    NTSTATUS  status        = 0;
    ULONG     buff_size     = 0;
    ULONG     info_size     = 0;
    void *    buffer        = NULL;
    PSYSTEM_PROCESS_INFORMATION spi = NULL;
    int first_time          = 1;

    double call_time = 0.0; //start, finish;

    __kmp_elapsed( & call_time );

    if ( glb_call_time &&
            ( call_time - glb_call_time < __kmp_load_balance_interval ) ) {
        running_threads = glb_running_threads;
        goto finish;
    }
    glb_call_time = call_time;

    // Do not spend time on running algorithm if we have a permanent error.
    if ( NtQuerySystemInformation == NULL ) {
        running_threads = -1;
        goto finish;
    }; // if

    if ( max <= 0 ) {
        max = INT_MAX;
    }; // if

    do {

        if ( first_time ) {
            buff_size = glb_buff_size;
        } else {
            buff_size = 2 * buff_size;
        }

        buffer = KMP_INTERNAL_REALLOC( buffer, buff_size );
        if ( buffer == NULL ) {
            running_threads = -1;
            goto finish;
        }; // if
        status = NtQuerySystemInformation( SystemProcessInformation, buffer, buff_size, & info_size );
        first_time = 0;

    } while ( status == STATUS_INFO_LENGTH_MISMATCH );
    glb_buff_size = buff_size;

    #define CHECK( cond )                       \
        {                                       \
            KMP_DEBUG_ASSERT( cond );           \
            if ( ! ( cond ) ) {                 \
                running_threads = -1;           \
                goto finish;                    \
            }                                   \
        }

    CHECK( buff_size >= info_size );
    spi = PSYSTEM_PROCESS_INFORMATION( buffer );
    for ( ; ; ) {
        ptrdiff_t offset = uintptr_t( spi ) - uintptr_t( buffer );
        CHECK( 0 <= offset && offset + sizeof( SYSTEM_PROCESS_INFORMATION ) < info_size );
        HANDLE pid = spi->ProcessId;
        ULONG num = spi->NumberOfThreads;
        CHECK( num >= 1 );
        size_t spi_size = sizeof( SYSTEM_PROCESS_INFORMATION ) + sizeof( SYSTEM_THREAD ) * ( num - 1 );
        CHECK( offset + spi_size < info_size );          // Make sure process info record fits the buffer.
        if ( spi->NextEntryOffset != 0 ) {
            CHECK( spi_size <= spi->NextEntryOffset );   // And do not overlap with the next record.
        }; // if
        // pid == 0 corresponds to the System Idle Process. It always has running threads
        // on all cores. So, we don't consider the running threads of this process.
        if ( pid != 0 ) {
            for ( int i = 0; i < num; ++ i ) {
                THREAD_STATE state = spi->Threads[ i ].State;
                // Count threads that have Ready or Running state.
                // !!! TODO: Why comment does not match the code???
                if ( state == StateRunning ) {
                    ++ running_threads;
                    // Stop counting running threads if the number is already greater than
                    // the number of available cores
                    if ( running_threads >= max ) {
                        goto finish;
                    }
                } // if
            }; // for i
        } // if
        if ( spi->NextEntryOffset == 0 ) {
            break;
        }; // if
        spi = PSYSTEM_PROCESS_INFORMATION( uintptr_t( spi ) + spi->NextEntryOffset );
    }; // forever

    #undef CHECK

    finish: // Clean up and exit.

        if ( buffer != NULL ) {
            KMP_INTERNAL_FREE( buffer );
        }; // if

        glb_running_threads = running_threads;

        return running_threads;

} //__kmp_get_load_balance()

