#pragma once

#include <queue>
#include <deque>


template <class T>
class random_access_queue : public std::queue < T, std::deque<T> > {
private:
	using queue_type = std::queue < T, std::deque<T> > ;
public:
	// constructors
	explicit random_access_queue(const container_type& cont) : queue_type(cont) {};

	explicit random_access_queue(container_type&& cont = container_type()) : queue_type(cont) {};

	random_access_queue(const queue& other) : queue_type(other) {};

	random_access_queue(queue&& other) : queue_type(other) {};

	template< class Alloc >
	explicit random_access_queue(const Alloc& alloc) : queue_type(alloc) {};

	template< class Alloc >
	random_access_queue(const container_type& cont, const Alloc& alloc) : queue_type(cont, alloc) {};

	template< class Alloc >
	random_access_queue(container_type&& cont, const Alloc& alloc) : queue_type(cont, alloc) {};

	template< class Alloc >
	random_access_queue(const queue& other, const Alloc& alloc) : queue_type(other, alloc) {};

	template< class Alloc >
	random_access_queue(queue&& other, const Alloc& alloc) : queue_type(other, alloc) {};

	// assignement operators
	random_access_queue<T>& operator=(const random_access_queue<T>& other) {
		queue_type::operator=(other);
	}

	random_access_queue<T>& operator=(random_access_queue<T>&& other) {
		queue_type::operator=(other);
	}

	// index operator
	T& operator[](size_t index) {
		return c[index];
	}
	const T& operator[](size_t index) const {
		return c[index];
	}
};