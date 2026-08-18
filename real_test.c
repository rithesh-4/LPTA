// A small but realistic program for LPTA demo
// Exercises: inlining, dead code elimination, constant folding,
// loop optimizations, branch simplification, GVN, LICM, etc.

#include <stdint.h>

// Helper: clamp a value
static int clamp(int val, int lo, int hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

// String length with bounds check
static int safe_strlen(const char *s, int max) {
    int len = 0;
    while (len < max && s[len] != '\0') {
        len++;
    }
    return len;
}

// Bubble sort (intentionally naive - good for loop opts)
void bubble_sort(int *arr, int n) {
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                int tmp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = tmp;
            }
        }
    }
}

// Recursive fibonacci (tests inlining + tail call opts)
int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

// Compute dot product of two arrays
int32_t dot_product(const int32_t *a, const int32_t *b, int n) {
    int32_t sum = 0;
    for (int i = 0; i < n; i++) {
        sum += a[i] * b[i];
    }
    return sum;
}

// Matrix multiply (C = A * B, n x n)
void mat_mul(const int32_t *A, const int32_t *B, int32_t *C, int n) {
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            int32_t acc = 0;
            for (int k = 0; k < n; k++) {
                acc += A[i * n + k] * B[k * n + j];
            }
            C[i * n + j] = acc;
        }
    }
}

// Dead code path + constant folding
int compute_with_dead_code(int x) {
    int a = x * 2;
    int b = a + 1;
    int unused = b * 42;      // dead code - should be eliminated
    int c = 3 * 7;            // constant fold to 21
    int d = (x > 0) ? x : 0; // branchless select
    int e = d + c;            // d + 21
    return e;
}

// Switch statement (good for JumpThreading + SimplifyCFG)
int classify(int x) {
    switch (x) {
        case 0: return -1;
        case 1: return 0;
        case 2: return 1;
        case 3: return 2;
        default: return x;
    }
}

// Population count (bit manipulation)
int popcount(uint64_t x) {
    int count = 0;
    while (x) {
        count += x & 1;
        x >>= 1;
    }
    return count;
}

// Main entry point
int main(int argc, char **argv) {
    int arr[] = {5, 3, 8, 1, 9, 2, 7, 4, 6};
    int n = 9;

    bubble_sort(arr, n);

    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += arr[i];
    }

    int f = fib(10);
    int dc = compute_with_dead_code(sum);
    int cl = clamp(dc, 0, 100);
    int sw = classify(f);
    int pc = popcount((uint64_t)f);

    // Return combined result
    return sum + f + cl + sw + pc;
}
