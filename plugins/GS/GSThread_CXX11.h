/*
 *	Copyright (C) 2007-2009 Gabest
 *	http://www.gabest.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#pragma once

#include <thread>
#include <functional>
#include <condition_variable>
#include <mutex>

#include "GS.h"
#include "Utilities/boost_spsc_queue.hpp"

template<class T, int CAPACITY> class GSJobQueue final
{
private:
	std::thread m_thread;
	std::function<void(T&)> m_func;
	bool m_exit;
	ringbuffer_base<T, CAPACITY> m_queue;

	std::mutex m_lock;
	std::mutex m_wait_lock;
	std::condition_variable m_empty;
	std::condition_variable m_notempty;

	void ThreadProc() {
		std::unique_lock<std::mutex> l(m_lock);

		while (true) {

			while (m_queue.empty()) {
				if (m_exit)
					return;

				m_notempty.wait(l);
			}

			l.unlock();

			while (m_queue.consume_one(*this))
				;

			{
				std::lock_guard<std::mutex> wait_guard(m_wait_lock);
			}
			m_empty.notify_one();

			l.lock();
		}
	}

public:
	GSJobQueue(std::function<void(T&)> func) :
		m_func(func),
		m_exit(false)
	{
		m_thread = std::thread(&GSJobQueue::ThreadProc, this);
	}

	~GSJobQueue()
	{
		{
			std::lock_guard<std::mutex> l(m_lock);
			m_exit = true;
		}
		m_notempty.notify_one();

		m_thread.join();
	}

	bool IsEmpty()
	{
		return m_queue.empty();
	}

	void Push(const T& item) {
		while(!m_queue.push(item))
			std::this_thread::yield();

		{
			std::lock_guard<std::mutex> l(m_lock);
		}
		m_notempty.notify_one();
	}

	void Wait()
	{
		if (IsEmpty())
			return;

		std::unique_lock<std::mutex> l(m_wait_lock);
		while (!IsEmpty())
			m_empty.wait(l);

		assert(IsEmpty());
	}

	void operator() (T& item) {
		m_func(item);
	}
};
