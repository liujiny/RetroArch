#!/usr/bin/env python3
"""Build the real PS4 driver with the existing asynchronous AudioOut mock."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--output", required=True, type=Path)
parser.add_argument("--baseline", action="store_true")
args = parser.parse_args()
p = Path(__file__).resolve().parents[2]
q = args.output.resolve()
q.mkdir(parents=True, exist_ok=True)
driver = p / "audio/drivers/ps4_audio.c"
if args.baseline:
    driver = q / "baseline.c"
    driver.write_bytes(subprocess.check_output(["git", "-C", str(p), "show",
        "f75482b01e5d62500fb496a8cc1af25dc5c3e492:audio/drivers/ps4_audio.c"]))
base = ["gcc", "-std=gnu89", "-g", "-O1", "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer", "-DHAVE_THREADS", "-D_GNU_SOURCE"]
base += ["-I" + str(p / rel) for rel in [".", "audio/drivers",
         "libretro-common/include", "tools/platform_stubs/orbis", "tools/platform_stubs/vita"]]
sources = [("driver", driver, ["-Dsthread_create=mock_sthread_create", "-Dfree=ps4_lifetime_free"]),
           ("mock", p / "tests/ps4_audio_buffer/lifecycle_mock.c", []),
           ("threads", p / "libretro-common/rthreads/rthreads.c", []),
           ("eventcount", p / "libretro-common/rthreads/retro_eventcount.c", []),
           ("test", p / "tests/ps4_audio_buffer/lifecycle.c", [])]
with (q / "build.log").open("w") as log:
    for name, src, extra in sources:
        subprocess.run(base + extra + ["-c", str(src), "-o", str(q / (name + ".o"))],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(["gcc", "-fsanitize=address,undefined", "-o", str(q / "test")]
                   + [str(q / (n + ".o")) for n, _, _ in sources] + ["-lpthread"],
                   stdout=log, stderr=subprocess.STDOUT, check=True)
with (q / "result.log").open("w") as log:
    result = subprocess.run([str(q / "test")], stdout=log, stderr=subprocess.STDOUT)
print("Lifecycle", "baseline" if args.baseline else "candidate", "exit", result.returncode)
if args.baseline:
    assert result.returncode != 0, "Baseline unexpectedly passed the lifetime contract"
else:
    assert result.returncode == 0, (q / "result.log").read_text()
