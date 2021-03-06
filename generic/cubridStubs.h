/*
 *-----------------------------------------------------------------------------
 *
 * cubridStubs.h --
 *
 *	Stubs for procedures in cubridStubDefs.txt
 *
 * Generated by genExtStubs.tcl: DO NOT EDIT
 * 2017-05-12 11:56:09Z
 *
 *-----------------------------------------------------------------------------
 */

typedef struct cubridStubDefs {

    /* Functions from libraries: libcascci cascci */

    int (*cci_connect_with_url_exPtr)(char *url, char *user, char *pass, T_CCI_ERROR * err_buf);
    int (*cci_disconnectPtr)(int conn_handle, T_CCI_ERROR *err_buf);
    int (*cci_get_db_versionPtr)(int con_handle, char *out_buf, int buf_size);
    CCI_AUTOCOMMIT_MODE (*cci_get_autocommitPtr)(int conn_handle);
    int (*cci_set_autocommitPtr)(int conn_handle, CCI_AUTOCOMMIT_MODE  autocommit_mode);
    int (*cci_get_db_parameterPtr)(int con_handle, T_CCI_DB_PARAM param_name, void *value, T_CCI_ERROR * err_buf);
    int (*cci_set_db_parameterPtr)(int con_handle, T_CCI_DB_PARAM param_name, void *value, T_CCI_ERROR * err_buf);
    int (*cci_end_tranPtr)(int conn_handle, char type, T_CCI_ERROR *err_buf);
    int (*cci_get_last_insert_idPtr)(int con_h_id, void *value, T_CCI_ERROR * err_buf);
    int (*cci_preparePtr)(int conn_handle, char *sql_stmt, char flag, T_CCI_ERROR *err_buf);
    int (*cci_row_countPtr)(int conn_handle, int *row_count, T_CCI_ERROR * err_buf);
    int (*cci_clob_newPtr)(int conn_handle, T_CCI_CLOB* clob, T_CCI_ERROR* error_buf);
    int (*cci_clob_readPtr)(int conn_handle, T_CCI_CLOB clob, long start_pos, int length, char *buf, T_CCI_ERROR* error_buf);
    int (*cci_clob_writePtr)(int conn_handle, T_CCI_CLOB clob, long start_pos, int length, const char *buf, T_CCI_ERROR* error_buf);
    int (*cci_clob_freePtr)(T_CCI_CLOB clob);
    int (*cci_blob_newPtr)(int conn_handle, T_CCI_BLOB* blob, T_CCI_ERROR* error_buf);
    int (*cci_blob_readPtr)(int conn_handle, T_CCI_BLOB blob, long start_pos, int length, char *buf, T_CCI_ERROR* error_buf);
    int (*cci_blob_writePtr)(int conn_handle, T_CCI_BLOB blob, long start_pos, int length, const char *buf, T_CCI_ERROR* error_buf);
    int (*cci_blob_freePtr)(T_CCI_BLOB blob);
    int (*cci_bind_paramPtr)(int req_handle, int index, T_CCI_A_TYPE a_type, void *value, T_CCI_U_TYPE u_type, char flag);
    int (*cci_executePtr)(int req_handle, char flag, int max_col_size, T_CCI_ERROR *err_buf);
    int (*cci_cursorPtr)(int req_handle, int offset, T_CCI_CURSOR_POS origin, T_CCI_ERROR *err_buf);
    int (*cci_fetchPtr)(int req_handle, T_CCI_ERROR *err_buf);
    int (*cci_get_dataPtr)(int req_handle, int col_no, int type, void *value, int *indicator);
    T_CCI_COL_INFO* (*cci_get_result_infoPtr)(int req_handle, T_CCI_CUBRID_STMT *stmt_type, int *num);
    int (*cci_close_req_handlePtr)(int req_handle);
    int (*cci_set_makePtr)(T_CCI_SET * set, T_CCI_U_TYPE u_type, int size, void *value, int *indicator);
    int (*cci_set_getPtr)(T_CCI_SET set, int index, T_CCI_A_TYPE a_type, void *value, int *indicator);
    int (*cci_set_sizePtr)(T_CCI_SET set);
    void (*cci_set_freePtr)(T_CCI_SET set);
} cubridStubDefs;
#define cci_connect_with_url_ex (cubridStubs->cci_connect_with_url_exPtr)
#define cci_disconnect (cubridStubs->cci_disconnectPtr)
#define cci_get_db_version (cubridStubs->cci_get_db_versionPtr)
#define cci_get_autocommit (cubridStubs->cci_get_autocommitPtr)
#define cci_set_autocommit (cubridStubs->cci_set_autocommitPtr)
#define cci_get_db_parameter (cubridStubs->cci_get_db_parameterPtr)
#define cci_set_db_parameter (cubridStubs->cci_set_db_parameterPtr)
#define cci_end_tran (cubridStubs->cci_end_tranPtr)
#define cci_get_last_insert_id (cubridStubs->cci_get_last_insert_idPtr)
#define cci_prepare (cubridStubs->cci_preparePtr)
#define cci_row_count (cubridStubs->cci_row_countPtr)
#define cci_clob_new (cubridStubs->cci_clob_newPtr)
#define cci_clob_read (cubridStubs->cci_clob_readPtr)
#define cci_clob_write (cubridStubs->cci_clob_writePtr)
#define cci_clob_free (cubridStubs->cci_clob_freePtr)
#define cci_blob_new (cubridStubs->cci_blob_newPtr)
#define cci_blob_read (cubridStubs->cci_blob_readPtr)
#define cci_blob_write (cubridStubs->cci_blob_writePtr)
#define cci_blob_free (cubridStubs->cci_blob_freePtr)
#define cci_bind_param (cubridStubs->cci_bind_paramPtr)
#define cci_execute (cubridStubs->cci_executePtr)
#define cci_cursor (cubridStubs->cci_cursorPtr)
#define cci_fetch (cubridStubs->cci_fetchPtr)
#define cci_get_data (cubridStubs->cci_get_dataPtr)
#define cci_get_result_info (cubridStubs->cci_get_result_infoPtr)
#define cci_close_req_handle (cubridStubs->cci_close_req_handlePtr)
#define cci_set_make (cubridStubs->cci_set_makePtr)
#define cci_set_get (cubridStubs->cci_set_getPtr)
#define cci_set_size (cubridStubs->cci_set_sizePtr)
#define cci_set_free (cubridStubs->cci_set_freePtr)
MODULE_SCOPE cubridStubDefs *cubridStubs;
