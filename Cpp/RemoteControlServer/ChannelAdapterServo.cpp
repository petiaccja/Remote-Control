#include "ChannelAdapterServo.h"
#include "Message.h"
#include "ChannelManagerServo.h"


ChannelAdapterServo::ChannelAdapterServo(ChannelManagerServo* manager) : manager(manager)
{ }


void ChannelAdapterServo::SetManager(ChannelManagerServo* manager) {
	this->manager = manager;
}


ChannelManagerServo* ChannelAdapterServo::GetManager() const {
	return manager;
}


bool ChannelAdapterServo::ProcessCommand(const ServoMessage& message, ServoMessage& reply) {
	if (!manager) {
		return false;
	}

	switch (message.action) {
		case ServoMessage::SET:
			manager->SetState(message.state, message.channel);
			return false;
		case ServoMessage::QUERY:
			reply.action = ServoMessage::REPLY;
			reply.state = manager->GetState(message.channel);
			reply.channel = message.channel;
			return true;
		default:
			return false;
	}
}