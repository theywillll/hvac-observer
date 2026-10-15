#include <stdio.h>
#include "../firmware/current_block_selftest/RawBlockTests.h"
int main() {
  const int failure = hvac_current_test::run();
  if (failure) { fprintf(stderr, "Block contract failed at test line %d\n", failure); return 1; }
  printf("Synthetic block contract passed (including 1000 queue reuse cycles).\n");
  printf("800-sample/channel pool bytes: %zu\n", sizeof(hvac_current::RawBlockPool<800>));
  return 0;
}
