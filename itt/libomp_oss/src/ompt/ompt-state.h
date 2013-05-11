/* first */
ompt_state(ompt_state_first, 0x61)           /* initial enumeration state */

/* work states (0..15) */
ompt_state(ompt_state_work_serial, 0x00)     /* working outside parallel */
ompt_state(ompt_state_work_parallel, 0x01)   /* working within parallel */
ompt_state(ompt_state_work_reduction, 0x02)  /* performing a reduction */

/* idle (16..31) */
ompt_state(ompt_state_idle, 0x10)            /* waiting for work */

/* overhead states (32..63) */
ompt_state(ompt_state_overhead, 0x20)        /* overhead excluding wait states */

/* wait states non-mutex (64..79) */
ompt_state(ompt_state_wait_barrier, 0x40)    /* waiting at a barrier */
ompt_state(ompt_state_wait_taskwait, 0x41)   /* waiting at a taskwait */
ompt_state(ompt_state_wait_taskgroup, 0x42)  /* waiting at a taskgroup */

/* wait states mutex (80..95) */  
ompt_state(ompt_state_wait_lock, 0x50)       /* waiting for lock */
ompt_state(ompt_state_wait_nest_lock, 0x51)  /* waiting for nest lock */
ompt_state(ompt_state_wait_critical, 0x52)   /* waiting for critical */
ompt_state(ompt_state_wait_atomic, 0x53)     /* waiting for atomic */
ompt_state(ompt_state_wait_ordered, 0x54)    /* waiting for ordered */

/* misc (96..127) */
ompt_state(ompt_state_undefined, 0x60)       /* undefined thread state */

/* last */
ompt_state(ompt_state_last, 0x62)            /* final enumeration state */
