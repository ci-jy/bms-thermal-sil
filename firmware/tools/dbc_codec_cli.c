/* Line-oriented front end to the C CAN codec, used by the SIL suite to
 * cross-check pack/unpack against cantools.
 *
 *   decode <id-hex> <16 hex chars>     -> "<name>=<raw> ..." (sign-extended raw)
 *   encode <id-hex> <name>=<raw> ...   -> 16 hex chars
 *   phys   <id-hex> <name>=<value> ... -> 16 hex chars (physical values, rounded)
 *   e2e    <id-hex> <16 hex chars>     -> data with alive/CRC recomputed, counter kept
 */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "can_codec.h"
#include "e2e.h"

static const can_signal_t *find_signal(const can_message_t *m, const char *name)
{
    for (size_t i = 0u; i < m->n_signals; i++) {
        if (strcmp(m->signals[i].name, name) == 0) {
            return &m->signals[i];
        }
    }
    return NULL;
}

static void parse_hex(const char *s, uint8_t d[8])
{
    for (int i = 0; i < 8; i++) {
        unsigned v = 0u;
        (void)sscanf(s + 2 * i, "%2x", &v);
        d[i] = (uint8_t)v;
    }
}

static void print_hex(const uint8_t d[8])
{
    for (int i = 0; i < 8; i++) {
        printf("%02x", d[i]);
    }
    printf("\n");
}

int main(void)
{
    char line[1024];
    while (fgets(line, sizeof(line), stdin) != NULL) {
        char *save = NULL;
        const char *cmd = strtok_r(line, " \n", &save);
        const char *id_s = strtok_r(NULL, " \n", &save);
        if (cmd == NULL || id_s == NULL) {
            continue;
        }
        const can_message_t *m = can_find_message((uint32_t)strtoul(id_s, NULL, 16));
        if (m == NULL) {
            printf("ERR unknown id\n");
            fflush(stdout);
            continue;
        }
        uint8_t d[8] = {0};
        if (strcmp(cmd, "decode") == 0 || strcmp(cmd, "e2e") == 0) {
            const char *hex = strtok_r(NULL, " \n", &save);
            parse_hex(hex ? hex : "0000000000000000", d);
            if (strcmp(cmd, "e2e") == 0) {
                e2e_protect(m->id, d, (uint8_t)(d[E2E_ALIVE_BYTE] & 0x0Fu));
                print_hex(d);
            } else {
                for (size_t i = 0u; i < m->n_signals; i++) {
                    printf("%s%s=%" PRId64, i ? " " : "", m->signals[i].name, can_signal_raw(&m->signals[i], d));
                }
                printf("\n");
            }
        } else {
            const int phys = strcmp(cmd, "phys") == 0;
            char *tok;
            while ((tok = strtok_r(NULL, " \n", &save)) != NULL) {
                char *eq = strchr(tok, '=');
                if (eq == NULL) {
                    continue;
                }
                *eq = '\0';
                const can_signal_t *s = find_signal(m, tok);
                if (s == NULL) {
                    continue;
                }
                if (phys) {
                    can_signal_set(s, d, strtof(eq + 1, NULL));
                } else {
                    can_signal_set_raw(s, d, strtoll(eq + 1, NULL, 10));
                }
            }
            print_hex(d);
        }
        fflush(stdout);
    }
    return 0;
}
