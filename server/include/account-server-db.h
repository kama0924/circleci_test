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

#ifndef __ACC_SERVER_DB_H__

#include <account-private.h>

int _account_insert_to_db(account_s* account, int pid, uid_t uid, int *account_id);
int _account_db_open(int mode, int pid, uid_t uid);
int _account_db_close(void);
int _account_global_db_open(void);
int _account_global_db_close(void);
int account_server_insert_account_type_to_user_db(account_type_s* account_type, int* account_type_id, uid_t uid);
int account_server_delete_account_type_by_app_id_from_user_db(const char * app_id);
GSList* _account_db_query_all(int pid, uid_t uid);
GSList* _account_type_query_all(void);
int _account_delete(int pid, uid_t uid, int account_id);
int _account_delete_from_db_by_user_name(int pid, uid_t uid, const char *user_name, const char *package_name);
//int _account_delete_from_db_by_package_name(int pid, uid_t uid, const char *package_name, gboolean permission);
int _account_update_to_db_by_id(int pid, uid_t uid, account_s *account, int account_id);
int _account_get_total_count_from_db(gboolean include_hidden, int *count);
int _account_query_account_by_account_id(int pid, uid_t uid, int account_db_id, account_s *account_record);
int _account_update_to_db_by_user_name(int pid, uid_t uid, account_s* account, const char *user_name, const char *package_name);
int _account_type_query_label_by_locale(const char* app_id, const char* locale, char **label);
GSList* _account_type_query_by_provider_feature(const char* key, int *error_code);
GList* _account_query_account_by_user_name(int pid, uid_t uid, const char *user_name, int *error_code);
GList* account_server_query_account_by_package_name(const char* package_name, int *error_code, int pid, uid_t uid);
GList* _account_query_account_by_capability(int pid, uid_t uid, const char* capability_type, const int capability_value, int *error_code);
GList* _account_query_account_by_capability_type(int pid, uid_t uid, const char* capability_type, int *error_code);
GSList* _account_get_capability_list_by_account_id(int account_id, int *error_code);
int _account_update_sync_status_by_id(uid_t uid, int account_db_id, const int sync_status);
GSList* _account_type_query_provider_feature_by_app_id(const char* app_id, int *error_code);
bool _account_type_query_supported_feature(const char* app_id, const char* capability, int *error_code);
int _account_type_update_to_db_by_app_id(account_type_s *account_type, const char* app_id);
GSList* _account_type_get_label_list_by_app_id(const char* app_id, int *error_code);
int _account_type_query_by_app_id(const char* app_id, account_type_s **account_type_record);
int _account_update_to_db_by_id_ex(account_s *account, int account_id);

GList* account_server_query_account_by_package_name(const char* package_name, int *error_code, int pid, uid_t uid);
int account_server_delete_account_by_package_name(const char* package_name, bool permission, int pid, uid_t uid);
int account_server_query_app_id_exist(const char *app_id);

#endif
