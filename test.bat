IF "%PROCESSOR_ARCHITECTURE%" == "AMD64" (
  out\DebugIA32\primordialsoup out\snapshots\HelloApp.vfuel
  out\DebugX64\primordialsoup.exe out\snapshots\HelloApp.vfuel
  out\ReleaseIA32\primordialsoup.exe out\snapshots\HelloApp.vfuel
  out\ReleaseX64\primordialsoup.exe out\snapshots\HelloApp.vfuel

  set PSOUP_TEST_PROCESS=out\DebugIA32\test_process.exe
  out\DebugIA32\primordialsoup.exe out\snapshots\IOTestRunner.vfuel
  set PSOUP_TEST_PROCESS=out\DebugX64\test_process.exe
  out\DebugX64\primordialsoup.exe out\snapshots\IOTestRunner.vfuel
  set PSOUP_TEST_PROCESS=out\ReleaseIA32\test_process.exe
  out\ReleaseIA32\primordialsoup.exe out\snapshots\IOTestRunner.vfuel
  set PSOUP_TEST_PROCESS=out\ReleaseX64\test_process.exe
  out\ReleaseX64\primordialsoup.exe out\snapshots\IOTestRunner.vfuel

  out\DebugIA32\primordialsoup.exe out\snapshots\TestRunner.vfuel
  out\DebugX64\primordialsoup.exe out\snapshots\TestRunner.vfuel
  out\ReleaseIA32\primordialsoup.exe out\snapshots\TestRunner.vfuel
  out\ReleaseX64\primordialsoup.exe out\snapshots\TestRunner.vfuel

  out\ReleaseIA32\primordialsoup.exe out\snapshots\BenchmarkRunner.vfuel
  out\ReleaseX64\primordialsoup.exe out\snapshots\BenchmarkRunner.vfuel
)

IF "%PROCESSOR_ARCHITECTURE%" == "ARM64" (
  out\DebugARM64\primordialsoup.exe out\snapshots\HelloApp.vfuel
  out\ReleaseARM64\primordialsoup.exe out\snapshots\HelloApp.vfuel

  set PSOUP_TEST_PROCESS=out\DebugARM64\test_process.exe
  out\DebugARM64\primordialsoup.exe out\snapshots\IOTestRunner.vfuel
  set PSOUP_TEST_PROCESS=out\ReleaseARM64\test_process.exe
  out\ReleaseARM64\primordialsoup.exe out\snapshots\IOTestRunner.vfuel

  out\DebugARM64\primordialsoup.exe out\snapshots\TestRunner.vfuel
  out\ReleaseARM64\primordialsoup.exe out\snapshots\TestRunner.vfuel

  out\ReleaseARM64\primordialsoup.exe out\snapshots\BenchmarkRunner.vfuel
)