
#include "ValidationEvent.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace exegesis {

namespace {

struct ValidationEventInfo {
  const char *const Name;
  const char *const Description;
};

// Information about validation events, indexed by `ValidationEvent` enum
// value.
static constexpr ValidationEventInfo ValidationEventInfos[] = {
    {"instructions-retired", "Count retired instructions"},
    {"l1d-cache-load-misses", "Count L1D load cache misses"},
    {"l1d-cache-store-misses", "Count L1D store cache misses"},
    {"l1i-cache-load-misses", "Count L1I load cache misses"},
    {"data-tlb-load-misses", "Count DTLB load misses"},
    {"data-tlb-store-misses", "Count DTLB store misses"},
    {"instruction-tlb-load-misses", "Count ITLB load misses"},
    {"branch-prediction-misses", "Branch prediction misses"},
};

static_assert(sizeof(ValidationEventInfos) ==
                  NumValidationEvents * sizeof(ValidationEventInfo),
              "please update ValidationEventInfos");

} // namespace

const char *getValidationEventName(ValidationEvent VE) {
  return ValidationEventInfos[VE].Name;
}
const char *getValidationEventDescription(ValidationEvent VE) {
  return ValidationEventInfos[VE].Description;
}

Expected<ValidationEvent> getValidationEventByName(StringRef Name) {
  int VE = 0;
  for (const ValidationEventInfo &Info : ValidationEventInfos) {
    if (Name == Info.Name)
      return static_cast<ValidationEvent>(VE);
    ++VE;
  }

  return make_error<StringError>("Invalid validation event string",
                                 errc::invalid_argument);
}

} // namespace exegesis
} // namespace llvm
