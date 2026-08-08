#include <iostream>
#include <sstream>
#include <print>

import env;
import parser;

int main() {
    std::println("Hello World!");
    env::Env global_env;
    std::string str;
    str.reserve(1024);
    while (true) {
        std::string cur;
        std::getline(std::cin, cur);
        if (const auto pos = cur.find('>'); pos != std::string::npos) {
            str.append(cur.substr(0, pos));
            break;
        }
        str.append(cur);
        str.push_back('\n');
    }
    std::ostringstream out;
    std::istringstream in{str};
    parser::compile_assembly(in, out);
    std::istringstream bytes{out.str(), std::ios_base::in};
    auto ret_code = parser::main<true>(global_env, bytes);
    std::println("Ret code: {}", ret_code);
}