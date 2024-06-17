#include <stdio.h>

void fibo(int n) {
    int a = 1, b = 1, next;
    
    if (n == 1) {
        printf("%d\n", a);
        return;
    } else if (n == 2) {
        printf("%d %d\n", a, b);
        return;
    }
    
    printf("%d %d", a, b);
    
    for (int i = 3; i <= n; i++) {
        next = a + b;
        printf(" %d", next);
        a = b;
        b = next;
    }
    
}


int main() {
    int var;
    scanf("%d", &var);
    
    fibo(var);
    
    return 0;
}


