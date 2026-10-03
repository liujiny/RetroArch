# PS4 system-library heap for GLSL compilation

O Mattias and P Lottes both crashed in the bundled R4 Shacc at RIP
0x806a3925 (module offset 0x5ef925), writing address 0x10 with RBX=0.
The preceding call at 0x5ef90c requests 72 bytes via PLT 0x3c8, whose
relocation resolves to libSceLibcInternal malloc (gQX+4GDQjpM).
It returns NULL; the compiler continues dereferencing it. User reports
CRT Geom works. Source and logs are retained in project testbuild/q-logs.

The OrbisDev frontend uses its own mspace. Dynamic cores use separate
allocators. Success allocating game RAM does not establish capacity of the
system libc heap used by Piglet/Shacc. The CRT supplies weak heap size -1.
Override only sceLibcHeapSize with an explicit 256 MiB strong definition;
retain the CRT's extended-allocation setting and existing allocator ABI.

This is a common allocation-headroom fix to test, not proof that every shader
compiler crash is resolved. No shader-name blacklist, source substitution,
shader simplification, signal recovery or binary patch is included.
The captured Mattias/Lottes shaders remain unchanged. Test both, the working
Geom control, repeated preset switching, loading cores and normal startup.

`python3 tests/orbis_shader/check_heap.py retroarch_orbis.elf` verifies the
strong symbol value and the libc/process parameter pointers in the linked
binary. PS4 confirmation is still required. Package Q also contains the
independent FBNeo PS5 CV1000 synchronization port; audio work is deferred.
