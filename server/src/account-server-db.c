/*
 *
 * Copyright (c) 2012 - 2013 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include <glib.h>
#include <db-util.h>
#include <pthread.h>
#include <vconf.h>

#include <pkgmgr-info.h>
//#include <tzplatform_config.h>

#include <dbg.h>
#include <account_ipc_marshal.h>
#include <account_free.h>
#include <account-private.h>
#include <account_db_helper.h>
#include <account_crypto_service.h>
#include <account_err.h>
#include "account_type.h"
#include "account-server-db.h"

//typedef sqlite3_stmt* account_stmt;

#define EMAIL_SERVICE_CMDLINE "/usr/bin/email-service"

#define EMAIL_APPID "email-setting-efl"

#define ACCOUNT_DB_OPEN_READONLY 0
#define ACCOUNT_DB_OPEN_READWRITE 1

#define MAX_TEXT 4096

#define _TIZEN_PUBLIC_
#ifndef _TIZEN_PUBLIC_

#endif

static sqlite3* g_hAccountDB = NULL;
static sqlite3* g_hAccountDB2 = NULL;
static sqlite3* g_hAccountGlobalDB = NULL;
static sqlite3* g_hAccountGlobalDB2 = NULL;
pthread_mutex_t account_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t account_global_mutex = PTHREAD_MUTEX_INITIALIZER;

//static char *_account_dup_text(const char *text_data);
static int _account_insert_custom(account_s *account, int account_id);
static int _account_update_custom(account_s *account, int account_id);
static int _account_type_update_provider_feature(sqlite3 * account_db_handle, account_type_s *account_type, const char* app_id);

static void _account_insert_delete_update_notification_send(char *noti_name)
{
	if (!noti_name) {
		_ERR("Noti Name is NULL!!!!!!\n");
		return;
	}

	_INFO("noti_type = %s", noti_name);

	if (vconf_set_str(VCONFKEY_ACCOUNT_MSG_STR, noti_name) != 0) {
		_ERR("Vconf MSG Str set FAILED !!!!!!\n");;
	}
}

int _account_get_current_appid_cb(const pkgmgrinfo_appinfo_h handle, void *user_data)
{
	char* appid = NULL;
	char* item = NULL;
	GSList** appid_list = (GSList**)user_data;
	int pkgmgr_ret = -1;

	pkgmgr_ret = pkgmgrinfo_appinfo_get_appid(handle, &appid);

	if (pkgmgr_ret != PMINFO_R_OK)
		ACCOUNT_DEBUG("pkgmgrinfo_appinfo_get_appid(%d)", pkgmgr_ret);

	item = _account_dup_text(appid);
	*appid_list = g_slist_append(*appid_list, item);

	return 0;
}

static inline int __read_proc(const char *path, char *buf, int size)
{
	int fd = 0, ret = 0;

	if (buf == NULL || path == NULL) {
		ACCOUNT_ERROR("path and buffer is mandatory\n");
		return -1;
	}

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		ACCOUNT_ERROR("fd open error(%d)\n", fd);
		return -1;
	}

	ret = read(fd, buf, size - 1);
	if (ret <= 0) {
		ACCOUNT_ERROR("fd read error(%d)\n", fd);
		close(fd);
		return -1;
	} else {
		buf[ret] = 0;
	}

	close(fd);

	return ret;
}

char *_account_get_proc_cmdline_bypid(int pid)
{
	char buf[128];
	int ret = 0;

	ACCOUNT_SNPRINTF(buf, sizeof(buf), "/proc/%d/cmdline", pid);
	ret = __read_proc(buf, buf, sizeof(buf));
	if (ret <= 0) {
		ACCOUNT_DEBUG("No proc directory (%d)\n", pid);
		return NULL;
	}

	return strdup(buf);
}

/*
static int _account_execute_query_from_global_db(const char *query)
{
	int rc = -1;
	char* pszErrorMsg = NULL;

	if (!query) {
		ACCOUNT_ERROR("NULL query\n");
		return _ACCOUNT_ERROR_QUERY_SYNTAX_ERROR;
	}

	if (!g_hAccountGlobalDB) {
		ACCOUNT_ERROR("Global DB is not opened\n");
		return _ACCOUNT_ERROR_DB_NOT_OPENED;
	}

	rc = sqlite3_exec(g_hAccountGlobalDB, query, NULL, NULL, &pszErrorMsg);
	if (SQLITE_OK != rc) {
		ACCOUNT_ERROR("sqlite3_exec rc(%d) query(%s) failed(%s).", rc, query, pszErrorMsg);
		sqlite3_free(pszErrorMsg);
	}

	return rc;
}
*/

/*
static int _account_begin_transaction_from_global_db(void)
{
	ACCOUNT_DEBUG("_account_begin_transaction start");
	int ret = -1;

	ret = _account_execute_query_from_global_db("BEGIN IMMEDIATE TRANSACTION");

	if (ret == SQLITE_BUSY) {
		ACCOUNT_ERROR(" sqlite3 busy = %d", ret);
		return _ACCOUNT_ERROR_DATABASE_BUSY;
	} else if (ret != SQLITE_OK) {
		ACCOUNT_ERROR("_account_svc_begin_transaction_in_global_db fail :: %d", ret);
		return _ACCOUNT_ERROR_DB_FAILED;
	}

	ACCOUNT_DEBUG("_account_begin_transaction_in_global_db end");
	return _ACCOUNT_ERROR_NONE;
}

static int _account_end_transaction_from_global_db(bool is_success)
{
	ACCOUNT_DEBUG("_account_end_transaction_in_global_db start");

	int ret = -1;

	if (is_success == true) {
		ret = _account_execute_query_from_global_db("COMMIT TRANSACTION");
		ACCOUNT_DEBUG("_account_end_transaction_in_global_db COMMIT");
	} else {
		ret = _account_execute_query_from_global_db("ROLLBACK TRANSACTION");
		ACCOUNT_DEBUG("_account_end_transaction ROLLBACK");
	}

	if (ret == SQLITE_PERM) {
		ACCOUNT_ERROR("Account permission denied :: %d", ret);
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	if (ret == SQLITE_BUSY) {
		ACCOUNT_DEBUG(" sqlite3 busy = %d", ret);
		return _ACCOUNT_ERROR_DATABASE_BUSY;
	}

	if (ret != SQLITE_OK) {
		ACCOUNT_ERROR("_account_svc_end_transaction_in_global_db fail :: %d", ret);
		return _ACCOUNT_ERROR_DB_FAILED;
	}

	ACCOUNT_DEBUG("_account_end_transaction_in_global_db end");
	return _ACCOUNT_ERROR_NONE;
}
*/

int _account_global_db_open(void)
{
	int rc = 0;
	int ret = -1;
	char account_db_path[256] = {0, };

	_INFO("start _account_global_db_open()");

	ACCOUNT_MEMSET(account_db_path, 0x00, sizeof(account_db_path));
	ACCOUNT_GET_GLOBAL_DB_PATH(account_db_path, sizeof(account_db_path));

	if (g_hAccountGlobalDB) {
		_ERR("Account database is using in another app. %x", g_hAccountDB);
		return _ACCOUNT_ERROR_DATABASE_BUSY;
	}

	ret = _account_db_handle_close(g_hAccountGlobalDB2);
	if (ret != _ACCOUNT_ERROR_NONE)
		ACCOUNT_DEBUG("db_util_close(g_hAccountGlobalDB2) fail ret = %d", ret);

	ACCOUNT_DEBUG("before _account_global_db_open()");
//	if (mode == ACCOUNT_DB_OPEN_READWRITE)
//		rc = db_util_open(account_db_path, &g_hAccountDB, DB_UTIL_REGISTER_HOOK_METHOD);
//	else if (mode == ACCOUNT_DB_OPEN_READONLY)
	rc = db_util_open_with_options(account_db_path, &g_hAccountGlobalDB, SQLITE_OPEN_READONLY, NULL);
//	else
//		return _ACCOUNT_ERROR_DB_NOT_OPENED;
	ACCOUNT_DEBUG("after _account_global_db_open() sqlite_rc = %d", rc);

	if (rc == SQLITE_PERM || _account_db_err_code(g_hAccountGlobalDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Account permission denied");
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	if (rc == SQLITE_BUSY) {
		ACCOUNT_ERROR("busy handler fail.");
		return _ACCOUNT_ERROR_DATABASE_BUSY;
	}

	if (rc != SQLITE_OK) {
		ACCOUNT_ERROR("The database isn't connected.");
		return _ACCOUNT_ERROR_DB_NOT_OPENED;
	}

	_INFO("end _account_global_db_open()");
	return _ACCOUNT_ERROR_NONE;
}

int _account_global_db_close(void)
{
	ACCOUNT_DEBUG("start account_global_db_close()");
	int ret = -1;
/*
	ret = _account_db_handle_close(g_hAccountGlobalDB2);
	if (ret != _ACCOUNT_ERROR_NONE)
		ACCOUNT_DEBUG("db_util_close(g_hAccountGlobalDB2) fail ret = %d", ret);
*/
	ret = _account_db_handle_close(g_hAccountGlobalDB);
	if (ret != _ACCOUNT_ERROR_NONE) {
		ACCOUNT_ERROR("db_util_close(g_hAccountGlobalDB) fail ret = %d", ret);
		g_hAccountGlobalDB2 = g_hAccountGlobalDB;
	}
	g_hAccountGlobalDB = NULL;

	return ret;
}

static bool _account_check_add_more_account(const char* app_id)
{
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0;

	ACCOUNT_RETURN_VAL((app_id != 0), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("APP ID IS NULL"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT COUNT(*) FROM %s WHERE AppId = '%s' and MultipleAccountSupport = 1", ACCOUNT_TYPE_TABLE, app_id);
	rc = _account_get_record_count(g_hAccountDB, query);

	/* multiple account support case (User DB & global DB) */
	if (rc > 0 || _account_get_record_count(g_hAccountGlobalDB, query) > 0) {
		ACCOUNT_SLOGD("app id (%s) supports multiple account. rc(%d)\n", app_id, rc);
		return TRUE;
	}

	/* multiple account not support case */
	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);
	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT COUNT(*) FROM %s WHERE package_name = '%s'", ACCOUNT_TABLE, app_id);
	rc = _account_get_record_count(g_hAccountDB, query);

	if (rc <= 0) {
		ACCOUNT_SLOGD("app id (%s) supports single account. and there is no account of the app id\n", app_id);
		return TRUE;
	}

	return FALSE;
}


int _account_db_open(int mode, int pid, uid_t uid)
{
	int rc = 0;
	int ret = -1;
	char account_db_dir[256] = {0, };
	char account_db_path[256] = {0, };

	_INFO("start _account_db_open() pid [%d]. uid [%d]", pid, uid);

	ACCOUNT_MEMSET(account_db_dir, 0x00, sizeof(account_db_dir));
	ACCOUNT_MEMSET(account_db_path, 0x00, sizeof(account_db_path));

	ACCOUNT_GET_USER_DB_PATH(account_db_path, sizeof(account_db_path), uid);

	if (g_hAccountDB) {
		_ERR("Account database is using in another app. %x", g_hAccountDB);
		return _ACCOUNT_ERROR_DATABASE_BUSY;
	}

	ret = _account_db_handle_close(g_hAccountDB2);
	if (ret != _ACCOUNT_ERROR_NONE)
		ACCOUNT_DEBUG("db_util_close(g_hAccountDB2) fail ret = %d", ret);

	ACCOUNT_GET_USER_DB_DIR(account_db_dir, sizeof(account_db_dir), uid);

	if (mkdir(account_db_dir, 0777) != 0)
		ACCOUNT_DEBUG("\"%s\" is already exist directory", account_db_dir);

	ACCOUNT_DEBUG("before db_util_open()");
//	if (mode == ACCOUNT_DB_OPEN_READWRITE)
		rc = db_util_open(account_db_path, &g_hAccountDB, DB_UTIL_REGISTER_HOOK_METHOD);
//	else if (mode == ACCOUNT_DB_OPEN_READONLY)
//		rc = db_util_open_with_options(account_db_path, &g_hAccountDB, SQLITE_OPEN_READONLY, NULL);
//	else
//		return _ACCOUNT_ERROR_DB_NOT_OPENED;
	ACCOUNT_DEBUG("after db_util_open() sqlite_rc = %d", rc);

	if (rc == SQLITE_PERM || _account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Account permission denied");
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	if (rc == SQLITE_BUSY) {
		ACCOUNT_ERROR("busy handler fail.");
		return _ACCOUNT_ERROR_DATABASE_BUSY;
	}

	if (rc != SQLITE_OK) {
		ACCOUNT_ERROR("The database isn't connected.");
		return _ACCOUNT_ERROR_DB_NOT_OPENED;
	}

	rc = _account_check_is_all_table_exists(g_hAccountDB);

	if (rc < 0) {
		_ERR("_account_check_is_all_table_exists rc=[%d]", rc);
		return rc;
	} else if (rc == ACCOUNT_TABLE_TOTAL_COUNT) {
		_INFO("Tables OK");
	} else {
		int ret = _account_create_all_tables(g_hAccountDB);
		if (ret != _ACCOUNT_ERROR_NONE) {
			_ERR("_account_create_all_tables fail ret=[%d]", ret);
			return ret;
		}
	}

	_INFO("end _account_db_open()");
	return _ACCOUNT_ERROR_NONE;
}

int _account_db_close(void)
{
	ACCOUNT_DEBUG("start db_util_close()");
	int ret = -1;
/*
	ret = _account_db_handle_close(g_hAccountDB2);
	if (ret != _ACCOUNT_ERROR_NONE)
		ACCOUNT_DEBUG("db_util_close(g_hAccountDB2) fail ret = %d", ret);
*/
	ret = _account_db_handle_close(g_hAccountDB);
	if (ret != _ACCOUNT_ERROR_NONE) {
		ACCOUNT_ERROR("db_util_close(g_hAccountDB) fail ret = %d", ret);
		g_hAccountDB2 = g_hAccountDB;
	}
	g_hAccountDB = NULL;

	return ret;
}

static int _account_execute_insert_query(account_s *account)
{
	_INFO("_account_execute_insert_query start");

	int				rc = 0;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;

	/* check whether app id exist in account type db */

	if (!account->user_name && !account->display_name && !account->email_address) {
		_INFO("");
		ACCOUNT_ERROR("Mandetory fields is NULL. At least one field is required among username, display name, email address\n");
		return _ACCOUNT_ERROR_INVALID_PARAMETER;
	}

	_INFO("");
	ACCOUNT_MEMSET(query, 0x00, sizeof(query));
	ACCOUNT_SNPRINTF(query, sizeof(query), "INSERT INTO %s (user_name, email_address , display_name , icon_path , source , package_name , "
			"access_token , domain_name , auth_type , secret , sync_support , txt_custom0, txt_custom1, txt_custom2, txt_custom3, txt_custom4, "
			"int_custom0, int_custom1, int_custom2, int_custom3, int_custom4, txt_custom0 ) values " // to do urusa
			"(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",	ACCOUNT_TABLE);

	hstmt = _account_prepare_query(g_hAccountDB, query);
	ACCOUNT_RETURN_VAL((hstmt != NULL), {}, _ACCOUNT_ERROR_DB_FAILED, ("_account_prepare_query(g_hAccountDB, ) failed(%s).\n", _account_db_err_msg(g_hAccountDB)));

	_INFO("");
	_account_convert_account_to_sql(account, hstmt, query);

	_INFO("");
	rc = _account_query_step(hstmt);
	if (rc != SQLITE_DONE) {
		_INFO("");
		ACCOUNT_ERROR("account_db_query_step() failed(%d, %s)", rc, _account_db_err_msg(g_hAccountDB));

		if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM)
			error_code = _ACCOUNT_ERROR_PERMISSION_DENIED;
		else
			error_code = _ACCOUNT_ERROR_DB_FAILED;
	}

	_INFO("");
	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	_INFO("_account_execute_insert_query end");
	return error_code;
}

static int _account_insert_capability(account_s *account, int account_id)
{
	_INFO("_account_insert_capability start");
	int				rc, count = 1;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	account_stmt	hstmt = NULL;

	ACCOUNT_RETURN_VAL((account != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("ACCOUNT HANDLE IS NULL"));

	if (g_slist_length(account->capablity_list) == 0) {
		ACCOUNT_DEBUG("_account_insert_capability, no capability\n");
		return _ACCOUNT_ERROR_NONE;
	}

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT COUNT(*) from %s where _id=%d", ACCOUNT_TABLE, account_id);

	_INFO("_account_insert_capability _account_get_record_count [%s]", query);
	rc = _account_get_record_count(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		_ERR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	if (rc <= 0)
		return _ACCOUNT_ERROR_RECORD_NOT_FOUND;

	/* insert query*/

	GSList *iter;

	for (iter = account->capablity_list; iter != NULL; iter = g_slist_next(iter)) {
		int ret;
		count = 1;

		account_capability_s* cap_data = NULL;
		cap_data = (account_capability_s*)iter->data;

		_INFO("cap_data->type = %s, cap_data->value = %d \n", cap_data->type, cap_data->value);

		ACCOUNT_MEMSET(query, 0x00, sizeof(query));
		ACCOUNT_SNPRINTF(query, sizeof(query), "INSERT INTO %s(key, value, package_name, user_name, account_id) VALUES "
				"(?, ?, ?, ?, ?) ", CAPABILITY_TABLE);
		hstmt = _account_prepare_query(g_hAccountDB, query);

		ACCOUNT_RETURN_VAL((hstmt != NULL), {}, _ACCOUNT_ERROR_DB_FAILED, ("_account_prepare_query(g_hAccountDB, ) failed(%s).\n", _account_db_err_msg(g_hAccountDB)));

		ret = _account_query_bind_text(hstmt, count++, cap_data->type);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));
		ret = _account_query_bind_int(hstmt, count++, cap_data->value);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Integer binding fail"));
		ret = _account_query_bind_text(hstmt, count++, (char*)account->package_name);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));
		ret = _account_query_bind_text(hstmt, count++, (char*)account->user_name);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));
		ret = _account_query_bind_int(hstmt, count++, (int)account_id);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Integer binding fail"));

		rc = _account_query_step(hstmt);
		_INFO("_account_insert_capability _account_query_step[%d]", rc);

		if (rc != SQLITE_DONE) {
			_ERR("_account_query_step() failed(%d, %s)", rc, _account_db_err_msg(g_hAccountDB));
			break;
		}

		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;

	}

	_INFO("_account_insert_capability end");
	return _ACCOUNT_ERROR_NONE;
}

static int _account_update_capability(account_s *account, int account_id)
{
	int				rc, count = 1;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	account_stmt	hstmt = NULL;

	ACCOUNT_RETURN_VAL((account != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("ACCOUNT HANDLE IS NULL"));

	if (g_slist_length(account->capablity_list) == 0) {
		ACCOUNT_ERROR("_account_update_capability, no capability\n");
		return _ACCOUNT_ERROR_NONE;
	}

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT COUNT(*) from %s where _id=%d", ACCOUNT_TABLE, account_id);

	rc = _account_get_record_count(g_hAccountDB, query);

	if (rc <= 0) {
		ACCOUNT_SLOGI("_account_update_capability : related account item is not existed rc=%d , %s", rc, _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_RECORD_NOT_FOUND;
	}

	ACCOUNT_MEMSET(query, 0x00, sizeof(query));

	ACCOUNT_SNPRINTF(query, sizeof(query), "DELETE FROM %s WHERE account_id=? ", CAPABILITY_TABLE);
	hstmt = _account_prepare_query(g_hAccountDB, query);
	count = 1;
	_account_query_bind_int(hstmt, count++, (int)account_id);
	rc = _account_query_step(hstmt);

	if (rc != SQLITE_DONE) {
		ACCOUNT_ERROR("_account_query_step() failed(%d, %s)", rc, _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_DB_FAILED;
	}
	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	GSList *iter;

	for (iter = account->capablity_list; iter != NULL; iter = g_slist_next(iter)) {
		int ret;
		count = 1;
		ACCOUNT_MEMSET(query, 0x00, sizeof(query));
		ACCOUNT_SNPRINTF(query, sizeof(query), "INSERT INTO %s(key, value, package_name, user_name, account_id) VALUES "
				"(?, ?, ?, ?, ?) ", CAPABILITY_TABLE);

		hstmt = _account_prepare_query(g_hAccountDB, query);

		ACCOUNT_RETURN_VAL((hstmt != NULL), {}, _ACCOUNT_ERROR_DB_FAILED, ("_account_prepare_query(g_hAccountDB, ) failed(%s).\n", _account_db_err_msg(g_hAccountDB)));

		account_capability_s* cap_data = NULL;
		cap_data = (account_capability_s*)iter->data;

		ret = _account_query_bind_text(hstmt, count++, cap_data->type);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));
		ret = _account_query_bind_int(hstmt, count++, cap_data->value);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Integer binding fail"));
		ret = _account_query_bind_text(hstmt, count++, (char*)account->package_name);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));
		ret = _account_query_bind_text(hstmt, count++, (char*)account->user_name);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));
		ret = _account_query_bind_int(hstmt, count++, (int)account_id);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Integer binding fail"));

		rc = _account_query_step(hstmt);

		if (rc != SQLITE_DONE) {
			ACCOUNT_ERROR("_account_query_step() failed(%d, %s)", rc, _account_db_err_msg(g_hAccountDB));
			break;
		}

		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;

	}

	return _ACCOUNT_ERROR_NONE;
}

static int _account_update_capability_by_user_name(account_s *account, const char *user_name, const char *package_name)
{
	int				rc, count = 1;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	account_stmt	hstmt = NULL;

	ACCOUNT_RETURN_VAL((account != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("ACCOUNT HANDLE IS NULL"));

	if (g_slist_length(account->capablity_list) == 0) {
		ACCOUNT_ERROR("_account_update_capability_by_user_name, no capability\n");
		return _ACCOUNT_ERROR_NONE;
	}

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT COUNT(*) from %s where package_name= '%s' and user_name='%s'", ACCOUNT_TABLE, package_name, user_name);

	rc = _account_get_record_count(g_hAccountDB, query);

	if (rc <= 0) {
		ACCOUNT_SLOGI("_account_update_capability_by_user_name : related account item is not existed rc=%d , %s ", rc, _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_RECORD_NOT_FOUND;
	}

	ACCOUNT_MEMSET(query, 0x00, sizeof(query));

	ACCOUNT_SNPRINTF(query, sizeof(query), "DELETE FROM %s WHERE package_name=? and user_name=? ", CAPABILITY_TABLE);
	hstmt = _account_prepare_query(g_hAccountDB, query);
	count = 1;
	_account_query_bind_text(hstmt, count++, (char*)account->package_name);
	_account_query_bind_text(hstmt, count++, (char*)account->user_name);
	rc = _account_query_step(hstmt);
	if (rc != SQLITE_DONE) {
		ACCOUNT_ERROR("_account_query_step() failed(%d, %s)", rc, _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_DB_FAILED;
	}

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	GSList* iter;

	for (iter = account->capablity_list; iter != NULL; iter = g_slist_next(iter)) {
		int ret;
		count = 1;
		ACCOUNT_MEMSET(query, 0x00, sizeof(query));
		ACCOUNT_SNPRINTF(query, sizeof(query), "INSERT INTO %s(key, value, package_name, user_name, account_id) VALUES "
				"(?, ?, ?, ?, ?) ", CAPABILITY_TABLE);

		hstmt = _account_prepare_query(g_hAccountDB, query);

		ACCOUNT_RETURN_VAL((hstmt != NULL), {}, _ACCOUNT_ERROR_DB_FAILED, ("_account_prepare_query(g_hAccountDB, ) failed(%s).\n", _account_db_err_msg(g_hAccountDB)));

		account_capability_s* cap_data = NULL;
		cap_data = (account_capability_s*)iter->data;

		ret = _account_query_bind_text(hstmt, count++, cap_data->type);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));
		ret = _account_query_bind_int(hstmt, count++, cap_data->value);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Integer binding fail"));
		ret = _account_query_bind_text(hstmt, count++, (char*)account->package_name);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));
		ret = _account_query_bind_text(hstmt, count++, (char*)account->user_name);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));
		ret = _account_query_bind_int(hstmt, count++, (int)account->id);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Integer binding fail"));

		rc = _account_query_step(hstmt);

		if (rc != SQLITE_DONE) {
			ACCOUNT_ERROR("_account_query_step() failed(%d, %s)", rc, _account_db_err_msg(g_hAccountDB));
			break;
		}

		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;

	}

	return _ACCOUNT_ERROR_NONE;
}


bool _account_add_capability_to_account_cb(const char* capability_type, int capability_value, account_s *account)
{
	account_capability_s *cap_data = (account_capability_s*)malloc(sizeof(account_capability_s));

	if (cap_data == NULL)
		return FALSE;
	ACCOUNT_MEMSET(cap_data, 0, sizeof(account_capability_s));

	cap_data->type = _account_dup_text(capability_type);
	cap_data->value = capability_value;
	_INFO("cap_data->type = %s, cap_data->value = %d", cap_data->type, cap_data->value);

	account->capablity_list = g_slist_append(account->capablity_list, (gpointer)cap_data);

	return TRUE;
}


bool _account_add_custom_to_account_cb(const char* key, const char* value, account_s *account)
{
	account_custom_s *custom_data = (account_custom_s*)malloc(sizeof(account_custom_s));

	if (custom_data == NULL) {
		ACCOUNT_DEBUG("_account_add_custom_to_account_cb :: malloc fail\n");
		return FALSE;
	}
	ACCOUNT_MEMSET(custom_data, 0, sizeof(account_custom_s));

	custom_data->account_id = account->id;
	custom_data->app_id = _account_dup_text(account->package_name);
	custom_data->key = _account_dup_text(key);
	custom_data->value = _account_dup_text(value);
	_INFO("custom_data->key = %s, custom_data->value = %s", custom_data->key, custom_data->value);

	account->custom_list = g_slist_append(account->custom_list, (gpointer)custom_data);

	return TRUE;
}


static int _account_compare_old_record_by_user_name(account_s *new_account, const char* user_name, const char* package_name)
{
	int				error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0;
	account_s *old_account = NULL;

	ACCOUNT_RETURN_VAL((new_account != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("ACCOUNT IS NULL"));
	ACCOUNT_RETURN_VAL((user_name != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("USER NAME IS NULL"));
	ACCOUNT_RETURN_VAL((package_name != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("PACKAGE NAME IS NULL"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	old_account = (account_s*)calloc(1, sizeof(account_s));
	if (!old_account) {
		ACCOUNT_FATAL("Memory alloc fail\n");
		return _ACCOUNT_ERROR_OUT_OF_MEMORY;
	}

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE user_name = '%s' and package_name='%s'", ACCOUNT_TABLE, user_name, package_name);
	hstmt = _account_prepare_query(g_hAccountDB, query);

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR(rc == SQLITE_ROW, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	while (rc == SQLITE_ROW) {
		_account_convert_column_to_account(hstmt, old_account);
		rc = _account_query_step(hstmt);
	}

	rc = _account_query_finalize(hstmt);
	ACCOUNT_CATCH_ERROR((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	// get capability
	error_code = _account_query_capability_by_account_id(g_hAccountDB, _account_add_capability_to_account_cb, old_account->id, (void*)old_account);
	ACCOUNT_CATCH_ERROR((error_code == _ACCOUNT_ERROR_NONE), {}, error_code, ("account_query_capability_by_account_id error"));

	// get custom text
	error_code = _account_query_custom_by_account_id(g_hAccountDB, _account_add_custom_to_account_cb, old_account->id, (void*)old_account);
	ACCOUNT_CATCH_ERROR((error_code == _ACCOUNT_ERROR_NONE), {}, error_code, ("_account_query_custom_by_account_id error"));

	// compare
	new_account->id = old_account->id;

	//user name
	if (!new_account->user_name) {
		if (old_account->user_name)
			new_account->user_name = _account_dup_text(old_account->user_name);
	}

	// display name
	if (!new_account->display_name) {
		if (old_account->display_name)
			new_account->display_name = _account_dup_text(old_account->display_name);
	}

	// email address
	if (!new_account->email_address) {
		if (old_account->email_address)
			new_account->email_address = _account_dup_text(old_account->email_address);
	}

	// domain name
	if (!new_account->domain_name) {
		if (old_account->domain_name)
			new_account->domain_name = _account_dup_text(old_account->domain_name);
	}

	// icon path
	if (!new_account->icon_path) {
		if (old_account->icon_path)
			new_account->icon_path = _account_dup_text(old_account->icon_path);
	}

	// source
	if (!new_account->source) {
		if (old_account->source)
			new_account->source = _account_dup_text(old_account->source);
	}

	_ACCOUNT_FREE(new_account->package_name);
	new_account->package_name = _account_dup_text(old_account->package_name);

	// access token
	if (!new_account->access_token) {
		if (old_account->access_token)
			new_account->access_token = _account_dup_text(old_account->access_token);
	}

	// auth type
	if (new_account->auth_type == _ACCOUNT_AUTH_TYPE_INVALID)
		new_account->auth_type = old_account->auth_type;

	//secret
	if (new_account->secret == _ACCOUNT_SECRECY_INVALID)
		new_account->secret = old_account->secret;

	// sync support
	if (new_account->sync_support == _ACCOUNT_SYNC_INVALID)
		new_account->sync_support = old_account->sync_support;

	// TODO user text
	int i;
	for (i = 0; i < USER_TXT_CNT; i++) {
		if (!new_account->user_data_txt[i]) {
			if (old_account->user_data_txt[i])
				new_account->user_data_txt[i] = _account_dup_text(old_account->user_data_txt[i]);
		}
	}

	// TODO user int
	for (i = 0; i < USER_INT_CNT; i++) {
		if (new_account->user_data_int[i] == 0)
				new_account->user_data_int[i] = old_account->user_data_int[i];
	}

CATCH:
	if (old_account)
		_account_free_account_with_items(old_account);

	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;
	}

	return _ACCOUNT_ERROR_NONE;
}

static int _account_update_account_by_user_name(int pid, uid_t uid, account_s *account, const char *user_name, const char *package_name)
{
	int				rc = 0, binding_count = 0, count = 0;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;

	ACCOUNT_RETURN_VAL((user_name != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("user_name is NULL.\n"));
	ACCOUNT_RETURN_VAL((package_name != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("package_name is NULL.\n"));

	char* current_appid = NULL;
	char* verified_appid = NULL;

	current_appid = _account_get_current_appid(pid, uid);
	error_code = _account_get_represented_appid_from_db(g_hAccountDB, g_hAccountGlobalDB, current_appid, uid, &verified_appid);

	_ACCOUNT_FREE(current_appid);
	_ACCOUNT_FREE(verified_appid);

	if (error_code != _ACCOUNT_ERROR_NONE) {
		ACCOUNT_ERROR("No permission to update\n");
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	error_code = encrypt_access_token(account);
	if (error_code != _ACCOUNT_ERROR_NONE) {
		_ERR("_encrypt_access_token error");
		return error_code;
	}

	_account_compare_old_record_by_user_name(account, user_name, package_name);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	if (!account->package_name) {
		ACCOUNT_ERROR("Package name is mandetory field, it can not be NULL!!!!\n");
		return _ACCOUNT_ERROR_INVALID_PARAMETER;
	}

	if (!account->user_name && !account->display_name && !account->email_address) {
		ACCOUNT_ERROR("One field should be set among user name, display name, email address\n");
		return _ACCOUNT_ERROR_INVALID_PARAMETER;
	}

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT COUNT(*) FROM %s WHERE user_name='%s' and package_name='%s'"
			, ACCOUNT_TABLE, user_name, package_name);

	count = _account_get_record_count(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	if (count <= 0) {
		ACCOUNT_SLOGI("_account_update_account_by_user_name : The account not exist!, count = %d, user_name=%s, package_name=%s\n",
			count, user_name, package_name);
		return _ACCOUNT_ERROR_RECORD_NOT_FOUND;
	}

	ACCOUNT_MEMSET(query, 0x00, sizeof(query));

	//TODO: Is it required to update id ? As of now I can only think of falied rollback cases (between account and gSSO DB)
	ACCOUNT_SNPRINTF(query, sizeof(query), "UPDATE %s SET user_name=?, email_address =?, display_name =?, "
			"icon_path =?, source =?, package_name =? , access_token =?, domain_name =?, auth_type =?, secret =?, sync_support =?,"
			"txt_custom0=?, txt_custom1=?, txt_custom2=?, txt_custom3=?, txt_custom4=?, "
			"int_custom0=?, int_custom1=?, int_custom2=?, int_custom3=?, int_custom4=? WHERE user_name=? and package_name=? ", ACCOUNT_TABLE);

	hstmt = _account_prepare_query(g_hAccountDB, query);
	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		_account_end_transaction(g_hAccountDB, FALSE);
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}
	ACCOUNT_RETURN_VAL((hstmt != NULL), {}, _ACCOUNT_ERROR_DB_FAILED, ("_account_svc_query_prepare() failed(%s).\n", _account_db_err_msg(g_hAccountDB)));

	binding_count = _account_convert_account_to_sql(account, hstmt, query);

	_account_query_bind_text(hstmt, binding_count++, user_name);
	_account_query_bind_text(hstmt, binding_count++, package_name);

	rc = _account_query_step(hstmt);
	if (rc != SQLITE_DONE)
		ACCOUNT_ERROR("account_db_query_step() failed(%d, %s)", rc, _account_db_err_msg(g_hAccountDB));

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	/*update capability*/
	error_code = _account_update_capability_by_user_name(account, user_name, package_name);

	/* update custom */
	error_code = _account_update_custom(account, account->id);

	return error_code;
}

int _account_insert_to_db(account_s* account, int pid, uid_t uid, int *account_id)
{
	_INFO("");
	int		error_code = _ACCOUNT_ERROR_NONE;
	int		ret_transaction = 0;

	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));
	ACCOUNT_RETURN_VAL((account != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("ACCOUNT HANDLE IS NULL"));
	ACCOUNT_RETURN_VAL((account_id != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("ACCOUNT ID POINTER IS NULL"));

	if (!account->user_name && !account->display_name && !account->email_address) {
		ACCOUNT_ERROR("One field should be set among user name, display name, email address\n");
		return _ACCOUNT_ERROR_INVALID_PARAMETER;
	}

	account_s *data = (account_s*)account;
	ACCOUNT_SLOGD("(%s)-(%d) account_insert_to_db: begin_transaction.\n", __FUNCTION__, __LINE__);

	pthread_mutex_lock(&account_mutex);

	/* transaction control required*/
	ret_transaction = _account_begin_transaction(g_hAccountDB);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		pthread_mutex_unlock(&account_mutex);
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	if (ret_transaction == _ACCOUNT_ERROR_DATABASE_BUSY) {
		ACCOUNT_ERROR("account insert:_account_begin_transaction fail %d\n", ret_transaction);
		pthread_mutex_unlock(&account_mutex);
		return _ACCOUNT_ERROR_DATABASE_BUSY;
	} else if (ret_transaction != _ACCOUNT_ERROR_NONE) {
		ACCOUNT_ERROR("account insert:_account_begin_transaction fail %d\n", ret_transaction);
		pthread_mutex_unlock(&account_mutex);
		return ret_transaction;
	}

	*account_id = _account_get_next_sequence(g_hAccountDB, ACCOUNT_TABLE);
	data->id = *account_id;

	char* appid = NULL;
	appid = _account_get_current_appid(pid, uid);

	if (!appid) {
		_INFO("");
		// API caller cannot be recognized
		ret_transaction = _account_end_transaction(g_hAccountDB, FALSE);
		ACCOUNT_ERROR("App id is not registered in account type DB, transaction ret (%x)!!!!\n", ret_transaction);
		pthread_mutex_unlock(&account_mutex);
		return _ACCOUNT_ERROR_NOT_REGISTERED_PROVIDER;
	}

	_INFO("");
	char* verified_appid = NULL;
	error_code  = _account_get_represented_appid_from_db(g_hAccountDB, g_hAccountGlobalDB, appid, uid, &verified_appid);//FIX
	_ACCOUNT_FREE(appid);
	if (error_code != _ACCOUNT_ERROR_NONE) {
		_ERR("error_code = %d", error_code);
		ret_transaction = _account_end_transaction(g_hAccountDB, FALSE);
		ACCOUNT_ERROR("App id is not registered in account type DB, transaction ret (%x)!!!!\n", ret_transaction);
		_ACCOUNT_FREE(verified_appid);
		pthread_mutex_unlock(&account_mutex);
		return error_code;
	}

	if (verified_appid) {
		_INFO("");
		error_code = _account_check_duplicated(g_hAccountDB, data, verified_appid, uid);
		if (error_code != _ACCOUNT_ERROR_NONE) {
			_INFO("");
			ret_transaction = _account_end_transaction(g_hAccountDB, FALSE);
			ACCOUNT_DEBUG("_account_check_duplicated(), rollback insert query(%x)!!!!\n", ret_transaction);
			*account_id = -1;
			pthread_mutex_unlock(&account_mutex);
			return error_code;
		}
		if (!_account_check_add_more_account(verified_appid)) {
			ret_transaction = _account_end_transaction(g_hAccountDB, FALSE);
			ACCOUNT_ERROR("No more account cannot be added, transaction ret (%x)!!!!\n", ret_transaction);
			pthread_mutex_unlock(&account_mutex);
			_ACCOUNT_FREE(verified_appid);
			return _ACCOUNT_ERROR_NOT_ALLOW_MULTIPLE;
		}

		_ACCOUNT_FREE(data->package_name);
		data->package_name = _account_dup_text(verified_appid);
		_ACCOUNT_FREE(verified_appid);
	}

	if (!_account_check_add_more_account(data->package_name)) {
		_INFO("");
		ret_transaction = _account_end_transaction(g_hAccountDB, FALSE);
		ACCOUNT_ERROR("No more account cannot be added, transaction ret (%x)!!!!\n", ret_transaction);
		pthread_mutex_unlock(&account_mutex);
		return _ACCOUNT_ERROR_NOT_ALLOW_MULTIPLE;
	}

	error_code = encrypt_access_token(data);
	if (error_code != _ACCOUNT_ERROR_NONE) {
		ret_transaction = _account_end_transaction(g_hAccountDB, FALSE);
		ACCOUNT_ERROR("encrypt_access_token fail, rollback insert query(%x)!!!!\n", ret_transaction);
		*account_id = -1;
		pthread_mutex_unlock(&account_mutex);
		return error_code;
	}

	error_code = _account_execute_insert_query(data);

	if (error_code != _ACCOUNT_ERROR_NONE) {
		_INFO("");
		ret_transaction = _account_end_transaction(g_hAccountDB, FALSE);
		ACCOUNT_ERROR("INSERT account fail, rollback insert query(%x)!!!!\n", ret_transaction);
		*account_id = -1;
		pthread_mutex_unlock(&account_mutex);
		return error_code;
	}

	_INFO("");
	error_code = _account_insert_capability(data, *account_id);
	if (error_code != _ACCOUNT_ERROR_NONE) {
		_INFO("");
		ret_transaction = _account_end_transaction(g_hAccountDB, FALSE);
		ACCOUNT_ERROR("INSERT capability fail, rollback insert capability query(%x)!!!!\n", ret_transaction);
		*account_id = -1;
		pthread_mutex_unlock(&account_mutex);
		return error_code;
	}

	_INFO("");
	error_code = _account_insert_custom(data, *account_id);
	if (error_code != _ACCOUNT_ERROR_NONE) {
		ret_transaction = _account_end_transaction(g_hAccountDB, FALSE);
		ACCOUNT_ERROR("INSERT custom fail, rollback insert capability query(%x)!!!!\n", ret_transaction);
		*account_id = -1;
		pthread_mutex_unlock(&account_mutex);
		return error_code;
	}

	_INFO("");

	pthread_mutex_unlock(&account_mutex);
	_account_end_transaction(g_hAccountDB, TRUE);
	ACCOUNT_SLOGD("(%s)-(%d) account _end_transaction.\n", __FUNCTION__, __LINE__);

	char buf[64] = {0,};
	ACCOUNT_SNPRINTF(buf, sizeof(buf), "%s:%d", _ACCOUNT_NOTI_NAME_INSERT, *account_id);
	_account_insert_delete_update_notification_send(buf);
	_INFO("account _notification_send end.");

	return _ACCOUNT_ERROR_NONE;
}

GSList* _account_get_capability_list_by_account_id(int account_id, int *error_code)
{
	*error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0;
	GSList* capability_list = NULL;

	ACCOUNT_RETURN_VAL((account_id > 0), { *error_code = _ACCOUNT_ERROR_INVALID_PARAMETER; }, NULL, ("ACCOUNT INDEX IS LESS THAN 0"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), { *error_code = _ACCOUNT_ERROR_DB_NOT_OPENED; }, NULL, ("The database isn't connected."));

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE account_id = %d", CAPABILITY_TABLE, account_id);
	hstmt = _account_prepare_query(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		*error_code = _ACCOUNT_ERROR_PERMISSION_DENIED;
		return NULL;
	}

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR_P(rc == SQLITE_ROW, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	account_capability_s* capability_record = NULL;

	while (rc == SQLITE_ROW) {
		capability_record = (account_capability_s*) malloc(sizeof(account_capability_s));

		if (capability_record == NULL) {
			ACCOUNT_FATAL("malloc Failed");
			break;
		}

		ACCOUNT_MEMSET(capability_record, 0x00, sizeof(account_capability_s));

		_account_convert_column_to_capability(hstmt, capability_record);

		//cb_ret = callback(capability_record->type, capability_record->value, user_data);

		//_account_free_capability_items(capability_record);
		//_ACCOUNT_FREE(capability_record);

		//ACCOUNT_CATCH_ERROR(cb_ret == TRUE, {}, _ACCOUNT_ERROR_NONE, ("Callback func returs FALSE, its iteration is stopped!!!!\n"));

		capability_list = g_slist_append(capability_list, capability_record);
		rc = _account_query_step(hstmt);
	}

	rc = _account_query_finalize(hstmt);
	if (rc != _ACCOUNT_ERROR_NONE) {
		_ERR("finalize error");
		_account_gslist_capability_free(capability_list);
		*error_code = rc;
		return NULL;
	}

	hstmt = NULL;

	*error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), { *error_code = rc; }, NULL, ("finalize error"));
		hstmt = NULL;
	}

	pthread_mutex_unlock(&account_mutex);

	return capability_list;
}

static int _account_compare_old_record(account_s *new_account, int account_id)
{
	int				error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0;
	account_s *old_account = NULL;

	ACCOUNT_RETURN_VAL((account_id > 0), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("ACCOUNT INDEX IS LESS THAN 0"));
	ACCOUNT_RETURN_VAL((new_account != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("ACCOUNT IS NULL"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	old_account = (account_s*)calloc(1, sizeof(account_s));
	if (old_account == NULL) {
		_ERR("Out of Memory");
		return _ACCOUNT_ERROR_OUT_OF_MEMORY;
	}

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE _id = %d", ACCOUNT_TABLE, account_id);
	hstmt = _account_prepare_query(g_hAccountDB, query);

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR(rc == SQLITE_ROW, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	while (rc == SQLITE_ROW) {
		_account_convert_column_to_account(hstmt, old_account);
		rc = _account_query_step(hstmt);
	}

	rc = _account_query_finalize(hstmt);
	ACCOUNT_CATCH_ERROR((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	// get capability
	error_code = _account_query_capability_by_account_id(g_hAccountDB, _account_add_capability_to_account_cb, old_account->id, (void*)old_account);
	ACCOUNT_CATCH_ERROR((error_code == _ACCOUNT_ERROR_NONE), {}, error_code, ("account_query_capability_by_account_id error"));

	// get custom text
	error_code = _account_query_custom_by_account_id(g_hAccountDB, _account_add_custom_to_account_cb, old_account->id, (void*)old_account);
	ACCOUNT_CATCH_ERROR((error_code == _ACCOUNT_ERROR_NONE), {}, error_code, ("_account_query_custom_by_account_id error"));

	// compare

	new_account->id = old_account->id;

	//user name
	if (!new_account->user_name) {
		if (old_account->user_name)
			new_account->user_name = _account_dup_text(old_account->user_name);
	}

	// display name
	if (!new_account->display_name) {
		if (old_account->display_name)
			new_account->display_name = _account_dup_text(old_account->display_name);
	}

	// email address
	if (!new_account->email_address) {
		if (old_account->email_address)
			new_account->email_address = _account_dup_text(old_account->email_address);
	}

	// domain name
	if (!new_account->domain_name) {
		if (old_account->domain_name)
			new_account->domain_name = _account_dup_text(old_account->domain_name);
	}

	// icon path
	if (!new_account->icon_path) {
		if (old_account->icon_path)
			new_account->icon_path = _account_dup_text(old_account->icon_path);
	}

	// source
	if (!new_account->source) {
		if (old_account->source)
			new_account->source = _account_dup_text(old_account->source);
	}

	_ACCOUNT_FREE(new_account->package_name);
	new_account->package_name = _account_dup_text(old_account->package_name);

	// access token
	if (!new_account->access_token) {
		if (old_account->access_token)
			new_account->access_token = _account_dup_text(old_account->access_token);
	}

	// user text
	int i;
	for (i = 0; i < USER_TXT_CNT; i++) {
		if (!new_account->user_data_txt[i]) {
			if (old_account->user_data_txt[i])
				new_account->user_data_txt[i] = _account_dup_text(old_account->user_data_txt[i]);
		}
	}

	// auth type
	if (new_account->auth_type == _ACCOUNT_AUTH_TYPE_INVALID)
		new_account->auth_type = old_account->auth_type;

	// secret
	if (new_account->secret == _ACCOUNT_SECRECY_INVALID)
		new_account->secret = old_account->secret;

	// sync support
	if (new_account->sync_support == _ACCOUNT_SYNC_INVALID)
		new_account->sync_support = old_account->sync_support;

	// user int
	for (i = 0; i < USER_INT_CNT; i++) {
		if (new_account->user_data_int[i] == 0)
				new_account->user_data_int[i] = old_account->user_data_int[i];
	}

CATCH:
	if (old_account)
		_account_free_account_with_items(old_account);

	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;
	}

	return _ACCOUNT_ERROR_NONE;
}

static int _account_get_package_name_from_account_id(int account_id, char **package_name)
{
	int				error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0;
	account_s *old_account = NULL;

	ACCOUNT_RETURN_VAL((account_id > 0), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("ACCOUNT INDEX IS LESS THAN 0"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	old_account = (account_s*)calloc(1, sizeof(account_s));
	if (old_account == NULL) {
		_ERR("Out Of memory");
		return _ACCOUNT_ERROR_OUT_OF_MEMORY;
	}

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE _id = %d", ACCOUNT_TABLE, account_id);
	hstmt = _account_prepare_query(g_hAccountDB, query);

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR(rc == SQLITE_ROW, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	while (rc == SQLITE_ROW) {
		_account_convert_column_to_account(hstmt, old_account);
		rc = _account_query_step(hstmt);
	}

	rc = _account_query_finalize(hstmt);
	ACCOUNT_CATCH_ERROR((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	// get package name.
	*package_name = _account_dup_text(old_account->package_name);

CATCH:
	if (old_account)
		_account_free_account_with_items(old_account);

	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;
	}

	return error_code;

}

static int _account_update_account(int pid, uid_t uid, account_s *account, int account_id)
{
	int				rc = 0, binding_count = 0;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				error_code = _ACCOUNT_ERROR_NONE, count = 0, ret_transaction = 0;
	account_stmt	hstmt = NULL;

	if (!account->package_name) {
		ACCOUNT_ERROR("Package name is mandetory field, it can not be NULL!!!!\n");
		return _ACCOUNT_ERROR_INVALID_PARAMETER;
	}

	/* Check permission of requested appid */
	char* current_appid = NULL;
	char *package_name = NULL;

	current_appid = _account_get_current_appid(pid, uid);
	error_code = _account_get_package_name_from_account_id(account_id, &package_name);

	if (error_code != _ACCOUNT_ERROR_NONE || package_name == NULL) {
		ACCOUNT_ERROR("No package name with account_id\n");
		_ACCOUNT_FREE(current_appid);
		_ACCOUNT_FREE(package_name);
		return _ACCOUNT_ERROR_RECORD_NOT_FOUND;
	}

	error_code = _account_check_appid_group_with_package_name(current_appid, package_name, uid);
	ACCOUNT_DEBUG("UPDATE:account_id[%d],current_appid[%s]package_name[%s]", account_id, current_appid, package_name);	// TODO: remove the log later.

	_ACCOUNT_FREE(current_appid);
	_ACCOUNT_FREE(package_name);

	if (error_code != _ACCOUNT_ERROR_NONE) {
		ACCOUNT_ERROR("No permission to update\n");
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	error_code = encrypt_access_token(account);
	if (error_code != _ACCOUNT_ERROR_NONE) {
		_ERR("_encrypt_access_token error");
		return error_code;
	}

	error_code = _account_compare_old_record(account, account_id);
	if (error_code != _ACCOUNT_ERROR_NONE) {
		ACCOUNT_ERROR("_account_compare_old_record fail\n");
		return error_code;
	}

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	} else if (_account_db_err_code(g_hAccountDB) == SQLITE_BUSY) {
		ACCOUNT_ERROR("database busy(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_DATABASE_BUSY;
	}

	if (!account->user_name && !account->display_name && !account->email_address) {
		ACCOUNT_ERROR("One field should be set among user name, display name, email address\n");
		return _ACCOUNT_ERROR_INVALID_PARAMETER;
	}

	ACCOUNT_MEMSET(query, 0x00, sizeof(query));

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT COUNT(*) FROM %s WHERE _id = %d ", ACCOUNT_TABLE, account_id);

	count = _account_get_record_count(g_hAccountDB, query);
	if (count <= 0) {
		ACCOUNT_DEBUG(" Account record not found, count = %d\n", count);
		return _ACCOUNT_ERROR_RECORD_NOT_FOUND;
	}

	/* transaction control required*/
	ret_transaction = _account_begin_transaction(g_hAccountDB);
	if (ret_transaction == _ACCOUNT_ERROR_DATABASE_BUSY) {
		ACCOUNT_ERROR("database busy(%s)", _account_db_err_msg(g_hAccountDB));
		pthread_mutex_unlock(&account_mutex);
		return _ACCOUNT_ERROR_DATABASE_BUSY;
	}

	ACCOUNT_MEMSET(query, 0x00, sizeof(query));
	ACCOUNT_SNPRINTF(query, sizeof(query), "UPDATE %s SET user_name=?, email_address =?, display_name =?, "
			"icon_path =?, source =?, package_name =? , access_token =?, domain_name =?, auth_type =?, secret =?, sync_support =?,"
			"txt_custom0=?, txt_custom1=?, txt_custom2=?, txt_custom3=?, txt_custom4=?, "
			"int_custom0=?, int_custom1=?, int_custom2=?, int_custom3=?, int_custom4=? WHERE _id=? ", ACCOUNT_TABLE);

	hstmt = _account_prepare_query(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ret_transaction = _account_end_transaction(g_hAccountDB, FALSE);
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	ACCOUNT_RETURN_VAL((hstmt != NULL), {}, _ACCOUNT_ERROR_DB_FAILED, ("_account_svc_query_prepare() failed(%s)(%x).\n", _account_db_err_msg(g_hAccountDB), _account_end_transaction(g_hAccountDB, FALSE)));

	binding_count = _account_convert_account_to_sql(account, hstmt, query);
	_account_query_bind_int(hstmt, binding_count++, account_id);

	rc = _account_query_step(hstmt);
	if (rc != SQLITE_DONE)
		ACCOUNT_SLOGE("account_db_query_step() failed(%d, %s)", rc, _account_db_err_msg(g_hAccountDB));

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	_INFO("update query=%s", query);

	/*update capability*/
	error_code = _account_update_capability(account, account_id);
	if (error_code != _ACCOUNT_ERROR_NONE && error_code != _ACCOUNT_ERROR_RECORD_NOT_FOUND) {
		ret_transaction = _account_end_transaction(g_hAccountDB, FALSE);
		ACCOUNT_ERROR("update capability Failed, trying to roll back(%x) !!!\n", ret_transaction);
		return error_code;
	}

	/* update custom */
	error_code = _account_update_custom(account, account_id);
	if (error_code != _ACCOUNT_ERROR_NONE && error_code != _ACCOUNT_ERROR_RECORD_NOT_FOUND) {
		ret_transaction = _account_end_transaction(g_hAccountDB, FALSE);
		ACCOUNT_ERROR("update capability Failed, trying to roll back(%x) !!!\n", ret_transaction);
		return error_code;
	}

	ret_transaction = _account_end_transaction(g_hAccountDB, TRUE);

	_INFO("update end");

	return error_code;
}


static int _account_update_account_ex(account_s *account, int account_id)
{
	int				rc = 0, binding_count = 0;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				error_code = _ACCOUNT_ERROR_NONE, count = 0, ret_transaction = 0;
	account_stmt	hstmt = NULL;

	if (!account->package_name) {
		ACCOUNT_ERROR("Package name is mandetory field, it can not be NULL!!!!\n");
		return _ACCOUNT_ERROR_INVALID_PARAMETER;
	}

	error_code = encrypt_access_token(account);
	if (error_code != _ACCOUNT_ERROR_NONE) {
		_ERR("_encrypt_access_token error");
		return error_code;
	}

	error_code = _account_compare_old_record(account, account_id);
	if (error_code != _ACCOUNT_ERROR_NONE) {
		ACCOUNT_ERROR("_account_compare_old_record fail\n");
		return error_code;
	}

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	if (!account->user_name && !account->display_name && !account->email_address) {
		ACCOUNT_ERROR("One field should be set among user name, display name, email address\n");
		return _ACCOUNT_ERROR_INVALID_PARAMETER;
	}

	ACCOUNT_MEMSET(query, 0x00, sizeof(query));

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT COUNT(*) FROM %s WHERE _id = %d ", ACCOUNT_TABLE, account_id);

	count = _account_get_record_count(g_hAccountDB, query);
	if (count <= 0) {
		ACCOUNT_DEBUG(" Account record not found, count = %d\n", count);
		return _ACCOUNT_ERROR_RECORD_NOT_FOUND;
	}

	/* transaction control required*/
	ret_transaction = _account_begin_transaction(g_hAccountDB);
	if (ret_transaction == _ACCOUNT_ERROR_DATABASE_BUSY) {
		ACCOUNT_ERROR("database busy(%s)", _account_db_err_msg(g_hAccountDB));
		pthread_mutex_unlock(&account_mutex);
		return _ACCOUNT_ERROR_DATABASE_BUSY;
	}

	ACCOUNT_MEMSET(query, 0x00, sizeof(query));
	ACCOUNT_SNPRINTF(query, sizeof(query), "UPDATE %s SET user_name=?, email_address =?, display_name =?, "
			"icon_path =?, source =?, package_name =? , access_token =?, domain_name =?, auth_type =?, secret =?, sync_support =?,"
			"txt_custom0=?, txt_custom1=?, txt_custom2=?, txt_custom3=?, txt_custom4=?, "
			"int_custom0=?, int_custom1=?, int_custom2=?, int_custom3=?, int_custom4=? WHERE _id=? ", ACCOUNT_TABLE);

	hstmt = _account_prepare_query(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ret_transaction = _account_end_transaction(g_hAccountDB, FALSE);
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	ACCOUNT_RETURN_VAL((hstmt != NULL), {}, _ACCOUNT_ERROR_DB_FAILED, ("_account_svc_query_prepare() failed(%s)(%x).\n", _account_db_err_msg(g_hAccountDB), _account_end_transaction(g_hAccountDB, FALSE)));

	_INFO("account_update_to_db_by_id_ex_p : before convert() : account_id[%d], user_name=%s", account->id, account->user_name);
	binding_count = _account_convert_account_to_sql(account, hstmt, query);
	_INFO("account_update_to_db_by_id_ex_p : after convert() : account_id[%d], user_name=%s", account->id, account->user_name);
	_INFO("account_update_to_db_by_id_ex_p : before bind()");
	rc = _account_query_bind_int(hstmt, binding_count++, account_id);
	_INFO("account_update_to_db_by_id_ex_p : after bind() : ret = %d", rc);

	rc = _account_query_step(hstmt);
	if (rc != SQLITE_DONE)
		ACCOUNT_SLOGE("account_db_query_step() failed(%d, %s)", rc, _account_db_err_msg(g_hAccountDB));
	_INFO("account_update_to_db_by_id_ex_p : after query_step() : ret = %d", rc);

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;
	_INFO("account_update_to_db_by_id_ex_p : after query_filnalize() : ret = %d", rc);

	_INFO("account_update_to_db_by_id_ex_p : before update_capability()");
	/*update capability*/
	error_code = _account_update_capability(account, account_id);
	if (error_code != _ACCOUNT_ERROR_NONE && error_code != _ACCOUNT_ERROR_RECORD_NOT_FOUND) {
		ret_transaction = _account_end_transaction(g_hAccountDB, FALSE);
		ACCOUNT_ERROR("update capability Failed, trying to roll back(%x) !!!\n", ret_transaction);
		return error_code;
	}
	_INFO("account_update_to_db_by_id_ex_p : after update_capability()");

	_INFO("account_update_to_db_by_id_ex_p : before update_custom()");
	/* update custom */
	error_code = _account_update_custom(account, account_id);
	if (error_code != _ACCOUNT_ERROR_NONE && error_code != _ACCOUNT_ERROR_RECORD_NOT_FOUND) {
		ret_transaction = _account_end_transaction(g_hAccountDB, FALSE);
		ACCOUNT_ERROR("update capability Failed, trying to roll back(%x) !!!\n", ret_transaction);
		return error_code;
	}
	_INFO("account_update_to_db_by_id_ex_p : after update_custom()");

	ret_transaction = _account_end_transaction(g_hAccountDB, TRUE);

	return error_code;
}


int _account_update_to_db_by_id(int pid, uid_t uid, account_s* account, int account_id)
{
	ACCOUNT_RETURN_VAL((account != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("DATA IS NULL"));
	ACCOUNT_RETURN_VAL((account_id > 0), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Account id is not valid"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));
	int	error_code = _ACCOUNT_ERROR_NONE;
	account_s* data = (account_s*)account;

	pthread_mutex_lock(&account_mutex);

	error_code = _account_update_account(pid, uid, data, account_id);

	if (error_code != _ACCOUNT_ERROR_NONE) {
		pthread_mutex_unlock(&account_mutex);
		return error_code;
	}

	pthread_mutex_unlock(&account_mutex);

	char buf[64] = {0,};
	ACCOUNT_SNPRINTF(buf, sizeof(buf), "%s:%d", _ACCOUNT_NOTI_NAME_UPDATE, account_id);
	_account_insert_delete_update_notification_send(buf);

	return _ACCOUNT_ERROR_NONE;
}

int _account_update_to_db_by_id_ex(account_s* account, int account_id)
{
	ACCOUNT_RETURN_VAL((account != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("DATA IS NULL"));
	ACCOUNT_RETURN_VAL((account_id > 0), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Account id is not valid"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));
	int	error_code = _ACCOUNT_ERROR_NONE;
	account_s* data = account;

	pthread_mutex_lock(&account_mutex);

	_INFO("before update_account_ex() : account_id[%d], user_name=%s", account_id, data->user_name);
	error_code = _account_update_account_ex(data, account_id);
	_INFO("after update_account_ex() : account_id[%d], user_name=%s", account_id, data->user_name);

	if (error_code != _ACCOUNT_ERROR_NONE) {
		pthread_mutex_unlock(&account_mutex);
		return error_code;
	}

	pthread_mutex_unlock(&account_mutex);

	char buf[64] = {0,};
	ACCOUNT_SNPRINTF(buf, sizeof(buf), "%s:%d", _ACCOUNT_NOTI_NAME_UPDATE, account_id);
	_account_insert_delete_update_notification_send(buf);

	return _ACCOUNT_ERROR_NONE;
}

int _account_update_to_db_by_user_name(int pid, uid_t uid, account_s* account, const char *user_name, const char *package_name)
{
	ACCOUNT_RETURN_VAL((user_name != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("USER NAME IS NULL"));
	ACCOUNT_RETURN_VAL((package_name != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("PACKAGE NAME IS NULL"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	int	error_code = _ACCOUNT_ERROR_NONE;
	account_s *data = (account_s*)account;

	pthread_mutex_lock(&account_mutex);

	error_code = _account_update_account_by_user_name(pid, uid, data, user_name, package_name);

	pthread_mutex_unlock(&account_mutex);

	char buf[64] = {0,};
	ACCOUNT_SNPRINTF(buf, sizeof(buf), "%s:%d", _ACCOUNT_NOTI_NAME_UPDATE, data->id);
	_account_insert_delete_update_notification_send(buf);

	return error_code;
}

GSList* _account_db_query_all(int pid, uid_t uid)
{
	//int			error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0;
	GSList			*account_list = NULL;

	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, NULL, ("The database isn't connected."));

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s ", ACCOUNT_TABLE);
	hstmt = _account_prepare_query(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return NULL;
	}

	rc = _account_query_step(hstmt);

	account_s *account_record = NULL;

	if (rc != SQLITE_ROW) {
		_ERR("The record isn't found");
		goto CATCH;
	}

	while (rc == SQLITE_ROW) {
		account_record = (account_s*) malloc(sizeof(account_s));

		if (account_record == NULL) {
			ACCOUNT_FATAL("malloc Failed");
			break;
		}

		ACCOUNT_MEMSET(account_record, 0x00, sizeof(account_s));
		_account_convert_column_to_account(hstmt, account_record);
		account_list = g_slist_append(account_list, account_record);
		rc = _account_query_step(hstmt);
	}

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {_account_gslist_account_free(account_list); }, NULL, ("finalize error"));
	hstmt = NULL;

	GSList* iter;

	for (iter = account_list; iter != NULL; iter = g_slist_next(iter)) {
		account_s *account = NULL;
		account = (account_s*)iter->data;
		_account_query_capability_by_account_id(g_hAccountDB, _account_add_capability_to_account_cb, account->id, (void*)account);
		_account_query_custom_by_account_id(g_hAccountDB, _account_add_custom_to_account_cb, account->id, (void*)account);
	}

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {_account_gslist_account_free(account_list); }, NULL, ("finalize error"));
		hstmt = NULL;
	}

	if (account_list)
		_remove_sensitive_info_from_non_owning_account_slist(account_list, pid, uid);

	return account_list;
}

int _account_update_sync_status_by_id(uid_t uid, int account_db_id, const int sync_status)
{
	int				error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0;
	int count = 1;

	ACCOUNT_RETURN_VAL((account_db_id > 0), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("ACCOUNT INDEX IS LESS THAN 0"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));
	if ((sync_status < 0) || (sync_status >= _ACCOUNT_SYNC_MAX)) {
		ACCOUNT_SLOGE("(%s)-(%d) sync_status is less than 0 or more than enum max.\n", __FUNCTION__, __LINE__);
		return _ACCOUNT_ERROR_INVALID_PARAMETER;
	}

	pthread_mutex_lock(&account_mutex);

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT COUNT(*) from %s where _id=%d", ACCOUNT_TABLE, account_db_id);

	rc = _account_get_record_count(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		pthread_mutex_unlock(&account_mutex);
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	if (rc <= 0) {
		ACCOUNT_SLOGE("account_update_sync_status_by_id : related account item is not existed rc=%d , %s", rc, _account_db_err_msg(g_hAccountDB));
		pthread_mutex_unlock(&account_mutex);
		return _ACCOUNT_ERROR_RECORD_NOT_FOUND;
	}

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "UPDATE %s SET sync_support=? WHERE _id = %d", ACCOUNT_TABLE, account_db_id);
	hstmt = _account_prepare_query(g_hAccountDB, query);

	_account_query_bind_int(hstmt, count, sync_status);

	rc = _account_query_step(hstmt);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		pthread_mutex_unlock(&account_mutex);
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	ACCOUNT_CATCH_ERROR(rc == SQLITE_DONE, {}, _ACCOUNT_ERROR_DB_FAILED,
				("account_db_query_step() failed(%d, %s)", rc, _account_db_err_msg(g_hAccountDB)));

	rc = _account_query_finalize(hstmt);
	if (rc != _ACCOUNT_ERROR_NONE) {
		ACCOUNT_ERROR("_account_query_finalize error");
		pthread_mutex_unlock(&account_mutex);
		return rc;
	}

	char buf[64] = {0,};
	ACCOUNT_SNPRINTF(buf, sizeof(buf), "%s:%d", _ACCOUNT_NOTI_NAME_SYNC_UPDATE, account_db_id);
	_account_insert_delete_update_notification_send(buf);

	hstmt = NULL;
	error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		pthread_mutex_unlock(&account_mutex);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;
	}

	pthread_mutex_unlock(&account_mutex);

	return error_code;
}

int _account_query_account_by_account_id(int pid, uid_t uid, int account_db_id, account_s *account_record)
{
	_INFO("_account_query_account_by_account_id() start, account_db_id=[%d]", account_db_id);

	int				error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0;

	ACCOUNT_RETURN_VAL((account_db_id > 0), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("ACCOUNT INDEX IS LESS THAN 0"));
	ACCOUNT_RETURN_VAL(account_record != NULL, {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("ACCOUNT IS NULL"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_DEBUG("starting db operations");

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE _id = %d", ACCOUNT_TABLE, account_db_id);
	hstmt = _account_prepare_query(g_hAccountDB, query);
	rc = _account_db_err_code(g_hAccountDB);
	_INFO("after _account_prepare_query, rc=[%d]", rc);

	if (rc == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	ACCOUNT_DEBUG("before _account_query_step");
	rc = _account_query_step(hstmt);
	ACCOUNT_DEBUG("after _account_query_step returned [%d]", rc);
	ACCOUNT_CATCH_ERROR(rc == SQLITE_ROW, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	while (rc == SQLITE_ROW) {
		ACCOUNT_DEBUG("before _account_convert_column_to_account");
		_account_convert_column_to_account(hstmt, account_record);
		ACCOUNT_DEBUG("after _account_convert_column_to_account");
		ACCOUNT_DEBUG("user_name = %s, user_txt[0] = %s, user_int[1] = %d", account_record->user_name, account_record->user_data_txt[0], account_record->user_data_int[1]);
		rc = _account_query_step(hstmt);
	}

	ACCOUNT_DEBUG("account_record->id=[%d]", account_record->id);

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));

	ACCOUNT_DEBUG("before _account_query_capability_by_account_id");
	_account_query_capability_by_account_id(g_hAccountDB, _account_add_capability_to_account_cb, account_record->id, (void*)account_record);
	ACCOUNT_DEBUG("after _account_query_capability_by_account_id");

	ACCOUNT_DEBUG("before _account_query_custom_by_account_id");
	_account_query_custom_by_account_id(g_hAccountDB, _account_add_custom_to_account_cb, account_record->id, (void*)account_record);
	ACCOUNT_DEBUG("after _account_query_custom_by_account_id");

	hstmt = NULL;
	error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;
	}

	if (account_record)
		_remove_sensitive_info_from_non_owning_account(account_record, pid, uid);

	pthread_mutex_unlock(&account_mutex);
	ACCOUNT_DEBUG("_account_query_account_by_account_id end [%d]", error_code);

	return error_code;
}

GList* _account_query_account_by_user_name(int pid, uid_t uid, const char *user_name, int *error_code)
{
	*error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0;
	account_s *account_head = NULL;

	if (user_name == NULL) {
		_ERR("USER NAME IS NULL");
		*error_code = _ACCOUNT_ERROR_INVALID_PARAMETER;
		goto CATCH;
	}

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE user_name = ?", ACCOUNT_TABLE);

	hstmt = _account_prepare_query(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		*error_code = _ACCOUNT_ERROR_PERMISSION_DENIED;
		goto CATCH;
	}

	int binding_count = 1;
	_account_query_bind_text(hstmt, binding_count++, user_name);

	rc = _account_query_step(hstmt);

	if (rc != SQLITE_ROW) {
		_ERR("The record isn't found");
		*error_code = _ACCOUNT_ERROR_RECORD_NOT_FOUND;
		goto CATCH;
	}

	int tmp = 0;

	account_head = (account_s*) malloc(sizeof(account_s));
	if (account_head == NULL) {
		ACCOUNT_FATAL("malloc Failed");
		if (hstmt != NULL) {
			rc = _account_query_finalize(hstmt);
			if (rc != _ACCOUNT_ERROR_NONE) {
				_ERR("finalize error");
				*error_code = rc;
				goto CATCH;
			}
			hstmt = NULL;
		}
		*error_code = _ACCOUNT_ERROR_OUT_OF_MEMORY;
		goto CATCH;
	}
	ACCOUNT_MEMSET(account_head, 0x00, sizeof(account_s));

	while (rc == SQLITE_ROW) {
		account_s* account_record = NULL;

		account_record = (account_s*) malloc(sizeof(account_s));

		if (account_record == NULL) {
			ACCOUNT_FATAL("malloc Failed");
			break;
		}
		ACCOUNT_MEMSET(account_record, 0x00, sizeof(account_s));

		_account_convert_column_to_account(hstmt, account_record);

		account_head->account_list = g_list_append(account_head->account_list, account_record);

		rc = _account_query_step(hstmt);
		tmp++;
	}

	rc = _account_query_finalize(hstmt);

	if (rc != _ACCOUNT_ERROR_NONE) {
		_ERR("finalize error");
		*error_code = rc;
		goto CATCH;
	}

	hstmt = NULL;

	GList *iter;

	tmp = g_list_length(account_head->account_list);

	for (iter = account_head->account_list; iter != NULL; iter = g_list_next(iter)) {
		//account_h account;
		//account = (account_h)iter->data;

		account_s *testaccount = (account_s*)iter->data;

		_account_query_capability_by_account_id(g_hAccountDB, _account_add_capability_to_account_cb, testaccount->id, (void*)testaccount);
		_account_query_custom_by_account_id(g_hAccountDB, _account_add_custom_to_account_cb, testaccount->id, (void*)testaccount);
	}

	*error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		if (rc != _ACCOUNT_ERROR_NONE) {
			_ERR("finalize error");
			*error_code = rc;
		}
		hstmt = NULL;
	}

	pthread_mutex_unlock(&account_mutex);

	if (account_head) {
		_remove_sensitive_info_from_non_owning_account_list(account_head->account_list, pid, uid);
		GList* result = account_head->account_list;
		_ACCOUNT_FREE(account_head);
		return result;
	}

	return NULL;
}

GList*
_account_query_account_by_capability(int pid, uid_t uid, const char* capability_type, const int capability_value, int *error_code)
{
	*error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0;

	ACCOUNT_RETURN_VAL((capability_type != NULL), { *error_code = _ACCOUNT_ERROR_INVALID_PARAMETER; }, NULL, ("capability_type IS NULL"));

	if ((capability_value  < 0) || (capability_value >= _ACCOUNT_CAPABILITY_STATE_MAX)) {
		ACCOUNT_SLOGE("(%s)-(%d) capability_value is not equal to 0 or 1.\n", __FUNCTION__, __LINE__);
		*error_code = _ACCOUNT_ERROR_INVALID_PARAMETER;
		return NULL;
	}

	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), { *error_code = _ACCOUNT_ERROR_DB_NOT_OPENED; }, NULL, ("The database isn't connected."));

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE _id IN (SELECT account_id from %s WHERE key=? AND value=?)", ACCOUNT_TABLE, CAPABILITY_TABLE);

	hstmt = _account_prepare_query(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		*error_code = _ACCOUNT_ERROR_PERMISSION_DENIED;
		return NULL;
	}

	int binding_count = 1;
	_account_query_bind_text(hstmt, binding_count++, capability_type);
	_account_query_bind_int(hstmt, binding_count++, capability_value);

	rc = _account_query_step(hstmt);

	account_s* account_head = NULL;

	ACCOUNT_CATCH_ERROR_P(rc == SQLITE_ROW, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	int tmp = 0;

	account_head = (account_s*) malloc(sizeof(account_s));
	if (account_head == NULL) {
		ACCOUNT_FATAL("malloc Failed");
		if (hstmt != NULL) {
			rc = _account_query_finalize(hstmt);
			ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), { *error_code = rc; }, NULL, ("finalize error"));
			hstmt = NULL;
		}
		*error_code = _ACCOUNT_ERROR_OUT_OF_MEMORY;
		return NULL;
	}
	ACCOUNT_MEMSET(account_head, 0x00, sizeof(account_s));

	while (rc == SQLITE_ROW) {
		account_s* account_record = NULL;

		account_record = (account_s*) malloc(sizeof(account_s));

		if (account_record == NULL) {
			ACCOUNT_FATAL("malloc Failed");
			break;
		}
		ACCOUNT_MEMSET(account_record, 0x00, sizeof(account_s));

		_account_convert_column_to_account(hstmt, account_record);

		account_head->account_list = g_list_append(account_head->account_list, account_record);

		rc = _account_query_step(hstmt);
		tmp++;
	}

	rc = _account_query_finalize(hstmt);
	ACCOUNT_CATCH_ERROR_P((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	GList *iter;


	tmp = g_list_length(account_head->account_list);

	for (iter = account_head->account_list; iter != NULL; iter = g_list_next(iter)) {
		//account_h account = NULL;
		//account = (account_h)iter->data;
		account_s* testaccount = (account_s*)iter->data;

		_account_query_capability_by_account_id(g_hAccountDB, _account_add_capability_to_account_cb, testaccount->id, (void*)testaccount);
		_account_query_custom_by_account_id(g_hAccountDB, _account_add_custom_to_account_cb, testaccount->id, (void*)testaccount);

	}

	*error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		if (rc != _ACCOUNT_ERROR_NONE) {
			*error_code = rc;
			_ERR("finalize error");
		}
		hstmt = NULL;
	}

	if (*error_code != _ACCOUNT_ERROR_NONE && account_head) {
		_account_glist_account_free(account_head->account_list);
		_ACCOUNT_FREE(account_head);
		account_head = NULL;
	}

	pthread_mutex_unlock(&account_mutex);

	if (account_head) {
		_remove_sensitive_info_from_non_owning_account_list(account_head->account_list, pid, uid);
		GList* result = account_head->account_list;
		_ACCOUNT_FREE(account_head);
		return result;
	}

	return NULL;
}

GList* _account_query_account_by_capability_type(int pid, uid_t uid, const char* capability_type, int *error_code)
{
	*error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0;

	ACCOUNT_RETURN_VAL((capability_type != NULL), { *error_code = _ACCOUNT_ERROR_INVALID_PARAMETER; }, NULL, ("capability_type IS NULL"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), { *error_code = _ACCOUNT_ERROR_DB_NOT_OPENED; },
					   NULL, ("The database isn't connected."));

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE _id IN (SELECT account_id from %s WHERE key=?)", ACCOUNT_TABLE, CAPABILITY_TABLE);

	hstmt = _account_prepare_query(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		*error_code = _ACCOUNT_ERROR_PERMISSION_DENIED;
		return NULL;
	}

	int binding_count = 1;
	_account_query_bind_text(hstmt, binding_count++, capability_type);

	rc = _account_query_step(hstmt);

	account_s* account_head = NULL;

	ACCOUNT_CATCH_ERROR_P(rc == SQLITE_ROW, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	int tmp = 0;

	account_head = (account_s*) malloc(sizeof(account_s));
	if (account_head == NULL) {
		ACCOUNT_FATAL("malloc Failed");
		if (hstmt != NULL) {
			rc = _account_query_finalize(hstmt);
			ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), { *error_code = rc; }, NULL, ("finalize error"));
			hstmt = NULL;
		}
		*error_code = _ACCOUNT_ERROR_OUT_OF_MEMORY;
		return NULL;
	}
	ACCOUNT_MEMSET(account_head, 0x00, sizeof(account_s));

	while (rc == SQLITE_ROW) {
		account_s* account_record = NULL;

		account_record = (account_s*) malloc(sizeof(account_s));

		if (account_record == NULL) {
			ACCOUNT_FATAL("malloc Failed");
			break;
		}
		ACCOUNT_MEMSET(account_record, 0x00, sizeof(account_s));

		_account_convert_column_to_account(hstmt, account_record);

		account_head->account_list = g_list_append(account_head->account_list, account_record);

		rc = _account_query_step(hstmt);
		tmp++;
	}

	rc = _account_query_finalize(hstmt);
	ACCOUNT_CATCH_ERROR_P((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	GList *iter;


	tmp = g_list_length(account_head->account_list);

	for (iter = account_head->account_list; iter != NULL; iter = g_list_next(iter)) {
		account_s* testaccount = (account_s*)iter->data;

		_account_query_capability_by_account_id(g_hAccountDB, _account_add_capability_to_account_cb, testaccount->id, (void*)testaccount);
		_account_query_custom_by_account_id(g_hAccountDB, _account_add_custom_to_account_cb, testaccount->id, (void*)testaccount);

	}

	*error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		if (rc != _ACCOUNT_ERROR_NONE) {
			*error_code = rc;
			_ERR("finalize error");
		}
		hstmt = NULL;
	}

	if ((*error_code != _ACCOUNT_ERROR_NONE) && account_head) {
		_account_glist_account_free(account_head->account_list);
		_ACCOUNT_FREE(account_head);
		account_head = NULL;
	}

	pthread_mutex_unlock(&account_mutex);

	if (account_head) {
		_remove_sensitive_info_from_non_owning_account_list(account_head->account_list, pid, uid);
		GList* result = account_head->account_list;
		_ACCOUNT_FREE(account_head);
		return result;
	}

	return NULL;
}

GList* account_server_query_account_by_package_name(const char* package_name, int *error_code, int pid, uid_t uid)
{
	_INFO("account_server_query_account_by_package_name start");

	GList * account_list = NULL;
	*error_code = _ACCOUNT_ERROR_NONE;

	ACCOUNT_RETURN_VAL((package_name != NULL), { *error_code = _ACCOUNT_ERROR_INVALID_PARAMETER; }, NULL, ("PACKAGE NAME IS NULL"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), { *error_code = _ACCOUNT_ERROR_DB_NOT_OPENED; }, NULL, ("The database isn't connected."));

	account_list = _account_query_account_by_package_name(g_hAccountDB, package_name, error_code, pid, uid);

	_INFO("account_server_query_account_by_package_name end");

	return account_list;
}

int account_server_delete_account_by_package_name(const char* package_name, bool permission, int pid, uid_t uid)
{
	_INFO("account_db_delete_account_by_package_name");

	int error_code = _ACCOUNT_ERROR_NONE;

	ACCOUNT_RETURN_VAL((package_name != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("PACKAGE NAME IS NULL"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	error_code = _account_delete_account_by_package_name(g_hAccountDB, package_name, permission, pid, uid);

	_INFO("account_server_delete_account_by_package_name end");

	return error_code;
}

int _account_delete(int pid, uid_t uid, int account_id)
{
	int				error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0;
	int				ret_transaction = 0;
	bool			is_success = FALSE;

	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	int count = -1;
	/* Check requested ID to delete */
	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT COUNT(*) FROM %s WHERE _id=%d", ACCOUNT_TABLE, account_id);

	count = _account_get_record_count(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		pthread_mutex_unlock(&account_mutex);
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	if (count <= 0) {
		ACCOUNT_ERROR("account id(%d) is not exist. count(%d)\n", account_id, count);
		return _ACCOUNT_ERROR_RECORD_NOT_FOUND;
	}

	/* Check permission of requested appid */
	char* current_appid = NULL;
	char *package_name = NULL;

	current_appid = _account_get_current_appid(pid, uid);

	error_code = _account_get_package_name_from_account_id(account_id, &package_name);

	if (error_code != _ACCOUNT_ERROR_NONE) {
		ACCOUNT_ERROR("No package name with account_id\n");
		_ACCOUNT_FREE(current_appid);
		_ACCOUNT_FREE(package_name);
		return _ACCOUNT_ERROR_RECORD_NOT_FOUND;
	}
	ACCOUNT_DEBUG("DELETE:account_id[%d],current_appid[%s]package_name[%s]", account_id, current_appid, package_name);

	error_code = _account_check_appid_group_with_package_name(current_appid, package_name, uid);

	_ACCOUNT_FREE(current_appid);
	_ACCOUNT_FREE(package_name);

	if (error_code != _ACCOUNT_ERROR_NONE) {
		ACCOUNT_ERROR("No permission to delete\n");
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	/* transaction control required*/
	ret_transaction = _account_begin_transaction(g_hAccountDB);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		pthread_mutex_unlock(&account_mutex);
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	if (ret_transaction == _ACCOUNT_ERROR_DATABASE_BUSY) {
		ACCOUNT_ERROR("database busy(%s)", _account_db_err_msg(g_hAccountDB));
		pthread_mutex_unlock(&account_mutex);
		return _ACCOUNT_ERROR_DATABASE_BUSY;
	}

	if (ret_transaction != _ACCOUNT_ERROR_NONE) {
		ACCOUNT_ERROR("account_delete:_account_begin_transaction fail %d\n", ret_transaction);
		pthread_mutex_unlock(&account_mutex);
		return ret_transaction;
	}

	ACCOUNT_MEMSET(query, 0x00, sizeof(query));
	ACCOUNT_SNPRINTF(query, sizeof(query), "DELETE FROM %s WHERE account_id = %d", CAPABILITY_TABLE, account_id);

	hstmt = _account_prepare_query(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		pthread_mutex_unlock(&account_mutex);
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	ACCOUNT_CATCH_ERROR(hstmt != NULL, {}, _ACCOUNT_ERROR_DB_FAILED,
			("_account_svc_query_prepare(%s) failed(%s).\n", query, _account_db_err_msg(g_hAccountDB)));

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR(rc == SQLITE_DONE, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	rc = _account_query_finalize(hstmt);

	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	ACCOUNT_MEMSET(query, 0, sizeof(query));

	ACCOUNT_SNPRINTF(query, sizeof(query), "DELETE FROM %s WHERE _id = %d", ACCOUNT_TABLE, account_id);

	hstmt = _account_prepare_query(g_hAccountDB, query);
	ACCOUNT_CATCH_ERROR(hstmt != NULL, {}, _ACCOUNT_ERROR_DB_FAILED,
			("_account_svc_query_prepare(%s) failed(%s).\n", query, _account_db_err_msg(g_hAccountDB)));

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR(rc == SQLITE_DONE, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found. id=%d, rc=%d\n", account_id, rc));

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	/* delete custom data */
	ACCOUNT_MEMSET(query, 0, sizeof(query));

	ACCOUNT_SNPRINTF(query, sizeof(query), "DELETE FROM %s WHERE AccountId = %d", ACCOUNT_CUSTOM_TABLE, account_id);

	hstmt = _account_prepare_query(g_hAccountDB, query);

	ACCOUNT_CATCH_ERROR(hstmt != NULL, {}, _ACCOUNT_ERROR_DB_FAILED,
			("_account_svc_query_prepare(%s) failed(%s).\n", query, _account_db_err_msg(g_hAccountDB)));

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR(rc == SQLITE_DONE, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found. id=%d, rc=%d\n", account_id, rc));

	rc = _account_query_finalize(hstmt);
	ACCOUNT_CATCH_ERROR(rc == _ACCOUNT_ERROR_NONE, {}, rc, ("finalize error", account_id, rc));
	hstmt = NULL;

	is_success = TRUE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		if (rc != _ACCOUNT_ERROR_NONE) {
			ACCOUNT_ERROR("rc (%d)", rc);
			is_success = FALSE;
		}

		hstmt = NULL;
	}

	ret_transaction = _account_end_transaction(g_hAccountDB, is_success);

	if (ret_transaction != _ACCOUNT_ERROR_NONE) {
		ACCOUNT_ERROR("account_delete:_account_end_transaction fail %d, is_success=%d\n", ret_transaction, is_success);
	} else {
		if (is_success == true) {
			char buf[64] = {0,};
			ACCOUNT_SNPRINTF(buf, sizeof(buf), "%s:%d", _ACCOUNT_NOTI_NAME_DELETE, account_id);
			_account_insert_delete_update_notification_send(buf);
		}
	}

	pthread_mutex_unlock(&account_mutex);

	return error_code;
}

static int _account_query_account_by_username_and_package(const char* username, const char* package_name, account_s *account)
{
	//FIXME
	//return -1;
	int				error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0;
	int				binding_count = 1;

	ACCOUNT_RETURN_VAL((username != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("username IS NULL"));
	ACCOUNT_RETURN_VAL((package_name != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("package_name IS NULL"));
	ACCOUNT_RETURN_VAL((account != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("ACCOUNT IS NULL"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE user_name = ? and package_name = ?", ACCOUNT_TABLE);
	hstmt = _account_prepare_query(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	_account_query_bind_text(hstmt, binding_count++, username);
	_account_query_bind_text(hstmt, binding_count++, package_name);

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR(rc == SQLITE_ROW, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	account_s *account_record = account;

	while (rc == SQLITE_ROW) {
		_account_convert_column_to_account(hstmt, account_record);
		rc = _account_query_step(hstmt);
	}

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	_account_query_capability_by_account_id(g_hAccountDB, _account_add_capability_to_account_cb, account_record->id, (void*)account_record);
	_account_query_custom_by_account_id(g_hAccountDB, _account_add_custom_to_account_cb, account_record->id, (void*)account_record);

	hstmt = NULL;
	error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;
	}

	pthread_mutex_unlock(&account_mutex);

	return error_code;
}

int _account_create(account_s **account)
{
	if (!account) {
		ACCOUNT_SLOGE("(%s)-(%d) account is NULL.\n", __FUNCTION__, __LINE__);
		return _ACCOUNT_ERROR_INVALID_PARAMETER;
	}

	account_s *data = (account_s*)malloc(sizeof(account_s));

	if (data == NULL) {
		ACCOUNT_FATAL("Memory Allocation Failed");
		return _ACCOUNT_ERROR_OUT_OF_MEMORY;
	}
	ACCOUNT_MEMSET(data, 0, sizeof(account_s));

	/*Setting account as visible by default*/
	//data->secret = _ACCOUNT_SECRECY_VISIBLE;

	/*Setting account as not supporting sync by default*/
	//data->sync_support = _ACCOUNT_SYNC_NOT_SUPPORT;

	*account = data;

	return _ACCOUNT_ERROR_NONE;
}

int _account_destroy(account_s *account)
{
	account_s *data = account;

	ACCOUNT_RETURN_VAL((data != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Account handle is null!"));

	_account_free_account_with_items(data);

	return _ACCOUNT_ERROR_NONE;
}

int _account_get_account_id(account_s* account, int *account_id)
{
	if (!account)
		return _ACCOUNT_ERROR_INVALID_PARAMETER;

	if (!account_id)
		return _ACCOUNT_ERROR_INVALID_PARAMETER;

	*account_id = account->id;

	return _ACCOUNT_ERROR_NONE;
}

int _account_delete_from_db_by_user_name(int pid, uid_t uid, const char *user_name, const char *package_name)
{
	_INFO("[%s][%s]", user_name, package_name);

	int				error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0;
	int				ret_transaction = 0;
	bool			is_success = FALSE;
	account_s		*account = NULL;
	int				binding_count = 1;
	int				account_id = -1;

	ACCOUNT_RETURN_VAL((user_name != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("user_name is null!"));
	ACCOUNT_RETURN_VAL((package_name != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("package_name is null!"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	/* Check permission of requested appid */
	char* current_appid = NULL;
	char* package_name_temp = NULL;

	current_appid = _account_get_current_appid(pid, uid);

	package_name_temp = _account_dup_text(package_name);

	ACCOUNT_DEBUG("DELETE:user_name[%s],current_appid[%s], package_name[%s]", user_name, current_appid, package_name_temp);

	error_code = _account_check_appid_group_with_package_name(current_appid, package_name_temp, uid);

	_ACCOUNT_FREE(current_appid);
	_ACCOUNT_FREE(package_name_temp);

	if (error_code != _ACCOUNT_ERROR_NONE) {
		ACCOUNT_ERROR("No permission to delete\n");
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	rc = _account_create(&account);
	rc = _account_query_account_by_username_and_package(user_name, package_name, account);

	_INFO("");

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		_account_destroy(account);
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	_INFO("");
	account_s* account_data = (account_s*)account;

	rc = _account_get_account_id(account_data, &account_id);

	rc = _account_destroy(account);

	/* transaction control required*/
	ret_transaction = _account_begin_transaction(g_hAccountDB);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		pthread_mutex_unlock(&account_mutex);
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	_INFO("");
	if (ret_transaction == _ACCOUNT_ERROR_DATABASE_BUSY) {
		ACCOUNT_ERROR("database busy(%s)", _account_db_err_msg(g_hAccountDB));
		pthread_mutex_unlock(&account_mutex);
		return _ACCOUNT_ERROR_DATABASE_BUSY;
	} else if (ret_transaction != _ACCOUNT_ERROR_NONE) {
		ACCOUNT_ERROR("account_delete:_account_begin_transaction fail %d\n", ret_transaction);
		pthread_mutex_unlock(&account_mutex);
		return ret_transaction;
	}

	/* delete custom data */
	ACCOUNT_MEMSET(query, 0, sizeof(query));
	ACCOUNT_SNPRINTF(query, sizeof(query), "DELETE FROM %s WHERE AccountId = ?", ACCOUNT_CUSTOM_TABLE);

	hstmt = _account_prepare_query(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		_account_end_transaction(g_hAccountDB, FALSE);
		pthread_mutex_unlock(&account_mutex);
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	ACCOUNT_CATCH_ERROR(hstmt != NULL, {}, _ACCOUNT_ERROR_DB_FAILED,
			("_account_svc_query_prepare(%s) failed(%s).\n", query, _account_db_err_msg(g_hAccountDB)));

	_account_query_bind_int(hstmt, binding_count++, account_id);

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR(rc == SQLITE_DONE, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	/* delete capability */
	ACCOUNT_SNPRINTF(query, sizeof(query), "DELETE FROM %s WHERE user_name = ? and package_name = ?", CAPABILITY_TABLE);

	hstmt = _account_prepare_query(g_hAccountDB, query);

	ACCOUNT_CATCH_ERROR(hstmt != NULL, {}, _ACCOUNT_ERROR_DB_FAILED,
			("_account_svc_query_prepare(%s) failed(%s).\n", query, _account_db_err_msg(g_hAccountDB)));

	binding_count = 1;
	_account_query_bind_text(hstmt, binding_count++, user_name);
	_account_query_bind_text(hstmt, binding_count++, package_name);

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR(rc == SQLITE_DONE, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	ACCOUNT_MEMSET(query, 0, sizeof(query));

	_INFO("");
	ACCOUNT_SNPRINTF(query, sizeof(query), "DELETE FROM %s WHERE user_name = ? and package_name = ?", ACCOUNT_TABLE);

	hstmt = _account_prepare_query(g_hAccountDB, query);
	ACCOUNT_CATCH_ERROR(hstmt != NULL, {}, _ACCOUNT_ERROR_DB_FAILED,
			("_account_svc_query_prepare(%s) failed(%s).\n", query, _account_db_err_msg(g_hAccountDB)));

	_INFO("");
	binding_count = 1;
	_account_query_bind_text(hstmt, binding_count++, user_name);
	_account_query_bind_text(hstmt, binding_count++, package_name);

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR(rc == SQLITE_DONE, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found. user_name=%s, package_name=%s, rc=%d\n", user_name, package_name, rc));

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	is_success = TRUE;

	hstmt = NULL;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;
	}

	ret_transaction = _account_end_transaction(g_hAccountDB, is_success);

	if (ret_transaction != _ACCOUNT_ERROR_NONE) {
		ACCOUNT_ERROR("account_svc_delete:_account_svc_end_transaction fail %d, is_success=%d\n", ret_transaction, is_success);
	} else {
		if (is_success == true) {
			char buf[64] = {0,};
			ACCOUNT_SNPRINTF(buf, sizeof(buf), "%s:%d", _ACCOUNT_NOTI_NAME_DELETE, account_id);
			_account_insert_delete_update_notification_send(buf);
		}
	}

	pthread_mutex_unlock(&account_mutex);

	return error_code;
}


int _account_get_total_count_from_db(gboolean include_hidden, int *count)
{
	if (!count) {
		ACCOUNT_SLOGE("(%s)-(%d) count is NULL.\n", __FUNCTION__, __LINE__);
		return _ACCOUNT_ERROR_INVALID_PARAMETER;
	}

	if (!g_hAccountDB) {
		ACCOUNT_ERROR("DB is not opened\n");
		return _ACCOUNT_ERROR_DB_NOT_OPENED;
	}

	char query[1024] = {0, };
	ACCOUNT_MEMSET(query, 0x00, sizeof(query));

	if (include_hidden)
		ACCOUNT_SNPRINTF(query, sizeof(query), "select count(*) from %s", ACCOUNT_TABLE);
	else
		ACCOUNT_SNPRINTF(query, sizeof(query), "select count(*) from %s where secret = %d", ACCOUNT_TABLE, _ACCOUNT_SECRECY_VISIBLE);

	*count = _account_get_record_count(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		pthread_mutex_unlock(&account_mutex);
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	int rc = -1;
	int ncount = 0;
	account_stmt pStmt = NULL;

	rc = sqlite3_prepare_v2(g_hAccountDB, query, strlen(query), &pStmt, NULL);
	if (SQLITE_OK != rc) {
		ACCOUNT_SLOGE("sqlite3_prepare_v2() failed(%d, %s).", rc, _account_db_err_msg(g_hAccountDB));
		sqlite3_finalize(pStmt);
		return _ACCOUNT_ERROR_DB_FAILED;
	}

	rc = sqlite3_step(pStmt);
	if (SQLITE_ROW != rc) {
		ACCOUNT_ERROR("[ERROR] sqlite3_step() failed\n");
		sqlite3_finalize(pStmt);
		return _ACCOUNT_ERROR_RECORD_NOT_FOUND;
	}

	ncount = sqlite3_column_int(pStmt, 0);

	*count = ncount;

	sqlite3_finalize(pStmt);

	if (ncount < 0) {
		ACCOUNT_ERROR("[ERROR] Number of account : %d, End", ncount);
		return _ACCOUNT_ERROR_DB_FAILED;
	}

	return _ACCOUNT_ERROR_NONE;
}


int account_server_query_app_id_exist(const char* app_id)
{
	_INFO("account_server_query_app_id_exist start app_id=[%s]", app_id);
	int ret = _ACCOUNT_ERROR_NONE;

	ret = _account_type_query_app_id_exist_from_all_db(g_hAccountDB, g_hAccountGlobalDB, app_id);

	_INFO("account_server_query_app_id_exist end error_code=[%d]", ret);

	return ret;
}

int account_server_insert_account_type_to_user_db(account_type_s *account_type, int *account_type_id, uid_t uid)
{
	ACCOUNT_RETURN_VAL((account_type != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("ACCOUNT TYPE HANDLE IS NULL"));
	ACCOUNT_RETURN_VAL((account_type->app_id != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("APP ID OF ACCOUNT TYPE IS NULL"));
	ACCOUNT_RETURN_VAL((account_type_id != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("ACCOUNT TYPE ID IS NULL"));

	_INFO("account_server_insert_account_type_to_user_db start uid=[%d]", uid);
	int ret = _ACCOUNT_ERROR_NONE;

	if (_account_type_check_duplicated(g_hAccountDB, account_type->app_id) ||
			_account_type_check_duplicated(g_hAccountGlobalDB, account_type->app_id)) {
		*account_type_id = -1;
		return _ACCOUNT_ERROR_DUPLICATED;
	}

	ret = _account_type_insert_to_db(g_hAccountDB, account_type, account_type_id);
	_INFO("account_server_insert_account_type_to_user_db end error_code=[%d]", ret);

	return ret;
}

int account_server_delete_account_type_by_app_id_from_user_db(const char * app_id)
{
	ACCOUNT_RETURN_VAL((app_id != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("APP ID OF ACCOUNT TYPE IS NULL"));

	_INFO("account_server_delete_account_type_by_app_id_from_user_db start");
	int ret = _ACCOUNT_ERROR_NONE;

	ret = _account_type_delete_by_app_id(g_hAccountDB, app_id);
	_INFO("account_server_delete_account_type_by_app_id_from_user_db end error_code=[%d]", ret);

	return ret;
}

GSList* _account_type_query_provider_feature_by_app_id_from_global_db(const char* app_id, int *error_code)
{
	_INFO("_account_type_query_provider_feature_by_app_id_in_global_db app_id=%s", app_id);
	account_stmt hstmt = NULL;
	char query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int rc = 0, binding_count = 1;
	GSList* feature_list = NULL;

	ACCOUNT_RETURN_VAL((app_id != NULL), { *error_code = _ACCOUNT_ERROR_INVALID_PARAMETER; }, NULL, ("APP ID IS NULL"));
	ACCOUNT_RETURN_VAL((error_code != NULL), {_ERR("error_code pointer is NULL"); }, NULL, (""));
	ACCOUNT_RETURN_VAL((g_hAccountGlobalDB != NULL), { *error_code = _ACCOUNT_ERROR_INVALID_PARAMETER; _ERR("The database isn't connected."); }, NULL, ("The database isn't connected."));

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE app_id = ?", PROVIDER_FEATURE_TABLE);
	_INFO("account query=[%s]", query);

	hstmt = _account_prepare_query(g_hAccountGlobalDB, query);

	if (_account_db_err_code(g_hAccountGlobalDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		*error_code = _ACCOUNT_ERROR_PERMISSION_DENIED;
		return NULL;
	}

	_INFO("before _account_query_bind_text");
	_account_query_bind_text(hstmt, binding_count++, app_id);

	rc = _account_query_step(hstmt);

	ACCOUNT_CATCH_ERROR_P(rc == SQLITE_ROW, { *error_code = _ACCOUNT_ERROR_RECORD_NOT_FOUND; _ERR("The record isn't found from global db. rc=[%d]", rc); }, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	provider_feature_s* feature_record = NULL;

	while (rc == SQLITE_ROW) {

		feature_record = (provider_feature_s*) malloc(sizeof(provider_feature_s));

		if (feature_record == NULL) {
			ACCOUNT_FATAL("malloc Failed");
			break;
		}

		ACCOUNT_MEMSET(feature_record, 0x00, sizeof(provider_feature_s));

		_account_type_convert_column_to_provider_feature(hstmt, feature_record);

		_INFO("Adding account feature_list");
		feature_list = g_slist_append(feature_list, feature_record);

		rc = _account_query_step(hstmt);
	}

	*error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		if (rc != _ACCOUNT_ERROR_NONE) {
			*error_code = rc;
			_ERR("global db fianlize error");
		}
	}

	if (*error_code != _ACCOUNT_ERROR_NONE)
		_account_type_gslist_feature_free(feature_list);

	_INFO("Returning account feature_list from global db");

	return feature_list;
}

GSList* _account_type_query_provider_feature_by_app_id(const char* app_id, int *error_code)
{
	_INFO("_account_type_query_provider_feature_by_app_id app_id=%s", app_id);
	account_stmt hstmt = NULL;
	char query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int rc = 0, binding_count = 1;
	GSList* feature_list = NULL;

	ACCOUNT_RETURN_VAL((app_id != NULL), { *error_code = _ACCOUNT_ERROR_INVALID_PARAMETER; }, NULL, ("APP ID IS NULL"));
	ACCOUNT_RETURN_VAL((error_code != NULL), {_ERR("error_code pointer is NULL"); }, NULL, (""));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), { *error_code = _ACCOUNT_ERROR_DB_NOT_OPENED; }, NULL, ("The database isn't connected."));

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE app_id = ?", PROVIDER_FEATURE_TABLE);
	_INFO("account query=[%s]", query);

	hstmt = _account_prepare_query(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		*error_code = _ACCOUNT_ERROR_PERMISSION_DENIED;
		return NULL;
	}

	_account_query_bind_text(hstmt, binding_count++, app_id);

	rc = _account_query_step(hstmt);

	ACCOUNT_CATCH_ERROR_P(rc == SQLITE_ROW, { *error_code = _ACCOUNT_ERROR_RECORD_NOT_FOUND;
			_ERR("The record isn't found from user db. rc=[%d]", rc); },
				_ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	provider_feature_s* feature_record = NULL;

	while (rc == SQLITE_ROW) {

		feature_record = (provider_feature_s*) malloc(sizeof(provider_feature_s));

		if (feature_record == NULL) {
			ACCOUNT_FATAL("malloc Failed");
			break;
		}

		ACCOUNT_MEMSET(feature_record, 0x00, sizeof(provider_feature_s));

		_account_type_convert_column_to_provider_feature(hstmt, feature_record);

		_INFO("Adding account feature_list");
		feature_list = g_slist_append(feature_list, feature_record);

		rc = _account_query_step(hstmt);
	}

	*error_code = _ACCOUNT_ERROR_NONE;

	rc = _account_query_finalize(hstmt);
	ACCOUNT_CATCH_ERROR_P((rc == _ACCOUNT_ERROR_NONE), { *error_code = rc; }, rc, ("account finalize error"));
	hstmt = NULL;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		if (rc != _ACCOUNT_ERROR_NONE) {
			*error_code = rc;
			_ERR("account fianlize error");
		}
		hstmt = NULL;
	}
	_INFO("*error_code=[%d]", *error_code);

	if (*error_code == _ACCOUNT_ERROR_RECORD_NOT_FOUND)
		feature_list = _account_type_query_provider_feature_by_app_id_from_global_db(app_id, error_code);

	if (*error_code != _ACCOUNT_ERROR_NONE) {
		_account_type_gslist_feature_free(feature_list);
		return NULL;
	}

	_INFO("Returning account feature_list");

	return feature_list;
}

int _account_type_query_provider_feature_cb_by_app_id_from_global_db(account_type_provider_feature_cb callback, const char* app_id, void *user_data)
{
	int				error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0, binding_count = 1;

	_INFO("_account_type_query_provider_feature_cb_by_app_id_in_global_db start app_id=%s", app_id);
	ACCOUNT_RETURN_VAL((app_id != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("APP ID IS NULL"));
	ACCOUNT_RETURN_VAL((callback != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("NO CALLBACK FUNCTION"));
	ACCOUNT_RETURN_VAL((g_hAccountGlobalDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE app_id = ?", PROVIDER_FEATURE_TABLE);
	hstmt = _account_prepare_query(g_hAccountGlobalDB, query);

	if (_account_db_err_code(g_hAccountGlobalDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountGlobalDB));
		ACCOUNT_CATCH_ERROR(rc == SQLITE_ROW, {}, _ACCOUNT_ERROR_PERMISSION_DENIED, ("global db permission denied.\n"));
	}

	_account_query_bind_text(hstmt, binding_count++, app_id);

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR(rc == SQLITE_ROW, {_ERR("The record isn't found. rc=[%d]", rc); }, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	provider_feature_s* feature_record = NULL;

	while (rc == SQLITE_ROW) {
		bool cb_ret = FALSE;
		feature_record = (provider_feature_s*) malloc(sizeof(provider_feature_s));

		if (feature_record == NULL) {
			ACCOUNT_FATAL("malloc Failed");
			break;
		}

		ACCOUNT_MEMSET(feature_record, 0x00, sizeof(provider_feature_s));

		_account_type_convert_column_to_provider_feature(hstmt, feature_record);

		cb_ret = callback(feature_record->app_id, feature_record->key, user_data);

		_account_type_free_feature_with_items(feature_record);

		ACCOUNT_CATCH_ERROR(cb_ret == TRUE, {}, _ACCOUNT_ERROR_NONE, ("Callback func returns FALSE, its iteration is stopped!!!!\n"));

		rc = _account_query_step(hstmt);
	}

	error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		if (rc != _ACCOUNT_ERROR_NONE) {
			error_code = rc;
			_ERR("global db finalize error[%d]", rc);
		}
		hstmt = NULL;
	}

	_INFO("_account_type_query_provider_feature_cb_by_app_id_in_global_db end. error_code=[%d]", error_code);

	return error_code;
}

int _account_type_query_provider_feature_cb_by_app_id(account_type_provider_feature_cb callback, const char* app_id, void *user_data)
{
	int				error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0, binding_count = 1;

	_INFO("_account_type_query_provider_feature_cb_by_app_id start app_id=%s", app_id);
	ACCOUNT_RETURN_VAL((app_id != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("APP ID IS NULL"));
	ACCOUNT_RETURN_VAL((callback != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("NO CALLBACK FUNCTION"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE app_id = ?", PROVIDER_FEATURE_TABLE);
	hstmt = _account_prepare_query(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	_account_query_bind_text(hstmt, binding_count++, app_id);

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR(rc == SQLITE_ROW, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found in user db.\n"));

	provider_feature_s* feature_record = NULL;

	while (rc == SQLITE_ROW) {
		bool cb_ret = FALSE;
		feature_record = (provider_feature_s*) malloc(sizeof(provider_feature_s));

		if (feature_record == NULL) {
			ACCOUNT_FATAL("malloc Failed");
			break;
		}

		ACCOUNT_MEMSET(feature_record, 0x00, sizeof(provider_feature_s));

		_account_type_convert_column_to_provider_feature(hstmt, feature_record);

		cb_ret = callback(feature_record->app_id, feature_record->key, user_data);

		_account_type_free_feature_with_items(feature_record);

		ACCOUNT_CATCH_ERROR(cb_ret == TRUE, {}, _ACCOUNT_ERROR_NONE, ("Callback func returs FALSE, its iteration is stopped!!!!\n"));

		rc = _account_query_step(hstmt);
	}

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;
	}
/*
	if (error_code == _ACCOUNT_ERROR_RECORD_NOT_FOUND)
		error_code = _account_type_query_provider_feature_cb_by_app_id_from_global_db(callback, app_id, user_data);
*/
	_INFO("_account_type_query_provider_feature_cb_by_app_id end");

	return error_code;
}

int account_type_query_provider_feature_cb_by_app_id(account_type_provider_feature_cb callback, const char* app_id, void *user_data)
{
	int				error_code = _ACCOUNT_ERROR_NONE;

	ACCOUNT_RETURN_VAL((app_id != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("APP ID IS NULL"));
	ACCOUNT_RETURN_VAL((callback != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("NO CALLBACK FUNCTION"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	error_code = _account_type_query_provider_feature_cb_by_app_id(callback, app_id, user_data);

	if (error_code == _ACCOUNT_ERROR_RECORD_NOT_FOUND)
		error_code = _account_type_query_provider_feature_cb_by_app_id_from_global_db(callback, app_id, user_data);

	return error_code;
}

bool _account_type_query_supported_feature_from_global_db(const char* app_id, const char* capability, int *error_code)
{
	_INFO("_account_type_query_supported_feature_in_global_db start");
	ACCOUNT_RETURN_VAL((g_hAccountGlobalDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	*error_code = _ACCOUNT_ERROR_NONE;

	char query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int record_count = 0;

	if (app_id == NULL || capability == NULL) {
		*error_code = _ACCOUNT_ERROR_INVALID_PARAMETER;
		return false;
	}

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT COUNT(*) FROM %s where app_id='%s' and key='%s'", PROVIDER_FEATURE_TABLE, app_id, capability);

	record_count = _account_get_record_count(g_hAccountGlobalDB, query);

	if (_account_db_err_code(g_hAccountGlobalDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountGlobalDB));
		*error_code = _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	if (record_count <= 0) {
		*error_code = _ACCOUNT_ERROR_RECORD_NOT_FOUND;
		return false;
	}

	_INFO("_account_type_query_supported_feature_in_global_db end");

	return true;
}

bool _account_type_query_supported_feature(const char* app_id, const char* capability, int *error_code)
{
	_INFO("_account_type_query_supported_feature start");

	*error_code = _ACCOUNT_ERROR_NONE;

	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				record_count = 0;

	if (app_id == NULL || capability == NULL) {
		*error_code = _ACCOUNT_ERROR_INVALID_PARAMETER;
		return false;
	}

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT COUNT(*) FROM %s where app_id='%s' and key='%s'", PROVIDER_FEATURE_TABLE, app_id, capability);

	record_count = _account_get_record_count(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		*error_code = _ACCOUNT_ERROR_PERMISSION_DENIED;
		return false;
	}

	if (record_count <= 0) {
		bool is_exist = false;
		is_exist = _account_type_query_supported_feature_from_global_db(app_id, capability, error_code);
		if (!is_exist)
			return false;
	}

	_INFO("_account_type_query_supported_feature end");

	return true;
}


static int _account_type_update_provider_feature(sqlite3 *account_db_handle, account_type_s *account_type, const char* app_id)
{
	int				rc, count = 1;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	account_stmt	hstmt = NULL;

	ACCOUNT_RETURN_VAL((account_type != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("ACCOUNT HANDLE IS NULL"));

	if (g_slist_length(account_type->provider_feature_list) == 0) {
		ACCOUNT_ERROR("no feature\n");
		return _ACCOUNT_ERROR_NONE;
	}

	ACCOUNT_DEBUG("app id", app_id);

	ACCOUNT_MEMSET(query, 0x00, sizeof(query));

	ACCOUNT_SNPRINTF(query, sizeof(query), "DELETE FROM %s WHERE app_id=? ", PROVIDER_FEATURE_TABLE);
	hstmt = _account_prepare_query(account_db_handle, query);

	if (_account_db_err_code(account_db_handle) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(account_db_handle));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	count = 1;
	_account_query_bind_text(hstmt, count++, app_id);
	rc = _account_query_step(hstmt);

	if (rc != SQLITE_DONE) {
		ACCOUNT_ERROR("_account_query_step() failed(%d, %s)", rc, _account_db_err_msg(account_db_handle));
		return _ACCOUNT_ERROR_DB_FAILED;
	}
	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	GSList *iter;

	for (iter = account_type->provider_feature_list; iter != NULL; iter = g_slist_next(iter)) {
		int ret;
		count = 1;
		ACCOUNT_MEMSET(query, 0x00, sizeof(query));
		ACCOUNT_SNPRINTF(query, sizeof(query), "INSERT INTO %s(app_id, key) VALUES "
				"(?, ?) ", PROVIDER_FEATURE_TABLE);

		hstmt = _account_prepare_query(account_db_handle, query);

		ACCOUNT_RETURN_VAL((hstmt != NULL), {}, _ACCOUNT_ERROR_DB_FAILED, ("_account_prepare_query() failed(%s).\n", _account_db_err_msg(account_db_handle)));

		provider_feature_s* feature_data = NULL;
		feature_data = (provider_feature_s*)iter->data;

		ret = _account_query_bind_text(hstmt, count++, account_type->app_id);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));
		ret = _account_query_bind_text(hstmt, count++, feature_data->key);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));

		rc = _account_query_step(hstmt);

		if (rc != SQLITE_DONE) {
			ACCOUNT_ERROR("_account_query_step() failed(%d, %s)", rc, _account_db_err_msg(account_db_handle));
			break;
		}
		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;
	}

	return _ACCOUNT_ERROR_NONE;
}

static int _account_type_update_label(sqlite3 *account_db_handle, account_type_s *account_type, const char* app_id)
{
	int				rc, count = 1;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	account_stmt	hstmt = NULL;

	ACCOUNT_RETURN_VAL((account_type != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("ACCOUNT HANDLE IS NULL"));

	if (g_slist_length(account_type->label_list) == 0)
		return _ACCOUNT_ERROR_NONE;

	ACCOUNT_MEMSET(query, 0x00, sizeof(query));

	ACCOUNT_SNPRINTF(query, sizeof(query), "DELETE FROM %s WHERE AppId=? ", LABEL_TABLE);
	hstmt = _account_prepare_query(account_db_handle, query);

	if (_account_db_err_code(account_db_handle) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(account_db_handle));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	count = 1;
	_account_query_bind_text(hstmt, count++, app_id);
	rc = _account_query_step(hstmt);

	if (rc != SQLITE_DONE) {
		ACCOUNT_ERROR("_account_query_step() failed(%d, %s)", rc, _account_db_err_msg(account_db_handle));
		return _ACCOUNT_ERROR_DB_FAILED;
	}
	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	GSList *iter;

	for (iter = account_type->label_list; iter != NULL; iter = g_slist_next(iter)) {
		int ret;
		count = 1;
		ACCOUNT_MEMSET(query, 0x00, sizeof(query));
		ACCOUNT_SNPRINTF(query, sizeof(query), "INSERT INTO %s(AppId, Label, Locale) VALUES "
				"(?, ?, ?) ", LABEL_TABLE);

		hstmt = _account_prepare_query(account_db_handle, query);

		ACCOUNT_RETURN_VAL((hstmt != NULL), {}, _ACCOUNT_ERROR_DB_FAILED, ("_account_prepare_query() failed(%s).\n", _account_db_err_msg(account_db_handle)));

		label_s* label_data = NULL;
		label_data = (label_s*)iter->data;

		ret = _account_query_bind_text(hstmt, count++, account_type->app_id);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));
		ret = _account_query_bind_text(hstmt, count++, label_data->label);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));
		ret = _account_query_bind_text(hstmt, count++, label_data->locale);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));

		rc = _account_query_step(hstmt);

		if (rc != SQLITE_DONE) {
			ACCOUNT_ERROR("_account_query_step() failed(%d, %s)", rc, _account_db_err_msg(account_db_handle));
			break;
		}
		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;
	}

	return _ACCOUNT_ERROR_NONE;
}


static int _account_type_update_account(sqlite3 *account_db_handle, account_type_s *account_type, const char* app_id)
{
	int				rc = 0, binding_count = 1;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;

	if (!account_type->app_id) {
		ACCOUNT_ERROR("app id is mandetory field, it can not be NULL!!!!\n");
		return _ACCOUNT_ERROR_INVALID_PARAMETER;
	}

	ACCOUNT_MEMSET(query, 0x00, sizeof(query));
	ACCOUNT_SNPRINTF(query, sizeof(query), "UPDATE %s SET AppId=?, ServiceProviderId=?, IconPath=?, "
			"SmallIconPath=?, MultipleAccountSupport=? WHERE AppId=? ", ACCOUNT_TYPE_TABLE);

	hstmt = _account_prepare_query(account_db_handle, query);

	if (_account_db_err_code(account_db_handle) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(account_db_handle));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	} else if (_account_db_err_code(account_db_handle) == SQLITE_BUSY) {
		ACCOUNT_ERROR("database busy(%s)", _account_db_err_msg(account_db_handle));
		return _ACCOUNT_ERROR_DATABASE_BUSY;
	}

	ACCOUNT_RETURN_VAL((hstmt != NULL), {}, _ACCOUNT_ERROR_DB_FAILED, ("_account_svc_query_prepare() failed(%s).\n", _account_db_err_msg(account_db_handle)));

	binding_count = _account_type_convert_account_to_sql(account_type, hstmt, query);
	_account_query_bind_text(hstmt, binding_count++, app_id);

	rc = _account_query_step(hstmt);
	if (rc != SQLITE_DONE)
		ACCOUNT_ERROR("account_db_query_step() failed(%d, %s)", rc, _account_db_err_msg(account_db_handle));

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	/*update label*/
	error_code = _account_type_update_label(account_db_handle, account_type, app_id);
	/* update provider feature */
	error_code = _account_type_update_provider_feature(account_db_handle, account_type, app_id);

	return error_code;
}

int _account_type_update_to_db_by_app_id(account_type_s* account_type, const char* app_id)
{
	int	error_code = _ACCOUNT_ERROR_NONE;

	ACCOUNT_RETURN_VAL((account_type != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("DATA IS NULL"));
	ACCOUNT_RETURN_VAL((app_id != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("APP ID IS NULL"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	account_type_s* data = account_type;

	pthread_mutex_lock(&account_mutex);

	error_code = _account_type_update_account(g_hAccountDB, data, app_id);

	pthread_mutex_unlock(&account_mutex);

	return error_code;
}

GSList* _account_type_get_label_list_by_app_id_from_global_db(const char* app_id, int *error_code)
{
	*error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0, binding_count = 1;
	GSList* label_list = NULL;

	ACCOUNT_RETURN_VAL((app_id != NULL), { *error_code = _ACCOUNT_ERROR_INVALID_PARAMETER; }, NULL, ("APP ID IS NULL"));
	ACCOUNT_RETURN_VAL((g_hAccountGlobalDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE AppId = ?", LABEL_TABLE);
	hstmt = _account_prepare_query(g_hAccountGlobalDB, query);

	if (_account_db_err_code(g_hAccountGlobalDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountGlobalDB));
		*error_code = _ACCOUNT_ERROR_PERMISSION_DENIED;

		goto CATCH;
	}

	_account_query_bind_text(hstmt, binding_count++, app_id);

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR_P((rc == SQLITE_ROW), {_ERR("The record isn't found. rc=[%d] done", rc); }, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	label_s* label_record = NULL;

	while (rc == SQLITE_ROW) {
		label_record = (label_s*) malloc(sizeof(label_s));

		if (label_record == NULL) {
			ACCOUNT_FATAL("malloc Failed");
			break;
		}

		ACCOUNT_MEMSET(label_record, 0x00, sizeof(label_s));

		_account_type_convert_column_to_label(hstmt, label_record);

		_INFO("Adding account label_list");
		label_list = g_slist_append(label_list, label_record);

		rc = _account_query_step(hstmt);
	}

	*error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		if (rc != _ACCOUNT_ERROR_NONE)
			_ERR("global db finalize error[%d]", rc);
		hstmt = NULL;
	}

	_INFO("Returning account global label_list");

	return label_list;
}

GSList* _account_type_get_label_list_by_app_id(const char* app_id, int *error_code)
{
	*error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0, binding_count = 1;
	GSList* label_list = NULL;

	ACCOUNT_RETURN_VAL((app_id != NULL), { *error_code = _ACCOUNT_ERROR_INVALID_PARAMETER; }, NULL, ("APP ID IS NULL"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), { *error_code = _ACCOUNT_ERROR_DB_NOT_OPENED; }, NULL, ("The database isn't connected."));

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE AppId = ?", LABEL_TABLE);
	hstmt = _account_prepare_query(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		*error_code = _ACCOUNT_ERROR_PERMISSION_DENIED;
		return NULL;
	}

	_account_query_bind_text(hstmt, binding_count++, app_id);

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR_P(rc == SQLITE_ROW, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	label_s* label_record = NULL;

	while (rc == SQLITE_ROW) {
		label_record = (label_s*) malloc(sizeof(label_s));

		if (label_record == NULL) {
			ACCOUNT_FATAL("malloc Failed");
			break;
		}

		ACCOUNT_MEMSET(label_record, 0x00, sizeof(label_s));

		_account_type_convert_column_to_label(hstmt, label_record);

		_INFO("Adding account label_list");
		label_list = g_slist_append(label_list, label_record);

		rc = _account_query_step(hstmt);
	}

	rc = _account_query_finalize(hstmt);
	if (rc != _ACCOUNT_ERROR_NONE) {
			_account_type_gslist_label_free(label_list);
			*error_code = rc;
			_ERR("finalize error");
			return NULL;
		}
	hstmt = NULL;

	*error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		if (rc != _ACCOUNT_ERROR_NONE) {
			_account_type_gslist_label_free(label_list);
			*error_code = rc;
			_ERR("finalize error");
			return NULL;
		}
		hstmt = NULL;
	}

	if (*error_code == _ACCOUNT_ERROR_RECORD_NOT_FOUND)
		label_list = _account_type_get_label_list_by_app_id_from_global_db(app_id, error_code);

	_INFO("Returning account label_list");

	return label_list;
}

int _account_type_query_label_by_app_id_from_global_db(account_type_label_cb callback, const char* app_id, void *user_data)
{
	int				error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0, binding_count = 1;

	_INFO("account_type_query_label_by_app_id_from_global_db start");

	ACCOUNT_RETURN_VAL((app_id != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("APP ID IS NULL"));
	ACCOUNT_RETURN_VAL((callback != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("NO CALLBACK FUNCTION"));
	ACCOUNT_RETURN_VAL((g_hAccountGlobalDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE AppId = ?", LABEL_TABLE);
	hstmt = _account_prepare_query(g_hAccountGlobalDB, query);

	if (_account_db_err_code(g_hAccountGlobalDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountGlobalDB));
		error_code = _ACCOUNT_ERROR_PERMISSION_DENIED;
		goto CATCH;
	}

	_account_query_bind_text(hstmt, binding_count++, app_id);

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR(rc == SQLITE_ROW, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	label_s* label_record = NULL;

	while (rc == SQLITE_ROW) {
		bool cb_ret = FALSE;
		label_record = (label_s*) malloc(sizeof(label_s));

		if (label_record == NULL) {
			ACCOUNT_FATAL("malloc Failed");
			break;
		}

		ACCOUNT_MEMSET(label_record, 0x00, sizeof(label_s));

		_account_type_convert_column_to_label(hstmt, label_record);

		cb_ret = callback(label_record->app_id, label_record->label , label_record->locale, user_data);

		_account_type_free_label_with_items(label_record);

		ACCOUNT_CATCH_ERROR(cb_ret == TRUE, {}, _ACCOUNT_ERROR_NONE, ("Callback func returs FALSE, its iteration is stopped!!!!\n"));

		rc = _account_query_step(hstmt);
	}

	error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		if (rc != _ACCOUNT_ERROR_NONE)
			_ERR("global db finalize error[%d]", rc);
		hstmt = NULL;
	}

	_INFO("account_type_query_label_by_app_id_from_global_db end [%d]", error_code);

	return error_code;
}

int _account_type_query_label_by_app_id(account_type_label_cb callback, const char* app_id, void *user_data)
{
	int				error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0, binding_count = 1;

	ACCOUNT_RETURN_VAL((app_id != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("APP ID IS NULL"));
	ACCOUNT_RETURN_VAL((callback != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("NO CALLBACK FUNCTION"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE AppId = ?", LABEL_TABLE);
	hstmt = _account_prepare_query(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	_account_query_bind_text(hstmt, binding_count++, app_id);

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR(rc == SQLITE_ROW, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	label_s* label_record = NULL;

	while (rc == SQLITE_ROW) {
		bool cb_ret = FALSE;
		label_record = (label_s*) malloc(sizeof(label_s));

		if (label_record == NULL) {
			ACCOUNT_FATAL("malloc Failed");
			break;
		}

		ACCOUNT_MEMSET(label_record, 0x00, sizeof(label_s));

		_account_type_convert_column_to_label(hstmt, label_record);

		cb_ret = callback(label_record->app_id, label_record->label , label_record->locale, user_data);

		_account_type_free_label_with_items(label_record);

		//ACCOUNT_CATCH_ERROR(cb_ret == TRUE, {}, _ACCOUNT_ERROR_NONE, ("Callback func returs FALSE, its iteration is stopped!!!!\n"));
		if (cb_ret != TRUE) {
			_INFO("Callback func returs FALSE, its iteration is stopped!!!!\n");
			break;
		}

		rc = _account_query_step(hstmt);
	}

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;
	}
/*
	if (error_code == _ACCOUNT_ERROR_RECORD_NOT_FOUND) {
		error_code = account_type_query_label_by_app_id_from_global_db(callback, app_id, user_data);
	}
*/
	return error_code;
}

int account_type_query_label_by_app_id(account_type_label_cb callback, const char* app_id, void *user_data)
{
	int error_code = _ACCOUNT_ERROR_NONE;

	ACCOUNT_RETURN_VAL((app_id != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("APP ID IS NULL"));
	ACCOUNT_RETURN_VAL((callback != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("NO CALLBACK FUNCTION"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	error_code = _account_type_query_label_by_app_id(callback, app_id, user_data);

	if (error_code == _ACCOUNT_ERROR_RECORD_NOT_FOUND)
		error_code = _account_type_query_label_by_app_id_from_global_db(callback, app_id, user_data);

	return error_code;
}

bool _account_get_label_text_cb(char* app_id, char* label, char* locale, void *user_data)
{
	account_type_s *data = (account_type_s*)user_data;

	label_s *label_data = (label_s*)malloc(sizeof(label_s));

	if (label_data == NULL) {
		ACCOUNT_FATAL("_account_get_label_text_cb : MALLOC FAIL\n");
		return FALSE;
	}
	ACCOUNT_MEMSET(label_data, 0, sizeof(label_s));

	label_data->app_id = _account_dup_text(app_id);
	label_data->label = _account_dup_text(label);
	label_data->locale = _account_dup_text(locale);

	data->label_list = g_slist_append(data->label_list, (gpointer)label_data);

	return TRUE;
}

bool _account_get_provider_feature_cb(char* app_id, char* key, void* user_data)
{
	account_type_s *data = (account_type_s*)user_data;

	provider_feature_s *feature_data = (provider_feature_s*)malloc(sizeof(provider_feature_s));

	if (feature_data == NULL) {
		ACCOUNT_FATAL("_account_get_provider_feature_cb : MALLOC FAIL\n");
		return FALSE;
	}
	ACCOUNT_MEMSET(feature_data, 0, sizeof(provider_feature_s));

	feature_data->app_id = _account_dup_text(app_id);
	feature_data->key = _account_dup_text(key);

	data->provider_feature_list = g_slist_append(data->provider_feature_list, (gpointer)feature_data);

	return TRUE;
}

int _account_type_query_by_app_id_from_global_db(const char* app_id, account_type_s** account_type_record)
{
	_INFO("_account_type_query_by_app_id_from_global_db start");

	int				error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0, binding_count = 1;

	ACCOUNT_RETURN_VAL((app_id != 0), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("APP ID IS NULL"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));
	ACCOUNT_RETURN_VAL((g_hAccountGlobalDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE AppId = ?", ACCOUNT_TYPE_TABLE);
	hstmt = _account_prepare_query(g_hAccountGlobalDB, query);

	if (_account_db_err_code(g_hAccountGlobalDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountGlobalDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	_account_query_bind_text(hstmt, binding_count++, app_id);

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR(rc == SQLITE_ROW, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	*account_type_record = create_empty_account_type_instance();

	while (rc == SQLITE_ROW) {
		_account_type_convert_column_to_account_type(hstmt, *account_type_record);
		rc = _account_query_step(hstmt);
	}

	rc = _account_query_finalize(hstmt);
	ACCOUNT_CATCH_ERROR((rc == _ACCOUNT_ERROR_NONE), {_ERR("global db finalize error rc=[%d]", rc); }, rc, ("finalize error"));
	_account_type_query_label_by_app_id_from_global_db(_account_get_label_text_cb, app_id, (void*)(*account_type_record));
	_account_type_query_provider_feature_cb_by_app_id_from_global_db(_account_get_provider_feature_cb, app_id, (void*)(*account_type_record));

	hstmt = NULL;
	error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;
	}

	_INFO("_account_type_query_by_app_id_from_global_db end [%d]", error_code);

	return error_code;
}

int _account_type_query_by_app_id(const char* app_id, account_type_s** account_type_record)
{
	_INFO("_account_type_query_by_app_id start");

	int				error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0, binding_count = 1;

	ACCOUNT_RETURN_VAL((app_id != 0), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("APP ID IS NULL"));
	ACCOUNT_RETURN_VAL((account_type_record != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("account type record(account_type_s**) is NULL"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE AppId = ?", ACCOUNT_TYPE_TABLE);
	hstmt = _account_prepare_query(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	_account_query_bind_text(hstmt, binding_count++, app_id);

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR(rc == SQLITE_ROW, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	*account_type_record = create_empty_account_type_instance();
	if (*account_type_record == NULL) {
		_ERR("Out of Memory");
		error_code = _ACCOUNT_ERROR_OUT_OF_MEMORY;
		goto CATCH;
	}

	while (rc == SQLITE_ROW) {
		_account_type_convert_column_to_account_type(hstmt, *account_type_record);
		rc = _account_query_step(hstmt);
	}

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	_account_type_query_label_by_app_id(_account_get_label_text_cb, app_id, (void*)(*account_type_record));
	_account_type_query_provider_feature_cb_by_app_id(_account_get_provider_feature_cb, app_id, (void*)(*account_type_record));

	hstmt = NULL;
	error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;
	}

	if (error_code == _ACCOUNT_ERROR_RECORD_NOT_FOUND)
		error_code = _account_type_query_by_app_id_from_global_db(app_id, account_type_record);

	_INFO("_account_type_query_by_app_id end [%d]", error_code);

	return error_code;
}

int _account_type_query_by_provider_feature_from_global_db(const char* key, GSList **account_type_list_all)
{
	int error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0;
	GSList			*account_type_list = NULL;

	_INFO("_account_type_query_by_provider_feature_from_global_db start key=%s", key);
	if (key == NULL) {
		ACCOUNT_ERROR("capability_type IS NULL.");
		error_code = _ACCOUNT_ERROR_INVALID_PARAMETER;
		goto CATCH;
	}

	if (g_hAccountGlobalDB == NULL) {
		ACCOUNT_ERROR("The database isn't connected.");
		error_code = _ACCOUNT_ERROR_DB_NOT_OPENED;
		goto CATCH;
	}

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE AppId IN (SELECT app_id from %s WHERE key=?)", ACCOUNT_TYPE_TABLE, PROVIDER_FEATURE_TABLE);

	hstmt = _account_prepare_query(g_hAccountGlobalDB, query);

	if (_account_db_err_code(g_hAccountGlobalDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountGlobalDB));
		error_code = _ACCOUNT_ERROR_PERMISSION_DENIED;
		goto CATCH;
	}

	int binding_count = 1;
	_account_query_bind_text(hstmt, binding_count++, key);

	rc = _account_query_step(hstmt);

	account_type_s *account_type_record = NULL;

	if (rc != SQLITE_ROW) {
		ACCOUNT_ERROR("The record isn't found. rc=[%d]", rc);
		error_code = _ACCOUNT_ERROR_RECORD_NOT_FOUND;
		goto CATCH;
	}

	while (rc == SQLITE_ROW) {
		account_type_record = (account_type_s*) malloc(sizeof(account_type_s));

		if (account_type_record == NULL) {
			ACCOUNT_FATAL("malloc Failed");
			break;
		}

		ACCOUNT_MEMSET(account_type_record, 0x00, sizeof(account_type_s));
		_account_type_convert_column_to_account_type(hstmt, account_type_record);
		account_type_list = g_slist_append(account_type_list, account_type_record);
		rc = _account_query_step(hstmt);
	}

	rc = _account_query_finalize(hstmt);
	if (rc != _ACCOUNT_ERROR_NONE) {
		_account_type_gslist_account_type_free(account_type_list);
		ACCOUNT_ERROR("finalize error(%s)", rc);
		error_code = rc;
		goto CATCH;
	}
	hstmt = NULL;

	GSList* iter;

	for (iter = account_type_list; iter != NULL; iter = g_slist_next(iter)) {
		account_type_s *account_type = NULL;
		account_type = (account_type_s*)iter->data;
		_account_type_query_label_by_app_id_from_global_db(_account_get_label_text_cb, account_type->app_id, (void*)account_type);
		_account_type_query_provider_feature_cb_by_app_id_from_global_db(_account_get_provider_feature_cb, account_type->app_id, (void*)account_type);
		_INFO("add label & provider_feature");
	}

	for (iter = account_type_list; iter != NULL; iter = g_slist_next(iter)) {

		account_type_s *account_type = NULL;
		account_type = (account_type_s*)iter->data;
		*account_type_list_all = g_slist_append(*account_type_list_all, account_type);
		_INFO("add account_type");
	}
	g_slist_free(account_type_list);

	error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		if (rc != _ACCOUNT_ERROR_NONE) {
			ACCOUNT_ERROR("finalize error(%s)", rc);
			return rc;
		}
		hstmt = NULL;
	}

	_INFO("_account_type_query_by_provider_feature_from_global_db end. error_code=[%d]", error_code);

	return error_code;
}

GSList* _account_type_query_by_provider_feature(const char* key, int *error_code)
{
	*error_code = _ACCOUNT_ERROR_NONE;
	account_stmt hstmt = NULL;
	char query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int rc = 0;
	GSList *account_type_list = NULL;

	_INFO("account_type_query_by_provider_feature start key=%s", key);
	if (key == NULL) {
		ACCOUNT_ERROR("capability_type IS NULL.");
		*error_code = _ACCOUNT_ERROR_INVALID_PARAMETER;
		goto CATCH;
	}

	if (g_hAccountDB == NULL) {
		ACCOUNT_ERROR("The database isn't connected.");
		*error_code = _ACCOUNT_ERROR_DB_NOT_OPENED;
		goto CATCH;
	}

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE AppId IN (SELECT app_id from %s WHERE key=?)", ACCOUNT_TYPE_TABLE, PROVIDER_FEATURE_TABLE);

	hstmt = _account_prepare_query(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		*error_code = _ACCOUNT_ERROR_PERMISSION_DENIED;
		goto CATCH;
	}

	int binding_count = 1;
	_account_query_bind_text(hstmt, binding_count++, key);

	rc = _account_query_step(hstmt);

	account_type_s *account_type_record = NULL;

	if (rc != SQLITE_ROW) {
		ACCOUNT_ERROR("The record isn't found. rc=[%d]", rc);
		*error_code = _ACCOUNT_ERROR_RECORD_NOT_FOUND;
		goto CATCH;
	}

	while (rc == SQLITE_ROW) {
		account_type_record = (account_type_s*) malloc(sizeof(account_type_s));

		if (account_type_record == NULL) {
			ACCOUNT_FATAL("malloc Failed");
			break;
		}

		ACCOUNT_MEMSET(account_type_record, 0x00, sizeof(account_type_s));
		_account_type_convert_column_to_account_type(hstmt, account_type_record);
		account_type_list = g_slist_append(account_type_list, account_type_record);
		rc = _account_query_step(hstmt);
	}

	rc = _account_query_finalize(hstmt);
	if (rc != _ACCOUNT_ERROR_NONE) {
		_account_type_gslist_account_type_free(account_type_list);
		ACCOUNT_ERROR("finalize error(%s)", rc);
		*error_code = rc;
		goto CATCH;
	}
	hstmt = NULL;

	GSList* iter;

	for (iter = account_type_list; iter != NULL; iter = g_slist_next(iter)) {
		account_type_s *account_type = NULL;
		account_type = (account_type_s*)iter->data;
		_account_type_query_label_by_app_id(_account_get_label_text_cb, account_type->app_id, (void*)account_type);
		_account_type_query_provider_feature_cb_by_app_id(_account_get_provider_feature_cb, account_type->app_id, (void*)account_type);
	}

	*error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		if (rc != _ACCOUNT_ERROR_NONE) {
			*error_code = rc;
			return NULL;
		}
		hstmt = NULL;
	}

	if (*error_code == _ACCOUNT_ERROR_NONE || *error_code == _ACCOUNT_ERROR_RECORD_NOT_FOUND) {
		rc = _account_type_query_by_provider_feature_from_global_db(key, &account_type_list);
		if (rc != _ACCOUNT_ERROR_NONE && rc != _ACCOUNT_ERROR_RECORD_NOT_FOUND) {
			ACCOUNT_ERROR("_account_type_query_by_provider_feature_from_global_db fail=[%d]", rc);
			_account_type_gslist_account_type_free(account_type_list);
			return NULL;
		}
		if (rc == _ACCOUNT_ERROR_NONE)
			*error_code = rc;
	}

	_INFO("account_type_query_by_provider_feature end");

	return account_type_list;
}

int _account_type_query_all_from_global_db(GSList **account_type_list_all)
{
	account_stmt	hstmt = NULL;
	char query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int rc = _ACCOUNT_ERROR_NONE;
	int error_code = _ACCOUNT_ERROR_NONE;
	GSList *account_type_list = NULL;

	_INFO("_account_type_query_all_in_global_db start");
	ACCOUNT_RETURN_VAL((g_hAccountGlobalDB != NULL), {}, NULL, ("The database isn't connected."));

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s ", ACCOUNT_TYPE_TABLE);
	hstmt = _account_prepare_query(g_hAccountGlobalDB, query);

	rc = _account_query_step(hstmt);

	if (_account_db_err_code(g_hAccountGlobalDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountGlobalDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	account_type_s *account_type_record = NULL;

	if (rc != SQLITE_ROW) {
		_INFO("[_ACCOUNT_ERROR_RECORD_NOT_FOUND]The record isn't found.");
		error_code = _ACCOUNT_ERROR_RECORD_NOT_FOUND;
		goto CATCH;
	}

	while (rc == SQLITE_ROW) {
		account_type_record = (account_type_s*) malloc(sizeof(account_type_s));

		if (account_type_record == NULL) {
			ACCOUNT_FATAL("malloc Failed");
			break;
		}

		ACCOUNT_MEMSET(account_type_record, 0x00, sizeof(account_type_s));
		_account_type_convert_column_to_account_type(hstmt, account_type_record);
		account_type_list = g_slist_append(account_type_list, account_type_record);
		rc = _account_query_step(hstmt);
	}

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {_account_type_gslist_account_type_free(account_type_list); }, rc, ("finalize error"));
	hstmt = NULL;

	GSList* iter;

	for (iter = account_type_list; iter != NULL; iter = g_slist_next(iter)) {
		account_type_s *account_type = NULL;
		account_type = (account_type_s*)iter->data;
		_account_type_query_label_by_app_id_from_global_db(_account_get_label_text_cb, account_type->app_id, (void*)account_type);
		_account_type_query_provider_feature_cb_by_app_id_from_global_db(_account_get_provider_feature_cb, account_type->app_id, (void*)account_type);
	}

	for (iter = account_type_list; iter != NULL; iter = g_slist_next(iter)) {
		account_type_s *account_type = NULL;
		account_type = (account_type_s*)iter->data;
		*account_type_list_all = g_slist_append(*account_type_list_all, account_type);
	}
	g_slist_free(account_type_list);

	error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {_account_type_gslist_account_type_free(account_type_list); }, rc, ("finalize error"));
		hstmt = NULL;
	}

	_INFO("_account_type_query_all_in_global_db end");

	return error_code;
}

GSList* _account_type_query_all(void)
{
	account_stmt hstmt = NULL;
	char query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int rc = 0;
	int error_code = _ACCOUNT_ERROR_NONE;
	GSList *account_type_list = NULL;

	_INFO("_account_type_query_all start");
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, NULL, ("The database isn't connected."));

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s ", ACCOUNT_TYPE_TABLE);
	hstmt = _account_prepare_query(g_hAccountDB, query);

	rc = _account_query_step(hstmt);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return NULL;
	}

	account_type_s *account_type_record = NULL;

	if (rc != SQLITE_ROW) {
		_INFO("[_ACCOUNT_ERROR_RECORD_NOT_FOUND]The record isn't found.");
		error_code = _ACCOUNT_ERROR_RECORD_NOT_FOUND;
		goto CATCH;
	}

	while (rc == SQLITE_ROW) {
		account_type_record = (account_type_s*) malloc(sizeof(account_type_s));

		if (account_type_record == NULL) {
			ACCOUNT_FATAL("malloc Failed");
			break;
		}

		ACCOUNT_MEMSET(account_type_record, 0x00, sizeof(account_type_s));
		_account_type_convert_column_to_account_type(hstmt, account_type_record);
		account_type_list = g_slist_append(account_type_list, account_type_record);
		rc = _account_query_step(hstmt);
	}

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {_account_type_gslist_account_type_free(account_type_list); }, NULL, ("finalize error"));
	hstmt = NULL;

	GSList* iter;

	for (iter = account_type_list; iter != NULL; iter = g_slist_next(iter)) {
		account_type_s *account_type = NULL;
		account_type = (account_type_s*)iter->data;
		_account_type_query_label_by_app_id(_account_get_label_text_cb, account_type->app_id, (void*)account_type);
		_account_type_query_provider_feature_cb_by_app_id(_account_get_provider_feature_cb, account_type->app_id, (void*)account_type);
	}

	error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {_account_type_gslist_account_type_free(account_type_list); }, NULL, ("finalize error"));
		hstmt = NULL;
	}

	if (error_code == _ACCOUNT_ERROR_NONE || error_code == _ACCOUNT_ERROR_RECORD_NOT_FOUND) {
		error_code = _account_type_query_all_from_global_db(&account_type_list);
		if (rc != _ACCOUNT_ERROR_NONE && rc != _ACCOUNT_ERROR_RECORD_NOT_FOUND) {
			ACCOUNT_ERROR("_account_type_query_all_from_global_db fail=[%d]", rc);
			_account_type_gslist_account_type_free(account_type_list);
			return NULL;
		}
	}

	_INFO("_account_type_query_all end");

	return account_type_list;
}

// output parameter label must be free
int _account_type_query_label_by_locale_from_global_db(const char* app_id, const char* locale, char **label)
{
	int				error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0, binding_count = 1;
	char*			converted_locale = NULL;

	ACCOUNT_RETURN_VAL((app_id != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("NO APP ID"));
	ACCOUNT_RETURN_VAL((g_hAccountGlobalDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));
	ACCOUNT_RETURN_VAL((label != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("label char is null"));
	ACCOUNT_RETURN_VAL((locale != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("locale char is null"));
	//Making label newly created

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	converted_locale = _account_dup_text(locale);
	gchar** tokens = g_strsplit(converted_locale, "-", 2);

	if (tokens != NULL) {
		if ((char*)(tokens[1]) != NULL) {
			char* upper_token = g_ascii_strup(tokens[1], strlen(tokens[1]));
			if (upper_token != NULL) {
				_ACCOUNT_FREE(converted_locale);
				converted_locale = g_strdup_printf("%s_%s", tokens[0], upper_token);
			}
			_ACCOUNT_FREE(upper_token);
		}
	}
	g_strfreev(tokens);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE AppId = ? AND Locale = '%s' ", LABEL_TABLE, converted_locale);
	_ACCOUNT_FREE(converted_locale);

	hstmt = _account_prepare_query(g_hAccountGlobalDB, query);

	if (_account_db_err_code(g_hAccountGlobalDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountGlobalDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	_account_query_bind_text(hstmt, binding_count++, app_id);

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR(rc == SQLITE_ROW, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	label_s* label_record = NULL;

	while (rc == SQLITE_ROW) {
		label_record = (label_s*) malloc(sizeof(label_s));

		if (label_record == NULL) {
			ACCOUNT_FATAL("malloc Failed");
			break;
		}

		ACCOUNT_MEMSET(label_record, 0x00, sizeof(label_s));

		_account_type_convert_column_to_label(hstmt, label_record);

		_ACCOUNT_FREE(*label);
		//Making label newly created
		*label = _account_dup_text(label_record->label);

		_account_type_free_label_with_items(label_record);

		rc = _account_query_step(hstmt);
	}

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;
	}

	_INFO("_account_type_query_label_by_locale_from_global_db() end : error_code = %d", error_code);

	return error_code;
}

// output parameter label must be free
int _account_type_query_label_by_locale(const char* app_id, const char* locale, char **label)
{
	int				error_code = _ACCOUNT_ERROR_NONE;
	account_stmt	hstmt = NULL;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	int				rc = 0, binding_count = 1;
	char*			converted_locale = NULL;

	ACCOUNT_RETURN_VAL((app_id != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("NO APP ID"));
	ACCOUNT_RETURN_VAL((g_hAccountDB != NULL), {}, _ACCOUNT_ERROR_DB_NOT_OPENED, ("The database isn't connected."));
	ACCOUNT_RETURN_VAL((label != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("label char is null"));
	ACCOUNT_RETURN_VAL((locale != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("locale char is null"));
	//Making label newly created

	ACCOUNT_MEMSET(query, 0x00, ACCOUNT_SQL_LEN_MAX);

	converted_locale = _account_dup_text(locale);
	gchar** tokens = g_strsplit(converted_locale, "-", 2);

	if (tokens != NULL) {
		if ((char*)(tokens[1]) != NULL) {
			char* upper_token = g_ascii_strup(tokens[1], strlen(tokens[1]));
			if (upper_token != NULL) {
				_ACCOUNT_FREE(converted_locale);
				converted_locale = g_strdup_printf("%s_%s", tokens[0], upper_token);
			}
			_ACCOUNT_FREE(upper_token);
		}
	}
	g_strfreev(tokens);

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT * FROM %s WHERE AppId = ? AND Locale = '%s' ", LABEL_TABLE, converted_locale);
	_ACCOUNT_FREE(converted_locale);

	hstmt = _account_prepare_query(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	_account_query_bind_text(hstmt, binding_count++, app_id);

	rc = _account_query_step(hstmt);
	ACCOUNT_CATCH_ERROR(rc == SQLITE_ROW, {}, _ACCOUNT_ERROR_RECORD_NOT_FOUND, ("The record isn't found.\n"));

	label_s* label_record = NULL;

	while (rc == SQLITE_ROW) {
		label_record = (label_s*) malloc(sizeof(label_s));

		if (label_record == NULL) {
			ACCOUNT_FATAL("malloc Failed");
			break;
		}

		ACCOUNT_MEMSET(label_record, 0x00, sizeof(label_s));

		_account_type_convert_column_to_label(hstmt, label_record);

		_ACCOUNT_FREE(*label);
		//Making label newly created
		*label = _account_dup_text(label_record->label);

		_account_type_free_label_with_items(label_record);

		rc = _account_query_step(hstmt);
	}

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	error_code = _ACCOUNT_ERROR_NONE;

CATCH:
	if (hstmt != NULL) {
		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;
	}

	if (error_code == _ACCOUNT_ERROR_RECORD_NOT_FOUND)
		error_code = _account_type_query_label_by_locale_from_global_db(app_id, locale, label);

	_INFO("_account_type_query_label_by_locale() end : error_code = %d", error_code);

	return error_code;
}

static int _account_insert_custom(account_s *account, int account_id)
{
	_INFO("_account_insert_custom start");

	int				rc, count = 1;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	account_stmt	hstmt = NULL;

	ACCOUNT_RETURN_VAL((account != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("ACCOUNT HANDLE IS NULL"));

	if (g_slist_length(account->custom_list) == 0) {
		ACCOUNT_DEBUG("_account_insert_custom, no custom data\n");
		return _ACCOUNT_ERROR_NONE;
	}

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT COUNT(*) from %s where _id=%d", ACCOUNT_TABLE, account_id);

	rc = _account_get_record_count(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%d, %s)", _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	}

	if (rc <= 0) {
		ACCOUNT_SLOGE("_account_insert_custom : related account item is not existed rc=%d , %s", rc, _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_RECORD_NOT_FOUND;
	}

	/* insert query*/

	GSList *iter;

	for (iter = account->custom_list; iter != NULL; iter = g_slist_next(iter)) {
		int ret;
		count = 1;
		ACCOUNT_MEMSET(query, 0x00, sizeof(query));
		ACCOUNT_SNPRINTF(query, sizeof(query), "INSERT INTO %s (AccountId, AppId, Key, Value) VALUES "
				"(?, ?, ?, ?) ", ACCOUNT_CUSTOM_TABLE);

		hstmt = _account_prepare_query(g_hAccountDB, query);

		if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
			ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
			return _ACCOUNT_ERROR_PERMISSION_DENIED;
		}

		ACCOUNT_RETURN_VAL((hstmt != NULL), {}, _ACCOUNT_ERROR_DB_FAILED, ("_account_prepare_query(g_hAccountDB, ) failed(%s).\n", _account_db_err_msg(g_hAccountDB)));

		account_custom_s* custom_data = NULL;
		custom_data = (account_custom_s*)iter->data;

		ret = _account_query_bind_int(hstmt, count++, account_id);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Int binding fail"));
		ret = _account_query_bind_text(hstmt, count++, account->package_name);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));
		ret = _account_query_bind_text(hstmt, count++, (char*)custom_data->key);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));
		ret = _account_query_bind_text(hstmt, count++, (char*)custom_data->value);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));

		rc = _account_query_step(hstmt);

		if (rc != SQLITE_DONE) {
			ACCOUNT_ERROR("_account_query_step() failed(%d, %s)", rc, _account_db_err_msg(g_hAccountDB));
			break;
		}

		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;

	}

	_INFO("_account_insert_custom end");

	return _ACCOUNT_ERROR_NONE;
}

static int _account_update_custom(account_s *account, int account_id)
{
	int				rc, count = 1;
	char			query[ACCOUNT_SQL_LEN_MAX] = {0, };
	account_stmt	hstmt = NULL;

	ACCOUNT_RETURN_VAL((account != NULL), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("ACCOUNT HANDLE IS NULL"));

	if (g_slist_length(account->custom_list) == 0) {
		ACCOUNT_DEBUG("_account_update_custom, no custom data\n");
		return _ACCOUNT_ERROR_NONE;
	}

	ACCOUNT_SNPRINTF(query, sizeof(query), "SELECT COUNT(*) from %s where _id=%d", ACCOUNT_TABLE, account_id);

	rc = _account_get_record_count(g_hAccountDB, query);

	if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
		ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
		pthread_mutex_unlock(&account_mutex);
		return _ACCOUNT_ERROR_PERMISSION_DENIED;
	} else if (_account_db_err_code(g_hAccountDB) == SQLITE_BUSY) {
		ACCOUNT_ERROR("database busy(%s)", _account_db_err_msg(g_hAccountDB));
		pthread_mutex_unlock(&account_mutex);
		return _ACCOUNT_ERROR_DATABASE_BUSY;
	}

	if (rc <= 0) {
		ACCOUNT_SLOGE("_account_update_custom : related account item is not existed rc=%d , %s", rc, _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_RECORD_NOT_FOUND;
	}

	ACCOUNT_MEMSET(query, 0x00, sizeof(query));

	ACCOUNT_SNPRINTF(query, sizeof(query), "DELETE FROM %s WHERE AccountId=? ", ACCOUNT_CUSTOM_TABLE);
	hstmt = _account_prepare_query(g_hAccountDB, query);
	count = 1;
	_account_query_bind_int(hstmt, count++, (int)account_id);
	rc = _account_query_step(hstmt);

	if (rc == SQLITE_BUSY) {
		ACCOUNT_ERROR("_account_query_step() failed(%d, %s)", rc, _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_DATABASE_BUSY;
	} else if (rc != SQLITE_DONE) {
		ACCOUNT_ERROR("_account_query_step() failed(%d, %s)", rc, _account_db_err_msg(g_hAccountDB));
		return _ACCOUNT_ERROR_DB_FAILED;
	}

	rc = _account_query_finalize(hstmt);
	ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
	hstmt = NULL;

	GSList *iter;

	for (iter = account->custom_list; iter != NULL; iter = g_slist_next(iter)) {
		int ret;
		count = 1;
		ACCOUNT_MEMSET(query, 0x00, sizeof(query));
		ACCOUNT_SNPRINTF(query, sizeof(query), "INSERT INTO %s(AccountId, AppId, Key, Value) VALUES "
				"(?, ?, ?, ?) ", ACCOUNT_CUSTOM_TABLE);

		hstmt = _account_prepare_query(g_hAccountDB, query);

		if (_account_db_err_code(g_hAccountDB) == SQLITE_PERM) {
			ACCOUNT_ERROR("Access failed(%s)", _account_db_err_msg(g_hAccountDB));
			return _ACCOUNT_ERROR_PERMISSION_DENIED;
		}

		ACCOUNT_RETURN_VAL((hstmt != NULL), {}, _ACCOUNT_ERROR_DB_FAILED, ("_account_prepare_query(g_hAccountDB, ) failed(%s).\n", _account_db_err_msg(g_hAccountDB)));

		account_custom_s* custom_data = NULL;
		custom_data = (account_custom_s*)iter->data;

		ret = _account_query_bind_int(hstmt, count++, (int)account_id);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Int binding fail"));
		ret = _account_query_bind_text(hstmt, count++, (char*)account->package_name);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));
		ret = _account_query_bind_text(hstmt, count++, (char*)custom_data->key);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));
		ret = _account_query_bind_text(hstmt, count++, (char*)custom_data->value);
		ACCOUNT_RETURN_VAL((ret == _ACCOUNT_ERROR_NONE), {}, _ACCOUNT_ERROR_INVALID_PARAMETER, ("Text binding fail"));

		rc = _account_query_step(hstmt);

		if (rc != SQLITE_DONE) {
			ACCOUNT_ERROR("_account_query_step() failed(%d, %s)", rc, _account_db_err_msg(g_hAccountDB));
			break;
		}

		rc = _account_query_finalize(hstmt);
		ACCOUNT_RETURN_VAL((rc == _ACCOUNT_ERROR_NONE), {}, rc, ("finalize error"));
		hstmt = NULL;

	}

	return _ACCOUNT_ERROR_NONE;
}

