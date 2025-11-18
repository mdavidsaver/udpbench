#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <memory.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define OOPS(MSG, ...) do{ \
int captured_err = errno; \
    fprintf(stderr, "at %d : " MSG " : (%d) %s\n", __LINE__, ##__VA_ARGS__, captured_err, strerror(captured_err)); \
    exit(1); \
} while(0)

#define STOPu8 1024*1024

// excluding IP+UDP headers
#define MTU 1400

// round up to x4
#define NUMu8 (((MTU-1)|0x3)+1)
#define NUMu32 (NUMu8/4u)

int main(int argc, char *argv[])
{
    if(argc<2)
        return 1;

    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));

    {
        char *scratch = strdup(argv[1]);
        if(!scratch)
            OOPS("strdup");

        char *sep = strchr(scratch, ':');
        if(!sep) {
            target.sin_port = htons(1234);
        } else {
            *sep++ = '\0';
            target.sin_port = htons(atoi(sep));
        }

        fprintf(stderr, "send to %s:%d\n",
                scratch, ntohs(target.sin_port));

        if(!inet_aton(scratch, &target.sin_addr))
            OOPS("Not an address \"%s\"", scratch);

        free(scratch);
    }

    uint32_t counter = 0;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock<0)
        OOPS("socket");

    {
        struct sockaddr_in bound;
        memset(&bound, 0, sizeof(bound));
        bound.sin_family = AF_INET;

        if(bind(sock, (const struct sockaddr*)&bound, sizeof(bound)))
            OOPS("bind()");
    }


    while(counter<(STOPu8/4)) {
        uint32_t buffer[NUMu32];

        for(size_t i=0; i<(NUMu32); i++) {
            buffer[i] = htonl(counter++);
        }

        if(sendto(sock,
                   buffer, sizeof(buffer),
                   0,
                   (const struct sockaddr*)&target, sizeof(target))
            !=sizeof(buffer))
            OOPS("sendto()");
    }

    return 0;
}
