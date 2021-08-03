### Versions

- `src/tinyexr` - https://github.com/syoyo/tinyexr 2021 Jul 20 `7f34939`.
- `src/tinyexr/deps/ZFP` - https://github.com/LLNL/zfp 2016 Mar 26 `a26be7c`.

### Notes

Building openexr: `/Applications/CMake.app/Contents/bin/cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=build/install -DBUILD_SHARED_LIBS=OFF` then `make -j 8`
