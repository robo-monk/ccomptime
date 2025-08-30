```c
$comptime({
    CCT_PUSH_INLINE_CODE("void x_%s() {}", v);
})
```

# 1) bootstrap the driver
cc -O2 -o nob nob.c          # or cl nob.c on MSVC
./nob                        # -> build/ccomptime-clang

# 2) compile your project with the driver instead of clang
#    (identical flags; you can set CC_REAL if clang has a version suffix)
CC_REAL=clang build/ccomptime-clang -I. -O2 -c src/foo.c -o build/foo.o
build/ccomptime-clang src/a.c src/b.c -Iinc -O2 -o app
build/ccomptime-clang -E src/foo.c | less    # view rewritten preprocessed
