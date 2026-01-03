volatile unsigned long sink;

void _start(void) {
    unsigned long x = 1;
    for (int i = 0; i < 10; ++i) x = x * 3 + 1;
    sink = x;

    for (;;) { }
}
