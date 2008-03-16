// Copyright (C) 2007 Codership Oy <info@codership.com>
/*
 * Interface to component messages
 *
 */

#ifndef _gcs_component_h_
#define _gcs_component_h_

#include <stdbool.h>

#define GCS_COMP_MEMB_ID_MAX_LEN 39 // should accommodate human readable UUID

#ifdef GCS_COMP_MSG_ACCESS
typedef struct gcs_comp_memb
{
    char id[GCS_COMP_MEMB_ID_MAX_LEN + 1]; /// ID assigned by the backend
}
gcs_comp_memb_t;

typedef struct gcs_comp_msg
{
    bool            primary;   /// 1 if we have a quorum, 0 if not
    long            my_idx;    /// this node's index in membership
    long            memb_num;  /// number of members in configuration
    gcs_comp_memb_t memb[0];   /// member array
}
gcs_comp_msg_t;
#else
typedef struct gcs_comp_msg gcs_comp_msg_t;
#endif

/*! Allocates component message */
extern gcs_comp_msg_t*
gcs_comp_msg_new    (bool prim, long my_id, long memb_num);

/*! Destroys component message */
extern void
gcs_comp_msg_delete (gcs_comp_msg_t* comp);

/*! Adds a member to the component message
 *  Returns an index of the member or negative error code:
 *  -1            when membership is full
 *  -ENOTUNIQ     when name collides with one that is in membership already
 *  -ENAMETOOLONG wnen memory allocation for new name fails */
extern long
gcs_comp_msg_add    (gcs_comp_msg_t* comp, const char* id);

/*! Returns total size of the component message */
extern long
gcs_comp_msg_size   (const gcs_comp_msg_t* comp);

/*! Creates a copy of the component message */
extern gcs_comp_msg_t*
gcs_comp_msg_copy   (const gcs_comp_msg_t* comp);

/*! Returns member ID by index, NULL if none */
extern const char*
gcs_comp_msg_id     (const gcs_comp_msg_t* comp, long idx);

/*! Returns member index by ID, -1 if none */
extern long
gcs_comp_msg_idx    (const gcs_comp_msg_t* comp, const char* id);

/*! Returns primary status of the component */
extern bool
gcs_comp_msg_primary (const gcs_comp_msg_t* comp);

/*! Returns our own idx */
extern long
gcs_comp_msg_self (const gcs_comp_msg_t* comp);

/*! Returns number of members in the component */
extern long
gcs_comp_msg_num (const gcs_comp_msg_t* comp);

#endif /* _gcs_component_h_ */
