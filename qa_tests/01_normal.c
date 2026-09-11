#include <stdio.h>

int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int main(void) {
    int val = factorial(5);
    printf("Factorial of 5 is %d\n", val);
    return 0;
}
