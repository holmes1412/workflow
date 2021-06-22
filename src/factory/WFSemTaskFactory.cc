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

#include <mutex>
#include <time.h>
#include <functional>
#include "list.h"
#include "WFTask.h"
#include "WFTaskFactory.h"
#include "WFGlobal.h"
#include "WFSemTaskFactory.h"

class __WFCondition : public WFCondition
{
public:
	__WFCondition(const std::string& str) :
		name(str)
	{ }

	struct rb_node rb;
	std::string name;
};

class __ConditionMap
{
public:
	void signal(const std::string& name, void *msg);
	void broadcast(const std::string& name, void *msg);

	WFMailboxTask *create(const std::string& name,
						  mailbox_callback_t&& cb);
	WFMailboxTask *create(const std::string& name,
						  const struct timespec *abstime,
						  mailbox_callback_t&& cb);

	WFMailboxTask *create_switch(const std::string& name,
								 mailbox_callback_t&& cb);
	WFMailboxTask *create_switch(const std::string& name,
								 const struct timespec *abstime,
								 mailbox_callback_t&& cb);

public:
	static __ConditionMap *get_instance()
	{
		static __ConditionMap kInstance;
		return &kInstance;
	}

	virtual ~__ConditionMap();

private:
	__ConditionMap()
	{
		this->condition_map.rb_node = NULL;
	}

	__WFCondition *find_condition(const std::string& name);
	struct rb_root condition_map;
	std::mutex mutex;
};

void __ConditionMap::signal(const std::string& name, void *msg)
{
	__WFCondition *cond = this->find_condition(name);

	cond->signal(msg);
}

void __ConditionMap::broadcast(const std::string& name, void *msg)
{
	__WFCondition *cond = this->find_condition(name);

	cond->broadcast(msg);
}

WFMailboxTask *__ConditionMap::create(const std::string& name,
									  mailbox_callback_t&& cb)
{
	__WFCondition *cond = this->find_condition(name);

	return WFSemTaskFactory::create_wait_task(cond, std::move(cb));
}

WFMailboxTask *__ConditionMap::create(const std::string& name,
									  const struct timespec *abstime,
									  mailbox_callback_t&& cb)
{
	__WFCondition *cond = this->find_condition(name);

	return WFSemTaskFactory::create_timedwait_task(cond, abstime,
												   std::move(cb));
}

WFMailboxTask *__ConditionMap::create_switch(const std::string& name,
											 mailbox_callback_t&& cb)
{
	__WFCondition *cond = this->find_condition(name);

	return WFSemTaskFactory::create_switch_wait_task(cond, std::move(cb));
}

WFMailboxTask *__ConditionMap::create_switch(const std::string& name,
											 const struct timespec *abstime,
											 mailbox_callback_t&& cb)
{
	__WFCondition *cond = this->find_condition(name);

	return WFSemTaskFactory::create_switch_timedwait_task(cond, abstime,
														  std::move(cb));
}

__ConditionMap::~__ConditionMap()
{
	__WFCondition *cond;
	WFWaitTask *task;
	struct list_head *pos;
	struct list_head *tmp;

	while (this->condition_map.rb_node)
	{
		cond = rb_entry(this->condition_map.rb_node, __WFCondition, rb);
		list_for_each_safe(pos, tmp, &cond->waiter_list)
		{
			task = list_entry(pos, WFWaitTask, list);
			list_del(pos);
			delete task;
		}

		rb_erase(this->condition_map.rb_node, &this->condition_map);
		delete cond;
	}
}

__WFCondition *__ConditionMap::find_condition(const std::string& name)
{
	struct rb_node **p = &this->condition_map.rb_node;
	struct rb_node *parent = NULL;
	__WFCondition *cond;

	this->mutex.lock();
	while (*p)
	{
		parent = *p;
		cond = rb_entry(*p, __WFCondition, rb);

		if (name < cond->name)
			p = &(*p)->rb_left;
		else if (name > cond->name)
			p = &(*p)->rb_right;
		else
			break;
	}

	if (*p == NULL)
	{
		cond = new __WFCondition(name);
		rb_link_node(&cond->rb, parent, p);
		rb_insert_color(&cond->rb, &this->condition_map);
	}

	this->mutex.unlock();

	return cond;
}

/////////////// factory api ///////////////
/*
SubTask *acquire_by_name(const std::string& name, mailbox_callback_t callback)
{
	return __SemaphoreMap::get_instance()->acquire(name, std::move(callback));
}

void release_by_name(const std::string& name, void *msg)
{
	return __SemaphoreMap::get_instance()->release(name, msg);
}
*/
void WFSemTaskFactory::signal_by_name(const std::string& name, void *msg)
{
	return __ConditionMap::get_instance()->signal(name, msg);
}

void WFSemTaskFactory::broadcast_by_name(const std::string& name, void *msg)
{
	return __ConditionMap::get_instance()->broadcast(name, msg);
}

WFMailboxTask *WFSemTaskFactory::create_wait_task(const std::string& name,
												  mailbox_callback_t callback)
{
	return __ConditionMap::get_instance()->create(name, std::move(callback));
}

WFMailboxTask *WFSemTaskFactory::create_timedwait_task(const std::string& name,
													   const struct timespec *abstime,
													   mailbox_callback_t callback)
{
	return __ConditionMap::get_instance()->create(name, abstime,
												  std::move(callback));
}

WFMailboxTask *WFSemTaskFactory::create_switch_wait_task(const std::string& name,
														 mailbox_callback_t callback)
{
	return __ConditionMap::get_instance()->create_switch(name, std::move(callback));
}

WFMailboxTask *WFSemTaskFactory::create_switch_timedwait_task(const std::string& name,
														 	  const struct timespec *abstime,
															  mailbox_callback_t callback)
{
	return __ConditionMap::get_instance()->create_switch(name, abstime,
														 std::move(callback));
}

WFMailboxTask *WFSemTaskFactory::create_wait_task(WFCondition *cond,
												  mailbox_callback_t callback)
{
	WFWaitTask *task = new WFWaitTask(std::move(callback));

	cond->mutex.lock();
	list_add_tail(&task->list, &cond->waiter_list);
	cond->mutex.unlock();

	return task;
}

WFMailboxTask *WFSemTaskFactory::create_timedwait_task(WFCondition *cond,
													   const struct timespec *abstime,
													   mailbox_callback_t callback)
{
	WFWaitTask *waiter = new WFWaitTask(std::move(callback));
	WFTimedWaitTask *task = new WFTimedWaitTask(waiter, &cond->mutex, abstime,
												WFGlobal::get_scheduler(),
												nullptr);
	waiter->set_timer(task);

	cond->mutex.lock();
	list_add_tail(&waiter->list, &cond->waiter_list);
	cond->mutex.unlock();

	return waiter;
}

WFMailboxTask *WFSemTaskFactory::create_switch_wait_task(WFCondition *cond,
														 mailbox_callback_t callback)
{
	WFWaitTask *task = new WFSwitchWaitTask(std::move(callback));

	cond->mutex.lock();
	list_add_tail(&task->list, &cond->waiter_list);
	cond->mutex.unlock();

	return task;
}

WFMailboxTask *WFSemTaskFactory::create_switch_timedwait_task(WFCondition *cond,
															  const struct timespec *abstime,
															  mailbox_callback_t callback)
{
	WFSwitchWaitTask *waiter = new WFSwitchWaitTask(std::move(callback));
	WFTimedWaitTask *task = new WFTimedWaitTask(waiter, &cond->mutex, abstime,
												WFGlobal::get_scheduler(),
												nullptr);
	waiter->set_timer(task);

	cond->mutex.lock();
	list_add_tail(&waiter->list, &cond->waiter_list);
	cond->mutex.unlock();

	return waiter;
}
