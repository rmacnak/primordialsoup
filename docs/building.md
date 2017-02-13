# Building

Primordial Soup has support for 32-bit and 64-bit architectures and has platform support for Android, [Fuchsia](https://fuchsia.googlesource.com/docs/+/master/README.md), Linux, macOS and Windows.

## Dependencies

Building Primordial Soup requires a C++ compiler and [SCons](http://scons.org/).

On Debian/Ubuntu:

```
sudo apt-get install g++-multilib scons
```

On macOS, install XCode and SCons.

On Windows, install Visual Studio and SCons.

To target Android, download the [Android NDK](https://developer.android.com/ndk/downloads/index.html).

## Building

The VM and snapshots are built with

```
./build
```

To target Android, build with

```
./build ndk=/path/to/android-ndk-r13b
```

## Testing

After building, the test suite and some benchmarks can be run with

```
./test
```
