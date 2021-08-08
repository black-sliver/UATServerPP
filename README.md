# UATServerPP

Header-only C++ library implementing [UAT](https://github.com/black-sliver/UAT)
server for asio/websocketpp.

See [here](https://github.com/black-sliver/UAT/blob/master/PROTOCOL.md)
for protocol description.


## How to build

* define `ASIO_BOOST` or `ASIO_STANDALONE` project-wide or before including
* `#include <uatserverpp.hpp>`
* add other header libraries to the include path in you Makefile or project:
  * [nlohmann::json](https://github.com/nlohmann/json)
  * [valijson](https://github.com/tristanpenman/valijson)
  * standalone [asio](https://github.com/chriskohlhoff/asio) (if not using boost)
  * [websocketpp](https://github.com/zaphoyd/websocketpp)
* if you build with mingw on windows you may want to patch asio to allow std::thread for mingw
* see [UATBridge/Makefile](https://github.com/black-sliver/UATBridge/blob/master/Makefile) for an example


## How to use

```c++
#include <list>
#include <uatserverpp.hpp>
UAT::Server* server;
asio::io_service service;

void run() {
    server = new UAT::Server(&service);
    server->set_name("Name of the Game or Mod");
    server->set_version("1.00");
    server->set_slots({"Player 1", "Player 2"}); // optional slots
    server->start(); // start to listen
    service.run(); // or any other way to poll the asio service
}

void some_service_callback() {
    std::list<UAT::Var> vars = {
        {"Player 1", "sword", 1} // slot is "" if there are no slots
    };
    server->set_vars(vars);
}

void some_other_callback() {
    service.post(some_service_callback); // run code inside asio service thread
}
```

see [UATBridge](https://github.com/black-sliver/UATBridge) for a complete example.
