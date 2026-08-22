/*
 * LPTA Judge Demo — 7 functions, each testing a different optimization.
 * Compile: clang -O0 -emit-llvm -S -o demo.ll demo.c
 * The IR has ~200 instructions; after optimization: ~30-50.
 */

/* 1. Dead code: unreachable code after return */
int dead_code(int x) {
    int a = x + 1;
    return a;
    int b = a * 2;  /* unreachable */
    return b;
}

/* 2. Constant folding: compile-time arithmetic */
int constant_fold(int x) {
    int a = 5 + 3;
    int b = x * 1;
    int c = x + 0;
    return a + b + c;
}

/* 3. Loop optimization: simple sum */
int loop_sum(int n) {
    int sum = 0;
    for (int i = 0; i < n; i++)
        sum += i;
    return sum;
}

/* 4. Inlining: small helpers called once */
static int square(int x) { return x * x; }
static int cube(int x) { return x * x * x; }
int inline_test(int x) {
    return square(x) + cube(x);
}

/* 5. Bitwise simplification */
int bitwise(int x) {
    int a = x & 0;       /* 0 */
    int b = x | 0;       /* x */
    int c = x ^ 0;       /* x */
    int d = x & x;       /* x */
    return a + b + c + d;
}

/* 6. Conditional: branch simplification */
int conditional(int x) {
    if (x > 0)
        return x;
    return 0;
}

/* 7. Tail recursion: factorial */
int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}
