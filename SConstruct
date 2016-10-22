# -*- mode: python -*-

import os
import platform

def BuildVM(cxx, arch, os, debug):
  env = Environment()
  env['CXX'] = cxx

  outdir = 'out/'
  if debug:
    env['CCFLAGS'] += ['-DDEBUG']
    outdir += 'Debug'
  else:
    env['CCFLAGS'] += ['-DNDEBUG']
    outdir += 'Release'

  if os == 'android':
    outdir += 'Android'
    
  if arch == 'ia32':
    env['CCFLAGS'] += ['-m32']
    env['LINKFLAGS'] += ['-m32']
    outdir += 'IA32/'
  elif arch == 'x64':
    env['CCFLAGS'] += ['-m64']
    env['LINKFLAGS'] += ['-m64']
    outdir += 'X64/'
  elif arch == 'arm':
    outdir += 'ARM/'
  elif arch == 'arm64':
    outdir += 'ARM64/'
  elif arch == 'mips':
    env['CCFLAGS'] += ['-EL']
    env['LINKFLAGS'] += ['-EL']
    outdir += 'MIPS/'
  elif arch == 'mips64':
    env['CCFLAGS'] += ['-EL']
    env['LINKFLAGS'] += ['-EL']
    outdir += 'MIPS64/'
  elif arch == 'riscv32':
    env['CCFLAGS'] += ['-m32']
    env['LINKFLAGS'] += ['-m32']
    outdir += 'RISCV32/'
  elif arch == 'riscv64':
    env['CCFLAGS'] += ['-m64']
    env['LINKFLAGS'] += ['-m64']
    outdir += 'RISCV64/'

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
    '-D_FORTIFY_SOURCE=2',
  ]

  if os == 'macos':
    env['LINKFLAGS'] += [
      '-fPIE',
      '-dead_strip',
    ]
    env['LIBS'] = [
      'm',
      'stdc++',
      'pthread',
    ]
  if os == 'linux':
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
  elif os == 'android':
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
    'os_thread',
    'os_thread_android',
    'os_thread_linux',
    'os_thread_macos',
    'port',
    'primitives',
    'snapshot',
    'thread',
    'thread_pool',
    'virtual_memory_android',
    'virtual_memory_linux',
    'virtual_memory_macos',
  ]
  for cc in vm_ccs:
    objects += env.Object(outdir + 'vm/' + cc + '.o', 'src/vm/' + cc + '.cc')

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
    objects += env.Object(outdir + 'double-conversion/' + cc + '.o',
                          'src/double-conversion/' + cc + '.cc')

  program = env.Program(outdir + 'primordialsoup', objects)
  return str(program[0])


def BuildSnapshots(outdir, host_vm):
  nssources = Glob('src/newspeak/*.ns')

  snapshots = []
  cmd = host_vm + ' snapshots/compiler.vfuel $SOURCES'
  snapshots += [outdir + 'HelloApp.vfuel']
  cmd += ' RuntimeForPrimordialSoup HelloApp ' + outdir + 'HelloApp.vfuel'
  snapshots += [outdir + 'TestRunner.vfuel']
  cmd += ' RuntimeWithBuildersForPrimordialSoup TestRunner ' + outdir + 'TestRunner.vfuel'
  snapshots += [outdir + 'BenchmarkRunner.vfuel']
  cmd += ' RuntimeForPrimordialSoup BenchmarkRunner ' + outdir + 'BenchmarkRunner.vfuel'
  snapshots += [outdir + 'CompilerApp.vfuel']
  cmd += ' RuntimeWithBuildersForPrimordialSoup CompilerApp ' + outdir + 'CompilerApp.vfuel'

  Command(target=snapshots, source=nssources, action=cmd)
  Requires(snapshots, host_vm)
  Depends(snapshots, 'snapshots/compiler.vfuel')


def Main():
  target_os = ARGUMENTS.get('os', None)
  host_os = None
  if platform.system() == 'Linux':
    host_os = 'linux'
  elif platform.system() == 'Darwin':
    host_os = 'macos'

  target_cxx = ARGUMENTS.get('cxx_target', None);
  host_cxx = None
  if platform.system() == 'Linux':
    host_cxx = 'g++'
  elif platform.system() == 'Darwin':
    host_cxx = 'clang++'
  host_cxx = ARGUMENTS.get('cxx_host', host_cxx)    

  target_arch = ARGUMENTS.get('arch', None)
  host_arch = None
  if platform.machine() == 'x86_64':
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

  # Always build for the host so we can create the snapshots.
  BuildVM(host_cxx, host_arch, host_os, True)
  host_vm = BuildVM(host_cxx, host_arch, host_os, False)
  BuildSnapshots('out/snapshots/', host_vm)

  if (target_os != None and host_os != target_os) or (target_arch != None and host_arch != target_arch):
    # If cross compiling, also build for the target.
    BuildVM(target_cxx, target_arch, target_os, True)
    BuildVM(target_cxx, target_arch, target_os, False)
  elif host_arch == 'x64' and target_arch == None:
    BuildVM(host_cxx, 'ia32', host_os, True)
    BuildVM(host_cxx, 'ia32', host_os, False)

Main()
