#include "httphandler.hpp"
#include <cassert>
#include <iostream>

int main() {
  bool status = http::is_path_safe("../../../../../etc/passwd");
  if (status) {
    std::cerr << "Error: `../../../../../etc/passwd` should be unsafe\n";
    return 1;
  }

  bool status1 = http::is_path_safe("./index.html");
  if (!status1) {
    std::cerr << "Error: `index.html` should be safe\n";
    return 1;
  }
}
