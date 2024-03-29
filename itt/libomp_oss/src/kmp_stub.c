/*
 * kmp_stub.c -- stub versions of user-callable OpenMP RT functions.
 * $Revision: 42150 $
 * $Date: 2013-03-15 15:40:38 -0500 (Fri, 15 Mar 2013) $
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

#include "kmp_stub.h"

#include <stdlib.h>
#include <limits.h>
#include <errno.h>

#include "kmp_os.h"             // KMP_OS_*

#if KMP_OS_WINDOWS
    #include <windows.h>
#else
    #include <sys/time.h>
#endif

#include "kmp.h"                // KMP_DEFAULT_STKSIZE
#include "kmp_version.h"

#include "omp.h"                // Function renamings.


static double frequency = 0.0;

// Helper functions.
static size_t __kmps_init() {
    static int    initialized = 0;
    static size_t dummy = 0;
    if ( ! initialized ) {

        // TODO: Analyze KMP_VERSION environment variable, print __kmp_version_copyright and
        // __kmp_version_build_time.
        // WARNING: Do not use "fprintf( stderr, ... )" because it will cause unresolved "__iob"
        // symbol (see C70080). We need to extract __kmp_printf() stuff from kmp_runtime.c and use
        // it.

        // Trick with dummy variable forces linker to keep __kmp_version_copyright and
        // __kmp_version_build_time strings in executable file (in case of static linkage).
        // When KMP_VERSION analyze is implemented, dummy variable should be deleted, function
        // should return void.
        dummy = __kmp_version_copyright - __kmp_version_build_time;

        #if KMP_OS_WINDOWS
            LARGE_INTEGER freq;
            BOOL status = QueryPerformanceFrequency( & freq );
            if ( status ) {
                frequency = double( freq.QuadPart );
            }; // if
        #endif

        initialized = 1;
    }; // if
    return dummy;
}; // __kmps_init

#define i __kmps_init();

/* set API functions */
void omp_set_num_threads( omp_int_t num_threads ) { i; }
void omp_set_dynamic( omp_int_t dynamic )         { i; __kmps_set_dynamic( dynamic ); }
void omp_set_nested( omp_int_t nested )           { i; __kmps_set_nested( nested );   }
#if OMP_30_ENABLED
    void omp_set_max_active_levels( omp_int_t max_active_levels ) { i; }
    void omp_set_schedule( omp_sched_t kind, omp_int_t modifier ) { i; __kmps_set_schedule( (kmp_sched_t)kind, modifier ); }
    int omp_get_ancestor_thread_num( omp_int_t level ) { i; return ( level ) ? ( -1 ) : ( 0 ); }
    int omp_get_team_size( omp_int_t level ) { i; return ( level ) ? ( -1 ) : ( 1 ); }
    int kmpc_set_affinity_mask_proc( int proc, void **mask ) { i; return -1; }
    int kmpc_unset_affinity_mask_proc( int proc, void **mask ) { i; return -1; }
    int kmpc_get_affinity_mask_proc( int proc, void **mask ) { i; return -1; }
#endif // OMP_30_ENABLED

/* kmp API functions */
void kmp_set_stacksize( omp_int_t arg )   { i; __kmps_set_stacksize( arg ); }
void kmp_set_stacksize_s( size_t arg )    { i; __kmps_set_stacksize( arg ); }
void kmp_set_blocktime( omp_int_t arg )   { i; __kmps_set_blocktime( arg ); }
void kmp_set_library( omp_int_t arg )     { i; __kmps_set_library( arg ); }
void kmp_set_defaults( char const * str ) { i; }

/* KMP memory management functions. */
void * kmp_malloc( size_t size )                 { i; return malloc( size ); }
void * kmp_calloc( size_t nelem, size_t elsize ) { i; return calloc( nelem, elsize ); }
void * kmp_realloc( void *ptr, size_t size )     { i; return realloc( ptr, size ); }
void   kmp_free( void * ptr )                    { i; free( ptr ); }

static int __kmps_blocktime = INT_MAX;

void __kmps_set_blocktime( int arg ) {
    i;
    __kmps_blocktime = arg;
} // __kmps_set_blocktime

int __kmps_get_blocktime( void ) {
    i;
    return __kmps_blocktime;
} // __kmps_get_blocktime

static int __kmps_dynamic = 0;

void __kmps_set_dynamic( int arg ) {
    i;
    __kmps_dynamic = arg;
} // __kmps_set_dynamic

int __kmps_get_dynamic( void ) {
    i;
    return __kmps_dynamic;
} // __kmps_get_dynamic

static int __kmps_library = 1000;

void __kmps_set_library( int arg ) {
    i;
    __kmps_library = arg;
} // __kmps_set_library

int __kmps_get_library( void ) {
    i;
    return __kmps_library;
} // __kmps_get_library

static int __kmps_nested = 0;

void __kmps_set_nested( int arg ) {
    i;
    __kmps_nested = arg;
} // __kmps_set_nested

int __kmps_get_nested( void ) {
    i;
    return __kmps_nested;
} // __kmps_get_nested

static size_t __kmps_stacksize = KMP_DEFAULT_STKSIZE;

void __kmps_set_stacksize( int arg ) {
    i;
    __kmps_stacksize = arg;
} // __kmps_set_stacksize

int __kmps_get_stacksize( void ) {
    i;
    return __kmps_stacksize;
} // __kmps_get_stacksize

#if OMP_30_ENABLED

static kmp_sched_t __kmps_sched_kind     = kmp_sched_default;
static int         __kmps_sched_modifier = 0;

    void __kmps_set_schedule( kmp_sched_t kind, int modifier ) {
        i;
        __kmps_sched_kind     = kind;
        __kmps_sched_modifier = modifier;
    } // __kmps_set_schedule

    void __kmps_get_schedule( kmp_sched_t *kind, int *modifier ) {
        i;
        *kind     = __kmps_sched_kind;
        *modifier = __kmps_sched_modifier;
    } // __kmps_get_schedule

#endif // OMP_30_ENABLED

#if OMP_40_ENABLED

static kmp_proc_bind_t __kmps_proc_bind = proc_bind_false;

void __kmps_set_proc_bind( kmp_proc_bind_t arg ) {
    i;
    __kmps_proc_bind = arg;
} // __kmps_set_proc_bind

kmp_proc_bind_t __kmps_get_proc_bind( void ) {
    i;
    return __kmps_proc_bind;
} // __kmps_get_proc_bind

#endif /* OMP_40_ENABLED */

double __kmps_get_wtime( void ) {
    // Elapsed wall clock time (in second) from "sometime in the past".
    double wtime = 0.0;
    i;
    #if KMP_OS_WINDOWS
        if ( frequency > 0.0 ) {
            LARGE_INTEGER now;
            BOOL status = QueryPerformanceCounter( & now );
            if ( status ) {
                wtime = double( now.QuadPart ) / frequency;
            }; // if
        }; // if
    #else
        // gettimeofday() returns seconds and microseconds sinse the Epoch.
        struct timeval  tval;
        int             rc;
        rc = gettimeofday( & tval, NULL );
        if ( rc == 0 ) {
            wtime = (double)( tval.tv_sec ) + 1.0E-06 * (double)( tval.tv_usec );
        } else {
            // TODO: Assert or abort here.
        }; // if
    #endif
    return wtime;
}; // __kmps_get_wtime

double __kmps_get_wtick( void ) {
    // Number of seconds between successive clock ticks.
    double wtick = 0.0;
    i;
    #if KMP_OS_WINDOWS
        {
            DWORD increment;
            DWORD adjustment;
            BOOL  disabled;
            BOOL  rc;
            rc = GetSystemTimeAdjustment( & adjustment, & increment, & disabled );
            if ( rc ) {
                wtick = 1.0E-07 * (double)( disabled ? increment : adjustment );
            } else {
                // TODO: Assert or abort here.
                wtick = 1.0E-03;
            }; // if
        }
    #else
        // TODO: gettimeofday() returns in microseconds, but what the precision?
        wtick = 1.0E-06;
    #endif
    return wtick;
}; // __kmps_get_wtick


/*
    These functions are exported from libraries, but not declared in omp,h and omp_lib.f:

        // omalyshe: eight entries below removed from the library (2011-11-22)
        kmpc_get_banner
        kmpc_get_poolmode
        kmpc_get_poolsize
        kmpc_get_poolstat
        kmpc_poolprint
        kmpc_print_banner
        kmpc_set_poolmode
        kmpc_set_poolsize

        kmpc_set_affinity
        kmp_threadprivate_insert
        kmp_threadprivate_insert_private_data
        VT_getthid
        vtgthid

    The list is collected on lin_32.

*/

// end of file //

