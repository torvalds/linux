extern "C" {
  void *CFAllocatorDefaultDoubleFree(void *unused);
  void CFAllocatorSystemDefaultDoubleFree();
  void CFAllocatorMallocDoubleFree();
  void CFAllocatorMallocZoneDoubleFree();
  void CallFreeOnWorkqueue(void *mem);
  void TestGCDDispatchAsync();
  void TestGCDDispatchSync();
  void TestGCDReuseWqthreadsAsync();
  void TestGCDReuseWqthreadsSync();
  void TestGCDDispatchAfter();
  void TestGCDInTSDDestructor();
  void TestGCDSourceEvent();
  void TestGCDSourceCancel();
  void TestGCDGroupAsync();
  void TestOOBNSObjects();
  void TestNSURLDeallocation();
  void TestPassCFMemoryToAnotherThread();
}
