#pragma once

#include <stdexcept>
#include <string>



class RcpException : public std::exception {
public:
	explicit RcpException(const std::string& what_arg) : message(what_arg) {
	}
	explicit RcpException(const char* what_arg) : message(what_arg) {
	}

	virtual const char* what() const override {
		return message.c_str();
	}
private:
	std::string message;
};



class RcpNetworkException : public RcpException {
public:
	using RcpException::RcpException;
};


class RcpInvalidCallException : public RcpException {
public:
	using RcpException::RcpException;
};


class RcpInvalidArgumentException : public RcpException {
public:
	using RcpException::RcpException;
};


class RcpInterruptedException : public RcpException {
public:
	using RcpException::RcpException;
};

class RcpTimeoutException : public RcpException {
public:
	using RcpException::RcpException;
};


