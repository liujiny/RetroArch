# R diagnostic build

Q failed physically: original CRT Lottes still crashes on the same Shacc
malloc(72) NULL dereference. Explicit sceLibcHeapSize is not a verified fix.
R retains Q as its controlled baseline and adds measurement, not a claimed
shader repair. It never patches imports, substitutes GLSL or catches SIGSEGV.

Before at most 12 user shader source loads, enumerate loaded modules, print
libc/Piglet/Shacc names and addresses, resolve malloc/free from the actual
system libc module, and compare 72-byte, 1-MiB and 16-MiB allocations with
the frontend allocator. Free each pointer only with its paired allocator.
These probes may warm/expand the heap; success is evidence of that request,
not proof of peak compiler capacity or that the original failure is fixed.
Log shader compile begin/stage/length and return so a crash identifies the
last active compiler phase. No per-frame frontend allocation probes.

Host tests exercise the production probe with separate ownership, allocation
failure, resolution failure, cleanup and the 12-call bound under sanitizers.
Physical module resolution and allocator behavior still require PS4 logs.
Package R also contains bounded FBNeo CV1000 stage timing. Play both games
before provoking the shader crash so their timing logs are retained.
