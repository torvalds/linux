@interface RootObject
@end

@interface BasicClass : RootObject {
  int _foo;
  char _boolean;
}

@property(nonatomic, assign) int bar;
@property(atomic, retain) id objectField;
@property(nonatomic, assign) id delegate;

- (void)someMethod;
@end

@implementation BasicClass

@synthesize bar = _bar;
@synthesize objectField = _objectField;
@synthesize delegate = _delegate;

- (void)someMethod {
  int value = self.bar;
  _foo = (_boolean != 0) ? self.bar : [self.objectField bar];
  [self setBar:value];
  id obj = self.objectField;
}
@end

