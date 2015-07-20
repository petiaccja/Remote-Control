#include "RcpPacket.h"

#include <limits>
#include <memory>
#include <cstring>

////////////////////////////////////////////////////////////////////////////////
// Constructors

RcpPacket::RcpPacket() {
	data = nullptr;
	size = 0;
	sequenceNumber = std::numeric_limits<uint32_t>::max();
	reliable = false;
}

RcpPacket::RcpPacket(const RcpPacket& other) {
	data = operator new(other.size);
	size = other.size;
	sequenceNumber = other.sequenceNumber;
	reliable = other.reliable;
	memcpy(data, other.data, size);
}

RcpPacket::RcpPacket(RcpPacket&& other) {
	data = other.data;
	size = other.size;
	sequenceNumber = other.sequenceNumber;
	reliable = other.reliable;

	other.data = nullptr;
	other.size = 0;
}

RcpPacket::~RcpPacket() {
	operator delete(data);
}


RcpPacket& RcpPacket::operator=(const RcpPacket& other) {
	this->~RcpPacket();
	new (this) RcpPacket(other);
	return *this;
}
RcpPacket& RcpPacket::operator=(RcpPacket&& other) {
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

void RcpPacket::setData(const void* data, size_t size) {
	operator delete(this->data);
	this->data = operator new(size);
	this->size = size;
	memcpy(this->data, data, size);
}

void RcpPacket::setReliable(bool isReliable) {
	reliable = isReliable;
}

void RcpPacket::clear() {
	operator delete(data);
	data = nullptr;
	size = 0;
}


////////////////////////////////////////////////////////////////////////////////
// Information query


uint32_t RcpPacket::getSequenceNumber() const {
	return sequenceNumber;
}

const void* RcpPacket::getData() const {
	return data;
}

size_t RcpPacket::getDataSize() const {
	return size;
}

bool RcpPacket::isReliable() const {
	return reliable;
}
