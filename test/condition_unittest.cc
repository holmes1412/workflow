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
#include "workflow/WFSemaphore.h"
#include "workflow/WFSemTaskFactory.h"
#include "workflow/WFFacilities.h"

TEST(condition_unittest, signal)
{
	WFCondition cond;
	std::mutex mutex;
	WFFacilities::WaitGroup wg(1);
	int ret = 3;
	int *ptr = &ret;

	auto *task1 = WFSemTaskFactory::create_wait_task(&cond, [&wg, &ptr](WFMailboxTask *) {
		*ptr = 1;
		wg.done();
	});

	auto *task2 = WFSemTaskFactory::create_wait_task(&cond, [&ptr](WFMailboxTask *) {
		*ptr = 2;
	});

	SeriesWork *series1 = Workflow::create_series_work(task1, nullptr);
	SeriesWork *series2 = Workflow::create_series_work(task2, nullptr);

	series1->start();
	series2->start();

	mutex.lock();
	cond.signal(NULL);
	mutex.unlock();
	wg.wait();
	EXPECT_EQ(ret, 1);
	cond.signal(NULL);
	usleep(1000);
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
	});
	SeriesWork *series1 = Workflow::create_series_work(task1, nullptr);

	auto *task2 = WFSemTaskFactory::create_wait_task(&cond, [&ptr](WFMailboxTask *) {
		(*ptr)++;
	});
	SeriesWork *series2 = Workflow::create_series_work(task2, nullptr);

	series1->start();
	series2->start();

	cond.broadcast(NULL);
	usleep(1000);
	EXPECT_EQ(ret, 2);
}

TEST(condition_unittest, timedwait)
{
	WFFacilities::WaitGroup wait_group(2);
	struct timespec ts;
	ts.tv_sec = 1;
	ts.tv_nsec = 0;

	auto *task1 = WFSemTaskFactory::create_timedwait_task("timedwait1", &ts,
		[&wait_group](WFMailboxTask *task) {
		EXPECT_EQ(task->get_error(), ETIMEDOUT);
		wait_group.done();
	});

	auto *task2 = WFSemTaskFactory::create_timedwait_task("timedwait2", &ts,
		[&wait_group](WFMailboxTask *task) {
		EXPECT_EQ(task->get_error(), 0);
		void **msg;
		size_t n;
		msg = task->get_mailbox(&n);
		EXPECT_EQ(n, 1);
		EXPECT_TRUE(strcmp((char *)*msg, "wake up!!") == 0);
		wait_group.done();
	});

	Workflow::start_series_work(task1, nullptr);
	Workflow::start_series_work(task2, nullptr);

	usleep(1000);
	char msg[10] = "wake up!!";
	WFSemTaskFactory::signal_by_name("timedwait2", msg);
	wait_group.wait();
}

TEST(condition_unittest, semaphore)
{
	int sem_concurrency = 1;
	int task_concurrency = 3;
	const char *words[3] = {"workflow", "srpc", "pyworkflow"};
	WFSemaphore sem(sem_concurrency, (void **)words);
	WFFacilities::WaitGroup wg(task_concurrency);

	for (int i = 0; i < task_concurrency; i++)
	{
		auto *t = WFTaskFactory::create_timer_task(i * 1, [&sem, &wg](WFTimerTask *task) {
			uint64_t id = (uint64_t)series_of(task)->get_context();
			void *dest_res;

			if (!sem.get(&dest_res))
			{
				auto *waiter = sem.create_wait_task([&sem, &wg](WFWaitTask *task) {
					uint64_t id = (uint64_t)series_of(task)->get_context();
					void **msg;
					size_t n;
					msg = task->get_mailbox(&n);
					fprintf(stderr, "%lu after wait. get resource:%s\n", id, (char *)*msg);
					usleep(10);
					wg.done();
					sem.post(*msg);
					//sem.post(reinterpret_cast<uint64_t *>(id));
				});
				series_of(task)->push_back(waiter);
			}
			else
			{
				fprintf(stderr, "%lu no wait. get resource: %s\n", id, (char *)dest_res);
				usleep(10);
				wg.done();
				//sem.post(reinterpret_cast<uint64_t *>(id));
				sem.post(dest_res);
			}
		});

		SeriesWork *series = Workflow::create_series_work(t, nullptr);
		series->set_context(reinterpret_cast<uint64_t *>(i));
		series->start();
	}
	wg.wait();
}
