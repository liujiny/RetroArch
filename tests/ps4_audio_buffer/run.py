#!/usr/bin/env python3
"""Run real buffer policy/consumer code against a deterministic fake device."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--output", required=True, type=Path)
parser.add_argument("--cc", default="cc")
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
source = (root / "audio/audio_driver.c").read_text()

def function(name):
    match = re.search(r"^static [^;{}]*\b" + name + r"\([^;{}]*\)\s*\{", source, re.M)
    assert match, name
    end = source.index("\n}", match.end()) + 2
    return source[match.start():end]

names = ["audio_driver_dev_frame_bytes", "audio_driver_pipe_chunk_bytes",
         "audio_driver_pipe_target_frames", "audio_driver_pipe_prime_frames",
         "audio_driver_ps4_check_underrun", "audio_driver_sink_bias",
         "audio_driver_compute_rate_adjust", "audio_driver_output_bound",
         "audio_driver_input_bound", "audio_driver_pipe_ahead"]
(out / "policy.h").write_text("\n\n".join(map(function, names)))
(out / "consumer.h").write_text(function("audio_driver_pipeline_consume"))
# This is the producer's actual combined device/pipe fill measurement.
a = source.index("      /* Rate control's fill, before this frame goes in;")
b = source.index("      /* Count before the ring write", a)
(out / "sample_fill.h").write_text("static void sample_fill(audio_driver_state_t *audio_st) {\n" + source[a:b] + "\n}\n")
# Also verify both pipeline routes take the PS4 policy, including prime completion.
assert function("audio_driver_transport_consume").count("audio_driver_ps4_check_underrun(") == 2
assert function("audio_driver_pipeline_consume").count("audio_driver_ps4_check_underrun(") == 2
cmd = [args.cc, "-std=gnu89", "-O2", "-g", "-DHAVE_THREADS", "-D__PS4__",
       "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-unused-function",
       "-I" + str(root), "-I" + str(root / "libretro-common/include"),
       "-I" + str(out), str(root / "tests/ps4_audio_buffer/test.c"),
       str(root / "libretro-common/queues/retro_spsc.c"),
       str(root / "libretro-common/memmap/memalign.c"), "-lm", "-o", str(out / "test")]
if args.sanitize:
    cmd[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
subprocess.run(cmd, check=True)
subprocess.run([str(out / "test")], check=True)
