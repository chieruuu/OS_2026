#include <stdio.h>
#include <ulib.h>

int zero;

int
main(void) {
    // Test the RISC-V DIVW result directly; C division by zero is undefined.
    int quotient;
    asm volatile("divw %0, %1, %2" : "=r"(quotient) : "r"(1), "r"(zero));
    cprintf("value is %d.\n", quotient);
    panic("FAIL: T.T\n");
}

