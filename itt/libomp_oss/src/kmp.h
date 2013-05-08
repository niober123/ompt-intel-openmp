/*! \file */
/*
 * kmp.h -- KPTS runtime header file.
 * $Revision: 42263 $
 * $Date: 2013-04-04 11:03:19 -0500 (Thu, 04 Apr 2013) $
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

#ifndef KMP_H
#define KMP_H

/* #define BUILD_PARALLEL_ORDERED 1 */

/* This fix replaces gettimeofday with clock_gettime for better scalability on
   the Altix.  Requires user code to be linked with -lrt.
*/
//#define FIX_SGI_CLOCK

#if defined( __GNUC__ ) && !defined( __INTEL_COMPILER )
typedef __float128 _Quad;
#endif

/* Defines for OpenMP 3.0 tasking and auto scheduling */

#if OMP_30_ENABLED

# ifndef KMP_STATIC_STEAL_ENABLED
#  define KMP_STATIC_STEAL_ENABLED 1
# endif

#define TASK_CURRENT_NOT_QUEUED  0
#define TASK_CURRENT_QUEUED      1

#define TASK_DEQUE_BITS          8  // Used solely to define TASK_DEQUE_SIZE and TASK_DEQUE_MASK.
#define TASK_DEQUE_SIZE          ( 1 << TASK_DEQUE_BITS )
#define TASK_DEQUE_MASK          ( TASK_DEQUE_SIZE - 1 )

#ifdef BUILD_TIED_TASK_STACK
#define TASK_STACK_EMPTY         0  // entries when the stack is empty

#define TASK_STACK_BLOCK_BITS    5  // Used to define TASK_STACK_SIZE and TASK_STACK_MASK
#define TASK_STACK_BLOCK_SIZE    ( 1 << TASK_STACK_BLOCK_BITS ) // Number of entries in each task stack array
#define TASK_STACK_INDEX_MASK    ( TASK_STACK_BLOCK_SIZE - 1 )  // Mask for determining index into stack block
#endif // BUILD_TIED_TASK_STACK

#define TASK_NOT_PUSHED          1
#define TASK_SUCCESSFULLY_PUSHED 0
#define TASK_TIED                1
#define TASK_UNTIED              0
#define TASK_EXPLICIT            1
#define TASK_IMPLICIT            0

#endif  // OMP_30_ENABLED

#define KMP_CANCEL_THREADS
#define KMP_THREAD_ATTR

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
/*  include <ctype.h> don't use; problems with /MD on Windows* OS NT due to bad Microsoft library  */
/*  some macros provided below to replace some of these functions  */
#ifndef __ABSOFT_WIN
#include <sys/types.h>
#endif
#include <limits.h>
#include <time.h>

#include <errno.h>

#include "kmp_os.h"
#include "kmp_version.h"
#include "kmp_debug.h"
#include "kmp_lock.h"
#include "kmp_i18n.h"

#define KMP_HANDLE_SIGNALS (KMP_OS_LINUX || KMP_OS_WINDOWS || KMP_OS_DARWIN)

#ifdef KMP_SETVERSION
/*  from factory/Include, to get VERSION_STRING embedded for 'what'  */
#include "kaiconfig.h"
#include "eye.h"
#include "own.h"
#include "setversion.h"
#endif

#include "kmp_wrapper_malloc.h"
#if KMP_OS_UNIX
# include <unistd.h>
# if !defined NSIG && defined _NSIG
#  define NSIG _NSIG
# endif
#endif

#if KMP_OS_LINUX
# pragma weak clock_gettime
#endif

#if KMP_MIC1
#include <lmmintrin.h>
#endif // KMP_MIC1 AC: no lmmintrin.h in KNC compiler

/*Select data placement in NUMA memory */
#define NO_FIRST_TOUCH 0
#define FIRST_TOUCH 1       /* Exploit SGI's first touch page placement algo */

/* If not specified on compile command line, assume no first touch */
#ifndef BUILD_MEMORY
#define BUILD_MEMORY NO_FIRST_TOUCH
#endif

// 0 - no fast memory allocation, alignment: 8-byte on x86, 16-byte on x64.
// 3 - fast allocation using sync, non-sync free lists of any size, non-self free lists of limited size.
#ifndef USE_FAST_MEMORY
#define USE_FAST_MEMORY 3
#endif

// Assume using BGET compare_exchange instruction instead of lock by default.
#ifndef USE_CMP_XCHG_FOR_BGET
#define USE_CMP_XCHG_FOR_BGET 1
#endif

// Test to see if queuing lock is better than bootstrap lock for bget
// #ifndef USE_QUEUING_LOCK_FOR_BGET
// #define USE_QUEUING_LOCK_FOR_BGET
// #endif

#ifndef NSEC_PER_SEC
# define NSEC_PER_SEC 1000000000L
#endif

#ifndef USEC_PER_SEC
# define USEC_PER_SEC 1000000L
#endif

// For error messages
#define KMP_IOMP_NAME       "Intel(R) OMP"

/*!
@ingroup BASIC_TYPES
@{
*/

// FIXME DOXYGEN... need to group these flags somehow (Making them an anonymous enum would do it...)
/*!
Values for bit flags used in the ident_t to describe the fields.
*/
/*! Use trampoline for internal microtasks */
#define KMP_IDENT_IMB             0x01     
/*! Use c-style ident structure */
#define KMP_IDENT_KMPC            0x02
/* 0x04 is no longer used */
/*! Entry point generated by auto-parallelization */
#define KMP_IDENT_AUTOPAR         0x08     
/*! Compiler generates atomic reduction option for kmpc_reduce* */
#define KMP_IDENT_ATOMIC_REDUCE   0x10   
/*! To mark a 'barrier' directive in user code */ 
#define KMP_IDENT_BARRIER_EXPL    0x20  
/*! To Mark implicit barriers. */  
#define KMP_IDENT_BARRIER_IMPL           0x0040
#define KMP_IDENT_BARRIER_IMPL_MASK      0x01C0
#define KMP_IDENT_BARRIER_IMPL_FOR       0x0040
#define KMP_IDENT_BARRIER_IMPL_SECTIONS  0x00C0

#define KMP_IDENT_BARRIER_IMPL_SINGLE    0x0140
#define KMP_IDENT_BARRIER_IMPL_WORKSHARE 0x01C0

/*!
 * The ident structure that describes a source location.
 */
typedef struct ident {
    kmp_int32 reserved_1;   /**<  might be used in Fortran; see above  */
    kmp_int32 flags;        /**<  also f.flags; KMP_IDENT_xxx flags; KMP_IDENT_KMPC identifies this union member  */
    kmp_int32 reserved_2;   /**<  not really used in Fortran any more; see above */
    kmp_int32 reserved_3;   /**< source[4] in Fortran, do not use for C++  */
    char     *psource;      /**< String describing the source location. 
                            The string is composed of semi-colon separated fields which describe the source file,
                            the function and a pair of line numbers that delimit the construct.
                             */
} ident_t;
/*! 
@} 
*/

// Some forward declarations.

typedef union  kmp_team      kmp_team_t;
typedef struct kmp_taskdata  kmp_taskdata_t;
typedef union  kmp_task_team kmp_task_team_t;
typedef union  kmp_team      kmp_team_p;
typedef union  kmp_info      kmp_info_p;
typedef union  kmp_root      kmp_root_p;


#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

/* Pack two 32-bit signed integers into a 64-bit signed integer */
/* ToDo: Fix word ordering for big-endian machines. */
#define KMP_PACK_64(HIGH_32,LOW_32) \
    ( (kmp_int64) ((((kmp_uint64)(HIGH_32))<<32) | (kmp_uint64)(LOW_32)) )


/*
 * Generic string manipulation macros.
 * Assume that _x is of type char *
 */
#define SKIP_WS(_x)     { while (*(_x) == ' ' || *(_x) == '\t') (_x)++; }
#define SKIP_DIGITS(_x) { while (*(_x) >= '0' && *(_x) <= '9') (_x)++; }
#define SKIP_TO(_x,_c)  { while (*(_x) != '\0' && *(_x) != (_c)) (_x)++; }

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

/* Enumeration types */

enum kmp_state_timer {
    ts_stop,
    ts_start,
    ts_pause,

    ts_last_state
};

enum dynamic_mode {
    dynamic_default,
#ifdef USE_LOAD_BALANCE
    dynamic_load_balance,
#endif /* USE_LOAD_BALANCE */
    dynamic_random,
    dynamic_thread_limit,
    dynamic_max
};

/* external schedule constants, duplicate enum omp_sched in omp.h in order to not include it here */
#ifndef KMP_SCHED_TYPE_DEFINED
#define KMP_SCHED_TYPE_DEFINED
typedef enum kmp_sched {
    kmp_sched_lower             = 0,     // lower and upper bounds are for routine parameter check
    // Note: need to adjust __kmp_sch_map global array in case this enum is changed
    kmp_sched_static            = 1,     // mapped to kmp_sch_static_chunked           (33)
    kmp_sched_dynamic           = 2,     // mapped to kmp_sch_dynamic_chunked          (35)
    kmp_sched_guided            = 3,     // mapped to kmp_sch_guided_chunked           (36)
    kmp_sched_auto              = 4,     // mapped to kmp_sch_auto                     (38)
    kmp_sched_upper_std         = 5,     // upper bound for standard schedules
    kmp_sched_lower_ext         = 100,   // lower bound of Intel extension schedules
    kmp_sched_trapezoidal       = 101,   // mapped to kmp_sch_trapezoidal              (39)
//  kmp_sched_static_steal      = 102,   // mapped to kmp_sch_static_steal             (44)
    kmp_sched_upper             = 102,
    kmp_sched_default = kmp_sched_static // default scheduling
} kmp_sched_t;
#endif

/*!
 @ingroup WORK_SHARING
 * Describes the loop schedule to be used for a parallel for loop.
 */
enum sched_type {
    kmp_sch_lower                     = 32,   /**< lower bound for unordered values */
    kmp_sch_static_chunked            = 33,
    kmp_sch_static                    = 34,   /**< static unspecialized */
    kmp_sch_dynamic_chunked           = 35,
    kmp_sch_guided_chunked            = 36,   /**< guided unspecialized */
    kmp_sch_runtime                   = 37,
    kmp_sch_auto                      = 38,   /**< auto */
    kmp_sch_trapezoidal               = 39,

    /* accessible only through KMP_SCHEDULE environment variable */
    kmp_sch_static_greedy             = 40,
    kmp_sch_static_balanced           = 41,
    /* accessible only through KMP_SCHEDULE environment variable */
    kmp_sch_guided_iterative_chunked  = 42,
    kmp_sch_guided_analytical_chunked = 43,

    kmp_sch_static_steal              = 44,   /**< accessible only through KMP_SCHEDULE environment variable */

    /* accessible only through KMP_SCHEDULE environment variable */
    kmp_sch_upper                     = 45,   /**< upper bound for unordered values */

    kmp_ord_lower                     = 64,   /**< lower bound for ordered values, must be power of 2 */
    kmp_ord_static_chunked            = 65,
    kmp_ord_static                    = 66,   /**< ordered static unspecialized */
    kmp_ord_dynamic_chunked           = 67,
    kmp_ord_guided_chunked            = 68,
    kmp_ord_runtime                   = 69,
    kmp_ord_auto                      = 70,   /**< ordered auto */
    kmp_ord_trapezoidal               = 71,
    kmp_ord_upper                     = 72,   /**< upper bound for ordered values */

    /*
     * For the "nomerge" versions, kmp_dispatch_next*() will always return
     * a single iteration/chunk, even if the loop is serialized.  For the
     * schedule types listed above, the entire iteration vector is returned
     * if the loop is serialized.  This doesn't work for gcc/gcomp sections.
     */
    kmp_nm_lower                      = 160,  /**< lower bound for nomerge values */

    kmp_nm_static_chunked             = 161,
    kmp_nm_static                     = 162,  /**< static unspecialized */
    kmp_nm_dynamic_chunked            = 163,
    kmp_nm_guided_chunked             = 164,  /**< guided unspecialized */
    kmp_nm_runtime                    = 165,
    kmp_nm_auto                       = 166,  /**< auto */
    kmp_nm_trapezoidal                = 167,

    /* accessible only through KMP_SCHEDULE environment variable */
    kmp_nm_static_greedy              = 168,
    kmp_nm_static_balanced            = 169,
    /* accessible only through KMP_SCHEDULE environment variable */
    kmp_nm_guided_iterative_chunked   = 170,
    kmp_nm_guided_analytical_chunked  = 171,
    kmp_nm_static_steal               = 172,  /* accessible only through OMP_SCHEDULE environment variable */

    kmp_nm_ord_static_chunked         = 193,
    kmp_nm_ord_static                 = 194,  /**< ordered static unspecialized */
    kmp_nm_ord_dynamic_chunked        = 195,
    kmp_nm_ord_guided_chunked         = 196,
    kmp_nm_ord_runtime                = 197,
    kmp_nm_ord_auto                   = 198,  /**< auto */
    kmp_nm_ord_trapezoidal            = 199,
    kmp_nm_upper                      = 200,  /**< upper bound for nomerge values */

    kmp_sch_default = kmp_sch_static  /**< default scheduling algorithm */
};

/* Type to keep runtime schedule set via OMP_SCHEDULE or omp_set_schedule() */
typedef struct kmp_r_sched {
    enum sched_type r_sched_type;
    int             chunk;
} kmp_r_sched_t;

extern enum sched_type __kmp_sch_map[]; // map OMP 3.0 schedule types with our internal schedule types

enum library_type {
    library_none,
    library_serial,
    library_turnaround,
    library_throughput
};

#if KMP_OS_LINUX
enum clock_function_type {
    clock_function_gettimeofday,
    clock_function_clock_gettime
};
#endif /* KMP_OS_LINUX */

/* ------------------------------------------------------------------------ */
/* -- fast reduction stuff ------------------------------------------------ */

#undef KMP_FAST_REDUCTION_BARRIER
#define KMP_FAST_REDUCTION_BARRIER 1

#undef KMP_FAST_REDUCTION_CORE_DUO
#if KMP_ARCH_X86 || KMP_ARCH_X86_64
    #define KMP_FAST_REDUCTION_CORE_DUO 1
#endif

enum _reduction_method {
    reduction_method_not_defined = 0,
    critical_reduce_block        = ( 1 << 8 ),
    atomic_reduce_block          = ( 2 << 8 ),
    tree_reduce_block            = ( 3 << 8 ),
    empty_reduce_block           = ( 4 << 8 )
};

// description of the packed_reduction_method variable
// the packed_reduction_method variable consists of two enum types variables that are packed together into 0-th byte and 1-st byte:
// 0: ( packed_reduction_method & 0x000000FF ) is a 'enum barrier_type' value of barrier that will be used in fast reduction: bs_plain_barrier or bs_reduction_barrier
// 1: ( packed_reduction_method & 0x0000FF00 ) is a reduction method that will be used in fast reduction;
// reduction method is of 'enum _reduction_method' type and it's defined the way so that the bits of 0-th byte are empty,
// so no need to execute a shift instruction while packing/unpacking

#if KMP_FAST_REDUCTION_BARRIER
    #define PACK_REDUCTION_METHOD_AND_BARRIER(reduction_method,barrier_type) \
            ( ( reduction_method ) | ( barrier_type ) )

    #define UNPACK_REDUCTION_METHOD(packed_reduction_method) \
            ( ( enum _reduction_method )( ( packed_reduction_method ) & ( 0x0000FF00 ) ) )

    #define UNPACK_REDUCTION_BARRIER(packed_reduction_method) \
            ( ( enum barrier_type )(      ( packed_reduction_method ) & ( 0x000000FF ) ) )
#else
    #define PACK_REDUCTION_METHOD_AND_BARRIER(reduction_method,barrier_type) \
            ( reduction_method )

    #define UNPACK_REDUCTION_METHOD(packed_reduction_method) \
            ( packed_reduction_method )

    #define UNPACK_REDUCTION_BARRIER(packed_reduction_method) \
            ( bs_plain_barrier )
#endif

#define TEST_REDUCTION_METHOD(packed_reduction_method,which_reduction_block) \
            ( ( UNPACK_REDUCTION_METHOD( packed_reduction_method ) ) == ( which_reduction_block ) )

#if KMP_FAST_REDUCTION_BARRIER
    #define TREE_REDUCE_BLOCK_WITH_REDUCTION_BARRIER \
            ( PACK_REDUCTION_METHOD_AND_BARRIER( tree_reduce_block, bs_reduction_barrier ) )

    #define TREE_REDUCE_BLOCK_WITH_PLAIN_BARRIER \
            ( PACK_REDUCTION_METHOD_AND_BARRIER( tree_reduce_block, bs_plain_barrier ) )
#endif

typedef int PACKED_REDUCTION_METHOD_T;

/* -- end of fast reduction stuff ----------------------------------------- */

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

#if KMP_OS_WINDOWS
# define USE_CBLKDATA
# pragma warning( push )
# pragma warning( disable: 271 310 )
# include <windows.h>
# pragma warning( pop )
#endif

#if KMP_OS_UNIX
# include <pthread.h>
# include <dlfcn.h>
#endif

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

/*
 * Only Linux* OS and Windows* OS support thread affinity.
 */
#if KMP_OS_LINUX || KMP_OS_WINDOWS  

extern size_t __kmp_affin_mask_size;
# define KMP_AFFINITY_CAPABLE() (__kmp_affin_mask_size > 0)
# define KMP_CPU_SETSIZE        (__kmp_affin_mask_size * CHAR_BIT)

# if KMP_OS_LINUX   
//
// On Linux* OS, the mask isactually a vector of length __kmp_affin_mask_size
// (in bytes).  It should be allocated on a word boundary.
//
// WARNING!!!  We have made the base type of the affinity mask unsigned char,
// in order to eliminate a lot of checks that the true system mask size is
// really a multiple of 4 bytes (on Linux* OS).
//
// THESE MACROS WON'T WORK PROPERLY ON BIG ENDIAN MACHINES!!!
//

typedef unsigned char kmp_affin_mask_t;

#  define _KMP_CPU_SET(i,mask)   (mask[i/CHAR_BIT] |= (((kmp_affin_mask_t)1) << (i % CHAR_BIT)))
#  define KMP_CPU_SET(i,mask)    _KMP_CPU_SET((i), ((kmp_affin_mask_t *)(mask)))
#  define _KMP_CPU_ISSET(i,mask) (!!(mask[i/CHAR_BIT] & (((kmp_affin_mask_t)1) << (i % CHAR_BIT))))
#  define KMP_CPU_ISSET(i,mask)  _KMP_CPU_ISSET((i), ((kmp_affin_mask_t *)(mask)))
#  define _KMP_CPU_CLR(i,mask)   (mask[i/CHAR_BIT] &= ~(((kmp_affin_mask_t)1) << (i % CHAR_BIT)))
#  define KMP_CPU_CLR(i,mask)    _KMP_CPU_CLR((i), ((kmp_affin_mask_t *)(mask)))

#  define KMP_CPU_ZERO(mask) \
        {                                                                    \
            int __i;                                                         \
            for (__i = 0; __i < __kmp_affin_mask_size; __i++) {              \
                ((kmp_affin_mask_t *)(mask))[__i] = 0;                       \
            }                                                                \
        }

#  define KMP_CPU_COPY(dest, src) \
        {                                                                    \
            int __i;                                                         \
            for (__i = 0; __i < __kmp_affin_mask_size; __i++) {              \
                ((kmp_affin_mask_t *)(dest))[__i]                            \
                  = ((kmp_affin_mask_t *)(src))[__i];                        \
            }                                                                \
        }

#  define KMP_CPU_COMPLEMENT(mask) \
        {                                                                    \
            int __i;                                                         \
            for (__i = 0; __i < __kmp_affin_mask_size; __i++) {              \
                ((kmp_affin_mask_t *)(mask))[__i]                            \
                  = ~((kmp_affin_mask_t *)(mask))[__i];                      \
            }                                                                \
        }

#  define KMP_CPU_UNION(dest, src) \
        {                                                                    \
            int __i;                                                         \
            for (__i = 0; __i < __kmp_affin_mask_size; __i++) {              \
                ((kmp_affin_mask_t *)(dest))[__i]                            \
                  |= ((kmp_affin_mask_t *)(src))[__i];                       \
            }                                                                \
        }

# endif /* KMP_OS_LINUX */

# if KMP_OS_WINDOWS
//
// On Windows* OS, the mask size is 4 bytes for IA-32 architecture, and on 
// Intel(R) 64 it is 8 bytes times the number of processor groups.
//

#  if KMP_ARCH_X86_64

typedef struct GROUP_AFFINITY {
    KAFFINITY mask;
    WORD group;
    WORD reserved[3];
} GROUP_AFFINITY;

typedef DWORD_PTR kmp_affin_mask_t;

extern int __kmp_num_proc_groups;

#   define _KMP_CPU_SET(i,mask) \
        (mask[i/(CHAR_BIT * sizeof(kmp_affin_mask_t))] |=                    \
        (((kmp_affin_mask_t)1) << (i % (CHAR_BIT * sizeof(kmp_affin_mask_t)))))

#   define KMP_CPU_SET(i,mask) \
        _KMP_CPU_SET((i), ((kmp_affin_mask_t *)(mask)))

#   define _KMP_CPU_ISSET(i,mask) \
        (!!(mask[i/(CHAR_BIT * sizeof(kmp_affin_mask_t))] &                  \
        (((kmp_affin_mask_t)1) << (i % (CHAR_BIT * sizeof(kmp_affin_mask_t))))))

#   define KMP_CPU_ISSET(i,mask) \
        _KMP_CPU_ISSET((i), ((kmp_affin_mask_t *)(mask)))

#   define _KMP_CPU_CLR(i,mask) \
        (mask[i/(CHAR_BIT * sizeof(kmp_affin_mask_t))] &=                    \
        ~(((kmp_affin_mask_t)1) << (i % (CHAR_BIT * sizeof(kmp_affin_mask_t)))))

#   define KMP_CPU_CLR(i,mask) \
        _KMP_CPU_CLR((i), ((kmp_affin_mask_t *)(mask)))

#   define KMP_CPU_ZERO(mask) \
        {                                                                    \
            int __i;                                                         \
            for (__i = 0; __i < __kmp_num_proc_groups; __i++) {              \
                ((kmp_affin_mask_t *)(mask))[__i] = 0;                       \
            }                                                                \
        }

#   define KMP_CPU_COPY(dest, src) \
        {                                                                    \
            int __i;                                                         \
            for (__i = 0; __i < __kmp_num_proc_groups; __i++) {              \
                ((kmp_affin_mask_t *)(dest))[__i]                            \
                  = ((kmp_affin_mask_t *)(src))[__i];                        \
            }                                                                \
        }

#   define KMP_CPU_COMPLEMENT(mask) \
        {                                                                    \
            int __i;                                                         \
            for (__i = 0; __i < __kmp_num_proc_groups; __i++) {              \
                ((kmp_affin_mask_t *)(mask))[__i]                            \
                  = ~((kmp_affin_mask_t *)(mask))[__i];                      \
            }                                                                \
        }

#   define KMP_CPU_UNION(dest, src) \
        {                                                                    \
            int __i;                                                         \
            for (__i = 0; __i < __kmp_num_proc_groups; __i++) {              \
                ((kmp_affin_mask_t *)(dest))[__i]                            \
                  |= ((kmp_affin_mask_t *)(src))[__i];                       \
            }                                                                \
        }

typedef DWORD (*kmp_GetActiveProcessorCount_t)(WORD);
extern kmp_GetActiveProcessorCount_t __kmp_GetActiveProcessorCount;

typedef WORD (*kmp_GetActiveProcessorGroupCount_t)(void);
extern kmp_GetActiveProcessorGroupCount_t __kmp_GetActiveProcessorGroupCount;

typedef BOOL (*kmp_GetThreadGroupAffinity_t)(HANDLE, GROUP_AFFINITY *);
extern kmp_GetThreadGroupAffinity_t __kmp_GetThreadGroupAffinity;

typedef BOOL (*kmp_SetThreadGroupAffinity_t)(HANDLE, const GROUP_AFFINITY *, GROUP_AFFINITY *);
extern kmp_SetThreadGroupAffinity_t __kmp_SetThreadGroupAffinity;

extern int __kmp_get_proc_group(kmp_affin_mask_t const *mask);

#  else

typedef DWORD kmp_affin_mask_t; /* for compatibility with older winbase.h */

#   define KMP_CPU_SET(i,mask)      (*(mask) |= (((kmp_affin_mask_t)1) << (i)))
#   define KMP_CPU_ISSET(i,mask)    (!!(*(mask) & (((kmp_affin_mask_t)1) << (i))))
#   define KMP_CPU_CLR(i,mask)      (*(mask) &= ~(((kmp_affin_mask_t)1) << (i)))
#   define KMP_CPU_ZERO(mask)       (*(mask) = 0)
#   define KMP_CPU_COPY(dest, src)  (*(dest) = *(src))
#   define KMP_CPU_COMPLEMENT(mask) (*(mask) = ~*(mask))
#   define KMP_CPU_UNION(dest, src) (*(dest) |= *(src))

#  endif /* KMP_ARCH_X86 */

# endif /* KMP_OS_WINDOWS */

//
// __kmp_allocate() will return memory allocated on a 4-bytes boundary.
// after zeroing it - it takes care of those assumptions stated above.
//
# define KMP_CPU_ALLOC(ptr) \
        (ptr = ((kmp_affin_mask_t *)__kmp_allocate(__kmp_affin_mask_size)))
# define KMP_CPU_FREE(ptr) __kmp_free(ptr)

//
// The following macro should be used to index an array of masks.
// The array should be declared as "kmp_affinity_t *" and allocated with
// size "__kmp_affinity_mask_size * len".  The macro takes care of the fact
// that on Windows* OS, sizeof(kmp_affin_t) is really the size of the mask, but
// on Linux* OS, sizeof(kmp_affin_t) is 1.
//
# define KMP_CPU_INDEX(array,i) \
        ((kmp_affin_mask_t *)(((char *)(array)) + (i) * __kmp_affin_mask_size))

//
// Declare local char buffers with this size for printing debug and info
// messages, using __kmp_affinity_print_mask().
//
#define KMP_AFFIN_MASK_PRINT_LEN        1024

enum affinity_type {
    affinity_none = 0,
    affinity_physical,
    affinity_logical,
    affinity_compact,
    affinity_scatter,
    affinity_explicit,
#if KMP_MIC
    affinity_balanced,
#endif
    affinity_disabled,  // not used outsize the env var parser
    affinity_default
};

enum affinity_gran {
    affinity_gran_fine = 0,
    affinity_gran_thread,
    affinity_gran_core,
    affinity_gran_package,
    affinity_gran_node,
#if KMP_OS_WINDOWS && KMP_ARCH_X86_64
    //
    // The "group" granularity isn't necesssarily coarser than all of the
    // other levels, but we put it last in the enum.
    //
    affinity_gran_group,
#endif /* KMP_OS_WINDOWS && KMP_ARCH_X86_64 */
    affinity_gran_default
};

enum affinity_top_method {
    affinity_top_method_all = 0, // try all (supported) methods, in order
#if KMP_ARCH_X86 || KMP_ARCH_X86_64
    affinity_top_method_apicid,
    affinity_top_method_x2apicid,
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */
    affinity_top_method_cpuinfo, // KMP_CPUINFO_FILE is usable on Windows* OS, too
#if KMP_OS_WINDOWS && KMP_ARCH_X86_64
    affinity_top_method_group,
#endif /* KMP_OS_WINDOWS && KMP_ARCH_X86_64 */
    affinity_top_method_flat,
    affinity_top_method_default
};

#define affinity_respect_mask_default   (-1)

extern enum affinity_type __kmp_affinity_type; /* Affinity type */
extern enum affinity_gran __kmp_affinity_gran; /* Affinity granularity */
extern int __kmp_affinity_gran_levels; /* corresponding int value */
extern int __kmp_affinity_dups; /* Affinity duplicate masks */
extern enum affinity_top_method __kmp_affinity_top_method;
extern int __kmp_affinity_compact; /* Affinity 'compact' value */
extern int __kmp_affinity_offset; /* Affinity offset value  */
extern int __kmp_affinity_verbose; /* Was verbose specified for KMP_AFFINITY? */
extern int __kmp_affinity_warnings; /* KMP_AFFINITY warnings enabled ? */
extern int __kmp_affinity_respect_mask; /* Respect process' initial affinity mask? */
extern char * __kmp_affinity_proclist; /* proc ID list */
extern kmp_affin_mask_t *__kmp_affinity_masks;
extern unsigned __kmp_affinity_num_masks;
extern int __kmp_get_system_affinity(kmp_affin_mask_t *mask, int abort_on_error);
extern int __kmp_set_system_affinity(kmp_affin_mask_t const *mask, int abort_on_error);
extern void __kmp_affinity_bind_thread(int which);

# if KMP_OS_LINUX
extern kmp_affin_mask_t *__kmp_affinity_get_fullMask();
# endif /* KMP_OS_LINUX */
extern char const * __kmp_cpuinfo_file;

#elif KMP_OS_DARWIN
    // affinity not supported
#else
    #error "Unknown or unsupported OS"
#endif /* KMP_OS_LINUX || KMP_OS_WINDOWS */

#if OMP_40_ENABLED

//
// This needs to be kept in sync with the values in omp.h !!!
//
typedef enum kmp_proc_bind_t {
    proc_bind_false = 0,
    proc_bind_true,
    proc_bind_master,
    proc_bind_close,
    proc_bind_spread,
    proc_bind_disabled,
    proc_bind_intel,    // use KMP_AFFINITY interface
    proc_bind_default
} kmp_proc_bind_t;

typedef struct kmp_nested_proc_bind_t {
    kmp_proc_bind_t *bind_types;
    int size;
    int used;
} kmp_nested_proc_bind_t;

extern kmp_nested_proc_bind_t __kmp_nested_proc_bind;

# if (KMP_OS_WINDOWS || KMP_OS_LINUX)
#  define KMP_PLACE_ALL       (-1)
#  define KMP_PLACE_UNDEFINED (-2)
# endif /* (KMP_OS_WINDOWS || KMP_OS_LINUX) */

extern int __kmp_affinity_num_places;

#endif /* OMP_40_ENABLED */

#if KMP_MIC
extern unsigned int __kmp_place_num_cores;
extern unsigned int __kmp_place_num_threads_per_core;
extern unsigned int __kmp_place_core_offset;
#endif

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

#define KMP_PAD(type, sz)     (sizeof(type) + (sz - ((sizeof(type) - 1) % (sz)) - 1))

//
// We need to avoid using -1 as a GTID as +1 is added to the gtid
// when storing it in a lock, and the value 0 is reserved.
//
#define KMP_GTID_DNE            (-2)    /* Does not exist */
#define KMP_GTID_SHUTDOWN       (-3)    /* Library is shutting down */
#define KMP_GTID_MONITOR        (-4)    /* Monitor thread ID */
#define KMP_GTID_UNKNOWN        (-5)    /* Is not known */
#define KMP_GTID_MIN            (-6)    /* Minimal gtid for low bound check in DEBUG */

#define __kmp_get_gtid()               __kmp_get_global_thread_id()
#define __kmp_entry_gtid()             __kmp_get_global_thread_id_reg()

#define __kmp_tid_from_gtid(gtid)     ( KMP_DEBUG_ASSERT( (gtid) >= 0 ), \
                                        /*(__kmp_threads[ (gtid) ]->th.th_team_serialized) ? 0 : /* TODO remove this check, it is redundant */ \
                                        __kmp_threads[ (gtid) ]->th.th_info.ds.ds_tid )

#define __kmp_get_tid()               ( __kmp_tid_from_gtid( __kmp_get_gtid() ) )
#define __kmp_gtid_from_tid(tid,team) ( KMP_DEBUG_ASSERT( (tid) >= 0 && (team) != NULL ), \
                                        team -> t.t_threads[ (tid) ] -> th.th_info .ds.ds_gtid )

#define __kmp_get_team()              ( __kmp_threads[ (__kmp_get_gtid()) ]-> th.th_team )
#define __kmp_team_from_gtid(gtid)    ( KMP_DEBUG_ASSERT( (gtid) >= 0 ), \
                                        __kmp_threads[ (gtid) ]-> th.th_team )

#define __kmp_thread_from_gtid(gtid)  ( KMP_DEBUG_ASSERT( (gtid) >= 0 ), __kmp_threads[ (gtid) ] )
#define __kmp_get_thread()            ( __kmp_thread_from_gtid( __kmp_get_gtid() ) )

    // Returns current thread (pointer to kmp_info_t). In contrast to __kmp_get_thread(), it works
    // with registered and not-yet-registered threads.
#define __kmp_gtid_from_thread(thr)   ( KMP_DEBUG_ASSERT( (thr) != NULL ), \
                                        (thr)->th.th_info.ds.ds_gtid )

// AT: Which way is correct?
// AT: 1. nproc = __kmp_threads[ ( gtid ) ] -> th.th_team -> t.t_nproc;
// AT: 2. nproc = __kmp_threads[ ( gtid ) ] -> th.th_team_nproc;
#define __kmp_get_team_num_threads(gtid) ( __kmp_threads[ ( gtid ) ] -> th.th_team -> t.t_nproc )


/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

#define KMP_UINT64_MAX         (~((kmp_uint64)1<<((sizeof(kmp_uint64)*(1<<3))-1)))

#define KMP_MIN_NTH           1

#ifndef KMP_MAX_NTH
#  ifdef PTHREAD_THREADS_MAX
#    define KMP_MAX_NTH          PTHREAD_THREADS_MAX
#  else
#    define KMP_MAX_NTH          (32 * 1024)
#  endif
#endif /* KMP_MAX_NTH */

#ifdef PTHREAD_STACK_MIN
# define KMP_MIN_STKSIZE         PTHREAD_STACK_MIN
#else
# define KMP_MIN_STKSIZE         ((size_t)(32 * 1024))
#endif

#define KMP_MAX_STKSIZE          (~((size_t)1<<((sizeof(size_t)*(1<<3))-1)))

#if KMP_ARCH_X86
# define KMP_DEFAULT_STKSIZE     ((size_t)(2 * 1024 * 1024))
#elif KMP_ARCH_X86_64
# define KMP_DEFAULT_STKSIZE     ((size_t)(4 * 1024 * 1024))
# define KMP_BACKUP_STKSIZE      ((size_t)(2 * 1024 * 1024))
#else
# define KMP_DEFAULT_STKSIZE     ((size_t)(1024 * 1024))
#endif

#define KMP_DEFAULT_MONITOR_STKSIZE     ((size_t)(64 * 1024))

#define KMP_DEFAULT_MALLOC_POOL_INCR    ((size_t) (1024 * 1024))
#define KMP_MIN_MALLOC_POOL_INCR        ((size_t) (4 * 1024))
#define KMP_MAX_MALLOC_POOL_INCR        (~((size_t)1<<((sizeof(size_t)*(1<<3))-1)))

#define KMP_MIN_STKOFFSET       (0)
#define KMP_MAX_STKOFFSET       KMP_MAX_STKSIZE
#define KMP_DEFAULT_STKOFFSET   KMP_MIN_STKOFFSET

#define KMP_MIN_MONITOR_WAKEUPS      (1)       /* min number of times monitor wakes up per second */
#define KMP_MAX_MONITOR_WAKEUPS      (1000)    /* maximum number of times monitor can wake up per second */
#define KMP_BLOCKTIME_MULTIPLIER     (1000)    /* number of blocktime units per second */
#define KMP_MIN_BLOCKTIME            (0)
#define KMP_MAX_BLOCKTIME            (INT_MAX) /* Must be this for "infinite" setting the work */
#define KMP_DEFAULT_BLOCKTIME        (200)     /*  __kmp_blocktime is in milliseconds  */
/* Calculate new number of monitor wakeups for a specific block time based on previous monitor_wakeups */
/* Only allow increasing number of wakeups */
#define KMP_WAKEUPS_FROM_BLOCKTIME(blocktime, monitor_wakeups) \
                                 ( ((blocktime) == KMP_MAX_BLOCKTIME) ? (monitor_wakeups) : \
                                   ((blocktime) == KMP_MIN_BLOCKTIME) ? KMP_MAX_MONITOR_WAKEUPS : \
                                   ((monitor_wakeups) > (KMP_BLOCKTIME_MULTIPLIER / (blocktime))) ? (monitor_wakeups) : \
                                       (KMP_BLOCKTIME_MULTIPLIER) / (blocktime) )

/* Calculate number of intervals for a specific block time based on monitor_wakeups */
#define KMP_INTERVALS_FROM_BLOCKTIME(blocktime, monitor_wakeups)  \
                                 ( ( (blocktime) + (KMP_BLOCKTIME_MULTIPLIER / (monitor_wakeups)) - 1 ) /  \
                                   (KMP_BLOCKTIME_MULTIPLIER / (monitor_wakeups)) )

#define KMP_MIN_STATSCOLS       40
#define KMP_MAX_STATSCOLS       4096
#define KMP_DEFAULT_STATSCOLS   80

#define KMP_MIN_INTERVAL        0
#define KMP_MAX_INTERVAL        (INT_MAX-1)
#define KMP_DEFAULT_INTERVAL    0

#define KMP_MIN_CHUNK           1
#define KMP_MAX_CHUNK           (INT_MAX-1)
#define KMP_DEFAULT_CHUNK       1

#define KMP_MIN_INIT_WAIT       1
#define KMP_MAX_INIT_WAIT       (INT_MAX/2)
#define KMP_DEFAULT_INIT_WAIT   2048U

#define KMP_MIN_NEXT_WAIT       1
#define KMP_MAX_NEXT_WAIT       (INT_MAX/2)
#define KMP_DEFAULT_NEXT_WAIT   1024U

// max possible dynamic loops in concurrent execution per team
#define KMP_MAX_DISP_BUF        7
#define KMP_MAX_ORDERED         8

#define KMP_MAX_FIELDS          32

#define KMP_MAX_BRANCH_BITS     31

#define KMP_MAX_ACTIVE_LEVELS_LIMIT INT_MAX

/* Minimum number of threads before switch to TLS gtid (experimentally determined) */
/* josh TODO: what about OS X* tuning? */
#if   KMP_ARCH_X86 || KMP_ARCH_X86_64
# define KMP_TLS_GTID_MIN     5
#else
# define KMP_TLS_GTID_MIN     INT_MAX
#endif

#define KMP_MASTER_TID(tid)      ( (tid) == 0 )
#define KMP_WORKER_TID(tid)      ( (tid) != 0 )

#define KMP_MASTER_GTID(gtid)    ( __kmp_tid_from_gtid((gtid)) == 0 )
#define KMP_WORKER_GTID(gtid)    ( __kmp_tid_from_gtid((gtid)) != 0 )
#define KMP_UBER_GTID(gtid)                                           \
    (                                                                 \
        KMP_DEBUG_ASSERT( (gtid) >= KMP_GTID_MIN ),                   \
        KMP_DEBUG_ASSERT( (gtid) < __kmp_threads_capacity ),          \
        (gtid) >= 0 && __kmp_root[(gtid)] && __kmp_threads[(gtid)] && \
        (__kmp_threads[(gtid)] == __kmp_root[(gtid)]->r.r_uber_thread)\
    )
#define KMP_INITIAL_GTID(gtid)   ( (gtid) == 0 )

#ifndef TRUE
#define FALSE   0
#define TRUE    (! FALSE)
#endif

/* NOTE: all of the following constants must be even */

#if KMP_OS_WINDOWS
#  define KMP_INIT_WAIT    64U          /* initial number of spin-tests   */
#  define KMP_NEXT_WAIT    32U          /* susequent number of spin-tests */
#elif KMP_OS_LINUX
#  define KMP_INIT_WAIT  1024U          /* initial number of spin-tests   */
#  define KMP_NEXT_WAIT   512U          /* susequent number of spin-tests */
#elif KMP_OS_DARWIN
/* TODO: tune for KMP_OS_DARWIN */
#  define KMP_INIT_WAIT  1024U          /* initial number of spin-tests   */
#  define KMP_NEXT_WAIT   512U          /* susequent number of spin-tests */
#endif

#if KMP_ARCH_X86 || KMP_ARCH_X86_64
struct kmp_cpuid {
    kmp_uint32  eax;
    kmp_uint32  ebx;
    kmp_uint32  ecx;
    kmp_uint32  edx;
};
extern void __kmp_x86_cpuid( int mode, int mode2, struct kmp_cpuid *p );
# if KMP_MIC
  static void __kmp_x86_pause( void ) { _mm_delay_32( 100 ); };
# else
  extern void __kmp_x86_pause( void );
# endif
# define KMP_CPU_PAUSE()        __kmp_x86_pause()
#else
# define KMP_CPU_PAUSE()        /* nothing to do */
#endif

#define KMP_INIT_YIELD(count)           { (count) = __kmp_yield_init; }

#define KMP_YIELD(cond)                 { KMP_CPU_PAUSE(); __kmp_static_yield( (cond) ); }

// Note the decrement of 2 in the following Macros.  With KMP_LIBRARY=turnaround,
// there should be no yielding since the starting value from KMP_INIT_YIELD() is odd.

#define KMP_YIELD_WHEN(cond,count)      { KMP_CPU_PAUSE(); (count) -= 2; \
                                                if (!(count)) { KMP_YIELD(cond); (count) = __kmp_yield_next; } }
#define KMP_YIELD_SPIN(count)           { KMP_CPU_PAUSE(); (count) -=2; \
                                                if (!(count)) { KMP_YIELD(1); (count) = __kmp_yield_next; } }

/* ------------------------------------------------------------------------ */
/* Support datatypes for the orphaned construct nesting checks.             */
/* ------------------------------------------------------------------------ */

enum cons_type {
    ct_none,
    ct_parallel,
    ct_pdo,
    ct_pdo_ordered,
    ct_psections,
    ct_psingle,

    /* the following must be left in order and not split up */
    ct_taskq,
    ct_task,                    /* really task inside non-ordered taskq, considered a worksharing type */
    ct_task_ordered,            /* really task inside ordered taskq, considered a worksharing type */
    /* the preceding must be left in order and not split up */

    ct_critical,
    ct_ordered_in_parallel,
    ct_ordered_in_pdo,
    ct_ordered_in_taskq,
    ct_master,
    ct_reduce,
    ct_barrier
};

/* test to see if we are in a taskq construct */
# define IS_CONS_TYPE_TASKQ( ct )       ( ((int)(ct)) >= ((int)ct_taskq) && ((int)(ct)) <= ((int)ct_task_ordered) )
# define IS_CONS_TYPE_ORDERED( ct )     ((ct) == ct_pdo_ordered || (ct) == ct_task_ordered)

struct cons_data {
    ident_t const     *ident;
    enum cons_type     type;
    int                prev;
    kmp_user_lock_p    name;    /* address exclusively for critical section name comparison */
};

struct cons_header {
    int                 p_top, w_top, s_top;
    int                 stack_size, stack_top;
    struct cons_data   *stack_data;
};

struct kmp_region_info {
    char                *text;
    int                 offset[KMP_MAX_FIELDS];
    int                 length[KMP_MAX_FIELDS];
};


/* ---------------------------------------------------------------------- */
/* ---------------------------------------------------------------------- */

#if KMP_OS_WINDOWS
    typedef HANDLE              kmp_thread_t;
    typedef DWORD               kmp_key_t;
#endif /* KMP_OS_WINDOWS */

#if KMP_OS_UNIX
    typedef pthread_t           kmp_thread_t;
    typedef pthread_key_t       kmp_key_t;
#endif

extern kmp_key_t  __kmp_gtid_threadprivate_key;

typedef struct kmp_sys_info {
    long maxrss;          /* the maximum resident set size utilized (in kilobytes)     */
    long minflt;          /* the number of page faults serviced without any I/O        */
    long majflt;          /* the number of page faults serviced that required I/O      */
    long nswap;           /* the number of times a process was "swapped" out of memory */
    long inblock;         /* the number of times the file system had to perform input  */
    long oublock;         /* the number of times the file system had to perform output */
    long nvcsw;           /* the number of times a context switch was voluntarily      */
    long nivcsw;          /* the number of times a context switch was forced           */
} kmp_sys_info_t;

typedef struct kmp_cpuinfo {
    int        initialized;  // If 0, other fields are not initialized.
    int        signature;    // CPUID(1).EAX
    int        family;       // CPUID(1).EAX[27:20] + CPUID(1).EAX[11:8] ( Extended Family + Family )
    int        model;        // ( CPUID(1).EAX[19:16] << 4 ) + CPUID(1).EAX[7:4] ( ( Extended Model << 4 ) + Model)
    int        stepping;     // CPUID(1).EAX[3:0] ( Stepping )
    int        sse2;         // 0 if SSE2 instructions are not supported, 1 otherwise.

    int        cpu_stackoffset;
    int        apic_id;
    int        physical_id;
    int        logical_id;
    kmp_uint64 frequency;    // Nominal CPU frequency in Hz.
} kmp_cpuinfo_t;


#ifdef BUILD_TV

struct tv_threadprivate {
    /* Record type #1 */
    void        *global_addr;
    void        *thread_addr;
};

struct tv_data {
    struct tv_data      *next;
    void                *type;
    union tv_union {
        struct tv_threadprivate tp;
    } u;
};

extern kmp_key_t __kmp_tv_key;

#endif /* BUILD_TV */

/* ------------------------------------------------------------------------ */
// Some forward declarations.

typedef union  kmp_team      kmp_team_t;
typedef struct kmp_taskdata  kmp_taskdata_t;
typedef union  kmp_task_team kmp_task_team_t;
typedef union  kmp_team      kmp_team_p;
typedef union  kmp_info      kmp_info_p;
typedef union  kmp_root      kmp_root_p;


/* ------------------------------------------------------------------------ */

/*
 * Taskq data structures
 */

#define HIGH_WATER_MARK(nslots)         (((nslots) * 3) / 4)
#define __KMP_TASKQ_THUNKS_PER_TH        1      /* num thunks that each thread can simultaneously execute from a task queue */

/*  flags for taskq_global_flags, kmp_task_queue_t tq_flags, kmpc_thunk_t th_flags  */

#define TQF_IS_ORDERED          0x0001  /*  __kmpc_taskq interface, taskq ordered  */
#define TQF_IS_LASTPRIVATE      0x0002  /*  __kmpc_taskq interface, taskq with lastprivate list  */
#define TQF_IS_NOWAIT           0x0004  /*  __kmpc_taskq interface, end taskq nowait  */
#define TQF_HEURISTICS          0x0008  /*  __kmpc_taskq interface, use heuristics to decide task queue size  */
#define TQF_INTERFACE_RESERVED1 0x0010  /*  __kmpc_taskq interface, reserved for future use  */
#define TQF_INTERFACE_RESERVED2 0x0020  /*  __kmpc_taskq interface, reserved for future use  */
#define TQF_INTERFACE_RESERVED3 0x0040  /*  __kmpc_taskq interface, reserved for future use  */
#define TQF_INTERFACE_RESERVED4 0x0080  /*  __kmpc_taskq interface, reserved for future use  */

#define TQF_INTERFACE_FLAGS     0x00ff  /*  all the __kmpc_taskq interface flags  */

#define TQF_IS_LAST_TASK        0x0100  /*  internal/read by instrumentation; only used with TQF_IS_LASTPRIVATE  */
#define TQF_TASKQ_TASK          0x0200  /*  internal use only; this thunk->th_task is the taskq_task  */
#define TQF_RELEASE_WORKERS     0x0400  /*  internal use only; must release worker threads once ANY queued task exists (global) */
#define TQF_ALL_TASKS_QUEUED    0x0800  /*  internal use only; notify workers that master has finished enqueuing tasks */
#define TQF_PARALLEL_CONTEXT    0x1000  /*  internal use only: this queue encountered in a parallel context: not serialized */
#define TQF_DEALLOCATED         0x2000  /*  internal use only; this queue is on the freelist and not in use */

#define TQF_INTERNAL_FLAGS      0x3f00  /*  all the internal use only flags  */

typedef struct KMP_ALIGN_CACHE kmpc_aligned_int32_t {
    kmp_int32                      ai_data;
} kmpc_aligned_int32_t;

typedef struct KMP_ALIGN_CACHE kmpc_aligned_queue_slot_t {
    struct kmpc_thunk_t   *qs_thunk;
} kmpc_aligned_queue_slot_t;

typedef struct kmpc_task_queue_t {
        /* task queue linkage fields for n-ary tree of queues (locked with global taskq_tree_lck) */
    kmp_lock_t                    tq_link_lck;          /*  lock for child link, child next/prev links and child ref counts */
    union {
        struct kmpc_task_queue_t *tq_parent;            /*  pointer to parent taskq, not locked */
        struct kmpc_task_queue_t *tq_next_free;         /*  for taskq internal freelists, locked with global taskq_freelist_lck */
    } tq;
    volatile struct kmpc_task_queue_t *tq_first_child;  /*  pointer to linked-list of children, locked by tq's tq_link_lck */
    struct kmpc_task_queue_t     *tq_next_child;        /*  next child in linked-list, locked by parent tq's tq_link_lck */
    struct kmpc_task_queue_t     *tq_prev_child;        /*  previous child in linked-list, locked by parent tq's tq_link_lck */
    volatile kmp_int32            tq_ref_count;         /*  reference count of threads with access to this task queue */
                                                        /*  (other than the thread executing the kmpc_end_taskq call) */
                                                        /*  locked by parent tq's tq_link_lck */

        /* shared data for task queue */
    struct kmpc_aligned_shared_vars_t    *tq_shareds;   /*  per-thread array of pointers to shared variable structures */
                                                        /*  only one array element exists for all but outermost taskq */

        /* bookkeeping for ordered task queue */
    kmp_uint32                    tq_tasknum_queuing;   /*  ordered task number assigned while queuing tasks */
    volatile kmp_uint32           tq_tasknum_serving;   /*  ordered number of next task to be served (executed) */

        /* thunk storage management for task queue */
    kmp_lock_t                    tq_free_thunks_lck;   /*  lock for thunk freelist manipulation */
    struct kmpc_thunk_t          *tq_free_thunks;       /*  thunk freelist, chained via th.th_next_free  */
    struct kmpc_thunk_t          *tq_thunk_space;       /*  space allocated for thunks for this task queue  */

        /* data fields for queue itself */
    kmp_lock_t                    tq_queue_lck;         /*  lock for [de]enqueue operations: tq_queue, tq_head, tq_tail, tq_nfull */
    kmpc_aligned_queue_slot_t    *tq_queue;             /*  array of queue slots to hold thunks for tasks */
    volatile struct kmpc_thunk_t *tq_taskq_slot;        /*  special slot for taskq task thunk, occupied if not NULL  */
    kmp_int32                     tq_nslots;            /*  # of tq_thunk_space thunks alloc'd (not incl. tq_taskq_slot space)  */
    kmp_int32                     tq_head;              /*  enqueue puts next item in here (index into tq_queue array) */
    kmp_int32                     tq_tail;              /*  dequeue takes next item out of here (index into tq_queue array) */
    volatile kmp_int32            tq_nfull;             /*  # of occupied entries in task queue right now  */
    kmp_int32                     tq_hiwat;             /*  high-water mark for tq_nfull and queue scheduling  */
    volatile kmp_int32            tq_flags;             /*  TQF_xxx  */

        /* bookkeeping for oustanding thunks */
    struct kmpc_aligned_int32_t  *tq_th_thunks;         /*  per-thread array for # of regular thunks currently being executed */
    kmp_int32                     tq_nproc;             /*  number of thunks in the th_thunks array */

        /* statistics library bookkeeping */
    ident_t                       *tq_loc;              /*  source location information for taskq directive */
} kmpc_task_queue_t;

typedef void (*kmpc_task_t) (kmp_int32 global_tid, struct kmpc_thunk_t *thunk);

/*  sizeof_shareds passed as arg to __kmpc_taskq call  */
typedef struct kmpc_shared_vars_t {             /*  aligned during dynamic allocation */
    kmpc_task_queue_t         *sv_queue;
    /*  (pointers to) shared vars  */
} kmpc_shared_vars_t;

typedef struct KMP_ALIGN_CACHE kmpc_aligned_shared_vars_t {
    volatile struct kmpc_shared_vars_t     *ai_data;
} kmpc_aligned_shared_vars_t;

/*  sizeof_thunk passed as arg to kmpc_taskq call  */
typedef struct kmpc_thunk_t {                   /*  aligned during dynamic allocation */
    union {                                     /*  field used for internal freelists too  */
        kmpc_shared_vars_t  *th_shareds;
        struct kmpc_thunk_t *th_next_free;      /*  freelist of individual thunks within queue, head at tq_free_thunks  */
    } th;
    kmpc_task_t th_task;                        /*  taskq_task if flags & TQF_TASKQ_TASK  */
    struct kmpc_thunk_t *th_encl_thunk;         /*  pointer to dynamically enclosing thunk on this thread's call stack */
    kmp_int32 th_flags;                         /*  TQF_xxx (tq_flags interface plus possible internal flags)  */
    kmp_int32 th_status;
    kmp_uint32 th_tasknum;                      /*  task number assigned in order of queuing, used for ordered sections */
    /*  private vars  */
} kmpc_thunk_t;

typedef struct KMP_ALIGN_CACHE kmp_taskq {
    int                 tq_curr_thunk_capacity;

    kmpc_task_queue_t  *tq_root;
    kmp_int32           tq_global_flags;

    kmp_lock_t          tq_freelist_lck;
    kmpc_task_queue_t  *tq_freelist;

    kmpc_thunk_t      **tq_curr_thunk;
} kmp_taskq_t;

/* END Taskq data structures */
/* --------------------------------------------------------------------------- */

typedef kmp_int32 kmp_critical_name[8];

/*!
@ingroup PARALLEL
The type for a microtask which gets passed to @ref __kmpc_fork_call().
The arguments to the outlined function are
@param global_tid the global thread identity of the thread executing the function.
@param bound_tid  the local identitiy of the thread executing the function
@param ... pointers to shared variables accessed by the function.
*/
typedef void (*kmpc_micro)              ( kmp_int32 * global_tid, kmp_int32 * bound_tid, ... );
typedef void (*kmpc_micro_bound)        ( kmp_int32 * bound_tid, kmp_int32 * bound_nth, ... );

/*! 
@ingroup THREADPRIVATE
@{
*/
/* --------------------------------------------------------------------------- */
/* Threadprivate initialization/finalization function declarations */

/*  for non-array objects:  __kmpc_threadprivate_register()  */

/*!
 Pointer to the constructor function.
 The first argument is the <tt>this</tt> pointer 
*/
typedef void *(*kmpc_ctor)    (void *);

/*!
 Pointer to the destructor function.
 The first argument is the <tt>this</tt> pointer 
*/
typedef void (*kmpc_dtor)     (void * /*, size_t */); /* 2nd arg: magic number for KCC unused by Intel compiler */
/*!
 Pointer to an alternate constructor.
 The first argument is the <tt>this</tt> pointer.
*/
typedef void *(*kmpc_cctor)   (void *, void *); 

/*  for array objects: __kmpc_threadprivate_register_vec()  */
                                /* First arg: "this" pointer */
                                /* Last arg: number of array elements */
/*!
 Array constructor.
 First argument is the <tt>this</tt> pointer
 Second argument the number of array elements. 
*/
typedef void *(*kmpc_ctor_vec)  (void *, size_t);
/*!
 Pointer to the array destructor function.
 The first argument is the <tt>this</tt> pointer 
 Second argument the number of array elements. 
*/
typedef void (*kmpc_dtor_vec)   (void *, size_t);
/*!
 Array constructor.
 First argument is the <tt>this</tt> pointer
 Third argument the number of array elements. 
*/
typedef void *(*kmpc_cctor_vec) (void *, void *, size_t); /* function unused by compiler */

/*!
@}
*/


/* ------------------------------------------------------------------------ */

/* keeps tracked of threadprivate cache allocations for cleanup later */
typedef struct kmp_cached_addr {
    void                      **addr;           /* address of allocated cache */
    struct kmp_cached_addr     *next;           /* pointer to next cached address */
} kmp_cached_addr_t;

struct private_data {
    struct private_data *next;          /* The next descriptor in the list      */
    void                *data;          /* The data buffer for this descriptor  */
    int                  more;          /* The repeat count for this descriptor */
    size_t               size;          /* The data size for this descriptor    */
};

struct private_common {
    struct private_common     *next;
    struct private_common     *link;
    void                      *gbl_addr;
    void                      *par_addr;        /* par_addr == gbl_addr for MASTER thread */
    size_t                     cmn_size;
};

struct shared_common
{
    struct shared_common      *next;
    struct private_data       *pod_init;
    void                      *obj_init;
    void                      *gbl_addr;
    union {
        kmpc_ctor              ctor;
        kmpc_ctor_vec          ctorv;
    } ct;
    union {
        kmpc_cctor             cctor;
        kmpc_cctor_vec         cctorv;
    } cct;
    union {
        kmpc_dtor              dtor;
        kmpc_dtor_vec          dtorv;
    } dt;
    size_t                     vec_len;
    int                        is_vec;
    size_t                     cmn_size;
};

#define KMP_HASH_TABLE_LOG2     9                               /* log2 of the hash table size */
#define KMP_HASH_TABLE_SIZE     (1 << KMP_HASH_TABLE_LOG2)      /* size of the hash table */
#define KMP_HASH_SHIFT          3                               /* throw away this many low bits from the address */
#define KMP_HASH(x)             ((((kmp_uintptr_t) x) >> KMP_HASH_SHIFT) & (KMP_HASH_TABLE_SIZE-1))

struct common_table {
    struct  private_common      *data[ KMP_HASH_TABLE_SIZE ];
};

struct shared_table {
    struct  shared_common       *data[ KMP_HASH_TABLE_SIZE ];
};
/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

#ifdef KMP_STATIC_STEAL_ENABLED
typedef struct KMP_ALIGN_CACHE dispatch_private_info32 {
    kmp_int32 count;
    kmp_int32 ub;
    /* Adding KMP_ALIGN_CACHE here doesn't help / can hurt performance */
    kmp_int32 lb;
    kmp_int32 st;
    kmp_int32 tc;
    kmp_int32 static_steal_counter; /* for static_steal only; maybe better to put after ub */

    // KMP_ALIGN( 16 ) ensures ( if the KMP_ALIGN macro is turned on )
    //    a) parm3 is properly aligned and
    //    b) all parm1-4 are in the same cache line.
    // Because of parm1-4 are used together, performance seems to be better
    // if they are in the same line (not measured though).

    struct KMP_ALIGN( 32 ) { // AC: changed 16 to 32 in order to simplify template
        kmp_int32 parm1;     //     structures in kmp_dispatch.cpp. This should
        kmp_int32 parm2;     //     make no real change at least while padding is off.
        kmp_int32 parm3;
        kmp_int32 parm4;
    };

    kmp_uint32 ordered_lower;
    kmp_uint32 ordered_upper;
#if KMP_OS_WINDOWS
    // This var can be placed in the hole between 'tc' and 'parm1', instead of 'static_steal_counter'.
    // It would be nice to measure execution times.
    // Conditional if/endif can be removed at all.
    kmp_int32 last_upper;
#endif /* KMP_OS_WINDOWS */
} dispatch_private_info32_t;

typedef struct KMP_ALIGN_CACHE dispatch_private_info64 {
    kmp_int64 count;   /* current chunk number for static and static-steal scheduling*/
    kmp_int64 ub;      /* upper-bound */
    /* Adding KMP_ALIGN_CACHE here doesn't help / can hurt performance */
    kmp_int64 lb;      /* lower-bound */
    kmp_int64 st;      /* stride */
    kmp_int64 tc;      /* trip count (number of iterations) */
    kmp_int64 static_steal_counter; /* for static_steal only; maybe better to put after ub */

    /* parm[1-4] are used in different ways by different scheduling algorithms */

    // KMP_ALIGN( 32 ) ensures ( if the KMP_ALIGN macro is turned on )
    //    a) parm3 is properly aligned and
    //    b) all parm1-4 are in the same cache line.
    // Because of parm1-4 are used together, performance seems to be better
    // if they are in the same line (not measured though).

    struct KMP_ALIGN( 32 ) {
        kmp_int64 parm1;
        kmp_int64 parm2;
        kmp_int64 parm3;
        kmp_int64 parm4;
    };

    kmp_uint64 ordered_lower;
    kmp_uint64 ordered_upper;
#if KMP_OS_WINDOWS
    // This var can be placed in the hole between 'tc' and 'parm1', instead of 'static_steal_counter'.
    // It would be nice to measure execution times.
    // Conditional if/endif can be removed at all.
    kmp_int64 last_upper;
#endif /* KMP_OS_WINDOWS */
} dispatch_private_info64_t;
#else /* KMP_STATIC_STEAL_ENABLED */
typedef struct KMP_ALIGN_CACHE dispatch_private_info32 {
    kmp_int32 lb;
    kmp_int32 ub;
    kmp_int32 st;
    kmp_int32 tc;

    kmp_int32 parm1;
    kmp_int32 parm2;
    kmp_int32 parm3;
    kmp_int32 parm4;

    kmp_int32 count;

    kmp_uint32 ordered_lower;
    kmp_uint32 ordered_upper;
#if KMP_OS_WINDOWS
    kmp_int32 last_upper;
#endif /* KMP_OS_WINDOWS */
} dispatch_private_info32_t;

typedef struct KMP_ALIGN_CACHE dispatch_private_info64 {
    kmp_int64 lb;      /* lower-bound */
    kmp_int64 ub;      /* upper-bound */
    kmp_int64 st;      /* stride */
    kmp_int64 tc;      /* trip count (number of iterations) */

    /* parm[1-4] are used in different ways by different scheduling algorithms */
    kmp_int64 parm1;
    kmp_int64 parm2;
    kmp_int64 parm3;
    kmp_int64 parm4;

    kmp_int64 count;   /* current chunk number for static scheduling */

    kmp_uint64 ordered_lower;
    kmp_uint64 ordered_upper;
#if KMP_OS_WINDOWS
    kmp_int64 last_upper;
#endif /* KMP_OS_WINDOWS */
} dispatch_private_info64_t;
#endif /* KMP_STATIC_STEAL_ENABLED */

typedef struct KMP_ALIGN_CACHE dispatch_private_info {
    union private_info {
        dispatch_private_info32_t  p32;
        dispatch_private_info64_t  p64;
    } u;
    enum sched_type schedule;  /* scheduling algorithm */
    kmp_int32       ordered;   /* ordered clause specified */
    kmp_int32       ordered_bumped;
    kmp_int32   ordered_dummy[KMP_MAX_ORDERED-3]; // to retain the structure size after making ordered_iteration scalar
    struct dispatch_private_info * next; /* stack of buffers for nest of serial regions */
    kmp_int32       nomerge;   /* don't merge iters if serialized */
    kmp_int32       type_size; /* the size of types in private_info */
    enum cons_type  pushed_ws;
} dispatch_private_info_t;

typedef struct dispatch_shared_info32 {
    /* chunk index under dynamic, number of idle threads under static-steal;
       iteration index otherwise */
    volatile kmp_uint32      iteration;
    volatile kmp_uint32      num_done;
    volatile kmp_uint32      ordered_iteration;
    kmp_int32   ordered_dummy[KMP_MAX_ORDERED-1]; // to retain the structure size after making ordered_iteration scalar
} dispatch_shared_info32_t;

typedef struct dispatch_shared_info64 {
    /* chunk index under dynamic, number of idle threads under static-steal;
       iteration index otherwise */
    volatile kmp_uint64      iteration;
    volatile kmp_uint64      num_done;
    volatile kmp_uint64      ordered_iteration;
    kmp_int64   ordered_dummy[KMP_MAX_ORDERED-1]; // to retain the structure size after making ordered_iteration scalar
} dispatch_shared_info64_t;

typedef struct dispatch_shared_info {
    union shared_info {
        dispatch_shared_info32_t  s32;
        dispatch_shared_info64_t  s64;
    } u;
/*    volatile kmp_int32      dispatch_abort;  depricated */
    volatile kmp_uint32     buffer_index;
} dispatch_shared_info_t;

typedef struct kmp_disp {
    /* Vector for ORDERED SECTION */
    void (*th_deo_fcn)( int * gtid, int * cid, ident_t *);
    /* Vector for END ORDERED SECTION */
    void (*th_dxo_fcn)( int * gtid, int * cid, ident_t *);

    dispatch_shared_info_t  *th_dispatch_sh_current;
    dispatch_private_info_t *th_dispatch_pr_current;

    dispatch_private_info_t *th_disp_buffer;
    kmp_int32                th_disp_index;
    void* dummy_padding[2]; // make it 64 bytes on Intel(R) 64
} kmp_disp_t;

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

/* Barrier stuff */

/* constants for barrier state update */
#define KMP_INIT_BARRIER_STATE  0       /* should probably start from zero */
#define KMP_BARRIER_SLEEP_BIT   0       /* bit used for suspend/sleep part of state */
#define KMP_BARRIER_UNUSED_BIT  1       /* bit that must never be set for valid state */
#define KMP_BARRIER_BUMP_BIT    2       /* lsb used for bump of go/arrived state */

#define KMP_BARRIER_SLEEP_STATE         ((kmp_uint) (1 << KMP_BARRIER_SLEEP_BIT))
#define KMP_BARRIER_UNUSED_STATE        ((kmp_uint) (1 << KMP_BARRIER_UNUSED_BIT))
#define KMP_BARRIER_STATE_BUMP          ((kmp_uint) (1 << KMP_BARRIER_BUMP_BIT))

#if (KMP_BARRIER_SLEEP_BIT >= KMP_BARRIER_BUMP_BIT)
# error "Barrier sleep bit must be smaller than barrier bump bit"
#endif
#if (KMP_BARRIER_UNUSED_BIT >= KMP_BARRIER_BUMP_BIT)
# error "Barrier unused bit must be smaller than barrier bump bit"
#endif


enum barrier_type {
    bs_plain_barrier = 0,       /* 0, All non-fork/join barriers (except reduction barriers if enabled) */
    bs_forkjoin_barrier,        /* 1, All fork/join (parallel region) barriers */
    #if KMP_FAST_REDUCTION_BARRIER
        bs_reduction_barrier,   /* 2, All barriers that are used in reduction */
    #endif // KMP_FAST_REDUCTION_BARRIER
    bs_last_barrier             /* Just a placeholder to mark the end */
};

// to work with reduction barriers just like with plain barriers
#if !KMP_FAST_REDUCTION_BARRIER
    #define bs_reduction_barrier bs_plain_barrier
#endif // KMP_FAST_REDUCTION_BARRIER

typedef enum kmp_bar_pat {      /* Barrier communication patterns */
    bp_linear_bar = 0,          /* Single level (degenerate) tree */
    bp_tree_bar = 1,            /* Balanced tree with branching factor 2^n */
    bp_hyper_bar = 2,           /* Hypercube-embedded tree with min branching factor 2^n */
    bp_last_bar = 3             /* Placeholder to mark the end */
} kmp_bar_pat_e;

/* Thread barrier needs volatile barrier fields */
typedef struct KMP_ALIGN_CACHE kmp_bstate {
    volatile kmp_uint   b_arrived;              /* STATE => task reached synch point. */
    #if (KMP_PERF_V19 == KMP_ON)
        KMP_ALIGN_CACHE
    #endif
    volatile kmp_uint   b_go;                   /* STATE => task should proceed.      */
} kmp_bstate_t;

union KMP_ALIGN_CACHE kmp_barrier_union {
    double       b_align;        /* use worst case alignment */
    char         b_pad[ KMP_PAD(kmp_bstate_t, CACHE_LINE) ];
    kmp_bstate_t bb;
};

typedef union kmp_barrier_union kmp_balign_t;

/* Team barrier needs only non-volatile arrived counter */
union KMP_ALIGN_CACHE kmp_barrier_team_union {
    double       b_align;        /* use worst case alignment */
    char         b_pad[ CACHE_LINE ];
    struct {
        kmp_uint     b_arrived;       /* STATE => task reached synch point. */
    };
};

typedef union kmp_barrier_team_union kmp_balign_team_t;

/*
 * Padding for Linux* OS pthreads condition variables and mutexes used to signal
 * threads when a condition changes.  This is to workaround an NPTL bug
 * where padding was added to pthread_cond_t which caused the initialization
 * routine to write outside of the structure if compiled on pre-NPTL threads.
 */

#if KMP_OS_WINDOWS
typedef struct kmp_win32_mutex
{
    /* The Lock */
    CRITICAL_SECTION cs;
} kmp_win32_mutex_t;

typedef struct kmp_win32_cond
{
    /* Count of the number of waiters. */
    int waiters_count_;

    /* Serialize access to <waiters_count_> */
    kmp_win32_mutex_t waiters_count_lock_;

    /* Number of threads to release via a <cond_broadcast> or a */
    /* <cond_signal> */
    int release_count_;

    /* Keeps track of the current "generation" so that we don't allow */
    /* one thread to steal all the "releases" from the broadcast. */
    int wait_generation_count_;

    /* A manual-reset event that's used to block and release waiting */
    /* threads. */
    HANDLE event_;
} kmp_win32_cond_t;
#endif

#if KMP_OS_UNIX

union KMP_ALIGN_CACHE kmp_cond_union {
    double              c_align;
    char                c_pad[ CACHE_LINE ];
    pthread_cond_t      c_cond;
};

typedef union kmp_cond_union kmp_cond_align_t;

union KMP_ALIGN_CACHE kmp_mutex_union {
    double              m_align;
    char                m_pad[ CACHE_LINE ];
    pthread_mutex_t     m_mutex;
};

typedef union kmp_mutex_union kmp_mutex_align_t;

#endif /* KMP_OS_UNIX */

typedef struct kmp_desc_base {
    void    *ds_stackbase;
    size_t            ds_stacksize;
    int               ds_stackgrow;
    kmp_thread_t      ds_thread;
    volatile int      ds_tid;
    int               ds_gtid;
#if KMP_OS_WINDOWS
    volatile int      ds_alive;
    DWORD             ds_thread_id;
        /*
            ds_thread keeps thread handle on Windows* OS. It is enough for RTL purposes. However,
            debugger support (libomp_db) cannot work with handles, because they uncomparable. For
            example, debugger requests info about thread with handle h. h is valid within debugger
            process, and meaningless within debugee process. Even if h is duped by call to
            DuplicateHandle(), so the result h' is valid within debugee process, but it is a *new*
            handle which does *not* equal to any other handle in debugee... The only way to
            compare handles is convert them to system-wide ids. GetThreadId() function is
            available only in Longhorn and Server 2003. :-( In contrast, GetCurrentThreadId() is
            available on all Windows* OS flavours (including Windows* 95). Thus, we have to get thread id by
            call to GetCurrentThreadId() from within the thread and save it to let libomp_db
            identify threads.
        */
#endif /* KMP_OS_WINDOWS */
} kmp_desc_base_t;

typedef union KMP_ALIGN_CACHE kmp_desc {
    double           ds_align;        /* use worst case alignment */
    char             ds_pad[ KMP_PAD(kmp_desc_base_t, CACHE_LINE) ];
    kmp_desc_base_t  ds;
} kmp_desc_t;


typedef struct kmp_local {
    volatile int           this_construct; /* count of single's encountered by thread */
    volatile int           last_construct; /* cache for team's count used by old algorithm */
    void                  *reduce_data;
#if KMP_USE_BGET
    void                  *bget_data;
    void                  *bget_list;
#if ! USE_CMP_XCHG_FOR_BGET
#ifdef USE_QUEUING_LOCK_FOR_BGET
    kmp_lock_t             bget_lock;      /* Lock for accessing bget free list */
#else
    kmp_bootstrap_lock_t   bget_lock;      /* Lock for accessing bget free list */
                                           /* Must be bootstrap lock so we can use it at library shutdown */
#endif /* USE_LOCK_FOR_BGET */
#endif /* ! USE_CMP_XCHG_FOR_BGET */
#endif /* KMP_USE_BGET */

#ifdef BUILD_TV
    struct tv_data        *tv_data;
#endif

    PACKED_REDUCTION_METHOD_T packed_reduction_method; /* stored by __kmpc_reduce*(), used by __kmpc_end_reduce*() */

} kmp_local_t;

/* Record for holding the values of the internal controls stack records */

typedef struct kmp_internal_control {
    int           serial_nesting_level;  /* corresponds to the value of the th_team_serialized field */
    int           nested;                /* internal control for nested parallelism (per thread) */
    int           dynamic;               /* internal control for dynamic adjustment of threads (per thread) */
    int           nproc;                 /* internal control for # of threads for next parallel region (per thread) */
    int           blocktime;             /* internal control for blocktime */
    int           bt_intervals;          /* internal control for blocktime intervals */
    int           bt_set;                /* internal control for whether blocktime is explicitly set */
#if OMP_30_ENABLED
    int           max_active_levels;     /* internal control for max_active_levels */
    kmp_r_sched_t sched;                 /* internal control for runtime schedule {sched,chunk} pair */
#endif // OMP_30_ENABLED
#if OMP_40_ENABLED
    kmp_proc_bind_t proc_bind;           /* internal control for affinity  */
#endif // OMP_40_ENABLED
    struct kmp_internal_control *next;

} kmp_internal_control_t;

#if OMP_30_ENABLED
static inline void
copy_icvs( kmp_internal_control_t *dst, kmp_internal_control_t *src ) {
    // int serial_nesting_level;        // Skip. There was no copy of this field in the original code.
    dst->nested  = src->nested;
    dst->dynamic = src->dynamic;
    dst->nproc   = src->nproc;
    dst->blocktime = src->blocktime;
    dst->bt_intervals = src->bt_intervals;
    dst->bt_set = src->bt_set;
#if OMP_30_ENABLED
    dst->max_active_levels = src->max_active_levels;
    dst->sched = src->sched;
#endif
#if OMP_40_ENABLED
    dst->proc_bind = src->proc_bind;
#endif
    //struct kmp_internal_control *next; // Skip. There was no copy of this field in the original code.
}
#endif // OMP_30_ENABLED

#if OMP_30_ENABLED

    #define get__blocktime( xteam, xtid )     ((xteam)->t.t_threads[(xtid)]->th.th_current_task->td_icvs.blocktime)
    #define get__bt_set( xteam, xtid )        ((xteam)->t.t_threads[(xtid)]->th.th_current_task->td_icvs.bt_set)
    #define get__bt_intervals( xteam, xtid )  ((xteam)->t.t_threads[(xtid)]->th.th_current_task->td_icvs.bt_intervals)

    #define get__nested_2(xteam,xtid)         ((xteam)->t.t_threads[(xtid)]->th.th_current_task->td_icvs.nested)
    #define get__dynamic_2(xteam,xtid)        ((xteam)->t.t_threads[(xtid)]->th.th_current_task->td_icvs.dynamic)
    #define get__nproc_2(xteam,xtid)          ((xteam)->t.t_threads[(xtid)]->th.th_current_task->td_icvs.nproc)
    #define get__sched_2(xteam,xtid)          ((xteam)->t.t_threads[(xtid)]->th.th_current_task->td_icvs.sched)

    #define set__blocktime_team( xteam, xtid, xval ) \
            ( ( (xteam)->t.t_threads[(xtid)]->th.th_current_task->td_icvs.blocktime )    = (xval) )

    #define set__bt_intervals_team( xteam, xtid, xval ) \
            ( ( (xteam)->t.t_threads[(xtid)]->th.th_current_task->td_icvs.bt_intervals ) = (xval) )

    #define set__bt_set_team( xteam, xtid, xval ) \
            ( ( (xteam)->t.t_threads[(xtid)]->th.th_current_task->td_icvs.bt_set )       = (xval) )



    #define set__nested( xthread, xval )                            \
            ( ( (xthread)->th.th_serial_team->t.t_threads[0]                                ->th.th_current_task->td_icvs.nested ) = \
              ( (xthread)->th.th_team       ->t.t_threads[((xthread)->th.th_info.ds.ds_tid)]->th.th_current_task->td_icvs.nested ) = \
              (xval) )
    #define get__nested( xthread ) \
            ( ( (xthread)->th.th_team       ->t.t_threads[((xthread)->th.th_info.ds.ds_tid)]->th.th_current_task->td_icvs.nested ) \
            ? (FTN_TRUE) : (FTN_FALSE) )

    #define set__dynamic( xthread, xval )                            \
            ( ( (xthread)->th.th_serial_team->t.t_threads[0]                                ->th.th_current_task->td_icvs.dynamic ) = \
              ( (xthread)->th.th_team       ->t.t_threads[((xthread)->th.th_info.ds.ds_tid)]->th.th_current_task->td_icvs.dynamic ) = \
              (xval) )
    #define get__dynamic( xthread ) \
            ( ( (xthread)->th.th_team       ->t.t_threads[((xthread)->th.th_info.ds.ds_tid)]->th.th_current_task->td_icvs.dynamic ) \
            ? (FTN_TRUE) : (FTN_FALSE) )

    #define set__nproc( xthread, xval )                            \
            ( ( (xthread)->th.th_serial_team->t.t_threads[0]                                ->th.th_current_task->td_icvs.nproc ) = \
              ( (xthread)->th.th_team       ->t.t_threads[((xthread)->th.th_info.ds.ds_tid)]->th.th_current_task->td_icvs.nproc ) = \
              (xval) )

    #define set__nproc_p( xthread, xval )                            \
            (                                                        \
              ( (xthread)->th.th_team       ->t.t_threads[((xthread)->th.th_info.ds.ds_tid)]->th.th_current_task->td_icvs.nproc ) = \
              (xval) )

    #define set__max_active_levels( xthread, xval )                            \
            ( ( (xthread)->th.th_serial_team->t.t_threads[0]                                ->th.th_current_task->td_icvs.max_active_levels ) = \
              ( (xthread)->th.th_team       ->t.t_threads[((xthread)->th.th_info.ds.ds_tid)]->th.th_current_task->td_icvs.max_active_levels ) = \
              (xval) )

    #define set__sched( xthread, xval )                            \
            ( ( (xthread)->th.th_serial_team->t.t_threads[0]                                ->th.th_current_task->td_icvs.sched ) = \
              ( (xthread)->th.th_team       ->t.t_threads[((xthread)->th.th_info.ds.ds_tid)]->th.th_current_task->td_icvs.sched ) = \
              (xval) )

#if OMP_40_ENABLED

    #define set__proc_bind( xthread, xval )                          \
            (                                                        \
              ( (xthread)->th.th_team       ->t.t_threads[((xthread)->th.th_info.ds.ds_tid)]->th.th_current_task->td_icvs.proc_bind ) = \
              (xval) )

    #define get__proc_bind( xthread ) \
            ( (xthread)->th.th_team       ->t.t_threads[((xthread)->th.th_info.ds.ds_tid)]->th.th_current_task->td_icvs.proc_bind )

#endif /* OMP_40_ENABLED */

#else

    #define get__blocktime( xteam, xtid )    ((xteam)->t.t_set_blocktime[   (xtid)])
    #define get__bt_set( xteam, xtid )       ((xteam)->t.t_set_bt_set[      (xtid)])
    #define get__bt_intervals( xteam, xtid ) ((xteam)->t.t_set_bt_intervals[(xtid)])

    #define set__nested( xthread, xval )                            \
            ( ( (xthread)->th.th_serial_team->t.t_set_nested[0] ) = \
              ( (xthread)->th.th_team->t.t_set_nested[((xthread)->th.th_info.ds.ds_tid)] ) = \
              (xval) )
    #define get__nested( xthread ) \
            ( ( (xthread)->th.th_team->t.t_set_nested[((xthread)->th.th_info.ds.ds_tid)] ) \
            ? (FTN_TRUE) : (FTN_FALSE) )

    #define set__dynamic( xthread, xval )                            \
            ( ( (xthread)->th.th_serial_team->t.t_set_dynamic[0] ) = \
              ( (xthread)->th.th_team->t.t_set_dynamic[((xthread)->th.th_info.ds.ds_tid)] ) = \
              (xval) )
    #define get__dynamic( xthread ) \
            ( ( (xthread)->th.th_team->t.t_set_dynamic[((xthread)->th.th_info.ds.ds_tid)] ) \
            ? (FTN_TRUE) : (FTN_FALSE) )

    #define set__nproc( xthread, xval )                            \
            ( ( (xthread)->th.th_serial_team->t.t_set_nproc[0] ) = \
              ( (xthread)->th.th_team->t.t_set_nproc[((xthread)->th.th_info.ds.ds_tid)] ) = \
              (xval) )

    #define set__nproc_p( xthread, xval )                                                   \
            ( ( (xthread)->th.th_team->t.t_set_nproc[((xthread)->th.th_info.ds.ds_tid)] ) = (xval) )

    #define set__blocktime_team( xteam, xtid, xval ) \
            ( ( (xteam)->t.t_set_blocktime[(xtid)] ) = (xval) )

    #define set__bt_intervals_team( xteam, xtid, xval ) \
            ( ( (xteam)->t.t_set_bt_intervals[(xtid)] ) = (xval) )

    #define set__bt_set_team( xteam, xtid, xval ) \
            ( ( (xteam)->t.t_set_bt_set[(xtid)] ) = (xval) )

    #define get__nested_2(xteam,xtid)  ( (xteam)->t.t_set_nested[(xtid)] )
    #define get__dynamic_2(xteam,xtid) ( (xteam)->t.t_set_dynamic[(xtid)] )
    #define get__nproc_2(xteam,xtid)   ( (xteam)->t.t_set_nproc[(xtid)] )
    #define get__sched_2(xteam,xtid)   ( (xteam)->t.t_set_sched[(xtid)] )


#endif

#if OMP_30_ENABLED
/* ------------------------------------------------------------------------ */
// OpenMP tasking data structures
//

typedef enum kmp_tasking_mode {
    tskm_immediate_exec = 0,
    tskm_extra_barrier = 1,
    tskm_task_teams = 2,
    tskm_max = 2
} kmp_tasking_mode_t;

extern kmp_tasking_mode_t __kmp_tasking_mode;         /* determines how/when to execute tasks */
extern kmp_int32 __kmp_task_stealing_constraint;

/* NOTE: kmp_taskdata_t and kmp_task_t structures allocated in single block with taskdata first */
#define KMP_TASK_TO_TASKDATA(task)     (((kmp_taskdata_t *) task) - 1)
#define KMP_TASKDATA_TO_TASK(taskdata) (kmp_task_t *) (taskdata + 1)

// The tt_found_tasks flag is a signal to all threads in the team that tasks were spawned and
// queued since the previous barrier release.
// State is used to alternate task teams for successive barriers
#define KMP_TASKING_ENABLED(task_team,state) \
    ((TCR_SYNC_4((task_team)->tt.tt_found_tasks) == TRUE) && \
     (TCR_4((task_team)->tt.tt_state)       == (state)))
/*!
@ingroup BASIC_TYPES
@{
*/

/*!
 */
typedef kmp_int32 (* kmp_routine_entry_t)( kmp_int32, void * );

/*  sizeof_kmp_task_t passed as arg to kmpc_omp_task call  */
/*!
 */
typedef struct kmp_task {                   /* GEH: Shouldn't this be aligned somehow? */
    void *              shareds;            /**< pointer to block of pointers to shared vars   */
    kmp_routine_entry_t routine;            /**< pointer to routine to call for executing task */
    kmp_int32           part_id;            /**< part id for the task                          */
    /*  private vars  */
} kmp_task_t;
/*!
@}
*/

#if OMP_40_ENABLED
typedef struct kmp_taskgroup {
    kmp_uint32            count;   // number of allocated and not yet complete tasks
    struct kmp_taskgroup *parent;  // parent taskgroup
} kmp_taskgroup_t;
#endif

#ifdef BUILD_TIED_TASK_STACK

/* Tied Task stack definitions */
typedef struct kmp_stack_block {
    kmp_taskdata_t *          sb_block[ TASK_STACK_BLOCK_SIZE ];
    struct kmp_stack_block *  sb_next;
    struct kmp_stack_block *  sb_prev;
} kmp_stack_block_t;

typedef struct kmp_task_stack {
    kmp_stack_block_t         ts_first_block;  // first block of stack entries
    kmp_taskdata_t **         ts_top;          // pointer to the top of stack
    kmp_int32                 ts_entries;      // number of entries on the stack
} kmp_task_stack_t;

#endif // BUILD_TIED_TASK_STACK

typedef struct kmp_tasking_flags {          /* Total struct must be exactly 32 bits */
    /* Compiler flags */                    /* Total compiler flags must be 16 bits */
    unsigned tiedness    : 1;               /* task is either tied (1) or untied (0) */
    unsigned final       : 1;               /* task is final(1) so execute immediately */
    unsigned merged_if0  : 1;               /* no __kmpc_task_{begin/complete}_if0 calls in if0 code path */
    unsigned reserved13  : 13;              /* reserved for compiler use */

    /* Library flags */                     /* Total library flags must be 16 bits */
    unsigned tasktype    : 1;               /* task is either explicit(1) or implicit (0) */
    unsigned task_serial : 1;               /* this task is executed immediately (1) or deferred (0) */
    unsigned tasking_ser : 1;               /* all tasks in team are either executed immediately (1) or may be deferred (0) */
    unsigned team_serial : 1;               /* entire team is serial (1) [1 thread] or parallel (0) [>= 2 threads] */
                                            /* If either team_serial or tasking_ser is set, task team may be NULL */
    /* Task State Flags: */
    unsigned started     : 1;               /* 1==started, 0==not started     */
    unsigned executing   : 1;               /* 1==executing, 0==not executing */
    unsigned complete    : 1;               /* 1==complete, 0==not complete   */
    unsigned freed       : 1;               /* 1==freed, 0==allocateed        */
    unsigned native      : 1;               /* 1==gcc-compiled task, 0==intel */
    unsigned reserved31  : 7;               /* reserved for library use */

} kmp_tasking_flags_t;


struct kmp_taskdata {                                 /* aligned during dynamic allocation       */
    kmp_int32               td_task_id;               /* id, assigned by debugger                */
    kmp_tasking_flags_t     td_flags;                 /* task flags                              */
    kmp_team_t *            td_team;                  /* team for this task                      */
    kmp_info_p *            td_alloc_thread;          /* thread that allocated data structures   */
                                                      /* Currently not used except for perhaps IDB */
    kmp_taskdata_t *        td_parent;                /* parent task                             */
    kmp_int32               td_level;                 /* task nesting level                      */
    ident_t *               td_ident;                 /* task identifier                         */
                            // Taskwait data.
    ident_t *               td_taskwait_ident;
    kmp_uint32              td_taskwait_counter;
    kmp_int32               td_taskwait_thread;       /* gtid + 1 of thread encountered taskwait */
    kmp_internal_control_t  td_icvs;                  /* Internal control variables for the task */
    volatile kmp_uint32     td_allocated_child_tasks;  /* Child tasks (+ current task) not yet deallocated */
    volatile kmp_uint32     td_incomplete_child_tasks; /* Child tasks not yet complete */
#if OMP_40_ENABLED
    kmp_taskgroup_t *       td_taskgroup;         // Each task keeps pointer to its current taskgroup
#endif
    _Quad                   td_dummy;             // Align structure 16-byte size since allocated just before kmp_task_t
}; // struct kmp_taskdata

// Make sure padding above worked
KMP_BUILD_ASSERT( sizeof(kmp_taskdata_t) % sizeof(void *) == 0 );

// Data for task team but per thread
typedef struct kmp_base_thread_data {
    kmp_info_p *            td_thr;                // Pointer back to thread info
                                                   // Used only in __kmp_execute_tasks, maybe not avail until task is queued?
    kmp_bootstrap_lock_t    td_deque_lock;         // Lock for accessing deque
    kmp_taskdata_t **       td_deque;              // Deque of tasks encountered by td_thr, dynamically allocated
    kmp_uint32              td_deque_head;         // Head of deque (will wrap)
    kmp_uint32              td_deque_tail;         // Tail of deque (will wrap)
    kmp_int32               td_deque_ntasks;       // Number of tasks in deque
                                                   // GEH: shouldn't this be volatile since used in while-spin?
    kmp_int32               td_deque_last_stolen;  // Thread number of last successful steal
#ifdef BUILD_TIED_TASK_STACK
    kmp_task_stack_t        td_susp_tied_tasks;    // Stack of suspended tied tasks for task scheduling constraint
#endif // BUILD_TIED_TASK_STACK
} kmp_base_thread_data_t;

typedef union KMP_ALIGN_CACHE kmp_thread_data {
    kmp_base_thread_data_t  td;
    double                  td_align;       /* use worst case alignment */
    char                    td_pad[ KMP_PAD(kmp_base_thread_data_t, CACHE_LINE) ];
} kmp_thread_data_t;


// Data for task teams which are used when tasking is enabled for the team
typedef struct kmp_base_task_team {
    kmp_bootstrap_lock_t    tt_threads_lock;       /* Lock used to allocate per-thread part of task team */
                                                   /* must be bootstrap lock since used at library shutdown*/
    kmp_task_team_t *       tt_next;               /* For linking the task team free list */
    kmp_thread_data_t *     tt_threads_data;       /* Array of per-thread structures for task team */
                                                   /* Data survives task team deallocation */
    kmp_int32               tt_found_tasks;        /* Have we found tasks and queued them while executing this team? */
                                                   /* TRUE means tt_threads_data is set up and initialized */
    kmp_int32               tt_nproc;              /* #threads in team           */
    kmp_int32               tt_max_threads;        /* number of entries allocated for threads_data array */

    KMP_ALIGN_CACHE
    volatile kmp_uint32     tt_unfinished_threads; /* #threads still active      */

    KMP_ALIGN_CACHE
    volatile kmp_uint32     tt_active;             /* is the team still actively executing tasks */

    KMP_ALIGN_CACHE
    volatile kmp_uint32     tt_ref_ct;             /* #threads accessing struct  */
                                                   /* (not incl. master)         */
    kmp_int32               tt_state;              /* alternating 0/1 for task team identification */
                                                   /* Note: VERY sensitive to padding! */
} kmp_base_task_team_t;

union KMP_ALIGN_CACHE kmp_task_team {
    kmp_base_task_team_t tt;
    double               tt_align;       /* use worst case alignment */
    char                 tt_pad[ KMP_PAD(kmp_base_task_team_t, CACHE_LINE) ];
};

#endif  // OMP_30_ENABLED

#if ( USE_FAST_MEMORY == 3 ) || ( USE_FAST_MEMORY == 5 )
// Free lists keep same-size free memory slots for fast memory allocation routines
typedef struct kmp_free_list {
    void             *th_free_list_self;   // Self-allocated tasks free list
    void             *th_free_list_sync;   // Self-allocated tasks stolen/returned by other threads
    void             *th_free_list_other;  // Non-self free list (to be returned to owner's sync list)
} kmp_free_list_t;
#endif

/* ------------------------------------------------------------------------ */
// OpenMP thread data structures
//

typedef struct KMP_ALIGN_CACHE kmp_base_info {
/*
 * Start with the readonly data which is cache aligned and padded.
 * this is written before the thread starts working by the master.
 * (uber masters may update themselves later)
 * (usage does not consider serialized regions)
 */
    kmp_desc_t        th_info;
    kmp_team_p       *th_team;       /* team we belong to */
    kmp_root_p       *th_root;       /* pointer to root of task hierarchy */
    kmp_info_p       *th_next_pool;  /* next available thread in the pool */
    kmp_disp_t       *th_dispatch;   /* thread's dispatch data */
    int               th_in_pool;    /* in thread pool (32 bits for TCR/TCW) */

    /* The following are cached from the team info structure */
    /* TODO use these in more places as determined to be needed via profiling */
    int               th_team_nproc;      /* number of threads in a team */
    kmp_info_p       *th_team_master;     /* the team's master thread */
    int               th_team_serialized; /* team is serialized */

    /* The blocktime info is copied from the team struct to the thread sruct */
    /* at the start of a barrier, and the values stored in the team are used */
    /* at points in the code where the team struct is no longer guaranteed   */
    /* to exist (from the POV of worker threads).                            */
    int               th_team_bt_intervals;
    int               th_team_bt_set;


#if KMP_OS_WINDOWS || KMP_OS_LINUX
    kmp_affin_mask_t  *th_affin_mask; /* thread's current affinity mask */
#endif


/*
 * The data set by the master at reinit, then R/W by the worker
 */
    KMP_ALIGN_CACHE int     th_set_nproc;  /* if > 0, then only use this request for the next fork */
#if OMP_40_ENABLED
    kmp_proc_bind_t         th_set_proc_bind; /* if != proc_bind_default, use request for next fork */
# if (KMP_OS_WINDOWS || KMP_OS_LINUX)
    int                     th_current_place; /* place currently bound to */
    int                     th_new_place;     /* place to bind to in par reg */
    int                     th_first_place;   /* first place in partition */
    int                     th_last_place;    /* last place in partition */
# endif
#endif
    kmp_local_t             th_local;
    struct private_common  *th_pri_head;

/*
 * Now the data only used by the worker (after initial allocation)
 */
    /* TODO the first serial team should actually be stored in the info_t
     * structure.  this will help reduce initial allocation overhead */
    KMP_ALIGN_CACHE kmp_team_p *th_serial_team; /*serialized team held in reserve*/
/* The following are also read by the master during reinit */
    struct common_table    *th_pri_common;

    volatile kmp_uint32     th_spin_here;   /* thread-local location for spinning */
                                            /* while awaiting queuing lock acquire */

    volatile kmp_uint32    *th_sleep_loc;

/*
 * Two variables used for consistency check - struct cons_header *th_cons and inte th_first moved below
 * from here in order to avoid performance regression
*/
    ident_t          *th_ident;
    unsigned         th_x;                     // Random number generator data
    unsigned         th_a;                     // Random number generator data

#if OMP_30_ENABLED
/*
 * Tasking-related data for the thread
 */
    kmp_task_team_t    * th_task_team;           // Task team struct
    kmp_taskdata_t     * th_current_task;        // Innermost Task being executed
    kmp_uint8            th_task_state;          // alternating 0/1 for task team identification
#endif  // OMP_30_ENABLED

    /*
     * More stuff for keeping track of active/sleeping threads
     * (this part is written by the worker thread)
     */
    kmp_uint8            th_active_in_pool;      // included in count of
                                                 // #active threads in pool
    int                  th_active;              // ! sleeping
                                                 // 32 bits for TCR/TCW


    struct cons_header * th_cons;
    int                  th_first;

/*
 * Add the syncronizing data which is cache aligned and padded.
 */
    KMP_ALIGN_CACHE kmp_balign_t      th_bar[ bs_last_barrier ];

    KMP_ALIGN_CACHE volatile     kmp_int32    th_next_waiting;  /* gtid+1 of next thread on lock wait queue, 0 if none */

#if ( USE_FAST_MEMORY == 3 ) || ( USE_FAST_MEMORY == 5 )
    #define NUM_LISTS 4
    kmp_free_list_t   th_free_lists[NUM_LISTS];   // Free lists for fast memory allocation routines
#endif

#if KMP_OS_WINDOWS
    kmp_win32_cond_t  th_suspend_cv;
    kmp_win32_mutex_t th_suspend_mx;
    int               th_suspend_init;
#endif
#if KMP_OS_UNIX
    kmp_cond_align_t  th_suspend_cv;
    kmp_mutex_align_t th_suspend_mx;
    int               th_suspend_init_count;
#endif

} kmp_base_info_t;

typedef union KMP_ALIGN_CACHE kmp_info {
    double          th_align;        /* use worst case alignment */
    char            th_pad[ KMP_PAD(kmp_base_info_t, CACHE_LINE) ];
    kmp_base_info_t th;
} kmp_info_t;

/* ------------------------------------------------------------------------ */
// OpenMP thread team data structures
//
typedef struct kmp_base_data {
    volatile kmp_uint32 t_value;
} kmp_base_data_t;

typedef union KMP_ALIGN_CACHE kmp_sleep_team {
    double              dt_align;        /* use worst case alignment */
    char                dt_pad[ KMP_PAD(kmp_base_data_t, CACHE_LINE) ];
    kmp_base_data_t     dt;
} kmp_sleep_team_t;

typedef union KMP_ALIGN_CACHE kmp_ordered_team {
    double              dt_align;        /* use worst case alignment */
    char                dt_pad[ KMP_PAD(kmp_base_data_t, CACHE_LINE) ];
    kmp_base_data_t     dt;
} kmp_ordered_team_t;

typedef int     (*launch_t)( int gtid );

/* Minimum number of ARGV entries to malloc if necessary */
#define KMP_MIN_MALLOC_ARGV_ENTRIES     100

#if KMP_MIC && OMP_30_ENABLED
# define KMP_BARRIER_ICV_PULL   1
#else
# define KMP_BARRIER_ICV_PULL   0
#endif

#if (KMP_PERF_V106 == KMP_ON)
//
// Set up how many argv pointers will fit in cache lines containing
// *t_inline_argv. Historically, we have supported at least 96 bytes.
//
// Using a larger value for more space between the master write/worker read
// section and read/write by all section seems to buy more performance
// on EPCC PARALLEL.
//
//# define KMP_INLINE_ARGV_BYTES          ( 2 * CACHE_LINE )
# if KMP_BARRIER_ICV_PULL
#  define KMP_INLINE_ARGV_BYTES          192
//#  define KMP_INLINE_ARGV_BYTES         ( 2 * CACHE_LINE - ( ( 5 * KMP_PTR_SKIP + 10 * sizeof(int) + sizeof(kmp_int64) ) % CACHE_LINE ) )
# elif KMP_ARCH_X86 || KMP_ARCH_X86_64
#  define KMP_INLINE_ARGV_BYTES         ( 4 * CACHE_LINE - ( ( 3 * KMP_PTR_SKIP + 2 * sizeof(int) + 2 * sizeof(kmp_int8) + sizeof(kmp_int16) + sizeof(kmp_uint32) ) % CACHE_LINE ) )
# else
#  define KMP_INLINE_ARGV_BYTES         ( 2 * CACHE_LINE - ( ( 3 * KMP_PTR_SKIP + 2 * sizeof(int) ) % CACHE_LINE ) )
# endif
# define KMP_INLINE_ARGV_ENTRIES        ( KMP_INLINE_ARGV_BYTES / KMP_PTR_SKIP )
#endif

typedef struct KMP_ALIGN_CACHE kmp_base_team {
/*
 * Syncronization Data
 */
    KMP_ALIGN_CACHE kmp_ordered_team_t       t_ordered;
    kmp_balign_team_t        t_bar[ bs_last_barrier ];

    /* count of single directive encountered by team */
    volatile int             t_construct;
    kmp_lock_t               t_single_lock;  /* team specific lock */

/*
 * Master only
 */
    KMP_ALIGN_CACHE int      t_master_tid;   /* tid of master in parent team */
    int                      t_master_this_cons; /* "this_construct" single counter of master in parent team */
    int                      t_master_last_cons; /* "last_construct" single counter of master in parent team */
    ident_t                 *t_ident;        /* if volatile, have to change too much other crud to volatile too */
    kmp_team_p              *t_parent;       /* parent team */
    kmp_team_p              *t_next_pool;    /* next free team in the team pool */
    kmp_disp_t              *t_dispatch;     /* thread's dispatch data */
#if OMP_30_ENABLED
    kmp_task_team_t         *t_task_team;    /* Task team struct */
#endif /* OMP_30_ENABLED */
#if OMP_40_ENABLED
    kmp_proc_bind_t          t_proc_bind;    /* bind type for par region */
#endif // OMP_40_ENABLED

/*
 * Master write, workers read
 */
    KMP_ALIGN_CACHE
    void                     **t_argv;
    int                      t_argc;
#if (KMP_PERF_V106 == KMP_ON)
    /* swap cache lines  for t_nproc and t_max_argc */
    int                      t_nproc;        /* number of threads in team */
#else
    int                      t_max_argc;
#endif
    microtask_t              t_pkfn;
    launch_t                 t_invoke;       /* procedure to launch the microtask */

#if KMP_ARCH_X86 || KMP_ARCH_X86_64
    kmp_int8                 t_fp_control_saved;
    kmp_int8                 t_pad2b;
    kmp_int16                t_x87_fpu_control_word; /* FP control regs */
    kmp_uint32               t_mxcsr;                
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

#if KMP_BARRIER_ICV_PULL
   //
   // Note: Putting ICV's before the fp control info causes a very slight
   // ~1% improvement for EPCC parallel on fxe256lin01 / 256 threads, but
   // causes a 17% regression on fxe64lin01 / 64 threads.
   //
   kmp_internal_control_t    t_initial_icvs;
#endif // KMP_BARRIER_ICV_PULL

#if (KMP_PERF_V106 == KMP_ON)
    void                    *t_inline_argv[ KMP_INLINE_ARGV_ENTRIES ];
#endif

#if (KMP_PERF_V19 == KMP_ON)
    KMP_ALIGN_CACHE
#endif
    kmp_info_t             **t_threads;
#if (KMP_PERF_V106 == KMP_ON)
    /* swap cache lines  for t_nproc and t_max_argc */
    int                      t_max_argc;
#else
    int                      t_nproc;        /* number of threads in team */
#endif
    int                      t_max_nproc;    /* maximum threads this team can handle (this is dynamicly expandable) */
    int                      t_serialized;   /* levels deep of serialized teams */
    dispatch_shared_info_t  *t_disp_buffer;  /* buffers for dispatch system */
    int                      t_id;           // team's id, assigned by debugger.
#if OMP_30_ENABLED
    int                      t_level;        /* nested parallel level */
    int                      t_active_level; /* nested active parallel level */
    kmp_r_sched_t            t_sched;        /* run-time schedule for the team */
#endif // OMP_30_ENABLED
#if OMP_40_ENABLED && (KMP_OS_WINDOWS || KMP_OS_LINUX)
    int                      t_first_place;  /* first & last place in      */
    int                      t_last_place;   /* parent thread's partition. */
                                             /* Restore these values to    */
                                             /* master after par region.   */
#endif // OMP_40_ENABLED && (KMP_OS_WINDOWS || KMP_OS_LINUX)
#if KMP_MIC
    int                      t_size_changed; /* team size was changed?: 0 - no, 1 - yes, -1 - changed via omp_set_num_threads() call */
#endif

/*
 * Read/write by workers as well
 */
#if KMP_ARCH_X86 || KMP_ARCH_X86_64
    // Using CACHE_LINE=64 reduces memory footprint,
    //    but causes a big perf regression of epcc 'parallel' and 'barrier' on fxe256lin01.
    // This extra padding serves to fix the performance of epcc 'parallel' and 'barrier' when CACHE_LINE=64.
    // TODO: investigate more and get rid if this padding.
    char dummy_padding[1024];
#endif
    KMP_ALIGN_CACHE
#if OMP_30_ENABLED
    kmp_taskdata_t          *t_implicit_task_taskdata;  // Taskdata for the thread's implicit task
#else
    // Internal control variables for current thread team
    // TODO  Convert these fields to an array of  kmp_internal_control_t which simplifies parameter passing
    //       and also prevents performance degradation due to false sharing when all threads set a control var
    int                     *t_set_nproc;    /* internal control for # of threads for next
                                                parallel region (per thread) */
    int                     *t_set_nested;   /* internal control for nested parallelism (per thread) */
    int                     *t_set_dynamic;  /* internal control for dynamic adjustment of threads (per thread) */
    int                     *t_set_blocktime; /* internal control for blocktime */
    int                     *t_set_bt_intervals; /* internal control for blocktime intervals */
    int                     *t_set_bt_set;   /* internal control for whether blocktime is explicitly set */
#endif // OMP_30_ENABLED

    kmp_internal_control_t  *t_control_stack_top;  /* internal control stack for additional nested teams.
                                                      for SERIALIZED teams nested 2 or more levels deep */

    int                      t_master_active;/* save on fork, restore on join */
    kmp_taskq_t              t_taskq;        /* this team's task queue */
    void                    *t_copypriv_data;  /* team specific pointer to copyprivate data array */
    kmp_uint32               t_copyin_counter; 
} kmp_base_team_t;

union KMP_ALIGN_CACHE kmp_team {
    kmp_base_team_t     t;
    double              t_align;       /* use worst case alignment */
    char                t_pad[ KMP_PAD(kmp_base_team_t, CACHE_LINE) ];
};


typedef union KMP_ALIGN_CACHE kmp_time_global {
    double              dt_align;        /* use worst case alignment */
    char                dt_pad[ KMP_PAD(kmp_base_data_t, CACHE_LINE) ];
    kmp_base_data_t     dt;
} kmp_time_global_t;

typedef struct kmp_base_global {
    /* cache-aligned */
    kmp_time_global_t   g_time;

    /* non cache-aligned */
    volatile int        g_abort;
    volatile int        g_done;

    int                 g_dynamic;
    enum dynamic_mode   g_dynamic_mode;

} kmp_base_global_t;

typedef union KMP_ALIGN_CACHE kmp_global {
    kmp_base_global_t   g;
    double              g_align;        /* use worst case alignment */
    char                g_pad[ KMP_PAD(kmp_base_global_t, CACHE_LINE) ];
} kmp_global_t;


typedef struct kmp_base_root {
    // TODO: GEH - combine r_active with r_in_parallel then r_active == (r_in_parallel>= 0)
    // TODO: GEH - then replace r_active with t_active_levels if we can to reduce the synch
    //             overhead or keeping r_active

    volatile int        r_active;       /* TRUE if some region in a nest has > 1 thread */
                                        // GEH: This is misnamed, should be r_in_parallel
    volatile int        r_nested;       // TODO: GEH - This is unused, just remove it entirely.
    int                 r_in_parallel;  /* keeps a count of active parallel regions per root */
                                        // GEH: This is misnamed, should be r_active_levels
    kmp_team_t         *r_root_team;
    kmp_team_t         *r_hot_team;
    kmp_info_t         *r_uber_thread;
    kmp_lock_t          r_begin_lock;
    volatile int        r_begin;
    int                 r_blocktime; /* blocktime for this root and descendants */
} kmp_base_root_t;

typedef union KMP_ALIGN_CACHE kmp_root {
    kmp_base_root_t     r;
    double              r_align;        /* use worst case alignment */
    char                r_pad[ KMP_PAD(kmp_base_root_t, CACHE_LINE) ];
} kmp_root_t;

struct fortran_inx_info {
    kmp_int32   data;
};

/* ------------------------------------------------------------------------ */

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

extern int      __kmp_settings;
extern int      __kmp_duplicate_library_ok;
extern int      __kmp_forkjoin_frames;
extern PACKED_REDUCTION_METHOD_T __kmp_force_reduction_method;
extern int      __kmp_determ_red;

#ifdef KMP_DEBUG
extern int      kmp_a_debug;
extern int      kmp_b_debug;
extern int      kmp_c_debug;
extern int      kmp_d_debug;
extern int      kmp_e_debug;
extern int      kmp_f_debug;
#endif /* KMP_DEBUG */

/* For debug information logging using rotating buffer */
#define KMP_DEBUG_BUF_LINES_INIT        512
#define KMP_DEBUG_BUF_LINES_MIN         1

#define KMP_DEBUG_BUF_CHARS_INIT        128
#define KMP_DEBUG_BUF_CHARS_MIN         2

extern int     __kmp_debug_buf;            /* TRUE means use buffer, FALSE means print to stderr */
extern int     __kmp_debug_buf_lines;      /* How many lines of debug stored in buffer */
extern int     __kmp_debug_buf_chars;      /* How many characters allowed per line in buffer */
extern int     __kmp_debug_buf_atomic;     /* TRUE means use atomic update of buffer entry pointer */

extern char   *__kmp_debug_buffer;         /* Debug buffer itself */
extern int     __kmp_debug_count;          /* Counter for number of lines printed in buffer so far */
extern int     __kmp_debug_buf_warn_chars; /* Keep track of char increase recommended in warnings */
/* end rotating debug buffer */

extern int      __kmp_par_range;           /* +1 => only go par for constructs in range */

#define KMP_PAR_RANGE_ROUTINE_LEN       1024
extern char     __kmp_par_range_routine[KMP_PAR_RANGE_ROUTINE_LEN];
#define KMP_PAR_RANGE_FILENAME_LEN      1024
extern char     __kmp_par_range_filename[KMP_PAR_RANGE_FILENAME_LEN];
extern int      __kmp_par_range_lb;
extern int      __kmp_par_range_ub;

/* For printing out dynamic storage map for threads and teams */
extern int      __kmp_storage_map;         /* True means print storage map for threads and teams */
extern int      __kmp_storage_map_verbose; /* True means storage map includes placement info */
extern int      __kmp_storage_map_verbose_specified;

extern kmp_cpuinfo_t    __kmp_cpuinfo;
extern kmp_uint64       __kmp_cpu_frequency;
    // CPU frequency, in Hz. Set by __kmp_runtime_initialize(). 0 means "is not set yet",
    // ~ 0 signals an errror.

extern volatile int __kmp_init_serial;
extern volatile int __kmp_init_gtid;
extern volatile int __kmp_init_common;
extern volatile int __kmp_init_middle;
extern volatile int __kmp_init_parallel;
extern volatile int __kmp_init_monitor;
extern volatile int __kmp_init_user_locks;
extern int __kmp_init_counter;
extern int __kmp_root_counter;
extern int __kmp_version;

/* list of address of allocated caches for commons */
extern kmp_cached_addr_t *__kmp_threadpriv_cache_list;

/* Barrier algorithm types and options */
extern kmp_uint32    __kmp_barrier_gather_bb_dflt;
extern kmp_uint32    __kmp_barrier_release_bb_dflt;
extern kmp_bar_pat_e __kmp_barrier_gather_pat_dflt;
extern kmp_bar_pat_e __kmp_barrier_release_pat_dflt;
extern kmp_uint32    __kmp_barrier_gather_branch_bits  [ bs_last_barrier ];
extern kmp_uint32    __kmp_barrier_release_branch_bits [ bs_last_barrier ];
extern kmp_bar_pat_e __kmp_barrier_gather_pattern      [ bs_last_barrier ];
extern kmp_bar_pat_e __kmp_barrier_release_pattern     [ bs_last_barrier ];
extern char const   *__kmp_barrier_branch_bit_env_name [ bs_last_barrier ];
extern char const   *__kmp_barrier_pattern_env_name    [ bs_last_barrier ];
extern char const   *__kmp_barrier_type_name           [ bs_last_barrier ];
extern char const   *__kmp_barrier_pattern_name        [ bp_last_bar ];

/* Global Locks */
extern kmp_bootstrap_lock_t __kmp_initz_lock;     /* control initialization */
extern kmp_bootstrap_lock_t __kmp_forkjoin_lock;  /* control fork/join access and load calculation if rml is used*/
extern kmp_bootstrap_lock_t __kmp_exit_lock;      /* exit() is not always thread-safe */
extern kmp_bootstrap_lock_t __kmp_monitor_lock;   /* control monitor thread creation */
extern kmp_bootstrap_lock_t __kmp_tp_cached_lock; /* used for the hack to allow threadprivate cache and __kmp_threads expansion to co-exist */

extern kmp_lock_t __kmp_global_lock;    /* control OS/global access  */
extern kmp_queuing_lock_t __kmp_dispatch_lock;  /* control dispatch access  */
extern kmp_lock_t __kmp_debug_lock;     /* control I/O access for KMP_DEBUG */

/* used for yielding spin-waits */
extern unsigned int __kmp_init_wait;    /* initial number of spin-tests   */
extern unsigned int __kmp_next_wait;    /* susequent number of spin-tests */

extern enum library_type __kmp_library;

extern enum sched_type  __kmp_sched;    /* default runtime scheduling */
extern enum sched_type  __kmp_static;   /* default static scheduling method */
extern enum sched_type  __kmp_guided;   /* default guided scheduling method */
#if OMP_30_ENABLED
extern enum sched_type  __kmp_auto;     /* default auto scheduling method */
#endif // OMP_30_ENABLED
extern int              __kmp_chunk;    /* default runtime chunk size */

extern size_t     __kmp_stksize;        /* stack size per thread         */
extern size_t     __kmp_monitor_stksize;/* stack size for monitor thread */
extern size_t     __kmp_stkoffset;      /* stack offset per thread       */

extern size_t     __kmp_malloc_pool_incr; /* incremental size of pool for kmp_malloc() */
extern int        __kmp_env_chunk;      /* was KMP_CHUNK specified?     */
extern int        __kmp_env_stksize;    /* was KMP_STACKSIZE specified? */
extern int        __kmp_env_omp_stksize;/* was OMP_STACKSIZE specified? */
extern int        __kmp_env_all_threads;    /* was KMP_ALL_THREADS or KMP_MAX_THREADS specified? */
extern int        __kmp_env_omp_all_threads;/* was OMP_THREAD_LIMIT specified? */
extern int        __kmp_env_blocktime;  /* was KMP_BLOCKTIME specified? */
extern int        __kmp_env_checks;     /* was KMP_CHECKS specified?    */
extern int        __kmp_env_consistency_check;     /* was KMP_CONSISTENCY_CHECK specified?    */
extern int        __kmp_generate_warnings; /* should we issue warnings? */
extern int        __kmp_reserve_warn;   /* have we issued reserve_threads warning? */

#ifdef DEBUG_SUSPEND
extern int        __kmp_suspend_count;  /* count inside __kmp_suspend() */
#endif

extern kmp_uint32 __kmp_yield_init;
extern kmp_uint32 __kmp_yield_next;
extern kmp_uint32 __kmp_yielding_on;
extern kmp_uint32 __kmp_yield_cycle;
extern kmp_int32  __kmp_yield_on_count;
extern kmp_int32  __kmp_yield_off_count;


/* ------------------------------------------------------------------------- */
extern int        __kmp_allThreadsSpecified;

extern size_t     __kmp_align_alloc;
/* following data protected by initialization routines */
extern int        __kmp_xproc;          /* number of processors in the system */
extern int        __kmp_avail_proc;      /* number of processors available to the process */
extern int        __kmp_sys_min_stksize; /* system-defined minimum stack size */
extern int        __kmp_sys_max_nth;    /* system-imposed maximum number of threads */
extern int        __kmp_max_nth;        /* maximum total number of concurrently-existing threads */
extern int        __kmp_threads_capacity; /* capacity of the arrays __kmp_threads and __kmp_root */
extern int        __kmp_dflt_team_nth;  /* default number of threads in a parallel region a la OMP_NUM_THREADS */
extern int        __kmp_dflt_team_nth_ub; /* upper bound on "" determined at serial initialization */
extern int        __kmp_tp_capacity;    /* capacity of __kmp_threads if threadprivate is used (fixed) */
extern int        __kmp_tp_cached;      /* whether threadprivate cache has been created (__kmpc_threadprivate_cached()) */
extern int        __kmp_dflt_nested;    /* nested parallelism enabled by default a la OMP_NESTED */
extern int        __kmp_dflt_blocktime; /* number of milliseconds to wait before blocking (env setting) */
extern int        __kmp_monitor_wakeups;/* number of times monitor wakes up per second */
extern int        __kmp_bt_intervals;   /* number of monitor timestamp intervals before blocking */
#ifdef KMP_ADJUST_BLOCKTIME
extern int        __kmp_zero_bt;        /* whether blocktime has been forced to zero */
#endif /* KMP_ADJUST_BLOCKTIME */
extern int        __kmp_ht_capable;     /* whether CPUs support Intel(R) Hyper-Threading Technology */
extern int        __kmp_ht_enabled;     /* whether Intel(R) Hyper-Threading Technology is enabled in OS */
extern int        __kmp_ncores;         /* Number of physical procs in HT machine */
extern int        __kmp_ht_log_per_phy; /* Maximum possible number of logical processors per package */
extern int        __kmp_nThreadsPerCore;/* Number of hyperthreads per core in HT machine. */
extern int        __kmp_abort_delay;    /* Number of millisecs to delay on abort for VTune */

extern int        __kmp_need_register_atfork_specified;
extern int        __kmp_need_register_atfork;/* At initialization, call pthread_atfork to install fork handler */
extern int        __kmp_gtid_mode;      /* Method of getting gtid, values:
                                           0 - not set, will be set at runtime
                                           1 - using stack search
                                           2 - dynamic TLS (pthread_getspecific(Linux* OS/OS X*) or TlsGetValue(Windows* OS))
                                           3 - static TLS (__declspec(thread) __kmp_gtid), Linux* OS .so only.
                                         */
extern int        __kmp_adjust_gtid_mode; /* If true, adjust method based on #threads */
#ifdef KMP_TDATA_GTID
#if KMP_OS_WINDOWS
extern __declspec(thread) int __kmp_gtid; /* This thread's gtid, if __kmp_gtid_mode == 3 */
#else
extern __thread int __kmp_gtid;
#endif /* KMP_OS_WINDOWS - workaround because Intel(R) Many Integrated Core compiler 20110316 doesn't accept __declspec */
#endif
extern int        __kmp_tls_gtid_min;   /* #threads below which use sp search for gtid */
extern int        __kmp_foreign_tp;     /* If true, separate TP var for each foreign thread */
#if KMP_ARCH_X86 || KMP_ARCH_X86_64
extern int        __kmp_inherit_fp_control; /* copy fp creg(s) parent->workers at fork */
extern kmp_int16  __kmp_init_x87_fpu_control_word; /* init thread's FP control reg */
extern kmp_uint32 __kmp_init_mxcsr;      /* init thread's mxscr */
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

#if OMP_30_ENABLED
extern int        __kmp_dflt_max_active_levels; /* max_active_levels for nested parallelism enabled by default a la OMP_MAX_ACTIVE_LEVELS */
#endif // OMP_30_ENABLED

# if KMP_OS_LINUX
extern enum clock_function_type __kmp_clock_function;
extern int __kmp_clock_function_param;
# endif /* KMP_OS_LINUX */

# ifdef USE_LOAD_BALANCE
extern double      __kmp_load_balance_interval;   /* Interval for the load balance algorithm */
# endif /* USE_LOAD_BALANCE */

// OpenMP 3.1 - Nested num threads array
struct kmp_nested_nthreads_t {
    int * nth;
    int   size;
    int   used;
};

extern struct kmp_nested_nthreads_t __kmp_nested_nth;

/* ------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------- */
/* the following are protected by the fork/join lock */
/* write: lock  read: anytime */
extern          kmp_info_t **__kmp_threads;      /* Descriptors for the threads */
/* read/write: lock */
extern volatile kmp_team_t  *     __kmp_team_pool;
extern volatile kmp_info_t  *     __kmp_thread_pool;

/* total number of threads reachable from some root thread including all root threads*/
extern volatile int __kmp_nth;
/* total number of threads reachable from some root thread including all root threads,
   and those in the thread pool */
extern volatile int __kmp_all_nth;
extern int __kmp_thread_pool_nth;
extern volatile int __kmp_thread_pool_active_nth;

extern kmp_root_t **__kmp_root;         /* root of thread hierarchy */
/* end data protected by fork/join lock */
/* --------------------------------------------------------------------------- */

extern kmp_global_t  __kmp_global;         /* global status */

extern kmp_info_t __kmp_monitor;
extern volatile kmp_uint32 __kmp_team_counter;      // Used by Debugging Support Library.
extern volatile kmp_uint32 __kmp_task_counter;      // Used by Debugging Support Library.

#define _KMP_GEN_ID( counter )                                         \
    (                                                                  \
        ~ 0                                                            \
    )



#define KMP_GEN_TASK_ID()    _KMP_GEN_ID( __kmp_task_counter )
#define KMP_GEN_TEAM_ID()    _KMP_GEN_ID( __kmp_team_counter )

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

extern void __kmp_print_storage_map_gtid( int gtid, void *p1, void* p2, size_t size, char const *format, ... );

extern void __kmp_serial_initialize( void );
extern void __kmp_middle_initialize( void );
extern void __kmp_parallel_initialize( void );

extern void __kmp_internal_begin( void );
extern void __kmp_internal_end_library( int gtid );
extern void __kmp_internal_end_thread( int gtid );
extern void __kmp_internal_end_atexit( void );
extern void __kmp_internal_end_fini( void );
extern void __kmp_internal_end_dtor( void );
extern void __kmp_internal_end_dest( void* );

extern int  __kmp_register_root( int initial_thread );
extern void __kmp_unregister_root( int gtid );

extern int  __kmp_ignore_mppbeg( void );
extern int  __kmp_ignore_mppend( void );

extern int  __kmp_enter_single( int gtid, ident_t *id_ref, int push_ws );
extern void __kmp_exit_single( int gtid );

extern void __kmp_parallel_deo( int *gtid_ref, int *cid_ref, ident_t *loc_ref );
extern void __kmp_parallel_dxo( int *gtid_ref, int *cid_ref, ident_t *loc_ref );


#ifdef USE_LOAD_BALANCE
extern int  __kmp_get_load_balance( int );
#endif

#ifdef BUILD_TV
extern void __kmp_tv_threadprivate_store( kmp_info_t *th, void *global_addr, void *thread_addr );
#endif

extern int  __kmp_get_global_thread_id( void );
extern int  __kmp_get_global_thread_id_reg( void );
extern void __kmp_exit_thread( int exit_status );
extern void __kmp_abort( char const * format, ... );
extern void __kmp_abort_thread( void );
extern void __kmp_abort_process( void );
extern void __kmp_warn( char const * format, ... );

extern void __kmp_set_num_threads( int new_nth, int gtid );

// Returns current thread (pointer to kmp_info_t). Current thread *must* be registered.
inline kmp_info_t * __kmp_entry_thread() 
{
      int gtid = __kmp_entry_gtid();

      return __kmp_threads[gtid];
}

#if OMP_30_ENABLED

extern void __kmp_set_max_active_levels( int gtid, int new_max_active_levels );
extern int  __kmp_get_max_active_levels( int gtid );
extern int  __kmp_get_ancestor_thread_num( int gtid, int level );
extern int  __kmp_get_team_size( int gtid, int level );
extern void __kmp_set_schedule( int gtid, kmp_sched_t new_sched, int chunk );
extern void __kmp_get_schedule( int gtid, kmp_sched_t * sched, int * chunk );

#endif // OMP_30_ENABLED

extern unsigned short __kmp_get_random( kmp_info_t * thread );
extern void __kmp_init_random( kmp_info_t * thread );

extern kmp_r_sched_t __kmp_get_schedule_global( void );
extern void __kmp_adjust_num_threads( int new_nproc );

extern void * ___kmp_allocate( size_t size KMP_SRC_LOC_DECL );
extern void * ___kmp_page_allocate( size_t size KMP_SRC_LOC_DECL );
extern void   ___kmp_free( void * ptr KMP_SRC_LOC_DECL );
#define __kmp_allocate( size )      ___kmp_allocate( (size) KMP_SRC_LOC_CURR )
#define __kmp_page_allocate( size ) ___kmp_page_allocate( (size) KMP_SRC_LOC_CURR )
#define __kmp_free( ptr )           ___kmp_free( (ptr) KMP_SRC_LOC_CURR )

#if USE_FAST_MEMORY
extern void * ___kmp_fast_allocate( kmp_info_t *this_thr, size_t size KMP_SRC_LOC_DECL );
extern void   ___kmp_fast_free( kmp_info_t *this_thr, void *ptr KMP_SRC_LOC_DECL );
extern void   __kmp_free_fast_memory( kmp_info_t *this_thr );
extern void   __kmp_initialize_fast_memory( kmp_info_t *this_thr );
#define __kmp_fast_allocate( this_thr, size ) ___kmp_fast_allocate( (this_thr), (size) KMP_SRC_LOC_CURR )
#define __kmp_fast_free( this_thr, ptr )      ___kmp_fast_free( (this_thr), (ptr) KMP_SRC_LOC_CURR )
#endif

extern void * ___kmp_thread_malloc( kmp_info_t *th, size_t size KMP_SRC_LOC_DECL );
extern void * ___kmp_thread_calloc( kmp_info_t *th, size_t nelem, size_t elsize KMP_SRC_LOC_DECL );
extern void * ___kmp_thread_realloc( kmp_info_t *th, void *ptr, size_t size KMP_SRC_LOC_DECL );
extern void   ___kmp_thread_free( kmp_info_t *th, void *ptr KMP_SRC_LOC_DECL );
#define __kmp_thread_malloc(  th, size )          ___kmp_thread_malloc(  (th), (size)            KMP_SRC_LOC_CURR )
#define __kmp_thread_calloc(  th, nelem, elsize ) ___kmp_thread_calloc(  (th), (nelem), (elsize) KMP_SRC_LOC_CURR )
#define __kmp_thread_realloc( th, ptr, size )     ___kmp_thread_realloc( (th), (ptr), (size)     KMP_SRC_LOC_CURR )
#define __kmp_thread_free(    th, ptr )           ___kmp_thread_free(    (th), (ptr)             KMP_SRC_LOC_CURR )

#define KMP_INTERNAL_MALLOC(sz)    malloc(sz)
#define KMP_INTERNAL_FREE(p)       free(p)
#define KMP_INTERNAL_REALLOC(p,sz) realloc((p),(sz))
#define KMP_INTERNAL_CALLOC(n,sz)  calloc((n),(sz))

extern void __kmp_push_num_threads( ident_t *loc, int gtid, int num_threads );

#if OMP_40_ENABLED
extern void __kmp_push_proc_bind( ident_t *loc, int gtid, kmp_proc_bind_t proc_bind );
#endif

extern void __kmp_yield( int cond );
extern void __kmp_release( kmp_info_t *target_thr, volatile kmp_uint *spin,
                           enum kmp_mem_fence_type fetchadd_fence );

extern void __kmpc_dispatch_init_4( ident_t *loc, kmp_int32 gtid,
    enum sched_type schedule, kmp_int32 lb, kmp_int32 ub, kmp_int32 st,
    kmp_int32 chunk );
extern void __kmpc_dispatch_init_4u( ident_t *loc, kmp_int32 gtid,
    enum sched_type schedule, kmp_uint32 lb, kmp_uint32 ub, kmp_int32 st,
    kmp_int32 chunk );
extern void __kmpc_dispatch_init_8( ident_t *loc, kmp_int32 gtid,
    enum sched_type schedule, kmp_int64 lb, kmp_int64 ub, kmp_int64 st,
    kmp_int64 chunk );
extern void __kmpc_dispatch_init_8u( ident_t *loc, kmp_int32 gtid,
    enum sched_type schedule, kmp_uint64 lb, kmp_uint64 ub, kmp_int64 st,
    kmp_int64 chunk );

extern int __kmpc_dispatch_next_4( ident_t *loc, kmp_int32 gtid,
    kmp_int32 *p_last, kmp_int32 *p_lb, kmp_int32 *p_ub, kmp_int32 *p_st );
extern int __kmpc_dispatch_next_4u( ident_t *loc, kmp_int32 gtid,
    kmp_int32 *p_last, kmp_uint32 *p_lb, kmp_uint32 *p_ub, kmp_int32 *p_st );
extern int __kmpc_dispatch_next_8( ident_t *loc, kmp_int32 gtid,
    kmp_int32 *p_last, kmp_int64 *p_lb, kmp_int64 *p_ub, kmp_int64 *p_st );
extern int __kmpc_dispatch_next_8u( ident_t *loc, kmp_int32 gtid,
    kmp_int32 *p_last, kmp_uint64 *p_lb, kmp_uint64 *p_ub, kmp_int64 *p_st );

extern void __kmpc_dispatch_fini_4( ident_t *loc, kmp_int32 gtid );
extern void __kmpc_dispatch_fini_8( ident_t *loc, kmp_int32 gtid );
extern void __kmpc_dispatch_fini_4u( ident_t *loc, kmp_int32 gtid );
extern void __kmpc_dispatch_fini_8u( ident_t *loc, kmp_int32 gtid );


#ifdef KMP_GOMP_COMPAT

extern void __kmp_aux_dispatch_init_4( ident_t *loc, kmp_int32 gtid,
    enum sched_type schedule, kmp_int32 lb, kmp_int32 ub, kmp_int32 st,
    kmp_int32 chunk, int push_ws );
extern void __kmp_aux_dispatch_init_4u( ident_t *loc, kmp_int32 gtid,
    enum sched_type schedule, kmp_uint32 lb, kmp_uint32 ub, kmp_int32 st,
    kmp_int32 chunk, int push_ws );
extern void __kmp_aux_dispatch_init_8( ident_t *loc, kmp_int32 gtid,
    enum sched_type schedule, kmp_int64 lb, kmp_int64 ub, kmp_int64 st,
    kmp_int64 chunk, int push_ws );
extern void __kmp_aux_dispatch_init_8u( ident_t *loc, kmp_int32 gtid,
    enum sched_type schedule, kmp_uint64 lb, kmp_uint64 ub, kmp_int64 st,
    kmp_int64 chunk, int push_ws );
extern void __kmp_aux_dispatch_fini_chunk_4( ident_t *loc, kmp_int32 gtid );
extern void __kmp_aux_dispatch_fini_chunk_8( ident_t *loc, kmp_int32 gtid );
extern void __kmp_aux_dispatch_fini_chunk_4u( ident_t *loc, kmp_int32 gtid );
extern void __kmp_aux_dispatch_fini_chunk_8u( ident_t *loc, kmp_int32 gtid );

#endif /* KMP_GOMP_COMPAT */


extern kmp_uint32 __kmp_eq_4(  kmp_uint32 value, kmp_uint32 checker );
extern kmp_uint32 __kmp_neq_4( kmp_uint32 value, kmp_uint32 checker );
extern kmp_uint32 __kmp_lt_4(  kmp_uint32 value, kmp_uint32 checker );
extern kmp_uint32 __kmp_ge_4(  kmp_uint32 value, kmp_uint32 checker );
extern kmp_uint32 __kmp_le_4(  kmp_uint32 value, kmp_uint32 checker );

extern kmp_uint32 __kmp_eq_8(  kmp_uint64 value, kmp_uint64 checker );
extern kmp_uint32 __kmp_neq_8( kmp_uint64 value, kmp_uint64 checker );
extern kmp_uint32 __kmp_lt_8(  kmp_uint64 value, kmp_uint64 checker );
extern kmp_uint32 __kmp_ge_8(  kmp_uint64 value, kmp_uint64 checker );
extern kmp_uint32 __kmp_le_8(  kmp_uint64 value, kmp_uint64 checker );

extern kmp_uint32 __kmp_wait_yield_4( kmp_uint32 volatile * spinner, kmp_uint32 checker, kmp_uint32 (*pred) (kmp_uint32, kmp_uint32), void * obj );
extern kmp_uint64 __kmp_wait_yield_8( kmp_uint64 volatile * spinner, kmp_uint64 checker, kmp_uint32 (*pred) (kmp_uint64, kmp_uint64), void * obj );

extern void __kmp_wait_sleep( kmp_info_t *this_thr, volatile kmp_uint *spinner, kmp_uint checker, kmp_int final_spin
);
extern void __kmp_infinite_loop( void );

extern void __kmp_cleanup( void );

#if KMP_HANDLE_SIGNALS
    extern int  __kmp_handle_signals;
    extern void __kmp_install_signals( int parallel_init );
    extern void __kmp_remove_signals( void );
#endif

extern void __kmp_clear_system_time( void );
extern void __kmp_read_system_time( double *delta );

extern void __kmp_check_stack_overlap( kmp_info_t *thr );

extern void __kmp_expand_host_name( char *buffer, size_t size );
extern void __kmp_expand_file_name( char *result, size_t rlen, char *pattern );

#if KMP_OS_WINDOWS
extern void __kmp_initialize_system_tick( void );  /* Initialize timer tick value */
#endif

extern void __kmp_runtime_initialize( void );  /* machine specific initialization */
extern void __kmp_runtime_destroy( void );

#if KMP_OS_LINUX || KMP_OS_WINDOWS
extern char *__kmp_affinity_print_mask(char *buf, int buf_len, kmp_affin_mask_t *mask);
extern void __kmp_affinity_initialize(void);
extern void __kmp_affinity_uninitialize(void);
extern void __kmp_affinity_set_init_mask(int gtid, int isa_root); /* set affinity according to KMP_AFFINITY */
#if OMP_40_ENABLED
extern void __kmp_affinity_set_place(int gtid);
#endif
extern void __kmp_change_thread_affinity_mask( int gtid, kmp_affin_mask_t *new_mask,
                                               kmp_affin_mask_t *old_mask );
extern void __kmp_affinity_determine_capable( const char *env_var );
extern int __kmp_aux_set_affinity(void **mask);
extern int __kmp_aux_get_affinity(void **mask);
extern int __kmp_aux_set_affinity_mask_proc(int proc, void **mask);
extern int __kmp_aux_unset_affinity_mask_proc(int proc, void **mask);
extern int __kmp_aux_get_affinity_mask_proc(int proc, void **mask);
extern void __kmp_balanced_affinity( int tid, int team_size );

#endif /* KMP_OS_LINUX || KMP_OS_WINDOWS */

#if KMP_OS_LINUX && (KMP_ARCH_X86 || KMP_ARCH_X86_64)

extern int __kmp_futex_determine_capable( void );

#endif // KMP_OS_LINUX && (KMP_ARCH_X86 || KMP_ARCH_X86_64)

extern void __kmp_gtid_set_specific( int gtid );
extern int  __kmp_gtid_get_specific( void );

extern double __kmp_read_cpu_time( void );

extern int  __kmp_read_system_info( struct kmp_sys_info *info );

extern void __kmp_create_monitor( kmp_info_t *th );

extern void *__kmp_launch_thread( kmp_info_t *thr );

extern void __kmp_create_worker( int gtid, kmp_info_t *th, size_t stack_size );

#if KMP_OS_WINDOWS
extern int  __kmp_still_running(kmp_info_t *th);
extern int  __kmp_is_thread_alive( kmp_info_t * th, DWORD *exit_val );
extern void __kmp_free_handle( kmp_thread_t tHandle );
#endif

extern void __kmp_reap_monitor( kmp_info_t *th );
extern void __kmp_reap_worker( kmp_info_t *th );
extern void __kmp_terminate_thread( int gtid );

extern void __kmp_suspend( int th_gtid, volatile kmp_uint *spinner, kmp_uint checker );
extern void __kmp_resume( int target_gtid, volatile kmp_uint *spinner );

extern void __kmp_elapsed( double * );
extern void __kmp_elapsed_tick( double * );

extern void __kmp_enable( int old_state );
extern void __kmp_disable( int *old_state );

extern void __kmp_thread_sleep( int millis );

extern void __kmp_common_initialize( void );
extern void __kmp_common_destroy( void );
extern void __kmp_common_destroy_gtid( int gtid );

#if KMP_OS_UNIX
extern void __kmp_register_atfork( void );
#endif
extern void __kmp_suspend_initialize( void );
extern void __kmp_suspend_uninitialize_thread( kmp_info_t *th );

extern kmp_info_t * __kmp_allocate_thread( kmp_root_t *root,
                                           kmp_team_t *team, int tid);
#if OMP_40_ENABLED
extern kmp_team_t * __kmp_allocate_team( kmp_root_t *root, int new_nproc, int max_nproc,
                                         kmp_proc_bind_t proc_bind,
                                         kmp_internal_control_t *new_icvs,
                                         int argc );
#elif OMP_30_ENABLED
extern kmp_team_t * __kmp_allocate_team( kmp_root_t *root, int new_nproc, int max_nproc,
                                         kmp_internal_control_t *new_icvs,
                                         int argc );
#else
extern kmp_team_t * __kmp_allocate_team( kmp_root_t *root, int new_nproc, int max_nproc,
                                         int new_set_nproc, int new_set_dynamic, int new_set_nested,
                                         int new_set_blocktime, int new_bt_intervals, int new_bt_set,
                                         int argc );
#endif // OMP_30_ENABLED
extern void __kmp_free_thread( kmp_info_t * );
extern void __kmp_free_team( kmp_root_t *, kmp_team_t * );
extern kmp_team_t * __kmp_reap_team( kmp_team_t * );

/* ------------------------------------------------------------------------ */

extern void __kmp_initialize_bget( kmp_info_t *th );
extern void __kmp_finalize_bget( kmp_info_t *th );

KMP_EXPORT void *kmpc_malloc( size_t size );
KMP_EXPORT void *kmpc_calloc( size_t nelem, size_t elsize );
KMP_EXPORT void *kmpc_realloc( void *ptr, size_t size );
KMP_EXPORT void  kmpc_free( void *ptr );

/* ------------------------------------------------------------------------ */
/* declarations for internal use */

extern int  __kmp_barrier( enum barrier_type bt, int gtid, int is_split,
                           size_t reduce_size, void *reduce_data, void (*reduce)(void *, void *) );
extern void __kmp_end_split_barrier ( enum barrier_type bt, int gtid );

extern int __kmp_fork_call( ident_t *loc, int gtid, int exec_master,
  kmp_int32 argc, microtask_t microtask, launch_t invoker,
/* TODO: revert workaround for Intel(R) 64 tracker #96 */
#if KMP_ARCH_X86_64 && KMP_OS_LINUX
                             va_list *ap
#else
                             va_list ap
#endif
                             );

extern void __kmp_join_call( ident_t *loc, int gtid );

extern void __kmp_internal_fork( ident_t *id, int gtid, kmp_team_t *team );
extern void __kmp_internal_join( ident_t *id, int gtid, kmp_team_t *team );
extern int __kmp_invoke_task_func( int gtid );
extern void __kmp_run_before_invoked_task( int gtid, int tid, kmp_info_t *this_thr, kmp_team_t *team );
extern void __kmp_run_after_invoked_task( int gtid, int tid, kmp_info_t *this_thr, kmp_team_t *team );

// should never have been exported
KMP_EXPORT int __kmpc_invoke_task_func( int gtid );

extern void __kmp_save_internal_controls( kmp_info_t * thread );
extern void __kmp_user_set_library (enum library_type arg);
extern void __kmp_aux_set_library (enum library_type arg);
extern void __kmp_aux_set_stacksize( size_t arg);
extern void __kmp_aux_set_blocktime (int arg, kmp_info_t *thread, int tid);
extern void __kmp_aux_set_defaults( char const * str, int len );

/* Functions below put here to call them from __kmp_aux_env_initialize() in kmp_settings.c */
void kmpc_set_blocktime (int arg);
void ompc_set_nested( int flag );
void ompc_set_dynamic( int flag );
void ompc_set_num_threads( int arg );

#if OMP_30_ENABLED
extern void __kmp_push_current_task_to_thread( kmp_info_t *this_thr,
                  kmp_team_t *team, int tid );
extern void __kmp_pop_current_task_from_thread( kmp_info_t *this_thr );
extern kmp_task_t* __kmp_task_alloc( ident_t *loc_ref, kmp_int32 gtid,
  kmp_tasking_flags_t *flags, size_t sizeof_kmp_task_t, size_t sizeof_shareds,
  kmp_routine_entry_t task_entry );
extern void __kmp_init_implicit_task( ident_t *loc_ref, kmp_info_t *this_thr,
                  kmp_team_t *team, int tid, int set_curr_task );

extern int  __kmp_execute_tasks( kmp_info_t *thread, kmp_int32 gtid, volatile kmp_uint *spinner,
                                 kmp_uint checker, int final_spin, int *thread_finished, 
                                 int c = 0 );
extern void __kmp_reap_task_teams( void );
extern void __kmp_unref_task_team( kmp_task_team_t *task_team, kmp_info_t *thread );
extern void __kmp_wait_to_unref_task_teams( void );
extern void __kmp_task_team_setup ( kmp_info_t *this_thr, kmp_team_t *team );
extern void __kmp_task_team_sync  ( kmp_info_t *this_thr, kmp_team_t *team );
extern void __kmp_task_team_wait  ( kmp_info_t *this_thr, kmp_team_t *team
);
extern void __kmp_tasking_barrier( kmp_team_t *team, kmp_info_t *thread, int gtid );

#endif // OMP_30_ENABLED

/* declarations in the assembler library for internal use */

/* 32-bit */
extern kmp_int32 __kmp_test_then_add32( volatile kmp_int32 *p, kmp_int32 d );
extern kmp_int32 __kmp_test_then_or32( volatile kmp_int32 *p, kmp_int32 d );
extern kmp_int32 __kmp_test_then_and32( volatile kmp_int32 *p, kmp_int32 d );
extern kmp_int64 __kmp_test_then_add64( volatile kmp_int64 *p, kmp_int64 d );
extern kmp_int64 __kmp_test_then_or64( volatile kmp_int64 *p, kmp_int64 d );
extern kmp_int64 __kmp_test_then_and64( volatile kmp_int64 *p, kmp_int64 d );

#define KMP_COMPARE_AND_STORE_ACQ8     __kmp_compare_and_store8
#define KMP_COMPARE_AND_STORE_REL8     __kmp_compare_and_store8
extern kmp_int8 __kmp_compare_and_store8( volatile kmp_int8 *p, kmp_int8 cv, kmp_int8 sv );
#define KMP_COMPARE_AND_STORE_ACQ16     __kmp_compare_and_store16
#define KMP_COMPARE_AND_STORE_REL16     __kmp_compare_and_store16
extern kmp_int16 __kmp_compare_and_store16( volatile kmp_int16 *p, kmp_int16 cv, kmp_int16 sv );

/* Define KMP_COMPARE_AND_STORE* in kmp_os.h for MIC, here for other platforms */
#ifndef KMP_COMPARE_AND_STORE_ACQ32
#define KMP_COMPARE_AND_STORE_ACQ32     __kmp_compare_and_store32
#define KMP_COMPARE_AND_STORE_REL32     __kmp_compare_and_store32
#endif /* KMP_COMPARE_AND_STORE_ACQ32 */
extern kmp_int32 __kmp_compare_and_store32( volatile kmp_int32 *p, kmp_int32 cv, kmp_int32 sv );
#ifndef KMP_COMPARE_AND_STORE_ACQ64
#define KMP_COMPARE_AND_STORE_ACQ64     __kmp_compare_and_store64
#define KMP_COMPARE_AND_STORE_REL64     __kmp_compare_and_store64
#endif /* KMP_COMPARE_AND_STORE_ACQ64 */
extern kmp_int32 __kmp_compare_and_store64( volatile kmp_int64 *p, kmp_int64 cv, kmp_int64 sv );

#if KMP_ARCH_X86
#define KMP_COMPARE_AND_STORE_PTR(dst,cmp,src) \
    KMP_COMPARE_AND_STORE_REL32((volatile kmp_int32 *)dst,(kmp_int32)cmp,(kmp_int32)src)
#else /* 64 bit pointers */
#ifndef KMP_COMPARE_AND_STORE_PTR
#define KMP_COMPARE_AND_STORE_PTR(dst,cmp,src) \
    KMP_COMPARE_AND_STORE_REL64((volatile kmp_int64 *)dst,(kmp_int64)cmp,(kmp_int64)src)
#endif /* KMP_COMPARE_AND_STORE_PTR */
#endif /* KMP_ARCH_X86 */

#if KMP_ARCH_X86 || KMP_ARCH_X86_64
extern void       __kmp_query_cpuid( kmp_cpuinfo_t *p );
#if KMP_MIC
// no routines for floating addition on MIC
#else
extern kmp_real32 __kmp_test_then_add_real32 ( volatile kmp_real32 *addr, kmp_real32 data );
extern kmp_real64 __kmp_test_then_add_real64 ( volatile kmp_real64 *addr, kmp_real64 data );
#endif
extern kmp_int8  __kmp_compare_and_store_ret8(  volatile kmp_int8  *p, kmp_int8  cv, kmp_int8  sv );
extern kmp_int16 __kmp_compare_and_store_ret16( volatile kmp_int16 *p, kmp_int16 cv, kmp_int16 sv );
extern kmp_int32 __kmp_compare_and_store_ret32( volatile kmp_int32 *p, kmp_int32 cv, kmp_int32 sv );
extern kmp_int64 __kmp_compare_and_store_ret64( volatile kmp_int64 *p, kmp_int64 cv, kmp_int64 sv );

extern kmp_int8  __kmp_xchg_fixed8(  volatile kmp_int8  *addr, kmp_int8  data );
extern kmp_int16 __kmp_xchg_fixed16( volatile kmp_int16 *addr, kmp_int16 data );
extern kmp_int32 __kmp_xchg_fixed32( volatile kmp_int32 *addr, kmp_int32 data );

#if KMP_MIC
static kmp_real32 __kmp_xchg_real32( volatile kmp_real32 *addr, kmp_real32 data ) {
    kmp_int32 tmp = __sync_lock_test_and_set( (kmp_int32*)addr, *(kmp_int32*)&data );
    return *(kmp_real32*)&tmp;
}
#else
extern kmp_real32 __kmp_xchg_real32( volatile kmp_real32 *addr, kmp_real32 data );
#endif // KMP_MIC
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

#if KMP_ARCH_X86_64
#if KMP_MIC
static kmp_real64 __kmp_xchg_real64( volatile kmp_real64 *addr, kmp_real64 data ) {
    kmp_int64 tmp = __sync_lock_test_and_set( (kmp_int64*)addr, *(kmp_int64*)&data );
    return *(kmp_real64*)&tmp;
}
#else
extern kmp_real64 __kmp_xchg_real64( volatile kmp_real64 *addr, kmp_real64 data );
#endif // KMP_MIC
extern kmp_int64 __kmp_xchg_fixed64( volatile kmp_int64 *addr, kmp_int64 data );
#endif /* KMP_ARCH_X86_64 */

#if KMP_ARCH_X86 || KMP_ARCH_X86_64

#if KMP_MIC
    #if KMP_MIC1
        // no sse in LRB1
        static inline void __kmp_load_mxcsr( kmp_uint32 *p ) { _mm_setvxcsr( *p ); }
        static inline void __kmp_store_mxcsr( kmp_uint32 *p ) { *p = _mm_getvxcsr(); }
    #else // KMP_MIC1
        #if __MIC2__
            static inline void __kmp_load_mxcsr( kmp_uint32 *p ) { _mm_setcsr( *p ); }
            static inline void __kmp_store_mxcsr( kmp_uint32 *p ) { *p = _mm_getcsr(); }
       #else
            #error "Non LRB1 detected. Re-evaluate if KMP_INHERIT_FP_CONTROL works."
        #endif
    #endif // KMP_MIC1
#else // KMP_MIC
extern void __kmp_load_mxcsr( kmp_uint32 *p );
extern void __kmp_store_mxcsr( kmp_uint32 *p );
#endif // KMP_MIC

//static inline void __kmp_load_mxcsr ( kmp_uint32 *p ) { _mm_setcsr( *p ); }
//static inline void __kmp_store_mxcsr( kmp_uint32 *p ) { *p = _mm_getcsr(); }

extern void __kmp_load_x87_fpu_control_word( kmp_int16 *p );
extern void __kmp_store_x87_fpu_control_word( kmp_int16 *p );
extern void __kmp_clear_x87_fpu_status_word();
# define KMP_X86_MXCSR_MASK      0xffffffc0   /* ignore status flags (6 lsb) */
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

extern int __kmp_invoke_microtask( microtask_t pkfn, int gtid, int npr, int argc, void *argv[] );

extern int  __kmp_is_address_mapped( void *addr );
extern kmp_uint64 __kmp_hardware_timestamp(void);

/* ------------------------------------------------------------------------ */

KMP_EXPORT void   __kmpc_begin                ( ident_t *, kmp_int32 flags );
KMP_EXPORT void   __kmpc_end                  ( ident_t * );

KMP_EXPORT void   __kmpc_threadprivate_register_vec ( ident_t *, void * data, kmpc_ctor_vec ctor,
                                                  kmpc_cctor_vec cctor, kmpc_dtor_vec dtor, size_t vector_length );
KMP_EXPORT void   __kmpc_threadprivate_register     ( ident_t *, void * data, kmpc_ctor ctor, kmpc_cctor cctor, kmpc_dtor dtor );
KMP_EXPORT void * __kmpc_threadprivate              ( ident_t *, kmp_int32 global_tid, void * data, size_t size );

KMP_EXPORT kmp_int32  __kmpc_global_thread_num  ( ident_t * );
KMP_EXPORT kmp_int32  __kmpc_global_num_threads ( ident_t * );
KMP_EXPORT kmp_int32  __kmpc_bound_thread_num   ( ident_t * );
KMP_EXPORT kmp_int32  __kmpc_bound_num_threads  ( ident_t * );

KMP_EXPORT kmp_int32  __kmpc_ok_to_fork     ( ident_t * );
KMP_EXPORT void   __kmpc_fork_call          ( ident_t *, kmp_int32 nargs, kmpc_micro microtask, ... );

KMP_EXPORT void   __kmpc_serialized_parallel     ( ident_t *, kmp_int32 global_tid );
KMP_EXPORT void   __kmpc_end_serialized_parallel ( ident_t *, kmp_int32 global_tid );

KMP_EXPORT void   __kmpc_flush              ( ident_t *, ... );
KMP_EXPORT void   __kmpc_barrier            ( ident_t *, kmp_int32 global_tid );
KMP_EXPORT kmp_int32  __kmpc_master         ( ident_t *, kmp_int32 global_tid );
KMP_EXPORT void   __kmpc_end_master         ( ident_t *, kmp_int32 global_tid );
KMP_EXPORT void   __kmpc_ordered            ( ident_t *, kmp_int32 global_tid );
KMP_EXPORT void   __kmpc_end_ordered        ( ident_t *, kmp_int32 global_tid );
KMP_EXPORT void   __kmpc_critical           ( ident_t *, kmp_int32 global_tid, kmp_critical_name * );
KMP_EXPORT void   __kmpc_end_critical       ( ident_t *, kmp_int32 global_tid, kmp_critical_name * );

KMP_EXPORT kmp_int32  __kmpc_barrier_master ( ident_t *, kmp_int32 global_tid );
KMP_EXPORT void   __kmpc_end_barrier_master ( ident_t *, kmp_int32 global_tid );

KMP_EXPORT kmp_int32  __kmpc_barrier_master_nowait ( ident_t *, kmp_int32 global_tid );

KMP_EXPORT kmp_int32  __kmpc_single         ( ident_t *, kmp_int32 global_tid );
KMP_EXPORT void   __kmpc_end_single         ( ident_t *, kmp_int32 global_tid );

KMP_EXPORT void KMPC_FOR_STATIC_INIT    ( ident_t *loc, kmp_int32 global_tid, kmp_int32 schedtype, kmp_int32 *plastiter,
                                          kmp_int *plower, kmp_int *pupper, kmp_int *pstride, kmp_int incr, kmp_int chunk );

KMP_EXPORT void __kmpc_for_static_fini  ( ident_t *loc, kmp_int32 global_tid );

KMP_EXPORT void __kmpc_copyprivate( ident_t *loc, kmp_int32 global_tid, size_t cpy_size, void *cpy_data, void(*cpy_func)(void*,void*), kmp_int32 didit );

extern void KMPC_SET_NUM_THREADS        ( int arg );
extern void KMPC_SET_DYNAMIC            ( int flag );
extern void KMPC_SET_NESTED             ( int flag );

/* --------------------------------------------------------------------------- */

/*
 * Taskq interface routines
 */

KMP_EXPORT kmpc_thunk_t * __kmpc_taskq (ident_t *loc, kmp_int32 global_tid, kmpc_task_t taskq_task, size_t sizeof_thunk,
                                        size_t sizeof_shareds, kmp_int32 flags, kmpc_shared_vars_t **shareds);
KMP_EXPORT void __kmpc_end_taskq (ident_t *loc, kmp_int32 global_tid, kmpc_thunk_t *thunk);
KMP_EXPORT kmp_int32 __kmpc_task (ident_t *loc, kmp_int32 global_tid, kmpc_thunk_t *thunk);
KMP_EXPORT void __kmpc_taskq_task (ident_t *loc, kmp_int32 global_tid, kmpc_thunk_t *thunk, kmp_int32 status);
KMP_EXPORT void __kmpc_end_taskq_task (ident_t *loc, kmp_int32 global_tid, kmpc_thunk_t *thunk);
KMP_EXPORT kmpc_thunk_t * __kmpc_task_buffer (ident_t *loc, kmp_int32 global_tid, kmpc_thunk_t *taskq_thunk, kmpc_task_t task);

/* ------------------------------------------------------------------------ */

#if OMP_30_ENABLED
/*
 * OMP 3.0 tasking interface routines
 */

KMP_EXPORT kmp_int32
__kmpc_omp_task( ident_t *loc_ref, kmp_int32 gtid, kmp_task_t * new_task );
KMP_EXPORT kmp_task_t*
__kmpc_omp_task_alloc( ident_t *loc_ref, kmp_int32 gtid, kmp_int32 flags,
                       size_t sizeof_kmp_task_t, size_t sizeof_shareds,
                       kmp_routine_entry_t task_entry );
KMP_EXPORT void
__kmpc_omp_task_begin_if0( ident_t *loc_ref, kmp_int32 gtid, kmp_task_t * task );
KMP_EXPORT void
__kmpc_omp_task_complete_if0( ident_t *loc_ref, kmp_int32 gtid, kmp_task_t *task );
KMP_EXPORT kmp_int32
__kmpc_omp_task_parts( ident_t *loc_ref, kmp_int32 gtid, kmp_task_t * new_task );
KMP_EXPORT kmp_int32
__kmpc_omp_taskwait( ident_t *loc_ref, kmp_int32 gtid );

KMP_EXPORT kmp_int32
__kmpc_omp_taskyield( ident_t *loc_ref, kmp_int32 gtid, int end_part );

#if TASK_UNUSED
void __kmpc_omp_task_begin( ident_t *loc_ref, kmp_int32 gtid, kmp_task_t * task );
void __kmpc_omp_task_complete( ident_t *loc_ref, kmp_int32 gtid, kmp_task_t *task );
#endif // TASK_UNUSED

/* ------------------------------------------------------------------------ */
#endif // OMP_30_ENABLED

#if OMP_40_ENABLED
KMP_EXPORT void __kmpc_taskgroup( ident* loc, int gtid );
KMP_EXPORT void __kmpc_end_taskgroup( ident* loc, int gtid );
#endif

/*
 * Lock interface routines (fast versions with gtid passed in)
 */
KMP_EXPORT void __kmpc_init_lock( ident_t *loc, kmp_int32 gtid,  void **user_lock );
KMP_EXPORT void __kmpc_init_nest_lock( ident_t *loc, kmp_int32 gtid, void **user_lock );
KMP_EXPORT void __kmpc_destroy_lock( ident_t *loc, kmp_int32 gtid, void **user_lock );
KMP_EXPORT void __kmpc_destroy_nest_lock( ident_t *loc, kmp_int32 gtid, void **user_lock );
KMP_EXPORT void __kmpc_set_lock( ident_t *loc, kmp_int32 gtid, void **user_lock );
KMP_EXPORT void __kmpc_set_nest_lock( ident_t *loc, kmp_int32 gtid, void **user_lock );
KMP_EXPORT void __kmpc_unset_lock( ident_t *loc, kmp_int32 gtid, void **user_lock );
KMP_EXPORT void __kmpc_unset_nest_lock( ident_t *loc, kmp_int32 gtid, void **user_lock );
KMP_EXPORT int __kmpc_test_lock( ident_t *loc, kmp_int32 gtid, void **user_lock );
KMP_EXPORT int __kmpc_test_nest_lock( ident_t *loc, kmp_int32 gtid, void **user_lock );

/* ------------------------------------------------------------------------ */

/*
 * Interface to fast scalable reduce methods routines
 */

KMP_EXPORT kmp_int32 __kmpc_reduce_nowait( ident_t *loc, kmp_int32 global_tid,
                                           kmp_int32 num_vars, size_t reduce_size,
                                           void *reduce_data, void (*reduce_func)(void *lhs_data, void *rhs_data),
                                           kmp_critical_name *lck );
KMP_EXPORT void __kmpc_end_reduce_nowait( ident_t *loc, kmp_int32 global_tid, kmp_critical_name *lck );
KMP_EXPORT kmp_int32 __kmpc_reduce( ident_t *loc, kmp_int32 global_tid,
                                    kmp_int32 num_vars, size_t reduce_size,
                                    void *reduce_data, void (*reduce_func)(void *lhs_data, void *rhs_data),
                                    kmp_critical_name *lck );
KMP_EXPORT void __kmpc_end_reduce( ident_t *loc, kmp_int32 global_tid, kmp_critical_name *lck );

/*
 * internal fast reduction routines
 */

extern PACKED_REDUCTION_METHOD_T
__kmp_determine_reduction_method( ident_t *loc, kmp_int32 global_tid,
                                  kmp_int32 num_vars, size_t reduce_size,
                                  void *reduce_data, void (*reduce_func)(void *lhs_data, void *rhs_data),
                                  kmp_critical_name *lck );

// this function is for testing set/get/determine reduce method
KMP_EXPORT kmp_int32 __kmp_get_reduce_method( void );

KMP_EXPORT kmp_uint64 __kmpc_get_taskid();
KMP_EXPORT kmp_uint64 __kmpc_get_parent_taskid();

KMP_EXPORT void __kmpc_place_threads(int,int,int);

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

// C++ port
// missing 'extern "C"' declarations

KMP_EXPORT kmp_int32 __kmpc_in_parallel( ident_t *loc );
KMP_EXPORT void __kmpc_pop_num_threads(  ident_t *loc, kmp_int32 global_tid );
KMP_EXPORT void __kmpc_push_num_threads( ident_t *loc, kmp_int32 global_tid, kmp_int32 num_threads );

#if OMP_40_ENABLED
KMP_EXPORT void __kmpc_push_proc_bind( ident_t *loc, kmp_int32 global_tid, int proc_bind );
#endif

KMP_EXPORT void*
__kmpc_threadprivate_cached( ident_t * loc, kmp_int32 global_tid,
                             void * data, size_t size, void *** cache );

// Symbols for MS mutual detection.
extern int _You_must_link_with_exactly_one_OpenMP_library;
extern int _You_must_link_with_Intel_OpenMP_library;
#if KMP_OS_WINDOWS && ( KMP_VERSION_MAJOR > 4 )
    extern int _You_must_link_with_Microsoft_OpenMP_library;
#endif


// The routines below are not exported.
// Consider making them 'static' in corresponding source files.
void
kmp_threadprivate_insert_private_data( int gtid, void *pc_addr, void *data_addr, size_t pc_size );
struct private_common *
kmp_threadprivate_insert( int gtid, void *pc_addr, void *data_addr, size_t pc_size );

#ifdef __cplusplus
}
#endif

#endif /* KMP_H */

