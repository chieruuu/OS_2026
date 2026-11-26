#!/usr/bin/env python3
"""Exercise the added COW/exec cases and retain real QEMU transcripts."""
from pathlib import Path
import os
import sys
import subprocess
import pexpect

os.chdir(Path(__file__).resolve().parents[1])
out = Path('../report').resolve()
out.mkdir(exist_ok=True)
cases = [(False, 'cowtest'), (False, 'exectest'), (True, 'cowtest')]
if len(sys.argv) > 1: cases = [(False, program) for program in sys.argv[1:]]
for eager, program in cases:
    tag = program + ('-eager' if eager else '')
    subprocess.run(['make', 'clean'], check=True, stdout=subprocess.DEVNULL)
    build = subprocess.run(['make', '-j4', 'build-' + program,
                            'DEFS=' + ('-DLAB5_EAGER_COPY' if eager else '')],
                           text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (out / (tag + '-build.log')).write_text(build.stdout)
    build.check_returncode()
    qemu = pexpect.spawn('qemu-system-riscv64', ['-machine', 'virt', '-nographic',
                         '-bios', 'default', '-kernel', 'bin/ucore.img'],
                         encoding='utf-8', timeout=30, echo=False)
    try:
        with (out / (tag + '.log')).open('w') as log:
            qemu.logfile_read = log
            if program in ('cowtest', 'exectest'):
                qemu.expect_exact(program + ' pass:')
            qemu.expect_exact('init check memory pass.')
            assert 'kernel panic' not in qemu.before and 'user panic' not in qemu.before
            qemu.send('\x01x')
            qemu.expect(pexpect.EOF)
        print(tag + ': PASS')
    finally:
        qemu.close(force=True)
# Leave the default COW exit build ready for make qemu / make debug.
subprocess.run(['make', 'clean'], check=True, stdout=subprocess.DEVNULL)
subprocess.run(['make', '-j4'], check=True, stdout=subprocess.DEVNULL)
