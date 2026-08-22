// Edge Case 05: Switch Statements
// Tests: dense switch, sparse switch, jump tables, fallthrough, nested switch

// Dense switch (should become jump table)
int dense_switch(int x) {
    switch (x) {
        case 0: return 10;
        case 1: return 20;
        case 2: return 30;
        case 3: return 40;
        case 4: return 50;
        case 5: return 60;
        case 6: return 70;
        case 7: return 80;
        case 8: return 90;
        case 9: return 100;
        default: return -1;
    }
}

// Sparse switch (should become comparison chain)
int sparse_switch(int x) {
    switch (x) {
        case 1:    return 100;
        case 100:  return 200;
        case 999:  return 300;
        case 5000: return 400;
        case 9999: return 500;
        default:   return 0;
    }
}

// Switch with fallthrough
int fallthrough_switch(int x) {
    int result = 0;
    switch (x) {
        case 1:
            result += 10;
            // fallthrough
        case 2:
            result += 20;
            // fallthrough
        case 3:
            result += 30;
            break;
        case 4:
            result += 40;
            break;
        default:
            result = -1;
    }
    return result;
}

// Nested switch
int nested_switch(int x, int y) {
    int result = 0;
    switch (x) {
        case 1:
            switch (y) {
                case 1: result = 11; break;
                case 2: result = 12; break;
                default: result = 10; break;
            }
            break;
        case 2:
            switch (y) {
                case 1: result = 21; break;
                case 2: result = 22; break;
                default: result = 20; break;
            }
            break;
        default:
            result = 0;
    }
    return result;
}

// Switch with expressions in cases
int expr_switch(int x) {
    int a = 5;
    int b = 10;
    switch (x) {
        case 5:     return a * 2;
        case 10:    return b * 3;
        case 15:    return a + b;
        case 20:    return a * b;
        case 25:    return a - b;
        default:    return 0;
    }
}

// Large switch (20+ cases)
int large_switch(int x) {
    switch (x) {
        case 1: return 1; case 2: return 4; case 3: return 9;
        case 4: return 16; case 5: return 25; case 6: return 36;
        case 7: return 49; case 8: return 64; case 9: return 81;
        case 10: return 100; case 11: return 121; case 12: return 144;
        case 13: return 169; case 14: return 196; case 15: return 225;
        case 16: return 256; case 17: return 289; case 18: return 324;
        case 19: return 361; case 20: return 400;
        default: return -1;
    }
}

// Switch in loop
int switch_in_loop(int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        switch (i % 4) {
            case 0: sum += i; break;
            case 1: sum -= i; break;
            case 2: sum += i * 2; break;
            case 3: sum -= i * 2; break;
        }
    }
    return sum;
}

int main(void) {
    int r = 0;
    r += dense_switch(5);
    r += sparse_switch(999);
    r += fallthrough_switch(2);
    r += nested_switch(1, 2);
    r += expr_switch(10);
    r += large_switch(15);
    r += switch_in_loop(100);
    return r;
}
