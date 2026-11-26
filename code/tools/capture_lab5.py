#!/usr/bin/env python3
"""Capture live make qemu / make grade results in xterm."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import time

root = Path(__file__).resolve().parents[2]
report = root / 'report'
code = root / 'code'


def worker(stage, ready, finished):
    import pexpect

    class Output:
        def write(self, value):
            log.write(value)
            sys.stdout.write(value)
            self.flush()

        def flush(self):
            log.flush()
            sys.stdout.flush()

    name = 'qemu' if stage == 'qemu' else stage
    prompt = 'student@wsl:~/homework/os/OS_2026/code$ '
    with (report / (name + '.log')).open('w') as log:
        child = pexpect.spawn('bash', ['--noprofile', '--norc', '-i'], cwd=str(code),
                              env={**os.environ, 'PS1': prompt, 'TERM': 'xterm'},
                              encoding='utf-8', timeout=180, dimensions=(130, 126))
        child.logfile_read = Output()
        try:
            child.expect_exact(prompt)
            child.sendline('make grade' if stage == 'grade' else 'make qemu')
            if stage == 'grade':
                child.expect_exact(prompt)
                assert 'Total Score: 130/130' in child.before
            else:
                child.expect_exact('K> ')
                assert 'init check memory pass.' in child.before
                assert 'kernel panic' not in child.before and 'user panic' not in child.before
                if stage == 'challenge':
                    assert 'cowtest pass:' in child.before
            Path(ready).touch()
            while not Path(finished).exists():
                time.sleep(.05)
            if stage != 'grade':
                child.send('\x01x')
                child.expect_exact(prompt)
            child.sendline('exit')
            child.expect(pexpect.EOF)
        finally:
            child.close(force=True)


def build(target=None):
    subprocess.run(['make', 'clean'], cwd=code, check=True, stdout=subprocess.DEVNULL)
    with (report / 'build.log').open('w') as log:
        subprocess.run(['make', '-j4'] + ([target] if target else []), cwd=code,
                       check=True, stdout=log, stderr=subprocess.STDOUT)


def capture():
    images = report / 'image'
    images.mkdir(exist_ok=True)
    display = ':' + str(100 + os.getpid() % 5000)
    env = dict(os.environ, DISPLAY=display)
    server = subprocess.Popen(['Xvfb', display, '-screen', '0', '2000x2600x24', '-nolisten', 'tcp'],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(100):
            if Path('/tmp/.X11-unix/X' + display[1:]).exists():
                break
            time.sleep(.05)
        build()
        with tempfile.TemporaryDirectory(prefix='lab5-live-') as temp:
            for stage in ('qemu', 'grade', 'challenge'):
                if stage == 'challenge':
                    build('build-cowtest')
                ready = Path(temp) / (stage + '.ready')
                finished = Path(temp) / (stage + '.finished')
                terminal = subprocess.Popen([
                    'xterm', '-geometry', '126x130+0+0', '-fa', 'DejaVu Sans Mono', '-fs', '9',
                    '-bg', '#111111', '-fg', '#eeeeee', '-b', '12', '-title', 'Lab5-' + stage,
                    '-e', sys.executable, str(Path(__file__).resolve()), '--worker', stage,
                    str(ready), str(finished)
                ], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                try:
                    deadline = time.monotonic() + 180
                    while not ready.exists():
                        if terminal.poll() is not None:
                            raise RuntimeError(stage + ' terminal exited before the result')
                        if time.monotonic() > deadline:
                            raise RuntimeError(stage + ' timed out; see report/' + stage + '.log')
                        time.sleep(.1)
                    time.sleep(.2)
                    windows = subprocess.check_output(['xwininfo', '-root', '-tree'], env=env, text=True)
                    window = next(line.split()[0] for line in windows.splitlines() if '"Lab5-' + stage + '"' in line)
                    subprocess.run(['import', '-window', window, str(images / (stage + '.png'))], env=env, check=True)
                    subprocess.run(['convert', str(images / (stage + '.png')), '-trim', '+repage',
                                    '-bordercolor', '#111111', '-border', '12',
                                    str(images / (stage + '.png'))], check=True)
                    finished.touch()
                    terminal.wait(timeout=10)
                    print(stage + ': live terminal captured', flush=True)
                finally:
                    if terminal.poll() is None:
                        terminal.terminate()
                        terminal.wait()
        build()
    finally:
        server.terminate()
        server.wait()


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--worker':
        worker(*sys.argv[2:])
    else:
        capture()
