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

To target Fuchsia, [create a checkout](https://fuchsia.googlesource.com/docs/+/HEAD/development/source_code/README.md) of the garnet layer or higher.

## Building

The VM and snapshots are built with

```
./build
```

To target Android, build with

```
./build ndk=/path/to/android-ndk-r19c
```

To target Fuchsia, clone this repository to `third_party/primordialsoup` in a Fuchsia checkout, and include `third_party/primordialsoup/packages` in the list of packages. E.g.,

```
git clone https://github.com/rmacnak/primordialsoup.git third_party/primordialsoup
fx set x64 out/release-x64 --release --monolith garnet/packages/default,third_party/primordialsoup/packages
fx full-build
```

## Testing

After building, the test suite and some benchmarks can be run with

```
./test
```

On Fuchsia,

```
run fuchsia-pkg://fuchsia.com/hello_app#meta/hello_app.cmx
run fuchsia-pkg://fuchsia.com/test_runner#meta/test_runner.cmx
run fuchsia-pkg://fuchsia.com/benchmark_runner#meta/benchmark_runner.cmx
```
