# -*- mode: python -*-

import os
import platform

def BuildVM(cxx, arch, target_os, debug, sanitize):
  if target_os == 'windows' and arch == 'ia32':
    env = Environment(TARGET_ARCH='x86', tools=['msvc', 'mslink'])
  elif target_os == 'windows' and arch == 'x64':
    env = Environment(TARGET_ARCH='x86_64', tools=['msvc', 'mslink'])
  elif target_os == 'emscripten':
    env = Environment(ENV = os.environ, tools=['g++', 'gnulink'])
    env['CXX'] = cxx
  else:
    env = Environment(tools=['g++', 'gnulink'])
    env['CXX'] = cxx

  configname = ''
  if debug:
    if target_os == 'windows':
      env['CCFLAGS'] += ['/DDEBUG=1']
    else:
      env['CCFLAGS'] += ['-DDEBUG']
    configname += 'Debug'
  else:
    if target_os == 'windows':
      env['CCFLAGS'] += ['/DNDEBUG']
    else:
      env['CCFLAGS'] += ['-DNDEBUG']
    configname += 'Release'

  if target_os == 'android':
    configname += 'Android'
  elif target_os == 'emscripten':
    configname += 'Emscripten'

  if sanitize == 'address':
    configname += 'ASan'
  elif sanitize == 'leak':
    configname += 'LSan'
  elif sanitize == 'memory':
    configname += 'MSan'
  elif sanitize == 'thread':
    configname += 'TSan'
  elif sanitize == 'undefined':
    configname += 'UBSan'

  if arch == 'ia32':
    if target_os == 'windows':
      env['LINKFLAGS'] += ['/MACHINE:X86']
    else:
      env['CCFLAGS'] += ['-m32']
      env['LINKFLAGS'] += ['-m32']
    configname += 'IA32'
  elif arch == 'x64':
    if target_os == 'windows':
      env['LINKFLAGS'] += ['/MACHINE:X64']
    elif target_os == 'macos':
      env['CCFLAGS'] += ['-arch', 'x86_64']
      env['LINKFLAGS'] += ['-arch', 'x86_64']
    else:
      env['CCFLAGS'] += ['-m64']
      env['LINKFLAGS'] += ['-m64']
    configname += 'X64'
  elif arch == 'arm':
    configname += 'ARM'
  elif arch == 'arm64':
    if target_os == 'macos':
      env['CCFLAGS'] += ['-arch', 'arm64']
      env['LINKFLAGS'] += ['-arch', 'arm64']
    configname += 'ARM64'
  elif arch == 'mips':
    env['CCFLAGS'] += ['-EL']
    env['LINKFLAGS'] += ['-EL']
    configname += 'MIPS'
  elif arch == 'mips64':
    env['CCFLAGS'] += ['-EL']
    env['LINKFLAGS'] += ['-EL']
    configname += 'MIPS64'
  elif arch == 'riscv32':
    configname += 'RISCV32'
  elif arch == 'riscv64':
    configname += 'RISCV64'
  elif arch == 'wasm':
    configname += 'WASM'
    env['LINKFLAGS'] += [ '-s', 'WASM=1' ]
  else:
    raise Exception('Unknown architecture: ' + arch)

  outdir = os.path.join('out', configname)

  if target_os == 'windows':
    env['CCFLAGS'] += [
      '/O2',
      '/Z7',  # Debug symbols
      '/WX',  # Warnings as errors
      '/W3',  # Most warnings
      '/wd4200',  # Zero-sized array in struct
      '/wd4996',  # Deprecated POSIX names
      '/wd4244',  # Implicit narrowing conversion
      '/wd4800',  # Implicit bool conversion
      '/wd4146',  # Negating unsigned type
      '/D_HAS_EXCEPTIONS=0',
      '/DSTRICT',
      '/D_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES=1',
      '/D_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT=1'
    ]
  else:
    env['CCFLAGS'] += [
      '-std=c++11',
      '-O3',
      '-g3',
      '-Werror',
      '-Wall',
      '-Wextra',
      '-Wnon-virtual-dtor',
      '-Wvla',
      '-Wno-unused-parameter',
      '-fno-rtti',
      '-fno-exceptions',
      '-fpic',
      '-fvisibility=hidden',
      '-fdata-sections',
      '-ffunction-sections',
    ]
    if target_os != 'emscripten':
      env['CCFLAGS'] += ['-fstack-protector']
    if sanitize != None:
      env['CCFLAGS'] += ['-fsanitize=' + sanitize ]
      env['LINKFLAGS'] += ['-fsanitize=' + sanitize ]
    else:
      env['CCFLAGS'] += ['-D_FORTIFY_SOURCE=2']

  if target_os == 'macos':
    env['LINKFLAGS'] += [
      '-fPIE',
      '-dead_strip',
    ]
    env['LIBS'] = [
      'm',
      'stdc++',
      'pthread',
    ]
  elif target_os == 'linux':
    env['LINKFLAGS'] += [
      '-pie',
      '-Wl,-z,relro,-z,now',
      '-Wl,--gc-sections',
      '-Wl,-z,noexecstack',
    ]
    env['LIBS'] = [
      'm',
      'stdc++',
      'pthread',
    ]
  elif target_os == 'android':
    env['LINKFLAGS'] += [
      '-pie',
      '-Wl,-z,relro,-z,now',
      '-Wl,--gc-sections',
      '-Wl,-z,noexecstack',
      '-static-libstdc++',
    ]
    env['LIBS'] = [
      'm',
      'log',
    ]
  elif target_os == 'windows':
    env['LINKFLAGS'] += [
      '/DYNAMICBASE',  # Address space layout randomization
      '/NXCOMPAT',
      '/DEBUG',  # Debug symbols
    ]
  elif target_os == 'emscripten':
    env['LINKFLAGS'] += [
      '-s', 'ALLOW_MEMORY_GROWTH=1',
      '-s', 'ENVIRONMENT=web',
      '-s', 'EXPORTED_FUNCTIONS=["_load_snapshot", "_handle_message", "_handle_signal"]',
      '-s', 'FILESYSTEM=0',
      '-s', 'MALLOC=emmalloc',
      '-s', 'TOTAL_STACK=131072',
      '--shell-file', 'meta/shell.html',
    ]
  else:
    raise Exception('Unknown operating system: ' + target_os)

  env['CPPPATH'] = ['.']

  objects = []

  vm_ccs = [
    'assert',
    'double_conversion',
    'heap',
    'interpreter',
    'isolate',
    'large_integer',
    'lookup_cache',
    'main',
    'main_emscripten',
    'message_loop',
    'message_loop_emscripten',
    'message_loop_epoll',
    'message_loop_fuchsia',
    'message_loop_iocp',
    'message_loop_kqueue',
    'object',
    'os_android',
    'os_emscripten',
    'os_fuchsia',
    'os_linux',
    'os_macos',
    'os_win',
    'port',
    'primitives',
    'primordial_soup',
    'snapshot',
    'thread_android',
    'thread_emscripten',
    'thread_fuchsia',
    'thread_linux',
    'thread_macos',
    'thread_pool',
    'thread_win',
    'virtual_memory_emscripten',
    'virtual_memory_fuchsia',
    'virtual_memory_posix',
    'virtual_memory_win',
  ]
  for cc in vm_ccs:
    objects += env.Object(os.path.join(outdir, 'vm', cc + '.o'),
                          os.path.join('vm', cc + '.cc'))

  double_conversion_ccs = [
    'bignum',
    'bignum-dtoa',
    'cached-powers',
    'diy-fp',
    'double-conversion',
    'fast-dtoa',
    'fixed-dtoa',
    'strtod',
  ]
  for cc in double_conversion_ccs:
    objects += env.Object(os.path.join(outdir, 'double-conversion', cc + '.o'),
                          os.path.join('double-conversion', cc + '.cc'))

  if target_os == 'emscripten':
    program = env.Program(os.path.join(outdir, 'primordialsoup.html'), objects)
    Depends(program, 'meta/shell.html');
  else:
    program = env.Program(os.path.join(outdir, 'primordialsoup'), objects)
  return str(program[0])


def BuildSnapshots(outdir, host_vm):
  nssources = Glob(os.path.join('newspeak', '*.ns'))
  compilersnapshot = os.path.join('snapshots', 'compiler.vfuel')
  snapshots = []
  cmd = host_vm + ' ' + compilersnapshot + ' $SOURCES'

  helloout = os.path.join(outdir, 'HelloApp.vfuel')
  snapshots += [helloout]
  cmd += ' Runtime HelloApp ' + helloout

  testout = os.path.join(outdir, 'TestRunner.vfuel')
  snapshots += [testout]
  cmd += ' RuntimeWithMirrors TestRunner ' + testout

  benchmarkout = os.path.join(outdir, 'BenchmarkRunner.vfuel')
  snapshots += [benchmarkout]
  cmd += ' Runtime BenchmarkRunner ' + benchmarkout

  compilerout = os.path.join(outdir, 'CompilerApp.vfuel')
  snapshots += [compilerout]
  cmd += ' RuntimeWithMirrors CompilerApp ' + compilerout

  Command(target=snapshots, source=nssources, action=cmd)
  Requires(snapshots, host_vm)
  Depends(snapshots, compilersnapshot)


def Main():
  host_os = None
  default_host_cxx = None
  if platform.system() == 'Linux':
    host_os = 'linux'
    default_host_cxx = 'g++'
  elif platform.system() == 'Darwin':
    host_os = 'macos'
    default_host_cxx = 'clang++'
  elif platform.system() == 'Windows':
    host_os = 'windows'
    default_host_cxx = 'cl'
  host_cxx = ARGUMENTS.get('cxx_host', default_host_cxx)

  target_os = ARGUMENTS.get('os', host_os)
  default_target_cxx = None
  if target_os == 'emscripten':
    default_target_cxx = 'em++'
  else:
    default_target_cxx = default_host_cxx
  target_cxx = ARGUMENTS.get('cxx_target', default_target_cxx)

  target_arch = ARGUMENTS.get('arch', None)
  host_arch = None
  if (platform.machine() == 'x86_64' or
      platform.machine() == 'AMD64'):
    host_arch = 'x64'
  elif platform.machine() == 'i386':
    host_arch = 'ia32'
  elif (platform.machine() == 'aarch64' or
        platform.machine() == 'arm64'):
    host_arch = 'arm64'
  elif (platform.machine() == 'armv7l' or
        platform.machine() == 'armv6l'):
    host_arch = 'arm'
  elif platform.machine() == 'mips':
    host_arch = 'mips'
  elif platform.machine() == 'riscv32':
    host_arch = 'riscv32'
  elif platform.machine() == 'riscv64':
    host_arch = 'riscv64'
  else:
    raise Exception("Unknown machine: " + platform.machine())

  sanitize = ARGUMENTS.get('sanitize', None)
  if sanitize not in ['address', 'leak', 'memory', 'thread', 'undefined', None]:
    raise Exception("Unknown sanitize option: " + sanitize)

  # Build a release host VM for building the snapshots. Don't use the santizers
  # here so the snapshots have a fixed dependency and won't be rebuilt for each
  # sanitizer.
  host_vm = BuildVM(host_cxx, host_arch, host_os, False, None)
  BuildSnapshots('out/snapshots/', host_vm)

  # Build for the host.
  BuildVM(host_cxx, host_arch, host_os, True, sanitize)
  if sanitize != None:
    # Avoid specifying the host release build twice.
    BuildVM(host_cxx, host_arch, host_os, False, sanitize)

  if ((target_os != None and host_os != target_os) or
      (target_arch != None and host_arch != target_arch)):
    # If cross compiling, also build for the target.
    BuildVM(target_cxx, target_arch, target_os, True, sanitize)
    BuildVM(target_cxx, target_arch, target_os, False, sanitize)
  elif host_arch == 'x64' and sanitize != 'thread' and target_arch == None \
       and target_os != 'macos' :
    # If on X64, also build for IA32. Skip when using TSan or targeting Mac, as
    # they don't support IA32. Also skip when a specific architecture was asked
    # for.
    BuildVM(host_cxx, 'ia32', host_os, True, sanitize)
    BuildVM(host_cxx, 'ia32', host_os,  False, sanitize)
  elif target_arch == None and target_os == 'macos':
    # On Mac, also build for the other Mac architecture, unless a specific
    # architecture was asked for.
    if host_arch != 'arm64':
      BuildVM(host_cxx, 'arm64', host_os, True, sanitize)
      BuildVM(host_cxx, 'arm64', host_os,  False, sanitize)
    if host_arch != 'x64':
      BuildVM(host_cxx, 'x64', host_os, True, sanitize)
      BuildVM(host_cxx, 'x64', host_os,  False, sanitize)

  ndk = ARGUMENTS.get('ndk', None)
  if ndk != None:
    # The following paths were taken from android-ndk-r19c. They might differ
    # between versions of the Android NDK.
    if host_os == 'linux':
      android_host_name = 'linux-x86_64'
    elif host_os == 'macos':
      android_host_name = 'darwin-x86_64'
    elif host_os == 'windows':
      android_host_name = 'windows-x86_64'
    else:
      raise Exception("Android NDK paths not known for this OS")

    target_cxx = ndk + '/toolchains/llvm/prebuilt/' \
        + android_host_name + '/bin/armv7a-linux-androideabi24-clang++'
    BuildVM(target_cxx, 'arm', 'android', False, None);
    BuildVM(target_cxx, 'arm', 'android', True, None);

    target_cxx = ndk + '/toolchains/llvm/prebuilt/' \
        + android_host_name + '/bin/aarch64-linux-android24-clang++'
    BuildVM(target_cxx, 'arm64', 'android', False, None);
    BuildVM(target_cxx, 'arm64', 'android', True, None);

    target_cxx = ndk + '/toolchains/llvm/prebuilt/' \
        + android_host_name + '/bin/i686-linux-android24-clang++'
    BuildVM(target_cxx, 'ia32', 'android', False, None);
    BuildVM(target_cxx, 'ia32', 'android', True, None);

    target_cxx = ndk + '/toolchains/llvm/prebuilt/' \
        + android_host_name + '/bin/x86_64-linux-android24-clang++'
    BuildVM(target_cxx, 'x64', 'android', False, None);
    BuildVM(target_cxx, 'x64', 'android', True, None);

Main()
