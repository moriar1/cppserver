#include "socket.hpp"
#include <string>

namespace http {

bool is_path_safe(std::string_view path); // for test
void handle_client(Socket accept_sock, std::string ip);

} // namespace http
