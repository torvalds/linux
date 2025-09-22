#include "dfsan_thread.h"

#include <pthread.h>

#include "dfsan.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"

namespace __dfsan {

DFsanThread *DFsanThread::Create(thread_callback_t start_routine, void *arg,
                                 bool track_origins) {
  uptr PageSize = GetPageSizeCached();
  uptr size = RoundUpTo(sizeof(DFsanThread), PageSize);
  DFsanThread *thread = (DFsanThread *)MmapOrDie(size, __func__);
  thread->start_routine_ = start_routine;
  thread->arg_ = arg;
  thread->track_origins_ = track_origins;
  thread->destructor_iterations_ = GetPthreadDestructorIterations();

  return thread;
}

void DFsanThread::SetThreadStackAndTls() {
  uptr tls_size = 0;
  uptr stack_size = 0;
  GetThreadStackAndTls(IsMainThread(), &stack_.bottom, &stack_size, &tls_begin_,
                       &tls_size);
  stack_.top = stack_.bottom + stack_size;
  tls_end_ = tls_begin_ + tls_size;

  int local;
  CHECK(AddrIsInStack((uptr)&local));
}

void DFsanThread::ClearShadowForThreadStackAndTLS() {
  dfsan_set_label(0, (void *)stack_.bottom, stack_.top - stack_.bottom);
  if (tls_begin_ != tls_end_)
    dfsan_set_label(0, (void *)tls_begin_, tls_end_ - tls_begin_);
  DTLS *dtls = DTLS_Get();
  CHECK_NE(dtls, 0);
  ForEachDVT(dtls, [](const DTLS::DTV &dtv, int id) {
    dfsan_set_label(0, (void *)(dtv.beg), dtv.size);
  });
}

void DFsanThread::Init() {
  SetThreadStackAndTls();
  ClearShadowForThreadStackAndTLS();
}

void DFsanThread::TSDDtor(void *tsd) {
  DFsanThread *t = (DFsanThread *)tsd;
  t->Destroy();
}

void DFsanThread::Destroy() {
  malloc_storage().CommitBack();
  // We also clear the shadow on thread destruction because
  // some code may still be executing in later TSD destructors
  // and we don't want it to have any poisoned stack.
  ClearShadowForThreadStackAndTLS();
  uptr size = RoundUpTo(sizeof(DFsanThread), GetPageSizeCached());
  UnmapOrDie(this, size);
  DTLS_Destroy();
}

thread_return_t DFsanThread::ThreadStart() {
  if (!start_routine_) {
    // start_routine_ == 0 if we're on the main thread or on one of the
    // OS X libdispatch worker threads. But nobody is supposed to call
    // ThreadStart() for the worker threads.
    return 0;
  }

  // The only argument is void* arg.
  //
  // We have never supported propagating the pointer arg as tainted,
  // __dfsw_pthread_create/__dfso_pthread_create ignore the taint label.
  // Note that the bytes pointed-to (probably the much more common case)
  // can still have taint labels attached to them.
  dfsan_clear_thread_local_state();

  return start_routine_(arg_);
}

DFsanThread::StackBounds DFsanThread::GetStackBounds() const {
  return {stack_.bottom, stack_.top};
}

uptr DFsanThread::stack_top() { return GetStackBounds().top; }

uptr DFsanThread::stack_bottom() { return GetStackBounds().bottom; }

bool DFsanThread::AddrIsInStack(uptr addr) {
  const auto bounds = GetStackBounds();
  return addr >= bounds.bottom && addr < bounds.top;
}

static pthread_key_t tsd_key;
static bool tsd_key_inited = false;

void DFsanTSDInit(void (*destructor)(void *tsd)) {
  CHECK(!tsd_key_inited);
  tsd_key_inited = true;
  CHECK_EQ(0, pthread_key_create(&tsd_key, destructor));
}

static THREADLOCAL DFsanThread *dfsan_current_thread;

DFsanThread *GetCurrentThread() { return dfsan_current_thread; }

void SetCurrentThread(DFsanThread *t) {
  // Make sure we do not reset the current DFsanThread.
  CHECK_EQ(0, dfsan_current_thread);
  dfsan_current_thread = t;
  // Make sure that DFsanTSDDtor gets called at the end.
  CHECK(tsd_key_inited);
  pthread_setspecific(tsd_key, t);
}

void DFsanTSDDtor(void *tsd) {
  DFsanThread *t = (DFsanThread *)tsd;
  if (t->destructor_iterations_ > 1) {
    t->destructor_iterations_--;
    CHECK_EQ(0, pthread_setspecific(tsd_key, tsd));
    return;
  }
  dfsan_current_thread = nullptr;
  // Make sure that signal handler can not see a stale current thread pointer.
  atomic_signal_fence(memory_order_seq_cst);
  DFsanThread::TSDDtor(tsd);
}

}  // namespace __dfsan
