@interface RootObject
+ (instancetype)alloc;

- (instancetype)init;
@end

@interface BaseClass : RootObject
+ (instancetype)sharedInstance;

- (instancetype)initWithFoo:(int)foo;
@end

static BaseClass *sharedInstance = (void *)0;
static int counter = 0;

@implementation BaseClass
+ (instancetype)sharedInstance {
  if (sharedInstance) {
    return sharedInstance;
  }
  sharedInstance = [[BaseClass alloc] initWithFoo:3];
  return sharedInstance;
}


- (instancetype)initWithFoo:(int)foo {
  self = [super init];
  if (self) {
    counter += foo;
  }
  return self;
}
@end

