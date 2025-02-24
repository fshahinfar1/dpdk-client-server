#pragma once
#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

struct entry {
    int prefixlen;
    int data;
};
typedef struct entry entry_t;

/* 
 * @out, a pointer to another pointer, it will be set to the memory allocated
 * for the rules.
 * @returns number of the rules
 * */
int load_routing_dataset(entry_t **out)
{
    // TODO: this is hardcoded and might brake if the user invoke the program
    // from somewhere else
    const char * file_path = "./dataset/ipv4.txt";
    FILE *f = fopen(file_path, "r");
    char buf[256];
    size_t sz = 0;
    size_t num_entries = 0;
    while ((sz = fread(&buf, 1, 1, f)) != 0) {
        for (size_t i = 0; i < sz; i++) {
            if (buf[i] == '\n')
                num_entries++;
        }
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "failed to seek to the begining of the file\n");
        return -1;
    }

    size_t key_o = 0;
    char ipv4[16]; size_t ipv4_o = 0;
    char pref[8]; size_t pref_o = 0;
    enum {
        READING_IP,
        READING_PREFIX,
    }state = 0;
    entry_t *keys = calloc(num_entries, sizeof(entry_t));
    assert(keys != NULL);
    while ((sz = fread(&buf, 1, 1, f)) > 0) {
        for (size_t i = 0; i < sz; i++) {
            char c = buf[i];
            if (c == '\0')
                break;
            switch (state) {
                case READING_IP:
                    if ((c >= '0' && c <= '9') || (c == '.')) {
                        ipv4[ipv4_o++] = c;
                        assert(ipv4_o < 16);
                    } else if (c == '/') {
                        state = READING_PREFIX;
                        ipv4[ipv4_o++] = '\0';
                        assert(ipv4_o < 16);
                        /* printf("ip: %s\n", ipv4); */
                    } else {
                        printf("READING_IP && saw: %c key-index: %lu\n", c, key_o);
                        assert (0);
                    }
                    break;
                case READING_PREFIX:
                    if (c >= '0' && c <= '9') {
                        pref[pref_o++] = c;
                    } else if (c == '\n') {
                        state = READING_IP;
                        pref[pref_o++] = '\0';
                        // we should have key now
                        entry_t *k = &keys[key_o++];
                        k->prefixlen = atoi(pref);
                        inet_pton(AF_INET, ipv4, &k->data);

                        pref_o = 0;
                        ipv4_o = 0;
                    } else {
                        assert (0);
                    }
                    break;
                default:
                    assert (0);
                    break;
            }
        }
    }

    *out = keys;
    return num_entries;
}
