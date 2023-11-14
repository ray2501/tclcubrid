#ifdef __cplusplus
extern "C" {
#endif

#include <tcl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "cas_cci.h"
#include "broker_cas_error.h"
#include "cubridStubs.h"

MODULE_SCOPE Tcl_LoadHandle CubridInitStubs(Tcl_Interp*);

/*
 * Only the _Init function is exported.
 */
extern DLLEXPORT int    Cubrid_Init(Tcl_Interp * interp);

#ifdef __cplusplus
}
#endif


/*
 * This struct is to record CUBRID database info
 */
struct CUBRIDDATA {
    int          connection;
    Tcl_Interp   *interp;
};

typedef struct CUBRIDDATA CUBRIDDATA;

struct CLOBDataLink {
    T_CCI_CLOB clob;
    struct CLOBDataLink *next;
};

typedef struct CLOBDataLink CLOBDataLink;

struct BLOBDataLink {
    T_CCI_BLOB blob;
    struct BLOBDataLink *next;
};

typedef struct BLOBDataLink BLOBDataLink;

/*
 * This struct is to record statement info
 */
struct CUBRIDStmt {
    int          request;
    CLOBDataLink *cloblink;
    BLOBDataLink *bloblink;
};

typedef struct CUBRIDStmt CUBRIDStmt;

typedef struct ThreadSpecificData {
  int initialized;                  /* initialization flag */
  Tcl_HashTable *cubrid_hashtblPtr; /* per thread hash table. */
  int stmt_count;
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;

TCL_DECLARE_MUTEX(myMutex);

/*
 * For Tcl_LoadFile
 */
static Tcl_Mutex cubridMutex;
static int cubridRefCount = 0;
static Tcl_LoadHandle cubridLoadHandle = NULL;

#define CUBRID_LOB_READ_BUF_SIZE 1048576


/*
 * cubrid_str2bit is from CUBRID database driver source code
 */
static char* cubrid_str2bit(char* str)
{
    int i=0,len=0,t=0;
    char* buf=NULL;
    int shift = 8;

    if(str == NULL)
        return NULL;
    len = strlen(str);

    if(0 == len%shift)
        t =1;

    buf = (char*)malloc(len/shift+1+1);
    memset(buf,0,len/shift+1+1);

    for(i=0;i<len;i++)
    {
        if(str[len-i-1] == '1')
        {
            buf[len/shift - i/shift-t] |= (1<<(i%shift));
        }
        else if(str[len-i-1] == '0')
        {
            //nothing
        }
        else
        {
            return NULL;
        }
    }
    return buf;
}


void CUBRID_Thread_Exit(ClientData clientdata)
{
  /*
   * This extension records hash table info in ThreadSpecificData,
   * so we need delete it when we exit a thread.
   */
  Tcl_HashSearch search;
  Tcl_HashEntry *entry;

  ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
      Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

  if(tsdPtr->cubrid_hashtblPtr) {
    for (entry = Tcl_FirstHashEntry(tsdPtr->cubrid_hashtblPtr, &search);
         entry != NULL;
         entry = Tcl_FirstHashEntry(tsdPtr->cubrid_hashtblPtr, &search)) {
        // Try to delete entry
        Tcl_DeleteHashEntry(entry);
    }

    Tcl_DeleteHashTable(tsdPtr->cubrid_hashtblPtr);
    ckfree(tsdPtr->cubrid_hashtblPtr);
  }
}


/*
 * Handle cubrid command delete, unload library.
 */
static void DbDeleteCmd(void *db) {
  CUBRIDDATA *pDb = (CUBRIDDATA*)db;
  T_CCI_ERROR cci_error;

  if(pDb->connection > 0) {
    cci_disconnect (pDb->connection, &cci_error);
    pDb->connection = 0;
  }

  Tcl_Free((char*)pDb);
  pDb = 0;

  Tcl_MutexLock(&cubridMutex);
  if (--cubridRefCount == 0) {
      Tcl_FSUnloadFile(NULL, cubridLoadHandle);
      cubridLoadHandle = NULL;
  }
  Tcl_MutexUnlock(&cubridMutex);
}


/*
 * STMT_HANDLE command function
 */
static int CUBRID_STMT(void *cd, Tcl_Interp *interp, int objc,Tcl_Obj *const*objv){
  CUBRIDDATA *pDb = (CUBRIDDATA *) cd;
  CUBRIDStmt *pStmt;
  Tcl_HashEntry *hashEntryPtr;
  int choice;
  int rc = TCL_OK;
  char *mHandle;

  ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
      Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

  if (tsdPtr->initialized == 0) {
    tsdPtr->initialized = 1;
    tsdPtr->cubrid_hashtblPtr = (Tcl_HashTable *) ckalloc(sizeof(Tcl_HashTable));
    Tcl_InitHashTable(tsdPtr->cubrid_hashtblPtr, TCL_STRING_KEYS);
  }

  static const char *STMT_strs[] = {
    "bind",
    "execute",
    "cursor",
    "fetch_row_list",
    "fetch_row_dict",
    "columns",
    "columntype",
    "close",
    0
  };

  enum STMT_enum {
    STMT_BIND,
    STMT_EXECUTE,
    STMT_CURSOR,
    STMT_FETCH_ROW_LIST,
    STMT_FETCH_ROW_DICT,
    STMT_COLUMNS,
    STMT_COLUMNTYPE,
    STMT_CLOSE
  };

  if( objc < 2 ){
    Tcl_WrongNumArgs(interp, 1, objv, "SUBCOMMAND ...");
    return TCL_ERROR;
  }

  if( Tcl_GetIndexFromObj(interp, objv[1], STMT_strs, "option", 0, &choice) ){
    return TCL_ERROR;
  }

  /*
   * Get back CUBRIDStmt pointer from our hash table
   */
  mHandle = Tcl_GetStringFromObj(objv[0], 0);
  hashEntryPtr = Tcl_FindHashEntry( tsdPtr->cubrid_hashtblPtr, mHandle );
  if( !hashEntryPtr ) {
    if( interp ) {
        Tcl_Obj *resultObj = Tcl_GetObjResult( interp );
        Tcl_AppendStringsToObj( resultObj, "invalid handle ", mHandle, (char *)NULL );
    }

    return TCL_ERROR;
  }

  pStmt = Tcl_GetHashValue( hashEntryPtr );
  if(pStmt->request < 0) {
      return TCL_ERROR;
  }

  switch( (enum STMT_enum)choice ){
    case STMT_BIND: {
      int res;
      int index;
      char *type;
      int len;
      T_CCI_U_TYPE utype;
      T_CCI_A_TYPE atype;
      T_CCI_ERROR cci_error;
      char *res_buf;
      int int_val;
      int64_t int64_val;
      T_CCI_BIT bit;
      double double_val;
      T_CCI_CLOB clob = NULL;
      T_CCI_BLOB blob = NULL;
      CLOBDataLink *clob_current, *clob_next;
      BLOBDataLink *blob_current, *blob_next;
      char *temp_data_char;

      /*
       * For Collection Types
       */
      char **set_array = NULL;
      int *set_null = NULL;
      Tcl_Obj *elementPtr = NULL;
      char *stringPtr = NULL;
      int strLength = 0;
      T_CCI_SET set = NULL;
      int count = 0;

      if( objc == 5){
        if(Tcl_GetIntFromObj(interp, objv[2], &index) != TCL_OK) {
            return TCL_ERROR;
        }

        type = Tcl_GetStringFromObj(objv[3], &len);
        if( !type || len < 1 ) {
          return TCL_ERROR;
        }
      } else {
        Tcl_WrongNumArgs(interp, 2, objv, "index type value");
        return TCL_ERROR;
      }

      if(strcmp(type, "char")==0) {
          utype = CCI_U_TYPE_CHAR;
          atype = CCI_A_TYPE_STR;

          res_buf = Tcl_GetStringFromObj(objv[4], &len);
          if( !res_buf ) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, res_buf, utype, 0);
      } else if(strcmp(type, "varchar")==0) {
          utype = CCI_U_TYPE_STRING;
          atype = CCI_A_TYPE_STR;

          res_buf = Tcl_GetStringFromObj(objv[4], &len);
          if( !res_buf ) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, res_buf, utype, 0);
      } else if(strcmp(type, "bit")==0) {
          utype = CCI_U_TYPE_BIT;
          atype = CCI_A_TYPE_BIT;

          res_buf = Tcl_GetStringFromObj(objv[4], &len);
          if( !res_buf || len < 1 ) {
            return TCL_ERROR;
          }

          temp_data_char = cubrid_str2bit(res_buf);
          if( !temp_data_char ) {
            Tcl_SetResult (interp, (char *)"bit conversion fail", NULL);
            return TCL_ERROR;
          }

          bit.buf = temp_data_char;
          bit.size = len / 8 + 1;

          res = cci_bind_param(pStmt->request, index, atype, &bit, utype, 0);
          free(temp_data_char);
      } else if(strcmp(type, "varbit")==0) {
          utype = CCI_U_TYPE_VARBIT;
          atype = CCI_A_TYPE_BIT;

          res_buf = Tcl_GetStringFromObj(objv[4], &len);
          if( !res_buf || len < 1 ) {
            return TCL_ERROR;
          }

          temp_data_char = cubrid_str2bit(res_buf);
          if( !temp_data_char ) {
            Tcl_SetResult (interp, (char *)"bit conversion fail", NULL);
            return TCL_ERROR;
          }

          bit.buf = temp_data_char;
          bit.size = len / 8 + 1;

          res = cci_bind_param(pStmt->request, index, atype, &bit, utype, 0);
          free(temp_data_char);
      } else if(strcmp(type, "numeric")==0) {
          utype = CCI_U_TYPE_NUMERIC;
          atype = CCI_A_TYPE_STR;

          res_buf = Tcl_GetStringFromObj(objv[4], &len);
          if( !res_buf || len < 1 ) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, res_buf, utype, 0);
      } else if(strcmp(type, "integer")==0) {
          utype = CCI_U_TYPE_INT;
          atype = CCI_A_TYPE_INT;

          if(Tcl_GetIntFromObj(interp, objv[4], &int_val) != TCL_OK) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, &int_val, utype, 0);
      } else if(strcmp(type, "smallint")==0) {
          utype = CCI_U_TYPE_SHORT;
          atype = CCI_A_TYPE_INT;

          if(Tcl_GetIntFromObj(interp, objv[4], &int_val) != TCL_OK) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, &int_val, utype, 0);
      } else if(strcmp(type, "float")==0) {
          utype = CCI_U_TYPE_FLOAT;
          atype = CCI_A_TYPE_FLOAT;

          /*
           * Is is OK to use double?
           */
          if(Tcl_GetDoubleFromObj(interp, objv[4], &double_val) != TCL_OK) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, &double_val, utype, 0);
      } else if(strcmp(type, "real")==0) {
          /*
           * In CUBRID database, FLOAT and REAL are used interchangeably.
           */
          utype = CCI_U_TYPE_FLOAT;
          atype = CCI_A_TYPE_FLOAT;

          /*
           * Is is OK to use double?
           */
          if(Tcl_GetDoubleFromObj(interp, objv[4], &double_val) != TCL_OK) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, &double_val, utype, 0);
      } else if(strcmp(type, "double")==0) {
          utype = CCI_U_TYPE_DOUBLE;
          atype = CCI_A_TYPE_DOUBLE;

          if(Tcl_GetDoubleFromObj(interp, objv[4], &double_val) != TCL_OK) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, &double_val, utype, 0);
     } else if(strcmp(type, "monetary")==0) {
          utype = CCI_U_TYPE_MONETARY;
          atype = CCI_A_TYPE_DOUBLE;

          if(Tcl_GetDoubleFromObj(interp, objv[4], &double_val) != TCL_OK) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, &double_val, utype, 0);
      } else if(strcmp(type, "date")==0) {
          utype = CCI_U_TYPE_DATE;
          atype = CCI_A_TYPE_STR;

          res_buf = Tcl_GetStringFromObj(objv[4], &len);
          if( !res_buf || len < 1 ) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, res_buf, utype, 0);
      } else if(strcmp(type, "time")==0) {
          utype = CCI_U_TYPE_TIME;
          atype = CCI_A_TYPE_STR;

          res_buf = Tcl_GetStringFromObj(objv[4], &len);
          if( !res_buf || len < 1 ) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, res_buf, utype, 0);
      } else if(strcmp(type, "timestamp")==0) {
          utype = CCI_U_TYPE_TIMESTAMP;
          atype = CCI_A_TYPE_STR;

          res_buf = Tcl_GetStringFromObj(objv[4], &len);
          if( !res_buf || len < 1 ) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, res_buf, utype, 0);
      } else if(strcmp(type, "timestamptz")==0) {
          utype = CCI_U_TYPE_TIMESTAMPTZ;
          atype = CCI_A_TYPE_STR;

          res_buf = Tcl_GetStringFromObj(objv[4], &len);
          if( !res_buf || len < 1 ) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, res_buf, utype, 0);
      } else if(strcmp(type, "timestampltz")==0) {
          utype = CCI_U_TYPE_TIMESTAMPLTZ;
          atype = CCI_A_TYPE_STR;

          res_buf = Tcl_GetStringFromObj(objv[4], &len);
          if( !res_buf || len < 1 ) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, res_buf, utype, 0);
      } else if(strcmp(type, "bigint")==0) {
          utype = CCI_U_TYPE_BIGINT;
          atype = CCI_A_TYPE_BIGINT;

          if(Tcl_GetWideIntFromObj(interp, objv[4], &int64_val) != TCL_OK) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, &int64_val, utype, 0);
      } else if(strcmp(type, "datetime")==0) {
          utype = CCI_U_TYPE_DATETIME;
          atype = CCI_A_TYPE_STR;

          res_buf = Tcl_GetStringFromObj(objv[4], &len);
          if( !res_buf || len < 1 ) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, res_buf, utype, 0);
      } else if(strcmp(type, "datetimetz")==0) {
          utype = CCI_U_TYPE_DATETIMETZ;
          atype = CCI_A_TYPE_STR;

          res_buf = Tcl_GetStringFromObj(objv[4], &len);
          if( !res_buf || len < 1 ) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, res_buf, utype, 0);
      } else if(strcmp(type, "datetimeltz")==0) {
          utype = CCI_U_TYPE_DATETIMELTZ;
          atype = CCI_A_TYPE_STR;

          res_buf = Tcl_GetStringFromObj(objv[4], &len);
          if( !res_buf || len < 1 ) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, res_buf, utype, 0);
      } else if(strcmp(type, "clob")==0) {
          utype = CCI_U_TYPE_CLOB;
          atype = CCI_A_TYPE_CLOB;

          res_buf = Tcl_GetStringFromObj(objv[4], &len);
          if( !res_buf || len < 1 ) {
            return TCL_ERROR;
          }

          if(len >= CUBRID_LOB_READ_BUF_SIZE) {
            Tcl_SetResult (interp, (char *)"size is too big", NULL);
            return TCL_ERROR;
          }

          res = cci_clob_new (pDb->connection, &clob, &cci_error);

          if(res < 0) {
              Tcl_SetResult (interp, (char *)"clob new failed", NULL);
              return TCL_ERROR;
          }

          res = cci_clob_write (pDb->connection, clob, 0, len, res_buf, &cci_error);
          if(res < 0) {
              Tcl_SetResult (interp, (char *)"clob write failed", NULL);
              return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, (void *) clob, utype, 0);

          /*
           * store our clob info
           */
          clob_current = pStmt->cloblink;
          if(clob_current == NULL) {
              clob_current = (CLOBDataLink *) malloc(sizeof(CLOBDataLink));
              if(!clob_current) {
                Tcl_SetResult (interp, (char *)"malloc clob data memory failed", NULL);

                cci_clob_free(&clob); //Free clob memory
                return TCL_ERROR;
              }

              clob_current->clob = clob;
              clob_current->next = NULL;
          } else {
              while(clob_current->next) {
                clob_current = clob_current->next;
              }

              clob_next = (CLOBDataLink *) malloc(sizeof(CLOBDataLink));
              if(!clob_next) {
                Tcl_SetResult (interp, (char *)"malloc clob data memory next failed", NULL);

                cci_clob_free(&clob); //Free clob memory
                return TCL_ERROR;
              }

              clob_next->clob = clob;
              clob_next->next = NULL;
              clob_current->next = clob_next;
          }
      } else if(strcmp(type, "blob")==0) {
          utype = CCI_U_TYPE_BLOB;
          atype = CCI_A_TYPE_BLOB;

          res_buf = Tcl_GetStringFromObj(objv[4], &len);
          if( !res_buf || len < 1 ) {
            return TCL_ERROR;
          }

          if(len >= CUBRID_LOB_READ_BUF_SIZE) {
            Tcl_SetResult (interp, (char *)"size is too big", NULL);
            return TCL_ERROR;
          }

          res = cci_blob_new (pDb->connection, &blob, &cci_error);
          if(res < 0) {
              Tcl_SetResult (interp, (char *)"blob new failed", NULL);
              return TCL_ERROR;
          }

          res = cci_blob_write (pDb->connection, blob, 0, len, res_buf, &cci_error);
          if(res < 0) {
              Tcl_SetResult (interp, (char *)"blob write failed", NULL);
              return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, (void *) blob, utype, 0);

          /*
           * store our blob info
           */
          blob_current = pStmt->bloblink;
          if(blob_current == NULL) {
              blob_current = (BLOBDataLink *) malloc(sizeof(BLOBDataLink));
              if(!blob_current) {
                Tcl_SetResult (interp, (char *)"malloc blob data memory failed", NULL);

                cci_blob_free(&blob); //Free blob memory
                return TCL_ERROR;
              }

              blob_current->blob = blob;
              blob_current->next = NULL;
          } else {
              while(blob_current->next) {
                blob_current = blob_current->next;
              }

              blob_next = (BLOBDataLink *) malloc(sizeof(BLOBDataLink));
              if(!blob_next) {
                Tcl_SetResult (interp, (char *)"malloc blob data memory next failed", NULL);

                cci_blob_free(&blob); //Free blob memory
                return TCL_ERROR;
              }

              blob_next->blob = blob;
              blob_next->next = NULL;
              blob_current->next = blob_next;
          }
      } else if(strcmp(type, "set")==0) {
          utype = CCI_U_TYPE_SET;
          atype = CCI_A_TYPE_SET;

          if(Tcl_ListObjLength(interp, objv[4], &len) != TCL_OK) {
              return TCL_ERROR;
          }

          if(len <= 0) {
              Tcl_SetResult (interp, (char *)"bind data set: list length is zero", NULL);
              return TCL_ERROR;
          }

          set_array = (char **) malloc (sizeof(char *) * len);
          if(!set_array) {
              Tcl_SetResult (interp, (char *)"bind data set set_array malloc failed", NULL);
              return TCL_ERROR;
          }

          for (count = 0; count < len; count++) {
              set_array[count] = NULL;
          }

          set_null = (int *) malloc (sizeof(int) * len);
          if(!set_null) {
              if(set_array) free(set_array);
              Tcl_SetResult (interp, (char *)"bind data set set_null malloc failed", NULL);
              return TCL_ERROR;
          }

          for (count = 0; count < len; count++) {
              Tcl_ListObjIndex(interp, objv[4], count, &elementPtr);
              stringPtr = Tcl_GetStringFromObj(elementPtr, &strLength);

              set_array[count] = stringPtr;
              set_null[count] = 0;
          }

          if ((res = cci_set_make(&set, CCI_U_TYPE_STRING, len, set_array, set_null)) < 0) {
              if(set_array) free(set_array);
              if(set_null) free(set_null);

              return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, set, utype, 0);

          cci_set_free(set);

          if(set_array) free(set_array);
          if(set_null) free(set_null);
      } else if(strcmp(type, "multiset")==0) {
          utype = CCI_U_TYPE_MULTISET;
          atype = CCI_A_TYPE_SET;

          if(Tcl_ListObjLength(interp, objv[4], &len) != TCL_OK) {
              return TCL_ERROR;
          }

          if(len <= 0) {
              Tcl_SetResult (interp, (char *)"bind data multiset: list length is zero", NULL);
              return TCL_ERROR;
          }

          set_array = (char **) malloc (sizeof(char *) * len);
          if(!set_array) {
              Tcl_SetResult (interp, (char *)"bind data multiset set_array malloc failed", NULL);
              return TCL_ERROR;
          }

          for (count = 0; count < len; count++) {
              set_array[count] = NULL;
          }

          set_null = (int *) malloc (sizeof(int) * len);
          if(!set_null) {
              if(set_array) free(set_array);
              Tcl_SetResult (interp, (char *)"bind data multiset set_null malloc failed", NULL);
              return TCL_ERROR;
          }

          for (count = 0; count < len; count++) {
              Tcl_ListObjIndex(interp, objv[4], count, &elementPtr);
              stringPtr = Tcl_GetStringFromObj(elementPtr, &strLength);

              set_array[count] = stringPtr;
              set_null[count] = 0;
          }

          if ((res = cci_set_make(&set, CCI_U_TYPE_STRING, len, set_array, set_null)) < 0) {
              if(set_array) free(set_array);
              if(set_null) free(set_null);

              return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, set, utype, 0);

          cci_set_free(set);

          if(set_array) free(set_array);
          if(set_null) free(set_null);
      } else if(strcmp(type, "sequence")==0) {
          utype = CCI_U_TYPE_SEQUENCE;
          atype = CCI_A_TYPE_SET;

          if(Tcl_ListObjLength(interp, objv[4], &len) != TCL_OK) {
              return TCL_ERROR;
          }

          if(len <= 0) {
              Tcl_SetResult (interp, (char *)"bind data sequence: list length is zero", NULL);
              return TCL_ERROR;
          }

          set_array = (char **) malloc (sizeof(char *) * len);
          if(!set_array) {
              Tcl_SetResult (interp, (char *)"bind data sequence set_array malloc failed", NULL);
              return TCL_ERROR;
          }

          for (count = 0; count < len; count++) {
              set_array[count] = NULL;
          }

          set_null = (int *) malloc (sizeof(int) * len);
          if(!set_null) {
              if(set_array) free(set_array);
              Tcl_SetResult (interp, (char *)"bind data sequence set_null malloc failed", NULL);
              return TCL_ERROR;
          }

          for (count = 0; count < len; count++) {
              Tcl_ListObjIndex(interp, objv[4], count, &elementPtr);
              stringPtr = Tcl_GetStringFromObj(elementPtr, &strLength);

              set_array[count] = stringPtr;
              set_null[count] = 0;
          }

          if ((res = cci_set_make(&set, CCI_U_TYPE_STRING, len, set_array, set_null)) < 0) {
              if(set_array) free(set_array);
              if(set_null) free(set_null);

              return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, set, utype, 0);

          cci_set_free(set);

          if(set_array) free(set_array);
          if(set_null) free(set_null);
      } else if(strcmp(type, "enum")==0) {
          utype = CCI_U_TYPE_STRING;
          atype = CCI_A_TYPE_STR;

          res_buf = Tcl_GetStringFromObj(objv[4], &len);
          if( !res_buf ) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, res_buf, utype, 0);
      } else if(strcmp(type, "json")==0) {
          utype = CCI_U_TYPE_JSON;
          atype = CCI_A_TYPE_STR;

          res_buf = Tcl_GetStringFromObj(objv[4], &len);
          if( !res_buf ) {
            return TCL_ERROR;
          }

          res = cci_bind_param(pStmt->request, index, atype, res_buf, utype, 0);
      } else if(strcmp(type, "null")==0) {
          utype = CCI_U_TYPE_NULL;
          atype = CCI_A_TYPE_STR;

          res = cci_bind_param(pStmt->request, index, atype, NULL, utype, 0);
      } else {
          return TCL_ERROR;
      }

      if(res < 0) {
          Tcl_SetResult (interp, (char *)"bind data failed", NULL);
          return TCL_ERROR;
      }

      Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
      break;
    }

    case STMT_EXECUTE: {
      Tcl_Obj *return_obj;
      T_CCI_ERROR cci_error;
      int res;
      CLOBDataLink *clob_current, *clob_next;
      BLOBDataLink *blob_current, *blob_next;

      if( objc != 2){
        Tcl_WrongNumArgs(interp, 2, objv, 0);
        return TCL_ERROR;
      }

      res = cci_execute (pStmt->request, 0, 0, &cci_error);
      if (res < 0) {
          return_obj = Tcl_NewBooleanObj(0);
      } else {
          return_obj = Tcl_NewBooleanObj(1);
      }

      /*
       * After we execute prepared statement, try to free CLOB/BLOB memory.
       */
      if(pStmt->cloblink) {
           clob_current = pStmt->cloblink;
           while(clob_current) {
              clob_next = clob_current->next;
              cci_clob_free(clob_current->clob);
              if(clob_current) free(clob_current);
              clob_current = NULL;

              clob_current = clob_next;
           }
      }

      if(pStmt->bloblink) {
           blob_current = pStmt->bloblink;
           while(clob_current) {
              blob_next = blob_current->next;
              cci_blob_free(blob_current->blob);
              if(blob_current) free(blob_current);
              blob_current = NULL;

              blob_current = blob_next;
           }
      }

      Tcl_SetObjResult(interp, return_obj);
      break;
    }

    case STMT_CURSOR: {
      int offset = 0;
      char *pos = NULL;
      int len = 0;
      T_CCI_CURSOR_POS origin;
      T_CCI_ERROR cci_error;
      int error;
      char *errorStr =  NULL;

      if( objc == 4){
        if(Tcl_GetIntFromObj(interp, objv[2], &offset) != TCL_OK) {
            return TCL_ERROR;
        }

        pos = Tcl_GetStringFromObj(objv[3], &len);
        if( !pos || len < 1 ) {
          return TCL_ERROR;
        }

       /*
        * CCI_CURSOR_FIRST = 0,
        * CCI_CURSOR_CURRENT = 1,
        * CCI_CURSOR_LAST = 2
        */
        if(strcmp(pos, "FIRST")==0) {
            origin = CCI_CURSOR_FIRST;
        } else if(strcmp(pos, "CURRENT")==0) {
            origin = CCI_CURSOR_CURRENT;
        } else if(strcmp(pos, "LAST")==0) {
            origin = CCI_CURSOR_LAST;
        } else {
            return TCL_ERROR;
        }
      } else {
        Tcl_WrongNumArgs(interp, 2, objv, "offset pos");
        return TCL_ERROR;
      }

      error = cci_cursor (pStmt->request, offset, origin, &cci_error);
      if (error == CCI_ER_NO_MORE_DATA) {
          Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
          return TCL_OK;
      }

      if (error < 0) {
          errorStr = cci_error.err_msg;
          Tcl_SetResult (interp, errorStr, NULL);

          return TCL_ERROR;
      }

      Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
      break;
    }

    case STMT_FETCH_ROW_LIST: {
      T_CCI_COL_INFO *col_info;
      T_CCI_CUBRID_STMT stmt_type;
      T_CCI_U_TYPE type;
      T_CCI_ERROR cci_error;
      int i = 0;
      int col_count = 0;
      int error, ind;
      char *errorStr = NULL;
      Tcl_Obj *pResultStr;
      char *res_buf;
      int int_val;
      int64_t int64_val;
      double double_val;
      T_CCI_BIT bit;
      T_CCI_DATE date;
      struct tm tm1;
      char tbuf[64];
      T_CCI_CLOB clob;
      T_CCI_BLOB blob;
      T_CCI_SET cci_set;
      int set_size = 0;
      Tcl_Obj *pResultSet;
      int count = 0;
      char *set_buffer = NULL;
      char buffer[CUBRID_LOB_READ_BUF_SIZE];
      int res = 0;

      if( objc != 2){
        Tcl_WrongNumArgs(interp, 2, objv, 0);
        return TCL_ERROR;
      }

      pResultStr = Tcl_NewListObj(0, NULL);

      /*
       * getting column information when the prepared statement is the
       * SELECT query
       */
      col_info = cci_get_result_info (pStmt->request, &stmt_type, &col_count);
      if (col_info == NULL) {
          Tcl_SetResult (interp, (char *)"get result info fail", NULL);
          return TCL_ERROR;
      }

      error = cci_fetch (pStmt->request, &cci_error);
      if (error < 0) {
          errorStr = cci_error.err_msg;
          Tcl_SetResult (interp, errorStr, NULL);

          return TCL_ERROR;
      }

      for (i = 1; i <= col_count; i++) {
          type = CCI_GET_RESULT_INFO_TYPE (col_info, i);

          switch (type) {
            case CCI_U_TYPE_INT:
            case CCI_U_TYPE_SHORT:
                error = cci_get_data(pStmt->request, i, CCI_A_TYPE_INT, &int_val, &ind);
                if (error < 0) {
                  Tcl_SetResult (interp, (char *)"get data failed", NULL);
                  return TCL_ERROR;
                }

                if (ind < 0) {
                    //We got a NULL
                    Tcl_ListObjAppendElement(interp, pResultStr, Tcl_NewStringObj("", -1));
                } else {
                    Tcl_ListObjAppendElement(interp, pResultStr, Tcl_NewIntObj(int_val));
                }

                break;

            case CCI_U_TYPE_BIGINT:
                error = cci_get_data(pStmt->request, i, CCI_A_TYPE_BIGINT, &int64_val, &ind);
                if (error < 0) {
                  Tcl_SetResult (interp, (char *)"get data failed", NULL);
                  return TCL_ERROR;
                }

                if (ind < 0) {
                    Tcl_ListObjAppendElement(interp, pResultStr, Tcl_NewStringObj("", -1));
                } else {
                    Tcl_ListObjAppendElement(interp, pResultStr, Tcl_NewWideIntObj(int64_val));
                }

                break;

            case CCI_U_TYPE_FLOAT:
            case CCI_U_TYPE_DOUBLE:
            case CCI_U_TYPE_NUMERIC:
            case CCI_U_TYPE_MONETARY:
                error = cci_get_data(pStmt->request, i, CCI_A_TYPE_STR, &res_buf, &ind);
                if (error < 0) {
                  Tcl_SetResult (interp, (char *)"get data failed", NULL);
                  return TCL_ERROR;
                }

                if (ind < 0) {
                    Tcl_ListObjAppendElement(interp, pResultStr, Tcl_NewStringObj("", -1));
                } else {
                    double_val = atof(res_buf);
                    Tcl_ListObjAppendElement(interp, pResultStr, Tcl_NewDoubleObj(double_val));
                }

                break;

           case CCI_U_TYPE_DATE:
           case CCI_U_TYPE_TIME:
           case CCI_U_TYPE_TIMESTAMP:
                error = cci_get_data(pStmt->request, i, CCI_A_TYPE_DATE, &date, &ind);
                if (error < 0) {
                  Tcl_SetResult (interp, (char *)"get data failed", NULL);
                  return TCL_ERROR;
                }

                if (ind < 0) {
                    Tcl_ListObjAppendElement(interp, pResultStr, Tcl_NewStringObj("", -1));
                } else {
                    if (type == CCI_U_TYPE_DATE) {
                        tm1.tm_hour = 0;
                        tm1.tm_min = 0;
                        tm1.tm_sec = 0;
                        tm1.tm_year = date.yr - 1900;
                        tm1.tm_mon = date.mon - 1;
                        tm1.tm_mday = date.day;
                        strftime(tbuf,sizeof(tbuf),"%Y/%m/%d", &tm1);
                    } else if (type == CCI_U_TYPE_TIME) {
                        tm1.tm_hour = date.hh;
                        tm1.tm_min = date.mm;
                        tm1.tm_sec = date.ss;
                        tm1.tm_year = date.yr - 1900;
                        tm1.tm_mon = date.mon - 1;
                        tm1.tm_mday = date.day;
                        strftime(tbuf,sizeof(tbuf),"%H:%M:%S", &tm1);
                    } else {
                        tm1.tm_hour = date.hh;
                        tm1.tm_min = date.mm;
                        tm1.tm_sec = date.ss;
                        tm1.tm_year = date.yr - 1900;
                        tm1.tm_mon = date.mon - 1;
                        tm1.tm_mday = date.day;
                        strftime(tbuf,sizeof(tbuf),"%Y/%m/%d %H:%M:%S.00", &tm1);
                    }

                    Tcl_ListObjAppendElement(interp, pResultStr, Tcl_NewStringObj(tbuf, -1));
                }

                break;

           case CCI_U_TYPE_BIT:
           case CCI_U_TYPE_VARBIT:
                error = cci_get_data(pStmt->request, i, CCI_A_TYPE_BIT, &bit, &ind);
                if (error < 0) {
                  Tcl_SetResult (interp, (char *)"get data failed", NULL);
                  return TCL_ERROR;
                }

                if (ind < 0) {
                    Tcl_ListObjAppendElement(interp, pResultStr, Tcl_NewStringObj("", -1));
                } else {
                    Tcl_ListObjAppendElement(interp, pResultStr, Tcl_NewStringObj(bit.buf, bit.size));
                }

                break;

           case CCI_U_TYPE_CLOB:
                error = cci_get_data(pStmt->request, i, CCI_A_TYPE_CLOB, (void *)&clob, &ind);
                if (error < 0) {
                  Tcl_SetResult (interp, (char *)"get data failed", NULL);
                  return TCL_ERROR;
                }

                if (ind < 0) {
                    Tcl_ListObjAppendElement(interp, pResultStr, Tcl_NewStringObj("", -1));
                } else {
                    res = cci_clob_read (pDb->connection, clob, 0, CUBRID_LOB_READ_BUF_SIZE,
                                         buffer, &cci_error);
                    if(res < 0) {
                           Tcl_SetResult (interp, (char *)"read clob failed", NULL);
                           return TCL_ERROR;
                    }

                    Tcl_ListObjAppendElement(interp, pResultStr, Tcl_NewStringObj(buffer, res));
                    cci_clob_free(clob);
                }

                break;

           case CCI_U_TYPE_BLOB:
                error = cci_get_data(pStmt->request, i, CCI_A_TYPE_BLOB, (void *)&blob, &ind);
                if (error < 0) {
                  Tcl_SetResult (interp, (char *)"get data failed", NULL);
                  return TCL_ERROR;
                }

                if (ind < 0) {
                    Tcl_ListObjAppendElement(interp, pResultStr, Tcl_NewStringObj("", -1));
                } else {
                    res = cci_blob_read (pDb->connection, blob, 0, CUBRID_LOB_READ_BUF_SIZE,
                                         buffer, &cci_error);
                    if(res < 0) {
                           Tcl_SetResult (interp, (char *)"read blob failed", NULL);
                           return TCL_ERROR;
                    }

                    Tcl_ListObjAppendElement(interp, pResultStr, Tcl_NewStringObj(buffer, res));
                    cci_clob_free(blob);
                }

                break;

           default:
                if (CCI_IS_COLLECTION_TYPE (type)) {
                    error = cci_get_data(pStmt->request, i, CCI_A_TYPE_SET, (void *)&cci_set, &ind);
                    if (error < 0) {
                      Tcl_SetResult (interp, (char *)"get data failed", NULL);
                      return TCL_ERROR;
                    }

                    if (ind < 0) {
                        Tcl_ListObjAppendElement(interp, pResultStr, Tcl_NewStringObj("", -1));
                    } else {
                        set_size = cci_set_size(cci_set);
                        if(set_size <= 0) {
                          Tcl_SetResult (interp, (char *)"Set size is wrong.", NULL);
                          return TCL_ERROR;
                        }

                        pResultSet = Tcl_NewListObj(0, NULL);
                        for (count = 0; count < set_size; count++)
                        {
                          res = cci_set_get (cci_set, count + 1, CCI_A_TYPE_STR, &set_buffer, &ind);
                          if (res < 0)
                          {
                            Tcl_SetResult (interp, (char *)"Get set data fail.", NULL);
                            return TCL_ERROR;
                          }

                          Tcl_ListObjAppendElement(interp, pResultSet, Tcl_NewStringObj(set_buffer, -1));
                        }

                        Tcl_ListObjAppendElement(interp, pResultStr, pResultSet);
                        cci_set_free (cci_set);
                    }
                } else {
                    error = cci_get_data(pStmt->request, i, CCI_A_TYPE_STR, &res_buf, &ind);
                    if (error < 0) {
                      Tcl_SetResult (interp, (char *)"get data failed", NULL);
                      return TCL_ERROR;
                    }

                    if (ind < 0) {
                        Tcl_ListObjAppendElement(interp, pResultStr, Tcl_NewStringObj("", -1));
                    } else {
                        Tcl_ListObjAppendElement(interp, pResultStr, Tcl_NewStringObj(res_buf, -1));
                    }
                }

                break;
          }
      }

      Tcl_SetObjResult(interp, pResultStr);
      break;
    }

    case STMT_FETCH_ROW_DICT: {
      T_CCI_COL_INFO *col_info;
      T_CCI_CUBRID_STMT stmt_type;
      T_CCI_U_TYPE type;
      char *name;
      T_CCI_ERROR cci_error;
      int i = 0;
      int col_count = 0;
      int error, ind;
      char *errorStr = NULL;
      Tcl_Obj *pResultStr;
      char *res_buf;
      int int_val;
      int64_t int64_val;
      double double_val;
      T_CCI_BIT bit;
      T_CCI_DATE date;
      struct tm tm1;
      char tbuf[64];
      T_CCI_CLOB clob;
      T_CCI_BLOB blob;
      T_CCI_SET cci_set;
      int set_size = 0;
      Tcl_Obj *pResultSet;
      int count = 0;
      char *set_buffer = NULL;
      char buffer[CUBRID_LOB_READ_BUF_SIZE];
      int res = 0;

      if( objc != 2){
        Tcl_WrongNumArgs(interp, 2, objv, 0);
        return TCL_ERROR;
      }

      pResultStr = Tcl_NewListObj(0, NULL);

      /*
       * getting column information when the prepared statement is the
       * SELECT query
       */
      col_info = cci_get_result_info (pStmt->request, &stmt_type, &col_count);
      if (col_info == NULL) {
          Tcl_SetResult (interp, (char *)"get result info fail", NULL);
          return TCL_ERROR;
      }

      error = cci_fetch (pStmt->request, &cci_error);
      if (error < 0) {
          errorStr = cci_error.err_msg;
          Tcl_SetResult (interp, errorStr, NULL);

          return TCL_ERROR;
      }

      for (i = 1; i <= col_count; i++) {
          name = CCI_GET_RESULT_INFO_NAME (col_info, i);
          type = CCI_GET_RESULT_INFO_TYPE (col_info, i);

          switch (type) {
            case CCI_U_TYPE_INT:
            case CCI_U_TYPE_SHORT:
                error = cci_get_data(pStmt->request, i, CCI_A_TYPE_INT, &int_val, &ind);
                if (error < 0) {
                  Tcl_SetResult (interp, (char *)"get data failed", NULL);
                  return TCL_ERROR;
                }

                if (ind < 0) {
                    /*
                     * If a value is NULL, the returned dictionary for the
                     * row will not contain the corresponding key.
                     */
                } else {
                    Tcl_DictObjPut (interp, pResultStr,
                                Tcl_NewStringObj(name, -1),
                                Tcl_NewIntObj(int_val));
                }

                break;

            case CCI_U_TYPE_BIGINT:
                error = cci_get_data(pStmt->request, i, CCI_A_TYPE_BIGINT, &int64_val, &ind);
                if (error < 0) {
                  Tcl_SetResult (interp, (char *)"get data failed", NULL);
                  return TCL_ERROR;
                }

                if (ind < 0) {
                    /*
                     * If a value is NULL, the returned dictionary for the
                     * row will not contain the corresponding key.
                     */
                } else {
                    Tcl_DictObjPut (interp, pResultStr,
                                Tcl_NewStringObj(name, -1),
                                Tcl_NewWideIntObj(int64_val));
                }

                break;

            case CCI_U_TYPE_FLOAT:
            case CCI_U_TYPE_DOUBLE:
            case CCI_U_TYPE_NUMERIC:
            case CCI_U_TYPE_MONETARY:
                error = cci_get_data(pStmt->request, i, CCI_A_TYPE_STR, &res_buf, &ind);
                if (error < 0) {
                  Tcl_SetResult (interp, (char *)"get data failed", NULL);
                  return TCL_ERROR;
                }

                if (ind < 0) {
                    /*
                     * If a value is NULL, the returned dictionary for the
                     * row will not contain the corresponding key.
                     */
                } else {
                    double_val = atof(res_buf);
                    Tcl_DictObjPut (interp, pResultStr,
                                Tcl_NewStringObj(name, -1),
                                Tcl_NewDoubleObj(double_val));
                }

                break;

           case CCI_U_TYPE_DATE:
           case CCI_U_TYPE_TIME:
           case CCI_U_TYPE_TIMESTAMP:
                error = cci_get_data(pStmt->request, i, CCI_A_TYPE_DATE, &date, &ind);
                if (error < 0) {
                  Tcl_SetResult (interp, (char *)"get data failed", NULL);
                  return TCL_ERROR;
                }

                if (ind < 0) {
                    /*
                     * If a value is NULL, the returned dictionary for the
                     * row will not contain the corresponding key.
                     */
                } else {
                    if (type == CCI_U_TYPE_DATE) {
                        tm1.tm_hour = 0;
                        tm1.tm_min = 0;
                        tm1.tm_sec = 0;
                        tm1.tm_year = date.yr - 1900;
                        tm1.tm_mon = date.mon - 1;
                        tm1.tm_mday = date.day;
                        strftime(tbuf,sizeof(tbuf),"%Y/%m/%d", &tm1);
                    } else if (type == CCI_U_TYPE_TIME) {
                        tm1.tm_hour = date.hh;
                        tm1.tm_min = date.mm;
                        tm1.tm_sec = date.ss;
                        tm1.tm_year = date.yr - 1900;
                        tm1.tm_mon = date.mon - 1;
                        tm1.tm_mday = date.day;
                        strftime(tbuf,sizeof(tbuf),"%H:%M:%S", &tm1);
                    } else {
                        tm1.tm_hour = date.hh;
                        tm1.tm_min = date.mm;
                        tm1.tm_sec = date.ss;
                        tm1.tm_year = date.yr - 1900;
                        tm1.tm_mon = date.mon - 1;
                        tm1.tm_mday = date.day;
                        strftime(tbuf,sizeof(tbuf),"%Y/%m/%d %H:%M:%S", &tm1);
                    }

                    Tcl_DictObjPut (interp, pResultStr,
                                Tcl_NewStringObj(name, -1),
                                Tcl_NewStringObj(tbuf, -1));
                }

                break;

           case CCI_U_TYPE_BIT:
           case CCI_U_TYPE_VARBIT:
                error = cci_get_data(pStmt->request, i, CCI_A_TYPE_BIT, &bit, &ind);
                if (error < 0) {
                  Tcl_SetResult (interp, (char *)"get data failed", NULL);
                  return TCL_ERROR;
                }

                if (ind < 0) {
                    /*
                     * If a value is NULL, the returned dictionary for the
                     * row will not contain the corresponding key.
                     */
                } else {
                    Tcl_DictObjPut (interp, pResultStr,
                                Tcl_NewStringObj(name, -1),
                                Tcl_NewStringObj(bit.buf, bit.size));
                }

                break;

           case CCI_U_TYPE_CLOB:
                error = cci_get_data(pStmt->request, i, CCI_A_TYPE_CLOB, (void *)&clob, &ind);
                if (error < 0) {
                  Tcl_SetResult (interp, (char *)"get data failed", NULL);
                  return TCL_ERROR;
                }

                if (ind < 0) {
                    /*
                     * If a value is NULL, the returned dictionary for the
                     * row will not contain the corresponding key.
                     */
                } else {
                    res = cci_clob_read (pDb->connection, clob, 0, CUBRID_LOB_READ_BUF_SIZE,
                                         buffer, &cci_error);
                    if(res < 0) {
                           Tcl_SetResult (interp, (char *)"read clob failed", NULL);
                           return TCL_ERROR;
                    }

                    Tcl_DictObjPut (interp, pResultStr,
                                Tcl_NewStringObj(name, -1),
                                Tcl_NewStringObj(buffer, res));
                    cci_clob_free(clob);
                }

               break;

           case CCI_U_TYPE_BLOB:
                error = cci_get_data(pStmt->request, i, CCI_A_TYPE_BLOB, (void *)&blob, &ind);
                if (error < 0) {
                  Tcl_SetResult (interp, (char *)"get data failed", NULL);
                  return TCL_ERROR;
                }

                if (ind < 0) {
                    /*
                     * If a value is NULL, the returned dictionary for the
                     * row will not contain the corresponding key.
                     */
                } else {
                    res = cci_blob_read (pDb->connection, blob, 0, CUBRID_LOB_READ_BUF_SIZE,
                                         buffer, &cci_error);
                    if(res < 0) {
                           Tcl_SetResult (interp, (char *)"read blob failed", NULL);
                           return TCL_ERROR;
                    }

                    Tcl_DictObjPut (interp, pResultStr,
                                Tcl_NewStringObj(name, -1),
                                Tcl_NewStringObj(buffer, res));
                    cci_clob_free(blob);
                }

                break;

           default:
                if (CCI_IS_COLLECTION_TYPE (type)) {
                    error = cci_get_data(pStmt->request, i, CCI_A_TYPE_SET, (void *)&cci_set, &ind);
                    if (error < 0) {
                      Tcl_SetResult (interp, (char *)"get data failed", NULL);
                      return TCL_ERROR;
                    }

                    if (ind < 0) {
                        /*
                        * If a value is NULL, the returned dictionary for the
                        * row will not contain the corresponding key.
                        */
                    } else {
                        set_size = cci_set_size(cci_set);
                        if(set_size <= 0) {
                          Tcl_SetResult (interp, (char *)"Set size is wrong.", NULL);
                          return TCL_ERROR;
                        }

                        pResultSet = Tcl_NewListObj(0, NULL);
                        for (count = 0; count < set_size; count++)
                        {
                          res = cci_set_get (cci_set, count + 1, CCI_A_TYPE_STR, &set_buffer, &ind);
                          if (res < 0)
                          {
                            Tcl_SetResult (interp, (char *)"Get set data fail.", NULL);
                            return TCL_ERROR;
                          }

                          Tcl_ListObjAppendElement(interp, pResultSet, Tcl_NewStringObj(set_buffer, -1));
                        }

                        Tcl_DictObjPut (interp, pResultStr,
                                    Tcl_NewStringObj(name, -1),
                                    pResultSet);

                        cci_set_free (cci_set);
                    }
                } else {
                    error = cci_get_data(pStmt->request, i, CCI_A_TYPE_STR, &res_buf, &ind);
                    if (error < 0) {
                      Tcl_SetResult (interp, (char *)"get data failed", NULL);
                      return TCL_ERROR;
                    }

                    if (ind < 0) {
                        /*
                        * If a value is NULL, the returned dictionary for the
                        * row will not contain the corresponding key.
                        */
                    } else {
                        Tcl_DictObjPut (interp, pResultStr,
                                    Tcl_NewStringObj(name, -1),
                                    Tcl_NewStringObj(res_buf, -1));
                    }
                }

                break;
          }
      }

      Tcl_SetObjResult(interp, pResultStr);
      break;
    }

    case STMT_COLUMNS: {
      T_CCI_COL_INFO *col_info;
      T_CCI_CUBRID_STMT stmt_type;
      char *name;
      int col_count = 0;
      int i = 0;
      Tcl_Obj *pResultStr;

      if( objc != 2){
        Tcl_WrongNumArgs(interp, 2, objv, 0);
        return TCL_ERROR;
      }

      pResultStr = Tcl_NewListObj(0, NULL);

      col_info = cci_get_result_info (pStmt->request, &stmt_type, &col_count);
      if (col_info == NULL) {
          Tcl_SetResult (interp, (char *)"get result info fail", NULL);
          return TCL_ERROR;
      }

      for (i = 1; i <= col_count; i++) {
          name = CCI_GET_RESULT_INFO_NAME (col_info, i);
          Tcl_ListObjAppendElement(interp, pResultStr, Tcl_NewStringObj(name, -1));
      }

      Tcl_SetObjResult(interp, pResultStr);
      break;
    }

    case STMT_COLUMNTYPE: {
      T_CCI_COL_INFO *col_info;
      T_CCI_CUBRID_STMT stmt_type;
      T_CCI_U_TYPE type;
      int col_count = 0;
      int index = 0;
      Tcl_Obj *pResultStr = NULL;

      if( objc == 3){
        if(Tcl_GetIntFromObj(interp, objv[2], &index) != TCL_OK) {
            return TCL_ERROR;
        }

        if (index < 1) {
            return TCL_ERROR;
        }
      } else {
        Tcl_WrongNumArgs(interp, 2, objv, "index");
        return TCL_ERROR;
      }

      col_info = cci_get_result_info (pStmt->request, &stmt_type, &col_count);
      if (col_info == NULL) {
          Tcl_SetResult (interp, (char *)"get result info fail", NULL);
          return TCL_ERROR;
      }

      type = CCI_GET_RESULT_INFO_TYPE (col_info, index);
      if(type == CCI_U_TYPE_CHAR) {
         pResultStr = Tcl_NewStringObj("char", -1);
      } else if(type == CCI_U_TYPE_STRING) {
         pResultStr = Tcl_NewStringObj("varchar", -1);
      } else if(type == CCI_U_TYPE_BIT) {
         pResultStr = Tcl_NewStringObj("bit", -1);
      } else if(type == CCI_U_TYPE_VARBIT) {
         pResultStr = Tcl_NewStringObj("varbit", -1);
      } else if(type == CCI_U_TYPE_NUMERIC) {
         pResultStr = Tcl_NewStringObj("numeric", -1);
      } else if(type == CCI_U_TYPE_INT) {
         pResultStr = Tcl_NewStringObj("integer", -1);
      } else if(type == CCI_U_TYPE_SHORT) {
         pResultStr = Tcl_NewStringObj("smallint", -1);
      } else if(type == CCI_U_TYPE_FLOAT) {
         pResultStr = Tcl_NewStringObj("float", -1);
      } else if(type == CCI_U_TYPE_DOUBLE) {
         pResultStr = Tcl_NewStringObj("double", -1);
      } else if(type == CCI_U_TYPE_MONETARY) {
         pResultStr = Tcl_NewStringObj("monetary", -1);
      } else if(type == CCI_U_TYPE_DATE) {
         pResultStr = Tcl_NewStringObj("date", -1);
      } else if(type == CCI_U_TYPE_TIME) {
         pResultStr = Tcl_NewStringObj("time", -1);
      } else if(type == CCI_U_TYPE_TIMESTAMP) {
         pResultStr = Tcl_NewStringObj("timestamp", -1);
      } else if(type == CCI_U_TYPE_TIMESTAMPTZ) {
         pResultStr = Tcl_NewStringObj("timestamptz", -1);
      } else if(type == CCI_U_TYPE_TIMESTAMPLTZ) {
         pResultStr = Tcl_NewStringObj("timestampltz", -1);
      } else if(type == CCI_U_TYPE_BIGINT) {
         pResultStr = Tcl_NewStringObj("bigint", -1);
      } else if(type == CCI_U_TYPE_DATETIME) {
         pResultStr = Tcl_NewStringObj("datetime", -1);
      } else if(type == CCI_U_TYPE_DATETIMETZ) {
         pResultStr = Tcl_NewStringObj("datetimetz", -1);
      } else if(type == CCI_U_TYPE_DATETIMELTZ) {
         pResultStr = Tcl_NewStringObj("datetimeltz", -1);
      } else if(type == CCI_U_TYPE_CLOB) {
         pResultStr = Tcl_NewStringObj("clob", -1);
      } else if(type == CCI_U_TYPE_BLOB) {
         pResultStr = Tcl_NewStringObj("blob", -1);
      } else if(CCI_IS_SET_TYPE(type)) {
         pResultStr = Tcl_NewStringObj("set", -1);
      } else if(CCI_IS_MULTISET_TYPE(type)) {
         pResultStr = Tcl_NewStringObj("multiset", -1);
      } else if(CCI_IS_SEQUENCE_TYPE(type)) {
         pResultStr = Tcl_NewStringObj("sequence", -1);
      } else if(type == CCI_U_TYPE_ENUM) {
         pResultStr = Tcl_NewStringObj("enum", -1);
      } else if(type == CCI_U_TYPE_JSON) {
         pResultStr = Tcl_NewStringObj("json", -1);
      } else {
         pResultStr = Tcl_NewStringObj("", -1);
      }

      Tcl_SetObjResult(interp, pResultStr);
      break;
    }

    case STMT_CLOSE: {
      Tcl_Obj *return_obj;
      CLOBDataLink *clob_current, *clob_next;
      BLOBDataLink *blob_current, *blob_next;

      if( objc != 2){
        Tcl_WrongNumArgs(interp, 2, objv, 0);
        return TCL_ERROR;
      }

      if (cci_close_req_handle(pStmt->request) < 0) {
          return_obj = Tcl_NewBooleanObj(0);
      } else {
          return_obj = Tcl_NewBooleanObj(1);
      }

      pStmt->request = 0;

      /*
       * Check our BLOB/CLOB link again
       */
      if(pStmt->cloblink) {
           clob_current = pStmt->cloblink;
           while(clob_current) {
              clob_next = clob_current->next;
              cci_clob_free(clob_current->clob);
              if(clob_current) free(clob_current);
              clob_current = NULL;

              clob_current = clob_next;
           }
      }

      if(pStmt->bloblink) {
           blob_current = pStmt->bloblink;
           while(clob_current) {
              blob_next = blob_current->next;
              cci_blob_free(blob_current->blob);
              if(blob_current) free(blob_current);
              blob_current = NULL;

              blob_current = blob_next;
           }
      }

      Tcl_Free((char*)pStmt);
      pStmt = 0;

      Tcl_MutexLock(&myMutex);
      if( hashEntryPtr )  Tcl_DeleteHashEntry(hashEntryPtr);
      Tcl_MutexUnlock(&myMutex);

      Tcl_DeleteCommand(interp, Tcl_GetStringFromObj(objv[0], 0));
      Tcl_SetObjResult(interp, return_obj);

      break;
    }
  }

  return rc;
}


/*
 * db handle command function
 */
static int DbObjCmd(void *cd, Tcl_Interp *interp, int objc,Tcl_Obj *const*objv){
  CUBRIDDATA *pDb = (CUBRIDDATA *) cd;
  int choice;
  int rc = TCL_OK;

  ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
      Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

  if (tsdPtr->initialized == 0) {
    tsdPtr->initialized = 1;
    tsdPtr->cubrid_hashtblPtr = (Tcl_HashTable *) ckalloc(sizeof(Tcl_HashTable));
    Tcl_InitHashTable(tsdPtr->cubrid_hashtblPtr, TCL_STRING_KEYS);
  }

  static const char *DB_strs[] = {
    "getAutocommit",
    "setAutocommit",
    "getIsolationLevel",
    "setIsolationLevel",
    "commit",
    "rollback",
    "prepare",
    "server_version",
    "row_count",
    "last_insert_id",
    "close",
    0
  };

  enum DB_enum {
    DB_GETAUTOCOMMIT,
    DB_SETAUTOCOMMIT,
    DB_GETISOLATION,
    DB_SETISOLATION,
    DB_COMMIT,
    DB_ROLLBACK,
    DB_PREPARE,
    DB_VERSION,
    DB_ROW_COUNT,
    DB_LAST_INSERT_ID,
    DB_CLOSE,
  };

  if( objc < 2 ){
    Tcl_WrongNumArgs(interp, 1, objv, "SUBCOMMAND ...");
    return TCL_ERROR;
  }

  if( Tcl_GetIndexFromObj(interp, objv[1], DB_strs, "option", 0, &choice) ){
    return TCL_ERROR;
  }

  if(pDb->connection < 0 ) {
      return TCL_ERROR;
  }

  switch( (enum DB_enum)choice ){
    case DB_GETAUTOCOMMIT: {
      CCI_AUTOCOMMIT_MODE mode = CCI_AUTOCOMMIT_TRUE;

      if( objc != 2){
        Tcl_WrongNumArgs(interp, 2, objv, 0);
        return TCL_ERROR;
      }

      mode = cci_get_autocommit(pDb->connection);
      if(mode == CCI_AUTOCOMMIT_TRUE) {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(1));
      } else {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(0));
      }
      break;
    }

    case DB_SETAUTOCOMMIT: {
      int autocommit = 0;
      CCI_AUTOCOMMIT_MODE mode = CCI_AUTOCOMMIT_TRUE;
      Tcl_Obj *return_obj;

      if( objc == 3){
        if(Tcl_GetIntFromObj(interp, objv[2], &autocommit) != TCL_OK) {
            return TCL_ERROR;
        }

        if (autocommit < 0) {
            return TCL_ERROR;
        }
      } else {
        Tcl_WrongNumArgs(interp, 2, objv, "autocommit");
        return TCL_ERROR;
      }

      if (autocommit==0) {
          mode = CCI_AUTOCOMMIT_FALSE;
      } else {
          mode = CCI_AUTOCOMMIT_TRUE;
      }

      if (cci_set_autocommit(pDb->connection, mode) != 0) {
          return_obj = Tcl_NewBooleanObj(0);
      } else {
          return_obj = Tcl_NewBooleanObj(1);
      }

      Tcl_SetObjResult(interp, return_obj);
      break;
    }

    case DB_GETISOLATION: {
      T_CCI_DB_PARAM param_name = CCI_PARAM_ISOLATION_LEVEL;
      T_CCI_ERROR cci_error;
      int level;
      int res;

      if( objc != 2){
        Tcl_WrongNumArgs(interp, 2, objv, 0);
        return TCL_ERROR;
      }

      res = cci_get_db_parameter(pDb->connection, param_name, (void *) &level, &cci_error);
      if(res < 0) {
        Tcl_SetResult(interp, (char *)"Get value failed", TCL_STATIC);
        return TCL_ERROR;
      } else {
        if(level==4) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("readcommitted", -1));
        } else if(level==5) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("repeatableread", -1));
        } else if(level==6) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("serializable", -1));
        }
      }

      break;
    }

    case DB_SETISOLATION: {
      T_CCI_DB_PARAM param_name = CCI_PARAM_ISOLATION_LEVEL;
      T_CCI_ERROR cci_error;
      int level = 0;
      int len = 0;
      char *setLevel = NULL;
      Tcl_Obj *return_obj;

      if( objc == 3){
        setLevel = Tcl_GetStringFromObj(objv[2], &len);

        if( !setLevel || len < 1 ) {
          return TCL_ERROR;
        }
      } else {
        Tcl_WrongNumArgs(interp, 2, objv, "autocommit");
        return TCL_ERROR;
      }

      if(strcmp(setLevel, "readcommitted")==0) {
         level = 4;
      } else if(strcmp(setLevel, "repeatableread")==0) {
         level = 5;
      } else if(strcmp(setLevel, "serializable")==0) {
         level = 6;
      }

      if(cci_set_db_parameter(pDb->connection, param_name, (void *) &level, &cci_error) != 0) {
          return_obj = Tcl_NewBooleanObj(0);
      } else {
          return_obj = Tcl_NewBooleanObj(1);
      }

      Tcl_SetObjResult(interp, return_obj);

      break;
    }

    case DB_COMMIT: {
      int error;
      T_CCI_ERROR cci_error;

      if( objc != 2){
        Tcl_WrongNumArgs(interp, 2, objv, 0);
        return TCL_ERROR;
      }

      /*
       * If auto-commit mode is OFF, you must explicitly commit or roll back
       * transaction by using the cci_end_tran() function.
       */
      error = cci_end_tran(pDb->connection, CCI_TRAN_COMMIT, &cci_error);
      if(error < 0) {
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
      } else {
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
      }

      break;
    }

    case DB_ROLLBACK: {
      int error;
      T_CCI_ERROR cci_error;

      if( objc != 2){
        Tcl_WrongNumArgs(interp, 2, objv, 0);
        return TCL_ERROR;
      }

      /*
       * If auto-commit mode is OFF, you must explicitly commit or roll back
       * transaction by using the cci_end_tran() function.
       */
      error = cci_end_tran(pDb->connection, CCI_TRAN_ROLLBACK, &cci_error);
      if(error < 0) {
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
      } else {
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
      }

      break;
    }

    case DB_PREPARE: {
      char *zQuery =  NULL;
      char *errorStr =  NULL;
      int len = 0;
      Tcl_HashEntry *newHashEntryPtr;
      char handleName[16 + TCL_INTEGER_SPACE];
      Tcl_Obj *pResultStr = NULL;
      CUBRIDStmt *pStmt;
      int newvalue;
      T_CCI_ERROR cci_error;

      if( objc == 3){
        zQuery = Tcl_GetStringFromObj(objv[2], &len);

        if( !zQuery || len < 1 ) {
          return TCL_ERROR;
        }
      }else{
        Tcl_WrongNumArgs(interp, 2, objv, "SQL_String");
        return TCL_ERROR;
      }

      pStmt = (CUBRIDStmt *)Tcl_Alloc( sizeof(*pStmt) );
      if( pStmt==0 ){
        Tcl_SetResult(interp, (char *)"malloc failed", TCL_STATIC);
        return TCL_ERROR;
      }

      pStmt->request = cci_prepare (pDb->connection, zQuery, 0, &cci_error);
      if(pStmt->request < 0) {
          errorStr = cci_error.err_msg;
          Tcl_SetResult (interp, errorStr, NULL);

          Tcl_Free((char*)pStmt);
          pStmt = 0;

          return TCL_ERROR;
      } else {
          pStmt->cloblink = NULL;
          pStmt->bloblink = NULL;

          Tcl_MutexLock(&myMutex);
          sprintf( handleName, "cubrid_stat%d", tsdPtr->stmt_count++ );

          pResultStr = Tcl_NewStringObj( handleName, -1 );

          newHashEntryPtr = Tcl_CreateHashEntry(tsdPtr->cubrid_hashtblPtr, handleName, &newvalue);
          Tcl_SetHashValue(newHashEntryPtr, pStmt);
          Tcl_MutexUnlock(&myMutex);

          Tcl_CreateObjCommand(interp, handleName, (Tcl_ObjCmdProc *) CUBRID_STMT,
                (char *) pDb, (Tcl_CmdDeleteProc *)NULL);
      }

      Tcl_SetObjResult(interp, pResultStr);

      break;
    }

    case DB_VERSION: {
      char ver_str[255];
      int res;

      if( objc != 2){
        Tcl_WrongNumArgs(interp, 2, objv, 0);
        return TCL_ERROR;
      }

      res = cci_get_db_version(pDb->connection, ver_str, 255);
      if(res < 0) {
        return TCL_ERROR;
      } else {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(ver_str, -1));
      }

      break;
    }

    case DB_ROW_COUNT: {
      int res;
      int rowcount;
      T_CCI_ERROR cci_error;

      if( objc != 2){
        Tcl_WrongNumArgs(interp, 2, objv, 0);
        return TCL_ERROR;
      }

      res = cci_row_count(pDb->connection, &rowcount, &cci_error);
      if(res < 0) {
        return TCL_ERROR;
      } else {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(rowcount));
      }

      break;
    }

    case DB_LAST_INSERT_ID: {
      int res;
      char *last_id = NULL;
      T_CCI_ERROR cci_error;

      if( objc != 2){
        Tcl_WrongNumArgs(interp, 2, objv, 0);
        return TCL_ERROR;
      }

      res = cci_get_last_insert_id(pDb->connection, &last_id, &cci_error);
      if(res < 0) {
        return TCL_ERROR;
      } else {
        if (last_id == NULL) {
            /*
             * Problem: return "0" is OK?
             */
            Tcl_SetObjResult(interp, Tcl_NewStringObj("0", -1));
        } else {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(last_id, -1));
        }
      }

      break;
    }

    case DB_CLOSE: {
      if( objc != 2){
        Tcl_WrongNumArgs(interp, 2, objv, 0);
        return TCL_ERROR;
      }

      Tcl_DeleteCommand(interp, Tcl_GetStringFromObj(objv[0], 0));

      break;
    }

  } /* End of the SWITCH statement */

  return rc;
}


static int CUBRID_MAIN(void *cd, Tcl_Interp *interp, int objc,Tcl_Obj *const*objv){
  CUBRIDDATA *p;
  const char *zArg;
  int i;
  char *host = NULL;
  int port = 0;
  char *dbname = NULL;
  char *username = NULL;
  char *password = NULL;
  char *properties = NULL;
  char connect_url[2048] = {'\0'};
  T_CCI_ERROR cci_error;

  if( objc<2 || (objc&1)!=0 ){
    Tcl_WrongNumArgs(interp, 1, objv,
      "HANDLE ?-host HOST? ?-port PORT? ?-dbname DBNAME? ?-user username? ?-passwd password? ?-property properties?"
    );
    return TCL_ERROR;
  }

  for(i=2; i+1<objc; i+=2){
    zArg = Tcl_GetStringFromObj(objv[i], 0);

    if( strcmp(zArg, "-host")==0 ){
        host = Tcl_GetStringFromObj(objv[i + 1], 0);
    }else if( strcmp(zArg, "-port")==0 ){
        /*
         * Port number must be in range [0..65535], so check it.
         */
        if(Tcl_GetIntFromObj(interp, objv[i + 1], &port) != TCL_OK) {
            return TCL_ERROR;
        }

        if(port < 0 || port > 65535) {
            Tcl_AppendResult(interp, "port number must be in range [0..65535]", (char*)0);
            return TCL_ERROR;
        }
    }else if( strcmp(zArg, "-dbname")==0 ){
        dbname = Tcl_GetStringFromObj(objv[i + 1], 0);
    }else if( strcmp(zArg, "-user")==0 ){
        username = Tcl_GetStringFromObj(objv[i + 1], 0);
    }else if( strcmp(zArg, "-passwd")==0 ){
        password = Tcl_GetStringFromObj(objv[i + 1], 0);
    }else if( strcmp(zArg, "-property")==0 ){
        properties = Tcl_GetStringFromObj(objv[i + 1], 0);
    }else{
      Tcl_AppendResult(interp, "unknown option: ", zArg, (char*)0);
      return TCL_ERROR;
    }
  }

  /* Check our parameters */
  if(host == NULL) {
      host = "localhost";
  }

  if (port == 0) {
      port = 33000;
  }

  if(dbname == NULL) {
      dbname = "demo";
  }

  if(username == NULL) {
      username = "public";
  }

  if(password == NULL) {
      password = "";
  }

  Tcl_MutexLock(&cubridMutex);
  if (cubridRefCount == 0) {
    if ((cubridLoadHandle = CubridInitStubs(interp)) == NULL) {
        Tcl_MutexUnlock(&cubridMutex);
        return TCL_ERROR;
    }
  }
  ++cubridRefCount;
  Tcl_MutexUnlock(&cubridMutex);

  p = (CUBRIDDATA *)Tcl_Alloc( sizeof(*p) );
  if( p==0 ){
    Tcl_SetResult(interp, (char *)"malloc failed", TCL_STATIC);
    return TCL_ERROR;
  }

  memset(p, 0, sizeof(*p));

  if (properties && strlen(properties) > 0) {
      snprintf(connect_url, sizeof(connect_url), "cci:CUBRID:%s:%d:%s:::?%s",
               host, port, dbname, properties);
  } else {
      snprintf(connect_url, sizeof(connect_url), "cci:CUBRID:%s:%d:%s:::",
               host, port, dbname);
  }

  /*
   * I do not fill user and password in connect_url, so pass these parameters here
   */
  p->connection = cci_connect_with_url_ex(connect_url, username, password, &cci_error);

  if (p->connection < 0) {
      p->connection = 0;

      /*
       * It is a problem for me. Could I need to unload here?
       */
      Tcl_MutexLock(&cubridMutex);
      if (--cubridRefCount == 0) {
          Tcl_FSUnloadFile(NULL, cubridLoadHandle);
          cubridLoadHandle = NULL;
      }
      Tcl_MutexUnlock(&cubridMutex);

      if(p) Tcl_Free((char *)p);
      Tcl_SetResult (interp, "Connect CUBRID fail", NULL);
      return TCL_ERROR;
  }

  p->interp = interp;

  zArg = Tcl_GetStringFromObj(objv[1], 0);
  Tcl_CreateObjCommand(interp, zArg, DbObjCmd, (char*)p, DbDeleteCmd);

  return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Cubrid_Init --
 *
 *  Initialize the new package.
 *
 * Results:
 *  A standard Tcl result
 *
 * Side effects:
 *  The Cubrid package is created.
 *
 *----------------------------------------------------------------------
 */

int
Cubrid_Init(Tcl_Interp *interp)
{
    if (Tcl_InitStubs(interp, "8.6", 0) == NULL) {
        return TCL_ERROR;
    }
    if (Tcl_PkgProvide(interp, PACKAGE_NAME, PACKAGE_VERSION) != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     *   Tcl_GetThreadData handles the auto-initialization of all data in
     *  the ThreadSpecificData to NULL at first time.
     */
    Tcl_MutexLock(&myMutex);
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
        Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (tsdPtr->initialized == 0) {
        tsdPtr->initialized = 1;
        tsdPtr->cubrid_hashtblPtr = (Tcl_HashTable *) ckalloc(sizeof(Tcl_HashTable));
        Tcl_InitHashTable(tsdPtr->cubrid_hashtblPtr, TCL_STRING_KEYS);

        tsdPtr->stmt_count = 0;
    }
    Tcl_MutexUnlock(&myMutex);

    /* Add a thread exit handler to delete hash table */
    Tcl_CreateThreadExitHandler(CUBRID_Thread_Exit, (ClientData)NULL);

    Tcl_CreateObjCommand(interp, "cubrid", (Tcl_ObjCmdProc *) CUBRID_MAIN,
        (ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);

    return TCL_OK;
}
