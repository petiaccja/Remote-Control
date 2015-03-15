#pragma once

#include <cstdint>

class Packet {
	friend class RcpSocket;
	friend class RcpTester;
public:
	/// Create an empty packet.
	Packet();
	Packet(const Packet& other);
	Packet(Packet&& other);
	~Packet();
	Packet& operator=(const Packet& other);
	Packet& operator=(Packet&& other);

	/// Set the packet's content.
	void setData(const void* data, size_t size);

	/// Set weather it's a reliable packet.
	/// Reliable packets are guaranteed to arrive, and not be duplicated.
	/// Reliable packets also imply ordering, meaning that subsequent packets
	/// will arrive after this one. Unreliable packets are may get lost, may get duplicated,
	/// and can also arrive out of order.
	void setReliable(bool isReliable);

	/// Erase all data.
	void clear();
	

	/// Get the sequence number of the packet.
	/// Only valid for recieved and already sent packets. Otherwise numeric_limits<uint32_t>::max is returned.
	uint32_t getSequenceNumber() const;

	// Get a pointer to the packet's data.
	const void* getData() const;

	// Returns the size of the packet's data in bytes.
	size_t getDataSize() const;

	// Returns weather the packet is reliable.
	bool isReliable() const;

private:
	void* data;
	size_t size;
	bool reliable;
	uint32_t sequenceNumber;
};