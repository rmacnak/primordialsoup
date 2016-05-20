# Building

## Dependencies

Building Primordial Soup requires a C++ compiler and Scons.

On Debian/Ubuntu:

```
sudo apt-get install gcc-multilib scons
```

On Mac OS X, install XCode and Homebrew, then:

```
brew install scons
```

## Building

The VM and snapshots are built with

```
./build
```

## Testing

After building, the test suite and some benchmarks and be run with

```
./test
```
