/* Internal-heap mode reads capacity at +0x68, not the ordinary +0x10. */
#ifndef PLATFORM_ORBIS_HEAP_H
#define PLATFORM_ORBIS_HEAP_H

#include <stdint.h>
#include <stddef.h>

extern uint64_t sceLibcHeapExtendedAlloc;
extern const unsigned char sceLibcMallocReplace[];
extern const unsigned char sceLibcNewReplace[];
extern const unsigned char sceLibcMallocReplaceForTls[];

uint64_t sceLibcHeapSize = UINT64_C(256) * 1024 * 1024;

struct orbis_libc_param
{
   uint64_t size;
   uint32_t version;
   uint32_t heap_mode;
   const uint64_t *heap_size;
   const void *delayed_alloc;
   const uint64_t *extended_alloc;
   const void *initial_size;
   const void *malloc_replace;
   const void *new_replace;
   const void *page_size;
   const void *need_libc;
   const void *reserved_50;
   const void *reserved_58;
   const void *tls_replace;
   const uint64_t *internal_heap_size;
   const void *reserved_70;
   const void *reserved_78;
   const void *reserved_80;
   const void *reserved_88;
};

typedef char orbis_libc_param_size_check[
      sizeof(struct orbis_libc_param) == 0x90 ? 1 : -1];
typedef char orbis_libc_internal_heap_offset_check[
      offsetof(struct orbis_libc_param, internal_heap_size) == 0x68 ? 1 : -1];

/* Override only this CRT table; retain its mode and replacement tables. */
const struct orbis_libc_param _sceLibcParam = {
   0x90, 12, 1,
   &sceLibcHeapSize, NULL, &sceLibcHeapExtendedAlloc, NULL,
   sceLibcMallocReplace, sceLibcNewReplace, NULL, NULL,
   NULL, NULL, sceLibcMallocReplaceForTls, &sceLibcHeapSize,
   NULL, NULL, NULL, NULL
};

#endif
