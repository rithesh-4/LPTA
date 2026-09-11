#include <stdio.h>

int main(void) {
    int *p = NULL;
    *p = 42; // Intentionally triggers SIGSEGV / UB
    printf("Value: %d\n", *p);
    return 0;
}
