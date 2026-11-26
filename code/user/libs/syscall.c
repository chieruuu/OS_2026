#include <defs.h>
#include <unistd.h>
#include <stdarg.h>
#include <syscall.h>

#define MAX_ARGS            5

static inline int
syscall(int64_t num, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t p4, uint64_t p5)
{
    register uint64_t a0 asm("a0") = num;
    register uint64_t a1 asm("a1") = p1;
    register uint64_t a2 asm("a2") = p2;
    register uint64_t a3 asm("a3") = p3;
    register uint64_t a4 asm("a4") = p4;
    register uint64_t a5 asm("a5") = p5;
    asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5) : "memory");
    return (int)a0;
}

int
sys_exit(int64_t error_code) {
    return syscall(SYS_exit, error_code, 0, 0, 0, 0);
}

int
sys_fork(void) {
    return syscall(SYS_fork, 0, 0, 0, 0, 0);
}

int
sys_wait(int64_t pid, int *store) {
    return syscall(SYS_wait, pid, (uintptr_t)store, 0, 0, 0);
}

int
sys_yield(void) {
    return syscall(SYS_yield, 0, 0, 0, 0, 0);
}

int
sys_kill(int64_t pid) {
    return syscall(SYS_kill, pid, 0, 0, 0, 0);
}

int
sys_getpid(void) {
    return syscall(SYS_getpid, 0, 0, 0, 0, 0);
}

int
sys_putc(int64_t c) {
    return syscall(SYS_putc, c, 0, 0, 0, 0);
}

int
sys_pgdir(void) {
    return syscall(SYS_pgdir, 0, 0, 0, 0, 0);
}


int sys_exec(const char *name, size_t len, unsigned char *binary, size_t size)
{
    return syscall(SYS_exec, (uintptr_t)name, len, (uintptr_t)binary, size, 0);
}
