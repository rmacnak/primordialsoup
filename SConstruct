# -*- mode: python -*-

import os
import platform

def BuildVM(cxx, arch, target_os, debug, sanitize):
  if target_os == 'windows' and arch == 'ia32':
    env = Environment(TARGET_ARCH='x86')
  elif target_os == 'windows' and arch == 'x64':
    env = Environment(TARGET_ARCH='x86_64')
  else:
    env = Environment()
    env['CXX'] = cxx

  configname = ''
  if debug:
    if target_os == 'windows':
      env['CCFLAGS'] += ['/DDEBUG=1']
    else:
      env['CCFLAGS'] += ['-DDEBUG']
    configname += 'Debug'
  else:
    if target_os == "windows":
      env['CCFLAGS'] += ['/DNDEBUG']
    else:
      env['CCFLAGS'] += ['-DNDEBUG']
    configname += 'Release'

  if target_os == 'android':
    configname += 'Android'

  if sanitize == 'address':
    configname += 'ASan'
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
    else:
      env['CCFLAGS'] += ['-m64']
      env['LINKFLAGS'] += ['-m64']
    configname += 'X64'
  elif arch == 'arm':
    configname += 'ARM'
  elif arch == 'arm64':
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
    env['CCFLAGS'] += ['-m32']
    env['LINKFLAGS'] += ['-m32']
    configname += 'RISCV32'
  elif arch == 'riscv64':
    env['CCFLAGS'] += ['-m64']
    env['LINKFLAGS'] += ['-m64']
    configname += 'RISCV64'

  outdir = os.path.join('out', configname)

  if target_os == "windows":
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
      '-fstack-protector',
      '-fpic',
    ]
    if sanitize == 'address':
      env['CCFLAGS'] += ['-fsanitize=address']
      env['LINKFLAGS'] += ['-fsanitize=address']
    elif sanitize == 'thread':
      env['CCFLAGS'] += ['-fsanitize=thread']
      env['LINKFLAGS'] += ['-fsanitize=thread']
    elif sanitize == 'undefined':
      env['CCFLAGS'] += [
        '-fsanitize=undefined',
        '-fno-sanitize=alignment,null',
      ]
      env['LINKFLAGS'] += ['-fsanitize=undefined']
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
      '-fPIE',
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
    ]
    env['LIBS'] = [
      'm',
      'stdc++',
      'log',
    ]
  elif target_os == 'windows':
    env['LINKFLAGS'] += [
      '/DYNAMICBASE',  # Address space layout randomization
      '/NXCOMPAT',
      '/DEBUG',  # Debug symbols
    ]

  env['CPPPATH'] = ['src']

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
    'message',
    'object',
    'os_android',
    'os_linux',
    'os_macos',
    'os_win',
    'os_thread',
    'os_thread_android',
    'os_thread_linux',
    'os_thread_macos',
    'os_thread_win',
    'port',
    'primitives',
    'snapshot',
    'thread',
    'thread_pool',
    'virtual_memory_android',
    'virtual_memory_linux',
    'virtual_memory_macos',
    'virtual_memory_win',
  ]
  for cc in vm_ccs:
    objects += env.Object(os.path.join(outdir, 'vm', cc + '.o'),
                          os.path.join('src', 'vm', cc + '.cc'))

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
                          os.path.join('src', 'double-conversion', cc + '.cc'))

  program = env.Program(os.path.join(outdir, 'primordialsoup'), objects)
  return str(program[0])


def BuildSnapshots(outdir, host_vm):
  nssources = Glob(os.path.join('src', 'newspeak', '*.ns'))
  compilersnapshot = os.path.join('snapshots', 'compiler.vfuel')
  snapshots = []
  cmd = host_vm + ' ' + compilersnapshot + ' $SOURCES'

  helloout = os.path.join(outdir, 'HelloApp.vfuel')
  snapshots += [helloout]
  cmd += ' RuntimeForPrimordialSoup HelloApp ' + helloout

  testout = os.path.join(outdir, 'TestRunner.vfuel')
  snapshots += [testout]
  cmd += ' RuntimeWithBuildersForPrimordialSoup TestRunner ' + testout

  benchmarkout = os.path.join(outdir, 'BenchmarkRunner.vfuel')
  snapshots += [benchmarkout]
  cmd += ' RuntimeForPrimordialSoup BenchmarkRunner ' + benchmarkout

  compilerout = os.path.join(outdir, 'CompilerApp.vfuel')
  snapshots += [compilerout]
  cmd += ' RuntimeWithBuildersForPrimordialSoup CompilerApp ' + compilerout

  Command(target=snapshots, source=nssources, action=cmd)
  Requires(snapshots, host_vm)
  Depends(snapshots, compilersnapshot)


def Main():
  target_os = ARGUMENTS.get('os', None)
  host_os = None
  if platform.system() == 'Linux':
    host_os = 'linux'
  elif platform.system() == 'Darwin':
    host_os = 'macos'
  elif platform.system() == 'Windows':
    host_os = 'windows'

  target_cxx = ARGUMENTS.get('cxx_target', None)
  host_cxx = None
  if platform.system() == 'Linux':
    host_cxx = 'g++'
  elif platform.system() == 'Darwin':
    host_cxx = 'clang++'
  elif platform.system() == 'Windows':
    host_cxx = 'cl'
  host_cxx = ARGUMENTS.get('cxx_host', host_cxx)

  target_arch = ARGUMENTS.get('arch', None)
  host_arch = None
  if (platform.machine() == 'x86_64' or
      platform.machine() == 'AMD64'):
    host_arch = 'x64'
  elif platform.machine() == 'i386':
    host_arch = 'ia32'
  elif platform.machine() == 'aarch64':
    host_arch = 'arm64'
  elif platform.machine() == 'armv7l':
    host_arch = 'arm'
  elif platform.machine() == 'mips':
    host_arch = 'mips'
  elif platform.machine() == 'riscv32':
    host_arch = 'riscv32'
  elif platform.machine() == 'riscv64':
    host_arch = 'riscv64'
  else:
    raise Exception("Unknown machine" + platform.machine())

  sanitize = ARGUMENTS.get('sanitize', None)
  if sanitize not in ['address', 'thread', 'undefined', None]:
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
  elif host_arch == 'x64' and sanitize != 'thread' and target_arch == None :
    # If on X64, also build for IA32. Skip when using TSan as it doesn't
    # support IA32. Also skip when a specific architecture was asked for.
    BuildVM(host_cxx, 'ia32', host_os, True, sanitize)
    BuildVM(host_cxx, 'ia32', host_os,  False, sanitize)

Main()
