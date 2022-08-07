IF "%PROCESSOR_ARCHITECTURE%" == "AMD64" (
  out\DebugIA32\primordialsoup out\snapshots\HelloApp.vfuel
  out\DebugX64\primordialsoup.exe out\snapshots\HelloApp.vfuel
  out\ReleaseIA32\primordialsoup.exe out\snapshots\HelloApp.vfuel
  out\ReleaseX64\primordialsoup.exe out\snapshots\HelloApp.vfuel

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

  out\DebugARM64\primordialsoup.exe out\snapshots\TestRunner.vfuel
  out\ReleaseARM64\primordialsoup.exe out\snapshots\TestRunner.vfuel

  out\ReleaseARM64\primordialsoup.exe out\snapshots\BenchmarkRunner.vfuel
)