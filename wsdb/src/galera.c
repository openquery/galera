// Copyright (C) 2007 Codership Oy <info@codership.com>

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <galerautils.h>
#include <gcs.h>
#include <dirent.h>
#include <sys/stat.h>

#define __USE_BSD 1
#include <sys/time.h>

#include "wsdb_api.h"
#include "conn.h"
#include "galera.h"
#include "wsdb_api.h"
#include "gcs.h"
#include "galerautils.h"
#include "wsdb_priv.h"
#include "job_queue.h"

enum galera_repl_state {
    GALERA_INITIALIZED,
    GALERA_ENABLED,
    GALERA_DISABLED,
};

struct galera_info {
    /* state of wsdb library */
    struct wsdb_info wsdb;
    /* state of gcs library */
    //struct gcs_status gcs;

    enum galera_repl_state repl_state;
};

/* application's handlers */
static galera_context_retain_fun ctx_retain_cb      = NULL;
static galera_context_store_fun  ctx_store_cb       = NULL;
static galera_bf_execute_fun     bf_execute_cb      = NULL;
static galera_bf_apply_row_fun   bf_apply_row_cb    = NULL;
static galera_ws_start_fun       ws_start_cb        = NULL;
static galera_log_cb_t           galera_log_handler = NULL;

/* application context pointer */
//static void *app_ctx = NULL;

/* gcs parameters */
static gcs_to_t           *to_queue    = NULL;
#ifdef REMOVED
static gcs_to_t           *commit_queue= NULL;
#endif /* REMOVED */
static gcs_conn_t         *gcs_conn    = NULL;
static gcs_backend_type_t  gcs_backend = GCS_BACKEND_DUMMY;
static char               *gcs_channel = "dummy_galera";
static char               *gcs_socket  = NULL;

static struct job_queue   *applier_queue = NULL;

/* global status structure */
struct galera_info Galera;

static gu_mutex_t commit_mtx;

static FILE *wslog_L;
static FILE *wslog_G;

void galera_log(galera_severity_t code, char *fmt, ...) {
    va_list ap;
    char msg[1024] = {0};
    char FMT[1024] = {0};
    char SYS_ERR[1024] = {0};
    GU_DBUG_ENTER("galera_log");
    if (errno) {
        sprintf(SYS_ERR, "\nSystem error: %d, %s", errno, strerror(errno));
        errno = 0;
    }
    va_start(ap, fmt);
    sprintf(FMT, "GALERA (%d): %s", code, fmt);
    vsprintf(msg, FMT, ap);
    va_end(ap);
    strcat(msg, SYS_ERR);
    if (galera_log_handler) {
    	    galera_log_handler(code, msg);
    } else {
        fprintf(stderr, msg);
    }
    GU_DBUG_VOID_RETURN;
}

/* @struct contains one write set and its TO sequence number
 */
struct job_context {
    trx_seqno_t seqno;
    struct wsdb_write_set *ws;
};

static int ws_conflict_check(void *ctx1, void *ctx2) {
    struct job_context *job1 = (struct job_context *)ctx1;
    struct job_context *job2 = (struct job_context *)ctx2;

    if (job1->seqno < job2->seqno) return 0;

    /* job1 is sequenced after job2, must check if they conflict */

    {
      /* serious mis-use of certification test
       * we mangle ws seqno's so that certification_test certifies
       * against just only the job2 ws.
       * If somebody cares to modify wsdb_certification_test, it might
       * break this logic => take care
       */
      trx_seqno_t last_seen_saved = job1->ws->last_seen_trx;
      int rcode;

      job1->ws->last_seen_trx = job2->seqno - 1;
      rcode = wsdb_certification_test(job1->ws, (job2->seqno + 1)); 

      job1->ws->last_seen_trx = last_seen_saved;
      if (rcode) {
        return 1;
      }
    }
    return 0;
}

enum galera_status galera_init(galera_gcs_backend_t backend,
			       const char*          group,
			       const char*          address,
			       const char*          data_dir,
			       galera_log_cb_t      logger)
{
    GU_DBUG_ENTER("galera_init");
    galera_log_handler = logger;
    
    /* set up GCS parameters */
    switch (backend) {
    case GALERA_GCS_DUMMY:
        GU_DBUG_PRINT("galera", ("using dummy backend"));
	gcs_backend = GCS_BACKEND_DUMMY;
	break;
    case GALERA_GCS_SPREAD:
        GU_DBUG_PRINT("galera", ("using spread backend"));
	gcs_backend = GCS_BACKEND_SPREAD;
	if (address) {
	    gcs_socket = strdup (address);
	} else {
	    gcs_socket = "localhost:4803"; /// default for Spread
	}
	if (group) {
	    gcs_channel = strdup (group);
	}
	break;
    case GALERA_GCS_VS:
	GU_DBUG_PRINT("galera", ("using vs backend"));
	gcs_backend = GCS_BACKEND_VS;
	if (address) {
	    gcs_socket = strdup(address);
	} else {
	    gcs_socket = "tcp:127.0.0.1:4567";
	}
	break;
    default:
        GU_DBUG_PRINT("galera", ("unknown backend: %d", backend));
	/* FIXME: there must be a sensible message about unsupported backend */
	assert (0);
    }

    /* initialize wsdb */
    wsdb_init(data_dir, logger);

    gu_conf_set_log_callback(logger);

    /* initialize total order queue */
    to_queue = gcs_to_create(16384, 1);

    Galera.repl_state = GALERA_INITIALIZED;

    gu_mutex_init(&commit_mtx, NULL);

    /* create worker queue */
    applier_queue = job_queue_create(2, ws_conflict_check);

    /* debug level printing to /tmp directory */
    {
      DIR *dir = opendir("/tmp/galera");
      if (!dir) {
        mkdir("/tmp/galera", S_IRWXU | S_IRWXG);
      }
      wslog_L = fopen("/tmp/galera/ws_local.log", "w");
      wslog_G = fopen("/tmp/galera/ws_global.log", "w");
    }
    GU_DBUG_RETURN(GALERA_OK);
}

void galera_dbug_push (const char* control)
{
    GU_DBUG_PUSH(control);
}

void galera_dbug_pop (void)
{
    GU_DBUG_POP();
}

enum galera_status galera_tear_down() {
    if (to_queue) gcs_to_destroy(&to_queue);
    return GALERA_OK;
}

enum galera_status galera_enable() {
    int rcode;

    GU_DBUG_ENTER("galera_enable");
    if (gcs_conn) {
        GU_DBUG_RETURN(GALERA_NODE_FAIL);
    }

    rcode = gcs_open(&gcs_conn, gcs_channel, gcs_socket, gcs_backend);
    switch(rcode) {
    case GCS_ERR_OK:
	assert (gcs_conn);
	galera_log (GALERA_LOG_INFO, "Successfully opened gcs connection");
	break;
    default:
	galera_log (GALERA_NODE_FAIL, "gcs_open() failed");
	GU_DBUG_RETURN(GALERA_NODE_FAIL);
    }

    Galera.repl_state = GALERA_ENABLED;
    GU_DBUG_RETURN(GALERA_OK);
}

enum galera_status galera_disable() {
    int rcode;

    GU_DBUG_ENTER("galera_disable");
    if (!gcs_conn) {
        GU_DBUG_RETURN(GALERA_NODE_FAIL);
    }
    rcode = gcs_close(&gcs_conn);
    gcs_conn = NULL;

    Galera.repl_state = GALERA_DISABLED;
    GU_DBUG_RETURN(GALERA_OK);
}

enum galera_status galera_set_context_retain_handler(
    galera_context_retain_fun handler
) {
    ctx_retain_cb = handler;
    return GALERA_OK;
}

enum galera_status galera_set_context_store_handler(
    galera_context_store_fun handler
) {
    ctx_store_cb = handler;
    return GALERA_OK;
}

enum galera_status galera_set_execute_handler(galera_bf_execute_fun handler) {
    bf_execute_cb = handler;
    return GALERA_OK;
}

enum galera_status galera_set_apply_row_handler(
    galera_bf_apply_row_fun handler
) {
    bf_apply_row_cb = handler;
    return GALERA_OK;
}

enum galera_status galera_set_ws_start_handler(galera_ws_start_fun handler) {
    ws_start_cb = handler;
    return GALERA_OK;
}

static void print_ws(FILE* fid, struct wsdb_write_set *ws, gcs_seqno_t seqno) {
    u_int16_t i;

    if (!fid) return;

    fprintf(fid, "WS: %llu\n", (long long unsigned int)seqno);
    for (i=0; i < ws->query_count; i++) {
      char *query = gu_malloc (ws->queries[i].query_len + 1);
      memset(query, '\0', ws->queries[i].query_len + 1);
      memcpy(query, ws->queries[i].query, ws->queries[i].query_len);
      fprintf(fid, "QUERY (%llu): %s\n", (long long unsigned int)seqno, query);
      gu_free (query);
    }
    fflush(fid);
}
#ifdef REMOVED
static void print_ws(struct job_worker *worker, struct wsdb_write_set *ws) {
    u_int16_t i;

    if (worker) {
      galera_log(GALERA_LOG_INFO,"job: %d",worker->id);
    } else {
      galera_log(GALERA_LOG_INFO,"LOCAL");
    }
    /* applying connection context statements */
    for (i=0; i < ws->query_count; i++) {
      char *query = gu_malloc (ws->queries[i].query_len + 1);
      memset(query, '\0', ws->queries[i].query_len + 1);
      memcpy(query, ws->queries[i].query, ws->queries[i].query_len);
      galera_log(GALERA_LOG_INFO, "QUERY: %s", query );
      gu_free (query);
    }
}
#endif

static int apply_queries(void *app_ctx, struct wsdb_write_set *ws) {
    u_int16_t i;
    GU_DBUG_ENTER(__PRETTY_FUNCTION__);

    /* SQL statement apply method */
    for (i=0; i < ws->query_count; i++) {
        int rcode = bf_execute_cb(
            app_ctx, ws->queries[i].query, ws->queries[i].query_len
        );
        switch (rcode) {
        case 0: break;
        default: {
            char *query = gu_malloc (ws->queries[i].query_len + 1);
            memset(query, '\0',(ws->queries[i].query_len + 1));
            memcpy(query, ws->queries[i].query, ws->queries[i].query_len);
	    gu_error("query apply failed: %s", query);
            gu_free (query);
            GU_DBUG_RETURN(GALERA_TRX_FAIL);
            break;
        }
        }
    }
    GU_DBUG_RETURN(GALERA_OK);
}
static int apply_rows(void *app_ctx, struct wsdb_write_set *ws) {
    u_int16_t i;
    GU_DBUG_ENTER("apply_rows");

    if (bf_apply_row_cb == NULL) {
        galera_log(GALERA_FATAL, "row data applier has not been defined"); 
        GU_DBUG_RETURN(GALERA_FATAL);
    }

    /* row data apply method */
    for (i=0; i < ws->item_count; i++) {
        int rcode;
        if (ws->items[i].data_mode != ROW) {
            gu_error("bad row mode: %d for item: %d", 
		     ws->items[i].data_mode, i);
            continue;
        }

        rcode = bf_apply_row_cb(
            app_ctx, ws->items[i].u.row.data, ws->items[i].u.row.length
        );
        switch (rcode) {
        case 0: break;
        default: {
            gu_warn("row apply failed: %d", rcode);
            GU_DBUG_RETURN(GALERA_TRX_FAIL);
            break;
        }
        }
    }
    GU_DBUG_RETURN(GALERA_OK);
}

static int apply_write_set(void *app_ctx, struct wsdb_write_set *ws) {
    u_int16_t i;
    int rcode;

    GU_DBUG_ENTER("apply_write_set");
    assert(bf_execute_cb);

    /* applying connection context statements */
    for (i=0; i < ws->conn_query_count; i++) {
        int rcode = bf_execute_cb(
            app_ctx, ws->conn_queries[i].query, ws->conn_queries[i].query_len
        );
        switch (rcode) {
        case 0: break;
        default: {
            char *query = gu_malloc (ws->conn_queries[i].query_len + 1);
            memset(query, '\0', ws->conn_queries[i].query_len + 1);
            memcpy(query, ws->conn_queries[i].query, ws->conn_queries[i].query_len);
	    gu_error("connection query apply failed: %s", query);
            gu_free (query);
            GU_DBUG_RETURN(GALERA_TRX_FAIL);
            break;
        }
        }
    }

    switch (ws->level) {
    case WSDB_WS_QUERY:     
        rcode = apply_queries(app_ctx, ws);
        if (rcode != GALERA_OK) GU_DBUG_RETURN(rcode);
        break;
    case WSDB_WS_DATA_ROW:  
        rcode = apply_rows(app_ctx, ws);
        if (rcode != GALERA_OK) GU_DBUG_RETURN(rcode);
        break;
    case WSDB_WS_DATA_COLS: 
        galera_log(
                GALERA_TRX_FAIL, 
                "column data replication is not supported yet"
            );
            GU_DBUG_RETURN(GALERA_TRX_FAIL);
    }
    GU_DBUG_RETURN(GALERA_OK);
}

static int apply_query(void *app_ctx, char *query, int len) {

    int rcode;

    GU_DBUG_ENTER("apply_commit");

    assert(bf_execute_cb);

    rcode = bf_execute_cb(app_ctx, query, len);
    if (rcode) {
        gu_error("query commit failed: %d query '%s'", rcode, query);
        GU_DBUG_RETURN(GALERA_TRX_FAIL);
    }
    
    GU_DBUG_RETURN(GALERA_OK);
}

static void process_conn_write_set( 
    struct job_worker *applier, void *app_ctx, struct wsdb_write_set *ws, 
    gcs_seqno_t seqno_l
) {
    int rcode;

    /* wait for total order */
    if (gcs_to_grab(to_queue, seqno_l) != 0) {
	gu_fatal("Failed to grab to_queue: %llu", seqno_l);
	abort();
    }
    
    /* certification ok */
    rcode = apply_write_set(app_ctx, ws);
    if (rcode) {
        galera_log(
            GALERA_TRX_FAIL, "unknown galera fail: %d trx: %llu", rcode,seqno_l
	    );
    }
    
    /* release total order */
    gcs_to_release(to_queue, seqno_l);
    return;
}

static void process_query_write_set( 
    struct job_worker *applier, void *app_ctx, struct wsdb_write_set *ws, 
    gcs_seqno_t seqno_g, gcs_seqno_t seqno_l
) {
    int rcode;
    struct job_context ctx;

    galera_log(111, "recv gcs_to_grab: %llu", seqno_l);
    /* wait for total order */
    if (gcs_to_grab(to_queue, seqno_l) != 0) {
	gu_fatal("Failed to grab to_queue: %llu", seqno_l);
	abort();
    }

    /* certification test */
    rcode = wsdb_append_write_set(seqno_g, ws);

    //print_ws(wslog_G, ws, seqno_l);


 retry:
    switch (rcode) {
    case WSDB_OK:   /* certification ok */
        
        /* synchronize with other appliers */
        ctx.seqno = seqno_l;
        ctx.ws    = ws;
        job_queue_start_job(applier_queue, applier, (void *)&ctx);

        while((rcode = apply_write_set(app_ctx, ws))) {
	    gu_warn("ws apply failed for: %llu, last_seen: %llu", seqno_g, ws->last_seen_trx);
        }
        
        job_queue_end_job(applier_queue, applier);

        rcode = apply_query(app_ctx, "commit\0", 7);
        if (rcode) {
	    gu_warn("ws apply commit failed for: %llu, last_seen: %llu", seqno_g, ws->last_seen_trx);
          rcode= WSDB_OK;
          goto retry;
        }

        /* register committed transaction */
        if (!rcode) {
            wsdb_set_global_trx_committed(seqno_g);
        } else {
            gu_warn("could not apply trx: %llu", seqno_l);
	}
	break;
    case WSDB_CERTIFICATION_FAIL:
        /* certification failed, release */
        gu_warn("trx certification failed: %llu - %llu", seqno_l, ws->last_seen_trx);
        print_ws(wslog_G, ws, seqno_l);
        break;
    default:  
        gu_error(
            "unknown galera fail: %d trdx: %llu",rcode,seqno_l
	    );
        break;
    }
    /* release total order */
    gcs_to_release(to_queue, seqno_l);
    galera_log(111, "handled ws for: %llu", seqno_g);
    
    return;
}

static void process_write_set( 
    struct job_worker *applier, void *app_ctx, uint8_t *data, size_t data_len, 
    gcs_seqno_t seqno_g, gcs_seqno_t seqno_l
) {
    struct wsdb_write_set ws;
    XDR xdrs;

    xdrmem_create(&xdrs, (char *)data, data_len, XDR_DECODE);
    if (!xdr_wsdb_write_set(&xdrs, &ws)) {
        galera_log(GALERA_TRX_FAIL, "XDR allocation failed");
        return;
    }

    ws_start_cb(app_ctx, seqno_l);

    switch (ws.type) {
    case WSDB_WS_TYPE_TRX:
        process_query_write_set(applier, app_ctx, &ws, seqno_g, seqno_l);
        break;
    case WSDB_WS_TYPE_CONN:
        process_conn_write_set(applier, app_ctx, &ws, seqno_l);
        break;
    }

    ws_start_cb(app_ctx, 0);

    /* free xdr objects */
    xdrs.x_op = XDR_FREE;
    xdr_wsdb_write_set(&xdrs, &ws);

    return;
}

enum galera_status galera_recv(void *app_ctx) {
    int rcode;
    struct job_worker *applier;

    /* we must have gcs connection */
    if (!gcs_conn) {
        return GALERA_NODE_FAIL;
    }

    applier = job_queue_new_worker(applier_queue);

    for (;;) {
        gcs_act_type_t  action_type;
        size_t          action_size;
        uint8_t        *action;
        gcs_seqno_t     seqno_g, seqno_l;

        errno = 0;
        rcode = gcs_recv(
            gcs_conn, &action_type, &action_size, &action, &seqno_g, &seqno_l
        );
	if (rcode < 0) return GALERA_CONN_FAIL;
        switch (action_type) {
        case GCS_ACT_DATA:
            process_write_set(
                applier, app_ctx, action, action_size, seqno_g, seqno_l
            );
            /* gu_free(action) causes segfault 
	     * It seems that action is allocated by gcs using 
	     * standard malloc(), so standard free() should work ok.
	     * (teemu 12.3.2008)
	     */
	    free(action);
            break;
        case GCS_ACT_SNAPSHOT:
        case GCS_ACT_PRIMARY:
        case GCS_ACT_NON_PRIMARY:
	    // Must advance queue counter even if ignoring the action
	    if (gcs_to_grab    (to_queue, seqno_l) != 0) {
		gu_fatal("Failed to grab to_queue: %llu", seqno_l);
		abort();
	    }
	    gcs_to_release (to_queue, seqno_l);
	    gu_free (action);
            break;
        default:
            return GALERA_FATAL;
        }
    }
    return GALERA_OK;
}

enum galera_status galera_cancel_commit(trx_id_t victim_trx) {
    enum galera_status ret_code;
    int rcode;
    struct timeval enter, leave, diff;
    if (Galera.repl_state != GALERA_ENABLED) return GALERA_OK;
    /* take commit mutex to be sure, committing trx does not
     * conflict with us
     */
    gettimeofday(&enter, NULL);
    gu_mutex_lock(&commit_mtx);
    gcs_seqno_t victim_seqno = wsdb_get_local_trx_seqno(victim_trx);

    /* continue to kill the victim */
    if (victim_seqno) {
        gu_info("cancelling trx commit: trx_id %llu seqno %llu", 
		victim_trx, victim_seqno);
        rcode = gcs_to_cancel(to_queue, victim_seqno);
        if (rcode) {
	    gu_warn("trx cancel fail: %d", rcode);
	    ret_code = GALERA_WARNING;
        } else {
	    ret_code = GALERA_OK;
        }
    } else {
	ret_code = GALERA_WARNING;
	wsdb_assign_trx(victim_trx, GALERA_ABORT_SEQNO, GALERA_ABORT_SEQNO);
	gu_warn("no seqno for trx, marking trx aborted: %lu", victim_trx);
    }
    gu_mutex_unlock(&commit_mtx);
    gettimeofday(&leave, NULL);
    timersub(&leave, &enter, &diff);
    gu_info("Galera cancel commit took %li sec %li usec", diff.tv_sec, diff.tv_usec);

    return ret_code;
}

enum galera_status galera_withdraw_commit(uint64_t victim_seqno) {
    enum galera_status ret_code;
    /* int rcode; */

    if (Galera.repl_state != GALERA_ENABLED) return GALERA_OK;

    /* continue to kill the victim */
    if (victim_seqno) {
        gu_info("withdrawing trx commit: %llu", victim_seqno);
#ifdef REMOVED
        rcode = gcs_to_withdraw(commit_queue, victim_seqno);
        if (rcode) {
          galera_log(GALERA_LOG_ERROR, "trx withdraw fail: %d", rcode);
          ret_code = GALERA_WARNING;
        } else {
          ret_code = GALERA_OK;
        }
#else
	ret_code = GALERA_OK;
#endif /* REMOVED */
    } else {
      ret_code = GALERA_WARNING;
      galera_log(222, "no seqno for trx, marking trx aborted: %lu", victim_seqno);
    }
    return ret_code;
}

enum galera_status galera_assign_timestamp(uint32_t timestamp) {
    if (Galera.repl_state != GALERA_ENABLED) return GALERA_OK;
    return 0;
}

uint32_t galera_get_timestamp() {
    if (Galera.repl_state != GALERA_ENABLED) return GALERA_OK;
    return 0;
}

enum galera_status galera_committed(trx_id_t trx_id) {
// @unused:    int                    rcode;

    GU_DBUG_ENTER("galera_committed");
    if (Galera.repl_state != GALERA_ENABLED) return GALERA_OK;
    GU_DBUG_PRINT("galera", ("trx: %llu", trx_id));
    /* moved the call to ...local_trx_committed to galera_commit() under
       mutex protection
     */

    {
      gcs_seqno_t seqno_l = wsdb_get_local_trx_seqno(trx_id);
      wsdb_set_local_trx_committed(trx_id);
      wsdb_delete_local_trx_info(trx_id);
      galera_log(333,"galera_committed: %llu", seqno_l);
      if (seqno_l > 0 && gcs_to_release(to_queue, seqno_l)) {
	  gu_fatal("to release failed for %llu", seqno_l);
	  abort();
      }
    }
    GU_DBUG_RETURN(GALERA_OK);
}

enum galera_status galera_rolledback(trx_id_t trx_id) {
// @unused:    int                    rcode;

    GU_DBUG_ENTER("galera_rolledback");
    if (Galera.repl_state != GALERA_ENABLED) return GALERA_OK;
    GU_DBUG_PRINT("galera", ("trx: %llu", trx_id));

    {
	gcs_seqno_t seqno_l = wsdb_get_local_trx_seqno(trx_id);
	wsdb_delete_local_trx_info(trx_id);
	gu_info("galera_rolledback: %llu", seqno_l);
	if ((seqno_l > 0  && seqno_l < GALERA_ABORT_SEQNO) 
	    && gcs_to_release(to_queue, seqno_l)) {
	    gu_warn("to release failed for %llu", seqno_l);
	}
    }
    GU_DBUG_RETURN(GALERA_OK);
}

enum galera_status galera_commit(trx_id_t trx_id, conn_id_t conn_id) {

    int                    rcode;
    struct wsdb_write_set *ws;
    XDR                    xdrs;
    int                    data_max = 34000; /* only fixed xdr buf supported */
    uint8_t                data[data_max];
    int                    len;
    gcs_seqno_t            seqno_g, seqno_l;
    enum galera_status     retcode;

    GU_DBUG_ENTER("galera_commit");
    if (Galera.repl_state != GALERA_ENABLED) return GALERA_OK;

    GU_DBUG_PRINT("galera", ("trx: %llu", trx_id));

    errno = 0;

    /* hold commit time mutex */
    gu_mutex_lock(&commit_mtx);

    /* check if trx was cancelled before we got here */
    if (wsdb_get_local_trx_seqno(trx_id) == GALERA_ABORT_SEQNO) {
	galera_log(GALERA_LOG_INFO,"trx has been cancelled already: %llu", trx_id);
	if ((rcode = wsdb_delete_local_trx(trx_id))) {
	    galera_log(rcode, "could not delete trx: %llu", trx_id);
	}
	gu_mutex_unlock(&commit_mtx);
	GU_DBUG_RETURN(GALERA_TRX_FAIL);
    }

    /* retrieve write set */
    ws = wsdb_get_write_set(trx_id, conn_id);
    if (!ws) {
        /* this is possibly autocommit query, need to let it continue */
        gu_mutex_unlock(&commit_mtx);
        gu_warn("empty local trx ws %llu", trx_id);
        GU_DBUG_RETURN(GALERA_OK);
        //return GALERA_TRX_FAIL;
    }



    /* encode with xdr */
    /* TODO: is not optimal to allocate data buffer for xdr encoding
     *       intermediate result.
     *       Should use xdrrec stream instead and encode directly on
     *       gcs channel as we go.
     */
    memset(data, 0, data_max);
    xdrmem_create(&xdrs, (char *)data, data_max, XDR_ENCODE);
    if (!xdr_wsdb_write_set(&xdrs, ws)) {
        galera_log(GALERA_TRX_FAIL,"xdr failed for: %llu", trx_id);
        gu_mutex_unlock(&commit_mtx);
        GU_DBUG_RETURN(GALERA_TRX_FAIL);
    }
    len = xdr_getpos(&xdrs);

    /* */
    gu_mutex_unlock(&commit_mtx);

    /* replicate through gcs */
    rcode= gcs_repl(gcs_conn, GCS_ACT_DATA, len, data, &seqno_g, &seqno_l);
    if (rcode < 0) {
        galera_log(GALERA_CONN_FAIL,"gcs failed for: %llu, %d", trx_id, rcode);
        GU_DBUG_RETURN(GALERA_CONN_FAIL);
    }

    gu_mutex_lock(&commit_mtx);

    /* check if trx was cancelled before we got here */
    if (wsdb_get_local_trx_seqno(trx_id) == GALERA_ABORT_SEQNO) {
	gu_info("trx has been cancelled during rcs_repl(): trx_id %llu  seqno_l %llu", 
		trx_id, seqno_l);
	/* Assign seqnos anyway, galera_rollbacked() uses it to release to */
	wsdb_assign_trx(trx_id, seqno_l, seqno_g);
	gu_mutex_unlock(&commit_mtx);

	/* Call self cancel to allow gcs_to_release() to skip this seqno */
	gcs_to_self_cancel(to_queue, seqno_l);
	GU_DBUG_RETURN(GALERA_TRX_FAIL);
    }
    
    /* record seqnos for local transaction */
    wsdb_assign_trx(trx_id, seqno_l, seqno_g);
    gu_mutex_unlock(&commit_mtx);
    
    /* wait for total order */
    galera_log(333, "gcs_to_grab for: %llu", seqno_l);
    rcode = gcs_to_grab(to_queue, seqno_l);
    
    if (rcode) {
	gu_warn("gcs_to_grab aborted: %d seqno %llu", rcode, seqno_l);
	retcode = GALERA_TRX_FAIL;
	goto after_cert_test;
    }
    
    galera_log(333,"local trx commit starting: %llu", seqno_g);
    
    /* certification test */
    //print_ws(wslog_L, ws, seqno_l);
    rcode = wsdb_append_write_set(seqno_g, ws);
    switch (rcode) {
    case WSDB_OK:
        /* certification ok */
        retcode = GALERA_OK;
        galera_log(333,"local trx commit certified: %llu - %llu", seqno_g, ws->last_seen_trx);
        break;
    case WSDB_CERTIFICATION_FAIL:
        /* certification failed, release */
        retcode = GALERA_TRX_FAIL;
        gu_info("local trx commit certification failed: %llu - %llu",
		seqno_l, ws->last_seen_trx);
        print_ws(wslog_L, ws, seqno_l);
        break;
    default:  
        retcode = GALERA_CONN_FAIL;
        gu_warn("wsdb append failed: seqno_g %llu seqno_l %llu", seqno_g, seqno_l);
        break;
    }
    
after_cert_test:
    wsdb_write_set_free(ws);
    GU_DBUG_RETURN(retcode);
}

enum galera_status galera_append_query(trx_id_t trx_id, char *query) {

    if (Galera.repl_state != GALERA_ENABLED) return GALERA_OK;

    errno = 0;
    switch (wsdb_append_query(trx_id, query)) {
    case WSDB_OK:              return GALERA_OK;
    case WSDB_ERR_TRX_UNKNOWN: return GALERA_TRX_FAIL;
    default:                   return GALERA_CONN_FAIL;
    }
    return GALERA_OK;
}

enum galera_status galera_append_row(
    trx_id_t trx_id,
    uint16_t len,
    uint8_t *data
) {
    if (Galera.repl_state != GALERA_ENABLED) return GALERA_OK;
    errno = 0;

    switch(wsdb_append_row(trx_id, len, data)) {
    case WSDB_OK:               return GALERA_OK;
    case WSDB_ERR_TRX_UNKNOWN:  return GALERA_TRX_FAIL;
    default:                    return GALERA_CONN_FAIL;
    }
}

enum galera_status galera_append_row_key(
    trx_id_t trx_id,
    char    *dbtable,
    uint16_t dbtable_len,
    uint8_t *key,
    uint16_t key_len,
    enum galera_action action
) {
    struct wsdb_key_rec   wsdb_key;
    struct wsdb_table_key table_key;
    struct wsdb_key_part  key_part;
    char wsdb_action  = WSDB_ACTION_UPDATE;

    if (Galera.repl_state != GALERA_ENABLED) return GALERA_OK;
    errno = 0;

    /* TODO: make this setupping static, needs mutex protection */
    wsdb_key.key             = &table_key;
    table_key.key_part_count = 1;
    table_key.key_parts      = &key_part;
    key_part.type            = WSDB_TYPE_VOID;

    /* assign key info */
    wsdb_key.dbtable     = dbtable;
    wsdb_key.dbtable_len = dbtable_len;
    key_part.length      = key_len;
    key_part.data        = key;

    switch (action) {
    case GALERA_UPDATE: wsdb_action=WSDB_ACTION_UPDATE; break;
    case GALERA_DELETE: wsdb_action=WSDB_ACTION_DELETE; break;
    case GALERA_INSERT: wsdb_action=WSDB_ACTION_INSERT; break;
    }

    switch(wsdb_append_row_key(trx_id, &wsdb_key, wsdb_action)) {
    case WSDB_OK:               return GALERA_OK;
    case WSDB_ERR_TRX_UNKNOWN:  return GALERA_TRX_FAIL;
    default:                    return GALERA_CONN_FAIL;
    }
}

enum galera_status galera_set_variable(
    conn_id_t  conn_id,
    char *key,   uint16_t key_len, 
    char *query, uint16_t query_len
) {
    if (Galera.repl_state != GALERA_ENABLED) return GALERA_OK;

    errno = 0;
    switch(wsdb_store_set_variable(conn_id, key, key_len, query, query_len)) {
    case WSDB_OK:              return GALERA_OK;
    case WSDB_ERR_TRX_UNKNOWN: return GALERA_TRX_FAIL;
    default:                   return GALERA_CONN_FAIL;
    }
    return GALERA_OK;
}

enum galera_status galera_set_database(
    conn_id_t conn_id, char *query, uint16_t query_len
) {
    if (Galera.repl_state != GALERA_ENABLED) return GALERA_OK;

    errno = 0;
    switch(wsdb_store_set_database(conn_id, query, query_len)) {
    case WSDB_OK:              return GALERA_OK;
    case WSDB_ERR_TRX_UNKNOWN: return GALERA_TRX_FAIL;
    default:                   return GALERA_CONN_FAIL;
    }
    return GALERA_OK;
}

enum galera_status galera_to_execute_start(
    conn_id_t conn_id, char *query, uint16_t query_len
) {

    int                    rcode;
    struct wsdb_write_set *ws;
    XDR                    xdrs;
    int                    data_max = 34000; /* only fixed xdr buf supported */
    uint8_t                data[data_max];
    int                    len;
    gcs_seqno_t            seqno_g, seqno_l;

    GU_DBUG_ENTER("galera_to_execute_start");
    if (Galera.repl_state != GALERA_ENABLED) return GALERA_OK;
    GU_DBUG_PRINT("galera", ("conn: %llu", conn_id));

    /* retrieve write set */
    ws = wsdb_get_conn_write_set(conn_id);
    if (!ws) {
        GU_DBUG_RETURN(GALERA_CONN_FAIL);
    }

    /* append the query to be executed */
    wsdb_set_exec_query(ws, query, query_len);

    /* encode with xdr */
    /* TODO: is not optimal to allocate data buffer for xdr encoding
     *       intermediate result.
     *       Should use xdrrec stream instead and encode directly on
     *       gcs channel as we go.
     */
    memset(data, 0, data_max);
    xdrmem_create(&xdrs, (char *)data, data_max, XDR_ENCODE);
    if (!xdr_wsdb_write_set(&xdrs, ws)) {
        galera_log(GALERA_CONN_FAIL,"xdr failed for: %llu", conn_id);
        GU_DBUG_RETURN(GALERA_CONN_FAIL);
    }
    len = xdr_getpos(&xdrs);

    /* replicate through gcs */
    rcode= gcs_repl(gcs_conn, GCS_ACT_DATA, len, data, &seqno_g, &seqno_l);
    if (rcode < 0) {
        galera_log(GALERA_CONN_FAIL,"gcs failed for: %llu, %d", conn_id, rcode);
        GU_DBUG_RETURN(GALERA_CONN_FAIL);
    }

    /* wait for total order */
    if (gcs_to_grab(to_queue, seqno_l) != 0) {
	gu_fatal("Failed to grab to_queue: %llu", seqno_l);
	abort();
    }
    
    /* record sequence number in connection info */
    conn_set_seqno(conn_id, seqno_g);

    /* release write set */
    wsdb_write_set_free(ws);

    GU_DBUG_RETURN(GALERA_OK);
}

enum galera_status galera_to_execute_end(conn_id_t conn_id) {
    gcs_seqno_t seqno;

    GU_DBUG_ENTER("galera_to_execute_end");
    if (Galera.repl_state != GALERA_ENABLED) return GALERA_OK;

    seqno = conn_get_seqno(conn_id);
    if (!seqno) {
        galera_log(GALERA_CONN_FAIL, "missing connection seqno: %llu",conn_id);
        GU_DBUG_RETURN(GALERA_CONN_FAIL);
    }

    /* release total order */
    gcs_to_release(to_queue, seqno);

    /* cleanup seqno reference */
    conn_set_seqno(conn_id, 0);
    
#ifdef REMOVED
    if (gcs_to_grab    (commit_queue, seqno) != 0) {
	gu_fatal("Failed to grab to_queue: %llu", seqno);
	abort();
    }
    gcs_to_release (commit_queue, seqno);
#endif /* REMOVED */

    GU_DBUG_RETURN(WSDB_OK);
}
