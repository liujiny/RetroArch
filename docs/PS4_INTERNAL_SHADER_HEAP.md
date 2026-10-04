# PS4 internal shader heap (S candidate)

R on PS4 reproduced Lottes failing inside Shacc at offset 0x5ef925 after
malloc(72) returned NULL. System malloc accepted 1 MiB and rejected 16 MiB;
the independent frontend mspace accepted both. R's ordinary heap symbol
was already 256 MiB. This is not evidence of invalid GLSL.

Read-only inspection of this console's libSceLibcInternal confirmed that
`_sceLibcParam` version 12 / mode 1 selects a separate internal-heap path.
That path starts at 16 MiB and, for process type 1 and parameter version >=8,
reads a size pointer at +0x68. The ordinary size pointer at +0x10 is read
in another path. OrbisDev's weak CRT table leaves +0x68 NULL.

S supplies a strong table with only +0x68 newly populated, pointing to the
existing 256 MiB size. Version, heap mode, extended-allocation pointer,
replacement tables, and all other fields remain the same. No library
binary, allocator function, import or shader source is patched. The bounded
R diagnostic now reports the linked heap mode and the kernel's process type
and selected libc-param address, plus the existing allocation probes.

Local audit module SHA256: `c3eb91ccf9c193605e82b0ea6d93de0d6e40a5084c33ce0a2e043b42fc3ea716`.
On that specific module: +0x2ad6a checks mode/version; +0x2b30b selects the
16 MiB default; +0x2b304/+0x2b318 gate the override on process type/version;
+0x2b31f reads +0x68; +0x2b02f is the separate ordinary-size path.
Offsets document evidence, are not used by the application, and must not be
assumed to match other firmware. Private module/disassembly stay outside Git.

Validation: `tests/orbis_shader/check_heap.py` checks the strong table,
process pointer, mode, both capacities and unchanged slots in ELF, OELF and
unencrypted fSELF. `test_probe.py` exercises allocator ownership, lookup and
allocation failures, kernel parameter lookup and the diagnostic cap under
ASan/UBSan. These checks do not prove the new allocation succeeds on PS4.

Hardware acceptance still required: S starts, system 16 MiB probe succeeds,
Lottes/Mattias compile and repeatedly switch, Geom remains usable, games/core
switching remain normal. A larger heap is finite; this is not a guarantee
against all shader compiler bugs.
