//C
void test() {
   void (*foo)(void);
   foo = 0;
   foo(); // warn: function pointer is null
 }

 // C++
 class C {
 public:
   void f();
 };

 void test() {
   C *pc;
   pc->f(); // warn: object pointer is uninitialized
 }

 // C++
 class C {
 public:
   void f();
 };

 void test() {
   C *pc = 0;
   pc->f(); // warn: object pointer is null
 }

 // Objective-C
 @interface MyClass : NSObject
 @property (readwrite,assign) id x;
 - (long double)longDoubleM;
 @end

 void test() {
   MyClass *obj1;
   long double ld1 = [obj1 longDoubleM];
     // warn: receiver is uninitialized
 }

 // Objective-C
 @interface MyClass : NSObject
 @property (readwrite,assign) id x;
 - (long double)longDoubleM;
 @end

 void test() {
   MyClass *obj1;
   id i = obj1.x; // warn: uninitialized object pointer
 }

 // Objective-C
 @interface Subscriptable : NSObject
 - (id)objectAtIndexedSubscript:(unsigned int)index;
 @end

 @interface MyClass : Subscriptable
 @property (readwrite,assign) id x;
 - (long double)longDoubleM;
 @end

 void test() {
   MyClass *obj1;
   id i = obj1[0]; // warn: uninitialized object pointer
 }
