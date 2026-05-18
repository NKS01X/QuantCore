#include "publisher.hpp"

Publisher::Publisher(const std::string& address)
    : socket_(context_, zmq::socket_type::pub)
{
    socket_.bind(address);
}

Publisher::~Publisher()
{
    socket_.close();
    context_.close();
}

void Publisher::publish(const std::string& message)
{
    socket_.send(zmq::buffer(message), zmq::send_flags::none);
}
