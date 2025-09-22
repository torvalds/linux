@interface RootObject
@end

@interface BaseClass : RootObject
@property(atomic, assign, readonly) int field;
@end

@interface BaseClass(Private)
@property(atomic, assign, readwrite) int field;

- (int)something;
@end

@implementation BaseClass
- (int)something {
  self.field = self.field + 1;
  return self.field;
}
@end

