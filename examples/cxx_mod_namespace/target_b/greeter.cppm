export module greeter;

import util;

export const char *greet() {
    return "hello from target B";
}

export int greeter_value() { return util_value() * 100; }
