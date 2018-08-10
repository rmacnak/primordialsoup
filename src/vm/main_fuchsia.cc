// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/namespace.h>
#include <signal.h>
#include <zx/vmar.h>
#include <zx/vmo.h>

#include <thread>  // NOLINT

#include "lib/component/cpp/connect.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fxl/macros.h"

#include "vm/primordial_soup.h"
#include "vm/virtual_memory.h"

class PrimordialSoupComponentController
    : public fuchsia::sys::ComponentController {
 public:
  PrimordialSoupComponentController(
      fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller)
      : loop_(&kAsyncLoopConfigAttachToThread),
        binding_(this),
        wait_callbacks_(),
        namespace_(nullptr) {
    if (controller.is_valid()) {
      binding_.Bind(std::move(controller));
      binding_.set_error_handler([this] { Kill(); });
    }
  }

  ~PrimordialSoupComponentController() override {
    if (namespace_ != nullptr) {
      fdio_ns_destroy(namespace_);
    }
  }

  void Run(fuchsia::sys::Package package,
           fuchsia::sys::StartupInfo startup_info) {
    if (!SetupNamespace(&startup_info.flat_namespace)) return;

    uintptr_t snapshot;
    size_t snapshot_size;
    if (!MapSnapshot(&snapshot, &snapshot_size)) return;

    fidl::VectorPtr<fidl::StringPtr> arguments =
        std::move(startup_info.launch_info.arguments);
    intptr_t argc = arguments->size();
    const char** argv = new const char*[argc];
    for (intptr_t i = 0; i < argc; i++) {
      argv[i] = arguments->at(i)->data();
    }

    intptr_t exit_code =
        PrimordialSoup_RunIsolate(reinterpret_cast<void*>(snapshot),
                                  snapshot_size, argc, argv);

    delete[] argv;

    zx::vmar::root_self()->unmap(snapshot, snapshot_size);

    for (const auto& iter : wait_callbacks_) {
      iter(exit_code);
    }
    wait_callbacks_.clear();
  }

 private:
  bool SetupNamespace(fuchsia::sys::FlatNamespace* flat) {
    zx_status_t status = fdio_ns_create(&namespace_);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create namespace";
      return false;
    }

    for (size_t i = 0; i < flat->paths->size(); ++i) {
      zx::channel dir = std::move(flat->directories->at(i));
      zx_handle_t dir_handle = dir.release();
      const char* path = flat->paths->at(i)->data();
      status = fdio_ns_bind(namespace_, path, dir_handle);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to bind " << path << " to namespace";
        zx_handle_close(dir_handle);
        return false;
      }
    }

    return true;
  }

  bool MapSnapshot(uintptr_t* snapshot, size_t* snapshot_size) {
    fxl::UniqueFD root_dir(fdio_ns_opendir(namespace_));
    if (!root_dir.is_valid()) {
      FXL_LOG(ERROR) << "Failed to open namespace";
      return false;
    }

    const char* kSnapshotPath = "pkg/data/program.vfuel";
    fxl::UniqueFD snapshot_file(
        openat(root_dir.get(), kSnapshotPath, O_RDONLY));
    if (!snapshot_file.is_valid()) {
      FXL_LOG(ERROR) << "Failed to open snapshot at " << kSnapshotPath;
      return false;
    }

    fsl::SizedVmo snapshot_vmo;
    if (!fsl::VmoFromFd(std::move(snapshot_file), &snapshot_vmo)) {
      FXL_LOG(ERROR) << "Failed to map snapshot";
      return false;
    }

    zx::vmar::root_self()->map(0, snapshot_vmo.vmo(), 0, snapshot_vmo.size(),
                               ZX_VM_FLAG_PERM_READ, snapshot);
    *snapshot_size = snapshot_vmo.size();
    return true;
  }

  void Kill() override {
    loop_.Quit();
  }

  void Detach() override {
    binding_.set_error_handler(fit::closure());
  }

  void Wait(WaitCallback callback) override {
    wait_callbacks_.push_back(callback);
  }

  async::Loop loop_;
  fidl::Binding<fuchsia::sys::ComponentController> binding_;
  std::vector<WaitCallback> wait_callbacks_;
  fdio_ns_t* namespace_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PrimordialSoupComponentController);
};

static void RunComponent(
    fuchsia::sys::Package package,
    fuchsia::sys::StartupInfo startup_info,
    ::fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  PrimordialSoupComponentController app(std::move(controller));
  app.Run(std::move(package), std::move(startup_info));
}

class PrimordialSoupRunner : public fuchsia::sys::Runner {
 public:
  PrimordialSoupRunner()
    : context_(component::StartupContext::CreateFromStartupInfo()),
      bindings_() {
    context_->outgoing().AddPublicService<fuchsia::sys::Runner>(
      [this](fidl::InterfaceRequest<fuchsia::sys::Runner> request) {
        bindings_.AddBinding(this, std::move(request));
      });
  }

  ~PrimordialSoupRunner() override {}

 private:
  void StartComponent(
      fuchsia::sys::Package package,
      fuchsia::sys::StartupInfo startup_info,
      ::fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller)
      override {
    // TODO(rmacnak): Should the embedder or the VM spawn this thread?
    std::thread thread(RunComponent, std::move(package),
                       std::move(startup_info), std::move(controller));
    thread.detach();
  }

  std::unique_ptr<component::StartupContext> context_;
  fidl::BindingSet<fuchsia::sys::Runner> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PrimordialSoupRunner);
};

static void SIGINT_handler(int sig) {
  PrimordialSoup_InterruptAll();
}

int main(int argc, const char** argv) {
  PrimordialSoup_Startup();

  if (argc < 2) {
    async::Loop loop(&kAsyncLoopConfigAttachToThread);
    PrimordialSoupRunner runner;
    loop.Run();
  } else {
    psoup::VirtualMemory snapshot = psoup::VirtualMemory::MapReadOnly(argv[1]);
    void (*defaultSIGINT)(int) = signal(SIGINT, SIGINT_handler);
    async::Loop loop(&kAsyncLoopConfigAttachToThread);
    PrimordialSoup_RunIsolate(reinterpret_cast<void*>(snapshot.base()),
                              snapshot.size(), argc - 2, &argv[2]);
    signal(SIGINT, defaultSIGINT);
    snapshot.Free();
  }

  PrimordialSoup_Shutdown();
  return 0;
}
