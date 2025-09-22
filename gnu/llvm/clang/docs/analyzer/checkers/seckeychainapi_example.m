

void test() {
  unsigned int *ptr = 0;
  UInt32 length;

  SecKeychainItemFreeContent(ptr, &length);
    // warn: trying to free data which has not been allocated
}

void test() {
  unsigned int *ptr = 0;
  UInt32 *length = 0;
  void *outData;

  OSStatus st =
    SecKeychainItemCopyContent(2, ptr, ptr, length, outData);
    // warn: data is not released
}

void test() {
  unsigned int *ptr = 0;
  UInt32 *length = 0;
  void *outData;

  OSStatus st =
    SecKeychainItemCopyContent(2, ptr, ptr, length, &outData);

  SecKeychainItemFreeContent(ptr, outData);
    // warn: only call free if a non-NULL buffer was returned
}

void test() {
  unsigned int *ptr = 0;
  UInt32 *length = 0;
  void *outData;

  OSStatus st =
    SecKeychainItemCopyContent(2, ptr, ptr, length, &outData);

  st = SecKeychainItemCopyContent(2, ptr, ptr, length, &outData);
    // warn: release data before another call to the allocator

  if (st == noErr)
    SecKeychainItemFreeContent(ptr, outData);
}

void test() {
  SecKeychainItemRef itemRef = 0;
  SecKeychainAttributeInfo *info = 0;
  SecItemClass *itemClass = 0;
  SecKeychainAttributeList *attrList = 0;
  UInt32 *length = 0;
  void *outData = 0;

  OSStatus st =
    SecKeychainItemCopyAttributesAndData(itemRef, info,
                                         itemClass, &attrList,
                                         length, &outData);

  SecKeychainItemFreeContent(attrList, outData);
    // warn: deallocator doesn't match the allocator
}

