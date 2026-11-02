#define _GNU_SOURCE
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/aio_abi.h>
#include <linux/usb/functionfs.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include "ffs_descriptors.h"
#include "typer.h"

#define FFS_DIR "/dev/ffs-hidkbd"
#define GADGET_UDC "/sys/kernel/config/usb_gadget/t3-hidkbd/UDC"
#define UDC_NAME "98050000.dwc3"
#define LOCK_PATH "/run/lock/t3-hidkbd.lock"
#define IO_TIMEOUT_MS 5000
#define ENUM_TIMEOUT_MS 30000
#define MAX_PAYLOAD (16U * 1024U * 1024U)

struct device {
    int ep0, ep1, event, udc, lock;
    aio_context_t aio;
    struct iocb request;
    uint8_t report[8], idle, protocol, leds;
    bool enabled, started, bound, pending, failed;
    uint64_t last_report;
};
static volatile sig_atomic_t interrupted;

static void on_signal(int signo) { interrupted = signo; }

static uint64_t now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) { perror("clock_gettime"); exit(1); }
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static int exact_write(int fd, const void *data, size_t length)
{
    ssize_t n = write(fd, data, length);
    if (n == (ssize_t)length) return 0;
    if (n >= 0) errno = EIO;
    return -1; /* Never split a descriptor or HID report across writes. */
}

static int stall(struct device *d, const struct usb_ctrlrequest *s)
{
    uint8_t dummy = 0;
    /* A transfer opposite to the setup direction stalls FunctionFS ep0. */
    ssize_t n = (s->bRequestType & USB_DIR_IN) ? read(d->ep0, &dummy, 0) : write(d->ep0, &dummy, 0);
    if (n < 0 && errno == EL2HLT) return 0;
    return n < 0 ? -1 : 0;
}

static int control(struct device *d, const struct usb_ctrlrequest *s)
{
    const unsigned value = le16toh(s->wValue), length = le16toh(s->wLength);
    const void *reply = NULL;
    size_t reply_len = 0;
    uint8_t byte = 0;
    if (le16toh(s->wIndex) != 0) return stall(d, s);
    if (s->bRequestType == (USB_DIR_IN | USB_RECIP_INTERFACE) &&
        s->bRequest == USB_REQ_GET_DESCRIPTOR && !(value & 0xff)) {
        if (value >> 8 == 0x22) { reply = report_descriptor; reply_len = sizeof(report_descriptor); }
        else if (value >> 8 == 0x21) { reply = hid_descriptor; reply_len = sizeof(hid_descriptor); }
        else return stall(d, s);
    } else if (s->bRequestType == (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE)) {
        switch (s->bRequest) {
        case 0x01: /* GET_REPORT: report ID zero, input or LED output */
            if (value == 0x0100) { reply = d->report; reply_len = 8; }
            else if (value == 0x0200) { reply = &d->leds; reply_len = 1; }
            else return stall(d, s);
            break;
        case 0x02: /* GET_IDLE */
            if (value || length != 1) return stall(d, s);
            reply = &d->idle; reply_len = 1; break;
        case 0x03: /* GET_PROTOCOL */
            if (value || length != 1) return stall(d, s);
            reply = &d->protocol; reply_len = 1; break;
        default: return stall(d, s);
        }
    } else if (s->bRequestType == (USB_TYPE_CLASS | USB_RECIP_INTERFACE)) {
        switch (s->bRequest) {
        case 0x09: /* SET_REPORT: one-byte LED output, no interrupt OUT endpoint */
            if (value != 0x0200 || length != 1) return stall(d, s);
            {
                ssize_t n = read(d->ep0, &byte, 1);
                if (n != 1) { if (n >= 0) errno = EPROTO; return -1; }
            }
            d->leds = byte & 0x1f;
            return 0;
        case 0x0a: /* SET_IDLE: duration in 4 ms units */
            if ((value & 0xff) || length) return stall(d, s);
            if (read(d->ep0, &byte, 0) < 0) return -1;
            d->idle = (uint8_t)(value >> 8);
            return 0;
        case 0x0b: /* SET_PROTOCOL: boot (0) and report (1) both use 8 bytes */
            if (value > 1 || length) return stall(d, s);
            if (read(d->ep0, &byte, 0) < 0) return -1;
            d->protocol = (uint8_t)value;
            return 0;
        default: return stall(d, s);
        }
    } else return stall(d, s);
    if (reply_len > length) reply_len = length;
    return exact_write(d->ep0, reply, reply_len);
}

/* ep1 uses Linux native AIO so a host that stops polling cannot block ep0. */
static int service(struct device *d, int timeout)
{
    struct pollfd p[2] = {{d->ep0, POLLIN, 0}, {d->event, POLLIN, 0}};
    if (interrupted || d->failed) return -1;
    int ret = poll(p, 2, timeout);
    if (ret < 0) return errno == EINTR && !interrupted ? 0 : -1;
    for (size_t i = 0; i < 2; ++i)
        if (p[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fprintf(stderr, "FunctionFS endpoint closed\n"); d->failed = true; return -1;
        }
    if (p[0].revents & POLLIN) {
        struct usb_functionfs_event event;
        ssize_t n = read(d->ep0, &event, sizeof(event));
        if (n < 0 && (errno == EAGAIN || errno == EIDRM || errno == EINTR)) return interrupted ? -1 : 0;
        if (n != sizeof(event)) { fprintf(stderr, "invalid FunctionFS event read\n"); return -1; }
        switch (event.type) {
        case FUNCTIONFS_SETUP:
            if (control(d, &event.u.setup) && errno != EIDRM && errno != EINTR) {
                perror("ep0 setup"); return -1;
            }
            break;
        case FUNCTIONFS_ENABLE:
            d->enabled = true;
            d->protocol = 1; d->idle = 0; d->leds = 0;
            fprintf(stderr, "USB keyboard enabled\n"); break;
        case FUNCTIONFS_DISABLE:
        case FUNCTIONFS_UNBIND:
        case FUNCTIONFS_SUSPEND:
            d->enabled = false;
            if (d->started) {
                fprintf(stderr, "USB disabled/suspended during typing; aborting (no automatic replay)\n");
                d->failed = true;
            }
            break;
        case FUNCTIONFS_RESUME: d->enabled = true; break;
        case FUNCTIONFS_BIND: break;
        default: fprintf(stderr, "unknown FunctionFS event %u\n", event.type); return -1;
        }
    }
    if (p[1].revents & POLLIN) {
        uint64_t count;
        struct io_event event;
        struct timespec zero = {0, 0};
        if (read(d->event, &count, sizeof(count)) != sizeof(count)) return -1;
        long n = syscall(SYS_io_getevents, d->aio, 0L, 1L, &event, &zero);
        if (n != 1 || !d->pending) { fprintf(stderr, "invalid AIO completion\n"); return -1; }
        d->pending = false;
        if (event.res != 8) {
            fprintf(stderr, "interrupt IN transfer failed: %lld\n", (long long)event.res);
            d->failed = true; return -1;
        }
        d->last_report = now_ms();
    }
    return interrupted || d->failed ? -1 : 0;
}

static int send_report(struct device *d, const uint8_t report[8])
{
    if (!d->enabled || d->pending || interrupted) return -1;
    memmove(d->report, report, 8); /* idle retransmits may reference this buffer */
    memset(&d->request, 0, sizeof(d->request));
    d->request.aio_lio_opcode = IOCB_CMD_PWRITE;
    d->request.aio_fildes = (uint32_t)d->ep1;
    d->request.aio_buf = (uint64_t)(uintptr_t)d->report;
    d->request.aio_nbytes = 8;
    d->request.aio_flags = IOCB_FLAG_RESFD;
    d->request.aio_resfd = (uint32_t)d->event;
    struct iocb *requests[] = {&d->request};
    if (syscall(SYS_io_submit, d->aio, 1L, requests) != 1) { perror("io_submit"); return -1; }
    d->pending = true;
    uint64_t deadline = now_ms() + IO_TIMEOUT_MS;
    while (d->pending) {
        uint64_t now = now_ms();
        if (now >= deadline) { fprintf(stderr, "USB IN transfer timed out\n"); return -1; }
        if (service(d, (int)(deadline - now))) return -1;
    }
    return 0;
}

static int hold(struct device *d, unsigned duration)
{
    const uint64_t deadline = now_ms() + duration;
    for (;;) {
        uint64_t now = now_ms(), next = deadline;
        if (interrupted || d->failed) return -1;
        if (now >= deadline) return 0;
        if (d->idle) {
            uint64_t idle_at = d->last_report + (unsigned)d->idle * 4;
            if (now >= idle_at) {
                if (send_report(d, d->report)) return -1;
                continue;
            }
            if (idle_at < next) next = idle_at;
        }
        if (service(d, (int)(next - now))) return -1;
    }
}

static int play_step(void *ctx, const uint8_t report[8], unsigned duration, bool pad)
{
    struct device *d = ctx;
    if (pad) fprintf(stderr, "TODO: pad-A (%u ms; keyboard released)\n", duration);
    if (send_report(d, report)) return -1;
    return hold(d, duration);
}

static int trace_step(void *ctx, const uint8_t report[8], unsigned duration, bool pad)
{
    (void)ctx;
    if (interrupted) return -1;
    if (pad) fprintf(stderr, "TODO: pad-A (%u ms; keyboard released)\n", duration);
    printf("{\"kind\":\"%s\",\"ms\":%u,\"report\":\"", pad ? "pad-A" : "report", duration);
    for (unsigned i = 0; i < 8; ++i) printf("%02x", report[i]);
    puts("\"}");
    return ferror(stdout) ? -1 : 0;
}

static int configure(struct device *d)
{
    char current[128];
    d->lock = open(LOCK_PATH, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (d->lock < 0 || flock(d->lock, LOCK_EX | LOCK_NB)) {
        perror("keyboard lock (root required; is another typer running?)"); return -1;
    }
    d->udc = open(GADGET_UDC, O_RDWR | O_CLOEXEC);
    if (d->udc < 0) { perror("open UDC (run scripts/hid-gadget-up.sh first)"); return -1; }
    ssize_t n = read(d->udc, current, sizeof(current));
    if (n < 0) { perror("read UDC"); return -1; }
    for (ssize_t i = 0; i < n; ++i)
        if (current[i] != '\n' && current[i] != ' ' && current[i] != '\t') {
            fprintf(stderr, "gadget is already bound; refusing to take it over\n"); return -1;
        }
    d->ep0 = open(FFS_DIR "/ep0", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (d->ep0 < 0) { perror("open ep0"); return -1; }
    if (exact_write(d->ep0, ffs_descriptors, sizeof(ffs_descriptors)) ||
        exact_write(d->ep0, ffs_strings, sizeof(ffs_strings))) {
        perror("write FunctionFS descriptors/strings"); return -1;
    }
    d->ep1 = open(FFS_DIR "/ep1", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (d->ep1 < 0) { perror("open ep1"); return -1; }
    d->event = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (d->event < 0 || syscall(SYS_io_setup, 1U, &d->aio)) {
        perror("initialize Linux native AIO (CONFIG_AIO required)"); return -1;
    }
    if (interrupted) return -1;
    if (lseek(d->udc, 0, SEEK_SET) < 0 || exact_write(d->udc, UDC_NAME, sizeof(UDC_NAME) - 1)) {
        perror("bind UDC"); return -1;
    }
    d->bound = true;
    fprintf(stderr, "Bound %s; waiting up to 30 s for a PC host\n", UDC_NAME);
    uint64_t deadline = now_ms() + ENUM_TIMEOUT_MS;
    while (!d->enabled) {
        uint64_t now = now_ms();
        if (now >= deadline) { fprintf(stderr, "enumeration timed out\n"); return -1; }
        if (service(d, (int)(deadline - now))) return -1;
    }
    d->started = true;
    return 0;
}

static int cleanup(struct device *d)
{
    int result = 0;
    /* Unbind first: cancels outstanding USB requests even on a stalled host. */
    if (d->bound) {
        if (lseek(d->udc, 0, SEEK_SET) < 0 || exact_write(d->udc, "\n", 1)) {
            perror("unbind UDC; run hid-gadget-down.sh"); result = -1;
        }
    }
    if (d->aio && syscall(SYS_io_destroy, d->aio)) { perror("io_destroy"); result = -1; }
    if (d->ep1 >= 0) close(d->ep1);
    if (d->ep0 >= 0) close(d->ep0);
    if (d->event >= 0) close(d->event);
    if (d->udc >= 0) close(d->udc);
    if (d->lock >= 0) close(d->lock); /* lock inode intentionally persists */
    return result;
}

static void usage(FILE *out)
{
    fputs("Usage: hidkbd_ffs --payload FILE [--cps N] [--press-ms N]\n"
          "       [--start-delay-ms N] [--newline enter|cr|none]\n"
          "       [--mode pico|text] [--dry-run]\n"
          "Default: Pico batch sequence; pad-A is logged, never sent.\n"
          "--mode text emits plain text for the PC smoke test.\n"
          "--cps 1..500 sets a press+release period (ceil(1000/N) ms);\n"
          "  period must exceed --press-ms. Enter/cr are aliases for HID Enter.\n"
          "--dry-run prints JSONL steps without USB, root, or real-time delays.\n", out);
}

static int number(const char *s, unsigned max, unsigned *out)
{
    if (!*s) return -1;
    for (const char *p = s; *p; ++p) if (*p < '0' || *p > '9') return -1;
    errno = 0;
    unsigned long n = strtoul(s, NULL, 10);
    if (errno || n > max) return -1;
    *out = (unsigned)n;
    return 0;
}

static int parse(int argc, char **argv, struct options *o)
{
    static const struct option opts[] = {
        {"payload", required_argument, NULL, 'p'}, {"cps", required_argument, NULL, 'c'},
        {"press-ms", required_argument, NULL, 'k'}, {"start-delay-ms", required_argument, NULL, 's'},
        {"newline", required_argument, NULL, 'n'}, {"mode", required_argument, NULL, 'm'},
        {"dry-run", no_argument, NULL, 'd'}, {"help", no_argument, NULL, 'h'}, {NULL, 0, NULL, 0}
    };
    unsigned cps = 0;
    int c;
    *o = (struct options){.press_ms = KEY_HOLD_MS, .gap_ms = KEY_GAP_MS,
                          .start_ms = MACRO_START_DELAY_MS, .newline = true};
    while ((c = getopt_long(argc, argv, "", opts, NULL)) != -1) {
        switch (c) {
        case 'p': o->payload = optarg; break;
        case 'c': if (number(optarg, 500, &cps) || !cps) return -1; o->paced = true; break;
        case 'k': if (number(optarg, 60000, &o->press_ms) || !o->press_ms) return -1; break;
        case 's': if (number(optarg, 3600000, &o->start_ms)) return -1; break;
        case 'n':
            if (!strcmp(optarg, "none")) o->newline = false;
            else if (!strcmp(optarg, "enter") || !strcmp(optarg, "cr")) o->newline = true;
            else return -1;
            break;
        case 'm':
            if (!strcmp(optarg, "text")) o->plain = true;
            else if (!strcmp(optarg, "pico")) o->plain = false;
            else return -1;
            break;
        case 'd': o->dry_run = true; break;
        case 'h': usage(stdout); exit(0);
        default: return -1;
        }
    }
    if (optind != argc || !o->payload) return -1;
    if (cps) {
        unsigned period = (1000 + cps - 1) / cps;
        if (period <= o->press_ms) {
            fputs("--cps period must exceed --press-ms by at least 1 ms\n", stderr); return -1;
        }
        o->gap_ms = period - o->press_ms;
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct options options;
    if (parse(argc, argv, &options)) { usage(stderr); return 2; }
    FILE *payload = fopen(options.payload, "rb");
    if (!payload) { perror("open payload"); return 1; }
    /* Read before binding: file errors can never cause a partially typed file. */
    uint8_t *data = malloc(MAX_PAYLOAD + 1U);
    if (!data) { perror("allocate payload"); fclose(payload); return 1; }
    size_t size = fread(data, 1, MAX_PAYLOAD + 1U, payload);
    int failed = ferror(payload);
    if (fclose(payload)) failed = 1;
    if (failed || size > MAX_PAYLOAD) {
        fprintf(stderr, "payload read failed or exceeds 16 MiB limit\n"); free(data); return 1;
    }
    struct sigaction action = {.sa_handler = on_signal};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL) ||
        sigaction(SIGHUP, &action, NULL)) { perror("sigaction"); free(data); return 1; }
    int result;
    if (options.dry_run) {
        result = type_payload(data, size, &options, trace_step, NULL);
        if (fflush(stdout)) result = -1;
    } else {
        struct device d = {.ep0 = -1, .ep1 = -1, .event = -1, .udc = -1, .lock = -1, .protocol = 1};
        result = configure(&d);
        if (!result) result = type_payload(data, size, &options, play_step, &d);
        if (cleanup(&d)) result = -1;
        if (!result) fputs("Typing complete; all keys released and UDC unbound\n", stderr);
    }
    free(data);
    if (interrupted) return 128 + interrupted;
    return result ? 1 : 0;
}
