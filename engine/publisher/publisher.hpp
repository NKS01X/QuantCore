#pragma once

#include <string>
#include <zmq.hpp>

class Publisher
{
public:
    explicit Publisher(const std::string& address);
    ~Publisher();
    void publish(const std::string& message);

private:
    zmq::context_t context_;
    zmq::socket_t socket_;
};