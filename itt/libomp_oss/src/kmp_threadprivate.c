/*
 * kmp_threadprivate.c -- OpenMP threadprivate support library
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

#include "kmp.h"
#include "kmp_i18n.h"

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

#define USE_CHECKS_COMMON

#define KMP_INLINE_SUBR         1


/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

void
kmp_threadprivate_insert_private_data( int gtid, void *pc_addr, void *data_addr, size_t pc_size );
struct private_common *
kmp_threadprivate_insert( int gtid, void *pc_addr, void *data_addr, size_t pc_size );

struct shared_table     __kmp_threadprivate_d_table;

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

static
#ifdef KMP_INLINE_SUBR
__forceinline
#endif
struct private_common *
__kmp_threadprivate_find_task_common( struct common_table *tbl, int gtid, void *pc_addr )

{
    struct private_common *tn;

#ifdef KMP_TASK_COMMON_DEBUG
    KC_TRACE( 10, ( "__kmp_threadprivate_find_task_common: thread#%d, called with address %p\n",
                    gtid, pc_addr ) );
    dump_list();
#endif

    for (tn = tbl->data[ KMP_HASH(pc_addr) ]; tn; tn = tn->next) {
        if (tn->gbl_addr == pc_addr) {
#ifdef KMP_TASK_COMMON_DEBUG
            KC_TRACE( 10, ( "__kmp_threadprivate_find_task_common: thread#%d, found node %p on list\n",
                            gtid, pc_addr ) );
#endif
            return tn;
        }
    }
    return 0;
}

static
#ifdef KMP_INLINE_SUBR
__forceinline
#endif
struct shared_common *
__kmp_find_shared_task_common( struct shared_table *tbl, int gtid, void *pc_addr )
{
    struct shared_common *tn;

    for (tn = tbl->data[ KMP_HASH(pc_addr) ]; tn; tn = tn->next) {
        if (tn->gbl_addr == pc_addr) {
#ifdef KMP_TASK_COMMON_DEBUG
            KC_TRACE( 10, ( "__kmp_find_shared_task_common: thread#%d, found node %p on list\n",
                            gtid, pc_addr ) );
#endif
            return tn;
        }
    }
    return 0;
}


/*
 *      Create a template for the data initialized storage.
 *      Either the template is NULL indicating zero fill,
 *      or the template is a copy of the original data.
 */

static struct private_data *
__kmp_init_common_data( void *pc_addr, size_t pc_size )
{
    struct private_data *d;
    size_t       i;
    char        *p;

    d = (struct private_data *) __kmp_allocate( sizeof( struct private_data ) );
/*
    d->data = 0;  // AC: commented out because __kmp_allocate zeroes the memory
    d->next = 0;
*/
    d->size = pc_size;
    d->more = 1;

    p = (char*)pc_addr;

    for (i = pc_size;  i > 0; --i) {
        if (*p++ != '\0') {
            d->data = __kmp_allocate( pc_size );
            memcpy( d->data, pc_addr, pc_size );
            break;
        }
    }

    return d;
}

/*
 *      Initialize the data area from the template.
 */

static void
__kmp_copy_common_data( void *pc_addr, struct private_data *d )
{
    char *addr = (char *) pc_addr;
    int   i, offset;

    for (offset = 0; d != 0; d = d->next) {
        for (i = d->more; i > 0; --i) {
            if (d->data == 0)
                memset( & addr[ offset ], '\0', d->size );
            else
                memcpy( & addr[ offset ], d->data, d->size );
            offset += d->size;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

/* we are called from __kmp_serial_initialize() with __kmp_initz_lock held. */
void
__kmp_common_initialize( void )
{
    if( ! TCR_4(__kmp_init_common) ) {
        int q;
#ifdef KMP_DEBUG
        int gtid;
#endif

        __kmp_threadpriv_cache_list = NULL;

#ifdef KMP_DEBUG
        /* verify the uber masters were initialized */
        for(gtid = 0 ; gtid < __kmp_threads_capacity; gtid++ )
            if( __kmp_root[gtid] ) {
                KMP_DEBUG_ASSERT( __kmp_root[gtid]->r.r_uber_thread );
                for ( q = 0; q< KMP_HASH_TABLE_SIZE; ++q)
                    KMP_DEBUG_ASSERT( !__kmp_root[gtid]->r.r_uber_thread->th.th_pri_common->data[q] );
/*                    __kmp_root[ gitd ]-> r.r_uber_thread -> th.th_pri_common -> data[ q ] = 0;*/
            }
#endif /* KMP_DEBUG */

        for (q = 0; q < KMP_HASH_TABLE_SIZE; ++q)
            __kmp_threadprivate_d_table.data[ q ] = 0;

        TCW_4(__kmp_init_common, TRUE);
    }
}

/* Call all destructors for threadprivate data belonging to all threads.
   Currently unused! */
void
__kmp_common_destroy( void )
{
    if( TCR_4(__kmp_init_common) ) {
        int q;

        TCW_4(__kmp_init_common, FALSE);

        for (q = 0; q < KMP_HASH_TABLE_SIZE; ++q) {
            int gtid;
            struct private_common *tn;
            struct shared_common  *d_tn;

            /*  C++ destructors need to be called once per thread before exiting  */
            /*  don't call destructors for master thread though unless we used copy constructor */

            for (d_tn = __kmp_threadprivate_d_table.data[ q ]; d_tn; d_tn = d_tn->next) {
                if (d_tn->is_vec) {
                    if (d_tn->dt.dtorv != 0) {
                        for (gtid = 0; gtid < __kmp_all_nth; ++gtid) {
                            if( __kmp_threads[gtid] ) {
                                if( (__kmp_foreign_tp) ? (! KMP_INITIAL_GTID (gtid)) :
                                                         (! KMP_UBER_GTID (gtid)) ) {
                                    tn = __kmp_threadprivate_find_task_common( __kmp_threads[ gtid ]->th.th_pri_common,
                                                                               gtid, d_tn->gbl_addr );
                                    if (tn) {
                                        (*d_tn->dt.dtorv) (tn->par_addr, d_tn->vec_len);
                                    }
                                }
                            }
                        }
                        if (d_tn->obj_init != 0) {
                            (*d_tn->dt.dtorv) (d_tn->obj_init, d_tn->vec_len);
                        }
                    }
                } else {
                    if (d_tn->dt.dtor != 0) {
                        for (gtid = 0; gtid < __kmp_all_nth; ++gtid) {
                            if( __kmp_threads[gtid] ) {
                                if( (__kmp_foreign_tp) ? (! KMP_INITIAL_GTID (gtid)) :
                                                         (! KMP_UBER_GTID (gtid)) ) {
                                    tn = __kmp_threadprivate_find_task_common( __kmp_threads[ gtid ]->th.th_pri_common,
                                                                               gtid, d_tn->gbl_addr );
                                    if (tn) {
                                        (*d_tn->dt.dtor) (tn->par_addr);
                                    }
                                }
                            }
                        }
                        if (d_tn->obj_init != 0) {
                            (*d_tn->dt.dtor) (d_tn->obj_init);
                        }
                    }
                }
            }
            __kmp_threadprivate_d_table.data[ q ] = 0;
        }
    }
}

/* Call all destructors for threadprivate data belonging to this thread */
void
__kmp_common_destroy_gtid( int gtid )
{
    struct private_common *tn;
    struct shared_common *d_tn;

    KC_TRACE( 10, ("__kmp_common_destroy_gtid: T#%d called\n", gtid ) );
    if( (__kmp_foreign_tp) ? (! KMP_INITIAL_GTID (gtid)) :
                             (! KMP_UBER_GTID (gtid)) ) {

        if( TCR_4(__kmp_init_common) ) {

            /* Cannot do this here since not all threads have destroyed their data */
            /* TCW_4(__kmp_init_common, FALSE); */

            for (tn = __kmp_threads[ gtid ]->th.th_pri_head; tn; tn = tn->link) {

                d_tn = __kmp_find_shared_task_common( &__kmp_threadprivate_d_table,
                                                      gtid, tn->gbl_addr );

                KMP_DEBUG_ASSERT( d_tn );

                if (d_tn->is_vec) {
                    if (d_tn->dt.dtorv != 0) {
                        (void) (*d_tn->dt.dtorv) (tn->par_addr, d_tn->vec_len);
                    }
                    if (d_tn->obj_init != 0) {
                        (void) (*d_tn->dt.dtorv) (d_tn->obj_init, d_tn->vec_len);
                    }
                } else {
                    if (d_tn->dt.dtor != 0) {
                        (void) (*d_tn->dt.dtor) (tn->par_addr);
                    }
                    if (d_tn->obj_init != 0) {
                        (void) (*d_tn->dt.dtor) (d_tn->obj_init);
                    }
                }
            }
            KC_TRACE( 30, ("__kmp_common_destroy_gtid: T#%d threadprivate destructors complete\n",
                           gtid ) );
        }
    }
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

#ifdef KMP_TASK_COMMON_DEBUG
static void
dump_list( void )
{
    int p, q;

    for (p = 0; p < __kmp_all_nth; ++p) {
        if( !__kmp_threads[p] ) continue;
        for (q = 0; q < KMP_HASH_TABLE_SIZE; ++q) {
            if (__kmp_threads[ p ]->th.th_pri_common->data[ q ]) {
                struct private_common *tn;

                KC_TRACE( 10, ( "\tdump_list: gtid:%d addresses\n", p ) );

                for (tn = __kmp_threads[ p ]->th.th_pri_common->data[ q ]; tn; tn = tn->next)                 {
                    KC_TRACE( 10, ( "\tdump_list: THREADPRIVATE: Serial %p -> Parallel %p\n",
                                    tn->gbl_addr, tn->par_addr ) );
                }
            }
        }
    }
}
#endif /* KMP_TASK_COMMON_DEBUG */


/*
 * NOTE: this routine is to be called only from the serial part of the program.
 */

void
kmp_threadprivate_insert_private_data( int gtid, void *pc_addr, void *data_addr, size_t pc_size )
{
    struct shared_common **lnk_tn, *d_tn;
    KMP_DEBUG_ASSERT( __kmp_threads[ gtid ] &&
            __kmp_threads[ gtid ] -> th.th_root -> r.r_active == 0 );

    d_tn = __kmp_find_shared_task_common( &__kmp_threadprivate_d_table,
                                          gtid, pc_addr );

    if (d_tn == 0) {
        d_tn = (struct shared_common *) __kmp_allocate( sizeof( struct shared_common ) );

        d_tn->gbl_addr = pc_addr;
        d_tn->pod_init = __kmp_init_common_data( data_addr, pc_size );
/*
        d_tn->obj_init = 0;  // AC: commented out because __kmp_allocate zeroes the memory
        d_tn->ct.ctor = 0;
        d_tn->cct.cctor = 0;;
        d_tn->dt.dtor = 0;
        d_tn->is_vec = FALSE;
        d_tn->vec_len = 0L;
*/
        d_tn->cmn_size = pc_size;

        __kmp_acquire_lock( &__kmp_global_lock, gtid );

        lnk_tn = &(__kmp_threadprivate_d_table.data[ KMP_HASH(pc_addr) ]);

        d_tn->next = *lnk_tn;
        *lnk_tn = d_tn;

        __kmp_release_lock( &__kmp_global_lock, gtid );
    }
}

struct private_common *
kmp_threadprivate_insert( int gtid, void *pc_addr, void *data_addr, size_t pc_size )
{
    struct private_common *tn, **tt;
    struct shared_common  *d_tn;

    /* +++++++++ START OF CRITICAL SECTION +++++++++ */

    __kmp_acquire_lock( & __kmp_global_lock, gtid );

    tn = (struct private_common *) __kmp_allocate( sizeof (struct private_common) );

    tn->gbl_addr = pc_addr;

    d_tn = __kmp_find_shared_task_common( &__kmp_threadprivate_d_table,
                                          gtid, pc_addr );     /* Only the MASTER data table exists. */

    if (d_tn != 0) {
        /* This threadprivate variable has already been seen. */

        if ( d_tn->pod_init == 0 && d_tn->obj_init == 0 ) {
            d_tn->cmn_size = pc_size;

            if (d_tn->is_vec) {
                if (d_tn->ct.ctorv != 0) {
                    /* Construct from scratch so no prototype exists */
                    d_tn->obj_init = 0;
                }
                else if (d_tn->cct.cctorv != 0) {
                    /* Now data initialize the prototype since it was previously registered */
                    d_tn->obj_init = (void *) __kmp_allocate( d_tn->cmn_size );
                    (void) (*d_tn->cct.cctorv) (d_tn->obj_init, pc_addr, d_tn->vec_len);
                }
                else {
                    d_tn->pod_init = __kmp_init_common_data( data_addr, d_tn->cmn_size );
                }
            } else {
                if (d_tn->ct.ctor != 0) {
                    /* Construct from scratch so no prototype exists */
                    d_tn->obj_init = 0;
                }
                else if (d_tn->cct.cctor != 0) {
                    /* Now data initialize the prototype since it was previously registered */
                    d_tn->obj_init = (void *) __kmp_allocate( d_tn->cmn_size );
                    (void) (*d_tn->cct.cctor) (d_tn->obj_init, pc_addr);
                }
                else {
                    d_tn->pod_init = __kmp_init_common_data( data_addr, d_tn->cmn_size );
                }
            }
        }
    }
    else {
        struct shared_common **lnk_tn;

        d_tn = (struct shared_common *) __kmp_allocate( sizeof( struct shared_common ) );
        d_tn->gbl_addr = pc_addr;
        d_tn->cmn_size = pc_size;
        d_tn->pod_init = __kmp_init_common_data( data_addr, pc_size );
/*
        d_tn->obj_init = 0;  // AC: commented out because __kmp_allocate zeroes the memory
        d_tn->ct.ctor = 0;
        d_tn->cct.cctor = 0;
        d_tn->dt.dtor = 0;
        d_tn->is_vec = FALSE;
        d_tn->vec_len = 0L;
*/
        lnk_tn = &(__kmp_threadprivate_d_table.data[ KMP_HASH(pc_addr) ]);

        d_tn->next = *lnk_tn;
        *lnk_tn = d_tn;
    }

    tn->cmn_size = d_tn->cmn_size;

    if ( (__kmp_foreign_tp) ? (KMP_INITIAL_GTID (gtid)) : (KMP_UBER_GTID (gtid)) ) {
        tn->par_addr = (void *) pc_addr;
    }
    else {
        tn->par_addr = (void *) __kmp_allocate( tn->cmn_size );
    }

    __kmp_release_lock( & __kmp_global_lock, gtid );

    /* +++++++++ END OF CRITICAL SECTION +++++++++ */

#ifdef USE_CHECKS_COMMON
        if (pc_size > d_tn->cmn_size) {
            KC_TRACE( 10, ( "__kmp_threadprivate_insert: THREADPRIVATE: %p (%"
                            KMP_UINTPTR_SPEC " ,%" KMP_UINTPTR_SPEC ")\n",
                            pc_addr, pc_size, d_tn->cmn_size ) );
            KMP_FATAL( TPCommonBlocksInconsist );
        }
#endif /* USE_CHECKS_COMMON */

    tt = &(__kmp_threads[ gtid ]->th.th_pri_common->data[ KMP_HASH(pc_addr) ]);

#ifdef KMP_TASK_COMMON_DEBUG
    if (*tt != 0) {
        KC_TRACE( 10, ( "__kmp_threadprivate_insert: WARNING! thread#%d: collision on %p\n",
                        gtid, pc_addr ) );
    }
#endif
    tn->next = *tt;
    *tt = tn;

#ifdef KMP_TASK_COMMON_DEBUG
    KC_TRACE( 10, ( "__kmp_threadprivate_insert: thread#%d, inserted node %p on list\n",
                    gtid, pc_addr ) );
    dump_list( );
#endif

    /* Link the node into a simple list */

    tn->link = __kmp_threads[ gtid ]->th.th_pri_head;
    __kmp_threads[ gtid ]->th.th_pri_head = tn;

#ifdef BUILD_TV
    __kmp_tv_threadprivate_store( __kmp_threads[ gtid ], tn->gbl_addr, tn->par_addr );
#endif

    if( (__kmp_foreign_tp) ? (KMP_INITIAL_GTID (gtid)) : (KMP_UBER_GTID (gtid)) )
        return tn;

    /*
     * if C++ object with copy constructor, use it;
     * else if C++ object with constructor, use it for the non-master copies only;
     * else use pod_init and memcpy
     *
     * C++ constructors need to be called once for each non-master thread on allocate
     * C++ copy constructors need to be called once for each thread on allocate
     */

    /*
     * C++ object with constructors/destructors;
     * don't call constructors for master thread though
     */
    if (d_tn->is_vec) {
        if ( d_tn->ct.ctorv != 0) {
            (void) (*d_tn->ct.ctorv) (tn->par_addr, d_tn->vec_len);
        } else if (d_tn->cct.cctorv != 0) {
            (void) (*d_tn->cct.cctorv) (tn->par_addr, d_tn->obj_init, d_tn->vec_len);
        } else if (tn->par_addr != tn->gbl_addr) {
            __kmp_copy_common_data( tn->par_addr, d_tn->pod_init );
        }
    } else {
        if ( d_tn->ct.ctor != 0 ) {
            (void) (*d_tn->ct.ctor) (tn->par_addr);
        } else if (d_tn->cct.cctor != 0) {
            (void) (*d_tn->cct.cctor) (tn->par_addr, d_tn->obj_init);
        } else if (tn->par_addr != tn->gbl_addr) {
            __kmp_copy_common_data( tn->par_addr, d_tn->pod_init );
        }
    }
/* !BUILD_OPENMP_C
    if (tn->par_addr != tn->gbl_addr)
        __kmp_copy_common_data( tn->par_addr, d_tn->pod_init ); */

    return tn;
}

/* ------------------------------------------------------------------------ */
/* We are currently parallel, and we know the thread id.                    */
/* ------------------------------------------------------------------------ */

/*!
 @ingroup THREADPRIVATE

 @param loc source location information 
 @param data  pointer to data being privatized 
 @param ctor  pointer to constructor function for data 
 @param cctor  pointer to copy constructor function for data 
 @param dtor  pointer to destructor function for data 

 Register constructors and destructors for thread private data.
 This function is called when executing in parallel, when we know the thread id.
*/
void
__kmpc_threadprivate_register(ident_t *loc, void *data, kmpc_ctor ctor, kmpc_cctor cctor, kmpc_dtor dtor)
{
    struct shared_common *d_tn, **lnk_tn;

    KC_TRACE( 10, ("__kmpc_threadprivate_register: called\n" ) );

#ifdef USE_CHECKS_COMMON
    /* copy constructor must be zero for current code gen (Nov 2002 - jph) */
    KMP_ASSERT( cctor == 0);
#endif /* USE_CHECKS_COMMON */

    /* Only the global data table exists. */
    d_tn = __kmp_find_shared_task_common( &__kmp_threadprivate_d_table, -1, data );

    if (d_tn == 0) {
        d_tn = (struct shared_common *) __kmp_allocate( sizeof( struct shared_common ) );
        d_tn->gbl_addr = data;

        d_tn->ct.ctor = ctor;
        d_tn->cct.cctor = cctor;
        d_tn->dt.dtor = dtor;
/*
        d_tn->is_vec = FALSE;  // AC: commented out because __kmp_allocate zeroes the memory
        d_tn->vec_len = 0L;
        d_tn->obj_init = 0;
        d_tn->pod_init = 0;
*/
        lnk_tn = &(__kmp_threadprivate_d_table.data[ KMP_HASH(data) ]);

        d_tn->next = *lnk_tn;
        *lnk_tn = d_tn;
    }
}

void *
__kmpc_threadprivate(ident_t *loc, kmp_int32 global_tid, void *data, size_t size)
{
    void *ret;
    struct private_common *tn;

    KC_TRACE( 10, ("__kmpc_threadprivate: T#%d called\n", global_tid ) );

#ifdef USE_CHECKS_COMMON
    if (! __kmp_init_serial)
        KMP_FATAL( RTLNotInitialized );
#endif /* USE_CHECKS_COMMON */

    if ( ! __kmp_threads[global_tid] -> th.th_root -> r.r_active && ! __kmp_foreign_tp ) {
        /* The parallel address will NEVER overlap with the data_address */
        /* dkp: 3rd arg to kmp_threadprivate_insert_private_data() is the data_address; use data_address = data */

        KC_TRACE( 20, ("__kmpc_threadprivate: T#%d inserting private data\n", global_tid ) );
        kmp_threadprivate_insert_private_data( global_tid, data, data, size );

        ret = data;
    }
    else {
        KC_TRACE( 50, ("__kmpc_threadprivate: T#%d try to find private data at address %p\n",
                       global_tid, data ) );
        tn = __kmp_threadprivate_find_task_common( __kmp_threads[ global_tid ]->th.th_pri_common, global_tid, data );

        if ( tn ) {
            KC_TRACE( 20, ("__kmpc_threadprivate: T#%d found data\n", global_tid ) );
#ifdef USE_CHECKS_COMMON
            if ((size_t) size > tn->cmn_size) {
                KC_TRACE( 10, ( "THREADPRIVATE: %p (%" KMP_UINTPTR_SPEC " ,%" KMP_UINTPTR_SPEC ")\n",
                                data, size, tn->cmn_size ) );
                KMP_FATAL( TPCommonBlocksInconsist );
            }
#endif /* USE_CHECKS_COMMON */
        }
        else {
            /* The parallel address will NEVER overlap with the data_address */
            /* dkp: 3rd arg to kmp_threadprivate_insert() is the data_address; use data_address = data */
            KC_TRACE( 20, ("__kmpc_threadprivate: T#%d inserting data\n", global_tid ) );
            tn = kmp_threadprivate_insert( global_tid, data, data, size );
        }

        ret = tn->par_addr;
    }
    KC_TRACE( 10, ("__kmpc_threadprivate: T#%d exiting; return value = %p\n",
                   global_tid, ret ) );

    return ret;
}

/*!
 @ingroup THREADPRIVATE
 @param loc source location information 
 @param global_tid  global thread number 
 @param data  pointer to data to privatize 
 @param size  size of data to privatize 
 @param cache  pointer to cache 
 @return pointer to private storage 

 Allocate private storage for threadprivate data. 
*/
void *
__kmpc_threadprivate_cached(
    ident_t *  loc,
    kmp_int32  global_tid,   // gtid.
    void *     data,         // Pointer to original global variable.
    size_t     size,         // Size of original global variable.
    void ***   cache
) {
    void *ret, **my_cache;

    KC_TRACE( 10, ("__kmpc_threadprivate_cached: T#%d called with cache: %p, address: %p, size: %"
                   KMP_SIZE_T_SPEC "\n",
                   global_tid, *cache, data, size ) );

    if ( TCR_PTR(*cache) == 0) {
        __kmp_acquire_lock( & __kmp_global_lock, global_tid );

        if ( TCR_PTR(*cache) == 0) {
            int i;
            kmp_cached_addr_t *tp_cache_addr;

            __kmp_acquire_bootstrap_lock(&__kmp_tp_cached_lock);
            if(__kmp_threads_capacity > __kmp_tp_capacity)
                __kmp_msg(
                    kmp_ms_fatal,
                    KMP_MSG( ManyThreadsForTPDirective ),
                    KMP_HNT( Set_ALL_THREADPRIVATE, __kmp_threads_capacity),
                    __kmp_msg_null
                 );
            __kmp_tp_cached = 1;
            __kmp_release_bootstrap_lock(&__kmp_tp_cached_lock);
            /* TODO: free all this memory in __kmp_common_destroy using __kmp_threadpriv_cache_list */
                my_cache = (void**)
                    __kmp_allocate(
                        sizeof( void * ) * __kmp_tp_capacity + sizeof ( kmp_cached_addr_t )
                    );

            KC_TRACE( 50, ("__kmpc_threadprivate_cached: T#%d allocated cache at address %p\n",
                           global_tid, my_cache ) );
/*
            // AC: commented out because __kmp_allocate zeroes the memory
            for (i = 0; i < __kmp_tp_capacity; ++i)
                TCW_PTR(my_cache[i], 0);
*/
            /* add address of mycache for cleanup later to linked list */
            tp_cache_addr = (kmp_cached_addr_t *) & my_cache[__kmp_tp_capacity];
            tp_cache_addr -> addr = my_cache;
            tp_cache_addr -> next = __kmp_threadpriv_cache_list;
            __kmp_threadpriv_cache_list = tp_cache_addr;

            KMP_MB();

            TCW_PTR( *cache, my_cache);

            KMP_MB();
        }

        __kmp_release_lock( & __kmp_global_lock, global_tid );
    }


    if ((ret = TCR_PTR((*cache)[ global_tid ])) == 0) {
        ret = __kmpc_threadprivate( loc, global_tid, data, (size_t) size);

        TCW_PTR( (*cache)[ global_tid ], ret);
    }
    KC_TRACE( 10, ("__kmpc_threadprivate_cached: T#%d exiting; return value = %p\n",
                   global_tid, ret ) );

    return ret;
}

/*!
 @ingroup THREADPRIVATE
 @param loc source location information 
 @param data  pointer to data being privatized 
 @param ctor  pointer to constructor function for data 
 @param cctor  pointer to copy constructor function for data 
 @param dtor  pointer to destructor function for data 
 @param vector_length length of the vector (bytes or elements?)
 Register vector constructors and destructors for thread private data.
*/
void
__kmpc_threadprivate_register_vec( ident_t *loc, void *data, kmpc_ctor_vec ctor,
                                   kmpc_cctor_vec cctor, kmpc_dtor_vec dtor,
                                   size_t vector_length )
{
    struct shared_common *d_tn, **lnk_tn;

    KC_TRACE( 10, ("__kmpc_threadprivate_register_vec: called\n" ) );

#ifdef USE_CHECKS_COMMON
    /* copy constructor must be zero for current code gen (Nov 2002 - jph) */
    KMP_ASSERT( cctor == 0);
#endif /* USE_CHECKS_COMMON */

    d_tn = __kmp_find_shared_task_common( &__kmp_threadprivate_d_table,
                                          -1, data );        /* Only the global data table exists. */

    if (d_tn == 0) {
        d_tn = (struct shared_common *) __kmp_allocate( sizeof( struct shared_common ) );
        d_tn->gbl_addr = data;

        d_tn->ct.ctorv = ctor;
        d_tn->cct.cctorv = cctor;
        d_tn->dt.dtorv = dtor;
        d_tn->is_vec = TRUE;
        d_tn->vec_len = (size_t) vector_length;
/*
        d_tn->obj_init = 0;  // AC: commented out because __kmp_allocate zeroes the memory
        d_tn->pod_init = 0;
*/
        lnk_tn = &(__kmp_threadprivate_d_table.data[ KMP_HASH(data) ]);

        d_tn->next = *lnk_tn;
        *lnk_tn = d_tn;
    }
}
