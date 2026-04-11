module;
#include <string>
export module strlib;
export namespace strlib {
    std::string greet(const std::string &name) {
        return "Hello, " + name + "!";
    }
}
