/*
  Copyright (c) 2021 Sogou, Inc.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  Author: Li Yingxin (liyingxin@sogou-inc.com)
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <chrono>
#include <mutex>
#include <gtest/gtest.h>
#include "workflow/WFTask.h"
#include "workflow/WFTaskFactory.h"
//#include "workflow/WFCondition.h"
#include "workflow/WFSemaphore.h"
#include "workflow/WFSemTaskFactory.h"
#include "workflow/WFFacilities.h"

TEST(condition_unittest, signal)
{
	WFCondition cond;
	std::mutex mutex;
	int ret = 1979;
	int *ptr = &ret;

	auto *task1 = WFSemTaskFactory::create_wait_task(&cond, [&ptr](WFMailboxTask *) {
		*ptr = 1412;
		fprintf(stderr, "task1 callback\n");
	});
	SeriesWork *series1 = Workflow::create_series_work(task1, nullptr);

	auto *task2 = WFSemTaskFactory::create_wait_task(&cond, [&ptr](WFMailboxTask *) {
		*ptr = 2;
		fprintf(stderr, "task2 callback\n");
	});
	SeriesWork *series2 = Workflow::create_series_work(task2, nullptr);

	series1->start();
	series2->start();

	mutex.lock();
	cond.signal(NULL);
	mutex.unlock();
	usleep(10000);
	EXPECT_EQ(ret, 1412);
	cond.signal(NULL);
	usleep(10000);
	EXPECT_EQ(ret, 2);
}

TEST(condition_unittest, broadcast)
{
	WFCondition cond;
	std::mutex mutex;
	int ret = 0;
	int *ptr = &ret;

	auto *task1 = WFSemTaskFactory::create_wait_task(&cond, [&ptr](WFMailboxTask *) {
		(*ptr)++;
		fprintf(stderr, "task1 callback\n");
	});
	SeriesWork *series1 = Workflow::create_series_work(task1, nullptr);

	auto *task2 = WFSemTaskFactory::create_wait_task(&cond, [&ptr](WFMailboxTask *) {
		(*ptr)++;
		fprintf(stderr, "task2 callback\n");
	});
	SeriesWork *series2 = Workflow::create_series_work(task2, nullptr);

	series1->start();
	series2->start();

	cond.broadcast(NULL);
	usleep(10000);
	EXPECT_EQ(ret, 2);
}

TEST(condition_unittest, timedwait)
{
	WFFacilities::WaitGroup wait_group(1);
	struct timespec ts;
	ts.tv_sec = 2;
	ts.tv_nsec = 0;

	auto *task1 = WFSemTaskFactory::create_timedwait_task("timedwait", &ts,
		[&wait_group](WFMailboxTask *task) {
		EXPECT_EQ(task->get_error(), ETIMEDOUT);
		wait_group.done();
	});

	Workflow::start_series_work(task1, nullptr);
	wait_group.wait();
	usleep(10000);
}

