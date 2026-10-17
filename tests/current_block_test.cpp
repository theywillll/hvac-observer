#include <stdio.h>
#include "../firmware/current_block_selftest/RawBlockTests.h"
#include "raw_sequence_tests.h"
#include "../firmware/current_block_selftest/RawFeatureTests.h"
int main() {
  const int failure = hvac_current_test::run();
  if (failure) { fprintf(stderr, "Block contract failed at test line %d\n", failure); return 1; }
  printf("Synthetic block contract passed (including 1000 queue reuse cycles).\n");
  printf("800-sample/channel pool bytes: %zu\n", sizeof(hvac_current::RawBlockPool<800>));
  const int sequenceFailure = runSequenceTests();
  if (sequenceFailure) { fprintf(stderr, "Sequence guard test failed at line %d\n", sequenceFailure); return 1; }
  printf("Synthetic sequence buffer guards and publication checks passed.\n");
  const int featureFailure = hvac_feature_test::run();
  if (featureFailure) { fprintf(stderr, "Raw feature test failed at line %d\n", featureFailure); return 1; }
  printf("Synthetic offset removal and raw-code RMS tests passed.\n");
  return 0;
}
