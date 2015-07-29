#include "Message.h"
#include "Serializer.h"


////////////////////////////////////////////////////////////////////////////////
// MessageDecoder


void MessageDecoder::SetHandler(eMessageType type, HandlerType handler) {
	handlers[type] = handler;
}

void MessageDecoder::ClearHandler(eMessageType type) {
	handlers.erase(type);
}

void MessageDecoder::Clear() {
	handlers.clear();
}

void MessageDecoder::ProcessMessage(const void* message, size_t length) {
	// extract message type: first 1 byte of message
	if (length == 0) {
		return;
	}
	eMessageType type = (eMessageType)*reinterpret_cast<const uint8_t*>(message);
	// find and call appropriate handler, if any
	auto it = handlers.find(type);
	if (it == handlers.end()) {
		return;
	}
	it->second(message, length);
}


////////////////////////////////////////////////////////////////////////////////
// Message object functions (mostly serialization)


// ServoMessage

std::vector<uint8_t> ServoMessage::Serialize() {
	Serializer ser;
	ser << (uint8_t)eMessageType::DEVICE_SERVO;
	ser << (uint8_t)action;
	ser << channel;
	ser << state;
	return ser.Get();
}

bool ServoMessage::Deserlialize(const void* data, size_t size) {
	if (size < SerializedSize()) {
		return false;
	}

	Serializer ser;
	ser.Set(data, SerializedSize());

	eMessageType type;
	ser >> state;
	ser >> channel;
	ser >> (uint8_t&)action;
	ser >> (uint8_t&)type;
	if (type != eMessageType::DEVICE_SERVO) {
		return false;
	}

	return true;
}

constexpr size_t ServoMessage::SerializedSize() {
	return sizeof(eMessageType) + sizeof(action) + sizeof(channel) + sizeof(state);
}


// Authentication message

std::vector<uint8_t> ConnectionMessage::Serialize() {
	Serializer ser;
	ser << (uint8_t)eMessageType::CONNECTION;
	ser << (uint8_t)action;
	switch (action) {
		case ConnectionMessage::CONNECTION_REPLY:
			ser << isOk;
			break;
		case ConnectionMessage::PASSWORD_REPLY:
			ser << (uint32_t)password.size();
			for (auto v : password) {
				ser << v;
			}
			break;
	}
	return ser.Get();
}

bool ConnectionMessage::Deserlialize(const void* data, size_t size) {
	eMessageType type;
	uint32_t pwSize;

	if (size < 2) { // 2 = sizeof type + action
		return false;
	}

	Serializer ser;
	ser.Set(data, 2);
	ser >> (uint8_t&)action;
	ser >> (uint8_t&)type;

	if (type != eMessageType::CONNECTION) {
		return false;
	}

	switch (action) {
		case ConnectionMessage::CONNECTION_REPLY:
			if (size < 3) {
				return false;
			}
			ser.Set((uint8_t*)data + 2, 1);
			ser >> isOk;
			break;
		case ConnectionMessage::PASSWORD_REPLY:
			if (size < 6) {
				return false;
			}
			ser.Set((uint8_t*)data + 2, 4);
			ser >> pwSize;
			if (size < 6 + pwSize) {
				return false;
			}
			ser.Set((uint8_t*)data + 6, size - 6);
			password.resize(pwSize);
			for (; pwSize > 0; pwSize--) {
				ser >> password[pwSize - 1];
			}
			break;
	}

	return true;
}

// Device enumeration

std::vector<uint8_t> EnumDevicesMessage::Serialize() {
	Serializer ser;
	ser << (uint8_t)eMessageType::ENUM_DEVICES;
	ser << (uint32_t)devices.size();
	for (auto v : devices) {
		ser << (uint8_t)v.type;
		ser << v.channelCount;
	}
	return ser.Get();
}

bool EnumDevicesMessage::Deserlialize(const void* data, size_t size) {
	if (size < 5) {
		return false;
	}

	eMessageType type;
	uint32_t numDevs;

	Serializer ser;
	ser.Set(data, 5);
	ser >> numDevs;
	ser >> (uint8_t&)type;

	if (type != eMessageType::ENUM_DEVICES || size < 5 + numDevs*(sizeof(DeviceInfo::type) + sizeof(DeviceInfo::channelCount))) {
		return false;
	}

	ser.Set((uint8_t*)data + 5, size - 5);
	devices.resize(numDevs);
	for (; numDevs > 0; numDevs--) {
		ser >> devices[numDevs - 1].channelCount;
		ser >> (uint8_t&)devices[numDevs - 1].type;
	}

	return true;
}



// Channel enumeration

std::vector<uint8_t> EnumChannelsMessage::Serialize() {
	Serializer ser;
	ser << (uint8_t)eMessageType::ENUM_CHANNELS;
	ser << (uint8_t)type;
	ser << (uint32_t)channels.size();
	for (auto v : channels) {
		ser << v;
	}
	return ser.Get();
}

bool EnumChannelsMessage::Deserlialize(const void* data, size_t size) {
	if (size < 6) {
		return false;
	}

	eMessageType msgType;
	uint32_t numChannels;

	Serializer ser;
	ser.Set(data, 6);
	ser >> numChannels;
	ser >> (uint8_t&)type;
	ser >> (uint8_t&)msgType;

	if (msgType != eMessageType::ENUM_CHANNELS || size < 6 + numChannels*sizeof(int32_t)) {
		return false;
	}

	ser.Set((uint8_t*)data + 6, size - 6);
	channels.resize(numChannels);
	for (; numChannels > 0; numChannels--) {
		ser >> channels[numChannels - 1];
	}

	return true;
}
