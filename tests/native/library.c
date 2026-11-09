extern long gutenberg_host_add(long left, long right);
extern long gutenberg_host_factor;

static long value;

__attribute__((constructor)) static void initialize(void) {
    value = gutenberg_host_add(3, 4);
}

__attribute__((destructor)) static void finalize(void) {
    (void)gutenberg_host_add(-7, 0);
}

long native_value(void) {
    return value;
}

long native_accumulate(long delta) {
    value = gutenberg_host_add(value, delta) * gutenberg_host_factor;
    return value;
}

long* native_address(void) {
    return &value;
}

long* native_external_address(void) {
    return &gutenberg_host_factor;
}

double native_mixed(long a0, double f0, long a1, float f1, long a2, double f2, long a3, float f3, long a4,
                    double f4, long a5, float f5, long a6, double f6, long a7, float f7, long a8, double f8,
                    long a9, float f9) {
    return a0 + 2 * a1 + 3 * a2 + 4 * a3 + 5 * a4 + 6 * a5 + 7 * a6 + 8 * a7 + 9 * a8 + 10 * a9 + f0 + f1 +
           f2 + f3 + f4 + f5 + f6 + f7 + f8 + f9;
}

float native_single(float left, float right) {
    return left * right + 0.25f;
}
