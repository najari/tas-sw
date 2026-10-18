/* M25-3 ELF test program (built with the HighTec TriCore toolchain) */
volatile unsigned g_counter;

int add(int a, int b) {
    return a + b;
}

int mul_add(int a, int b, int c) {
    return a * b + c;
}

int main(void) {
    int i;
    for (i = 0; i < 10; i++) {
        g_counter = add(g_counter, i);
    }
    return mul_add(g_counter, 2, 1);
}
