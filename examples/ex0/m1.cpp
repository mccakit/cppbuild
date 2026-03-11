module;
#include <iostream>
export module a;
import b;
import c;
export int run() {
    return b_value() + c_value();
}
