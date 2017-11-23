// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include <signal.h>
#include <zx/vmar.h>
#include <zx/vmo.h>

#include <thread>  // NOLINT

#include "garnet/public/lib/app/cpp/application_context.h"
#include "garnet/public/lib/app/cpp/connect.h"
#include "garnet/public/lib/app/fidl/application_runner.fidl.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/macros.h"

#include "vm/primordial_soup.h"
#include "vm/virtual_memory.h"

class PrimordialSoupApplicationController : public app::ApplicationController {
 public:
  PrimordialSoupApplicationController(
      app::ApplicationPackagePtr application,
      app::ApplicationStartupInfoPtr startup_info,
      fidl::InterfaceRequest<app::ApplicationController> controller)
      : binding_(this),
        wait_callbacks_() {
    if (controller.is_pending()) {
      binding_.Bind(std::move(controller));
      binding_.set_connection_error_handler([this] { Kill(); });
    }

    zx::vmo& snapshot_vmo = application->data->vmo;
    size_t snapshot_size = application->data->size;
    uintptr_t snapshot = 0;
    zx::vmar::root_self().map(0, snapshot_vmo, 0, snapshot_size,
                              ZX_VM_FLAG_PERM_READ, &snapshot);

    fidl::Array<fidl::String>& arguments = startup_info->launch_info->arguments;
    intptr_t argc = 0;
    const char** argv = NULL;
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

    for (const auto& iter : wait_callbacks_) {
      iter(0);
    }
    wait_callbacks_.clear();
  }

  ~PrimordialSoupApplicationController() override {}

 private:
  void Kill() override { FXL_LOG(FATAL) << "Unimplemented"; }

  void Detach() override {
    binding_.set_connection_error_handler(fxl::Closure());
  }

  void Wait(const WaitCallback& callback) override {
    wait_callbacks_.push_back(callback);
  }

  fidl::Binding<app::ApplicationController> binding_;
  std::vector<WaitCallback> wait_callbacks_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PrimordialSoupApplicationController);
};

static void RunApplication(
    app::ApplicationPackagePtr application,
    app::ApplicationStartupInfoPtr startup_info,
    fidl::InterfaceRequest<app::ApplicationController> controller) {
  fsl::MessageLoop loop;
  PrimordialSoupApplicationController app(
      std::move(application), std::move(startup_info), std::move(controller));
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
