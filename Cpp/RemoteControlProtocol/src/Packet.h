#pragma once

#include <cstdint>

class Packet {
public:
	Packet();
	Packet(const Packet& other);
	~Packet();
	Packet& operator=(const Packet& other);

	bool IsReliable() const;
	uint32_t GetSequenceNumber() const;
	const void* GetData() const;
	size_t GetDataSize() const;
	uint32_t GetFlags() const;
private:
	void* data;
	size_t size;
	uint32_t flags;
	uint32_t sequenceNumber;
};