#!/usr/bin/env python3
"""Run guest and host GDBs together to record one getpid ecall/sret round trip."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import pexpect

parser = argparse.ArgumentParser()
parser.add_argument('--qemu', default='/tmp/qemu-6.2-debug-build/qemu-system-riscv64')
parser.add_argument('--output', default='../report/debug')
parser.add_argument('--capture-terminal', action='store_true')
args = parser.parse_args()
os.chdir(Path(__file__).resolve().parents[1])
out = Path(args.output).resolve()
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['make', 'build-hello'], check=True, stdout=subprocess.DEVNULL)
user = subprocess.check_output(['riscv64-unknown-elf-objdump', '-d', 'obj/__user_hello.out'], text=True)
kernel = subprocess.check_output(['riscv64-unknown-elf-objdump', '-d', 'bin/kernel'], text=True)
body = user.split('<sys_getpid>:', 1)[1].split('\n\n', 1)[0]
ecall = int(re.search(r'^\s*([0-9a-f]+):.*\becall\b', body, re.M)[1], 16)
body = kernel.split('<__trapret>:', 1)[1].split('\n\n', 1)[0]
sret = int(re.search(r'^\s*([0-9a-f]+):.*\bsret\b', body, re.M)[1], 16)

logs = []
sessions = []
def start(command, argv, name):
    session = pexpect.spawn(command, argv, encoding='utf-8', timeout=45, echo=False, maxread=65536, env={**os.environ, 'TERM': 'dumb'})
    log = (out / name).open('w')
    logs.append(log)
    session.logfile_read = log
    session._capture_chunks = []
    sessions.append(session)
    session.expect_exact('(gdb) ')
    cmd(session, 'set pagination off')
    cmd(session, 'set confirm off')
    cmd(session, 'set style enabled off')
    return session

qemu = None
qemu_log = None

def wait_prompt(session):
    session.expect_exact('(gdb) ')
    session._capture_chunks.append(session.before)
    return session.before

def capture_stage(session, name, title):
    if not args.capture_terminal:
        return
    import time
    print(chr(27) + '[2J' + chr(27) + '[H', end='')
    print('Lab 5 | ' + title)
    print('GDB live session / ' + ('guest' if session is guest else 'QEMU host'))
    print('\n'.join(''.join(session._capture_chunks).splitlines()[-34:]), flush=True)
    session._capture_chunks.clear()
    image = out.parent / 'image' / (name + '.png')
    image.parent.mkdir(parents=True, exist_ok=True)
    time.sleep(.2)
    subprocess.run(['import', '-window', os.environ['WINDOWID'], str(image)], check=True)
    subprocess.run(['convert', str(image), '-trim', '+repage', '-bordercolor', '#111111',
                    '-border', '12', str(image)], check=True)

def cmd(session, command):
    command = command.replace('\n', '\\n')
    session.logfile_read.write('(gdb) ' + command + '\n')
    session.logfile_read.flush()
    session._capture_chunks.append('(gdb) ' + command + '\n')
    session.sendline(command)
    result = wait_prompt(session)
    if 'Error in' in result or 'No symbol ' in result or 'Cannot ' in result:
        raise RuntimeError(command + '\n' + result)
    return result

def run(session, command='continue'):
    session.logfile_read.write('(gdb) ' + command + '\n')
    session.logfile_read.flush()
    session._capture_chunks.append('(gdb) ' + command + '\n')
    session.sendline(command)

try:
    qemu = pexpect.spawn('make', ['debug', 'QEMU=' + args.qemu], encoding='utf-8',
                         timeout=45, echo=False, env={**os.environ, 'TERM': 'dumb'})
    qemu_log = (out / 'qemu.log').open('w')
    qemu.logfile_read = qemu_log
    # make debug starts QEMU independently of either GDB session.
    import time
    for _ in range(100):
        found = subprocess.run(['pgrep', '-f', '^' + re.escape(args.qemu) + r'( |$)'],
                               capture_output=True, text=True)
        pids = found.stdout.splitlines()
        if pids:
            break
        time.sleep(.05)
    if len(pids) != 1:
        raise RuntimeError('Expected one independently running debug QEMU: ' + str(pids))
    guest = start('riscv64-unknown-elf-gdb', ['-q', 'bin/kernel'], 'guest-gdb.log')
    cmd(guest, 'set arch riscv:rv64')
    cmd(guest, 'target remote :1234')
    cmd(guest, 'add-symbol-file obj/__user_hello.out')
    host = start('sudo', ['-n', 'gdb', '-q', args.qemu], 'host-gdb.log')
    cmd(host, 'attach ' + pids[0])
    run(host)
    cmd(guest, f'break *{ecall:#x}')
    cmd(guest, 'continue')
    cmd(guest, 'x/i $pc')
    cmd(guest, 'info registers pc a0 sp sstatus sscratch')
    capture_stage(guest, 'gdb-01-user-ecall', '1. User ecall and syscall registers')
    host.sendcontrol('c')
    wait_prompt(host)
    cmd(host, f'break trans_ecall if ctx->base.pc_next == {ecall:#x}')
    cmd(host, 'break riscv_cpu_do_interrupt if cs->exception_index == 8 && ((RISCVCPU *)cs)->env.priv == 0')
    cmd(host, f'break helper_sret if env->sepc == {ecall + 4:#x}')
    run(host)
    run(guest, 'si')
    # A breakpoint invalidates the guest TB, so this step translates the ecall again.
    wait_prompt(host)
    cmd(host, 'bt 4')
    cmd(host, 'p/x ctx->base.pc_next')
    cmd(host, 'up')
    cmd(host, 'p/x insn')
    cmd(host, 'down')
    capture_stage(host, 'gdb-02-ecall-translation', '2. QEMU TCG translates ecall')
    cmd(host, 'disable 1')
    run(host)
    wait_prompt(host)
    cmd(host, 'set $env = &((RISCVCPU *)cs)->env')
    cmd(host, 'printf "ECALL before: priv=%lu pc=%#lx a0=%lu\n", $env->priv, $env->pc, $env->gpr[10]')
    cmd(host, 'finish')
    cmd(host, 'printf "ECALL after: priv=%lu pc=%#lx sepc=%#lx scause=%lu\n", $env->priv, $env->pc, $env->sepc, $env->scause')
    capture_stage(host, 'gdb-03-trap-entry', '3. Exception entry from U to S mode')
    cmd(host, 'disable 2')
    run(host)
    wait_prompt(guest)
    cmd(guest, 'x/i $pc')
    cmd(guest, 'info registers pc sepc scause sstatus sscratch')
    cmd(guest, 'break kern/syscall/syscall.c:syscall')
    cmd(guest, 'continue')
    cmd(guest, 'p *current->tf')
    capture_stage(guest, 'gdb-04-kernel-syscall', '4. Kernel syscall and trapframe')
    cmd(guest, f'tbreak *{sret:#x}')
    cmd(guest, 'continue')
    cmd(guest, 'x/i $pc')
    cmd(guest, 'info registers pc a0 sp sepc sstatus sscratch')
    capture_stage(guest, 'gdb-05-before-sret', '5. Restored context before sret')
    host.sendcontrol('c')
    wait_prompt(host)
    cmd(host, f'break trans_sret if ctx->base.pc_next == {sret:#x}')
    run(host)
    run(guest, 'si')
    wait_prompt(host)
    cmd(host, 'p/x ctx->base.pc_next')
    cmd(host, 'up')
    cmd(host, 'p/x insn')
    cmd(host, 'down')
    cmd(host, 'bt 4')
    capture_stage(host, 'gdb-06-sret-translation', '6. QEMU TCG translates sret')
    cmd(host, 'disable 4')
    run(host)
    wait_prompt(host)
    cmd(host, 'set $env = env')
    cmd(host, 'printf "SRET before: priv=%lu sepc=%#lx SPP=%lu SPIE=%lu\n", env->priv, env->sepc, (env->mstatus >> 8) & 1, (env->mstatus >> 5) & 1')
    cmd(host, 'bt 4')
    cmd(host, 'finish')
    cmd(host, 'printf "SRET after: priv=%lu SIE=%lu SPIE=%lu\n", $env->priv, ($env->mstatus >> 1) & 1, ($env->mstatus >> 5) & 1')
    capture_stage(host, 'gdb-07-sret-helper', '7. helper_sret restores privilege state')
    cmd(host, 'disable 3')
    run(host)
    wait_prompt(guest)
    result = cmd(guest, 'printf "USER returned: pc=%#lx a0=%lu\n", $pc, $a0')
    assert f'pc={ecall + 4:#x} a0=2' in result, result
    capture_stage(guest, 'gdb-08-user-return', '8. Return to user at ecall + 4')
    cmd(guest, 'detach')
    host.sendcontrol('c')
    wait_prompt(host)
    cmd(host, 'detach')
    (out / 'result.txt').write_text(f'PASS: U-mode getpid ecall {ecall:#x} -> S-mode handler -> sret {sret:#x} -> U-mode {ecall + 4:#x}, a0=2\n')
    print((out / 'result.txt').read_text())
finally:
    for session in reversed(sessions):
        session.close(force=True)
    for log in logs:
        log.close()
    if qemu is not None:
        qemu.send('\x01x')
        qemu.expect(pexpect.EOF)
        qemu.close(force=True)
    if qemu_log is not None:
        qemu_log.close()
