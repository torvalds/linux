# Scratch in the Linux kernel
Scratch is well-accepted by many to be the best programming language ever. Despite this, the Linux kernel has absolutely no Scratch in it!
The advantages to Scratch in the kernel are clear:
- Accessibility
  
    With the Linux kernel written in Scratch, more people will be able to contribute to Linux. "Programming" "languages" like C and Rust are based of the assumption that everyone is a dev.
  This assumption is simply false. 
- Speed
  
    While Scratch itself is slow, with the help of Turbowarp, it can be BLAZINGLY fast, by compiling Scratch to Javascript. This has the added benefit of allowing web developers
  to be able to more easily contribute to the kernel.
- Simplicity
  
    Scratch is simply cleaner. Code like ``printf("Hello, world!\n");`` is simply bloated. In Scratch, it's as simple as ``say "Hello, world!"``. You don't have to worry about
  anything complex like the "terminal" or "types," whatever those are.

In accordance with these, I have rewritten part of rust/kernel/time.rs in Scratch, and am starting the inclusion of Scratch in the kernel. Perhaps one day, we can rewrite
the entire kernel in Scratch, and get all of these benefits without worrying about interoperability with C.

## Request for comments
All comments will be added to the list ``COMMENTS``, in a project with the following code in the stage:
```
when [flag clicked]
forever {
  delete all of list [COMMENTS]
}
```
