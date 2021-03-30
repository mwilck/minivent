# minivent - a compact epoll-based event and timer handling library

**minivent** provides a minimal API for event-based programs on Linux, using
**epoll(7)** and and timerfd (see **timerfd_create(2)**). As such, it is
similar in purpose, but smaller and more limited in scope than
[libevent](https://libevent.org), [libev](http://software.schmorp.de/pkg/libev.html), or the
[GLib event loop](https://developer.gnome.org/glib/stable/glib-The-Main-Event-Loop.html).

 * Any event that has an associated pollable file descriptor can be handled.
 * No specific signal handling is implemented, but applications can define
   how signals are dealt with.
 * Timers are implemented without using signals.
 * Every event can have an associated timeout.
 * A single callback function is used for the incidents "event occured" and
   "event timed out".
 * Timers are handled as "events without file descriptor".

## License

The library itself is released under [GNU LGPL](LICENSE.txt), version 2.1 or later. 
The test programs are released under [GNU GPL](LICENSE.test.txt), version 2.0 or later.

## Building

Run `make`. Run `make test` to build the test programs, and `make run-test` to
run all test programs.

### External sources

**minivent** relies on some generic functionality which your program is
likely to have already. To avoid duplicate implementation and allow for
consistent behavior in the application, this functionality can either be
pulled from the application, or created from sources in the `external/`
subdirectory:

 * `common.h` - some generic utilities
 * `log.h`, `log.c` - logging functions
 * `cleanup.h`, `cleanup.c` - syntactic sugar for `__attribute__((cleanup()))`
 
By default, the build process uses the implementation in the `external/`
subdirectory. This can be overridden by setting environment variables:

 * `INCLUDE` - Include path (space separated list of directories relative to
   the minivent top directory, with `-I` prefix). Defaults to `-Iexternal`.
 * `LOG_O` - object file that provides the logging functionality. This can be
   an empty string. The minivent code only uses the `msg()` macro (see `log.h`).
 * `CLEANUP_O` - object file that provides the cleanup functionality. Can be
   set to an empty string.

### Build dependencies

The library itself depends only on functionality provided by the C
library. Some of the test programs depend on the [cmocka](https://cmocka.org/)
test framework. The test program for [Avahi](https://www.avahi.org/)
integration depends on Avahi.

## API documentation

The main API is documented in [event.h](event.h). Some utility functions are
available in other header files, such as [ts-util.h](ts-util.h) for dealing
with `struct timespec` objects.

### Data structures

The main data structure is `struct event`. Applications will typically embed a
`struct event` in their own data structures. Convenience macros are
provided to initialize `struct event` for typical cases.

`struct dispatcher` keeps track of registered events and dispatches
callbacks. Every application using **minivent** needs at least one `struct dispatcher`.

### Event callback

```
typedef int (*cb_fn)(struct event *evt, uint32_t events);
```

The callback function is passed a pointer to struct event, and a
bitmask of received events as obtained from **epoll_pwait(2)**. The callback
should check `evt->reason` to determine why it was called, and act
accordingly. The callback should return `EVENTCB_CONTINUE` unless the
application wants the event to be destroyed / removed from the event
dispatcher queue, in which case it should return `EVENTCB_CLEANUP` or
`EVENTCB_REMOVE`. In the `EVENTCB_CLEANUP` case, the event's `cleanup`
callback is invoked to allow the application to release resources.
The callback can call `event_modify()` in order to change the set of
epoll flags.

All timeouts are "oneshot" timers. If the event has timed out, and the
application wants to be woken up later again, it needs to set a new timeout
by calling `event_mod_timeout()`.

**Note**: Races between completion and timeout can't be completely avoided.
If a callback function returns `EVENTCB_CONTINUE` after a timeout (thus not
destroying the event object) and the event source (file descriptor) remains
active, the previously timed-out event may still occur and the callback may
be called for it. The callback should be prepared for this to happen.
See the callback `test_cb()` in [event-test.c](test/event-test.c)
for an example.

If an event occurs, the associated timeout (if any) is not
automatically cancelled. The callback must call `event_mod_timeout()` to
postpone or cancel the timeout.

### Releasing resources

Events can define a `cleanup()` callback that will be called when the event
callback returns `EVENTCB_CLEANUP`, or when a dispatcher's contents are
cleared with `cleanup_dispatcher()` or `free_dispatcher()`. This way, the
main application doesn't need to keep track of resources used by event
handlers. The `cleanup()` callback is allowed to call `free()` on the  `struct event *`
passed to it. Two standard cleanup callback functions are included.

If an event-loop based program needs to **fork** children, it will usually want
to close file descriptors and release resources related to the event loop.
Use `free_dispatcher()` for this purpose. **Note:** don't use
`cleanup_dispatcher()`, as it affects the state of the kernel objects used
by the event loop, even if it's called from a child process.

### Event loop

The `event_loop()` function is provided as a simle main loop for an
application. Alternatively, applications can implement their own main
program, and call `event_wait()` as they see fit.

#### Exiting the event loop by "committing suicide"

`event_loop()` has no built-in termination event. Normally, applications will
react to signals such as SIGTERM anyway. Therefore the preferred way to exit
the event loop is simply that the application sends some signal to itself
from some callback.
The chosen signal must be blocked during regular execution and unblocked
while waiting for events (using the `sigmask` argument of `event_loop()`),
and must not use the default action, i.e. it must have an
application-defined signal handler, which could be an empty function.

## Example code

See the programs in the `test/` subdirectory for
examples. Here is a simplified minimal program using **minivent** to run a
callback after one second:

    #include <stdio.h>
    #include <unistd.h>
    #include <signal.h>
    #include "event.h"
    
    static int cb(struct event *evt, uint32_t events)
    {
            fprintf(stderr, "Hello world! (%s)\n", reason_str[evt->reason]);
            kill(getpid(), SIGINT);
            return EVENTCB_CLEANUP;
    }
    
    int main(void) {
            struct dispatcher *dsp = new_dispatcher(CLOCK_REALTIME);
            struct event *evt = TIMER_EVENT_ON_STACK(cb, 1000000);
    
            event_add(dsp, &evt);
            event_loop(dsp, &mask, NULL);
            free_dispatcher(dsp);
			return 0;
    }

To make this actually work, code for setting up signal handling must be added.
The sample program [mini-test.c](test/mini-test.c) has the full working code.

[echo-test.c](test/echo-test.c) (an echo-server implementation) uses the
functionality of **minivent** extensively to demonstrate various aspects
of the API.

## Performance

**minivent** isn't fine-tuned for maximum throughput or IOPS, it was mainly developed
with "rare" events in mind, which are likely to time out. It is
single-threaded by design. This said, the `echo-test` program can be used as a
simple benchmark with the option `--max-wait=0`, it sends small messages back
and forth via sockets, between a configurable number of clients and an
event-driven server process:

    export LD_LIBRARY_PATH=$PWD
    ./test/echo-test --runtime=100 --max-wait=0 --num-clients=$N

Here are a few results from a 2-socket, 6-core Intel® E5-2620 v3 system:

| # clients | requests / s | max rtt / us |
|----------:|-------------:|-------------:|
| 1         |       140000 |           80 |
| 2         |       280000 |          137 |
| 4         |       270000 |          141 |
| 8         |       240000 |          265 |
| 16        |       260000 |          320 |
| 100       |       250000 |        10000 |

Here, a "request" means one message sent back and forth between client and
server, and the "rtt" measures the round trip time between the client sending
the message and retrieving the same message back. "max rtt" means the maximum
measured rtt over all request and all clients. It can be seen that the
single-threaded server is saturated with two clients.

[Perf](https://perf.wiki.kernel.org/index.php/Main_Page) data indicates that
in these simple benchmarks, most time is spent in the kernel, **write(2)** and
**read(2)** accounting for more than half of the CPU load.

## Missing features and caveats

This code is provided **WITHOUT ANY WARRANTY**. See the [license](LICENSE.txt) for details.

What is not implemented, and likely will never be:

 * event priorization,
 * use of other APIs than **epoll(7)**,
 * (add your preferred feature here).

The library is designed to be used in a single-threaded program. None of its
functions can be assumed to be thread-safe.
