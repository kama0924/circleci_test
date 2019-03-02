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

#include <unistd.h>
#include <stdbool.h>
#include <glib.h>
#include <pthread.h>
#include <dlog.h>
#include <dbg.h>

#define TIMEOUT 20

static int method_call_count = 0;
static int timer_count = 0;
static bool is_running_timer = false;
static pthread_mutex_t lifecycle_mutex = PTHREAD_MUTEX_INITIALIZER;

void terminate_main_loop();

void *lifecycle_termination_timer()
{
	while (TIMEOUT >= timer_count) {
		pthread_mutex_lock(&lifecycle_mutex);
		timer_count++;
		pthread_mutex_unlock(&lifecycle_mutex);
		_INFO("while timer_count = [%d]", timer_count);
		sleep(1);
	}

	if (method_call_count <= 0)
		terminate_main_loop();
	else
		_ERR("account method_call_count > 0");

	pthread_detach(pthread_self());

	pthread_mutex_lock(&lifecycle_mutex);
	is_running_timer = false;
	pthread_mutex_unlock(&lifecycle_mutex);
	pthread_exit(NULL);
}


void lifecycle_method_call_active()
{
	pthread_mutex_lock(&lifecycle_mutex);

	method_call_count++;
	_INFO("account lifecycle_method_call_active method_call_count = [%d]", method_call_count);

	pthread_mutex_unlock(&lifecycle_mutex);
}

void lifecycle_method_call_inactive()
{
	pthread_t curThread;

	pthread_mutex_lock(&lifecycle_mutex);

	method_call_count--;
	_INFO("account lifecycle_method_call_inactive method_call_count = [%d]", method_call_count);
	pthread_mutex_unlock(&lifecycle_mutex);

	if (method_call_count <= 0) {
		if (!is_running_timer) {
			pthread_mutex_lock(&lifecycle_mutex);
			pthread_mutex_unlock(&lifecycle_mutex);
			int ret = pthread_create(&curThread, NULL, lifecycle_termination_timer, NULL);
			_INFO("account create timer thread");
			if (ret != 0) {
				_ERR("account pthread_create fail!");
			} else {
				pthread_mutex_lock(&lifecycle_mutex);
				is_running_timer = true;
				pthread_mutex_unlock(&lifecycle_mutex);
			}
		} else {
			pthread_mutex_lock(&lifecycle_mutex);
			timer_count = 0;
			_INFO("account timer thread already is running. set timer_count = 0");
			pthread_mutex_unlock(&lifecycle_mutex);
		}
	}
}


