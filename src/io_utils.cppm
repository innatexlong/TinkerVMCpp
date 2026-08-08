//
// Created by Administrator on 2026/8/2.
//
module;
#include <istream>
#include <ostream>

export module io_utils;

export namespace io_utils {
    void read(std::istream& is, char* c, const int size) {
        is.read(c, size);
        if (is.gcount() != size) {
            throw std::runtime_error("Unexpected end of stream");
        }
        if (is.bad() || is.fail()) {
            throw std::runtime_error("Unexpected bad state of is");
        }
    }
    void write(std::ostream& os, const char* c, const int size) {
        os.write(c, size);
        if (os.eof()) {
            throw std::runtime_error("Unexpected end of stream");
        }
        if (os.bad() || os.fail()) {
            throw std::runtime_error("Unexpected bad state of os");
        }
    }
    void get(std::istream& is, char& c) {
        is.get(c);
        if (is.gcount() != sizeof(c)) {
            throw std::runtime_error("Unexpected end of stream");
        }
        if (is.bad() || is.fail()) {
            throw std::runtime_error("Unexpected bad state of is");
        }
    }
}
