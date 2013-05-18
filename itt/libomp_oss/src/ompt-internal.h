#ifndef __OMPT_INTERNAL_H__
#define __OMPT_INTERNAL_H__

#include "ompt.h"

#define OMPT_VERSION 1

#define _OMP_EXTERN

#define ompt_callback(e) e ## _callback

typedef enum {
  ompt_status_disabled,
  ompt_status_ready,
  ompt_status_track,
  ompt_status_track_callback,
} ompt_status_t;


typedef struct ompt_callbacks_s {
#define ompt_event(event, callback, eventid, is_impl) callback ompt_callback(event); 
#include "ompt-event.h"
} ompt_callbacks_t;


typedef struct {
  ompt_state_t        state;
  ompt_wait_id_t      wait_id;
  ompt_data_t         data;             /* OMPT tool data */
  uint64_t            next_parallel_id;
} ompt_thread_info_t;


typedef struct {
  ompt_data_t         data;
  ompt_frame_t        frame;
} ompt_task_info_t;


typedef struct {
  ompt_parallel_id_t  parallel_id;
} ompt_team_info_t;


extern ompt_status_t ompt_status;
extern ompt_callbacks_t ompt_callbacks;

#ifdef __cplusplus
extern "C" {
#endif

void ompt_init(void);
void ompt_fini(void);

#ifdef __cplusplus
};
#endif

#endif