// Copyright (C) 2007 Codership Oy <info@codership.com>

/*!
 * @file wsdb_api.h
 * @brief Write Set DataBase (WSDB) API
 *
 * This API can be used both from database engine and replication
 * framework.
 *
 */
#ifndef WSDB_API
#define WSDB_API

#include <limits.h>
#include <stdint.h>
#include <time.h>
#include <rpc/xdr.h>

/* WSDB severity codes */
#define WSDB_OK      0 //!< success
#define WSDB_INFO    1 //!< success
#define WSDB_WARNING 2 //!< minor warning, error logged
#define WSDB_ERROR   3 //!< statement aborted, server can continue
#define WSDB_FATAL   4 //!< fatal error, server must abort

/* WSDB detailed return codes */
#define WSDB_CERTIFICATION_PASS 101 //!<  certification test passed
#define WSDB_CERTIFICATION_FAIL 102 //!< certification test failed
#define WSDB_ERR_TRX_UNKNOWN    201
#define WSDB_ERR_TOO_LONG_KEY   202
#define WSDB_ERR_NO_KEY         203
#define WSDB_ERR_BAD_ACTION     204
#define WSDB_ERR_WS_FAIL        205
#define WSDB_ERR_BAD_QUERY      206
#define WSDB_ERR_CONN_UNKNOWN   207
#define WSDB_ERR_CONN_FAIL      208
#define WSDB_ERR_BAD_WRITE_SET  209
#define WSDB_ERR_FILE_OPEN      301
#define WSDB_ERR_FILE_END       302
#define WSDB_ERR_FILE_WRITE     303
#define WSDB_ERR_FILE_DELETE    304
#define WSDB_ERR_FILE_NOTFOUND  305
#define WSDB_ERR_CACHE          401
#define WSDB_ERR_MUTEX          501
#define WSDB_ERR_ARRAY_EMPTY    601
#define WSDB_ERR_ARRAY_SIZE     602
#define WSDB_ERR_ARRAY_FAIL     603


#define DEFAULT_WORK_DIR "/var/wsdb/data"
#define DEFAULT_CERT_FILE "wsdbtrx"
#define DEFAULT_LOCAL_FILE "wsdblocal"
#define PATH_SEPARATOR "/"
#define DEFAULT_BLOCK_SIZE  2048
#define DEFAULT_FILE_SIZE   10000

#define MAX_KEY_LEN 1024

/* @struct
   @brief wsdb state information
*/
struct wsdb_info {
    uint32_t local_trx_count;
};

/* @typedef
 * types for local and global transaction identifiers
 */
typedef uint64_t connid_t;
typedef uint64_t local_trxid_t;
typedef uint64_t trx_seqno_t;

/* special seqno to designate a cancelled transaction */
#ifndef ULLONG_MAX
#   define GALERA_ABORT_SEQNO 18446744073709551615ULL
#else
#   define GALERA_ABORT_SEQNO ULLONG_MAX
#endif


#ifdef REMOVED
/*! @enum 
 * @brief action codes 
 */
enum wsdb_action {
    INSERT = 1, /*!< insert operation */
    UPDATE,     /*!< update operation */
    DELETE,     /*!< delete operation */
};
#endif

#define WSDB_ACTION_INSERT 'I'
#define WSDB_ACTION_DELETE 'D'
#define WSDB_ACTION_UPDATE 'U'

#define WSDB_TYPE_CHAR   'C'
#define WSDB_TYPE_FLOAT  'F'
#define WSDB_TYPE_INT    'I'
#define WSDB_TYPE_BLOB   'B'
#define WSDB_TYPE_VOID   'V'

enum wsdb_ws_type {
    WSDB_WS_TYPE_TRX = 1, /*!< ws contains one transaction */
    WSDB_WS_TYPE_CONN,    /*!< ws contains one query for direct execution */
};

enum wsdb_ws_level {
    WSDB_WS_DATA_ROW = 1, /*!< ws consists of data rows */
    WSDB_WS_DATA_COLS,    /*!< ws consists of modified data columns */
    WSDB_WS_QUERY         /*!< ws is represented by the original SQL query */
};

/*! transaction executes in local state until it begins the committing */
enum wsdb_trx_state {
    LOCAL,      /*!< TRX is executing in local state */
    COMMITTING, /*!< TRX has replicated and is in committing state */
    COMMITTED,  /*!< TRX has certified successfully */
};
struct wsdb_table_name_rec {
    char *db_name;
    char *table_name;
};

struct wsdb_key_part {
    char      type;   //!< key column data type
    uint16_t  length; //!< length of key data
    void     *data;   //!< key data
};

struct wsdb_table_key {
    uint16_t              key_part_count; //!< number of parts in the key
    struct wsdb_key_part *key_parts;      //!< key part information
};

struct wsdb_key_rec {
    char                  *dbtable;     //!< dbname.tablename
    uint16_t               dbtable_len; //!< dbtable does not end with 0
    struct wsdb_table_key *key;         //!< unique key for the table
};

struct wsdb_col_data_rec {
    uint16_t column;  //!< column id
    char data_type;   //!< data type for column
    uint16_t length;  //!< length of column data
    void *data;       //!< data for column
};

struct wsdb_cols_data_rec {
    uint16_t                  col_count; //!< column count
    struct wsdb_col_data_rec *data;      //!< data for column values
};
struct wsdb_row_data_rec {
    uint16_t length;  //!< length of column data
    void *data;       //!< data for column
};

enum wsdb_item_data_mode {
    NO_DATA = 0,
    COLUMN = 1,
    ROW = 2
};

/*!
 * @struct
 * @brief item in write set
 */
struct wsdb_item_rec {
    char                  action;     //!< action code, WSDB_ACTION_*
    struct wsdb_key_rec  *key;        //!< key for row
    enum wsdb_item_data_mode data_mode; //!< 'C' columns 'R' rows
    union {
        struct wsdb_cols_data_rec cols;
        struct wsdb_row_data_rec  row;
    } u;
};

/*!
 * @struct
 * @brief future extension to assign timestamp for query
 */
struct wsdb_query {
    char     *query;      //!< the SQL query string
    uint16_t  query_len;  //!< length of query string
#ifdef TODO
    time_t    timeval;    //!< time value used for query processing
#endif
};

//typedef void (*free_wsdb_write_set_fun)(struct wsdb_write_set *);
/*!
 * @struct
 * @brief write set representation
 */
struct wsdb_write_set {
    local_trxid_t         local_trx_id;  //!< id of transaction in local state
    trx_seqno_t           last_seen_trx; //!< id of last committed trx
    enum wsdb_ws_type     type;
    enum wsdb_ws_level    level;
    enum wsdb_trx_state   state;
    u_int16_t             query_count;   //!< number of queries in trx buffer
    //char                **queries;
    struct wsdb_query    *queries;       //!< trx query buffer
    u_int16_t             conn_query_count; //!< number of connection queries
    struct wsdb_query    *conn_queries;      //!< query buffer
    uint16_t              item_count;    //!< number of items in write set
    struct wsdb_item_rec *items;         //!< write set items

    //free_wsdb_write_set_fun free;
};

/*!
 * @brief callback for handling log messages
 * 
 * @param code  severity code
 * @param msg   log message
 */
typedef void (*wsdb_log_cb_t) (int code, const char* msg);

/*! 
 * @brief database initialization. This must be called at system
 *        initialization time once before any other wsdb_* function
 *
 * @param data_dir working directory
 * @param logger external logging function. WSDB will call this.
 * @return wsdb success code
 * @retval WSDB_OK successful operation 
 * @retval WSDB_ERROR wsdb could not initialize, must abort
 */
int wsdb_init(const char *data_dir, wsdb_log_cb_t logger);

/*! @brief creates a write set for a transaction 
 *
 * This can turn out to be obsolote, as write sets can be
 * created implicitly during thye first append in the set.
 *
 * @param trx_id id of the local transaction
 */
int wsdb_create_write_set(local_trxid_t trx_id);

/*
 * @brief appends a query in trx'ns write set.
 * 
 * The queries (statements) can be used for applying
 * the write set. 
 * In the future, we will possibly record not just the 
 * SQL query, but some representation of the AST
 * (Abstract Syntax Tree for the parsed query) as well.
 *
 * @param trx_id id of the local state transaction
 * @param query  the SQL query
 *
 * @return success or error code
 * @retval WSDB_OK 
 */
int wsdb_append_query(local_trxid_t trx_id, char *query);
int wsdb_append_command(local_trxid_t trx_id, char *command);

/*
 * @brief appends one row in trx'ns write set, in local state
 * 
 * This function is a future extension. It allows inserting binary
 * mode data in the write set and makes it possible to optimize 
 * between SQL statement and binary row applying methods.
 *
 * @return success or error code
 * @retval WSDB_OK 
 */
int wsdb_append_row(
    local_trxid_t trx_id, uint16_t len, void *data
);

/*
 * @brief appends one row in trx'ns write set, in local state
 * 
 * This function is a future extension. It allows inserting binary
 * mode data in the write set and makes it possible to optimize 
 * between SQL statement and binary row applying methods.
 *
 * @return success or error code
 * @retval WSDB_OK 
 */
int wsdb_append_row_col(
    local_trxid_t trx_id, char *dbtable, uint16_t dbtable_len,
    uint16_t col, char data_type, uint16_t len, void *data
);

/*!
 * Appends the row modification reference. The modified data is not stored
 * with this call, only the reference to the modified data is recorded.
 * This method can be used if write set representation is chosen to be of
 * type WSDB_WS_QUERY. Then applying of the write set is
 * performed by executing the sql queries for the transaction.
 * 
 * Transaction calling this function operates in local state and the cluster
 * level transaction sequence number is not known yet. 
 *
 * @param trx_id locally unique trx identifier or 0. 
 * @param key key of the row modified
 * @param action the action performed on the row (INSERT, DELETE, UPDATE)
 *
 * @return success or error code 
 *  
 */ 
int wsdb_append_row_key(
    local_trxid_t trx_id,
    struct wsdb_key_rec *key, 
    char action
);

/*!
 * Appends the table modification reference. The modified data is not stored
 * with this call, only the reference to the modified data is recorded.
 * 
 * Transaction calling this function operates in local state and the cluster
 * level transaction sequence number is not known yet. 
 *
 * @param trx_id locally unique trx identifier or 0. 
 * @param dbtable name of the table modified in format: db.table
 * @param len length of the dbtable
 *
 * @return success or error code 
 *  
 */ 
int wsdb_append_table_lock(
    local_trxid_t  trx_id,
    char          *dbtable,
    uint16_t       len
);

/*!
 * @brief appends whole write_set
 * 
 * This method can be called after replication has received the write set.
 * Caller must provide the write set received from replication intact and the 
 * sequence number determined for the replication event.
 * 
 * Note that the write set may contain local state transaction identifiers, 
 * these will be replaced by the sequence number passed as parameter.
 * 
 * @return Success code. The write set appennd may fail for certification test.
 * 
 * @param trx_seqno  the cluster wide agreed commit order for the transaction.
 * @param write_set  The write set to be appended
 * 
 * @return success code, certification fail code or error code
 * @retval WSDB_OK
 * @retval WSDB_CERTIFICATION_FAIL certification failed, trx must abort
 */
int wsdb_append_write_set(
     trx_seqno_t trx_seqno, struct wsdb_write_set *write_set
);
 
 /*!
  * @brief changes TRX state to committing state
  * 
  * This method can be called during commit statement processing
  * after transactions write set has been read (by wsdb_get_write_set()).
  *
  * When a transaction state is in committing state, its write set
  * is not needed in the cache.
  *
  * It looks probable, that this function will be obsolete in the next 
  * release and cache release is performed inside wsdb_get_write_set().
  */
int wsdb_set_trx_committing(local_trxid_t trx_id);

 /*!
  * @brief changes TRX state to committed state.
  *
  * This can be called when applying of the write set is over.
  * The function records the last committed transaction seqno,
  * which is used to define the certification domain.
  *
  */
int wsdb_set_global_trx_committed(trx_seqno_t trx_seqno);
int wsdb_set_local_trx_committed(local_trxid_t trx_id);
int wsdb_assign_trx(
    local_trxid_t trx_id, trx_seqno_t seqno_l, trx_seqno_t seqno_g);

 /*!
  * @brief returns the local seqno associated with transaction
  *
  * This can be called after transaction has associated seqnos 
  */
trx_seqno_t wsdb_get_local_trx_seqno(local_trxid_t trx_id);
 
/*!
 * @brief removes transaction's write set from wsdb
 *
 * Both local and global versions are needed...
 *
 */
int wsdb_delete_local_trx(local_trxid_t trx_id );
int wsdb_delete_local_trx_info(local_trxid_t trx_id );
int wsdb_delete_global_trx(trx_seqno_t trx_id );

/*!
 * @brief returns the whole write set for a transaction.
 * 
 * 
 * @param trx_id the id of the transaction
 * @param conn_id the id of the connection
 * 
 * @return the write set of the transaction. This is ready to be
 *         replicated to the cluster
 */
struct wsdb_write_set *wsdb_get_write_set(
    local_trxid_t trx_id, connid_t conn_id
);

/*!
 * @brief returns the write set for a connection.
 * Write set will be of type WSDB_WS_TYPE_CONN
 *
 * @param conn_id the id of the connection
 * 
 * @return the write set of the connection
 */
struct wsdb_write_set *wsdb_get_conn_write_set(
    connid_t conn_id
);

/*!
 * @brief returns the write set for a connection.
 * Write set will be of type WSDB_WS_TYPE_CONN
 *
 * @param conn_id the id of the connection
 * 
 * @return the write set of the connection
 */
int wsdb_set_exec_query(
    struct wsdb_write_set *ws, char *query, uint16_t query_len
);

/*!
 * @brief XDR encoding/decoding for the write set.
 * 
 * Replicator should do the encoding before replicating the 
 * write set.
 * Replication receiver decodes the write set received from
 * group communication.
 * 
 * @param xdrs XDR stream 
 * @param ws the write set to be encoded/decoded
 * 
 * @return true/false
 */
//typedef int bool;
bool_t xdr_wsdb_write_set(XDR *xdrs, struct wsdb_write_set *ws);

/*!
 * @brief frees the write set structures.
 */
void wsdb_write_set_free(struct wsdb_write_set *ws);

/*!
 * @brief returns the queries for a transaction.
 * 
 * This should be called after transaction has successfully certified.
 * The returned queries can be used to apply the write set in slave nodes.
 * 
 * @param trx_seqno the sequence number of the transaction
 * 
 * @return the query block for the transaction
 */
struct wsdb_query_block *wsdb_get_write_set_queries(trx_seqno_t trx_seqno);

/*!
 * @brief certifies a transaction
 * 
 * Obsolete
 *
 * @return certification test result 
 * @retval WSDB_CERTIFIED Certification test passed, TRX can commit
 * @retval WSDB_CONFLICT  Certification test failed, TRX must abort
 */
int wsdb_certify(trx_seqno_t trx_seqno);

/*!
 * @brief builds connection management queries for write set
 *
 * @param conn_id ID for the connection
 * @param key unique key for the variable (name)
 * @param data the SQL query to set the variable value
 */
int wsdb_store_set_variable(
    connid_t conn_id, 
    char *key,  uint16_t key_len, 
    char *data, uint16_t data_len
);

/*!
 * @brief stores set dabase query for connection
 *
 * @param conn_id ID for the connection
 * @param set_db the set/use default database command
 */
int wsdb_store_set_database(
    connid_t conn_id, char *set_db, uint16_t set_db_len
);


#endif
