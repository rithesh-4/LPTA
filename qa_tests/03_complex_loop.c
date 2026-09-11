#include <stdio.h>

void compute(int *a, int *b, int n) {
    for (int i = 0; i < n; i++) {
        a[i] = b[i] * 3 + 7;
        if (a[i] % 2 == 0) {
            a[i] /= 2;
        } else {
            a[i] = a[i] * 3 + 1;
        }
    }
}

int main(void) {
    int x[100], y[100];
    for (int i = 0; i < 100; i++) y[i] = i;
    compute(x, y, 100);
    printf("Done: %d\n", x[0]);
    return 0;
}
