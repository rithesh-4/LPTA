// Edge Case 02: Loop Optimizations
// Tests: loop unrolling, LICM, induction variables, nested loops, trip count

// Simple counted loop
int simple_loop(int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += i;
    }
    return sum;
}

// Nested loops (matrix-style)
int nested_loops(int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            sum += i * j;
        }
    }
    return sum;
}

// Loop-invariant code motion candidate
int licm_candidate(int n, int x) {
    int sum = 0;
    // x * 2 is loop-invariant
    for (int i = 0; i < n; i++) {
        sum += x * 2 + i;
    }
    return sum;
}

// Do-while loop
int dowhile_loop(int n) {
    int sum = 0;
    int i = 0;
    do {
        sum += i;
        i++;
    } while (i < n);
    return sum;
}

// While loop with early break
int early_break(int n) {
    int i = 0;
    while (i < n) {
        if (i == 50) break;
        i++;
    }
    return i;
}

// Nested loops with different bounds
int triangular(int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j <= i; j++) {
            sum += 1;
        }
    }
    return sum;
}

// Loop with reduction variable
int reduction_loop(int n) {
    int product = 1;
    for (int i = 1; i <= n && i < 20; i++) {
        product *= i;
    }
    return product;
}

// Infinite loop (optimization barrier)
int infinite_loop(void) {
    int i = 0;
    while (1) {
        i++;
        if (i > 1000000) return i;
    }
}

int main(void) {
    int r = 0;
    r += simple_loop(100);
    r += nested_loops(10);
    r += licm_candidate(50, 5);
    r += dowhile_loop(100);
    r += early_break(100);
    r += triangular(50);
    r += reduction_loop(10);
    r += infinite_loop();
    return r;
}
