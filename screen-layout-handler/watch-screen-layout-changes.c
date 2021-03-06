#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <sys/signalfd.h>

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

int signal_received = 0;

void sigterm_handler(int sig) {
    signal_received = 1;
}

int main(int argc, char **argv) {
    sigset_t sigmask;
    int sigfd;
    Display *d;
    Window root_win;
    int xrr_event_base = 0;
    int xrr_error_base = 0;
    int x11_fd;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <script>\n", argv[0]);
        exit(1);
    }

    signal(SIGCHLD, SIG_IGN);
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &sigmask, NULL) == -1) {
        perror("Couldn't block signals for graceful signal recovery");
        exit(1);
    }
    sigfd = signalfd(-1, &sigmask, SFD_CLOEXEC);
    if (sigfd == -1) {
        perror("Couldn't create signalfd for graceful signal recovery");
        exit(1);
    }

    d = XOpenDisplay(NULL);
    if (!d) {
        fprintf(stderr, "Failed to open display\n");
        exit(1);
    }
    root_win = DefaultRootWindow(d);

    if (!XRRQueryExtension(d, &xrr_event_base, &xrr_error_base)) {
        fprintf(stderr, "RandR extension missing\n");
        exit(1);
    }
    XRRSelectInput(d, root_win, RRScreenChangeNotifyMask);

    XFlush(d);
    x11_fd = ConnectionNumber(d);
    while (42) {
        XEvent ev;
        fd_set in_fds;
        FD_ZERO(&in_fds);
        FD_SET(sigfd, &in_fds);
        FD_SET(x11_fd, &in_fds);

        if (select(FD_SETSIZE, &in_fds, NULL, NULL, NULL) < 0) {
            XCloseDisplay(d);
            exit(2);
        }

        if (FD_ISSET(sigfd, &in_fds)) {
            /* This must be SIGTERM as we are not listening on anything else */
            XCloseDisplay(d);
            exit(0);
        }

        while (XPending(d)) {
            XNextEvent(d, &ev);
            XRRUpdateConfiguration(&ev);

            if (ev.type != xrr_event_base + RRScreenChangeNotify) {
                /* skip other events (this shouldn't happen) */
                continue;
            }

            fprintf(stderr, "Screen layout change event received\n");
            XRRScreenChangeNotifyEvent *e = (XRRScreenChangeNotifyEvent *) &ev;
            switch (fork()) {
                case 0:
                    close(ConnectionNumber(d));
                    execvp(argv[1], &argv[1]);
                    perror("Failed to execute script");
                    exit(1);
                case -1:
                    perror("fork");
                    exit(1);
                default:
                    break;
            }
        }
    }
    return 0;
}
