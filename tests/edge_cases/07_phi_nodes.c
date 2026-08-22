// Edge Case 07: PHI Nodes / SSA Form
// Tests: complex control flow, diamond patterns, merge points

// Diamond pattern (classic SSA merge)
int diamond(int x, int y) {
    int result;
    if (x > 0) {
        result = x + y;
    } else {
        result = x - y;
    }
    return result * 2;
}

// Three-way merge
int three_way_merge(int x) {
    int v;
    if (x > 100) {
        v = x * 2;
    } else if (x > 50) {
        v = x + 10;
    } else {
        v = x - 5;
    }
    return v;
}

// Loop with multiple phi nodes
int multi_phi(int n) {
    int a = 0, b = 1;
    for (int i = 0; i < n; i++) {
        int temp = a + b;
        a = b;
        b = temp;
    }
    return a + b;
}

// Nested diamonds
int nested_diamond(int a, int b, int c) {
    int x, y;
    if (a > 0) {
        if (b > 0) {
            x = a + b;
        } else {
            x = a - b;
        }
    } else {
        if (c > 0) {
            x = a + c;
        } else {
            x = a - c;
        }
    }
    
    if (x > 0) {
        y = x * 2;
    } else {
        y = -x;
    }
    return y;
}

// Switch merge point
int switch_merge(int x) {
    int v;
    switch (x % 5) {
        case 0: v = 10; break;
        case 1: v = 20; break;
        case 2: v = 30; break;
        case 3: v = 40; break;
        case 4: v = 50; break;
        default: v = 0;
    }
    // All paths merge here - v has a phi
    return v + 1;
}

// Complex: multiple branches merge at multiple points
int complex_merge(int a, int b, int c) {
    int x, y, z;
    
    if (a > 0) x = 1; else x = 0;
    if (b > 0) y = 1; else y = 0;
    if (c > 0) z = 1; else z = 0;
    
    // Three independent diamonds, results merged
    return x + y + z;
}

// Chained conditions with shared code
int chained(int a, int b, int c) {
    int r = 0;
    if (a > 0) r += a;
    if (b > 0) r += b;
    if (c > 0) r += c;
    // r has a phi for each branch
    return r;
}

// Fibonacci with phi
int fib(int n) {
    if (n <= 0) return 0;
    if (n == 1) return 1;
    int prev2 = 0, prev1 = 1;
    for (int i = 2; i <= n; i++) {
        int curr = prev1 + prev2;
        prev2 = prev1;
        prev1 = curr;
    }
    return prev1;
}

// Loop-carried dependency through phi
int dependency(int n) {
    int acc = 0;
    for (int i = 1; i <= n; i++) {
        acc = acc + i;  // loop-carried dependency
    }
    return acc;
}

// Multiple exits creating phi
int multi_exit(int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        if (i % 7 == 0) continue;  // skip
        if (i > 50) break;         // early exit
        sum += i;
    }
    return sum;
}

int main(void) {
    int r = 0;
    r += diamond(10, 5);
    r += three_way_merge(75);
    r += multi_phi(20);
    r += nested_diamond(5, -3, 7);
    r += switch_merge(3);
    r += complex_merge(1, -1, 1);
    r += chained(5, -1, 3);
    r += fib(20);
    r += dependency(100);
    r += multi_exit(100);
    return r;
}
