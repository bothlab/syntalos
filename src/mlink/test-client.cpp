
#include "modulelink.h"

int main()
{
    auto mlink = Syntalos::initModuleLink();

    mlink->processMessagesForever();

    return EXIT_SUCCESS;
}
