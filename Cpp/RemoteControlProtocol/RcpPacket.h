#pragma once

#include <cstdint>
#include <cstddef>

class RcpPacket {
	friend class RcpSocket;
	friend class RcpTester;
public:
	/// Create an empty packet.
	RcpPacket();
	RcpPacket(const RcpPacket& other);
	RcpPacket(RcpPacket&& other);
	~RcpPacket();
	RcpPacket& operator=(const RcpPacket& other);
	RcpPacket& operator=(RcpPacket&& other);

	/// Set the packet's content.
	/// \param data The raw bytes of the message.
	/// \param size The number of bytes in data.
	void setData(const void* data, size_t size);

	/// Set weather it's a reliable packet.
	/// \param isReliable True is the packet is reliable.
	void setReliable(bool isReliable);

	/// Erase all data.
	void clear();
	

	/// [Deprecated] Get the sequence number of the packet.
	/// Only valid for received and already sent packets. Otherwise numeric_limits<uint32_t>::max is returned.
	uint32_t getSequenceNumber() const;

	/// Get packets contents.
	const void* getData() const;

	/// Get size of packet.
	/// \return Size of packet in bytes.
	size_t getDataSize() const;

	/// Returns weather the packet is reliable.
	/// \return True if reliable.
	bool isReliable() const;

private:
	void* data;
	size_t size;
	bool reliable;
	uint32_t sequenceNumber;
};
