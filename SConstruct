# -*- mode: python -*-

import os
import platform

OPT = '-O3'
WARNINGS = '-Wall -Wextra -Wnon-virtual-dtor -Wvla -Wno-conversion-null -Wno-unused-parameter'
FEATURES = '-g3 -fno-rtti -fno-exceptions -fstack-protector'
DEFINES = '-D_FORTIFY_SOURCE=2'
LDFLAGS = ''
if platform.system() == 'Linux':
  LDFLAGS += ' -Wl,-z,relro,-z,now -Wl,--gc-section -Wl,-z,noexecstack'
elif platform.system() == 'Darwin':
  LDFLAGS += ' -dead_strip'

def BuildVM(outdir, ARCH, DEBUG):
  env = Environment()
  env['CC'] = os.environ.get('CC', 'gcc')
  env['CXX'] = os.environ.get('CXX', 'g++')
  env['CCFLAGS'] = ' '.join([ARCH, OPT, WARNINGS, FEATURES, DEFINES, DEBUG])
  env['LINKFLAGS'] = ' '.join([ARCH, LDFLAGS])
  env['LIBS'] = ['pthread', 'm', 'stdc++']
  env['CPPPATH'] = [os.environ.get('I'), 'src']
  
  objects = []

  vm_ccs = [
    'assert',
    'double_conversion',
    'heap',
    'interpreter',
    'isolate',
    'lookup_cache',
    'main',
    'message',
    'object',
    'os_linux',
    'os_macos',
    'os_thread',
    'os_thread_linux',
    'os_thread_macos',
    'port',
    'primitives',
    'snapshot',
    'thread',
    'thread_pool',
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


def BuildSnapshots(outdir, vm):
  nssources = Glob('src/newspeak/*.ns3')

  snapshots = []
  cmd = vm + ' snapshots/compiler.vfuel $SOURCES'
  snapshots += [outdir + 'HelloApp.vfuel']
  cmd += ' RuntimeForPrimordialSoup HelloApp ' + outdir + 'HelloApp.vfuel'
  snapshots += [outdir + 'TestRunner.vfuel']
  cmd += ' RuntimeForPrimordialSoup TestRunner ' + outdir + 'TestRunner.vfuel'
  snapshots += [outdir + 'BenchmarkRunner.vfuel']
  cmd += ' RuntimeForPrimordialSoup BenchmarkRunner ' + outdir + 'BenchmarkRunner.vfuel'
  snapshots += [outdir + 'CompilerApp.vfuel']
  cmd += ' RuntimeWithBuildersForPrimordialSoup CompilerApp ' + outdir + 'CompilerApp.vfuel'

  Command(target=snapshots, source=nssources, action=cmd)
  Requires(snapshots, vm)
  Depends(snapshots, 'snapshots/compiler.vfuel')


machine = platform.machine()
if machine == 'x86_64':
  BuildVM('out/DebugX64/', '-m64', '-DDEBUG')
  BuildVM('out/ReleaseX64/', '-m64', '-DNDEBUG')
  BuildVM('out/DebugIA32/', '-m32', '-DDEBUG')
  BuildVM('out/ReleaseIA32/', '-m32', '-DNDEBUG')
  BuildSnapshots('out/snapshots/', 'out/ReleaseX64/primordialsoup')
elif machine == 'i386':
  BuildVM('out/DebugIA32/', '-m32', '-DDEBUG')
  BuildVM('out/ReleaseIA32/', '-m32', '-DNDEBUG')
  BuildSnapshots('out/snapshots/', 'out/ReleaseIA32/primordialsoup')
elif machine == 'aarch64':
  BuildVM('out/DebugARM64/', '', '-DDEBUG')
  BuildVM('out/ReleaseARM64/', '', '-DNDEBUG')
  BuildSnapshots('out/snapshots/', 'out/ReleaseARM64/primordialsoup')
elif machine == 'armv7l' or machine == 'armv6l':
  BuildVM('out/DebugARM/', '', '-DDEBUG')
  BuildVM('out/ReleaseARM/', '', '-DNDEBUG')
  BuildSnapshots('out/snapshots/', 'out/ReleaseARM/primordialsoup')
elif machine == 'mips':
  BuildVM('out/DebugMIPS/', '', '-DDEBUG')
  BuildVM('out/ReleaseMIPS/', '', '-DNDEBUG')
  BuildSnapshots('out/snapshots/', 'out/ReleaseMIPS/primordialsoup')
else:
  print("No configuration for " + machine)
  Exit(1)
