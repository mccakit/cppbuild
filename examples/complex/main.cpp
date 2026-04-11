#include <iostream>
extern "C" {
#include "mathlib.h"
}
import strlib;
import config;

int main() {
    std::cout << strlib::greet("world") << std::endl;
    std::cout << add(3, 4) << std::endl;
    std::cout << mul(5, 6) << std::endl;
    std::cout << config_value() << std::endl;
    return 0;
}
