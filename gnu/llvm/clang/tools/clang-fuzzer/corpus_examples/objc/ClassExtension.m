@interface RootObject
@end

@interface BaseClass : RootObject
@end

@interface BaseClass() {
  int _field1;
}
@property(atomic, assign, readonly) int field2;

- (int)addFields;
@end

@implementation BaseClass
- (int)addFields {
  return self->_field1 + [self field2];
}
@end

