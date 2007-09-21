/*
 * Copyright (c) 2006-2007 Douglas Gilbert.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "smp_lib.h"

/* This is a Serial Attached SCSI (SAS) management protocol (SMP) utility
 * program.
 *
 * This utility issues a REPORT GENERAL function and outputs its response.
 */

static char * version_str = "1.13 20070920";    /* sas2r11 */


static struct option long_options[] = {
        {"change_report", 0, 0, 'c'},
        {"help", 0, 0, 'h'},
        {"hex", 0, 0, 'H'},
        {"interface", 1, 0, 'I'},
        {"phy", 1, 0, 'p'},
        {"raw", 0, 0, 'r'},
        {"sa", 1, 0, 's'},
        {"verbose", 0, 0, 'v'},
        {"version", 0, 0, 'V'},
        {0, 0, 0, 0},
};

static void usage()
{
    fprintf(stderr, "Usage: "
          "smp_rep_general [--change_report] [--help] [--hex]\n"
          "                       [--interface=PARAMS] [--raw] "
          "[--sa=SAS_ADDR]\n"
          "                       [--verbose] [--version] "
          "SMP_DEVICE[,N]\n"
          "  where:\n"
          "    --change_report|-c   report expander change count "
          "only\n"
          "    --help|-h            print out usage message\n"
          "    --hex|-H             print response in hexadecimal\n"
          "    --interface=PARAMS|-I PARAMS    specify or override "
          "interface\n"
          "    --raw|-r             output response in binary\n"
          "    --sa=SAS_ADDR|-s SAS_ADDR    SAS address of SMP "
          "target (use leading '0x'\n"
          "                         or trailing 'h'). Depending on "
          "the interface, may\n"
          "                         not be needed\n"
          "    --verbose|-v         increase verbosity\n"
          "    --version|-V         print version string and exit\n\n"
          "Performs a SMP REPORT GENERAL function\n"
          );

}

static void dStrRaw(const char* str, int len)
{
    int k;

    for (k = 0 ; k < len; ++k)
        printf("%c", str[k]);
}

int main(int argc, char * argv[])
{
    int res, c, k, len;
    int do_change = 0;
    int do_hex = 0;
    int phy_id = 0;
    int do_raw = 0;
    int verbose = 0;
    long long sa_ll;
    unsigned long long sa = 0;
    char i_params[256];
    char device_name[512];
    char b[256];
    unsigned char smp_req[] = {SMP_FRAME_TYPE_REQ, SMP_FN_REPORT_GENERAL,
                               0, 0, 0, 0, 0, 0};
    unsigned char smp_resp[128];
    struct smp_target_obj tobj;
    struct smp_req_resp smp_rr;
    int subvalue = 0;
    char * cp;
    int ret = 0;

    memset(device_name, 0, sizeof device_name);
    memset(i_params, 0, sizeof i_params);
    while (1) {
        int option_index = 0;

        c = getopt_long(argc, argv, "chHI:p:rs:vV", long_options,
                        &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'c':
            ++do_change;
            break;
        case 'h':
        case '?':
            usage();
            return 0;
        case 'H':
            ++do_hex;
            break;
        case 'I':
            strncpy(i_params, optarg, sizeof(i_params));
            i_params[sizeof(i_params) - 1] = '\0';
            break;
        case 'p':
           phy_id = smp_get_num(optarg);
           if ((phy_id < 0) || (phy_id > 127)) {
                fprintf(stderr, "bad argument to '--phy'\n");
                return SMP_LIB_SYNTAX_ERROR;
            }
            if (verbose)
                fprintf(stderr, "'--phy=<n>' option not needed so "
                        "ignored\n");
            break;
        case 'r':
            ++do_raw;
            break;
        case 's':
           sa_ll = smp_get_llnum(optarg);
           if (-1LL == sa_ll) {
                fprintf(stderr, "bad argument to '--sa'\n");
                return SMP_LIB_SYNTAX_ERROR;
            }
            sa = (unsigned long long)sa_ll;
            break;
        case 'v':
            ++verbose;
            break;
        case 'V':
            fprintf(stderr, "version: %s\n", version_str);
            return 0;
        default:
            fprintf(stderr, "unrecognised switch code 0x%x ??\n", c);
            usage();
            return SMP_LIB_SYNTAX_ERROR;
        }
    }
    if (optind < argc) {
        if ('\0' == device_name[0]) {
            strncpy(device_name, argv[optind], sizeof(device_name) - 1);
            device_name[sizeof(device_name) - 1] = '\0';
            ++optind;
        }
        if (optind < argc) {
            for (; optind < argc; ++optind)
                fprintf(stderr, "Unexpected extra argument: %s\n",
                        argv[optind]);
            usage();
            return SMP_LIB_SYNTAX_ERROR;
        }
    }
    if (0 == device_name[0]) {
        cp = getenv("SMP_UTILS_DEVICE");
        if (cp)
            strncpy(device_name, cp, sizeof(device_name) - 1);
        else {
            fprintf(stderr, "missing device name!\n    [Could use "
                    "environment variable SMP_UTILS_DEVICE instead]\n");
            usage();
            return SMP_LIB_SYNTAX_ERROR;
        }
    }
    if ((cp = strchr(device_name, ','))) {
        *cp = '\0';
        if (1 != sscanf(cp + 1, "%d", &subvalue)) {
            fprintf(stderr, "expected number after comma in SMP_DEVICE "
                    "name\n");
            return SMP_LIB_SYNTAX_ERROR;
        }
    }
    if (0 == sa) {
        cp = getenv("SMP_UTILS_SAS_ADDR");
        if (cp) {
           sa_ll = smp_get_llnum(cp);
           if (-1LL == sa_ll) {
                fprintf(stderr, "bad value in environment variable "
                        "SMP_UTILS_SAS_ADDR\n");
                fprintf(stderr, "    use 0\n");
                sa_ll = 0;
            }
            sa = (unsigned long long)sa_ll;
        }
    }
    if (sa > 0) {
        if (! smp_is_naa5(sa)) {
            fprintf(stderr, "SAS (target) address not in naa-5 format "
                    "(may need leading '0x')\n");
            if ('\0' == i_params[0]) {
                fprintf(stderr, "    use '--interface=' to override\n");
                return SMP_LIB_SYNTAX_ERROR;
            }
        }
    }

    res = smp_initiator_open(device_name, subvalue, i_params, sa,
                             &tobj, verbose);
    if (res < 0)
        return SMP_LIB_FILE_ERROR;

    if (verbose) {
        fprintf(stderr, "    Report general request: ");
        for (k = 0; k < (int)sizeof(smp_req); ++k)
            fprintf(stderr, "%02x ", smp_req[k]);
        fprintf(stderr, "\n");
    }
    memset(&smp_rr, 0, sizeof(smp_rr));
    smp_rr.request_len = sizeof(smp_req);
    smp_rr.request = smp_req;
    smp_rr.max_response_len = sizeof(smp_resp);
    smp_rr.response = smp_resp;
    res = smp_send_req(&tobj, &smp_rr, verbose);

    if (res) {
        fprintf(stderr, "smp_send_req failed, res=%d\n", res);
        if (0 == verbose)
            fprintf(stderr, "    try adding '-v' option for more debug\n");
        ret = -1;
        goto err_out;
    }
    if (smp_rr.transport_err) {
        fprintf(stderr, "smp_send_req transport_error=%d\n",
                smp_rr.transport_err);
        ret = -1;
        goto err_out;
    }
    if ((smp_rr.act_response_len >= 0) && (smp_rr.act_response_len < 4)) {
        fprintf(stderr, "response too short, len=%d\n",
                smp_rr.act_response_len);
        ret = SMP_LIB_CAT_MALFORMED;
        goto err_out;
    }
    len = smp_resp[3];
    if (0 == len) {
        len = smp_get_func_def_resp_len(smp_resp[1]);
        if (len < 0) {
            len = 0;
            if (verbose > 0)
                fprintf(stderr, "unable to determine response length\n");
        }
    }
    len = 4 + (len * 4);        /* length in bytes, excluding 4 byte CRC */
    if (do_hex || do_raw) {
        if (do_hex)
            dStrHex((const char *)smp_resp, len, 1);
        else
            dStrRaw((const char *)smp_resp, len);
        if (SMP_FRAME_TYPE_RESP != smp_resp[0])
            ret = SMP_LIB_CAT_MALFORMED;
        if (smp_resp[1] != smp_req[1])
            ret = SMP_LIB_CAT_MALFORMED;
        if (smp_resp[2])
            ret = smp_resp[2];
        goto err_out;
    }
    if (SMP_FRAME_TYPE_RESP != smp_resp[0]) {
        fprintf(stderr, "expected SMP frame response type, got=0x%x\n",
                smp_resp[0]);
        ret = SMP_LIB_CAT_MALFORMED;
        goto err_out;
    }
    if (smp_resp[1] != smp_req[1]) {
        fprintf(stderr, "Expected function code=0x%x, got=0x%x\n",
                smp_req[1], smp_resp[1]);
        ret = SMP_LIB_CAT_MALFORMED;
        goto err_out;

    }
    if (smp_resp[2]) {
        cp = smp_get_func_res_str(smp_resp[2], sizeof(b), b);
        fprintf(stderr, "Report general result: %s\n", cp);
        ret = smp_resp[2];
        goto err_out;
    }
    if (do_change) {
        printf("%d\n", (smp_resp[4] << 8) + smp_resp[5]);
        goto err_out;
    }
    printf("Report general response:\n");
    printf("  expander change count: %d\n",
           (smp_resp[4] << 8) + smp_resp[5]);
    printf("  expander route indexes: %d\n",
           (smp_resp[6] << 8) + smp_resp[7]);
    printf("  number of phys: %d\n", smp_resp[9]);
    printf("  table to table supported: %d\n", !!(smp_resp[10] & 0x80));
    /* printf("  zone address resolved supported: %d\n",
              !!(smp_resp[10] & 0x8)); */
    printf("  configures others: %d\n", !!(smp_resp[10] & 0x4));
    printf("  configuring: %d\n", !!(smp_resp[10] & 0x2));
    printf("  externally configurable route table: %d\n",
           !!(smp_resp[10] & 0x1));
    if (smp_resp[12]) { /* assume naa-5 present */
        /* not in SAS-1; in SAS-1.1 and SAS-2 */
        printf("  enclosure logical identifier (hex): ");
        for (k = 0; k < 8; ++k)
            printf("%02x", smp_resp[12 + k]);
        printf("\n");
    }
    if ((0 == smp_resp[12]) && verbose)
        printf("  enclosure logical identifier <empty>\n");
    if (len < 36)
        goto err_out;
    printf("  STP bus inactivity timer: %d (unit: 100ms)\n",
           (smp_resp[30] << 8) + smp_resp[31]);
    printf("  STP maximum connect time: %d (unit: 100ms)\n",
           (smp_resp[32] << 8) + smp_resp[33]);
    printf("  STP SMP I_T nexus loss time: %d (unit: ms)\n",
           (smp_resp[34] << 8) + smp_resp[35]);
    if (len < 40)
        goto err_out;
    printf("  number of zone groups: %d (0->128, 1->256)\n",
           ((smp_resp[36] & 0xc0) >> 6));
    printf("  zone locked: %d\n", !!(smp_resp[36] & 0x10));
    printf("  physical presence supported: %d\n", !!(smp_resp[36] & 0x8));
    printf("  physical presence asserted: %d\n", !!(smp_resp[36] & 0x4));
    printf("  zoning supported: %d\n", !!(smp_resp[36] & 0x2));
    printf("  zoning enabled: %d\n", !!(smp_resp[36] & 0x1));
    printf("  maximum number of routed SAS addresses: %d\n",
           (smp_resp[38]  << 8) + smp_resp[39]);
    if (len < 48)
        goto err_out;
    printf("  active zone manager SAS address (hex): ");
    for (k = 0; k < 8; ++k)
        printf("%02x", smp_resp[40 + k]);
    printf("\n");
    if (len < 50)
        goto err_out;
    printf("  zone lock inactivity time limit: %d (unit: 100ms)\n",
           (smp_resp[48]  << 8) + smp_resp[49]);
    if (len < 56)
        goto err_out;
    printf("  first enclosure connector element index: %d\n", smp_resp[53]);
    printf("  number of enclosure connector element indexes: %d\n",
           smp_resp[54]);
    if (len < 60)
        goto err_out;
    printf("  reduced functionality: %d\n", !!(smp_resp[56] & 0x80));
    printf("  time to reduced functionality: %d (unit: 100ms)\n",
           smp_resp[57]);
    printf("  initial time to reduced functionality: %d (unit: 100ms)\n",
           smp_resp[58]);
    printf("  maximum reduced functionality time: %d (unit: second)\n",
           smp_resp[59]);
    if (len < 68)
        goto err_out;
    printf("  last self-configuarion status descriptor index: %d\n",
           (smp_resp[60] << 8) + smp_resp[61]);
    printf("  maximum number of stored self-configuarion status "
           "descriptors: %d\n", (smp_resp[62] << 8) + smp_resp[63]);
    printf("  last phy event information descriptor index: %d\n",
           (smp_resp[64] << 8) + smp_resp[65]);
    printf("  maximum number of stored phy event information "
           "descriptors: %d\n", (smp_resp[66] << 8) + smp_resp[67]);

err_out:
    res = smp_initiator_close(&tobj);
    if (res < 0) {
        if (0 == ret)
            return SMP_LIB_FILE_ERROR;
    }
    return (ret >= 0) ? ret : SMP_LIB_CAT_OTHER;
}
