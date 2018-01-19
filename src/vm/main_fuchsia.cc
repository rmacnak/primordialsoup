// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include <fcntl.h>
#include <fdio/namespace.h>
#include <signal.h>
#include <zx/vmar.h>
#include <zx/vmo.h>

#include <thread>  // NOLINT

#include "garnet/public/lib/app/cpp/application_context.h"
#include "garnet/public/lib/app/cpp/connect.h"
#include "garnet/public/lib/app/fidl/application_runner.fidl.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fxl/macros.h"

#include "vm/primordial_soup.h"
#include "vm/virtual_memory.h"

class PrimordialSoupApplicationController : public app::ApplicationController {
 public:
  PrimordialSoupApplicationController(
      fidl::InterfaceRequest<app::ApplicationController> controller)
      : binding_(this), wait_callbacks_(), namespace_(nullptr) {
    if (controller.is_pending()) {
      binding_.Bind(std::move(controller));
      binding_.set_connection_error_handler([this] { Kill(); });
    }
  }

  ~PrimordialSoupApplicationController() override {
    for (const auto& iter : wait_callbacks_) {
      iter(0);
    }
    wait_callbacks_.clear();

    if (namespace_ != nullptr) {
      fdio_ns_destroy(namespace_);
    }
  }

  void Run(app::ApplicationPackagePtr application,
           app::ApplicationStartupInfoPtr startup_info) {
    if (!SetupNamespace(&startup_info->flat_namespace)) return;

    uintptr_t snapshot;
    size_t snapshot_size;
    if (!MapSnapshot(&snapshot, &snapshot_size)) return;

    fidl::Array<fidl::String>& arguments = startup_info->launch_info->arguments;
    intptr_t argc = 0;
    const char** argv = nullptr;
    if (!arguments.is_null()) {
      argc = arguments.size();
      argv = new const char*[argc];
      for (intptr_t i = 0; i < argc; i++) {
        argv[i] = arguments[i].data();
      }
    }

    PrimordialSoup_RunIsolate(reinterpret_cast<void*>(snapshot), snapshot_size,
                              argc, argv);

    delete argv;

    zx::vmar::root_self().unmap(snapshot, snapshot_size);
  }

 private:
  bool SetupNamespace(app::FlatNamespacePtr* flat_namespace) {
    app::FlatNamespacePtr& flat = *flat_namespace;
    zx_status_t status = fdio_ns_create(&namespace_);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create namespace";
      return false;
    }

    for (size_t i = 0; i < flat->paths.size(); ++i) {
      zx::channel dir = std::move(flat->directories[i]);
      zx_handle_t dir_handle = dir.release();
      const char* path = flat->paths[i].data();
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

    zx::vmar::root_self().map(0, snapshot_vmo.vmo(), 0, snapshot_vmo.size(),
                              ZX_VM_FLAG_PERM_READ, snapshot);
    *snapshot_size = snapshot_vmo.size();
    return true;
  }

  void Kill() override {
    fsl::MessageLoop* loop = fsl::MessageLoop::GetCurrent();
    loop->QuitNow();
  }

  void Detach() override {
    binding_.set_connection_error_handler(fxl::Closure());
  }

  void Wait(const WaitCallback& callback) override {
    wait_callbacks_.push_back(callback);
  }

  fidl::Binding<app::ApplicationController> binding_;
  std::vector<WaitCallback> wait_callbacks_;
  fdio_ns_t* namespace_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PrimordialSoupApplicationController);
};

static void RunApplication(
    app::ApplicationPackagePtr application,
    app::ApplicationStartupInfoPtr startup_info,
    fidl::InterfaceRequest<app::ApplicationController> controller) {
  fsl::MessageLoop loop;
  PrimordialSoupApplicationController app(std::move(controller));
  app.Run(std::move(application), std::move(startup_info));
}

class PrimordialSoupApplicationRunner : public app::ApplicationRunner {
 public:
  explicit PrimordialSoupApplicationRunner(
      fidl::InterfaceRequest<app::ApplicationRunner> app_runner)
      : binding_(this, std::move(app_runner)) {}

  ~PrimordialSoupApplicationRunner() override {}

 private:
  void StartApplication(
      app::ApplicationPackagePtr application,
      app::ApplicationStartupInfoPtr startup_info,
      fidl::InterfaceRequest<app::ApplicationController> controller) override {
    // TODO(rmacnak): Should the embedder or the VM spawn this thread?
    std::thread thread(RunApplication, std::move(application),
                       std::move(startup_info), std::move(controller));
    thread.detach();
  }

  fidl::Binding<app::ApplicationRunner> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PrimordialSoupApplicationRunner);
};

static void SIGINT_handler(int sig) {
  PrimordialSoup_InterruptAll();
}

int main(int argc, const char** argv) {
  PrimordialSoup_Startup();

  if (argc < 2) {
    fsl::MessageLoop loop;
    std::unique_ptr<app::ApplicationContext> context =
        app::ApplicationContext::CreateFromStartupInfo();

    context->outgoing_services()->AddService<app::ApplicationRunner>(
        [](fidl::InterfaceRequest<app::ApplicationRunner> app_runner) {
          new PrimordialSoupApplicationRunner(std::move(app_runner));
        });
    loop.Run();
  } else {
    psoup::VirtualMemory snapshot = psoup::VirtualMemory::MapReadOnly(argv[1]);
    void (*defaultSIGINT)(int) = signal(SIGINT, SIGINT_handler);
    PrimordialSoup_RunIsolate(reinterpret_cast<void*>(snapshot.base()),
                              snapshot.size(), argc - 2, &argv[2]);
    signal(SIGINT, defaultSIGINT);
    snapshot.Free();
  }

  PrimordialSoup_Shutdown();
  return 0;
}
