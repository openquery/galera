// Copyright (C) 2007 Codership Oy <info@codership.com>

/*! \file \brief Total order access "class" implementation.
 * Although gcs_repl() and gcs_recv() calls return sequence
 * numbers in total order, there are concurrency issues between
 * application threads and they can grab critical section
 * mutex out of order. Wherever total order access to critical
 * section is required, these functions can be used to do this.
 */

#include <pthread.h>
#include <errno.h>
#include <string.h>

#include <galerautils.h>

#include "gcs.h"

typedef enum  {
  HOLDER = 0, //!< current TO holder
  WAIT,       //!< actively waiting in the queue
  CANCELED,   //!< Waiter has canceled its to request
  WITHDRAW,   //!< marked to be withdrawn
  RELEASED    //!< has been released, free entry now
} waiter_state_t;

typedef struct
{
    gu_cond_t cond;
    waiter_state_t state;
    int       has_aborted;
}
to_waiter_t;

struct gcs_to
{
    volatile gcs_seqno_t seqno;
    size_t               used;
    size_t               qlen;
    size_t               qmask;
    //gu_cond_t  *queue;
    to_waiter_t*         queue;
    gu_mutex_t           lock;
};

/** Returns pointer to the waiter with the given seqno */
static inline to_waiter_t*
to_get_waiter (gcs_to_t* to, gcs_seqno_t seqno)
{
    return (to->queue + (seqno & to->qmask));
}

#ifdef REMOVED
static set_bit(struct gcs_to *to, gcs_seqno_t seqno) {
  to->aborted[seqno / sizeof(char)] |= (1 << (seqno % sizeof(char));
}
#endif

gcs_to_t *gcs_to_create (int len, gcs_seqno_t seqno)
{
    gcs_to_t *ret;

    if (len <= 0) {
	gu_error ("Negative length parameter: %d", len);
	return NULL;
    }

    ret = GU_CALLOC (1, gcs_to_t);
    
    if (ret) {

	/* Make queue length a power of 2 */
	ret->qlen = 1;
	while (ret->qlen < len) {
	    // unsigned, can be bigger than any integer
	    ret->qlen = ret->qlen << 1;
	}
	ret->qmask = ret->qlen - 1;
	ret->seqno = seqno;

	ret->queue = GU_CALLOC (ret->qlen, to_waiter_t);

	if (ret->queue) {
	    size_t i;
	    for (i = 0; i < ret->qlen; i++) {
                to_waiter_t *w = ret->queue + i;
		gu_cond_init (&w->cond, NULL);
                w->state       = RELEASED;
                w->has_aborted = 0;
	    }
	    gu_mutex_init (&ret->lock, NULL);
	
	    return ret;
	}

	gu_free (ret);
    }

    return NULL;
}

int gcs_to_destroy (gcs_to_t** to)
{
    gcs_to_t *t = *to;
    int ret;
    size_t i;

    gu_mutex_lock (&t->lock);
    if (t->used) {
	gu_mutex_unlock (&t->lock);
	return -EBUSY;
    }
    
    for (i = 0; i < t->qlen; i++) {
        to_waiter_t *w = t->queue + i;
	if (gu_cond_destroy (&w->cond)) {
            // @todo: what if someone is waiting?
	    gu_warn ("Failed to destroy condition %d. Should not happen", i);
	}
    }    
    t->qlen = 0;
    
    gu_mutex_unlock (&t->lock);
    /* What else can be done here? */
    ret = gu_mutex_destroy (&t->lock);
    if (ret) return -ret; // application can retry

    gu_free (t->queue);
    gu_free (t);
    *to = NULL;
    return 0;
}

int gcs_to_grab (gcs_to_t* to, gcs_seqno_t seqno)
{
    int err;
    to_waiter_t *w;

    if ((err = gu_mutex_lock(&to->lock))) {
	gu_fatal("Mutex lock failed (%d): %s", err, strerror(err));
	abort();
    }
    w = to_get_waiter (to, seqno);


    switch (w->state) {
    case CANCELED:
	err = -ECANCELED;
	break;
    case RELEASED:
	if (seqno == to->seqno) {
	    w->state = HOLDER;
	} else if (seqno < to->seqno) {
	    gu_fatal("Trying to grab outdated seqno");
	    abort();
	} else { /* seqno > to->seqno */
	    w->state = WAIT;
	    to->used++;
	    gu_cond_wait(&w->cond, &to->lock);
	    to->used--;
	    if (w->state == CANCELED) {
		err = -ECANCELED;
	    } else if (w->state == WAIT)
		w->state = HOLDER;
	    else {
		gu_fatal("Invalid cond wait exit state %d", w->state);
		abort();
	    }
	}
	break;
    default:
	gu_fatal("TO queue over wrap");
	abort();
    }
    
    gu_mutex_unlock(&to->lock);
    return err;
}

int gcs_to_release (gcs_to_t *to, gcs_seqno_t seqno)
{
    int err;
    to_waiter_t *w;

    if ((err = gu_mutex_lock(&to->lock))) {
	gu_fatal("Mutex lock failed (%d): %s", err, strerror(err));
	abort();
    }

    w = to_get_waiter (to, seqno);
    
    if (seqno == to->seqno) {
	w->state = RELEASED;
	/* Iterate over CANCELED waiters and set states as RELEASED */
	for (to->seqno++; (w = to_get_waiter(to, to->seqno)) && 
		 w->state == CANCELED; to->seqno++) {
	    w->state = RELEASED;
	}
	if (w->state == WAIT)
	    gu_cond_signal(&w->cond);
    } else if (seqno > to->seqno) {
	if (w->state != CANCELED) {
	    gu_fatal("Illegal state in premature release: %d", w->state);
	    abort();
	}
	/* Leave state CANCELED so that real releaser can iterate  */
    } else {
	/* */
	if (w->state != RELEASED) {
	    gu_fatal("Outdated seqno and state not RELEASED: %d", w->state);
	    abort();
	}
    }

    gu_mutex_unlock(&to->lock);

    return err;
}

gcs_seqno_t gcs_to_seqno (gcs_to_t* to)
{
    return to->seqno - 1;
}

int gcs_to_cancel (gcs_to_t *to, gcs_seqno_t seqno)
{
    int err;
    to_waiter_t *w;
    if ((err = gu_mutex_lock (&to->lock))) {
	gu_fatal("Mutex lock failed (%d): %s", err, strerror(err));
	abort();
    }
    
    w = to_get_waiter (to, seqno);
    if (seqno > to->seqno) {
	w->state = CANCELED;
	err = gu_cond_signal (&w->cond);
	if (err) 
	    gu_warn("gu_cond_signal failed: %d", err);
    } else if (seqno == to->seqno) {
	gu_fatal("tried to cancel holder: state %d seqno %llu",
		 w->state, seqno);
	abort();
    } else {
	gu_fatal("trying to cancel used seqno: state %d cancel seqno = %llu, "
		 "TO seqno = %llu", w->state, seqno, to->seqno);
	abort();
    }
    
    gu_mutex_unlock (&to->lock);
    return err;
}

void gcs_to_self_cancel(gcs_to_t *to, gcs_seqno_t seqno)
{
    int err;
    to_waiter_t *w;
    if ((err = gu_mutex_lock (&to->lock))) {
	gu_fatal("Mutex lock failed (%d): %s", err, strerror(err));
	abort();
    }

    if (seqno < to->seqno) {
	gu_fatal("Cannot self cancel seqno that hasn't tried to grab: seqno %llu TO seqno %llu", seqno, to->seqno);
	abort();
    }
    
    w = to_get_waiter(to, seqno);
    w->state = CANCELED;

    gu_mutex_unlock(&to->lock);
}

int gcs_to_withdraw (gcs_to_t *to, gcs_seqno_t seqno)
{
    int rcode;
    int err;

    if ((err = gu_mutex_lock (&to->lock))) {
	gu_fatal("Mutex lock failed (%d): %s", err, strerror(err));
	abort();
    }
    {
        if (seqno >= to->seqno) {
            to_waiter_t *w = to_get_waiter (to, seqno);
            w->state       = WITHDRAW;
            w->has_aborted = 0;
            rcode = gu_cond_signal (&w->cond);
        } else {
            gu_warn ("trying to withdraw used seqno: cancel seqno = %llu, "
                     "TO seqno = %llu", seqno, to->seqno);
            /* gu_mutex_unlock (&to->lock); */
            rcode = -ERANGE;
        }
    }
    gu_mutex_unlock (&to->lock);
    return rcode;
}

int gcs_to_renew_wait (gcs_to_t *to, gcs_seqno_t seqno)
{
    int rcode;
    int err;
    if ((err = gu_mutex_lock (&to->lock))) {
	gu_fatal("Mutex lock failed (%d): %s", err, strerror(err));
	abort();
    }
    {
        if (seqno >= to->seqno) {
            to_waiter_t *w = to_get_waiter (to, seqno);
            w->state       = RELEASED;
            w->has_aborted = 0;
            rcode = 0;
        } else {
            gu_warn ("trying to renew used seqno: cancel seqno = %llu, "
                     "TO seqno = %llu", seqno, to->seqno);
            rcode = -ERANGE;
        }
    }
    gu_mutex_unlock (&to->lock);
    return rcode;
}

