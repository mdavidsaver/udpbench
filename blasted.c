#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <memory.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <unistd.h>
#include <signal.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define OOPS(MSG, ...) do{ \
    int captured_err = errno; \
    fprintf(stderr, "at %d : " MSG " : (%d) %s\n", __LINE__, ##__VA_ARGS__, captured_err, strerror(captured_err)); \
    exit(1); \
} while(0)

static int interruptor = -1;

static
void interrupted(int signo)
{
    if(interruptor!=-1)
        close(interruptor);
    interruptor = -1;
}

int main(int argc, char *argv[])
{
    if(argc<2)
        return 1;

    struct sockaddr_in bound;
    memset(&bound, 0, sizeof(bound));
    bound.sin_family = AF_INET;

    {
        char *scratch = strdup(argv[1]);

        char *sep = strchr(scratch, ':');
        if(!sep) {
            bound.sin_port = htons(1234);
        } else {
            *sep++ = '\0';
            bound.sin_port = htons(atoi(sep));
        }

        fprintf(stderr, "listening %s:%d\n",
                scratch, ntohs(bound.sin_port));

        if(!inet_aton(scratch, &bound.sin_addr))
            OOPS("Not an address \"%s\"", scratch);

        free(scratch);
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock<0)
        OOPS("socket");

    if(bind(sock, (const struct sockaddr*)&bound, sizeof(bound)))
        OOPS("bind()");

    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = &interrupted;

        if(sigaction(SIGINT, &sa, NULL))
            OOPS("SIGINT");
        if(sigaction(SIGTERM, &sa, NULL))
            OOPS("SIGTERM");
    }

    uint32_t expect = 0;
    size_t nbytes = 0;
    size_t npkt = 0;
    size_t nskips = 0;
    struct timespec first, last;

    while(1) {
        uint32_t buffer[0x10000/4u];
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);

        ssize_t ret = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&src, &slen);
        if(ret<0)
            break;

        if(clock_gettime(CLOCK_MONOTONIC, &last))
            OOPS("CLOCK_MONOTONIC");

        if(npkt==0)
            first = last;

        for(size_t i=0, N=ret/4u; i<N; i++) {
            uint32_t actual = ntohl(buffer[i]);
            if(actual==expect) {
                expect++;
            } else {
                expect = actual+1;
                nskips++;
                //putchar('!');
                printf("%u != %u\n", (unsigned)actual, (unsigned)expect);
            }
        }

        npkt++;
        nbytes += ret;
    }

    double t0 = first.tv_sec + first.tv_nsec*1e-9;
    double t1 = last.tv_sec + last.tv_nsec*1e-9;
    double tD = t1-t0;

    double rate = nbytes / tD; // bytes / sec

    printf("\n"
           "recv'd %zu bytes in %zu pkts\n"
           "#skips %zu\n"
           "in %.3f sec\n"
           "  %.3f Mb/s\n",
           nbytes, npkt,
           nskips,
           tD,
           rate / 1048576.0 * 8.0);

    return 0;
}
