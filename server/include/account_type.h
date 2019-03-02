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

#ifndef __ACCOUNT_TYPE_H__
#define __ACCOUNT_TYPE_H__

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum {
	_ACCOUNT_CAPABILITY_STATE_INVALID = 0, /**< Account capability is invalid */
	_ACCOUNT_CAPABILITY_DISABLED, /**< Account capability is disabled */
	_ACCOUNT_CAPABILITY_ENABLED, /**< Account capability is enabled */
	_ACCOUNT_CAPABILITY_STATE_MAX
} _account_capability_state_e;

typedef enum {
	_ACCOUNT_SECRECY_INVALID = 0, /**< Account secrecy is invalid */
	_ACCOUNT_SECRECY_INVISIBLE, /**< Account is not visible */
	_ACCOUNT_SECRECY_VISIBLE, /**< Account is visible */
	_ACCOUNT_SECRECY_MAX
} _secrecy_state_e;

typedef enum {
	_ACCOUNT_SYNC_INVALID = 0, /**< Account sync is invalid */
	_ACCOUNT_SYNC_NOT_SUPPORT,  /**< Account sync not supported */
	_ACCOUNT_SYNC_STATUS_OFF, /**< Account sync supported but all synchronization functionalities are off */
	_ACCOUNT_SYNC_STATUS_IDLE, /**< Account sync support and sync status is idle */
	_ACCOUNT_SYNC_STATUS_RUNNING, /**< Account sync support and sync status is running */
	_ACCOUNT_SUPPORTS_SYNC, /**<  NOT USED, WILL BE REMOVED TO PREVENT BUILD ERROR */
	_ACCOUNT_NOT_SUPPORTS_SYNC, /**<  NOT USED, WILL BE REMOVED TO PREVENT BUILD ERROR */
	_ACCOUNT_SYNC_MAX
} _account_sync_state_e;

typedef enum {
	_ACCOUNT_AUTH_TYPE_INVALID = 0, /**< Auth type is invalid */
	_ACCOUNT_AUTH_TYPE_XAUTH, /**< XAuth type */
	_ACCOUNT_AUTH_TYPE_OAUTH, /**< OAuth type */
	_ACCOUNT_AUTH_TYPE_CLIENT_LOGIN, /**< Client-Login type */
	_ACCOUNT_AUTH_TYPE_MAX
} _account_auth_type_e;

#define _ACCOUNT_NOTI_NAME_INSERT        "insert"

#define _ACCOUNT_NOTI_NAME_UPDATE        "update"

#define _ACCOUNT_NOTI_NAME_DELETE        "delete"

#define _ACCOUNT_NOTI_NAME_SYNC_UPDATE   "sync_update"

typedef bool (*account_type_label_cb)(char* app_id, char* label, char* locale, void *user_data);
typedef bool (*account_type_provider_feature_cb)(char* app_id, char* key, void* user_data);

#ifdef __cplusplus
}
#endif

#endif /* __ACCOUNT_TYPE_H__*/

