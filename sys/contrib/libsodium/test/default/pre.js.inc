try {
    this['Module'] = Module;
    Module.test;
} catch(e) {
    this['Module'] = Module = {};
}
if (typeof process === 'object') {
    if (typeof(FS) === 'object') {
        Module['preRun'] = Module['preRun'] || [];
        Module['preRun'].push(function() {
            FS.init();
            FS.mkdir('/test-data');
            FS.mount(NODEFS,  { root: '.' }, '/test-data');
        });
    }
} else {
    Module['print'] = function(x) {
        var event = new Event('test-output');
        event.data = x;
        window.dispatchEvent(event);
    };
}
