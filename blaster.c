#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <memory.h>
#include <string.h>
#include <errno.h>
#include <time.h>

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

static
void usage(const char* argv0)
{
    fprintf(stderr, "%s [-S] [-m <MTU>] [-b <BATCHSIZE>] [-t <totalMB>] <IP[:port#]>\n"
            "\n"
            "  -S  - use UDP_SEGMENT\n"
            "  -m <MTU> - set UDP payload size.  Default 1450\n"
            "  -b <BATCHSIZE> - Number of packets to push to kernel at once.  Default 1\n"
            "  -t <totalMB> - Total size to send, in MB.  Default 16MB\n"
            "  <IP[:port#[> - Destination endpoint address\n",
            argv0);
}

int main(int argc, char *argv[])
{
    unsigned mtu = 1450;
    size_t nbatch = 1u;
    size_t nbytes = 1024*1024*16;
    unsigned char use_segment = 0;
    {
        int ret = 0;
        int opt;
        while( (opt=getopt(argc, argv, "hm:b:t:S"))!= -1) {
            switch(opt) {
            case 'm': {
                int val = atoi(optarg);
                if(val<1400)
                    return 1;
                mtu = val;
            }
                break;
            case 'b': {
                int val = atoi(optarg);
                if(val<1)
                    return 1;
                nbatch = val;
            }
                break;
            case 't': {
                int val = atoi(optarg);
                if(val<1)
                    return 1;
                nbytes = 1024*1024*(size_t)val;
            }
                break;
            case 'S':
                use_segment = 1;
                break;
            default:
                fprintf(stderr, "Unexpected argument: -%c", opt);
                ret = 1;
                // fall through
            case 'h':
                usage(argv[0]);
                return ret;
            }
        }
    }

    if(use_segment && nbatch>64) {
        usage(argv[0]);
        fprintf(stderr, "-S # must be less than 65\n");
        return 1;
    }

    if(optind+1!=argc) {
        usage(argv[0]);
        return 1;
    }
    const char *target_name = argv[optind];

    // round MTU to x4
    mtu--;
    mtu |= 0x3;
    mtu++;

    fprintf(stderr,
            "UDP_SEGMENT: %c\n"
            "MTU: %u\n"
            "#batch: %zu\n"
            "#nbytes: %zu\n",
            use_segment ? 'Y': 'N', mtu, nbatch, nbytes);

    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));

    {
        char *scratch = strdup(target_name);
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

    size_t counter = 0;
    size_t stop = nbytes/4u;

    uint32_t *buffer = malloc(mtu*nbatch);

    struct mmsghdr *mhdrs = calloc(nbatch, sizeof(*mhdrs));
    struct iovec *iovecs = calloc(nbatch, sizeof(*iovecs));

    if(!buffer || !mhdrs || !iovecs)
        OOPS("malloc");

    unsigned orig_mtu = mtu;
    if(use_segment) {
        // UDP_SEGMENT delegates packet segmentation towards hardware (OS or maybe HW)
        mtu *= nbatch;
        nbatch = 1;
    }

    // prepare buffers
    for(size_t n=0; n<nbatch; n++) {
        iovecs[n].iov_len = mtu;
        iovecs[n].iov_base = &buffer[(mtu/4)*n];

        struct msghdr *hdr = &mhdrs[n].msg_hdr;
        hdr->msg_name = &target;
        hdr->msg_namelen = sizeof(target);
        hdr->msg_iov = &iovecs[n];
        hdr->msg_iovlen = 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock<0)
        OOPS("socket");

    {
        int buflen;
        socklen_t buflenlen = sizeof(buflen);
        if(getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buflen, &buflenlen))
            OOPS("get SO_SNDBUF 1");

        if(buflen < mtu*nbatch) {
            buflen = mtu*nbatch;
            if(setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buflen, sizeof(buflen))) {
                int err = errno;
                fprintf(stderr, "Unable to set SO_SNDBUF=%d : (%d) %s\n",
                        buflen, err, strerror(err));
            }
        }

        buflenlen = sizeof(buflen);
        if(getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buflen, &buflenlen))
            OOPS("get SO_SNDBUF 2");

        fprintf(stderr, "SO_SNDBUF: %d\n", buflen);
    }

    if(use_segment) {
        int val = orig_mtu;
        if(setsockopt(sock, SOL_UDP, UDP_SEGMENT, &val, sizeof(val)))
            OOPS("UDP_SEGMENT");
    }

    {
        struct sockaddr_in bound;
        memset(&bound, 0, sizeof(bound));
        bound.sin_family = AF_INET;

        if(bind(sock, (const struct sockaddr*)&bound, sizeof(bound)))
            OOPS("bind()");
    }

    struct timespec tstart, tend;

    (void)clock_gettime(CLOCK_MONOTONIC, &tstart);

    while(counter<stop) {

        for(size_t i=0, N=mtu*nbatch/4u; i<N; i++) {
            buffer[i] = htonl(counter++);
        }

        if(sendmmsg(sock, mhdrs, nbatch, 0)!=nbatch)
            OOPS("sendmmsg");
    }

    (void)clock_gettime(CLOCK_MONOTONIC, &tend);

    double t0 = tstart.tv_sec + tstart.tv_nsec*1e-9;
    double t1 = tend.tv_sec + tend.tv_nsec*1e-9;
    double tD = t1-t0;

    double rate = nbytes / tD; // bytes / sec

    printf("\n"
           "sent %zu bytes in %zu pkts\n"
           "in %.3f sec (time to queue, not to send!)\n"
           "  %.3f Mb/s\n",
           nbytes, nbytes/orig_mtu,
           tD,
           rate / 1048576.0 * 8.0);

    return 0;
}
