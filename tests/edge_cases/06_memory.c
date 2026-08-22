// Edge Case 06: Memory Operations
// Tests: load/store elimination, dead store elimination, GVN, aliasing

// Dead store elimination candidate
int dse_candidate(int n) {
    int x = 10;   // dead store
    x = 20;       // dead store
    x = 30;       // this is the one that matters
    return x + n;
}

// Redundant load elimination
int redundant_load(int *arr, int idx) {
    int a = arr[idx];  // load 1
    int b = arr[idx];  // redundant load (GVN should eliminate)
    return a + b;
}

// Store forwarding
int store_forwarding(int x) {
    int y;
    y = x + 1;     // store
    int z = y;     // should forward directly from store
    return z * 2;
}

// Memory through pointer (aliasing)
int pointer_aliasing(int *a, int *b, int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += a[i];
        b[i] = a[i] * 2;
    }
    return sum;
}

// Strict aliasing violation potential
int strict_aliasing(int *ip, float *fp) {
    *ip = 42;
    // This is UB in C but exists in real code
    // Compiler may or may not optimize across this
    return *ip;
}

// Stack-allocated arrays
int stack_array(int n) {
    int arr[100];
    for (int i = 0; i < 100 && i < n; i++) {
        arr[i] = i * i;
    }
    int sum = 0;
    for (int i = 0; i < 100 && i < n; i++) {
        sum += arr[i];
    }
    return sum;
}

// Struct member access
typedef struct {
    int x;
    int y;
    int z;
} Point;

int struct_ops(Point *p) {
    int sum = p->x + p->y + p->z;
    p->x = sum;
    return sum;
}

// Memset/memcpy patterns (compiler recognizes these)
int memset_pattern(int *arr, int n) {
    for (int i = 0; i < n; i++) {
        arr[i] = 0;  // should become memset
    }
    return arr[0];
}

int copy_pattern(int *dst, int *src, int n) {
    for (int i = 0; i < n; i++) {
        dst[i] = src[i];  // should become memcpy
    }
    return dst[0];
}

// Volatile-like pattern (optimization barrier)
int volatile_pattern(int x) {
    int *p = &x;
    *p = 10;
    // Without volatile, compiler may reorder
    *p = 20;
    return *p;
}

int main(void) {
    int arr[100];
    for (int i = 0; i < 100; i++) arr[i] = i;
    
    int r = 0;
    r += dse_candidate(5);
    r += redundant_load(arr, 10);
    r += store_forwarding(42);
    r += pointer_aliasing(arr, arr, 50);
    
    Point p = {1, 2, 3};
    r += struct_ops(&p);
    
    int zero_arr[100];
    r += memset_pattern(zero_arr, 100);
    r += copy_pattern(zero_arr, arr, 50);
    r += volatile_pattern(0);
    
    return r;
}
