#include <stdio.h>

int main(void) {
    int *p = NULL;
    *p = 42; // Intentionally triggers UB/segfault
    printf("Value: %d\n", *p);
    return 0;
}
