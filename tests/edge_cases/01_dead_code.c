// Edge Case 01: Dead Code Elimination
// Tests: unused variables, unreachable branches, dead functions, unused parameters

// Dead function - never called
int dead_function(int x) {
    return x * 42;
}

// Unused global
int unused_global = 999;

// Function with dead code paths
int has_dead_code(int a, int b) {
    int used = a + b;
    
    // Dead: condition always false at runtime (but compiler doesn't know)
    int dead_var = a * b;
    
    if (a > 1000) {
        // Likely dead code for small inputs
        int deep_dead = dead_var + used;
        return deep_dead;
    }
    
    return used;
}

// Function with unreachable code after return
int unreachable(int x) {
    if (x > 0) {
        return x;
        // Unreachable below
        int unreachable_var = x + 1;
        return unreachable_var;
    }
    return 0;
}

// Unused parameter (should be optimized away)
int unused_param(int used, int unused) {
    return used * 2;
}

// Function called only from dead code
int only_in_dead(int x) {
    return x + 1;
}

// Main that doesn't call dead_function
int main(void) {
    int result = has_dead_code(10, 20);
    result += unreachable(result);
    result += unused_param(result, 999);
    return result;
}
