#pragma once
#include <CurrentSnapshot.h>
#include <stdio.h>
#include <stdarg.h>

namespace hvac_current {
// Schema 1 payload only: merge into a normal frame after successful serialization.
// Not connected to Bridge; its message-size/latency budget is still unqualified.
static constexpr size_t CurrentWireCapacity = 1536;
namespace wire_detail {
class Writer {
 public:
  Writer(char* buffer, size_t capacity) : data_(buffer), capacity_(capacity) {
    good_ = buffer != nullptr && capacity > 0;
    if (good_) data_[0] = '\0';
  }
  void append(const char* format, ...) {
    if (!good_) return;
    va_list args; va_start(args,format);
    const int n=vsnprintf(data_+used_,capacity_-used_,format,args);
    va_end(args);
    if (n < 0 || size_t(n) >= capacity_-used_) { fail(); return; }
    used_+=size_t(n);
  }
  void id(uint32_t value) {
    if (value) append("%lu",static_cast<unsigned long>(value)); else append("null");
  }
  void fail() { good_=false; if(data_ && capacity_) data_[0]='\0'; }
  bool good() const { return good_; }
 private:
  char* data_; size_t capacity_, used_=0; bool good_=false;
};
} // namespace wire_detail

// ALL store access, including this call, requires external serialization.
// Takes a fresh snapshot internally; never serializes a retained, stale copy.
// False clears the output (when capacity > 0); discard the whole payload.
inline bool writeCurrentWire(CurrentSnapshotStore& store, uint64_t nowUs,
                             char* output, size_t capacity) {
  wire_detail::Writer w(output,capacity);
  if (!w.good()) return false;
  const auto s=store.snapshot(nowUs);
  const char* names[3]={"compressor_a","blower_a","condenser_fan_a"};
  const double limits[3]={200,100,30}; // Must agree with backend engineering ranges.
  w.append("{");
  for(unsigned c=0;c<3;++c) {
    const auto& v=s.channels[c];
    w.append("%s\"%s\":",c ? "," : "",names[c]);
    if (!v.available()) { w.append("null"); continue; }
    if (!isfinite(v.rmsAmps) || v.rmsAmps < 0 || v.rmsAmps > limits[c]) {
      w.fail(); return false;
    }
    char number[32]; const int n=snprintf(number,sizeof(number),"%.17g",v.rmsAmps);
    if(n <= 0 || size_t(n) >= sizeof(number)) { w.fail(); return false; }
    // Reject a non-C locale decimal separator instead of emitting broken JSON.
    for(int i=0;i<n;++i) if(!((number[i]>='0' && number[i]<='9') ||
        number[i]=='.' || number[i]=='-' || number[i]=='+' ||
        number[i]=='e' || number[i]=='E')) { w.fail(); return false; }
    w.append("%s",number);
  }
  w.append(",\"current_quality\":{\"schema\":1,\"session\":");
  if(s.session) w.append("\"%llu\"",static_cast<unsigned long long>(s.session));
  else w.append("null");
  w.append(",\"max_age_us\":%lu,\"channels\":[",static_cast<unsigned long>(s.maxAgeUs));
  for(unsigned c=0;c<3;++c) {
    const auto& v=s.channels[c];
    w.append("%s{\"channel\":%u,\"available\":%s,\"faults\":%lu,\"age_us\":",
             c ? "," : "",c,v.available() ? "true" : "false",
             static_cast<unsigned long>(v.faults));
    if(s.observed[c] && v.ageUs != UINT32_MAX)
      w.append("%lu",static_cast<unsigned long>(v.ageUs));
    else w.append("null");
    w.append(",\"window_seq\":");
    if(s.observed[c]) w.append("\"%llu\"",static_cast<unsigned long long>(v.sequence));
    else w.append("null");
    w.append(",\"timing_evidence_id\":"); w.id(v.timingEvidenceId);
    w.append(",\"calibration_id\":"); w.id(v.calibrationId);
    w.append(",\"calibration_revision\":"); w.id(v.calibrationRevision);
    w.append("}");
  }
  w.append("]}}");
  return w.good();
}
} // namespace hvac_current
