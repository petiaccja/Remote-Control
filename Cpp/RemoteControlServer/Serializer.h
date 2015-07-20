#pragma once

#include <cstdint>
#include <vector>
#include <type_traits>

class Serializer {
public:
	Serializer(size_t initialSize = 32);

	const std::vector<uint8_t>& Get() const;
	void Set(const void* data, size_t size);
	inline void Clear() { byteStream.clear(); }
	inline size_t Size() const { return byteStream.size(); }

	Serializer& operator<<(int8_t input);
	Serializer& operator<<(uint8_t input);
	Serializer& operator<<(int16_t input);
	Serializer& operator<<(uint16_t input);
	Serializer& operator<<(int32_t input);
	Serializer& operator<<(uint32_t input);
	Serializer& operator<<(int64_t input);
	Serializer& operator<<(uint64_t input);
	Serializer& operator<<(float input);
	Serializer& operator<<(double input);
	Serializer& operator<<(bool);
	Serializer& operator<<(const void*);
	
	Serializer& operator>>(int8_t& input);
	Serializer& operator>>(uint8_t& input);
	Serializer& operator>>(int16_t& input);
	Serializer& operator>>(uint16_t& input);
	Serializer& operator>>(int32_t& input);
	Serializer& operator>>(uint32_t& input);
	Serializer& operator>>(int64_t& input);
	Serializer& operator>>(uint64_t& input);
	Serializer& operator>>(float& input);
	Serializer& operator>>(double& input);
	Serializer& operator>>(bool&);
	Serializer& operator>>(const void*&);
	Serializer& operator>>(void*&);
private:
	std::vector<uint8_t> byteStream;
};
