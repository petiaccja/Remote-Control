#pragma once

#include <thread>
#include <condition_variable>
#include <queue>
#include <future>
#include <mutex>
#include <functional>
#include <type_traits>
#include <atomic>

class ThreadEnqueue {
	struct BundleBase {
		virtual void Execute() = 0;
		virtual ~BundleBase() = default;
	};

	template <class Func, class... Args>
	struct Bundle : public BundleBase {
	public:
		using ReturnType = typename std::result_of<Func(Args...)>::type;
		std::promise<ReturnType> pr;
		std::function<ReturnType()> func;
		void Execute() override {
			try {
				_Execute<ReturnType>();
			}
			catch (...) {
				try {
					pr.set_exception(std::current_exception());
				}
				catch (...) {

				}
			}
		}
	private:
		template <class T>
		void _Execute() {
			pr.set_value(func());
		}
		template <>
		void _Execute<void>() {
			func();
			pr.set_value();
		}
	};
public:
	ThreadEnqueue() {
		taskThread = std::thread([this] {
			std::unique_lock<std::mutex> lk(mtx);
			while (runTaskThread || taskQueue.size() > 0) {
				cv.wait(lk, [&] {return taskQueue.size() > 0 || !runTaskThread;});
				if (taskQueue.size() > 0) {
					taskQueue.front()->Execute();
					delete taskQueue.front();
					taskQueue.pop();
				}
			}
		});
	}
	~ThreadEnqueue() {
		runTaskThread = false;
		cv.notify_all();
		taskThread.join();
	}

	template <class Func, class... Args>
	auto Enqueue(Func func, Args&&... args) ->std::future<typename std::result_of<Func(Args...)>::type> {
		using ReturnType = typename std::result_of<Func(Args...)>::type;
		Bundle<Func, Args...>* bundle = new Bundle<Func, Args...>();
		bundle->func = std::bind(func, args...);
		std::unique_lock<std::mutex> lk(mtx);
		taskQueue.push(bundle);
		cv.notify_all();
		return bundle->pr.get_future();
	}
private:
	std::atomic_bool runTaskThread;
	std::thread taskThread;
	std::condition_variable cv;
	std::mutex mtx;
	std::queue<BundleBase*> taskQueue;
};