/*
 * kmp_lock.cpp -- lock-related functions
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

#include <stddef.h>

#include "kmp.h"
#include "kmp_i18n.h"
#include "kmp_lock.h"
#include "kmp_io.h"

#if KMP_OS_LINUX && (KMP_ARCH_X86 || KMP_ARCH_X86_64)
# include <unistd.h>
# include <sys/syscall.h>
// We should really include <futex.h>, but that causes compatibility problems on different
// Linux* OS distributions that either require that you include (or break when you try to include) 
// <pci/types.h>.
// Since all we need is the two macros below (which are part of the kernel ABI, so can't change)
// we just define the constants here and don't include <futex.h>
# ifndef FUTEX_WAIT
#  define FUTEX_WAIT    0
# endif
# ifndef FUTEX_WAKE
#  define FUTEX_WAKE    1
# endif
#endif


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

/* Implement spin locks for internal library use.             */
/* The algorithm implemented is Lamport's bakery lock [1974]. */

void
__kmp_validate_locks( void )
{
    int i;
    kmp_uint32  x, y;

    /* Check to make sure unsigned arithmetic does wraps properly */
    x = ~((kmp_uint32) 0) - 2;
    y = x - 2;

    for (i = 0; i < 8; ++i, ++x, ++y) {
        kmp_uint32 z = (x - y);
        KMP_ASSERT( z == 2 );
    }

    KMP_ASSERT( offsetof( kmp_base_queuing_lock, tail_id ) % 8 == 0 );
}


/* ------------------------------------------------------------------------ */
/* test and set locks */

//
// For the non-nested locks, we can only assume that the first 4 bytes were
// allocated, since gcc only allocates 4 bytes for omp_lock_t, and the Intel
// compiler only allocates a 4 byte pointer on IA-32 architecture.  On
// Windows* OS on Intel(R) 64, we can assume that all 8 bytes were allocated.
//
// gcc reserves >= 8 bytes for nested locks, so we can assume that the
// entire 8 bytes were allocated for nested locks on all 64-bit platforms.
//

static kmp_int32
__kmp_get_tas_lock_owner( kmp_tas_lock_t *lck )
{
    return TCR_4( lck->lk.poll ) - 1;
}

static inline bool
__kmp_is_tas_lock_nestable( kmp_tas_lock_t *lck )
{
    return lck->lk.depth_locked != -1;
}

__forceinline static void
__kmp_acquire_tas_lock_timed_template( kmp_tas_lock_t *lck, kmp_int32 gtid )
{
    KMP_MB();

#ifdef USE_LOCK_PROFILE
    kmp_uint32 curr = TCR_4( lck->lk.poll );
    if ( ( curr != 0 ) && ( curr != gtid + 1 ) )
        __kmp_printf( "LOCK CONTENTION: %p\n", lck );
    /* else __kmp_printf( "." );*/
#endif /* USE_LOCK_PROFILE */

    if ( KMP_COMPARE_AND_STORE_ACQ32( & ( lck->lk.poll ), 0, gtid + 1 ) ) {
        return;
    }

    kmp_uint32 spins;
    KMP_INIT_YIELD( spins );
    if ( TCR_4( __kmp_nth ) > ( __kmp_avail_proc ? __kmp_avail_proc :
      __kmp_xproc ) ) {
        KMP_YIELD( TRUE );
    }
    else {
        KMP_YIELD_SPIN( spins );
    }

    while ( KMP_COMPARE_AND_STORE_ACQ32( & ( lck->lk.poll ), 0, gtid + 1 )
      == 0 ) {
        //
        // FIXME - use exponential backoff here
        //
        if ( TCR_4( __kmp_nth ) > ( __kmp_avail_proc ? __kmp_avail_proc :
          __kmp_xproc ) ) {
            KMP_YIELD( TRUE );
        }
        else {
            KMP_YIELD_SPIN( spins );
        }
    }
}

void
__kmp_acquire_tas_lock( kmp_tas_lock_t *lck, kmp_int32 gtid )
{
    __kmp_acquire_tas_lock_timed_template( lck, gtid );
}

static void
__kmp_acquire_tas_lock_with_checks( kmp_tas_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_set_lock";
        if ( ( sizeof ( kmp_tas_lock_t ) <= OMP_LOCK_T_SIZE )
          && __kmp_is_tas_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
        if ( ( gtid >= 0 ) && ( __kmp_get_tas_lock_owner( lck ) == gtid ) ) {
            KMP_FATAL( LockIsAlreadyOwned, func );
        }
    }
    __kmp_acquire_tas_lock( lck, gtid );
}

int
__kmp_test_tas_lock( kmp_tas_lock_t *lck, kmp_int32 gtid )
{
    if ( KMP_COMPARE_AND_STORE_ACQ32( & ( lck->lk.poll ), 0, gtid + 1 ) ) {
        return TRUE;
    }
    return FALSE;
}

static int
__kmp_test_tas_lock_with_checks( kmp_tas_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_test_lock";
        if ( ( sizeof ( kmp_tas_lock_t ) <= OMP_LOCK_T_SIZE )
          && __kmp_is_tas_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
    }
    return __kmp_test_tas_lock( lck, gtid );
}

void
__kmp_release_tas_lock( kmp_tas_lock_t *lck, kmp_int32 gtid )
{
    KMP_MB();       /* Flush all pending memory write invalidates.  */

    TCW_4( lck->lk.poll, 0 );

    KMP_MB();       /* Flush all pending memory write invalidates.  */

    KMP_YIELD( TCR_4( __kmp_nth ) > ( __kmp_avail_proc ? __kmp_avail_proc :
      __kmp_xproc ) );
}

static void
__kmp_release_tas_lock_with_checks( kmp_tas_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_unset_lock";
        KMP_MB();  /* in case another processor initialized lock */
        if ( ( sizeof ( kmp_tas_lock_t ) <= OMP_LOCK_T_SIZE )
          && __kmp_is_tas_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
        if ( __kmp_get_tas_lock_owner( lck ) == -1 ) {
            KMP_FATAL( LockUnsettingFree, func );
        }
        if ( ( gtid >= 0 ) && ( __kmp_get_tas_lock_owner( lck ) >= 0 )
          && ( __kmp_get_tas_lock_owner( lck ) != gtid ) ) {
            KMP_FATAL( LockUnsettingSetByAnother, func );
        }
    }
    __kmp_release_tas_lock( lck, gtid );
}

void
__kmp_init_tas_lock( kmp_tas_lock_t * lck )
{
    TCW_4( lck->lk.poll, 0 );
}

static void
__kmp_init_tas_lock_with_checks( kmp_tas_lock_t * lck )
{
    __kmp_init_tas_lock( lck );
}

void
__kmp_destroy_tas_lock( kmp_tas_lock_t *lck )
{
    lck->lk.poll = 0;
}

static void
__kmp_destroy_tas_lock_with_checks( kmp_tas_lock_t *lck )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_destroy_lock";
        if ( ( sizeof ( kmp_tas_lock_t ) <= OMP_LOCK_T_SIZE )
          && __kmp_is_tas_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
        if ( __kmp_get_tas_lock_owner( lck ) != -1 ) {
            KMP_FATAL( LockStillOwned, func );
        }
    }
    __kmp_destroy_tas_lock( lck );
}


//
// nested test and set locks
//

void
__kmp_acquire_nested_tas_lock( kmp_tas_lock_t *lck, kmp_int32 gtid )
{
    KMP_DEBUG_ASSERT( gtid >= 0 );

    if ( __kmp_get_tas_lock_owner( lck ) == gtid ) {
        lck->lk.depth_locked += 1;
    }
    else {
        __kmp_acquire_tas_lock_timed_template( lck, gtid );
        lck->lk.depth_locked = 1;
    }
}

static void
__kmp_acquire_nested_tas_lock_with_checks( kmp_tas_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_set_nest_lock";
        if ( ! __kmp_is_tas_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
    }
    __kmp_acquire_nested_tas_lock( lck, gtid );
}

int
__kmp_test_nested_tas_lock( kmp_tas_lock_t *lck, kmp_int32 gtid )
{
    int retval;

    KMP_DEBUG_ASSERT( gtid >= 0 );

    if ( __kmp_get_tas_lock_owner( lck ) == gtid ) {
        retval = ++lck->lk.depth_locked;
    }
    else if ( !__kmp_test_tas_lock( lck, gtid ) ) {
        retval = 0;
    }
    else {
        KMP_MB();
        retval = lck->lk.depth_locked = 1;
    }
    return retval;
}

static int
__kmp_test_nested_tas_lock_with_checks( kmp_tas_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_test_nest_lock";
        if ( ! __kmp_is_tas_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
    }
    return __kmp_test_nested_tas_lock( lck, gtid );
}

void
__kmp_release_nested_tas_lock( kmp_tas_lock_t *lck, kmp_int32 gtid )
{
    KMP_DEBUG_ASSERT( gtid >= 0 );

    KMP_MB();
    if ( --(lck->lk.depth_locked) == 0 ) {
        __kmp_release_tas_lock( lck, gtid );
    }
}

static void
__kmp_release_nested_tas_lock_with_checks( kmp_tas_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_unset_nest_lock";
        KMP_MB();  /* in case another processor initialized lock */
        if ( ! __kmp_is_tas_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
        if ( __kmp_get_tas_lock_owner( lck ) == -1 ) {
            KMP_FATAL( LockUnsettingFree, func );
        }
        if ( __kmp_get_tas_lock_owner( lck ) != gtid ) {
            KMP_FATAL( LockUnsettingSetByAnother, func );
        }
    }
    __kmp_release_nested_tas_lock( lck, gtid );
}

void
__kmp_init_nested_tas_lock( kmp_tas_lock_t * lck )
{
    __kmp_init_tas_lock( lck );
    lck->lk.depth_locked = 0; // >= 0 for nestable locks, -1 for simple locks
}

static void
__kmp_init_nested_tas_lock_with_checks( kmp_tas_lock_t * lck )
{
    __kmp_init_nested_tas_lock( lck );
}

void
__kmp_destroy_nested_tas_lock( kmp_tas_lock_t *lck )
{
    __kmp_destroy_tas_lock( lck );
    lck->lk.depth_locked = 0;
}

static void
__kmp_destroy_nested_tas_lock_with_checks( kmp_tas_lock_t *lck )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_destroy_nest_lock";
        if ( ! __kmp_is_tas_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
        if ( __kmp_get_tas_lock_owner( lck ) != -1 ) {
            KMP_FATAL( LockStillOwned, func );
        }
    }
    __kmp_destroy_nested_tas_lock( lck );
}


#if KMP_OS_LINUX && (KMP_ARCH_X86 || KMP_ARCH_X86_64)

/* ------------------------------------------------------------------------ */
/* futex locks */

// futex locks are really just test and set locks, with a different method
// of handling contention.  They take the same amount of space as test and
// set locks, and are allocated the same way (i.e. use the area allocated by
// the compiler for non-nested locks / allocate nested locks on the heap).

static kmp_int32
__kmp_get_futex_lock_owner( kmp_futex_lock_t *lck )
{
    return ( TCR_4( lck->lk.poll ) >> 1 ) - 1;
}

static inline bool
__kmp_is_futex_lock_nestable( kmp_futex_lock_t *lck )
{
    return lck->lk.depth_locked != -1;
}

__forceinline static void
__kmp_acquire_futex_lock_timed_template( kmp_futex_lock_t *lck, kmp_int32 gtid )
{
    kmp_int32 gtid_code = ( gtid + 1 ) << 1;

    KMP_MB();

#ifdef USE_LOCK_PROFILE
    kmp_uint32 curr = TCR_4( lck->lk.poll );
    if ( ( curr != 0 ) && ( curr != gtid_code ) )
        __kmp_printf( "LOCK CONTENTION: %p\n", lck );
    /* else __kmp_printf( "." );*/
#endif /* USE_LOCK_PROFILE */

    KA_TRACE( 1000, ("__kmp_acquire_futex_lock: lck:%p(0x%x), T#%d entering\n",
      lck, lck->lk.poll, gtid ) );

    kmp_int32 poll_val;
    while ( ( poll_val = __kmp_compare_and_store_ret32( & ( lck->lk.poll ), 0,
      gtid_code ) ) != 0 ) {
        kmp_int32 cond = poll_val & 1;
        KA_TRACE( 1000, ("__kmp_acquire_futex_lock: lck:%p, T#%d poll_val = 0x%x cond = 0x%x\n",
           lck, gtid, poll_val, cond ) );

        //
        // NOTE: if you try to use the following condition for this branch
        //
        // if ( poll_val & 1 == 0 )
        //
        // Then the 12.0 compiler has a bug where the following block will
        // always be skipped, regardless of the value of the LSB of poll_val.
        //
        if ( ! cond ) {
            //
            // Try to set the lsb in the poll to indicate to the owner
            // thread that they need to wake this thread up.
            //
            if ( ! __kmp_compare_and_store32( & ( lck->lk.poll ),
              poll_val, poll_val | 1 ) ) {
                KA_TRACE( 1000, ("__kmp_acquire_futex_lock: lck:%p(0x%x), T#%d can't set bit 0\n",
                  lck, lck->lk.poll, gtid ) );
                continue;
            }
            poll_val |= 1;

            KA_TRACE( 1000, ("__kmp_acquire_futex_lock: lck:%p(0x%x), T#%d bit 0 set\n",
              lck, lck->lk.poll, gtid ) );
        }

        KA_TRACE( 1000, ("__kmp_acquire_futex_lock: lck:%p, T#%d before futex_wait(0x%x)\n",
           lck, gtid, poll_val ) );

        kmp_int32 rc;
        if ( ( rc = syscall( __NR_futex, & ( lck->lk.poll ), FUTEX_WAIT,
          poll_val, NULL, NULL, 0 ) ) != 0 ) {
            KA_TRACE( 1000, ("__kmp_acquire_futex_lock: lck:%p, T#%d futex_wait(0x%x) failed (rc=%d errno=%d)\n",
               lck, gtid, poll_val, rc, errno ) );
            continue;
        }

        KA_TRACE( 1000, ("__kmp_acquire_futex_lock: lck:%p, T#%d after futex_wait(0x%x)\n",
           lck, gtid, poll_val ) );
        //
        // This thread has now done a succesful futex wait call and was
        // entered on the OS futex queue.  We must now perform a futex
        // wake call when releasing the lock, as we have no idea how many
        // other threads are in the queue.
        //
        gtid_code |= 1;
    }

    KA_TRACE( 1000, ("__kmp_acquire_futex_lock: lck:%p(0x%x), T#%d exiting\n",
      lck, lck->lk.poll, gtid ) );
}

void
__kmp_acquire_futex_lock( kmp_futex_lock_t *lck, kmp_int32 gtid )
{
    __kmp_acquire_futex_lock_timed_template( lck, gtid );
}

static void
__kmp_acquire_futex_lock_with_checks( kmp_futex_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_set_lock";
        if ( ( sizeof ( kmp_futex_lock_t ) <= OMP_LOCK_T_SIZE )
          && __kmp_is_futex_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
        if ( ( gtid >= 0 ) && ( __kmp_get_futex_lock_owner( lck ) == gtid ) ) {
            KMP_FATAL( LockIsAlreadyOwned, func );
        }
    }
    __kmp_acquire_futex_lock( lck, gtid );
}

int
__kmp_test_futex_lock( kmp_futex_lock_t *lck, kmp_int32 gtid )
{
    if ( KMP_COMPARE_AND_STORE_ACQ32( & ( lck->lk.poll ), 0, ( gtid + 1 ) << 1 ) ) {
        return TRUE;
    }
    return FALSE;
}

static int
__kmp_test_futex_lock_with_checks( kmp_futex_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_test_lock";
        if ( ( sizeof ( kmp_futex_lock_t ) <= OMP_LOCK_T_SIZE )
          && __kmp_is_futex_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
    }
    return __kmp_test_futex_lock( lck, gtid );
}

void
__kmp_release_futex_lock( kmp_futex_lock_t *lck, kmp_int32 gtid )
{
    KMP_MB();       /* Flush all pending memory write invalidates.  */

    KA_TRACE( 1000, ("__kmp_release_futex_lock: lck:%p(0x%x), T#%d entering\n",
      lck, lck->lk.poll, gtid ) );

    kmp_int32 poll_val = __kmp_xchg_fixed32( & ( lck->lk.poll ), 0 );

    KA_TRACE( 1000, ("__kmp_release_futex_lock: lck:%p, T#%d released poll_val = 0x%x\n",
       lck, gtid, poll_val ) );

    if ( poll_val & 1 ) {
        KA_TRACE( 1000, ("__kmp_release_futex_lock: lck:%p, T#%d futex_wake 1 thread\n",
           lck, gtid ) );
        syscall( __NR_futex, & ( lck->lk.poll ), FUTEX_WAKE, 1, NULL, NULL, 0 );
    }

    KMP_MB();       /* Flush all pending memory write invalidates.  */

    KA_TRACE( 1000, ("__kmp_release_futex_lock: lck:%p(0x%x), T#%d exiting\n",
      lck, lck->lk.poll, gtid ) );

    KMP_YIELD( TCR_4( __kmp_nth ) > ( __kmp_avail_proc ? __kmp_avail_proc :
      __kmp_xproc ) );
}

static void
__kmp_release_futex_lock_with_checks( kmp_futex_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_unset_lock";
        KMP_MB();  /* in case another processor initialized lock */
        if ( ( sizeof ( kmp_futex_lock_t ) <= OMP_LOCK_T_SIZE )
          && __kmp_is_futex_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
        if ( __kmp_get_futex_lock_owner( lck ) == -1 ) {
            KMP_FATAL( LockUnsettingFree, func );
        }
        if ( ( gtid >= 0 ) && ( __kmp_get_futex_lock_owner( lck ) >= 0 )
          && ( __kmp_get_futex_lock_owner( lck ) != gtid ) ) {
            KMP_FATAL( LockUnsettingSetByAnother, func );
        }
    }
    __kmp_release_futex_lock( lck, gtid );
}

void
__kmp_init_futex_lock( kmp_futex_lock_t * lck )
{
    TCW_4( lck->lk.poll, 0 );
}

static void
__kmp_init_futex_lock_with_checks( kmp_futex_lock_t * lck )
{
    __kmp_init_futex_lock( lck );
}

void
__kmp_destroy_futex_lock( kmp_futex_lock_t *lck )
{
    lck->lk.poll = 0;
}

static void
__kmp_destroy_futex_lock_with_checks( kmp_futex_lock_t *lck )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_destroy_lock";
        if ( ( sizeof ( kmp_futex_lock_t ) <= OMP_LOCK_T_SIZE )
          && __kmp_is_futex_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
        if ( __kmp_get_futex_lock_owner( lck ) != -1 ) {
            KMP_FATAL( LockStillOwned, func );
        }
    }
    __kmp_destroy_futex_lock( lck );
}


//
// nested futex locks
//

void
__kmp_acquire_nested_futex_lock( kmp_futex_lock_t *lck, kmp_int32 gtid )
{
    KMP_DEBUG_ASSERT( gtid >= 0 );

    if ( __kmp_get_futex_lock_owner( lck ) == gtid ) {
        lck->lk.depth_locked += 1;
    }
    else {
        __kmp_acquire_futex_lock_timed_template( lck, gtid );
        lck->lk.depth_locked = 1;
    }
}

static void
__kmp_acquire_nested_futex_lock_with_checks( kmp_futex_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_set_nest_lock";
        if ( ! __kmp_is_futex_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
    }
    __kmp_acquire_nested_futex_lock( lck, gtid );
}

int
__kmp_test_nested_futex_lock( kmp_futex_lock_t *lck, kmp_int32 gtid )
{
    int retval;

    KMP_DEBUG_ASSERT( gtid >= 0 );

    if ( __kmp_get_futex_lock_owner( lck ) == gtid ) {
        retval = ++lck->lk.depth_locked;
    }
    else if ( !__kmp_test_futex_lock( lck, gtid ) ) {
        retval = 0;
    }
    else {
        KMP_MB();
        retval = lck->lk.depth_locked = 1;
    }
    return retval;
}

static int
__kmp_test_nested_futex_lock_with_checks( kmp_futex_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_test_nest_lock";
        if ( ! __kmp_is_futex_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
    }
    return __kmp_test_nested_futex_lock( lck, gtid );
}

void
__kmp_release_nested_futex_lock( kmp_futex_lock_t *lck, kmp_int32 gtid )
{
    KMP_DEBUG_ASSERT( gtid >= 0 );

    KMP_MB();
    if ( --(lck->lk.depth_locked) == 0 ) {
        __kmp_release_futex_lock( lck, gtid );
    }
}

static void
__kmp_release_nested_futex_lock_with_checks( kmp_futex_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_unset_nest_lock";
        KMP_MB();  /* in case another processor initialized lock */
        if ( ! __kmp_is_futex_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
        if ( __kmp_get_futex_lock_owner( lck ) == -1 ) {
            KMP_FATAL( LockUnsettingFree, func );
        }
        if ( __kmp_get_futex_lock_owner( lck ) != gtid ) {
            KMP_FATAL( LockUnsettingSetByAnother, func );
        }
    }
    __kmp_release_nested_futex_lock( lck, gtid );
}

void
__kmp_init_nested_futex_lock( kmp_futex_lock_t * lck )
{
    __kmp_init_futex_lock( lck );
    lck->lk.depth_locked = 0; // >= 0 for nestable locks, -1 for simple locks
}

static void
__kmp_init_nested_futex_lock_with_checks( kmp_futex_lock_t * lck )
{
    __kmp_init_nested_futex_lock( lck );
}

void
__kmp_destroy_nested_futex_lock( kmp_futex_lock_t *lck )
{
    __kmp_destroy_futex_lock( lck );
    lck->lk.depth_locked = 0;
}

static void
__kmp_destroy_nested_futex_lock_with_checks( kmp_futex_lock_t *lck )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_destroy_nest_lock";
        if ( ! __kmp_is_futex_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
        if ( __kmp_get_futex_lock_owner( lck ) != -1 ) {
            KMP_FATAL( LockStillOwned, func );
        }
    }
    __kmp_destroy_nested_futex_lock( lck );
}

#endif // KMP_OS_LINUX && (KMP_ARCH_X86 || KMP_ARCH_X86_64)


/* ------------------------------------------------------------------------ */
/* ticket (bakery) locks */

static kmp_int32
__kmp_get_ticket_lock_owner( kmp_ticket_lock_t *lck )
{
    return TCR_4( lck->lk.owner_id ) - 1;
}

static inline bool
__kmp_is_ticket_lock_nestable( kmp_ticket_lock_t *lck )
{
    return lck->lk.depth_locked != -1;
}

static kmp_uint32
__kmp_bakery_check(kmp_uint value, kmp_uint checker)
{
    register kmp_uint32 pause;

    if (value == checker) {
        return TRUE;
    }
    for (pause = checker - value; pause != 0; --pause) {
        __kmp_static_delay(TRUE);
    }
    return FALSE;
}

__forceinline static void
__kmp_acquire_ticket_lock_timed_template( kmp_ticket_lock_t *lck, kmp_int32 gtid )
{
    kmp_uint32 my_ticket;
    KMP_MB();

    my_ticket = KMP_TEST_THEN_INC32( (kmp_int32 *) &lck->lk.next_ticket );

#ifdef USE_LOCK_PROFILE
    if ( TCR_4( lck->lk.now_serving ) != my_ticket )
        __kmp_printf( "LOCK CONTENTION: %p\n", lck );
    /* else __kmp_printf( "." );*/
#endif /* USE_LOCK_PROFILE */

    if ( TCR_4( lck->lk.now_serving ) == my_ticket ) {
        return;
    }
    KMP_WAIT_YIELD( &lck->lk.now_serving, my_ticket, __kmp_bakery_check, lck );
}

void
__kmp_acquire_ticket_lock( kmp_ticket_lock_t *lck, kmp_int32 gtid )
{
    __kmp_acquire_ticket_lock_timed_template( lck, gtid );
}

static void
__kmp_acquire_ticket_lock_with_checks( kmp_ticket_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_set_lock";
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( __kmp_is_ticket_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
        if ( ( gtid >= 0 ) && ( __kmp_get_ticket_lock_owner( lck ) == gtid ) ) {
            KMP_FATAL( LockIsAlreadyOwned, func );
        }
    }

    __kmp_acquire_ticket_lock( lck, gtid );

    if ( __kmp_env_consistency_check ) {
        lck->lk.owner_id = gtid + 1;
    }
}

int
__kmp_test_ticket_lock( kmp_ticket_lock_t *lck, kmp_int32 gtid )
{
    kmp_uint32 my_ticket = TCR_4( lck->lk.next_ticket );
    if ( TCR_4( lck->lk.now_serving ) == my_ticket ) {
        kmp_uint32 next_ticket = my_ticket + 1;
        if ( KMP_COMPARE_AND_STORE_ACQ32( (kmp_int32 *) &lck->lk.next_ticket,
          my_ticket, next_ticket ) ) {
            return TRUE;
        }
    }
    return FALSE;
}

static int
__kmp_test_ticket_lock_with_checks( kmp_ticket_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_test_lock";
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( __kmp_is_ticket_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
    }

    int retval = __kmp_test_ticket_lock( lck, gtid );

    if ( __kmp_env_consistency_check && retval ) {
        lck->lk.owner_id = gtid + 1;
    }
    return retval;
}

void
__kmp_release_ticket_lock( kmp_ticket_lock_t *lck, kmp_int32 gtid )
{
    kmp_uint32  distance;

    KMP_MB();       /* Flush all pending memory write invalidates.  */

    distance = ( TCR_4( lck->lk.next_ticket ) - TCR_4( lck->lk.now_serving ) );

    lck->lk.now_serving += 1;

    KMP_MB();       /* Flush all pending memory write invalidates.  */

    KMP_YIELD( distance
      > (kmp_uint32) (__kmp_avail_proc ? __kmp_avail_proc : __kmp_xproc) );
}

static void
__kmp_release_ticket_lock_with_checks( kmp_ticket_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_unset_lock";
        KMP_MB();  /* in case another processor initialized lock */
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( __kmp_is_ticket_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
        if ( __kmp_get_ticket_lock_owner( lck ) == -1 ) {
            KMP_FATAL( LockUnsettingFree, func );
        }
        if ( ( gtid >= 0 ) && ( __kmp_get_ticket_lock_owner( lck ) >= 0 )
          && ( __kmp_get_ticket_lock_owner( lck ) != gtid ) ) {
            KMP_FATAL( LockUnsettingSetByAnother, func );
        }
        lck->lk.owner_id = 0;
    }
    __kmp_release_ticket_lock( lck, gtid );
}

void
__kmp_init_ticket_lock( kmp_ticket_lock_t * lck )
{
    lck->lk.location = NULL;
    TCW_4( lck->lk.next_ticket, 0 );
    TCW_4( lck->lk.now_serving, 0 );
    lck->lk.owner_id = 0;      // no thread owns the lock.
    lck->lk.depth_locked = -1; // -1 => not a nested lock.
    lck->lk.initialized = (kmp_ticket_lock *)lck;
}

static void
__kmp_init_ticket_lock_with_checks( kmp_ticket_lock_t * lck )
{
    __kmp_init_ticket_lock( lck );
}

void
__kmp_destroy_ticket_lock( kmp_ticket_lock_t *lck )
{
    lck->lk.initialized = NULL;
    lck->lk.location    = NULL;
    lck->lk.next_ticket = 0;
    lck->lk.now_serving = 0;
    lck->lk.owner_id = 0;
    lck->lk.depth_locked = -1;
}

static void
__kmp_destroy_ticket_lock_with_checks( kmp_ticket_lock_t *lck )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_destroy_lock";
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( __kmp_is_ticket_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
        if ( __kmp_get_ticket_lock_owner( lck ) != -1 ) {
            KMP_FATAL( LockStillOwned, func );
        }
    }
    __kmp_destroy_ticket_lock( lck );
}


//
// nested ticket locks
//

void
__kmp_acquire_nested_ticket_lock( kmp_ticket_lock_t *lck, kmp_int32 gtid )
{
    KMP_DEBUG_ASSERT( gtid >= 0 );

    if ( __kmp_get_ticket_lock_owner( lck ) == gtid ) {
        lck->lk.depth_locked += 1;
    }
    else {
        __kmp_acquire_ticket_lock_timed_template( lck, gtid );
        KMP_MB();
        lck->lk.depth_locked = 1;
        KMP_MB();
        lck->lk.owner_id = gtid + 1;
    }
}

static void
__kmp_acquire_nested_ticket_lock_with_checks( kmp_ticket_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_set_nest_lock";
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( ! __kmp_is_ticket_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
    }
    __kmp_acquire_nested_ticket_lock( lck, gtid );
}

int
__kmp_test_nested_ticket_lock( kmp_ticket_lock_t *lck, kmp_int32 gtid )
{
    int retval;

    KMP_DEBUG_ASSERT( gtid >= 0 );

    if ( __kmp_get_ticket_lock_owner( lck ) == gtid ) {
        retval = ++lck->lk.depth_locked;
    }
    else if ( !__kmp_test_ticket_lock( lck, gtid ) ) {
        retval = 0;
    }
    else {
        KMP_MB();
        retval = lck->lk.depth_locked = 1;
        KMP_MB();
        lck->lk.owner_id = gtid + 1;
    }
    return retval;
}

static int
__kmp_test_nested_ticket_lock_with_checks( kmp_ticket_lock_t *lck,
  kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_test_nest_lock";
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( ! __kmp_is_ticket_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
    }
    return __kmp_test_nested_ticket_lock( lck, gtid );
}

void
__kmp_release_nested_ticket_lock( kmp_ticket_lock_t *lck, kmp_int32 gtid )
{
    KMP_DEBUG_ASSERT( gtid >= 0 );

    KMP_MB();
    if ( --(lck->lk.depth_locked) == 0 ) {
        KMP_MB();
        lck->lk.owner_id = 0;
        __kmp_release_ticket_lock( lck, gtid );
    }
}

static void
__kmp_release_nested_ticket_lock_with_checks( kmp_ticket_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_unset_nest_lock";
        KMP_MB();  /* in case another processor initialized lock */
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( ! __kmp_is_ticket_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
        if ( __kmp_get_ticket_lock_owner( lck ) == -1 ) {
            KMP_FATAL( LockUnsettingFree, func );
        }
        if ( __kmp_get_ticket_lock_owner( lck ) != gtid ) {
            KMP_FATAL( LockUnsettingSetByAnother, func );
        }
    }
    __kmp_release_nested_ticket_lock( lck, gtid );
}

void
__kmp_init_nested_ticket_lock( kmp_ticket_lock_t * lck )
{
    __kmp_init_ticket_lock( lck );
    lck->lk.depth_locked = 0; // >= 0 for nestable locks, -1 for simple locks
}

static void
__kmp_init_nested_ticket_lock_with_checks( kmp_ticket_lock_t * lck )
{
    __kmp_init_nested_ticket_lock( lck );
}

void
__kmp_destroy_nested_ticket_lock( kmp_ticket_lock_t *lck )
{
    __kmp_destroy_ticket_lock( lck );
    lck->lk.depth_locked = 0;
}

static void
__kmp_destroy_nested_ticket_lock_with_checks( kmp_ticket_lock_t *lck )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_destroy_nest_lock";
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( ! __kmp_is_ticket_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
        if ( __kmp_get_ticket_lock_owner( lck ) != -1 ) {
            KMP_FATAL( LockStillOwned, func );
        }
    }
    __kmp_destroy_nested_ticket_lock( lck );
}


//
// access functions to fields which don't exist for all lock kinds.
//

static int
__kmp_is_ticket_lock_initialized( kmp_ticket_lock_t *lck )
{
    return lck == lck->lk.initialized;
}

static const ident_t *
__kmp_get_ticket_lock_location( kmp_ticket_lock_t *lck )
{
    return lck->lk.location;
}

static void
__kmp_set_ticket_lock_location( kmp_ticket_lock_t *lck, const ident_t *loc )
{
    lck->lk.location = loc;
}

static kmp_lock_flags_t
__kmp_get_ticket_lock_flags( kmp_ticket_lock_t *lck )
{
    return lck->lk.flags;
}

static void
__kmp_set_ticket_lock_flags( kmp_ticket_lock_t *lck, kmp_lock_flags_t flags )
{
    lck->lk.flags = flags;
}

/* ------------------------------------------------------------------------ */
/* queuing locks */

/*
 * First the states
 * (head,tail) =  0, 0  means lock is unheld, nobody on queue
 *   UINT_MAX or -1, 0  means lock is held, nobody on queue
 *                h, h  means lock is held or about to transition, 1 element on queue
 *                h, t  h <> t, means lock is held or about to transition, >1 elements on queue
 *
 * Now the transitions
 *    Acquire(0,0)  = -1 ,0
 *    Release(0,0)  = Error
 *    Acquire(-1,0) =  h ,h    h > 0
 *    Release(-1,0) =  0 ,0
 *    Acquire(h,h)  =  h ,t    h > 0, t > 0, h <> t
 *    Release(h,h)  = -1 ,0    h > 0
 *    Acquire(h,t)  =  h ,t'   h > 0, t > 0, t' > 0, h <> t, h <> t', t <> t'
 *    Release(h,t)  =  h',t    h > 0, t > 0, h <> t, h <> h', h' maybe = t
 *
 * And pictorially
 *
 *
 *          +-----+
 *          | 0, 0|------- release -------> Error
 *          +-----+
 *            |  ^
 *     acquire|  |release
 *            |  |
 *            |  |
 *            v  |
 *          +-----+
 *          |-1, 0|
 *          +-----+
 *            |  ^
 *     acquire|  |release
 *            |  |
 *            |  |
 *            v  |
 *          +-----+
 *          | h, h|
 *          +-----+
 *            |  ^
 *     acquire|  |release
 *            |  |
 *            |  |
 *            v  |
 *          +-----+
 *          | h, t|----- acquire, release loopback ---+
 *          +-----+                                   |
 *               ^                                    |
 *               |                                    |
 *               +------------------------------------+
 *
 */

#ifdef DEBUG_QUEUING_LOCKS

/* Stuff for circular trace buffer */
#define TRACE_BUF_ELE	1024
static char traces[TRACE_BUF_ELE][128] = { 0 }
static int tc = 0;
#define TRACE_LOCK(X,Y)          sprintf( traces[tc++ % TRACE_BUF_ELE], "t%d at %s\n", X, Y );
#define TRACE_LOCK_T(X,Y,Z)      sprintf( traces[tc++ % TRACE_BUF_ELE], "t%d at %s%d\n", X,Y,Z );
#define TRACE_LOCK_HT(X,Y,Z,Q)   sprintf( traces[tc++ % TRACE_BUF_ELE], "t%d at %s %d,%d\n", X, Y, Z, Q );

static void
__kmp_dump_queuing_lock( kmp_info_t *this_thr, kmp_int32 gtid,
  kmp_queuing_lock_t *lck, kmp_int32 head_id, kmp_int32 tail_id )
{
    kmp_int32 t, i;

    __kmp_printf_no_lock( "\n__kmp_dump_queuing_lock: TRACE BEGINS HERE! \n" );

    i = tc % TRACE_BUF_ELE;
    __kmp_printf_no_lock( "%s\n", traces[i] );
    i = (i+1) % TRACE_BUF_ELE;
    while ( i != (tc % TRACE_BUF_ELE) ) {
        __kmp_printf_no_lock( "%s", traces[i] );
        i = (i+1) % TRACE_BUF_ELE;
    }
    __kmp_printf_no_lock( "\n" );

    __kmp_printf_no_lock(
             "\n__kmp_dump_queuing_lock: gtid+1:%d, spin_here:%d, next_wait:%d, head_id:%d, tail_id:%d\n",
             gtid+1, this_thr->th.th_spin_here, this_thr->th.th_next_waiting,
             head_id, tail_id );

    __kmp_printf_no_lock( "\t\thead: %d ", lck->lk.head_id );

    if ( lck->lk.head_id >= 1 ) {
        t = __kmp_threads[lck->lk.head_id-1]->th.th_next_waiting;
        while (t > 0) {
            __kmp_printf_no_lock( "-> %d ", t );
            t = __kmp_threads[t-1]->th.th_next_waiting;
        }
    }
    __kmp_printf_no_lock( ";  tail: %d ", lck->lk.tail_id );
    __kmp_printf_no_lock( "\n\n" );
}

#endif /* DEBUG_QUEUING_LOCKS */

static kmp_int32
__kmp_get_queuing_lock_owner( kmp_queuing_lock_t *lck )
{
    return TCR_4( lck->lk.owner_id ) - 1;
}

static inline bool
__kmp_is_queuing_lock_nestable( kmp_queuing_lock_t *lck )
{
    return lck->lk.depth_locked != -1;
}

/* Acquire a lock using a the queuing lock implementation */
template <bool takeTime>
/* [TLW] The unused template above is left behind because of what BEB believes is a 
   potential compiler problem with __forceinline. */
__forceinline static void
__kmp_acquire_queuing_lock_timed_template( kmp_queuing_lock_t *lck,
  kmp_int32 gtid )
{
    register kmp_info_t *this_thr    = __kmp_thread_from_gtid( gtid );
    volatile kmp_int32  *head_id_p   = & lck->lk.head_id;
    volatile kmp_int32  *tail_id_p   = & lck->lk.tail_id;
    volatile kmp_uint32 *spin_here_p;
    kmp_int32 need_mf = 1;

    KA_TRACE( 1000, ("__kmp_acquire_queuing_lock: lck:%p, T#%d entering\n", lck, gtid ));

    KMP_DEBUG_ASSERT( this_thr != NULL );
    spin_here_p = & this_thr->th.th_spin_here;

#ifdef DEBUG_QUEUING_LOCKS
    TRACE_LOCK( gtid+1, "acq ent" );
    if ( *spin_here_p )
        __kmp_dump_queuing_lock( this_thr, gtid, lck, *head_id_p, *tail_id_p );
    if ( this_thr->th.th_next_waiting != 0 )
        __kmp_dump_queuing_lock( this_thr, gtid, lck, *head_id_p, *tail_id_p );
#endif
    KMP_DEBUG_ASSERT( !*spin_here_p );
    KMP_DEBUG_ASSERT( this_thr->th.th_next_waiting == 0 );


    /* The following st.rel to spin_here_p needs to precede the cmpxchg.acq to head_id_p
       that may follow, not just in execution order, but also in visibility order.  This way,
       when a releasing thread observes the changes to the queue by this thread, it can
       rightly assume that spin_here_p has already been set to TRUE, so that when it sets
       spin_here_p to FALSE, it is not premature.  If the releasing thread sets spin_here_p
       to FALSE before this thread sets it to TRUE, this thread will hang.
    */
    *spin_here_p = TRUE;  /* before enqueuing to prevent race */

    while( 1 ) {
        kmp_int32 enqueued;
        kmp_int32 head;
        kmp_int32 tail;

        head = *head_id_p;

        switch ( head ) {

            case -1:
            {
#ifdef DEBUG_QUEUING_LOCKS
                tail = *tail_id_p;
                TRACE_LOCK_HT( gtid+1, "acq read: ", head, tail );
#endif
                tail = 0;  /* to make sure next link asynchronously read is not set accidentally;
                           this assignment prevents us from entering the if ( t > 0 )
                           condition in the enqueued case below, which is not necessary for
                           this state transition */

                need_mf = 0;
                /* try (-1,0)->(tid,tid) */
                enqueued = KMP_COMPARE_AND_STORE_ACQ64( (volatile kmp_int64 *) tail_id_p,
                  KMP_PACK_64( -1, 0 ),
                  KMP_PACK_64( gtid+1, gtid+1 ) );
#ifdef DEBUG_QUEUING_LOCKS
                  if ( enqueued ) TRACE_LOCK( gtid+1, "acq enq: (-1,0)->(tid,tid)" );
#endif
            }
            break;

            default:
            {
                tail = *tail_id_p;
                KMP_DEBUG_ASSERT( tail != gtid + 1 );

#ifdef DEBUG_QUEUING_LOCKS
                TRACE_LOCK_HT( gtid+1, "acq read: ", head, tail );
#endif

                if ( tail == 0 ) {
                    enqueued = FALSE;
                }
                else {
                    need_mf = 0;
                    /* try (h,t) or (h,h)->(h,tid) */
                    enqueued = KMP_COMPARE_AND_STORE_ACQ32( tail_id_p, tail, gtid+1 );

#ifdef DEBUG_QUEUING_LOCKS
                        if ( enqueued ) TRACE_LOCK( gtid+1, "acq enq: (h,t)->(h,tid)" );
#endif
                }
            }
            break;

            case 0: /* empty queue */
            {
                kmp_int32 grabbed_lock;

#ifdef DEBUG_QUEUING_LOCKS
                tail = *tail_id_p;
                TRACE_LOCK_HT( gtid+1, "acq read: ", head, tail );
#endif
                /* try (0,0)->(-1,0) */

                /* only legal transition out of head = 0 is head = -1 with no change to tail */
                grabbed_lock = KMP_COMPARE_AND_STORE_ACQ32( head_id_p, 0, -1 );

                if ( grabbed_lock ) {

                    *spin_here_p = FALSE;

                    KA_TRACE( 1000, ("__kmp_acquire_queuing_lock: lck:%p, T#%d exiting: no queuing\n",
                              lck, gtid ));
#ifdef DEBUG_QUEUING_LOCKS
                    TRACE_LOCK_HT( gtid+1, "acq exit: ", head, 0 );
#endif
                    return; /* lock holder cannot be on queue */
                }
                enqueued = FALSE;
            }
            break;
        }

        if ( enqueued ) {
            if ( tail > 0 ) {
                kmp_info_t *tail_thr = __kmp_thread_from_gtid( tail - 1 );
                KMP_ASSERT( tail_thr != NULL );
                tail_thr->th.th_next_waiting = gtid+1;
                /* corresponding wait for this write in release code */
            }
            KA_TRACE( 1000, ("__kmp_acquire_queuing_lock: lck:%p, T#%d waiting for lock\n", lck, gtid ));


            /* ToDo: May want to consider using __kmp_wait_sleep  or something that sleeps for
             *       throughput only here.
             */
            KMP_MB();
            KMP_WAIT_YIELD(spin_here_p, FALSE, KMP_EQ, lck);

#ifdef DEBUG_QUEUING_LOCKS
            TRACE_LOCK( gtid+1, "acq spin" );

            if ( this_thr->th.th_next_waiting != 0 )
                __kmp_dump_queuing_lock( this_thr, gtid, lck, *head_id_p, *tail_id_p );
#endif
            KMP_DEBUG_ASSERT( this_thr->th.th_next_waiting == 0 );
            KA_TRACE( 1000, ("__kmp_acquire_queuing_lock: lck:%p, T#%d exiting: after waiting on queue\n",
                      lck, gtid ));

#ifdef DEBUG_QUEUING_LOCKS
            TRACE_LOCK( gtid+1, "acq exit 2" );
#endif
            /* got lock, we were dequeued by the thread that released lock */
            return;
        }

        /* Yield if number of threads > number of logical processors */
        /* ToDo: Not sure why this should only be in oversubscription case,
           maybe should be traditional YIELD_INIT/YIELD_WHEN loop */
        KMP_YIELD( TCR_4( __kmp_nth ) > (__kmp_avail_proc ? __kmp_avail_proc :
          __kmp_xproc ) );
#ifdef DEBUG_QUEUING_LOCKS
        TRACE_LOCK( gtid+1, "acq retry" );
#endif

    }
    KMP_ASSERT2( 0, "should not get here" );
}

void
__kmp_acquire_queuing_lock( kmp_queuing_lock_t *lck, kmp_int32 gtid )
{
    KMP_DEBUG_ASSERT( gtid >= 0 );

    __kmp_acquire_queuing_lock_timed_template<false>( lck, gtid );
}

static void
__kmp_acquire_queuing_lock_with_checks( kmp_queuing_lock_t *lck,
  kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_set_lock";
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( __kmp_is_queuing_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
        if ( __kmp_get_queuing_lock_owner( lck ) == gtid ) {
            KMP_FATAL( LockIsAlreadyOwned, func );
        }
    }

    __kmp_acquire_queuing_lock( lck, gtid );

    if ( __kmp_env_consistency_check ) {
        lck->lk.owner_id = gtid + 1;
    }
}

int
__kmp_test_queuing_lock( kmp_queuing_lock_t *lck, kmp_int32 gtid )
{
    volatile kmp_int32 *head_id_p  = & lck->lk.head_id;
    kmp_int32 head;
#ifdef KMP_DEBUG
    kmp_info_t *this_thr;
#endif

    KA_TRACE( 1000, ("__kmp_test_queuing_lock: T#%d entering\n", gtid ));
    KMP_DEBUG_ASSERT( gtid >= 0 );
#ifdef KMP_DEBUG
    this_thr = __kmp_thread_from_gtid( gtid );
    KMP_DEBUG_ASSERT( this_thr != NULL );
    KMP_DEBUG_ASSERT( !this_thr->th.th_spin_here );
#endif

    head = *head_id_p;

    if ( head == 0 ) { /* nobody on queue, nobody holding */

        /* try (0,0)->(-1,0) */

        if ( KMP_COMPARE_AND_STORE_ACQ32( head_id_p, 0, -1 ) ) {
            KA_TRACE( 1000, ("__kmp_test_queuing_lock: T#%d exiting: holding lock\n", gtid ));
            return TRUE;
        }
    }

    KA_TRACE( 1000, ("__kmp_test_queuing_lock: T#%d exiting: without lock\n", gtid ));
    return FALSE;
}

static int
__kmp_test_queuing_lock_with_checks( kmp_queuing_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_test_lock";
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( __kmp_is_queuing_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
    }

    int retval = __kmp_test_queuing_lock( lck, gtid );

    if ( __kmp_env_consistency_check && retval ) {
        lck->lk.owner_id = gtid + 1;
    }
    return retval;
}

void
__kmp_release_queuing_lock( kmp_queuing_lock_t *lck, kmp_int32 gtid )
{
    register kmp_info_t *this_thr;
    volatile kmp_int32 *head_id_p = & lck->lk.head_id;
    volatile kmp_int32 *tail_id_p = & lck->lk.tail_id;

    KA_TRACE( 1000, ("__kmp_release_queuing_lock: lck:%p, T#%d entering\n", lck, gtid ));
    KMP_DEBUG_ASSERT( gtid >= 0 );
    this_thr    = __kmp_thread_from_gtid( gtid );
    KMP_DEBUG_ASSERT( this_thr != NULL );
#ifdef DEBUG_QUEUING_LOCKS
    TRACE_LOCK( gtid+1, "rel ent" );

    if ( this_thr->th.th_spin_here )
        __kmp_dump_queuing_lock( this_thr, gtid, lck, *head_id_p, *tail_id_p );
    if ( this_thr->th.th_next_waiting != 0 )
        __kmp_dump_queuing_lock( this_thr, gtid, lck, *head_id_p, *tail_id_p );
#endif
    KMP_DEBUG_ASSERT( !this_thr->th.th_spin_here );
    KMP_DEBUG_ASSERT( this_thr->th.th_next_waiting == 0 );

    while( 1 ) {
        kmp_int32 dequeued;
        kmp_int32 head;
        kmp_int32 tail;

        head = *head_id_p;

#ifdef DEBUG_QUEUING_LOCKS
        tail = *tail_id_p;
        TRACE_LOCK_HT( gtid+1, "rel read: ", head, tail );
        if ( head == 0 ) __kmp_dump_queuing_lock( this_thr, gtid, lck, head, tail );
#endif
        KMP_DEBUG_ASSERT( head != 0 ); /* holding the lock, head must be -1 or queue head */

        if ( head == -1 ) { /* nobody on queue */

            /* try (-1,0)->(0,0) */
            if ( KMP_COMPARE_AND_STORE_REL32( head_id_p, -1, 0 ) ) {
                KA_TRACE( 1000, ("__kmp_release_queuing_lock: lck:%p, T#%d exiting: queue empty\n",
                          lck, gtid ));
#ifdef DEBUG_QUEUING_LOCKS
                TRACE_LOCK_HT( gtid+1, "rel exit: ", 0, 0 );
#endif
                return;
            }
            dequeued = FALSE;

        }
        else {

            tail = *tail_id_p;
            if ( head == tail ) {  /* only one thread on the queue */

#ifdef DEBUG_QUEUING_LOCKS
                if ( head <= 0 ) __kmp_dump_queuing_lock( this_thr, gtid, lck, head, tail );
#endif
                KMP_DEBUG_ASSERT( head > 0 );

                /* try (h,h)->(-1,0) */
                dequeued = KMP_COMPARE_AND_STORE_REL64( (kmp_int64 *) tail_id_p,
                  KMP_PACK_64( head, head ), KMP_PACK_64( -1, 0 ) );
#ifdef DEBUG_QUEUING_LOCKS
                TRACE_LOCK( gtid+1, "rel deq: (h,h)->(-1,0)" );
#endif

            }
            else {
                volatile kmp_int32 *waiting_id_p;
                kmp_info_t         *head_thr = __kmp_thread_from_gtid( head - 1 );
                KMP_DEBUG_ASSERT( head_thr != NULL );
                waiting_id_p = & head_thr->th.th_next_waiting;

                /* Does this require synchronous reads? */
#ifdef DEBUG_QUEUING_LOCKS
                if ( head <= 0 || tail <= 0 ) __kmp_dump_queuing_lock( this_thr, gtid, lck, head, tail );
#endif
                KMP_DEBUG_ASSERT( head > 0 && tail > 0 );

                /* try (h,t)->(h',t) or (t,t) */

                KMP_MB();
                /* make sure enqueuing thread has time to update next waiting thread field */
                *head_id_p = (kmp_int32) KMP_WAIT_YIELD((volatile kmp_uint*) waiting_id_p, 0, KMP_NEQ, NULL);
#ifdef DEBUG_QUEUING_LOCKS
                TRACE_LOCK( gtid+1, "rel deq: (h,t)->(h',t)" );
#endif
                dequeued = TRUE;
            }
        }

        if ( dequeued ) {
            kmp_info_t *head_thr = __kmp_thread_from_gtid( head - 1 );
            KMP_DEBUG_ASSERT( head_thr != NULL );

            /* Does this require synchronous reads? */
#ifdef DEBUG_QUEUING_LOCKS
            if ( head <= 0 || tail <= 0 ) __kmp_dump_queuing_lock( this_thr, gtid, lck, head, tail );
#endif
            KMP_DEBUG_ASSERT( head > 0 && tail > 0 );

            /* For clean code only.
             * Thread not released until next statement prevents race with acquire code.
             */
            head_thr->th.th_next_waiting = 0;
#ifdef DEBUG_QUEUING_LOCKS
            TRACE_LOCK_T( gtid+1, "rel nw=0 for t=", head );
#endif

            KMP_MB();
            /* reset spin value */
            head_thr->th.th_spin_here = FALSE;

            KA_TRACE( 1000, ("__kmp_release_queuing_lock: lck:%p, T#%d exiting: after dequeuing\n",
                      lck, gtid ));
#ifdef DEBUG_QUEUING_LOCKS
            TRACE_LOCK( gtid+1, "rel exit 2" );
#endif
            return;
        }
        /* KMP_CPU_PAUSE( );  don't want to make releasing thread hold up acquiring threads */

#ifdef DEBUG_QUEUING_LOCKS
        TRACE_LOCK( gtid+1, "rel retry" );
#endif

    } /* while */
    KMP_ASSERT2( 0, "should not get here" );
}

static void
__kmp_release_queuing_lock_with_checks( kmp_queuing_lock_t *lck,
  kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_unset_lock";
        KMP_MB();  /* in case another processor initialized lock */
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( __kmp_is_queuing_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
        if ( __kmp_get_queuing_lock_owner( lck ) == -1 ) {
            KMP_FATAL( LockUnsettingFree, func );
        }
        if ( __kmp_get_queuing_lock_owner( lck ) != gtid ) {
            KMP_FATAL( LockUnsettingSetByAnother, func );
        }
        lck->lk.owner_id = 0;
    }
    __kmp_release_queuing_lock( lck, gtid );
}

void
__kmp_init_queuing_lock( kmp_queuing_lock_t *lck )
{
    lck->lk.location = NULL;
    lck->lk.head_id = 0;
    lck->lk.tail_id = 0;
    lck->lk.next_ticket = 0;
    lck->lk.now_serving = 0;
    lck->lk.owner_id = 0;      // no thread owns the lock.
    lck->lk.depth_locked = -1; // >= 0 for nestable locks, -1 for simple locks.
    lck->lk.initialized = lck;

    KA_TRACE(1000, ("__kmp_init_queuing_lock: lock %p initialized\n", lck));
}

static void
__kmp_init_queuing_lock_with_checks( kmp_queuing_lock_t * lck )
{
    __kmp_init_queuing_lock( lck );
}

void
__kmp_destroy_queuing_lock( kmp_queuing_lock_t *lck )
{
    lck->lk.initialized = NULL;
    lck->lk.location = NULL;
    lck->lk.head_id = 0;
    lck->lk.tail_id = 0;
    lck->lk.next_ticket = 0;
    lck->lk.now_serving = 0;
    lck->lk.owner_id = 0;
    lck->lk.depth_locked = -1;
}

static void
__kmp_destroy_queuing_lock_with_checks( kmp_queuing_lock_t *lck )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_destroy_lock";
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( __kmp_is_queuing_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
        if ( __kmp_get_queuing_lock_owner( lck ) != -1 ) {
            KMP_FATAL( LockStillOwned, func );
        }
    }
    __kmp_destroy_queuing_lock( lck );
}


//
// nested queuing locks
//

void
__kmp_acquire_nested_queuing_lock( kmp_queuing_lock_t *lck, kmp_int32 gtid )
{
    KMP_DEBUG_ASSERT( gtid >= 0 );

    if ( __kmp_get_queuing_lock_owner( lck ) == gtid ) {
        lck->lk.depth_locked += 1;
    }
    else {
        __kmp_acquire_queuing_lock_timed_template<false>( lck, gtid );
        KMP_MB();
        lck->lk.depth_locked = 1;
        KMP_MB();
        lck->lk.owner_id = gtid + 1;
    }
}

static void
__kmp_acquire_nested_queuing_lock_with_checks( kmp_queuing_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_set_nest_lock";
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( ! __kmp_is_queuing_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
    }
    __kmp_acquire_nested_queuing_lock( lck, gtid );
}

int
__kmp_test_nested_queuing_lock( kmp_queuing_lock_t *lck, kmp_int32 gtid )
{
    int retval;

    KMP_DEBUG_ASSERT( gtid >= 0 );

    if ( __kmp_get_queuing_lock_owner( lck ) == gtid ) {
        retval = ++lck->lk.depth_locked;
    }
    else if ( !__kmp_test_queuing_lock( lck, gtid ) ) {
        retval = 0;
    }
    else {
        KMP_MB();
        retval = lck->lk.depth_locked = 1;
        KMP_MB();
        lck->lk.owner_id = gtid + 1;
    }
    return retval;
}

static int
__kmp_test_nested_queuing_lock_with_checks( kmp_queuing_lock_t *lck,
  kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_test_nest_lock";
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( ! __kmp_is_queuing_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
    }
    return __kmp_test_nested_queuing_lock( lck, gtid );
}

void
__kmp_release_nested_queuing_lock( kmp_queuing_lock_t *lck, kmp_int32 gtid )
{
    KMP_DEBUG_ASSERT( gtid >= 0 );

    KMP_MB();
    if ( --(lck->lk.depth_locked) == 0 ) {
        KMP_MB();
        lck->lk.owner_id = 0;
        __kmp_release_queuing_lock( lck, gtid );
    }
}

static void
__kmp_release_nested_queuing_lock_with_checks( kmp_queuing_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_unset_nest_lock";
        KMP_MB();  /* in case another processor initialized lock */
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( ! __kmp_is_queuing_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
        if ( __kmp_get_queuing_lock_owner( lck ) == -1 ) {
            KMP_FATAL( LockUnsettingFree, func );
        }
        if ( __kmp_get_queuing_lock_owner( lck ) != gtid ) {
            KMP_FATAL( LockUnsettingSetByAnother, func );
        }
    }
    __kmp_release_nested_queuing_lock( lck, gtid );
}

void
__kmp_init_nested_queuing_lock( kmp_queuing_lock_t * lck )
{
    __kmp_init_queuing_lock( lck );
    lck->lk.depth_locked = 0; // >= 0 for nestable locks, -1 for simple locks
}

static void
__kmp_init_nested_queuing_lock_with_checks( kmp_queuing_lock_t * lck )
{
    __kmp_init_nested_queuing_lock( lck );
}

void
__kmp_destroy_nested_queuing_lock( kmp_queuing_lock_t *lck )
{
    __kmp_destroy_queuing_lock( lck );
    lck->lk.depth_locked = 0;
}

static void
__kmp_destroy_nested_queuing_lock_with_checks( kmp_queuing_lock_t *lck )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_destroy_nest_lock";
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( ! __kmp_is_queuing_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
        if ( __kmp_get_queuing_lock_owner( lck ) != -1 ) {
            KMP_FATAL( LockStillOwned, func );
        }
    }
    __kmp_destroy_nested_queuing_lock( lck );
}


//
// access functions to fields which don't exist for all lock kinds.
//

static int
__kmp_is_queuing_lock_initialized( kmp_queuing_lock_t *lck )
{
    return lck == lck->lk.initialized;
}

static const ident_t *
__kmp_get_queuing_lock_location( kmp_queuing_lock_t *lck )
{
    return lck->lk.location;
}

static void
__kmp_set_queuing_lock_location( kmp_queuing_lock_t *lck, const ident_t *loc )
{
    lck->lk.location = loc;
}

static kmp_lock_flags_t
__kmp_get_queuing_lock_flags( kmp_queuing_lock_t *lck )
{
    return lck->lk.flags;
}

static void
__kmp_set_queuing_lock_flags( kmp_queuing_lock_t *lck, kmp_lock_flags_t flags )
{
    lck->lk.flags = flags;
}

/* ------------------------------------------------------------------------ */
/* DRDPA ticket locks                                                */
/* "DRDPA" means Dynamically Reconfigurable Distributed Polling Area */

static kmp_int32
__kmp_get_drdpa_lock_owner( kmp_drdpa_lock_t *lck )
{
    return TCR_4( lck->lk.owner_id ) - 1;
}

static inline bool
__kmp_is_drdpa_lock_nestable( kmp_drdpa_lock_t *lck )
{
    return lck->lk.depth_locked != -1;
}

__forceinline static void
__kmp_acquire_drdpa_lock_timed_template( kmp_drdpa_lock_t *lck, kmp_int32 gtid )
{
    kmp_uint64 ticket = KMP_TEST_THEN_INC64((kmp_int64 *)&lck->lk.next_ticket);
    kmp_uint64 mask = TCR_8(lck->lk.mask);              // volatile load
    volatile struct kmp_base_drdpa_lock::kmp_lock_poll *polls
      = (volatile struct kmp_base_drdpa_lock::kmp_lock_poll *)
      TCR_PTR(lck->lk.polls);                           // volatile load

#ifdef USE_LOCK_PROFILE
    if (TCR_8(polls[ticket & mask].poll) != ticket)
        __kmp_printf("LOCK CONTENTION: %p\n", lck);
    /* else __kmp_printf( "." );*/
#endif /* USE_LOCK_PROFILE */

    //
    // Now spin-wait, but reload the polls pointer and mask, in case the
    // polling area has been reconfigured.  Unless it is reconfigured, the
    // reloads stay in L1 cache and are cheap.
    //
    // Keep this code in sync with KMP_WAIT_YIELD, in kmp_dispatch.c !!!
    //
    // The current implementation of KMP_WAIT_YIELD doesn't allow for mask
    // and poll to be re-read every spin iteration.
    //
    kmp_uint32 spins;
    KMP_INIT_YIELD(spins);
    while (TCR_8(polls[ticket & mask]).poll < ticket) { // volatile load
        __kmp_static_delay(TRUE);

        //
        // If we are oversubscribed,
        // or ahve waited a bit (and KMP_LIBRARY=turnaround), then yield.
        // CPU Pause is in the macros for yield.
        //
        KMP_YIELD(TCR_4(__kmp_nth)
          > (__kmp_avail_proc ? __kmp_avail_proc : __kmp_xproc));
        KMP_YIELD_SPIN(spins);

        // Re-read the mask and the poll pointer from the lock structure.
        //
        // Make certain that "mask" is read before "polls" !!!
        //
        // If another thread picks reconfigures the polling area and updates
        // their values, and we get the new value of mask and the old polls
        // pointer, we could access memory beyond the end of the old polling
        // area.
        //
        mask = TCR_8(lck->lk.mask);                     // volatile load
        polls = (volatile struct kmp_base_drdpa_lock::kmp_lock_poll *)
          TCR_PTR(lck->lk.polls);                       // volatile load
    }

    //
    // Critical section starts here
    //
    KA_TRACE(1000, ("__kmp_acquire_drdpa_lock: ticket #%lld acquired lock %p\n",
      ticket, lck));
    lck->lk.now_serving = ticket;                       // non-volatile store

    //
    // Deallocate a garbage polling area if we know that we are the last
    // thread that could possibly access it.
    //
    // The >= check is in case __kmp_test_drdpa_lock() allocated the cleanup
    // ticket.
    //
    if ((lck->lk.old_polls != NULL) && (ticket >= lck->lk.cleanup_ticket)) {
        __kmp_free((void *)lck->lk.old_polls);
        lck->lk.old_polls = NULL;
        lck->lk.cleanup_ticket = 0;
    }

    //
    // Check to see if we should reconfigure the polling area.
    // If there is still a garbage polling area to be deallocated from a
    // previous reconfiguration, let a later thread reconfigure it.
    //
    if (lck->lk.old_polls == NULL) {
        bool reconfigure = false;
        volatile struct kmp_base_drdpa_lock::kmp_lock_poll *old_polls = polls;
        kmp_uint32 num_polls = TCR_4(lck->lk.num_polls);

        if (TCR_4(__kmp_nth)
          > (__kmp_avail_proc ? __kmp_avail_proc : __kmp_xproc)) {
            //
            // We are in oversubscription mode.  Contract the polling area
            // down to a single location, if that hasn't been done already.
            //
            if (num_polls > 1) {
                reconfigure = true;
                num_polls = TCR_4(lck->lk.num_polls);
                mask = 0;
                num_polls = 1;
                polls = (volatile struct kmp_base_drdpa_lock::kmp_lock_poll *)
                  __kmp_allocate(num_polls * sizeof(*polls));
                polls[0].poll = ticket;
            }
        }
        else {
            //
            // We are in under/fully subscribed mode.  Check the number of
            // threads waiting on the lock.  The size of the polling area
            // should be at least the number of threads waiting.
            //
            kmp_uint64 num_waiting = TCR_8(lck->lk.next_ticket) - ticket - 1;
            if (num_waiting > num_polls) {
                kmp_uint32 old_num_polls = num_polls;
                reconfigure = true;
                do {
                    mask = (mask << 1) | 1;
                    num_polls *= 2;
                } while (num_polls <= num_waiting);

                //
                // Allocate the new polling area, and copy the relevant portion
                // of the old polling area to the new area.  __kmp_allocate()
                // zeroes the memory it allocates, and most of the old area is
                // just zero padding, so we only copy the release counters.
                //
                polls = (volatile struct kmp_base_drdpa_lock::kmp_lock_poll *)
                  __kmp_allocate(num_polls * sizeof(*polls));
                int i;
                for (i = 0; i < old_num_polls; i++) {
                    polls[i].poll = old_polls[i].poll;
                }
            }
        }

        if (reconfigure) {
            //
            // Now write the updated fields back to the lock structure.
            //
            // Make certain that "polls" is written before "mask" !!!
            //
            // If another thread picks up the new value of mask and the old
            // polls pointer , it could access memory beyond the end of the
            // old polling area.
            //
            // On x86, we need memory fences.
            //
            KA_TRACE(1000, ("__kmp_acquire_drdpa_lock: ticket #%lld reconfiguring lock %p to %d polls\n",
              ticket, lck, num_polls));

            lck->lk.old_polls = old_polls;              // non-volatile store
            lck->lk.polls = polls;                      // volatile store

            KMP_MB();

            lck->lk.num_polls = num_polls;              // non-volatile store
            lck->lk.mask = mask;                        // volatile store

            KMP_MB();

            //
            // Only after the new polling area and mask have been flushed
            // to main memory can we update the cleanup ticket field.
            //
            // volatile load / non-volatile store
            //
            lck->lk.cleanup_ticket = TCR_8(lck->lk.next_ticket);
        }
    }
}

void
__kmp_acquire_drdpa_lock( kmp_drdpa_lock_t *lck, kmp_int32 gtid )
{
    __kmp_acquire_drdpa_lock_timed_template( lck, gtid );
}

static void
__kmp_acquire_drdpa_lock_with_checks( kmp_drdpa_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_set_lock";
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( __kmp_is_drdpa_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
        if ( ( gtid >= 0 ) && ( __kmp_get_drdpa_lock_owner( lck ) == gtid ) ) {
            KMP_FATAL( LockIsAlreadyOwned, func );
        }
    }

    __kmp_acquire_drdpa_lock( lck, gtid );

    if ( __kmp_env_consistency_check ) {
        lck->lk.owner_id = gtid + 1;
    }
}

int
__kmp_test_drdpa_lock( kmp_drdpa_lock_t *lck, kmp_int32 gtid )
{
    //
    // First get a ticket, then read the polls pointer and the mask.
    // The polls pointer must be read before the mask!!! (See above)
    //
    kmp_uint64 ticket = TCR_8(lck->lk.next_ticket);     // volatile load
    volatile struct kmp_base_drdpa_lock::kmp_lock_poll *polls
      = (volatile struct kmp_base_drdpa_lock::kmp_lock_poll *)
      TCR_PTR(lck->lk.polls);                           // volatile load
    kmp_uint64 mask = TCR_8(lck->lk.mask);              // volatile load
    if (TCR_8(polls[ticket & mask].poll) == ticket) {
        kmp_uint64 next_ticket = ticket + 1;
        if (KMP_COMPARE_AND_STORE_ACQ64((kmp_int64 *)&lck->lk.next_ticket,
          ticket, next_ticket)) {
            KA_TRACE(1000, ("__kmp_test_drdpa_lock: ticket #%lld acquired lock %p\n",
               ticket, lck));
            lck->lk.now_serving = ticket;               // non-volatile store

            //
            // Since no threads are waiting, there is no possiblity that
            // we would want to reconfigure the polling area.  We might
            // have the cleanup ticket value (which says that it is now
            // safe to deallocate old_polls), but we'll let a later thread
            // which calls __kmp_acquire_lock do that - this routine
            // isn't supposed to block, and we would risk blocks if we
            // called __kmp_free() to do the deallocation.
            //
            return TRUE;
        }
    }
    return FALSE;
}

static int
__kmp_test_drdpa_lock_with_checks( kmp_drdpa_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_test_lock";
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( __kmp_is_drdpa_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
    }

    int retval = __kmp_test_drdpa_lock( lck, gtid );

    if ( __kmp_env_consistency_check && retval ) {
        lck->lk.owner_id = gtid + 1;
    }
    return retval;
}

void
__kmp_release_drdpa_lock( kmp_drdpa_lock_t *lck, kmp_int32 gtid )
{
    //
    // Read the ticket value from the lock data struct, then the polls
    // pointer and the mask.  The polls pointer must be read before the
    // mask!!! (See above)
    //
    kmp_uint64 ticket = lck->lk.now_serving + 1;        // non-volatile load
    volatile struct kmp_base_drdpa_lock::kmp_lock_poll *polls
      = (volatile struct kmp_base_drdpa_lock::kmp_lock_poll *)
      TCR_PTR(lck->lk.polls);                           // volatile load
    kmp_uint64 mask = TCR_8(lck->lk.mask);              // volatile load
    KA_TRACE(1000, ("__kmp_release_drdpa_lock: ticket #%lld released lock %p\n",
       ticket - 1, lck));
    TCW_8(polls[ticket & mask].poll, ticket);           // volatile store
}

static void
__kmp_release_drdpa_lock_with_checks( kmp_drdpa_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_unset_lock";
        KMP_MB();  /* in case another processor initialized lock */
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( __kmp_is_drdpa_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
        if ( __kmp_get_drdpa_lock_owner( lck ) == -1 ) {
            KMP_FATAL( LockUnsettingFree, func );
        }
        if ( ( gtid >= 0 ) && ( __kmp_get_drdpa_lock_owner( lck ) >= 0 )
          && ( __kmp_get_drdpa_lock_owner( lck ) != gtid ) ) {
            KMP_FATAL( LockUnsettingSetByAnother, func );
        }
        lck->lk.owner_id = 0;
    }
    __kmp_release_drdpa_lock( lck, gtid );
}

void
__kmp_init_drdpa_lock( kmp_drdpa_lock_t *lck )
{
    lck->lk.location = NULL;
    lck->lk.mask = 0;
    lck->lk.num_polls = 1;
    lck->lk.polls = (volatile struct kmp_base_drdpa_lock::kmp_lock_poll *)
      __kmp_allocate(lck->lk.num_polls * sizeof(*(lck->lk.polls)));
    lck->lk.cleanup_ticket = 0;
    lck->lk.old_polls = NULL;
    lck->lk.next_ticket = 0;
    lck->lk.now_serving = 0;
    lck->lk.owner_id = 0;      // no thread owns the lock.
    lck->lk.depth_locked = -1; // >= 0 for nestable locks, -1 for simple locks.
    lck->lk.initialized = lck;

    KA_TRACE(1000, ("__kmp_init_drdpa_lock: lock %p initialized\n", lck));
}

static void
__kmp_init_drdpa_lock_with_checks( kmp_drdpa_lock_t * lck )
{
    __kmp_init_drdpa_lock( lck );
}

void
__kmp_destroy_drdpa_lock( kmp_drdpa_lock_t *lck )
{
    lck->lk.initialized = NULL;
    lck->lk.location    = NULL;
    if (lck->lk.polls != NULL) {
        __kmp_free((void *)lck->lk.polls);
        lck->lk.polls = NULL;
    }
    if (lck->lk.old_polls != NULL) {
        __kmp_free((void *)lck->lk.old_polls);
        lck->lk.old_polls = NULL;
    }
    lck->lk.mask = 0;
    lck->lk.num_polls = 0;
    lck->lk.cleanup_ticket = 0;
    lck->lk.next_ticket = 0;
    lck->lk.now_serving = 0;
    lck->lk.owner_id = 0;
    lck->lk.depth_locked = -1;
}

static void
__kmp_destroy_drdpa_lock_with_checks( kmp_drdpa_lock_t *lck )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_destroy_lock";
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( __kmp_is_drdpa_lock_nestable( lck ) ) {
            KMP_FATAL( LockNestableUsedAsSimple, func );
        }
        if ( __kmp_get_drdpa_lock_owner( lck ) != -1 ) {
            KMP_FATAL( LockStillOwned, func );
        }
    }
    __kmp_destroy_drdpa_lock( lck );
}


//
// nested drdpa ticket locks
//

void
__kmp_acquire_nested_drdpa_lock( kmp_drdpa_lock_t *lck, kmp_int32 gtid )
{
    KMP_DEBUG_ASSERT( gtid >= 0 );

    if ( __kmp_get_drdpa_lock_owner( lck ) == gtid ) {
        lck->lk.depth_locked += 1;
    }
    else {
        __kmp_acquire_drdpa_lock_timed_template( lck, gtid );
        KMP_MB();
        lck->lk.depth_locked = 1;
        KMP_MB();
        lck->lk.owner_id = gtid + 1;
    }
}

static void
__kmp_acquire_nested_drdpa_lock_with_checks( kmp_drdpa_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_set_nest_lock";
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( ! __kmp_is_drdpa_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
    }
    __kmp_acquire_nested_drdpa_lock( lck, gtid );
}

int
__kmp_test_nested_drdpa_lock( kmp_drdpa_lock_t *lck, kmp_int32 gtid )
{
    int retval;

    KMP_DEBUG_ASSERT( gtid >= 0 );

    if ( __kmp_get_drdpa_lock_owner( lck ) == gtid ) {
        retval = ++lck->lk.depth_locked;
    }
    else if ( !__kmp_test_drdpa_lock( lck, gtid ) ) {
        retval = 0;
    }
    else {
        KMP_MB();
        retval = lck->lk.depth_locked = 1;
        KMP_MB();
        lck->lk.owner_id = gtid + 1;
    }
    return retval;
}

static int
__kmp_test_nested_drdpa_lock_with_checks( kmp_drdpa_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_test_nest_lock";
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( ! __kmp_is_drdpa_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
    }
    return __kmp_test_nested_drdpa_lock( lck, gtid );
}

void
__kmp_release_nested_drdpa_lock( kmp_drdpa_lock_t *lck, kmp_int32 gtid )
{
    KMP_DEBUG_ASSERT( gtid >= 0 );

    KMP_MB();
    if ( --(lck->lk.depth_locked) == 0 ) {
        KMP_MB();
        lck->lk.owner_id = 0;
        __kmp_release_drdpa_lock( lck, gtid );
    }
}

static void
__kmp_release_nested_drdpa_lock_with_checks( kmp_drdpa_lock_t *lck, kmp_int32 gtid )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_unset_nest_lock";
        KMP_MB();  /* in case another processor initialized lock */
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( ! __kmp_is_drdpa_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
        if ( __kmp_get_drdpa_lock_owner( lck ) == -1 ) {
            KMP_FATAL( LockUnsettingFree, func );
        }
        if ( __kmp_get_drdpa_lock_owner( lck ) != gtid ) {
            KMP_FATAL( LockUnsettingSetByAnother, func );
        }
    }
    __kmp_release_nested_drdpa_lock( lck, gtid );
}

void
__kmp_init_nested_drdpa_lock( kmp_drdpa_lock_t * lck )
{
    __kmp_init_drdpa_lock( lck );
    lck->lk.depth_locked = 0; // >= 0 for nestable locks, -1 for simple locks
}

static void
__kmp_init_nested_drdpa_lock_with_checks( kmp_drdpa_lock_t * lck )
{
    __kmp_init_nested_drdpa_lock( lck );
}

void
__kmp_destroy_nested_drdpa_lock( kmp_drdpa_lock_t *lck )
{
    __kmp_destroy_drdpa_lock( lck );
    lck->lk.depth_locked = 0;
}

static void
__kmp_destroy_nested_drdpa_lock_with_checks( kmp_drdpa_lock_t *lck )
{
    if ( __kmp_env_consistency_check ) {
        char const * const func = "omp_destroy_nest_lock";
        if ( lck->lk.initialized != lck ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
        if ( ! __kmp_is_drdpa_lock_nestable( lck ) ) {
            KMP_FATAL( LockSimpleUsedAsNestable, func );
        }
        if ( __kmp_get_drdpa_lock_owner( lck ) != -1 ) {
            KMP_FATAL( LockStillOwned, func );
        }
    }
    __kmp_destroy_nested_drdpa_lock( lck );
}


//
// access functions to fields which don't exist for all lock kinds.
//

static int
__kmp_is_drdpa_lock_initialized( kmp_drdpa_lock_t *lck )
{
    return lck == lck->lk.initialized;
}

static const ident_t *
__kmp_get_drdpa_lock_location( kmp_drdpa_lock_t *lck )
{
    return lck->lk.location;
}

static void
__kmp_set_drdpa_lock_location( kmp_drdpa_lock_t *lck, const ident_t *loc )
{
    lck->lk.location = loc;
}

static kmp_lock_flags_t
__kmp_get_drdpa_lock_flags( kmp_drdpa_lock_t *lck )
{
    return lck->lk.flags;
}

static void
__kmp_set_drdpa_lock_flags( kmp_drdpa_lock_t *lck, kmp_lock_flags_t flags )
{
    lck->lk.flags = flags;
}

/* ------------------------------------------------------------------------ */
/* user locks
 *
 * They are implemented as a table of function pointers which are set to the
 * lock functions of the appropriate kind, once that has been determined.
 */

enum kmp_lock_kind __kmp_user_lock_kind = lk_default;

size_t __kmp_base_user_lock_size = 0;
size_t __kmp_user_lock_size = 0;

kmp_int32 ( *__kmp_get_user_lock_owner_ )( kmp_user_lock_p lck ) = NULL;
void ( *__kmp_acquire_user_lock_with_checks_ )( kmp_user_lock_p lck, kmp_int32 gtid ) = NULL;

int ( *__kmp_test_user_lock_with_checks_ )( kmp_user_lock_p lck, kmp_int32 gtid ) = NULL;
void ( *__kmp_release_user_lock_with_checks_ )( kmp_user_lock_p lck, kmp_int32 gtid ) = NULL;
void ( *__kmp_init_user_lock_with_checks_ )( kmp_user_lock_p lck ) = NULL;
void ( *__kmp_destroy_user_lock_ )( kmp_user_lock_p lck ) = NULL;
void ( *__kmp_destroy_user_lock_with_checks_ )( kmp_user_lock_p lck ) = NULL;
void ( *__kmp_acquire_nested_user_lock_with_checks_ )( kmp_user_lock_p lck, kmp_int32 gtid ) = NULL;

int ( *__kmp_test_nested_user_lock_with_checks_ )( kmp_user_lock_p lck, kmp_int32 gtid ) = NULL;
void ( *__kmp_release_nested_user_lock_with_checks_ )( kmp_user_lock_p lck, kmp_int32 gtid ) = NULL;
void ( *__kmp_init_nested_user_lock_with_checks_ )( kmp_user_lock_p lck ) = NULL;
void ( *__kmp_destroy_nested_user_lock_with_checks_ )( kmp_user_lock_p lck ) = NULL;

int ( *__kmp_is_user_lock_initialized_ )( kmp_user_lock_p lck ) = NULL;
const ident_t * ( *__kmp_get_user_lock_location_ )( kmp_user_lock_p lck ) = NULL;
void ( *__kmp_set_user_lock_location_ )( kmp_user_lock_p lck, const ident_t *loc ) = NULL;
kmp_lock_flags_t ( *__kmp_get_user_lock_flags_ )( kmp_user_lock_p lck ) = NULL;
void ( *__kmp_set_user_lock_flags_ )( kmp_user_lock_p lck, kmp_lock_flags_t flags ) = NULL;

void __kmp_set_user_lock_vptrs( kmp_lock_kind_t user_lock_kind )
{
    switch ( user_lock_kind ) {
        case lk_default:
        default:
        KMP_ASSERT( 0 );

        case lk_tas: {
            __kmp_base_user_lock_size = sizeof( kmp_base_tas_lock_t );
            __kmp_user_lock_size = sizeof( kmp_tas_lock_t );

            __kmp_get_user_lock_owner_ =
              ( kmp_int32 ( * )( kmp_user_lock_p ) )
              ( &__kmp_get_tas_lock_owner );

            __kmp_acquire_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_acquire_tas_lock_with_checks );

            __kmp_test_user_lock_with_checks_ =
              ( int  ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_test_tas_lock_with_checks );

            __kmp_release_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_release_tas_lock_with_checks );

            __kmp_init_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_init_tas_lock_with_checks );

            __kmp_destroy_user_lock_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_destroy_tas_lock );

            __kmp_destroy_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_destroy_tas_lock_with_checks );

            __kmp_acquire_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_acquire_nested_tas_lock_with_checks );

            __kmp_test_nested_user_lock_with_checks_ =
              ( int  ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_test_nested_tas_lock_with_checks );

            __kmp_release_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_release_nested_tas_lock_with_checks );

            __kmp_init_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_init_nested_tas_lock_with_checks );

            __kmp_destroy_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_destroy_nested_tas_lock_with_checks );

             __kmp_is_user_lock_initialized_ =
               ( int ( * )( kmp_user_lock_p ) ) NULL;

             __kmp_get_user_lock_location_ =
               ( const ident_t * ( * )( kmp_user_lock_p ) ) NULL;

             __kmp_set_user_lock_location_ =
               ( void ( * )( kmp_user_lock_p, const ident_t * ) ) NULL;

             __kmp_get_user_lock_flags_ =
               ( kmp_lock_flags_t ( * )( kmp_user_lock_p ) ) NULL;

             __kmp_set_user_lock_flags_ =
               ( void ( * )( kmp_user_lock_p, kmp_lock_flags_t ) ) NULL;
        }
        break;

#if KMP_OS_LINUX && (KMP_ARCH_X86 || KMP_ARCH_X86_64)

        case lk_futex: {
            __kmp_base_user_lock_size = sizeof( kmp_base_futex_lock_t );
            __kmp_user_lock_size = sizeof( kmp_futex_lock_t );

            __kmp_get_user_lock_owner_ =
              ( kmp_int32 ( * )( kmp_user_lock_p ) )
              ( &__kmp_get_futex_lock_owner );

            __kmp_acquire_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_acquire_futex_lock_with_checks );

            __kmp_test_user_lock_with_checks_ =
              ( int  ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_test_futex_lock_with_checks );

            __kmp_release_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_release_futex_lock_with_checks );

            __kmp_init_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_init_futex_lock_with_checks );

            __kmp_destroy_user_lock_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_destroy_futex_lock );

            __kmp_destroy_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_destroy_futex_lock_with_checks );

            __kmp_acquire_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_acquire_nested_futex_lock_with_checks );

            __kmp_test_nested_user_lock_with_checks_ =
              ( int  ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_test_nested_futex_lock_with_checks );

            __kmp_release_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_release_nested_futex_lock_with_checks );

            __kmp_init_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_init_nested_futex_lock_with_checks );

            __kmp_destroy_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_destroy_nested_futex_lock_with_checks );

             __kmp_is_user_lock_initialized_ =
               ( int ( * )( kmp_user_lock_p ) ) NULL;

             __kmp_get_user_lock_location_ =
               ( const ident_t * ( * )( kmp_user_lock_p ) ) NULL;

             __kmp_set_user_lock_location_ =
               ( void ( * )( kmp_user_lock_p, const ident_t * ) ) NULL;

             __kmp_get_user_lock_flags_ =
               ( kmp_lock_flags_t ( * )( kmp_user_lock_p ) ) NULL;

             __kmp_set_user_lock_flags_ =
               ( void ( * )( kmp_user_lock_p, kmp_lock_flags_t ) ) NULL;
        }
        break;

#endif // KMP_OS_LINUX && (KMP_ARCH_X86 || KMP_ARCH_X86_64)

        case lk_ticket: {
            __kmp_base_user_lock_size = sizeof( kmp_base_ticket_lock_t );
            __kmp_user_lock_size = sizeof( kmp_ticket_lock_t );

            __kmp_get_user_lock_owner_ =
              ( kmp_int32 ( * )( kmp_user_lock_p ) )
              ( &__kmp_get_ticket_lock_owner );

            __kmp_acquire_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_acquire_ticket_lock_with_checks );

            __kmp_test_user_lock_with_checks_ =
              ( int  ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_test_ticket_lock_with_checks );

            __kmp_release_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_release_ticket_lock_with_checks );

            __kmp_init_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_init_ticket_lock_with_checks );

            __kmp_destroy_user_lock_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_destroy_ticket_lock );

            __kmp_destroy_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_destroy_ticket_lock_with_checks );

            __kmp_acquire_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_acquire_nested_ticket_lock_with_checks );

            __kmp_test_nested_user_lock_with_checks_ =
              ( int  ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_test_nested_ticket_lock_with_checks );

            __kmp_release_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_release_nested_ticket_lock_with_checks );

            __kmp_init_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_init_nested_ticket_lock_with_checks );

            __kmp_destroy_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_destroy_nested_ticket_lock_with_checks );

             __kmp_is_user_lock_initialized_ =
               ( int ( * )( kmp_user_lock_p ) )
               ( &__kmp_is_ticket_lock_initialized );

             __kmp_get_user_lock_location_ =
               ( const ident_t * ( * )( kmp_user_lock_p ) )
               ( &__kmp_get_ticket_lock_location );

             __kmp_set_user_lock_location_ =
               ( void ( * )( kmp_user_lock_p, const ident_t * ) )
               ( &__kmp_set_ticket_lock_location );

             __kmp_get_user_lock_flags_ =
               ( kmp_lock_flags_t ( * )( kmp_user_lock_p ) )
               ( &__kmp_get_ticket_lock_flags );

             __kmp_set_user_lock_flags_ =
               ( void ( * )( kmp_user_lock_p, kmp_lock_flags_t ) )
               ( &__kmp_set_ticket_lock_flags );
        }
        break;

        case lk_queuing: {
            __kmp_base_user_lock_size = sizeof( kmp_base_queuing_lock_t );
            __kmp_user_lock_size = sizeof( kmp_queuing_lock_t );

            __kmp_get_user_lock_owner_ =
              ( kmp_int32 ( * )( kmp_user_lock_p ) )
              ( &__kmp_get_queuing_lock_owner );

            __kmp_acquire_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_acquire_queuing_lock_with_checks );

            __kmp_test_user_lock_with_checks_ =
              ( int  ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_test_queuing_lock_with_checks );

            __kmp_release_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_release_queuing_lock_with_checks );

            __kmp_init_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_init_queuing_lock_with_checks );

            __kmp_destroy_user_lock_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_destroy_queuing_lock );

            __kmp_destroy_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_destroy_queuing_lock_with_checks );

            __kmp_acquire_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_acquire_nested_queuing_lock_with_checks );

            __kmp_test_nested_user_lock_with_checks_ =
              ( int  ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_test_nested_queuing_lock_with_checks );

            __kmp_release_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_release_nested_queuing_lock_with_checks );

            __kmp_init_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_init_nested_queuing_lock_with_checks );

            __kmp_destroy_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_destroy_nested_queuing_lock_with_checks );

             __kmp_is_user_lock_initialized_ =
               ( int ( * )( kmp_user_lock_p ) )
               ( &__kmp_is_queuing_lock_initialized );

             __kmp_get_user_lock_location_ =
               ( const ident_t * ( * )( kmp_user_lock_p ) )
               ( &__kmp_get_queuing_lock_location );

             __kmp_set_user_lock_location_ =
               ( void ( * )( kmp_user_lock_p, const ident_t * ) )
               ( &__kmp_set_queuing_lock_location );

             __kmp_get_user_lock_flags_ =
               ( kmp_lock_flags_t ( * )( kmp_user_lock_p ) )
               ( &__kmp_get_queuing_lock_flags );

             __kmp_set_user_lock_flags_ =
               ( void ( * )( kmp_user_lock_p, kmp_lock_flags_t ) )
               ( &__kmp_set_queuing_lock_flags );
        }
        break;

        case lk_drdpa: {
            __kmp_base_user_lock_size = sizeof( kmp_base_drdpa_lock_t );
            __kmp_user_lock_size = sizeof( kmp_drdpa_lock_t );

            __kmp_get_user_lock_owner_ =
              ( kmp_int32 ( * )( kmp_user_lock_p ) )
              ( &__kmp_get_drdpa_lock_owner );

            __kmp_acquire_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_acquire_drdpa_lock_with_checks );

            __kmp_test_user_lock_with_checks_ =
              ( int  ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_test_drdpa_lock_with_checks );

            __kmp_release_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_release_drdpa_lock_with_checks );

            __kmp_init_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_init_drdpa_lock_with_checks );

            __kmp_destroy_user_lock_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_destroy_drdpa_lock );

            __kmp_destroy_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_destroy_drdpa_lock_with_checks );

            __kmp_acquire_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_acquire_nested_drdpa_lock_with_checks );

            __kmp_test_nested_user_lock_with_checks_ =
              ( int  ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_test_nested_drdpa_lock_with_checks );

            __kmp_release_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p, kmp_int32 ) )
              ( &__kmp_release_nested_drdpa_lock_with_checks );

            __kmp_init_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_init_nested_drdpa_lock_with_checks );

            __kmp_destroy_nested_user_lock_with_checks_ =
              ( void ( * )( kmp_user_lock_p ) )
              ( &__kmp_destroy_nested_drdpa_lock_with_checks );

             __kmp_is_user_lock_initialized_ =
               ( int ( * )( kmp_user_lock_p ) )
               ( &__kmp_is_drdpa_lock_initialized );

             __kmp_get_user_lock_location_ =
               ( const ident_t * ( * )( kmp_user_lock_p ) )
               ( &__kmp_get_drdpa_lock_location );

             __kmp_set_user_lock_location_ =
               ( void ( * )( kmp_user_lock_p, const ident_t * ) )
               ( &__kmp_set_drdpa_lock_location );

             __kmp_get_user_lock_flags_ =
               ( kmp_lock_flags_t ( * )( kmp_user_lock_p ) )
               ( &__kmp_get_drdpa_lock_flags );

             __kmp_set_user_lock_flags_ =
               ( void ( * )( kmp_user_lock_p, kmp_lock_flags_t ) )
               ( &__kmp_set_drdpa_lock_flags );
        }
        break;
    }
}


// ----------------------------------------------------------------------------
// User lock table & lock allocation

kmp_lock_table_t __kmp_user_lock_table = { 1, 0, NULL };
kmp_user_lock_p __kmp_lock_pool = NULL;

// Lock block-allocation support.
kmp_block_of_locks* __kmp_lock_blocks = NULL;
int __kmp_num_locks_in_block = 1;             // FIXME - tune this value

static kmp_lock_index_t
__kmp_lock_table_insert( kmp_user_lock_p lck )
{
    // Assume that kmp_global_lock is held upon entry/exit.
    kmp_lock_index_t index;
    if ( __kmp_user_lock_table.used >= __kmp_user_lock_table.allocated ) {
        kmp_lock_index_t size;
        kmp_user_lock_p *table;
        kmp_lock_index_t i;
        // Reallocate lock table.
        if ( __kmp_user_lock_table.allocated == 0 ) {
            size = 1024;
        }
        else {
            size = __kmp_user_lock_table.allocated * 2;
        }
        table = (kmp_user_lock_p *)__kmp_allocate( sizeof( kmp_user_lock_p ) * size );
        memcpy( table + 1, __kmp_user_lock_table.table + 1, sizeof( kmp_user_lock_p ) * ( __kmp_user_lock_table.used - 1 ) );
        table[ 0 ] = (kmp_user_lock_p)__kmp_user_lock_table.table;
            // We cannot free the previos table now, sinse it may be in use by other
            // threads. So save the pointer to the previous table in in the first element of the
            // new table. All the tables will be organized into a list, and could be freed when
            // library shutting down.
        __kmp_user_lock_table.table = table;
        __kmp_user_lock_table.allocated = size;
    }
    KMP_DEBUG_ASSERT( __kmp_user_lock_table.used < __kmp_user_lock_table.allocated );
    index = __kmp_user_lock_table.used;
    __kmp_user_lock_table.table[ index ] = lck;
    ++ __kmp_user_lock_table.used;
    return index;
}

static kmp_user_lock_p
__kmp_lock_block_allocate()
{
    // Assume that kmp_global_lock is held upon entry/exit.
    static int last_index = 0;
    if ( ( last_index >= __kmp_num_locks_in_block )
      || ( __kmp_lock_blocks == NULL ) ) {
        // Restart the index.
        last_index = 0;
        // Need to allocate a new block.
        KMP_DEBUG_ASSERT( __kmp_user_lock_size > 0 );
        size_t space_for_locks = __kmp_user_lock_size * __kmp_num_locks_in_block;
        char* buffer = (char*)__kmp_allocate( space_for_locks + sizeof( kmp_block_of_locks ) );
        // Set up the new block.
        kmp_block_of_locks *new_block = (kmp_block_of_locks *)(& buffer[space_for_locks]);
        new_block->next_block = __kmp_lock_blocks;
        new_block->locks = (void *)buffer;
        // Publish the new block.
        KMP_MB();
        __kmp_lock_blocks = new_block;
    }
    kmp_user_lock_p ret = (kmp_user_lock_p)(& ( ( (char *)( __kmp_lock_blocks->locks ) )
      [ last_index * __kmp_user_lock_size ] ) );
    last_index++;
    return ret;
}

//
// Get memory for a lock. It may be freshly allocated memory or reused memory
// from lock pool.
//
kmp_user_lock_p
__kmp_user_lock_allocate( void **user_lock, kmp_int32 gtid,
  kmp_lock_flags_t flags )
{
    kmp_user_lock_p lck;
    kmp_lock_index_t index;
    KMP_DEBUG_ASSERT( user_lock );

    __kmp_acquire_lock( &__kmp_global_lock, gtid );

    if ( __kmp_lock_pool == NULL ) {
        // Lock pool is empty. Allocate new memory.
        if ( __kmp_num_locks_in_block <= 1 ) { // Tune this cutoff point.
            lck = (kmp_user_lock_p) __kmp_allocate( __kmp_user_lock_size );
        }
        else {
            lck = __kmp_lock_block_allocate();
        }

        // Insert lock in the table so that it can be freed in __kmp_cleanup,
        // and debugger has info on all allocated locks.
        index = __kmp_lock_table_insert( lck );
    }
    else {
        // Pick up lock from pool.
        lck = __kmp_lock_pool;
        index = __kmp_lock_pool->pool.index;
        __kmp_lock_pool = __kmp_lock_pool->pool.next;
    }

    //
    // We could potentially differentiate between nested and regular locks
    // here, and do the lock table lookup for regular locks only.
    //
    if ( OMP_LOCK_T_SIZE < sizeof(void *) ) {
        * ( (kmp_lock_index_t *) user_lock ) = index;
    }
    else {
        * ( (kmp_user_lock_p *) user_lock ) = lck;
    }

    // mark the lock if it is critical section lock.
    __kmp_set_user_lock_flags( lck, flags );

    __kmp_release_lock( & __kmp_global_lock, gtid ); // AC: TODO: move this line upper

    return lck;
}

// Put lock's memory to pool for reusing.
void
__kmp_user_lock_free( void **user_lock, kmp_int32 gtid, kmp_user_lock_p lck )
{
    kmp_lock_pool_t * lock_pool;

    KMP_DEBUG_ASSERT( user_lock != NULL );
    KMP_DEBUG_ASSERT( lck != NULL );

    __kmp_acquire_lock( & __kmp_global_lock, gtid );

    lck->pool.next = __kmp_lock_pool;
    __kmp_lock_pool = lck;
    if ( OMP_LOCK_T_SIZE < sizeof(void *) ) {
        kmp_lock_index_t index = * ( (kmp_lock_index_t *) user_lock );
        KMP_DEBUG_ASSERT( 0 < index && index <= __kmp_user_lock_table.used );
        lck->pool.index = index;
    }

    __kmp_release_lock( & __kmp_global_lock, gtid );
}

kmp_user_lock_p
__kmp_lookup_user_lock( void **user_lock, char const *func )
{
    kmp_user_lock_p lck = NULL;

    if ( __kmp_env_consistency_check ) {
        if ( user_lock == NULL ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
    }

    if ( OMP_LOCK_T_SIZE < sizeof(void *) ) {
        kmp_lock_index_t index = *( (kmp_lock_index_t *)user_lock );
        if ( __kmp_env_consistency_check ) {
            if ( ! ( 0 < index && index < __kmp_user_lock_table.used ) ) {
                KMP_FATAL( LockIsUninitialized, func );
            }
        }
        KMP_DEBUG_ASSERT( 0 < index && index < __kmp_user_lock_table.used );
        KMP_DEBUG_ASSERT( __kmp_user_lock_size > 0 );
        lck = __kmp_user_lock_table.table[index];
    }
    else {
        lck = *( (kmp_user_lock_p *)user_lock );
    }

    if ( __kmp_env_consistency_check ) {
        if ( lck == NULL ) {
            KMP_FATAL( LockIsUninitialized, func );
        }
    }

    return lck;
}

void
__kmp_cleanup_user_locks( void )
{
    //
    // Reset lock pool. Do not worry about lock in the pool -- we will free
    // them when iterating through lock table (it includes all the locks,
    // dead or alive).
    //
    __kmp_lock_pool = NULL;

#define IS_CRITICAL(lck) \
        ( ( __kmp_get_user_lock_flags_ != NULL ) && \
        ( ( *__kmp_get_user_lock_flags_ )( lck ) & kmp_lf_critical_section ) )

    //
    // Loop through lock table, free all locks.
    //
    // Do not free item [0], it is reserved for lock tables list.
    //
    // FIXME - we are iterating through a list of (pointers to) objects of
    // type union kmp_user_lock, but we have no way of knowing whether the
    // base type is currently "pool" or whatever the global user lock type
    // is.
    //
    // We are relying on the fact that for all of the user lock types
    // (except "tas"), the first field in the lock struct is the "initialized"
    // field, which is set to the address of the lock object itself when
    // the lock is initialized.  When the union is of type "pool", the
    // first field is a pointer to the next object in the free list, which
    // will not be the same address as the object itself.
    //
    // This means that the check ( *__kmp_is_user_lock_initialized_ )( lck )
    // will fail for "pool" objects on the free list.  This must happen as
    // the "location" field of real user locks overlaps the "index" field
    // of "pool" objects.
    //
    // It would be better to run through the free list, and remove all "pool"
    // objects from the lock table before executing this loop.  However,
    // "pool" objects do not always have their index field set (only on
    // lin_32e), and I don't want to search the lock table for the address
    // of every "pool" object on the free list.
    //
    while ( __kmp_user_lock_table.used > 1 ) {
        const ident *loc;

        //
        // reduce __kmp_user_lock_table.used before freeing the lock,
        // so that state of locks is consistent
        //
        kmp_user_lock_p lck = __kmp_user_lock_table.table[
          --__kmp_user_lock_table.used ];

        if ( ( __kmp_is_user_lock_initialized_ != NULL ) &&
          ( *__kmp_is_user_lock_initialized_ )( lck ) ) {
            //
            // Issue a warning if: KMP_CONSISTENCY_CHECK AND lock is
            // initialized AND it is NOT a critical section (user is not
            // responsible for destroying criticals) AND we know source
            // location to report.
            //
            if ( __kmp_env_consistency_check && ( ! IS_CRITICAL( lck ) ) &&
              ( ( loc = __kmp_get_user_lock_location( lck ) ) != NULL ) &&
              ( loc->psource != NULL ) ) {
                kmp_str_loc_t str_loc = __kmp_str_loc_init( loc->psource, 0 );
                KMP_WARNING( CnsLockNotDestroyed, str_loc.file, str_loc.func,
                  str_loc.line, str_loc.col );
                __kmp_str_loc_free( &str_loc);
            }

#ifdef KMP_DEBUG
            if ( IS_CRITICAL( lck ) ) {
                KA_TRACE( 20, ("__kmp_cleanup_user_locks: free critical section lock %p (%p)\n", lck, *(void**)lck ) );
            }
            else {
                KA_TRACE( 20, ("__kmp_cleanup_user_locks: free lock %p (%p)\n", lck, *(void**)lck ) );
            }
#endif // KMP_DEBUG

            //
            // Cleanup internal lock dynamic resources
            // (for drdpa locks particularly).
            //
            __kmp_destroy_user_lock( lck );
        }

        //
        // Free the lock if block allocation of locks is not used.
        //
        if ( __kmp_lock_blocks == NULL ) {
            __kmp_free( lck );
        }
    }

#undef IS_CRITICAL

    //
    // delete lock table(s).
    //
    kmp_user_lock_p *table_ptr = __kmp_user_lock_table.table;
    __kmp_user_lock_table.table = NULL;
    __kmp_user_lock_table.allocated = 0;

    while ( table_ptr != NULL ) {
        //
        // In the first element we saved the pointer to the previous
        // (smaller) lock table.
        //
        kmp_user_lock_p *next = (kmp_user_lock_p *)( table_ptr[ 0 ] );
        __kmp_free( table_ptr );
        table_ptr = next;
    }

    //
    // Free buffers allocated for blocks of locks.
    //
    kmp_block_of_locks_t *block_ptr = __kmp_lock_blocks;
    __kmp_lock_blocks = NULL;

    while ( block_ptr != NULL ) {
        kmp_block_of_locks_t *next = block_ptr->next_block;
        __kmp_free( block_ptr->locks );
        //
        // *block_ptr itself was allocated at the end of the locks vector.
        //
	block_ptr = next;
    }

    TCW_4(__kmp_init_user_locks, FALSE);
}

