
#include "ipc-types-private.h"

#include "iceoryx_hoofs/posix_wrapper/signal_watcher.hpp"
#include "iceoryx_posh/popo/client.hpp"
#include "iceoryx_posh/popo/wait_set.hpp"
#include "iceoryx_posh/runtime/posh_runtime.hpp"

#include <iostream>
#include "../mlinkmodule.h"

using namespace Syntalos;

constexpr char APP_NAME[] = "syntalos";

struct ContextData {
    int64_t requestSequenceId = 0;
    int64_t expectedResponseSequenceId = requestSequenceId;
};

int main()
{
    iox::runtime::PoshRuntime::initRuntime(APP_NAME);

    auto mod = new MLinkModule();

    while (true)
        mod->testProcess();

    return (EXIT_SUCCESS);
}
