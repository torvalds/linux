

@interface MyObject : NSObject {
  id _myproperty;
}
@end

@implementation MyObject // warn: lacks 'dealloc'
@end

@interface MyObject : NSObject {}
@property(assign) id myproperty;
@end

@implementation MyObject // warn: does not send 'dealloc' to super
- (void)dealloc {
  self.myproperty = 0;
}
@end

@interface MyObject : NSObject {
  id _myproperty;
}
@property(retain) id myproperty;
@end

@implementation MyObject
@synthesize myproperty = _myproperty;
  // warn: var was retained but wasn't released
- (void)dealloc {
  [super dealloc];
}
@end

@interface MyObject : NSObject {
  id _myproperty;
}
@property(assign) id myproperty;
@end

@implementation MyObject
@synthesize myproperty = _myproperty;
  // warn: var wasn't retained but was released
- (void)dealloc {
  [_myproperty release];
  [super dealloc];
}
@end

