#include <ulib.h>
#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <elf.h>
#include <error.h>

static unsigned char image[8193];

int main(void)
{
    register long number asm("a0") = 0;
    asm volatile("ecall" : "+r"(number) : : "memory");
    assert(number == -E_INVAL);
    assert(sys_exec(NULL, 4, image, sizeof(image)) == -E_INVAL);
    struct elfhdr *elf = (void *)image;
    elf->e_magic = ELF_MAGIC;
    elf->e_entry = 0x900000;
    elf->e_phoff = sizeof(*elf);
    elf->e_phentsize = sizeof(struct proghdr);
    elf->e_phnum = 1;
    struct proghdr *ph = (void *)(image + elf->e_phoff);
    ph->p_type = ELF_PT_LOAD;
    ph->p_flags = ELF_PF_R | ELF_PF_X;
    ph->p_va = elf->e_entry;
    ph->p_offset = 4096;
    ph->p_filesz = 24;
    ph->p_memsz = 4096;
    // RV64: putc('E'), then exit(42), using this lab's a0/a1 syscall ABI.
    uint32_t instructions[] = {0x01e00513, 0x04500593, 0x00000073,
                               0x00100513, 0x02a00593, 0x00000073};
    memcpy(image + ph->p_offset, instructions, sizeof(instructions));
    int pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        exec("exec-child", image, sizeof(image));
        panic("exec returned after success");
    }
    int status = 0;
    assert(waitpid(pid, &status) == 0 && status == 42);
    cprintf("\nexectest pass: invalid syscall/pointer, ELF replacement, exit=42.\n");
    return 0;
}
