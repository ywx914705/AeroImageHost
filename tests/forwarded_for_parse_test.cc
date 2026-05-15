#include "ForwardedForParse.hpp"

#include <cassert>
#include <iostream>

int main() {
    assert(trim_http_ows("") == "");
    assert(trim_http_ows("  ") == "");
    assert(trim_http_ows("  1.2.3.4  ") == "1.2.3.4");
    assert(first_ip_from_x_forwarded_for("203.0.113.1, 198.51.100.1") == "203.0.113.1");
    assert(first_ip_from_x_forwarded_for("203.0.113.1") == "203.0.113.1");
    assert(first_ip_from_x_forwarded_for("") == "");

    std::cout << "forwarded_for_parse_tests: ok\n";
    return 0;
}
