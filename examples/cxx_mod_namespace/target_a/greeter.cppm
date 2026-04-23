export module greeter;

import util;

export const char *greet() {
    return "hello from target A";
}

export int greeter_value() { return util_value() * 10; }
