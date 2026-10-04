#include <assert.h>
#include <stdlib.h>
#include "../../samples/audio/psp_ring/device_mock.c"

/* An asynchronous device must release the last buffer before it is freed. */
void ps4_lifetime_free(void *data)
{
   assert(!playing);
   free(data);
}
int ps4_lifetime_pending(void) { return playing != NULL; }
