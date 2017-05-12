/*
 * cubridStubInit.c --
 *
 *	Stubs tables for the foreign CUBRID libraries so that
 *	Tcl extensions can use them without the linker's knowing about them.
 *
 * @CREATED@ 2017-05-12 11:56:09Z by genExtStubs.tcl from cubridStubDefs.txt
 *
 *-----------------------------------------------------------------------------
 */

#include <tcl.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "cas_cci.h"
#include "cubridStubs.h"

/*
 * Static data used in this file
 */

/*
 * ABI version numbers of the CUBRID API that we can cope with.
 */

static const char *const cubridSuffixes[] = {
    "", NULL
};

/*
 * Names of the libraries that might contain the CUBRID API
 */

static const char *const cubridStubLibNames[] = {
    /* @LIBNAMES@: DO NOT EDIT THESE NAMES */
    "libcascci", "cascci", NULL
    /* @END@ */
};

/*
 * Names of the functions that we need from CUBRID
 */

static const char *const cubridSymbolNames[] = {
    /* @SYMNAMES@: DO NOT EDIT THESE NAMES */
    "cci_connect_with_url_ex",
    "cci_disconnect",
    "cci_get_db_version",
    "cci_get_autocommit",
    "cci_set_autocommit",
    "cci_get_db_parameter",
    "cci_set_db_parameter",
    "cci_end_tran",
    "cci_get_last_insert_id",
    "cci_prepare",
    "cci_row_count",
    "cci_clob_new",
    "cci_clob_read",
    "cci_clob_write",
    "cci_clob_free",
    "cci_blob_new",
    "cci_blob_read",
    "cci_blob_write",
    "cci_blob_free",
    "cci_bind_param",
    "cci_execute",
    "cci_cursor",
    "cci_fetch",
    "cci_get_data",
    "cci_get_result_info",
    "cci_close_req_handle",
    "cci_set_make",
    "cci_set_get",
    "cci_set_size",
    "cci_set_free",
    NULL
    /* @END@ */
};

/*
 * Table containing pointers to the functions named above.
 */

static cubridStubDefs cubridStubsTable;
cubridStubDefs* cubridStubs = &cubridStubsTable;

/*
 *-----------------------------------------------------------------------------
 *
 * CubridInitStubs --
 *
 *	Initialize the Stubs table for the CUBRID API
 *
 * Results:
 *	Returns the handle to the loaded CUBRID client library, or NULL
 *	if the load is unsuccessful. Leaves an error message in the
 *	interpreter.
 *
 *-----------------------------------------------------------------------------
 */

MODULE_SCOPE Tcl_LoadHandle
CubridInitStubs(Tcl_Interp* interp)
{
    int i, j;
    int status;			/* Status of Tcl library calls */
    Tcl_Obj* path;		/* Path name of a module to be loaded */
    Tcl_Obj* shlibext;		/* Extension to use for load modules */
    Tcl_LoadHandle handle = NULL;
				/* Handle to a load module */

    /* Determine the shared library extension */

    status = Tcl_EvalEx(interp, "::info sharedlibextension", -1,
			TCL_EVAL_GLOBAL);
    if (status != TCL_OK) return NULL;
    shlibext = Tcl_GetObjResult(interp);
    Tcl_IncrRefCount(shlibext);

    /* Walk the list of possible library names to find a CUBRID client */

    status = TCL_ERROR;
    for (i = 0; status == TCL_ERROR && cubridStubLibNames[i] != NULL; ++i) {
	for (j = 0; status == TCL_ERROR && cubridSuffixes[j] != NULL; ++j) {
	    path = Tcl_NewStringObj(cubridStubLibNames[i], -1);
	    Tcl_AppendObjToObj(path, shlibext);
	    Tcl_AppendToObj(path, cubridSuffixes[j], -1);
	    Tcl_IncrRefCount(path);

	    /* Try to load a client library and resolve symbols within it. */

	    Tcl_ResetResult(interp);
	    status = Tcl_LoadFile(interp, path, cubridSymbolNames, 0,
			      (void*)cubridStubs, &handle);
	    Tcl_DecrRefCount(path);
	}
    }

    /* 
     * Either we've successfully loaded a library (status == TCL_OK), 
     * or we've run out of library names (in which case status==TCL_ERROR
     * and the error message reflects the last unsuccessful load attempt).
     */
    Tcl_DecrRefCount(shlibext);
    if (status != TCL_OK) {
	return NULL;
    }
    return handle;
}
