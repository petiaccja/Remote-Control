#pragma once

#include <cstdint>
#include <vector>
#include <functional>
#include <map>


////////////////////////////////////////////////////////////////////////////////
// All the network messages and commands that are required for the remote control
// of on-board hardware devices. The server and the remote client exchange
// messages defined here, and execute them.
////////////////////////////////////////////////////////////////////////////////



////////////////////////////////////////////////////////////////////////////////
// Message processing
////////////////////////////////////////////////////////////////////////////////


/// Message types.
enum class eMessageType : uint8_t {
	CONNECTION = 1,
	ENUM_DEVICES = 2,
	ENUM_CHANNELS = 3,
	DEVICE_SERVO = 10,
	DEVICE_PWM = 11,
	DEVICE_ADJUSTABLE_PWM = 12,
};


/// Stores handlers to decode and process different message types.
class MessageDecoder {
public:
	using HandlerType = std::function<void(const void*, size_t)>;
	void SetHandler(eMessageType type, HandlerType handler);
	void ClearHandler(eMessageType type);
	void Clear();

	void ProcessMessage(const void* message, size_t length);
private:
	std::map<eMessageType, HandlerType> handlers;
};


////////////////////////////////////////////////////////////////////////////////
// Individual message types
////////////////////////////////////////////////////////////////////////////////

/// Base class for messages
struct MessageBase {
	virtual std::vector<uint8_t> Serialize() = 0;
	virtual bool Deserlialize(const void* data, size_t size) = 0;
	virtual ~MessageBase() {}
};

/// Command for servo providers.
struct ServoMessage : public MessageBase {
	enum eAction : uint8_t {
		SET = 1,
		QUERY = 2,
		REPLY = 3,
	};
	eAction action;
	int32_t channel;
	float state;

	std::vector<uint8_t> Serialize() override;
	bool Deserlialize(const void* data, size_t size) override;
	static constexpr size_t SerializedSize();
};


/// Authentication messages.
struct ConnectionMessage : public MessageBase {
	enum eAction : uint8_t {
		CONNECTION_REQUEST = 1,
		CONNECTION_REPLY = 2,
		PASSWORD_REQUEST = 3,
		PASSWORD_REPLY = 4,
		DISCONNECT = 5,
	};
	eAction action;
	bool isOk;
	std::vector<uint8_t> password;

	std::vector<uint8_t> Serialize() override;
	bool Deserlialize(const void* data, size_t size) override;
};


/// Device and channel enumeration, other global parameters.
struct EnumDevicesMessage : public MessageBase {
	enum eDeviceType : uint8_t {
		SERVO = 1,
		PWM = 2,
		ADJUSTABLE_PWM = 3,
	};

	struct DeviceInfo {
		eDeviceType type;
		uint32_t channelCount;
	};

	std::vector<DeviceInfo> devices;

	std::vector<uint8_t> Serialize() override;
	bool Deserlialize(const void* data, size_t size) override;
};

struct EnumChannelsMessage : public MessageBase {
	enum eDeviceType : uint8_t {
		SERVO = 1,
		PWM = 2,
		ADJUSTABLE_PWM = 3,
	};

	eDeviceType type;
	std::vector<uint32_t> channels;

	std::vector<uint8_t> Serialize() override;
	bool Deserlialize(const void* data, size_t size) override;
};