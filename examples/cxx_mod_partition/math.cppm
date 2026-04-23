export module math;

export import :ops;

export int add(int a, int b) {
    return add_impl(a, b);
}
