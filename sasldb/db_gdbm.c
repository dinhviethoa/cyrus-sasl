/* db_gdbm.c--SASL gdbm interface
 * Rob Siemborski
 * Rob Earhart
 * $Id: db_gdbm.c,v 1.1.2.4 2001/07/26 22:12:14 rjs3 Exp $
 */
/* 
 * Copyright (c) 2001 Carnegie Mellon University.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The name "Carnegie Mellon University" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For permission or any other legal
 *    details, please contact  
 *      Office of Technology Transfer
 *      Carnegie Mellon University
 *      5000 Forbes Avenue
 *      Pittsburgh, PA  15213-3890
 *      (412) 268-4387, fax: (412) 268-7395
 *      tech-transfer@andrew.cmu.edu
 *
 * 4. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Computing Services
 *     at Carnegie Mellon University (http://www.cmu.edu/computing/)."
 *
 * CARNEGIE MELLON UNIVERSITY DISCLAIMS ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY BE LIABLE
 * FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <config.h>
#include <gdbm.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <assert.h>
#include "sasldb.h"

static int db_ok = 0;

/* This provides a version of _sasl_db_getsecret and
 * _sasl_db_putsecret which work with gdbm. */
static int
getsecret(const sasl_utils_t *utils,
	  sasl_conn_t *conn,
	  const char *auth_identity,
	  const char *realm,
	  sasl_secret_t ** secret)
{
  int result = SASL_OK;
  char *key;
  size_t key_len;
  GDBM_FILE db;
  datum gkey, gvalue;  
  void *cntxt;
  sasl_getopt_t *getopt;
  const char *path = SASL_DB_PATH;

  if (!auth_identity || !secret || !realm || !db_ok)
    return SASL_FAIL;

  result = _sasldb_alloc_key(utils, auth_identity, realm, SASL_AUX_PASSWORD,
			     &key, &key_len);
  if (result != SASL_OK)
    return result;

  if (utils->getcallback(conn, SASL_CB_GETOPT,
                        &getopt, &cntxt) == SASL_OK) {
      const char *p;
      if (getopt(cntxt, NULL, "sasldb_path", &p, NULL) == SASL_OK 
	  && p != NULL && *p != 0) {
          path = p;
      }
  }
  db = gdbm_open((char *)path, 0, GDBM_READER, S_IRUSR | S_IWUSR, NULL);
  if (! db) {
    result = SASL_FAIL;
    goto cleanup;
  }
  gkey.dptr = key;
  gkey.dsize = key_len;
  gvalue = gdbm_fetch(db, gkey);
  gdbm_close(db);
  if (! gvalue.dptr) {
    result = SASL_NOUSER;
    goto cleanup;
  }
  *secret = utils->malloc(sizeof(sasl_secret_t)
			  + gvalue.dsize
			  + 1);
  if (! *secret) {
    result = SASL_NOMEM;
    free(gvalue.dptr);
    goto cleanup;
  }
  (*secret)->len = gvalue.dsize;
  memcpy(&(*secret)->data, gvalue.dptr, gvalue.dsize);
  (*secret)->data[(*secret)->len] = '\0'; /* sanity */
  /* Note: not sasl_FREE!  This is memory allocated by gdbm,
   * which is using libc malloc/free. */
  free(gvalue.dptr);

 cleanup:
  utils->free(key);

  return result;
}

static int
putsecret(const sasl_utils_t *utils,
	  sasl_conn_t *conn,
	  const char *auth_identity,
	  const char *realm,
	  const sasl_secret_t * secret)
{
  int result = SASL_OK;
  char *key;
  size_t key_len;
  GDBM_FILE db;
  datum gkey;
  void *cntxt;
  sasl_getopt_t *getopt;
  const char *path = SASL_DB_PATH;

  if (!auth_identity || !realm)
      return SASL_FAIL;

  result = _sasldb_alloc_key(utils, auth_identity, realm, SASL_AUX_PASSWORD,
			     &key, &key_len);
  if (result != SASL_OK)
    return result;

  if (utils->getcallback(conn, SASL_CB_GETOPT,
			 &getopt, &cntxt) == SASL_OK) {
      const char *p;
      if (getopt(cntxt, NULL, "sasldb_path", &p, NULL) == SASL_OK 
	  && p != NULL && *p != 0) {
          path = p;
      }
  }
  db = gdbm_open((char *)path, 0, GDBM_WRCREAT, S_IRUSR | S_IWUSR, NULL);
  if (! db) {
      utils->log(NULL, SASL_LOG_ERR,
		"SASL error opening password file. Do you have write permissions?\n");
    result = SASL_FAIL;
    goto cleanup;
  }
  gkey.dptr = key;
  gkey.dsize = key_len;
  if (secret) {
    datum gvalue;
    gvalue.dptr = (char *)&secret->data;
    gvalue.dsize = secret->len;
    if (gdbm_store(db, gkey, gvalue, GDBM_REPLACE))
      result = SASL_FAIL;
  } else {
    if (gdbm_delete(db, gkey))
      result = SASL_NOUSER;
  }
  gdbm_close(db);

 cleanup:
  utils->free(key);

  return result;
}

sasl_server_getsecret_t *_sasl_db_getsecret = &getsecret;
sasl_server_putsecret_t *_sasl_db_putsecret = &putsecret;

int _sasl_check_db(const sasl_utils_t *utils,
		   sasl_conn_t *conn)
{
    const char *path = SASL_DB_PATH;
    int ret;
    void *cntxt;
    sasl_getopt_t *getopt;
    sasl_verifyfile_t *vf;

    if(!utils) return SASL_BADPARAM;

    if (utils->getcallback(conn, SASL_CB_GETOPT,
			   &getopt, &cntxt) == SASL_OK) {
	const char *p;
	if (getopt(cntxt, NULL, "sasldb_path", &p, NULL) == SASL_OK 
	    && p != NULL && *p != 0) {
	    path = p;
	}
    }

    ret = utils->getcallback(NULL, SASL_CB_VERIFYFILE,
			     &vf, &cntxt);
    if(ret != SASL_OK) return ret;

    ret = vf(cntxt, path, SASL_VRFY_PASSWD);

    if (ret == SASL_OK) {
	db_ok = 1;
    }

    if (ret == SASL_OK || ret == SASL_CONTINUE) {
	return SASL_OK;
    } else {
	return ret;
    }
}

typedef struct gdbm_handle 
{
    GDBM_FILE db;
    datum dkey;
    int first;
} handle_t;

sasldb_handle _sasldb_getkeyhandle(const sasl_utils_t *utils,
				   sasl_conn_t *conn) 
{
    const char *path = SASL_DB_PATH;
    sasl_getopt_t *getopt;
    void *cntxt;
    GDBM_FILE db;
    handle_t *handle;
    
    if(!utils || !conn) return NULL;

    if (utils->getcallback(conn, SASL_CB_GETOPT,
			   &getopt, &cntxt) == SASL_OK) {
	const char *p;
	if (getopt(cntxt, NULL, "sasldb_path", &p, NULL) == SASL_OK 
	    && p != NULL && *p != 0) {
	    path = p;
	}
    }

    db = gdbm_open((char *)path, 0, GDBM_READER, S_IRUSR | S_IWUSR, NULL);

    if(!db)
	return NULL;

    handle = utils->malloc(sizeof(handle_t));
    if(!handle) {
	gdbm_close(db);
	return NULL;
    }
    
    handle->db = db;
    handle->first = 1;

    return (sasldb_handle)handle;
}

int _sasldb_getnextkey(const sasl_utils_t *utils __attribute__((unused)),
		       sasldb_handle handle, char *out,
		       const size_t max_out, size_t *out_len) 
{
    handle_t *dbh = (handle_t *)handle;
    datum nextkey;

    if(!utils || !handle || !out || !max_out)
	return SASL_BADPARAM;

    if(dbh->first) {
	dbh->dkey = gdbm_firstkey(dbh->db);
	dbh->first = 0;
    } else {
	nextkey = gdbm_nextkey(dbh->db, dbh->dkey);
	dbh->dkey = nextkey;
    }

    if(dbh->dkey.dptr == NULL)
	return SASL_OK;
    
    if((unsigned)dbh->dkey.dsize > max_out)
	return SASL_BUFOVER;
    
    memcpy(out, dbh->dkey.dptr, dbh->dkey.dsize);
    if(out_len) *out_len = dbh->dkey.dsize;
    
    return SASL_CONTINUE;
}

int _sasldb_releasekeyhandle(const sasl_utils_t *utils,
			     sasldb_handle handle) 
{
    handle_t *dbh = (handle_t *)handle;
    
    if(!utils || !dbh) return SASL_BADPARAM;

    if(dbh->db) gdbm_close(dbh->db);

    utils->free(dbh);
    
    return SASL_OK;
}
