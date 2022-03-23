#include "webserver.h"

int main()
{
    WebServer server;
    server.init(8888, 8);
    server.event_listen();
    server.event_loop();
    return 0;
}

