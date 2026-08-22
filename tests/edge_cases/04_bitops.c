// Edge Case 04: Bitwise Operations
// Tests: shifts, masks, XOR tricks, bit counting, power-of-2 checks

// Count set bits (Brian Kernighan's)
int popcount(unsigned int x) {
    int count = 0;
    while (x) {
        x &= (x - 1);
        count++;
    }
    return count;
}

// Check if power of 2
int is_power_of_2(unsigned int x) {
    return x && !(x & (x - 1));
}

// XOR swap (should be optimized to normal swap)
void xor_swap(int *a, int *b) {
    *a ^= *b;
    *b ^= *a;
    *a ^= *b;
}

// Bit manipulation: set, clear, toggle, test
int set_bit(int x, int pos) { return x | (1 << pos); }
int clear_bit(int x, int pos) { return x & ~(1 << pos); }
int toggle_bit(int x, int pos) { return x ^ (1 << pos); }
int test_bit(int x, int pos) { return (x >> pos) & 1; }

// Reverse bits
unsigned int reverse_bits(unsigned int x) {
    unsigned int result = 0;
    for (int i = 0; i < 32; i++) {
        result = (result << 1) | (x & 1);
        x >>= 1;
    }
    return result;
}

// Count leading zeros (simplified)
int clz(unsigned int x) {
    if (x == 0) return 32;
    int count = 0;
    for (int i = 31; i >= 0; i--) {
        if (x & (1u << i)) break;
        count++;
    }
    return count;
}

// Bit field extraction
unsigned int extract_bits(unsigned int x, int start, int width) {
    unsigned int mask = (1u << width) - 1;
    return (x >> start) & mask;
}

// Round up to next power of 2
unsigned int next_pow2(unsigned int x) {
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x++;
    return x;
}

// Integer logarithm base 2
int ilog2(unsigned int x) {
    int r = 0;
    while (x >>= 1) r++;
    return r;
}

// Absolute value using bit tricks (branchless)
int abs_branchless(int x) {
    int mask = x >> 31;
    return (x + mask) ^ mask;
}

int main(void) {
    int r = 0;
    r += popcount(0xFF);
    r += is_power_of_2(1024);
    r += set_bit(0, 5);
    r += clear_bit(0xFF, 3);
    r += toggle_bit(0, 7);
    r += test_bit(0xFF, 4);
    r += reverse_bits(0x12345678);
    r += clz(0x00FF0000);
    r += extract_bits(0xDEADBEEF, 8, 8);
    r += next_pow2(100);
    r += ilog2(256);
    r += abs_branchless(-42);
    
    int a = 42, b = 99;
    xor_swap(&a, &b);
    r += a + b;
    
    return r;
}
