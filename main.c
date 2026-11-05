#include "protocol.h"

#define FLAG 0x7E
#define MAX_BITS 4096

/* байты заполняются от старшего бита к младшему, чтение и запись должны совпадать */
static int put_bit(FILE *stream, int bit, unsigned char *cur, int *nbits) {
    *cur = (unsigned char)((*cur << 1) | (bit & 1));
    (*nbits)++;
    if (*nbits == 8) {
        if (putc(*cur, stream) == EOF) {
            return -1;
        }
        *cur = 0;
        *nbits = 0;
    }
    return 0;
}

static int get_bit(FILE *stream, unsigned char *cur, int *nbits) {
    int c;
    if (*nbits == 0) {
        c = getc(stream);
        if (c == EOF) {
            return -1;
        }
        *cur = (unsigned char)c;
        *nbits = 8;
    }
    (*nbits)--;
    return (*cur >> *nbits) & 1;
}

ssize_t write_message(FILE* stream, const void *buf, size_t nbyte) {
    const unsigned char *msg = (const unsigned char *)buf;
    unsigned char cur = 0;
    int nbits = 0;
    int ones = 0;
    size_t i;
    int j;
    int bit;

    /* маркеры идут без стаффинга, иначе они перестанут быть маркерами */
    for (j = 7; j >= 0; j--) {
        if (put_bit(stream, (FLAG >> j) & 1, &cur, &nbits) < 0) {
            fprintf(stderr, "write_message: write error\n");
            return -1;
        }
    }

    for (i = 0; i < nbyte; i++) {
        for (j = 7; j >= 0; j--) {
            bit = (msg[i] >> j) & 1;
            if (put_bit(stream, bit, &cur, &nbits) < 0) {
                fprintf(stderr, "write_message: write error\n");
                return -1;
            }
            if (bit == 1) {
                ones++;
            } else {
                ones = 0;
            }
            if (ones == 5) {
                if (put_bit(stream, 0, &cur, &nbits) < 0) {
                    fprintf(stderr, "write_message: write error\n");
                    return -1;
                }
                ones = 0;
            }
        }
    }

    for (j = 7; j >= 0; j--) {
        if (put_bit(stream, (FLAG >> j) & 1, &cur, &nbits) < 0) {
            fprintf(stderr, "write_message: write error\n");
            return -1;
        }
    }

    while (nbits != 0) {
        if (put_bit(stream, 1, &cur, &nbits) < 0) {
            fprintf(stderr, "write_message: write error\n");
            return -1;
        }
    }

    return (ssize_t)nbyte;
}

ssize_t read_message(FILE *stream, void *buf) {
    unsigned char *out = (unsigned char *)buf;
    unsigned char bits[MAX_BITS];
    unsigned char cur = 0;
    int nbits = 0;
    unsigned int win = 0;
    int bit;
    int ones = 0;
    size_t n = 0;
    size_t len = 0;
    size_t i;

    /* окно из последних 8 бит, всё что до стартового маркера отбрасывается само */
    for (;;) {
        bit = get_bit(stream, &cur, &nbits);
        if (bit < 0) {
            fprintf(stderr, "read_message: start marker not found\n");
            return -1;
        }
        win = ((win << 1) | (unsigned int)bit) & 0xFF;
        if (win == FLAG) {
            break;
        }
    }

    win = 0;
    for (;;) {
        bit = get_bit(stream, &cur, &nbits);
        if (bit < 0) {
            fprintf(stderr, "read_message: end marker not found\n");
            return -1;
        }
        if (n == MAX_BITS) {
            fprintf(stderr, "read_message: message is too long\n");
            return -1;
        }
        bits[n++] = (unsigned char)bit;
        win = ((win << 1) | (unsigned int)bit) & 0xFF;
        if (n >= 8 && win == FLAG) {
            n -= 8; /* последние 8 бит это сам маркер, они не данные */
            break;
        }
    }

    ones = 0;
    for (i = 0; i < n; i++) {
        if (ones == 5) {
            if (bits[i] != 0) {
                fprintf(stderr, "read_message: bad bit sequence\n");
                return -1;
            }
            ones = 0;
            continue;
        }
        bits[len++] = bits[i];
        if (bits[i] == 1) {
            ones++;
        } else {
            ones = 0;
        }
    }

    if (len % 8 != 0) {
        fprintf(stderr, "read_message: payload is not a whole number of bytes\n");
        return -1;
    }
    if (len / 8 > MAX_MESSAGE_LEN) {
        fprintf(stderr, "read_message: message is too long\n");
        return -1;
    }

    for (i = 0; i < len; i++) {
        if (i % 8 == 0) {
            out[i / 8] = 0;
        }
        out[i / 8] = (unsigned char)((out[i / 8] << 1) | bits[i]);
    }

    return (ssize_t)(len / 8);
}