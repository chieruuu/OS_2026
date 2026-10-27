#!/usr/bin/env python3
"""Build and run the actual SLUB allocator on the host, without build changes."""

import os
from pathlib import Path
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    negative_cases = (
        "double-free", "interior-pointer", "unallocated-slot", "slab-header",
        "foreign-pointer", "reserved-page", "freed-slab", "large-interior",
        "large-double-free", "reinitialize",
    )
    with tempfile.TemporaryDirectory(prefix="lab2-slub-host-") as directory:
        executable = Path(directory) / "test_slub"
        command = [
            os.environ.get("CC", "gcc"), "-std=gnu99", "-Wall", "-Werror",
            "-Wno-unused", "-fno-builtin", "-fno-pie", "-no-pie",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-g",
        ]
        for include in ("libs", "kern/mm", "kern/debug", "kern/driver"):
            command.extend(["-I", str(root / include)])
        command.extend([str(root / "tools/test_slub.c"), "-o", str(executable)])
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
        print(f"[SLUB Negative Test] PASS ({len(negative_cases)} cases)", flush=True)


if __name__ == "__main__":
    main()
