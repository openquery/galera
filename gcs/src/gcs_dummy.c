// Copyright (C) 2007 Codership Oy <info@codership.com>
/* 
 * Dummy backend implementation
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>

#include <galerautils.h>

#include "gcs_queue.h"
#include "gcs_comp_msg.h"
#include "gcs_dummy.h"

typedef struct dummy_msg
{
    gcs_msg_type_t type;
    size_t         len;
    uint8_t        buf[0];
}
dummy_msg_t;

typedef struct gcs_backend_conn
{
    gcs_queue_t     *gc_q;   /* "serializator" */
    dummy_msg_t     *msg;    /* last undelivered message */
//    bool             pc_sent;
    gcs_seqno_t      msg_id;
    size_t           msg_max_size;
}
dummy_t;

static inline dummy_msg_t*
dummy_msg_create (gcs_msg_type_t const type,
		  size_t         const len,
		  const void*    const buf)
{
    dummy_msg_t *msg = NULL;

    if ((msg = gu_malloc (sizeof(dummy_msg_t) + len)))
    {
	    memcpy (msg->buf, buf, len);
	    msg->len  = len;
	    msg->type = type;
    }
    
    return msg;
}

static inline long dummy_msg_destroy (dummy_msg_t **msg)
{
    if (*msg)
    {
	gu_free (*msg);
	*msg = NULL;
    }
    return 0;
}

static
GCS_BACKEND_CLOSE_FN(dummy_close)
{
    dummy_t* dummy = backend->conn;
    
    if (!dummy) return -EBADFD;

    gcs_queue_free    (&dummy->gc_q);
    dummy_msg_destroy (&dummy->msg);
    gu_free (dummy);
    backend->conn = NULL;
    return 0;
}

static
GCS_BACKEND_SEND_FN(dummy_send)
{
    int err = 0;

    if (backend->conn)
    {
	dummy_msg_t *msg   = dummy_msg_create (msg_type, len, buf);
	if (msg)
	{
	    if ((err = gcs_queue_push (backend->conn->gc_q, msg)))
	    {
		dummy_msg_destroy (&msg);
		return err;
	    }
	    else
		return len;
	}
	else
	    err = -ENOMEM;
    }
    else
	err = -EFAULT;
    return err;
}

static
GCS_BACKEND_RECV_FN(dummy_recv)
{
    int ret = 0;
    dummy_t* conn = backend->conn;

    *sender_id = GCS_SENDER_NONE;
    *msg_type  = GCS_MSG_ERROR;

    assert (conn);

    /* skip it if we already have popped a message from the queue
     * in the previous call */
    if (!conn->msg)
    {
        if ((ret = gcs_queue_pop_wait (conn->gc_q,
                                       (void**) &conn->msg))) {
            if (-ENODATA == ret) {
                // wait was aborted while no data - connection closing
                ret = -ECONNABORTED;
            }
            return ret;
        }
        else {
                /* Alaways the same sender */

        }
    }

    *sender_id=0;	    
    assert (conn->msg);
    ret = conn->msg->len;
    
    if (conn->msg->len <= len)
    {
        memcpy (buf, conn->msg->buf, conn->msg->len);
        *msg_type = conn->msg->type;
        dummy_msg_destroy (&conn->msg);
    }
    else {
        memcpy (buf, conn->msg->buf, len);
    }

    return ret;
}

static
GCS_BACKEND_NAME_FN(dummy_name)
{
    return "built-in dummy backend";
}

static
GCS_BACKEND_MSG_SIZE_FN(dummy_msg_size)
{
    long max_size = backend->conn->msg_max_size;
    if (pkt_size <= max_size) {
	return pkt_size;
    }
    else {
	gu_warn ("Requested packet size: %d, maximum possible packet size: %d",
		 pkt_size, max_size);
	return max_size;
    }
}

static
const gcs_backend_t dummy_backend =
{
    .conn     = NULL,
    .close    = dummy_close,
    .send     = dummy_send,
    .recv     = dummy_recv,
    .name     = dummy_name,
    .msg_size = dummy_msg_size
};

/* A function to simulate primary component message,
 * as usual returns the total size of the message that would be */
static long dummy_create_pc (gcs_backend_t* dummy)
{
    gcs_comp_msg_t* comp = gcs_comp_msg_new (true, 0, 1);
    long            ret  = -ENOMEM; // assume the worst

    if (comp) {
	ret = gcs_comp_msg_add (comp, "Dummy localhost");
	assert (0 == ret); // we have only one member, index = 0
        // put it in the queue, like a usual message 
        ret = gcs_comp_msg_size(comp);
        ret = dummy_send (dummy, comp, ret, GCS_MSG_COMPONENT);
	gcs_comp_msg_delete (comp);
    }
    return ret;
}

GCS_BACKEND_OPEN_FN(gcs_dummy_open)
{
    long     ret   = -ENOMEM;
    dummy_t *dummy = NULL;

    if (!(dummy = GU_MALLOC(dummy_t)))
	goto out0;
    if (!(dummy->gc_q = gcs_queue()))
	goto out1;

    dummy->msg          = NULL;
    dummy->msg_id       = 0;
//    dummy->pc_sent      = false;
    dummy->msg_max_size = sysconf (_SC_PAGESIZE);

    *backend      = dummy_backend; // set methods
    backend->conn = dummy;         // set data

    ret = dummy_create_pc (backend);
    if (ret < 0) goto out2;

    return 0;

out2:
    gcs_queue_free (&dummy->gc_q);
out1:
    gu_free (dummy);
out0:
    backend->conn = NULL;
    return ret;
}

