/*
 * Copyright (c) 2006-2018, Douglas Gilbert
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "smp_lib.h"
#include "sg_unaligned.h"
#include "sg_pr2serr.h"

/* This is a Serial Attached SCSI (SAS) Serial Management Protocol (SMP)
 * utility.
 *
 * This utility issues a CONFIG GENERAL function and outputs its response.
 */

static const char * version_str = "1.15 20180724";    /* spl4r12 */

static struct option long_options[] = {
    {"connect", required_argument, 0, 'c'},
    {"expander", required_argument, 0, 'e'},
    {"expected", required_argument, 0, 'E'},
    {"help", no_argument, 0, 'h'},
    {"hex", no_argument, 0, 'H'},
    {"inactivity", required_argument, 0, 'i'},
    {"interface", required_argument, 0, 'I'},
    {"nexus", required_argument, 0, 'p'},
    {"open", required_argument, 0, 'o'},
    {"power", required_argument, 0, 'p'},
    {"raw", no_argument, 0, 'r'},
    {"reduced", required_argument, 0, 'R'},
    {"sa", required_argument, 0, 's'},
    {"ssp", required_argument, 0, 'S'},
    {"verbose", no_argument, 0, 'v'},
    {"version", no_argument, 0, 'V'},
    {0, 0, 0, 0},
};



static void
usage(void)
{
    pr2serr("Usage: smp_conf_general [--connect=CO] [--expander=ITDEFOI] "
            "[--expected=EX]\n"
            "                        [--help] [--hex] [--inactivity=IN]\n"
            "                        [--interface=PARAMS] [--nexus=NE] "
            "[--open=OP]\n"
            "                        [--power=PD] [--raw] [--reduced=RE]\n"
            "                        [--sa=SAS_ADDR] [--ssp=CTL] "
            "[--verbose]\n"
            "                        [--version] SMP_DEVICE[,N]\n"
            "  where:\n"
            "    --connect=CO|-c CO     STP maximum connect time limit "
            "(100 us)\n"
            "    --expander=ITDEFOI|-e ITDEFOI    initial time to delay "
            "expander\n"
            "                                     forward open indication "
            "(def: 0,\n"
            "                                     units: 100 ns)\n"
            "    --expected=EX|-E EX    set expected expander change "
            "count to EX\n"
            "    --help|-h              print out usage message then exit\n"
            "    --hex|-H               print response in hexadecimal\n"
            "    --inactivity=IN|-i IN    STP bus inactivity time "
            "limit (100 us)\n"
            "    --interface=PARAMS|-I PARAMS   specify or override "
            "interface\n"
            "    --nexus=NE|-n NE       STP SMP I_T nexus loss time "
            "(ms)\n"
            "    --open=OP|-o OP        STP reject to open limit "
            "(10 us)\n"
            "    --power=PD|-p PD       power done timeout (unit: second)\n"
            "    --raw|-r               output response in binary\n"
            "    --reduced=RE|-R RE     initial time to reduced "
            "functionality (100 ms)\n"
            "    --sa=SAS_ADDR|-s SAS_ADDR    SAS address of SMP "
            "target (use leading\n"
            "                                 '0x' or trailing 'h'). "
            "Depending on\n"
            "                                 the interface, may not be "
            "needed\n"
            "    --ssp=CTL|-S CTL       SSP maximum connect time limit "
            "(100 us)\n"
            "    --verbose|-v           increase verbosity\n"
            "    --version|-V           print version string and exit\n\n"
            "Performs a SMP CONFIGURE GENERAL function\n"
           );
}

static void
dStrRaw(const uint8_t * str, int len)
{
    int k;

    for (k = 0 ; k < len; ++k)
        printf("%c", str[k]);
}

int
main(int argc, char * argv[])
{
    bool do_connect = false;
    bool do_inactivity = false;
    bool do_itdefoi = false;
    bool do_nexus = false;
    bool do_open = false;
    bool do_pdt = false;
    bool do_raw = false;
    bool do_reduced = false;
    bool do_ssp = false;
    int res, c, k, len, act_resplen;
    int connect_val = 0;
    int expected_cc = 0;
    int do_hex = 0;
    int inactivity_val = 0;
    int itdefoi_val = 0;
    int nexus_val = 0;
    int open_val = 0;
    int power_val = 0;
    int reduced_val = 0;
    int ret = 0;
    int ssp_ctl = 0;
    int subvalue = 0;
    int verbose = 0;
    int64_t sa_ll;
    uint64_t sa = 0;
    char * cp;
    char i_params[256];
    char device_name[512];
    char b[256];
    unsigned char smp_req[] = {SMP_FRAME_TYPE_REQ, SMP_FN_CONFIG_GENERAL,
                               0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                               0, 0, 0, 0, 0, 0, 0, 0};
    unsigned char smp_resp[8];
    struct smp_req_resp smp_rr;
    struct smp_target_obj tobj;

    memset(device_name, 0, sizeof device_name);
    memset(i_params, 0, sizeof i_params);
    while (1) {
        int option_index = 0;

        c = getopt_long(argc, argv, "c:e:E:hHi:I:n:o:p:rR:s:S:vV",
                        long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'c':
            connect_val = smp_get_num(optarg);
            if ((connect_val < 0) || (connect_val > 65535)) {
                pr2serr("bad argument to '--connect'\n");
                return SMP_LIB_SYNTAX_ERROR;
            }
            do_connect = true;
            break;
        case 'e':
            itdefoi_val = smp_get_num(optarg);
            if ((itdefoi_val < 0) || (itdefoi_val > 255)) {
                pr2serr("bad argument to '--expander'\n");
                return SMP_LIB_SYNTAX_ERROR;
            }
            do_itdefoi = true;
            break;
        case 'E':
            expected_cc = smp_get_num(optarg);
            if ((expected_cc < 0) || (expected_cc > 65535)) {
                pr2serr("bad argument to '--expected'\n");
                return SMP_LIB_SYNTAX_ERROR;
            }
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
        case 'i':
            inactivity_val = smp_get_num(optarg);
            if ((inactivity_val < 0) || (inactivity_val > 65535)) {
                pr2serr("bad argument to '--inactivity', expect value from 0 "
                        "to 65535\n");
                return SMP_LIB_SYNTAX_ERROR;
            }
            do_inactivity = true;
            break;
        case 'n':
            nexus_val = smp_get_num(optarg);
            if ((nexus_val < 0) || (nexus_val > 65535)) {
                pr2serr("bad argument to '--nexus', expect value from 0 to "
                        "65535\n");
                return SMP_LIB_SYNTAX_ERROR;
            }
            do_nexus = true;
            break;
        case 'o':
            open_val = smp_get_num(optarg);
            if ((open_val < 0) || (open_val > 65535)) {
                pr2serr("bad argument to '--open', expect value from 0 to "
                        "65535\n");
                return SMP_LIB_SYNTAX_ERROR;
            }
            do_open = true;
            break;
        case 'p':
            power_val = smp_get_num(optarg);
            if ((power_val < 0) || (power_val > 255)) {
                pr2serr("bad argument to '--power', expect value from 0 to "
                        "255\n");
                return SMP_LIB_SYNTAX_ERROR;
            }
            do_pdt = true;
            break;
        case 'r':
            do_raw = true;
            break;
        case 'R':
            reduced_val = smp_get_num(optarg);
            if ((reduced_val < 0) || (reduced_val > 255)) {
                pr2serr("bad argument to '--reduced', expect value from 0 to "
                        "255\n");
                return SMP_LIB_SYNTAX_ERROR;
            }
            do_reduced = true;
            break;
        case 's':
            sa_ll = smp_get_llnum_nomult(optarg);
            if (-1LL == sa_ll) {
                pr2serr("bad argument to '--sa'\n");
                return SMP_LIB_SYNTAX_ERROR;
            }
            sa = (uint64_t)sa_ll;
            break;
        case 'S':
            ssp_ctl = smp_get_num(optarg);
            if ((ssp_ctl < 0) || (ssp_ctl > 65535)) {
                pr2serr("bad argument to '--ssp', expect value from 0 to "
                        "65535\n");
                return SMP_LIB_SYNTAX_ERROR;
            }
            do_ssp = true;
            break;
        case 'v':
            ++verbose;
            break;
        case 'V':
            pr2serr("version: %s\n", version_str);
            return 0;
        default:
            pr2serr("unrecognised switch code 0x%x ??\n", c);
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
                pr2serr("Unexpected extra argument: %s\n", argv[optind]);
            usage();
            return SMP_LIB_SYNTAX_ERROR;
        }
    }
    if (0 == device_name[0]) {
        cp = getenv("SMP_UTILS_DEVICE");
        if (cp)
            strncpy(device_name, cp, sizeof(device_name) - 1);
        else {
            pr2serr("missing device name on command line\n    [Could use "
                    "environment variable SMP_UTILS_DEVICE instead]\n\n");
            usage();
            return SMP_LIB_SYNTAX_ERROR;
        }
    }
    if ((cp = strchr(device_name, SMP_SUBVALUE_SEPARATOR))) {
        *cp = '\0';
        if (1 != sscanf(cp + 1, "%d", &subvalue)) {
            pr2serr("expected number after separator in SMP_DEVICE name\n");
            return SMP_LIB_SYNTAX_ERROR;
        }
    }
    if (0 == sa) {
        cp = getenv("SMP_UTILS_SAS_ADDR");
        if (cp) {
           sa_ll = smp_get_llnum_nomult(cp);
           if (-1LL == sa_ll) {
                pr2serr("bad value in environment variable "
                        "SMP_UTILS_SAS_ADDR\n    use 0\n");
                sa_ll = 0;
            }
            sa = (uint64_t)sa_ll;
        }
    }
    if (sa > 0) {
        if (! smp_is_sas_naa(sa)) {
            pr2serr("SAS (target) address not in naa-5 nor naa-3 format (may "
                    "need leading '0x')\n");
            if ('\0' == i_params[0]) {
                pr2serr("    use '--interface=' to override\n");
                return SMP_LIB_SYNTAX_ERROR;
            }
        }
    }

    res = smp_initiator_open(device_name, subvalue, i_params, sa,
                             &tobj, verbose);
    if (res < 0)
        return SMP_LIB_FILE_ERROR;

    sg_put_unaligned_be16((uint16_t)expected_cc, smp_req + 4);
    if (do_itdefoi) {
        smp_req[8] |= 0x80;
        smp_req[9] = itdefoi_val;
    }
    if (do_connect) {
        smp_req[8] |= 0x2;
        sg_put_unaligned_be16((uint16_t)connect_val, smp_req + 12);
    }
    if (do_inactivity) {
        smp_req[8] |= 0x1;
        sg_put_unaligned_be16((uint16_t)inactivity_val, smp_req + 10);
    }
    if (do_nexus) {
        smp_req[8] |= 0x4;
        sg_put_unaligned_be16((uint16_t)nexus_val, smp_req + 14);
    }
    if (do_open) {
        smp_req[8] |= 0x10;
        sg_put_unaligned_be16((uint16_t)open_val, smp_req + 18);
    }
    if (do_pdt) {
        smp_req[8] |= 0x20;
        smp_req[17] = power_val & 0xff;
    }
    if (do_reduced) {
        smp_req[8] |= 0x8;
        smp_req[16] = reduced_val & 0xff;
    }
    if (do_ssp) {
        smp_req[8] |= 0x40;
        sg_put_unaligned_be16((uint16_t)ssp_ctl, smp_req + 6);
    }
    if (verbose) {
        pr2serr("    Configure general request: ");
        for (k = 0; k < (int)sizeof(smp_req); ++k) {
            if (0 == (k % 16))
                pr2serr("\n      ");
            else if (0 == (k % 8))
                pr2serr(" ");
            pr2serr("%02x ", smp_req[k]);
        }
        pr2serr("\n");
    }
    memset(&smp_rr, 0, sizeof(smp_rr));
    smp_rr.request_len = sizeof(smp_req);
    smp_rr.request = smp_req;
    smp_rr.max_response_len = sizeof(smp_resp);
    smp_rr.response = smp_resp;
    res = smp_send_req(&tobj, &smp_rr, verbose);

    if (res) {
        pr2serr("smp_send_req failed, res=%d\n", res);
        if (0 == verbose)
            pr2serr("    try adding '-v' option for more debug\n");
        ret = -1;
        goto err_out;
    }
    if (smp_rr.transport_err) {
        pr2serr("smp_send_req transport_error=%d\n", smp_rr.transport_err);
        ret = -1;
        goto err_out;
    }
    act_resplen = smp_rr.act_response_len;
    if ((act_resplen >= 0) && (act_resplen < 4)) {
        pr2serr("response too short, len=%d\n", act_resplen);
        ret = SMP_LIB_CAT_MALFORMED;
        goto err_out;
    }
    len = smp_resp[3];
    if ((0 == len) && (0 == smp_resp[2])) {
        len = smp_get_func_def_resp_len(smp_resp[1]);
        if (len < 0) {
            len = 0;
            if (verbose > 0)
                pr2serr("unable to determine response length\n");
        }
    }
    len = 4 + (len * 4);        /* length in bytes, excluding 4 byte CRC */
    if ((act_resplen >= 0) && (len > act_resplen)) {
        if (verbose)
            pr2serr("actual response length [%d] less than deduced length "
                    "[%d]\n", act_resplen, len);
        len = act_resplen;
    }
    if (do_hex || do_raw) {
        if (do_hex)
            hex2stdout(smp_resp, len, 1);
        else
            dStrRaw(smp_resp, len);
        if (SMP_FRAME_TYPE_RESP != smp_resp[0])
            ret = SMP_LIB_CAT_MALFORMED;
        else if (smp_resp[1] != smp_req[1])
            ret = SMP_LIB_CAT_MALFORMED;
        else if (smp_resp[2]) {
            ret = smp_resp[2];
            if (verbose)
                pr2serr("Configure general result: %s\n",
                        smp_get_func_res_str(smp_resp[2], sizeof(b), b));
        }
        goto err_out;
    }
    if (SMP_FRAME_TYPE_RESP != smp_resp[0]) {
        pr2serr("expected SMP frame response type, got=0x%x\n", smp_resp[0]);
        ret = SMP_LIB_CAT_MALFORMED;
        goto err_out;
    }
    if (smp_resp[1] != smp_req[1]) {
        pr2serr("Expected function code=0x%x, got=0x%x\n", smp_req[1],
                smp_resp[1]);
        ret = SMP_LIB_CAT_MALFORMED;
        goto err_out;
    }
    if (smp_resp[2]) {
        cp = smp_get_func_res_str(smp_resp[2], sizeof(b), b);
        pr2serr("Configure general result: %s\n", cp);
        ret = smp_resp[2];
        goto err_out;
    }
err_out:
    res = smp_initiator_close(&tobj);
    if (res < 0) {
        pr2serr("close error: %s\n", safe_strerror(errno));
        if (0 == ret)
            return SMP_LIB_FILE_ERROR;
    }
    if (ret < 0)
        ret = SMP_LIB_CAT_OTHER;
    if (verbose && ret)
        pr2serr("Exit status %d indicates error detected\n", ret);
    return ret;
}
