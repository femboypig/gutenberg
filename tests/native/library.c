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
