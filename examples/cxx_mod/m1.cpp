module;
#include <iostream>
export module a;
import b.v1;
import c;
export int run() {
    return b_value() + c_value();
}
