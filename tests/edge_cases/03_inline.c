// Edge Case 03: Function Inlining
// Tests: small function inlining, recursion, mutual recursion, function pointers

// Small function - prime inlining candidate
static inline int add_three(int a, int b, int c) {
    return a + b + c;
}

// Slightly larger - might inline
static int square_and_add(int x, int y) {
    int sq = x * x;
    return sq + y;
}

// Recursive function - should NOT be inlined
int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

// Mutual recursion
int is_even(int n);
int is_odd(int n);

int is_even(int n) {
    if (n == 0) return 1;
    return is_odd(n - 1);
}

int is_odd(int n) {
    if (n == 0) return 0;
    return is_even(n - 1);
}

// Tail-recursive (compiler may optimize to loop)
int tail_sum(int n, int acc) {
    if (n == 0) return acc;
    return tail_sum(n - 1, acc + n);
}

// Function pointer - prevents inlining at call site
typedef int (*binop)(int, int);

int op_add(int a, int b) { return a + b; }
int op_mul(int a, int b) { return a * b; }
int op_sub(int a, int b) { return a - b; }

int apply_op(binop fn, int a, int b) {
    return fn(a, b);
}

// Multiple small functions
static int clamp(int val, int lo, int hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

static int max2(int a, int b) { return a > b ? a : b; }
static int min2(int a, int b) { return a < b ? a : b; }
static int abs_val(int x) { return x < 0 ? -x : x; }

int main(void) {
    int r = 0;
    r += add_three(1, 2, 3);
    r += square_and_add(5, 10);
    r += factorial(10);
    r += is_even(42);
    r += tail_sum(100, 0);
    
    binop ops[3] = {op_add, op_mul, op_sub};
    for (int i = 0; i < 3; i++) {
        r += apply_op(ops[i], 10, 5);
    }
    
    r += clamp(r, 0, 1000);
    r += max2(r, 500);
    r += min2(r, 2000);
    r += abs_val(-42);
    
    return r;
}
