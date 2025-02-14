function assert(b) {
    if (!b)
        throw new Error("Bad assertion.");
}

setGlobalConstRedeclarationShouldNotThrow(); // Allow duplicate const declarations at the global level.

for (let i = 0; i < 100; i++) {
    load("./global-const-redeclaration-setting/first.js");
    assert(foo === 20);
    load("./global-const-redeclaration-setting/second.js");
    assert(foo === 40);
}
