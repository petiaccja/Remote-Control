#pragma once

#include <mutex>
#include <algorithm>
#include <condition_variable>

#include <chrono>


class Barrier {
public:
	Barrier() {
		counter = 0;
	}

	void Reset() {
		counter = 0;
	}

	void Synchronize(int n) {
		std::unique_lock<std::mutex> lk(mtx);
		cv.wait(lk, [this, n] {return counter >= n;});
		counter = std::max(counter, n+1);
		cv.notify_all();
	}
private:
	int counter;
	std::condition_variable cv;
	std::mutex mtx;
};




class Sequence {
public:
	Sequence() {
		start = std::chrono::steady_clock::now();
	}

	void operator()(int ms) {
		std::this_thread::sleep_until(start + std::chrono::milliseconds(ms));
	}

private:
	std::chrono::steady_clock::time_point start;
};