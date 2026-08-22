// Edge Case 08: Global Variables
// Tests: constant propagation, global optimization, tentative definitions

// Constant globals (should be optimized away)
const int CONST_A = 42;
const int CONST_B = 100;
const int CONST_SUM = 42 + 100;  // compile-time constant

// Mutable globals
int mutable_global = 0;
int counter = 0;

// Static globals (file scope)
static int static_global = 10;
static const int static_const = 20;

// Global array
int global_arr[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

// Tentative definition
int tentative;  // tentative definition

// Function that modifies globals
int increment_counter(void) {
    counter++;
    return counter;
}

int read_and_modify(int x) {
    mutable_global = x * 2;
    return mutable_global + CONST_A;
}

// Constant propagation through globals
int use_constants(void) {
    int a = CONST_A;
    int b = CONST_B;
    int c = CONST_SUM;
    return a + b + c;
}

// Global used as lookup table
int lookup(int idx) {
    if (idx >= 0 && idx < 10) {
        return global_arr[idx];
    }
    return -1;
}

// Static local (persists across calls)
int static_local_func(void) {
    static int call_count = 0;
    call_count++;
    return call_count;
}

// Multiple translation units (simulated via extern)
extern int shared_state;

int read_shared(void) {
    return shared_state;
}

// Global function pointer
typedef int (*gfn)(int);
int double_it(int x) { return x * 2; }
int square_it(int x) { return x * x; }
gfn global_fn_ptr = double_it;

int apply_global_fn(int x) {
    return global_fn_ptr(x);
}

int main(void) {
    int r = 0;
    r += use_constants();
    r += increment_counter();
    r += increment_counter();
    r += read_and_modify(21);
    r += lookup(5);
    r += static_local_func();
    r += static_local_func();
    r += apply_global_fn(5);
    
    mutable_global = r;
    return r + mutable_global;
}
