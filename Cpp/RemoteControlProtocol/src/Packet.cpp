#include "Packet.h"

#include <limits>
#include <memory>
#include <cstring>

////////////////////////////////////////////////////////////////////////////////
// Constructors

Packet::Packet() {
	data = nullptr;
	size = 0;
	sequenceNumber = std::numeric_limits<uint32_t>::max();
	reliable = false;
}

Packet::Packet(const Packet& other) {
	data = operator new(other.size);
	size = other.size;
	sequenceNumber = other.sequenceNumber;
	reliable = other.reliable;
	memcpy(data, other.data, size);
}

Packet::Packet(Packet&& other) {
	data = other.data;
	size = other.size;
	sequenceNumber = other.sequenceNumber;
	reliable = other.reliable;

	other.data = nullptr;
	other.size = 0;
}

Packet::~Packet() {
	operator delete(data);
}


Packet& Packet::operator=(const Packet& other) {
	Packet::~Packet();
	new (this) Packet(other);
	return *this;
}
Packet& Packet::operator=(Packet&& other) {
	data = other.data;
	size = other.size;
	sequenceNumber = other.sequenceNumber;
	reliable = other.reliable;

	other.data = nullptr;
	other.size = 0;
	return *this;
}

////////////////////////////////////////////////////////////////////////////////
// Modifiers

void Packet::setData(const void* data, size_t size) {
	operator delete(this->data);
	this->data = operator new(size);
	this->size = size;
	memcpy(this->data, data, size);
}

void Packet::setReliable(bool isReliable) {
	reliable = isReliable;
}

void Packet::clear() {
	operator delete(data);
	data = nullptr;
	size = 0;
}


////////////////////////////////////////////////////////////////////////////////
// Information query


uint32_t Packet::getSequenceNumber() const {
	return sequenceNumber;
}

const void* Packet::getData() const {
	return data;
}

size_t Packet::getDataSize() const {
	return size;
}

bool Packet::isReliable() const {
	return reliable;
}