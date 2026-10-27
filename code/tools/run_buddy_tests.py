#!/usr/bin/env python3
"""Build and run the actual buddy allocator on the host, without build changes."""

import os
from pathlib import Path
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    negative_cases = (
        "alloc-zero", "free-zero", "double-free", "interior-free",
        "wrong-request", "reserved-free", "free-page-free", "out-of-range-free",
        "unaligned-descriptor", "register-zero", "register-overlap",
        "register-out-of-range", "metadata-capacity",
    )
    with tempfile.TemporaryDirectory(prefix="lab2-buddy-host-") as directory:
        executable = Path(directory) / "test_buddy"
        command = [
            os.environ.get("CC", "gcc"), "-std=gnu99", "-Wall", "-Werror",
            "-Wno-unused", "-fno-builtin", "-fno-pie", "-no-pie",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-g",
        ]
        for include in ("libs", "kern/mm", "kern/debug", "kern/driver"):
            command.extend(["-I", str(root / include)])
        command.extend([str(root / "tools/test_buddy.c"), "-o", str(executable)])
        subprocess.run(command, check=True)
        environment = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
        subprocess.run([str(executable)], check=True, env=environment)
        for case in negative_cases:
            result = subprocess.run(
                [str(executable), case], capture_output=True, text=True,
                env=environment,
            )
            if result.returncode != 77 or "EXPECTED PANIC" not in result.stdout:
                raise RuntimeError(
                    f"{case}: exit {result.returncode}\n"
                    f"{result.stdout}{result.stderr}"
                )
            print(f"PASS: rejected {case}", flush=True)
        print(f"[Buddy Negative Test] PASS ({len(negative_cases)} cases)", flush=True)


if __name__ == "__main__":
    main()
