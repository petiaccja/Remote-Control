#include "Serializer.h"


////////////////////////////////////////////////////////////////////////////////
// General stuff
Serializer::Serializer(size_t initialSize) {
	byteStream.reserve(initialSize);
}

const std::vector<uint8_t>& Serializer::Get() const {
	return byteStream;
}

void Serializer::Set(const void* data, size_t size) {
	byteStream.assign((uint8_t*)data, (uint8_t*)data + size);
}

////////////////////////////////////////////////////////////////////////////////
// Insert operators


Serializer& Serializer::operator<<(int8_t input) {
	byteStream.push_back(reinterpret_cast<uint8_t&>(input));
	return *this;
}

Serializer& Serializer::operator<<(uint8_t input) {
	byteStream.push_back(input);
	return *this;
}

Serializer& Serializer::operator<<(int16_t input) {
	operator<<(reinterpret_cast<uint16_t&>(input));
	return *this;
}

Serializer& Serializer::operator<<(uint16_t input) {
	byteStream.push_back(input >> 8);
	byteStream.push_back(input & 0xFF);
	return *this;
}

Serializer& Serializer::operator<<(int32_t input) {
	operator<<(reinterpret_cast<uint32_t&>(input));
	return *this;
}

Serializer& Serializer::operator<<(uint32_t input) {
	byteStream.reserve(sizeof(input));
	byteStream.push_back((input >> 24) & 0xFF);
	byteStream.push_back((input >> 16) & 0xFF);
	byteStream.push_back((input >> 8) & 0xFF);
	byteStream.push_back(input & 0xFF);
	return *this;
}

Serializer& Serializer::operator<<(int64_t input) {
	operator<<(reinterpret_cast<uint64_t&>(input));
	return *this;
}

Serializer& Serializer::operator<<(uint64_t input) {
	byteStream.reserve(sizeof(input));
	byteStream.push_back((input >> 56) & 0xFF);
	byteStream.push_back((input >> 48) & 0xFF);
	byteStream.push_back((input >> 40) & 0xFF);
	byteStream.push_back((input >> 32) & 0xFF);
	byteStream.push_back((input >> 24) & 0xFF);
	byteStream.push_back((input >> 16) & 0xFF);
	byteStream.push_back((input >> 8) & 0xFF);
	byteStream.push_back(input & 0xFF);
	return *this;
}

Serializer& Serializer::operator<<(float input) {
	static_assert(sizeof(float) == sizeof(uint32_t), "");
	union {
		float f;
		uint32_t u;
	} r;
	r.f = input;
	operator<<(r.u);
	return *this;
}

Serializer& Serializer::operator<<(double input) {
	static_assert(sizeof(double) == sizeof(uint64_t), "");
	union {
		double f;
		uint64_t u;
	} r;
	r.f = input;
	operator<<(r.u);
	return *this;
}

Serializer& Serializer::operator<<(bool input) {
	byteStream.push_back(reinterpret_cast<uint8_t&>(input));
	return *this;
}

Serializer& Serializer::operator<<(const void* input) {
	operator<<(reinterpret_cast<size_t&>(input));
	return *this;
}


////////////////////////////////////////////////////////////////////////////////
// Extract operators

Serializer& Serializer::operator>>(int8_t& input) {
	operator>>(reinterpret_cast<uint8_t&>(input));
	return *this;
}

Serializer& Serializer::operator>>(uint8_t& input) {
	if (byteStream.size() >= sizeof(input)) {
		input = byteStream[byteStream.size() - 1];
		byteStream.resize(byteStream.size() - sizeof(input));
	}
	return *this;
}

Serializer& Serializer::operator>>(int16_t& input) {
	operator>>(reinterpret_cast<uint16_t&>(input));
	return *this;
}

Serializer& Serializer::operator>>(uint16_t& input) {
	if (byteStream.size() >= sizeof(input)) {
		input = (uint16_t)byteStream[byteStream.size() - 1];
		input |= (uint16_t)byteStream[byteStream.size() - 2] << 8;
		byteStream.resize(byteStream.size() - sizeof(input));
	}
	return *this;
}

Serializer& Serializer::operator>>(int32_t& input) {
	operator>>(reinterpret_cast<uint32_t&>(input));
	return *this;
}

Serializer& Serializer::operator>>(uint32_t& input) {
	if (byteStream.size() >= sizeof(input)) {
		input = (uint32_t)byteStream[byteStream.size() - 1];
		input |= (uint32_t)byteStream[byteStream.size() - 2] << 8;
		input |= (uint32_t)byteStream[byteStream.size() - 3] << 16;
		input |= (uint32_t)byteStream[byteStream.size() - 4] << 24;
		byteStream.resize(byteStream.size() - sizeof(input));
	}
	return *this;
}

Serializer& Serializer::operator>>(int64_t& input) {
	operator>>(reinterpret_cast<uint64_t&>(input));
	return *this;
}

Serializer& Serializer::operator>>(uint64_t& input) {
	if (byteStream.size() >= sizeof(input)) {
		input = (uint64_t)byteStream[byteStream.size() - 1];
		input |= (uint64_t)byteStream[byteStream.size() - 2] << 8;
		input |= (uint64_t)byteStream[byteStream.size() - 3] << 16;
		input |= (uint64_t)byteStream[byteStream.size() - 4] << 24;
		input |= (uint64_t)byteStream[byteStream.size() - 5] << 32;
		input |= (uint64_t)byteStream[byteStream.size() - 6] << 40;
		input |= (uint64_t)byteStream[byteStream.size() - 7] << 48;
		input |= (uint64_t)byteStream[byteStream.size() - 8] << 56;
		byteStream.resize(byteStream.size() - sizeof(input));
	}
	return *this;
}

Serializer& Serializer::operator>>(float& input) {
	operator>>(reinterpret_cast<uint32_t&>(input));
	return *this;
}

Serializer& Serializer::operator>>(double& input) {
	operator>>(reinterpret_cast<uint64_t&>(input));
	return *this;
}

Serializer& Serializer::operator>>(bool& input) {
	operator>>(reinterpret_cast<uint8_t&>(input));
	return *this;
}

Serializer& Serializer::operator>>(const void*& input) {
	operator>>(reinterpret_cast<size_t&>(input));
	return *this;
}

Serializer& Serializer::operator>>(void*& input) {
	operator>>(reinterpret_cast<size_t&>(input));
	return *this;
}