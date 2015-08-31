#pragma once

#include <queue>
#include <deque>


template <class T>
class random_access_queue : public std::queue < T, std::deque<T> > {
private:
	using queue_type = std::queue < T, std::deque<T> > ;
public:
	// constructors
	explicit random_access_queue(const typename std::deque<T>& cont) : queue_type(cont) {};

	explicit random_access_queue(typename std::deque<T>&& cont = std::deque<T>()) : queue_type(cont) {};

	random_access_queue(const random_access_queue& other) : queue_type(other) {};

	random_access_queue(random_access_queue&& other) : queue_type(other) {};

	template< class Alloc >
	explicit random_access_queue(const Alloc& alloc) : queue_type(alloc) {};

	template< class Alloc >
	random_access_queue(const typename std::deque<T>& cont, const Alloc& alloc) : queue_type(cont, alloc) {};

	template< class Alloc >
	random_access_queue(typename std::deque<T>&& cont, const Alloc& alloc) : queue_type(cont, alloc) {};

	template< class Alloc >
	random_access_queue(const random_access_queue& other, const Alloc& alloc) : queue_type(other, alloc) {};

	template< class Alloc >
	random_access_queue(random_access_queue&& other, const Alloc& alloc) : queue_type(other, alloc) {};

	// assignement operators
	random_access_queue<T>& operator=(const random_access_queue<T>& other) {
		queue_type::operator=(other);
		return *this;
	}

	random_access_queue<T>& operator=(random_access_queue<T>&& other) {
		queue_type::operator=(other);
		return *this;
	}

	// index operator
	T& operator[](size_t index) {
		return queue_type::c[index];
	}
	const T& operator[](size_t index) const {
		return queue_type::c[index];
	}

	// iterators
	using iterator = typename std::deque<T>::iterator;
	using const_iterator = typename std::deque<T>::const_iterator;

	iterator begin() {
		return queue_type::c.begin();
	}
	iterator end() {
		return queue_type::c.end();
	}
	const_iterator begin() const {
		return queue_type::c.begin();
	}
	const_iterator end() const {
		return queue_type::c.end();
	}
	const_iterator cbegin() const {
		return queue_type::c.cbegin();
	}
	const_iterator cend() const {
		return queue_type::c.cend();
	}
};
