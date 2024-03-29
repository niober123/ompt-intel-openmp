/*
 * kmp_sched.c -- static scheduling -- iteration initialization
 * $Revision: 42178 $
 * $Date: 2013-03-22 07:07:59 -0500 (Fri, 22 Mar 2013) $
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

/*
 * Dynamic scheduling initialization and dispatch.
 *
 * NOTE: team->t.t_nproc is a constant inside of any dispatch loop, however
 *       it may change values between parallel regions.  __kmp_max_nth
 *       is the largest value __kmp_nth may take, 1 is the smallest.
 *
 */

#include "kmp.h"
#include "kmp_i18n.h"
#include "kmp_str.h"
#include "kmp_error.h"

// template for type limits
template< typename T >
struct i_maxmin {
    static const T mx;
    static const T mn;
};
template<>
struct i_maxmin< int > {
    static const int mx = 0x7fffffff;
    static const int mn = 0x80000000;
};
template<>
struct i_maxmin< unsigned int > {
    static const unsigned int mx = 0xffffffff;
    static const unsigned int mn = 0x00000000;
};
template<>
struct i_maxmin< long long > {
    static const long long mx = 0x7fffffffffffffffLL;
    static const long long mn = 0x8000000000000000LL;
};
template<>
struct i_maxmin< unsigned long long > {
    static const unsigned long long mx = 0xffffffffffffffffLL;
    static const unsigned long long mn = 0x0000000000000000LL;
};
//-------------------------------------------------------------------------
#ifdef KMP_DEBUG
//-------------------------------------------------------------------------
// template for debug prints specification ( d, u, lld, llu )
    char const * traits_t< int >::spec = "d";
    char const * traits_t< unsigned int >::spec = "u";
    char const * traits_t< long long >::spec = "lld";
    char const * traits_t< unsigned long long >::spec = "llu";
//-------------------------------------------------------------------------
#endif

template< typename T >
static void
__kmp_for_static_init(
    ident_t                          *loc,
    kmp_int32                         global_tid,
    kmp_int32                         schedtype,
    kmp_int32                        *plastiter,
    T                                *plower,
    T                                *pupper,
    typename traits_t< T >::signed_t *pstride,
    typename traits_t< T >::signed_t  incr,
    typename traits_t< T >::signed_t  chunk
) {
    typedef typename traits_t< T >::unsigned_t  UT;
    typedef typename traits_t< T >::signed_t    ST;
    /*  this all has to be changed back to TID and such.. */
    register kmp_int32   gtid = global_tid;
    register kmp_uint32  tid  = __kmp_tid_from_gtid( global_tid );
    register kmp_uint32  nth;
    register UT          trip_count;
    register kmp_team_t *team = __kmp_threads[ gtid ]->th.th_team;

    KE_TRACE( 10, ("__kmpc_for_static_init called (%d)\n", global_tid));
    #ifdef KMP_DEBUG
    {
        const char * buff;
        // create format specifiers before the debug output
        buff = __kmp_str_format(
            "__kmpc_for_static_init: T#%%d sched=%%d liter=%%d iter=(%%%s," \
            " %%%s, %%%s) incr=%%%s chunk=%%%s signed?<%s>\n",
            traits_t< T >::spec, traits_t< T >::spec, traits_t< ST >::spec,
            traits_t< ST >::spec, traits_t< ST >::spec, traits_t< T >::spec );
        KD_TRACE(100, ( buff, global_tid, schedtype, *plastiter,
            *plower, *pupper, *pstride, incr, chunk ) );
        __kmp_str_free( &buff );
    }
    #endif

    if ( __kmp_env_consistency_check ) {
        __kmp_push_workshare( global_tid, ct_pdo, loc );
        if ( incr == 0 ) {
            __kmp_error_construct( kmp_i18n_msg_CnsLoopIncrZeroProhibited, ct_pdo, loc );

        }
    }
    /* special handling for zero-trip loops */
    if ( incr > 0 ? (*pupper < *plower) : (*plower < *pupper) ) {
        *plastiter = FALSE;
        /* leave pupper and plower set to entire iteration space */
        *pstride = incr;   /* value should never be used */
	//        *plower = *pupper - incr;   // let compiler bypass the illegal loop (like for(i=1;i<10;i--))  THIS LINE CAUSED shape2F/h_tests_1.f TO HAVE A FAILURE ON A ZERO-TRIP LOOP (lower=1,\
	  upper=0,stride=1) - JPH June 23, 2009.
        #ifdef KMP_DEBUG
        {
            const char * buff;
            // create format specifiers before the debug output
            buff = __kmp_str_format(
                "__kmpc_for_static_init:(ZERO TRIP) liter=%%d lower=%%%s upper=%%%s stride = %%%s signed?<%s>, loc = %%s\n",
                traits_t< T >::spec, traits_t< T >::spec, traits_t< ST >::spec, traits_t< T >::spec );
            KD_TRACE(100, ( buff, *plastiter, *plower, *pupper, *pstride, loc->psource ) );
            __kmp_str_free( &buff );
        }
        #endif
        KE_TRACE( 10, ("__kmpc_for_static_init: T#%d return\n", global_tid ) );
        return;
    }

    /* determine if "for" loop is an active worksharing construct */
    if ( team -> t.t_serialized ) {
        /* serialized parallel, each thread executes whole iteration space */
        *plastiter = TRUE;
        /* leave pupper and plower set to entire iteration space */
        *pstride = (incr > 0) ? (*pupper - *plower + 1) : (-(*plower - *pupper + 1));

        #ifdef KMP_DEBUG
        {
            const char * buff;
            // create format specifiers before the debug output
            buff = __kmp_str_format(
                "__kmpc_for_static_init: (serial) liter=%%d lower=%%%s upper=%%%s stride = %%%s\n",
                traits_t< T >::spec, traits_t< T >::spec, traits_t< ST >::spec );
            KD_TRACE(100, ( buff, *plastiter, *plower, *pupper, *pstride ) );
            __kmp_str_free( &buff );
        }
        #endif
        KE_TRACE( 10, ("__kmpc_for_static_init: T#%d return\n", global_tid ) );
        return;
    }
    nth = team->t.t_nproc;
    if ( nth == 1 ) {
        *plastiter = TRUE;

        #ifdef KMP_DEBUG
        {
            const char * buff;
            // create format specifiers before the debug output
            buff = __kmp_str_format(
                "__kmpc_for_static_init: (serial) liter=%%d lower=%%%s upper=%%%s stride = %%%s\n",
                traits_t< T >::spec, traits_t< T >::spec, traits_t< ST >::spec );
            KD_TRACE(100, ( buff, *plastiter, *plower, *pupper, *pstride ) );
            __kmp_str_free( &buff );
        }
        #endif
        KE_TRACE( 10, ("__kmpc_for_static_init: T#%d return\n", global_tid ) );
        return;
    }

    /* compute trip count */
    if ( incr == 1 ) {
        trip_count = *pupper - *plower + 1;
    } else if (incr == -1) {
        trip_count = *plower - *pupper + 1;
    } else {
        if ( incr > 1 ) {
            trip_count = (*pupper - *plower) / incr + 1;
        } else {
            trip_count = (*plower - *pupper) / ( -incr ) + 1;
        }
    }
    if ( __kmp_env_consistency_check ) {
        /* tripcount overflow? */
        if ( trip_count == 0 && *pupper != *plower ) {
            __kmp_error_construct( kmp_i18n_msg_CnsIterationRangeTooLarge, ct_pdo, loc );
        }
    }

    /* compute remaining parameters */
    switch ( schedtype ) {
    case kmp_sch_static:
        {
            if ( trip_count < nth ) {
                KMP_DEBUG_ASSERT(
                    __kmp_static == kmp_sch_static_greedy || \
                    __kmp_static == kmp_sch_static_balanced
                ); // Unknown static scheduling type.
                if ( tid < trip_count ) {
                    *pupper = *plower = *plower + tid * incr;
                } else {
                    *plower = *pupper + incr;
                }
                *plastiter = ( tid == trip_count - 1 );
            } else {
                if ( __kmp_static == kmp_sch_static_balanced ) {
                    register UT small_chunk = trip_count / nth;
                    register UT extras = trip_count % nth;
                    *plower += incr * ( tid * small_chunk + ( tid < extras ? tid : extras ) );
                    *pupper = *plower + small_chunk * incr - ( tid < extras ? 0 : incr );
                    *plastiter = ( tid == nth - 1 );
                } else {
                    register T big_chunk_inc_count = ( trip_count/nth +
                                                     ( ( trip_count % nth ) ? 1 : 0) ) * incr;
                    register T old_upper = *pupper;

                    KMP_DEBUG_ASSERT( __kmp_static == kmp_sch_static_greedy );
                        // Unknown static scheduling type.

                    *plower += tid * big_chunk_inc_count;
                    *pupper = *plower + big_chunk_inc_count - incr;
                    if ( incr > 0 ) {
                        if ( *pupper < *plower ) {
                            *pupper = i_maxmin< T >::mx;
                        }
                        *plastiter = *plower <= old_upper && *pupper > old_upper - incr;
                        if ( *pupper > old_upper ) *pupper = old_upper; // tracker C73258
                    } else {
                        if ( *pupper > *plower ) {
                            *pupper = i_maxmin< T >::mn;
                        }
                        *plastiter = *plower >= old_upper && *pupper < old_upper - incr;
                        if ( *pupper < old_upper ) *pupper = old_upper; // tracker C73258
                    }
                }
            }
            break;
        }
    case kmp_sch_static_chunked:
        {
            register T span;
            if ( chunk < 1 ) {
                chunk = 1;
            }
            span = chunk * incr;
            *pstride = span * nth;
            *plower = *plower + (span * tid);
            *pupper = *plower + span - incr;
            /* TODO: is the following line a bug?  Shouldn't it be plastiter instead of *plastiter ? */
            if (*plastiter) { /* only calculate this if it was requested */
                kmp_int32 lasttid = ((trip_count - 1) / ( UT )chunk) % nth;
                *plastiter = (tid == lasttid);
            }
            break;
        }
    default:
        KMP_ASSERT2( 0, "__kmpc_for_static_init: unknown scheduling type" );
        break;
    }

    #ifdef KMP_DEBUG
    {
        const char * buff;
        // create format specifiers before the debug output
        buff = __kmp_str_format(
            "__kmpc_for_static_init: liter=%%d lower=%%%s upper=%%%s stride = %%%s signed?<%s>\n",
            traits_t< T >::spec, traits_t< T >::spec, traits_t< ST >::spec, traits_t< T >::spec );
        KD_TRACE(100, ( buff, *plastiter, *plower, *pupper, *pstride ) );
        __kmp_str_free( &buff );
    }
    #endif
    KE_TRACE( 10, ("__kmpc_for_static_init: T#%d return\n", global_tid ) );
    return;
}

//--------------------------------------------------------------------------------------
extern "C" {

/*!
@ingroup WORK_SHARING
@param    loc       Source code location
@param    gtid      Global thread id of this thread
@param    schedtype  Scheduling type
@param    plastiter Pointer to the "last iteration" flag
@param    plower    Pointer to the lower bound
@param    pupper    Pointer to the upper bound
@param    pstride   Pointer to the stride
@param    incr      Pointer to the increment
@param    chunk     Pointer to the chunk size

Each of the four functions here are identical apart from the argument types.

The functions compute the upper and lower bounds and stride to be used for the set of iterations
to be executed by the current thread from the statically scheduled loop that is described by the 
initial values of the bround, stride, increment and chunk size.

@{
*/
void
__kmpc_for_static_init_4( ident_t *loc, kmp_int32 gtid, kmp_int32 schedtype, kmp_int32 *plastiter,
                      kmp_int32 *plower, kmp_int32 *pupper,
                      kmp_int32 *pstride, kmp_int32 incr, kmp_int32 chunk )
{
    __kmp_for_static_init< kmp_int32 >(
                      loc, gtid, schedtype, plastiter, plower, pupper, pstride, incr, chunk );
}

/*!
 See @ref __kmpc_for_static_init_4
 */
void
__kmpc_for_static_init_4u( ident_t *loc, kmp_int32 gtid, kmp_int32 schedtype, kmp_int32 *plastiter,
                      kmp_uint32 *plower, kmp_uint32 *pupper,
                      kmp_int32 *pstride, kmp_int32 incr, kmp_int32 chunk )
{
    __kmp_for_static_init< kmp_uint32 >(
                      loc, gtid, schedtype, plastiter, plower, pupper, pstride, incr, chunk );
}

/*!
 See @ref __kmpc_for_static_init_4
 */
void
__kmpc_for_static_init_8( ident_t *loc, kmp_int32 gtid, kmp_int32 schedtype, kmp_int32 *plastiter,
                      kmp_int64 *plower, kmp_int64 *pupper,
                      kmp_int64 *pstride, kmp_int64 incr, kmp_int64 chunk )
{
    __kmp_for_static_init< kmp_int64 >(
                      loc, gtid, schedtype, plastiter, plower, pupper, pstride, incr, chunk );
}

/*!
 See @ref __kmpc_for_static_init_4
 */
void
__kmpc_for_static_init_8u( ident_t *loc, kmp_int32 gtid, kmp_int32 schedtype, kmp_int32 *plastiter,
                      kmp_uint64 *plower, kmp_uint64 *pupper,
                      kmp_int64 *pstride, kmp_int64 incr, kmp_int64 chunk )
{
    __kmp_for_static_init< kmp_uint64 >(
                      loc, gtid, schedtype, plastiter, plower, pupper, pstride, incr, chunk );
}
/*! 
@}
*/

} // extern "C"

