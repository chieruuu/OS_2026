#include <ulib.h>
#include <stdio.h>
#include <string.h>

static volatile unsigned char data[3 * 4096] __attribute__((aligned(4096)));
static volatile int result = 17;

int main(void)
{
    memset((void *)data, 0x5a, sizeof(data));
    int first = fork();
    assert(first >= 0);
    if (first == 0) {
        data[0] = 0xa1;
        data[4096] = 0xa2;
        int grandchild = fork();
        assert(grandchild >= 0);
        if (grandchild == 0) {
            assert(data[0] == 0xa1);
            data[0] = 0xb1;
            exit(23);
        }
        assert(waitpid(grandchild, (int *)&result) == 0 && result == 23);
        assert(data[0] == 0xa1 && data[4096] == 0xa2);
        exit(31);
    }
    int second = fork();
    assert(second >= 0);
    if (second == 0) {
        for (int i = 0; i < 10; i++) yield();
        assert(result == 17 && data[0] == 0x5a && data[4096] == 0x5a);
        data[8192] = 0xc1;
        exit(47);
    }
    assert(waitpid(first, (int *)&result) == 0 && result == 31);
    assert(waitpid(second, (int *)&result) == 0 && result == 47);
    for (int i = 0; i < sizeof(data); i++) assert(data[i] == 0x5a);
    cprintf("cowtest pass: nested fork, parent/child isolation, wait copyout, reclaim.\n");
    return 0;
}
