#include "socket.hpp"
#include <string>

namespace http {

void handle_client(Socket accept_sock, std::string ip);

} // namespace http
