#include <stdio.h>
#include "../firmware/current_block_selftest/RawBlockTests.h"
#include "raw_sequence_tests.h"
#include "../firmware/current_block_selftest/RawFeatureTests.h"
#include "../firmware/current_block_selftest/CurrentWindowTests.h"
#include "../firmware/current_block_selftest/CurrentSnapshotTests.h"
#include "current_wire_fixtures.h"
int main(int argc, char** argv) {
  if(argc == 2 && strcmp(argv[1],"--current-wire-fixtures") == 0) return emitCurrentFixtures();
  if(argc != 1) { fprintf(stderr,"Unknown test argument\n"); return 1; }
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
  const int windowFailure = hvac_window_test::run();
  if (windowFailure) { fprintf(stderr, "Window gate test failed at line %d\n", windowFailure); return 1; }
  printf("Synthetic session, timing, freshness and calibration gate tests passed.\n");
  const int snapshotFailure = hvac_snapshot_test::run();
  if (snapshotFailure) { fprintf(stderr, "Snapshot store test failed at line %d\n", snapshotFailure); return 1; }
  printf("Synthetic snapshot expiry, invalidation and extended-clock tests passed.\n");
  const int wireFailure = hvac_wire_test::run();
  if (wireFailure) { fprintf(stderr, "Wire format test failed at line %d\n", wireFailure); return 1; }
  printf("Synthetic bounded current-wire serialization tests passed.\n");
  return 0;
}
